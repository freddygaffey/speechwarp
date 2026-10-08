/* speechwarp - speed up a WAV file of speech.
 *
 * Licensed under the Apache License, Version 2.0. See LICENSE and NOTICE.
 *
 * Reads PCM (8, 16, 24 or 32 bit) or 32-bit float WAV, plain or WAVE_FORMAT_EXTENSIBLE, and writes 16-bit
 * PCM.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "speechwarp.h"

#define CHUNK_FRAMES 4096

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
          "usage: speechwarp [options] input.wav output.wav\n"
          "\n"
          "  -s, --speed N      how many times faster, %g to %g (default 2)\n"
          "  -l, --linear       speed everything up evenly (the same as --nonlinear 0)\n"
          "  -n, --nonlinear A  how unevenly to speed up, 0 to 1 (default 1)\n"
          "\n"
          "  for very high speeds (all off by default):\n"
          "  --pause-cap S      shorten every pause to at most S seconds, e.g. 0.06\n"
          "  --floor F          no speech slower than F times the speed, 0 to 1, e.g. 0.5\n"
          "  --heard-pause S[@FROM]\n"
          "                     keep each pause about S seconds long as heard, e.g. 0.03, from speed FROM\n"
          "                     up (default 3); the pause cap is then S times the speed. Replaces --pause-cap\n"
          "  --floor-blend F@FROM-FULL\n"
          "                     the speed floor rises from 0 at speed FROM to F at speed FULL, e.g. 0.5@4-6\n"
          "                     (FULL is optional: F@FROM steps at FROM). Replaces --floor\n"
          "  --rhythm-gap S     put an S-second silence in the output at a regular rate, e.g. 0.04\n"
          "  --rhythm-rate N    gaps a second (default 5)\n"
          "  --no-keep-speed    let trimmed pauses and gaps change the overall speed\n"
          "\n"
          "  -v, --verbose      report the lengths, the speed achieved and the syllables a second\n"
          "  -V, --version      print the version\n"
          "  -h, --help         print this\n",
          (double)SPEECHWARP_MIN_SPEED, (double)SPEECHWARP_MAX_SPEED);
}

static uint32_t read_u32(const unsigned char* p) {
  return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static unsigned read_u16(const unsigned char* p) { return p[0] | (unsigned)p[1] << 8; }

static void write_u32(unsigned char* p, uint32_t value) {
  p[0] = (unsigned char)value;
  p[1] = (unsigned char)(value >> 8);
  p[2] = (unsigned char)(value >> 16);
  p[3] = (unsigned char)(value >> 24);
}

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
  if (wav->format == 1) {
    if (wav->bytes_per_sample < 1 || wav->bytes_per_sample > 4) return "its sample size is not supported";
  } else if (wav->format == 3) {
    if (wav->bytes_per_sample != 4) return "only 32-bit float samples are supported";
  } else {
    return "it is compressed; only PCM and float WAV files are supported";
  }
  return NULL;
}

/* Read up to `max_frames` frames as floats. Returns the number read. */
static int read_frames(wav_reader* wav, float* out, int max_frames) {
  static unsigned char raw[CHUNK_FRAMES * 32 * 4];
  size_t frame_bytes = (size_t)wav->bytes_per_sample * wav->channels;
  size_t wanted = (size_t)max_frames * frame_bytes;
  int frames, count, i;

  if (wanted > wav->data_bytes) wanted = wav->data_bytes - wav->data_bytes % frame_bytes;
  frames = (int)(fread(raw, 1, wanted, wav->file) / frame_bytes);
  wav->data_bytes -= (uint32_t)(frames * frame_bytes);
  count = frames * wav->channels;
  for (i = 0; i < count; i++) {
    const unsigned char* p = raw + (size_t)i * wav->bytes_per_sample;
    if (wav->format == 3) {
      float value;
      uint32_t bits = read_u32(p);
      memcpy(&value, &bits, sizeof value);
      out[i] = value;
    } else if (wav->bytes_per_sample == 1) {
      out[i] = (p[0] - 128) / 128.0f; /* 8-bit WAV is unsigned */
    } else if (wav->bytes_per_sample == 2) {
      out[i] = (int16_t)read_u16(p) / 32768.0f;
    } else if (wav->bytes_per_sample == 3) {
      out[i] = (int32_t)((uint32_t)p[0] << 8 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 24) / 2147483648.0f;
    } else {
      out[i] = (int32_t)read_u32(p) / 2147483648.0f;
    }
  }
  return frames;
}

