/* speechwarp-transcribe - write down what is said in a WAV file, with whisper.cpp.
 *
 * Licensed under the Apache License, Version 2.0. See LICENSE and NOTICE.
 *
 * Reads PCM (8, 16, 24 or 32 bit) or 32-bit float WAV, plain or WAVE_FORMAT_EXTENSIBLE, mixes it to mono and
 * streams it through a session, about 30 seconds at a time, so memory stays flat however long the file is.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "speechwarp_listen.h"

#define CHUNK_FRAMES 4096
#define PROCESS_SECONDS 30

typedef struct {
  FILE* file;
  int format; /* 1 is PCM, 3 is float */
  int channels;
  int sample_rate;
  int bytes_per_sample;
  uint32_t data_bytes; /* left to read */
} wav_reader;

static void usage(FILE* to) {
  fprintf(to,
          "usage: speechwarp-transcribe [options] model.bin input.wav\n"
          "\n"
          "  --language L     the language spoken, e.g. en or de (default: detect it)\n"
          "  --words          list each word with its times and probability under its line\n"
          "  --srt            write SubRip subtitles instead of text\n"
          "  --preset P       fast, balanced (the default) or accurate\n"
          "  --prompt TEXT    words likely to appear, such as names\n"
          "  --threads N      CPU threads (default: the number of cores, at most 8)\n"
          "  --gpu            use the GPU, if the library was built with one\n"
          "  --once           transcribe the whole file in one call instead of streaming it\n"
          "\n"
          "  -v, --verbose    show whisper.cpp's log and the progress\n"
          "  -V, --version    print the versions\n"
          "  -h, --help       print this\n"
          "\n"
          "Models: %d in the catalogue, e.g. %s from %s\n",
          speechwarp_listen_catalogue_count(), speechwarp_listen_catalogue_file_name(0),
          speechwarp_listen_catalogue_url(0));
}