static void write_header(FILE* file, int sample_rate, int channels, uint32_t data_bytes) {
  unsigned char header[44];
  memcpy(header, "RIFF", 4);
  write_u32(header + 4, 36 + data_bytes);
  memcpy(header + 8, "WAVEfmt ", 8);
  write_u32(header + 16, 16);
  header[20] = 1; /* PCM */
  header[21] = 0;
  header[22] = (unsigned char)channels;
  header[23] = 0;
  write_u32(header + 24, (uint32_t)sample_rate);
  write_u32(header + 28, (uint32_t)sample_rate * channels * 2);
  header[32] = (unsigned char)(channels * 2);
  header[33] = 0;
  header[34] = 16;
  header[35] = 0;
  memcpy(header + 36, "data", 4);
  write_u32(header + 40, data_bytes);
  fwrite(header, 1, sizeof header, file);
}

/* Write whatever output is ready. Returns the frames written, or -1 if the disk is full. */
static long drain(speechwarp_stream* stream, FILE* file, int channels) {
  static int16_t out[CHUNK_FRAMES * 32];
  static unsigned char raw[CHUNK_FRAMES * 32 * 2];
  long total = 0;
  int frames, i;

  while ((frames = speechwarp_read_i16(stream, out, CHUNK_FRAMES)) > 0) {
    size_t bytes = (size_t)frames * channels * 2;
    for (i = 0; i < frames * channels; i++) {
      raw[2 * i] = (unsigned char)out[i];
      raw[2 * i + 1] = (unsigned char)((uint16_t)out[i] >> 8);
    }
    if (fwrite(raw, 1, bytes, file) != bytes) return -1;
    total += frames;
  }
  return total;
}