static uint32_t read_u32(const unsigned char* p) {
  return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static unsigned read_u16(const unsigned char* p) { return p[0] | (unsigned)p[1] << 8; }

static int skip(FILE* file, uint32_t bytes) {
  char buffer[512];
  while (bytes > 0) {
    size_t n = bytes < sizeof buffer ? bytes : sizeof buffer;
    if (fread(buffer, 1, n, file) != n) return 0;
    bytes -= (uint32_t)n;
  }
  return 1;
}

/* Read up to the start of the samples. Returns an error message, or NULL on success. */
static const char* open_wav(wav_reader* wav, const char* path) {
  unsigned char header[12], chunk[8], format[40];
  int have_format = 0;

  wav->file = fopen(path, "rb");
  if (!wav->file) return "cannot open it";
  if (fread(header, 1, 12, wav->file) != 12 || memcmp(header, "RIFF", 4) != 0 ||
      memcmp(header + 8, "WAVE", 4) != 0) {
    return "it is not a WAV file";
  }
  for (;;) {
    uint32_t size;
    if (fread(chunk, 1, 8, wav->file) != 8) return "it has no audio data";
    size = read_u32(chunk + 4);
    if (memcmp(chunk, "fmt ", 4) == 0) {
      uint32_t used = size < sizeof format ? size : (uint32_t)sizeof format;
      if (size < 16 || fread(format, 1, used, wav->file) != used) return "its format chunk is cut short";
      if (!skip(wav->file, size - used + (size & 1))) return "its format chunk is cut short";
      wav->format = (int)read_u16(format);
      wav->channels = (int)read_u16(format + 2);
      wav->sample_rate = (int)read_u32(format + 4);
      wav->bytes_per_sample = (int)read_u16(format + 14) / 8;
      if (wav->format == 0xFFFE && size >= 26) {
        wav->format = (int)read_u16(format + 24); /* WAVE_FORMAT_EXTENSIBLE: the real format is here */
      }
      have_format = 1;
    } else if (memcmp(chunk, "data", 4) == 0) {
      if (!have_format) return "its audio data comes before its format";
      wav->data_bytes = size;
      break;
    } else if (!skip(wav->file, size + (size & 1))) {
      return "it has no audio data";
    }
  }
  if (wav->channels < 1 || wav->channels > 32) return "its channel count is not supported";
  if (wav->format == 1) {
    if (wav->bytes_per_sample < 1 || wav->bytes_per_sample > 4) return "its sample size is not supported";
  } else if (wav->format == 3) {
    if (wav->bytes_per_sample != 4) return "only 32-bit float samples are supported";
  } else {
    return "it is compressed; only PCM and float WAV files are supported";
  }
  return NULL;
}

/* Read up to `max_frames` frames, mixed to mono. Returns the number read. */
static int read_mono(wav_reader* wav, float* out, int max_frames) {
  static unsigned char raw[CHUNK_FRAMES * 32 * 4];
  size_t frame_bytes = (size_t)wav->bytes_per_sample * wav->channels;
  size_t wanted = (size_t)max_frames * frame_bytes;
  int frames, f, c;

  if (wanted > wav->data_bytes) wanted = wav->data_bytes - wav->data_bytes % frame_bytes;
  frames = (int)(fread(raw, 1, wanted, wav->file) / frame_bytes);
  wav->data_bytes -= (uint32_t)(frames * frame_bytes);
  for (f = 0; f < frames; f++) {
    float sum = 0;
    for (c = 0; c < wav->channels; c++) {
      const unsigned char* p = raw + ((size_t)f * wav->channels + c) * wav->bytes_per_sample;
      float value;
      if (wav->format == 3) {
        uint32_t bits = read_u32(p);
        memcpy(&value, &bits, sizeof value);
      } else if (wav->bytes_per_sample == 1) {
        value = (p[0] - 128) / 128.0f; /* 8-bit WAV is unsigned */
      } else if (wav->bytes_per_sample == 2) {
        value = (int16_t)read_u16(p) / 32768.0f;
      } else if (wav->bytes_per_sample == 3) {
        value = (int32_t)((uint32_t)p[0] << 8 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 24) / 2147483648.0f;
      } else {
        value = (int32_t)read_u32(p) / 2147483648.0f;
      }
      sum += value;
    }
    out[f] = sum / wav->channels;
  }
  return frames;
}

/* 00:01:02,345 for SubRip, 00:01:02.345 otherwise. */
static void print_time(double seconds, char separator) {
  long ms = (long)floor(seconds * 1000 + 0.5);
  printf("%02ld:%02ld:%02ld%c%03ld", ms / 3600000, ms / 60000 % 60, ms / 1000 % 60, separator, ms % 1000);
}

/* Print a result's segments; `cue` numbers SubRip cues across calls. */
static void print_result(const speechwarp_listen_result* result, int srt, int words, int* cue) {
  int s, w, count = speechwarp_listen_result_segment_count(result);
  for (s = 0; s < count; s++) {
    double start = speechwarp_listen_result_segment_start(result, s);
    double end = speechwarp_listen_result_segment_end(result, s);
    if (srt) {
      printf("%d\n", ++*cue);
      print_time(start, ',');
      printf(" --> ");
      print_time(end, ',');
      printf("\n%s\n\n", speechwarp_listen_result_segment_text(result, s));
      continue;
    }
    if (words) {
      printf("[");
      print_time(start, '.');
      printf(" --> ");
      print_time(end, '.');
      printf("] ");
    }
    printf("%s\n", speechwarp_listen_result_segment_text(result, s));
    if (words) {
      for (w = 0; w < speechwarp_listen_result_word_count(result, s); w++) {
        printf("    %8.2f %8.2f  %.2f  %s\n", speechwarp_listen_result_word_start(result, s, w),
               speechwarp_listen_result_word_end(result, s, w),
               speechwarp_listen_result_word_probability(result, s, w),
               speechwarp_listen_result_word_text(result, s, w));
      }
    }
  }
  fflush(stdout);
}

/* Print and free what the session has finished. */
static int take(speechwarp_listen_session* session, int srt, int words, int* cue) {
  speechwarp_listen_result* result = speechwarp_listen_session_take(session);
  if (!result) return 0;
  print_result(result, srt, words, cue);
  speechwarp_listen_result_free(result);
  return 1;
}

int main(int argc, char** argv) {
  static float in[CHUNK_FRAMES];
  const char* paths[2];
  const char *language = NULL, *prompt = NULL, *preset_name = "balanced", *error;
  int path_count = 0, verbose = 0, srt = 0, words = 0, once = 0, threads = 0, flags = 0, preset, status = 0;
  int cue = 0, frames, i;
  wav_reader wav;
  speechwarp_listen_model* model;
  speechwarp_listen_options* options;

  for (i = 1; i < argc; i++) {
    const char* arg = argv[i];
    int has_value = i + 1 < argc;
    if (!strcmp(arg, "-h") || !strcmp(arg, "--help")) {
      usage(stdout);
      return 0;
    } else if (!strcmp(arg, "-V") || !strcmp(arg, "--version")) {
      printf("speechwarp-transcribe %s (whisper.cpp %s)\n", speechwarp_listen_version(),
             speechwarp_listen_engine_version());
      return 0;
    } else if (!strcmp(arg, "-v") || !strcmp(arg, "--verbose")) {
      verbose = 1;
    } else if (!strcmp(arg, "--words")) {
      words = 1;
    } else if (!strcmp(arg, "--srt")) {
      srt = 1;
    } else if (!strcmp(arg, "--once")) {
      once = 1;
    } else if (!strcmp(arg, "--gpu")) {
      flags |= SPEECHWARP_LISTEN_LOAD_GPU;
    } else if (!strcmp(arg, "--language") && has_value) {
      language = argv[++i];
    } else if (!strcmp(arg, "--prompt") && has_value) {
      prompt = argv[++i];
    } else if (!strcmp(arg, "--preset") && has_value) {
      preset_name = argv[++i];
    } else if (!strcmp(arg, "--threads") && has_value) {
      threads = atoi(argv[++i]);
    } else if (arg[0] == '-' && arg[1] != '\0') {
      fprintf(stderr, "speechwarp-transcribe: unknown or incomplete option %s\n\n", arg);
      usage(stderr);
      return 2;
    } else if (path_count < 2) {
      paths[path_count++] = arg;
    } else {
      path_count = 3;
    }
  }
  if (path_count != 2) {
    usage(stderr);
    return 2;
  }
  if (!strcmp(preset_name, "fast")) {
    preset = SPEECHWARP_LISTEN_PRESET_FAST;
  } else if (!strcmp(preset_name, "balanced")) {
    preset = SPEECHWARP_LISTEN_PRESET_BALANCED;
  } else if (!strcmp(preset_name, "accurate")) {
    preset = SPEECHWARP_LISTEN_PRESET_ACCURATE;
  } else {
    fprintf(stderr, "speechwarp-transcribe: --preset must be fast, balanced or accurate\n");
    return 2;
  }

  memset(&wav, 0, sizeof wav);
  error = open_wav(&wav, paths[1]);
  if (error) {
    fprintf(stderr, "speechwarp-transcribe: %s: %s\n", paths[1], error);
    return 1;
  }

  speechwarp_listen_set_log(verbose);
  options = speechwarp_listen_options_create();
  if (!options) return 1;
  if (speechwarp_listen_options_set_language(options, language) != SPEECHWARP_LISTEN_OK) {
    fprintf(stderr, "speechwarp-transcribe: whisper does not know the language %s\n", language);
    return 2;
  }
  speechwarp_listen_options_set_word_timestamps(options, words);
  speechwarp_listen_options_set_prompt(options, prompt);
  speechwarp_listen_options_set_preset(options, preset);

  model = speechwarp_listen_model_load(paths[0], threads, flags);
  if (!model) {
    fprintf(stderr, "speechwarp-transcribe: %s: cannot load it as a whisper model\n", paths[0]);
    return 1;
  }

  if (once) {
    /* The whole file in memory, one call. */
    float* all = NULL;
    int64_t count = 0, capacity = 0;
    speechwarp_listen_result* result;
    while ((frames = read_mono(&wav, in, CHUNK_FRAMES)) > 0) {
      if (count + frames > capacity) {
        float* grown;
        capacity = capacity ? capacity * 2 : (int64_t)wav.sample_rate * 60;
        grown = (float*)realloc(all, (size_t)capacity * sizeof(float));
        if (!grown) {
          fprintf(stderr, "speechwarp-transcribe: out of memory\n");
          return 1;
        }
        all = grown;
      }
      memcpy(all + count, in, (size_t)frames * sizeof(float));
      count += frames;
    }
    result = speechwarp_listen_transcribe(model, all, count, wav.sample_rate, options, &status);
    free(all);
    if (result) {
      print_result(result, srt, words, &cue);
      speechwarp_listen_result_free(result);
    }
  } else {
    speechwarp_listen_session* session = speechwarp_listen_session_create(model, wav.sample_rate, options);
    int64_t since_process = 0;
    if (!session) {
      fprintf(stderr, "speechwarp-transcribe: %s: %d Hz is not supported\n", paths[1], wav.sample_rate);
      return 1;
    }
    while (status >= 0 && (frames = read_mono(&wav, in, CHUNK_FRAMES)) > 0) {
      status = speechwarp_listen_session_write(session, in, frames);
      since_process += frames;
      if (status >= 0 && since_process >= (int64_t)wav.sample_rate * PROCESS_SECONDS) {
        since_process = 0;
        status = speechwarp_listen_session_process(session);
        if (status > 0) take(session, srt, words, &cue);
        if (verbose) {
          fprintf(stderr, "speechwarp-transcribe: %.0f s of %.0f s recognised\n",
                  speechwarp_listen_session_seconds_recognised(session),
                  speechwarp_listen_session_seconds_written(session));
        }
      }
    }
    if (status >= 0) status = speechwarp_listen_session_finish(session);
    take(session, srt, words, &cue);
    speechwarp_listen_session_free(session);
  }

  speechwarp_listen_model_free(model);
  speechwarp_listen_options_free(options);
  fclose(wav.file);
  if (status < 0) {
    fprintf(stderr, "speechwarp-transcribe: %s\n", speechwarp_listen_error_message(status));
    return 1;
  }
  return 0;
}