int main(int argc, char** argv) {
  static float in[CHUNK_FRAMES * 32];
  const char* paths[2];
  const char* error;
  double speed = 2, nonlinear = 1, pause_cap = 0, speed_floor = 0, rhythm_gap = 0, rhythm_rate = 5;
  double heard_pause = 0, heard_from = 3, blend = 0, blend_from = 1, blend_full = 1;
  int keep_speed = 1;
  int path_count = 0, verbose = 0, i, frames;
  long frames_in = 0, frames_out = 0, written;
  speechwarp_stream* stream;
  wav_reader wav;
  FILE* out;

  for (i = 1; i < argc; i++) {
    const char* arg = argv[i];
    int has_value = i + 1 < argc;
    if (!strcmp(arg, "-h") || !strcmp(arg, "--help")) {
      usage(stdout);
      return 0;
    } else if (!strcmp(arg, "-V") || !strcmp(arg, "--version")) {
      printf("speechwarp %s\n", speechwarp_version());
      return 0;
    } else if (!strcmp(arg, "-v") || !strcmp(arg, "--verbose")) {
      verbose = 1;
    } else if (!strcmp(arg, "-l") || !strcmp(arg, "--linear")) {
      nonlinear = 0;
    } else if ((!strcmp(arg, "-s") || !strcmp(arg, "--speed")) && has_value) {
      speed = atof(argv[++i]);
    } else if ((!strcmp(arg, "-n") || !strcmp(arg, "--nonlinear")) && has_value) {
      nonlinear = atof(argv[++i]);
    } else if (!strcmp(arg, "--pause-cap") && has_value) {
      pause_cap = atof(argv[++i]);
    } else if (!strcmp(arg, "--floor") && has_value) {
      speed_floor = atof(argv[++i]);
    } else if (!strcmp(arg, "--heard-pause") && has_value) {
      heard_from = 3;
      if (sscanf(argv[++i], "%lf@%lf", &heard_pause, &heard_from) < 1) {
        heard_pause = -1;
      }
    } else if (!strcmp(arg, "--floor-blend") && has_value) {
      int parts = sscanf(argv[++i], "%lf@%lf-%lf", &blend, &blend_from, &blend_full);
      if (parts < 2) {
        blend = -1;
      } else if (parts == 2) {
        blend_full = blend_from;
      }
    } else if (!strcmp(arg, "--rhythm-gap") && has_value) {
      rhythm_gap = atof(argv[++i]);
    } else if (!strcmp(arg, "--rhythm-rate") && has_value) {
      rhythm_rate = atof(argv[++i]);
    } else if (!strcmp(arg, "--no-keep-speed")) {
      keep_speed = 0;
    } else if (arg[0] == '-' && arg[1] != '\0') {
      fprintf(stderr, "speechwarp: unknown or incomplete option %s\n\n", arg);
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
  if (!(speed >= SPEECHWARP_MIN_SPEED && speed <= SPEECHWARP_MAX_SPEED)) {
    fprintf(stderr, "speechwarp: the speed must be from %g to %g\n", (double)SPEECHWARP_MIN_SPEED,
            (double)SPEECHWARP_MAX_SPEED);
    return 2;
  }
  if (!(nonlinear >= 0 && nonlinear <= 1)) {
    fprintf(stderr, "speechwarp: --nonlinear must be from 0 to 1\n");
    return 2;
  }

  if (!(pause_cap >= 0 && pause_cap <= 1) || !(speed_floor >= 0 && speed_floor <= 1) ||
      !(rhythm_gap >= 0 && rhythm_gap <= 0.2) || !(rhythm_rate >= 1 && rhythm_rate <= 16)) {
    fprintf(stderr, "speechwarp: --pause-cap must be 0 to 1, --floor 0 to 1, --rhythm-gap 0 to 0.2 and "
                    "--rhythm-rate 1 to 16\n");
    return 2;
  }

  if (!(heard_pause >= 0 && heard_pause <= 0.4 && heard_from >= 1 && heard_from <= SPEECHWARP_MAX_SPEED) ||
      !(blend >= 0 && blend <= 1 && blend_from >= 1 && blend_from <= SPEECHWARP_MAX_SPEED && blend_full >= 1 &&
        blend_full <= SPEECHWARP_MAX_SPEED)) {
    fprintf(stderr, "speechwarp: --heard-pause must be S[@FROM] with S 0 to 0.4 and FROM 1 to %g, and "
                    "--floor-blend F@FROM-FULL with F 0 to 1 and the speeds 1 to %g\n",
            (double)SPEECHWARP_MAX_SPEED, (double)SPEECHWARP_MAX_SPEED);
    return 2;
  }

  memset(&wav, 0, sizeof wav);
  error = open_wav(&wav, paths[0]);
  if (error) {
    fprintf(stderr, "speechwarp: %s: %s\n", paths[0], error);
    return 1;
  }
  stream = speechwarp_create(wav.sample_rate, wav.channels);
  if (!stream) {
    fprintf(stderr, "speechwarp: %s: %d Hz with %d channels is not supported\n", paths[0], wav.sample_rate,
            wav.channels);
    return 1;
  }
  speechwarp_set_speed(stream, (float)speed);
  speechwarp_set_nonlinear(stream, (float)nonlinear);
  speechwarp_set_pause_cap(stream, (float)pause_cap);
  speechwarp_set_speed_floor(stream, (float)speed_floor);
  if (heard_pause > 0) {
    speechwarp_set_heard_pause(stream, (float)heard_pause, (float)heard_from);
  }
  if (blend > 0) {
    speechwarp_set_floor_blend(stream, (float)blend, (float)blend_from, (float)blend_full);
  }
  speechwarp_set_rhythm_gap(stream, (float)rhythm_gap);
  speechwarp_set_rhythm_rate(stream, (float)rhythm_rate);
  speechwarp_set_keep_speed(stream, keep_speed);

  out = fopen(paths[1], "wb");
  if (!out) {
    fprintf(stderr, "speechwarp: %s: cannot create it\n", paths[1]);
    return 1;
  }
  write_header(out, wav.sample_rate, wav.channels, 0);

  for (;;) {
    frames = read_frames(&wav, in, CHUNK_FRAMES);
    if (frames > 0) {
      frames_in += frames;
      if (!speechwarp_write(stream, in, frames)) break;
    } else if (!speechwarp_flush(stream)) {
      break;
    }
    written = drain(stream, out, wav.channels);
    if (written < 0) break;
    frames_out += written;
    if (frames == 0) {
      /* Now the length is known, go back and fill it in. */
      uint32_t data_bytes = (uint32_t)((uint64_t)frames_out * wav.channels * 2);
      if (fseek(out, 0, SEEK_SET) != 0) break;
      write_header(out, wav.sample_rate, wav.channels, data_bytes);
      if (fclose(out) != 0) {
        out = NULL;
        break;
      }
      if (verbose) {
        double achieved = frames_out ? (double)frames_in / frames_out : 0.0;
        double syllables = speechwarp_syllable_rate(stream);
        fprintf(stderr, "%.2f s in, %.2f s out, %.2fx", (double)frames_in / wav.sample_rate,
                (double)frames_out / wav.sample_rate, achieved);
        if (syllables >= 0) {
          fprintf(stderr, ", %.1f syllables a second in, %.1f heard", syllables, syllables * achieved);
        }
        fprintf(stderr, "\n");
      }
      speechwarp_destroy(stream);
      fclose(wav.file);
      return 0;
    }
  }
  fprintf(stderr, "speechwarp: %s: could not write it (out of memory or disk space)\n", paths[1]);
  return 1;
}
