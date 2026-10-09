// Recognising real speech with a real model. Skips (exit 77) unless SPEECHWARP_LISTEN_MODEL names a model
// and the speech made by make_speech.cmake exists.
//
// usage: test_listen <speech directory> <passage.txt>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "speechwarp_listen.h"

static int failures = 0;

#define CHECK(condition)                                                     \
  do {                                                                       \
    if (!(condition)) {                                                      \
      std::fprintf(stderr, "%s:%d: failed: %s\n", __FILE__, __LINE__, #condition); \
      failures++;                                                            \
    }                                                                        \
  } while (0)

struct Audio {
  std::vector<float> samples;
  int rate = 0;
  double seconds() const { return rate ? double(samples.size()) / rate : 0; }
};

// Mono 16-bit PCM or 32-bit float WAV, as `say` writes it.
static bool read_wav(const std::string& path, Audio& audio) {
  std::ifstream file(path, std::ios::binary);
  if (!file) return false;
  std::vector<unsigned char> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  auto u32 = [&](size_t at) {
    return uint32_t(data[at]) | uint32_t(data[at + 1]) << 8 | uint32_t(data[at + 2]) << 16 | uint32_t(data[at + 3]) << 24;
  };
  auto u16 = [&](size_t at) { return unsigned(data[at]) | unsigned(data[at + 1]) << 8; };
  if (data.size() < 12 || std::memcmp(data.data(), "RIFF", 4) != 0) return false;
  int format = 0, bits = 0, channels = 0;
  for (size_t at = 12; at + 8 <= data.size();) {
    uint32_t size = u32(at + 4);
    if (std::memcmp(&data[at], "fmt ", 4) == 0) {
      format = int(u16(at + 8));
      channels = int(u16(at + 10));
      audio.rate = int(u32(at + 12));
      bits = int(u16(at + 22));
    } else if (std::memcmp(&data[at], "data", 4) == 0) {
      if (channels != 1) return false;
      size_t n = std::min<size_t>(size, data.size() - at - 8) / size_t(bits / 8);
      audio.samples.resize(n);
      for (size_t i = 0; i < n; i++) {
        size_t p = at + 8 + i * size_t(bits / 8);
        if (format == 3 && bits == 32) {
          uint32_t raw = u32(p);
          std::memcpy(&audio.samples[i], &raw, 4);
        } else if (format == 1 && bits == 16) {
          audio.samples[i] = float(int16_t(u16(p))) / 32768.0f;
        } else {
          return false;
        }
      }
      return true;
    }
    at += 8 + size + (size & 1);
  }
  return false;
}

// Lower-case words without punctuation, as the trainer's scoring compares them.
static std::vector<std::string> words_of(const std::string& text) {
  std::vector<std::string> out;
  std::string word;
  for (char c : text) {
    unsigned char u = static_cast<unsigned char>(c);
    if (std::isalnum(u) || c == '\'') {
      word += char(std::tolower(u));
    } else if (!word.empty()) {
      out.push_back(word);
      word.clear();
    }
  }
  if (!word.empty()) out.push_back(word);
  return out;
}

// Word error rate of `hypothesis` against `reference`: edits over reference length.
static double word_error_rate(const std::vector<std::string>& reference, const std::vector<std::string>& hypothesis) {
  std::vector<size_t> row(hypothesis.size() + 1), next(hypothesis.size() + 1);
  for (size_t j = 0; j <= hypothesis.size(); j++) row[j] = j;
  for (size_t i = 1; i <= reference.size(); i++) {
    next[0] = i;
    for (size_t j = 1; j <= hypothesis.size(); j++) {
      size_t substitute = row[j - 1] + (reference[i - 1] == hypothesis[j - 1] ? 0 : 1);
      next[j] = std::min({substitute, row[j] + 1, next[j - 1] + 1});
    }
    row.swap(next);
  }
  return reference.empty() ? 0 : double(row[hypothesis.size()]) / double(reference.size());
}

// Fraction of the reference's distinct words that appear anywhere in the hypothesis.
static double words_found(const std::vector<std::string>& reference, const std::vector<std::string>& hypothesis) {
  std::set<std::string> heard(hypothesis.begin(), hypothesis.end()), wanted(reference.begin(), reference.end());
  size_t found = 0;
  for (const std::string& w : wanted) found += heard.count(w);
  return wanted.empty() ? 1 : double(found) / double(wanted.size());
}

// Times in order and within bounds, words inside their segments, probabilities 0 to 1.
static void check_times(const speechwarp_listen_result* result, double length, bool words) {
  double previous = 0;
  int segments = speechwarp_listen_result_segment_count(result);
  for (int s = 0; s < segments; s++) {
    double start = speechwarp_listen_result_segment_start(result, s);
    double end = speechwarp_listen_result_segment_end(result, s);
    CHECK(start >= previous && start <= end && end <= length + 0.01);
    previous = start;
    int count = speechwarp_listen_result_word_count(result, s);
    if (words) CHECK(count > 0);
    double word_previous = start;
    for (int w = 0; w < count; w++) {
      double ws = speechwarp_listen_result_word_start(result, s, w);
      double we = speechwarp_listen_result_word_end(result, s, w);
      double p = speechwarp_listen_result_word_probability(result, s, w);
      CHECK(ws >= word_previous && ws <= we && we <= end + 1e-9);
      CHECK(p >= 0 && p <= 1);
      CHECK(std::strlen(speechwarp_listen_result_word_text(result, s, w)) > 0);
      word_previous = ws;
    }
  }
}

static std::string read_text(const std::string& path) {
  std::ifstream file(path);
  std::stringstream text;
  text << file.rdbuf();
  return text.str();
}

static double now() {
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

int main(int argc, char** argv) {
  const char* model_path = std::getenv("SPEECHWARP_LISTEN_MODEL");
  if (argc < 3) return 2;
  if (!model_path || !*model_path) {
    std::printf("SPEECHWARP_LISTEN_MODEL is not set: skipped\n");
    return 77;
  }
  Audio sentence, passage, ending;
  if (!read_wav(std::string(argv[1]) + "/sentence.wav", sentence) ||
      !read_wav(std::string(argv[1]) + "/passage.wav", passage) ||
      !read_wav(std::string(argv[1]) + "/ending.wav", ending) || ending.rate != passage.rate) {
    std::printf("no test speech (it is made with macOS's say): skipped\n");
    return 77;
  }
  // One recording of nearly three minutes: the passage in one voice, a pause, its ending again in another.
  const std::string text = read_text(argv[2]);
  passage.samples.resize(passage.samples.size() + size_t(passage.rate) * 2, 0.0f);
  passage.samples.insert(passage.samples.end(), ending.samples.begin(), ending.samples.end());
  const std::vector<std::string> passage_words = words_of(text + " " + text.substr(text.find("He set the radio")));
  const std::vector<std::string> sentence_words =
      words_of("The quick brown fox jumps over the lazy dog, and the farmer laughs.");

  double t = now();
  // SPEECHWARP_LISTEN_THREADS=1 keeps ggml's own threads out of a thread sanitiser's way.
  const char* threads = std::getenv("SPEECHWARP_LISTEN_THREADS");
  speechwarp_listen_model* model = speechwarp_listen_model_load(model_path, threads ? std::atoi(threads) : 0, 0);
  if (!model) {
    std::fprintf(stderr, "cannot load %s\n", model_path);
    return 1;
  }
  std::printf("model %s (%s, %s) loaded in %.2f s; whisper.cpp %s\n", model_path,
              speechwarp_listen_model_type(model), speechwarp_listen_model_multilingual(model) ? "multilingual" : "English",
              now() - t, speechwarp_listen_engine_version());

  // ---- A sentence said back: one call, at 22.05 kHz.
  {
    speechwarp_listen_options* options = speechwarp_listen_options_create();
    speechwarp_listen_options_set_language(options, "en-GB");
    int error = -99;
    t = now();
    speechwarp_listen_result* result = speechwarp_listen_transcribe(model, sentence.samples.data(),
                                                                    int64_t(sentence.samples.size()), sentence.rate,
                                                                    options, &error);
    CHECK(result && error == SPEECHWARP_LISTEN_OK);
    if (result) {
      std::vector<std::string> heard = words_of(speechwarp_listen_result_text(result));
      std::printf("sentence (%.1f s audio, %.2f s): \"%s\" [%s], %.0f%% of words found, WER %.2f\n",
                  sentence.seconds(), now() - t, speechwarp_listen_result_text(result),
                  speechwarp_listen_result_language(result), 100 * words_found(sentence_words, heard),
                  word_error_rate(sentence_words, heard));
      CHECK(words_found(sentence_words, heard) >= 0.8);
      CHECK(std::strcmp(speechwarp_listen_result_language(result), "en") == 0);
      CHECK(speechwarp_listen_result_segment_count(result) >= 1);
      check_times(result, sentence.seconds(), true);
      // Out of range.
      CHECK(speechwarp_listen_result_segment_text(result, 99) == nullptr);
      CHECK(std::isnan(speechwarp_listen_result_word_start(result, 0, 999)));
      speechwarp_listen_result_free(result);
    }

    // Without word times, and with the accurate preset and a prompt.
    speechwarp_listen_options_set_word_timestamps(options, 0);
    speechwarp_listen_options_set_preset(options, SPEECHWARP_LISTEN_PRESET_ACCURATE);
    speechwarp_listen_options_set_prompt(options, "Fox, farmer.");
    result = speechwarp_listen_transcribe(model, sentence.samples.data(), int64_t(sentence.samples.size()),
                                          sentence.rate, options, &error);
    CHECK(result != nullptr);
    if (result) {
      CHECK(speechwarp_listen_result_word_count(result, 0) == 0);
      CHECK(words_found(sentence_words, words_of(speechwarp_listen_result_text(result))) >= 0.8);
      speechwarp_listen_result_free(result);
    }

    // Cancelled options stop at once and stay cancelled.
    speechwarp_listen_options_cancel(options);
    result = speechwarp_listen_transcribe(model, sentence.samples.data(), int64_t(sentence.samples.size()),
                                          sentence.rate, options, &error);
    CHECK(result == nullptr && error == SPEECHWARP_LISTEN_ERROR_CANCELLED);
    speechwarp_listen_options_free(options);
  }

  // ---- Silence and very short input give no error.
  {
    std::vector<float> silence(16000 * 3, 0.0f);
    int error = -99;
    speechwarp_listen_result* result =
        speechwarp_listen_transcribe(model, silence.data(), int64_t(silence.size()), 16000, nullptr, &error);
    CHECK(result && error == SPEECHWARP_LISTEN_OK);
    std::printf("silence: %d segments \"%s\"\n", speechwarp_listen_result_segment_count(result),
                result ? speechwarp_listen_result_text(result) : "");
    speechwarp_listen_result_free(result);
    result = speechwarp_listen_transcribe(model, silence.data(), 10, 48000, nullptr, &error);
    CHECK(result && error == SPEECHWARP_LISTEN_OK);
    speechwarp_listen_result_free(result);
    result = speechwarp_listen_transcribe(model, nullptr, 0, 16000, nullptr, &error);
    CHECK(result && speechwarp_listen_result_segment_count(result) == 0);
    speechwarp_listen_result_free(result);
  }

  // ---- The passage in one call, for reference.
  std::vector<std::string> once_words;
  speechwarp_listen_options* options = speechwarp_listen_options_create();
  speechwarp_listen_options_set_language(options, "en");
  {
    int error = -99;
    t = now();
    speechwarp_listen_result* result = speechwarp_listen_transcribe(
        model, passage.samples.data(), int64_t(passage.samples.size()), passage.rate, options, &error);
    CHECK(result != nullptr);
    if (result) {
      once_words = words_of(speechwarp_listen_result_text(result));
      std::printf("passage in one call (%.0f s audio, %.1f s): %d segments, WER %.3f against the text\n",
                  passage.seconds(), now() - t, speechwarp_listen_result_segment_count(result),
                  word_error_rate(passage_words, once_words));
      CHECK(word_error_rate(passage_words, once_words) < 0.15);
      check_times(result, passage.seconds(), true);
      speechwarp_listen_result_free(result);
    }
  }

  // ---- The passage through a session: one thread writes in odd-sized pieces as fast as it can while this
  // one processes and takes. Same text as one call, within a tolerance.
  {
    speechwarp_listen_session* session = speechwarp_listen_session_create(model, passage.rate, options);
    CHECK(session != nullptr);
    std::atomic<bool> written{false};
    std::thread writer([&] {
      const size_t piece = 1237;
      for (size_t i = 0; i < passage.samples.size(); i += piece) {
        size_t n = std::min(piece, passage.samples.size() - i);
        if (speechwarp_listen_session_write(session, passage.samples.data() + i, int64_t(n)) != SPEECHWARP_LISTEN_OK) {
          failures++;
        }
        // About 20 times real time, faster than a tiny model recognises, like a decoder.
        std::this_thread::sleep_for(std::chrono::microseconds(int64_t(n) * 1000000 / passage.rate / 20));
      }
      written = true;
    });

    std::string text;
    int chunks = 0, segments = 0;
    double longest = 0;
    double previous_start = 0, previous_recognised = 0;
    auto collect = [&] {
      speechwarp_listen_result* result = speechwarp_listen_session_take(session);
      CHECK(result != nullptr);
      for (int s = 0; s < speechwarp_listen_result_segment_count(result); s++) {
        double start = speechwarp_listen_result_segment_start(result, s);
        CHECK(start >= previous_start);
        previous_start = start;
        longest = std::max(longest, speechwarp_listen_result_segment_end(result, s) - start);
        text += ' ';
        text += speechwarp_listen_result_segment_text(result, s);
        segments++;
      }
      check_times(result, passage.seconds(), true);
      speechwarp_listen_result_free(result);
    };
    t = now();
    while (!written) {
      int n = speechwarp_listen_session_process(session);
      CHECK(n >= 0);
      double recognised = speechwarp_listen_session_seconds_recognised(session);
      if (recognised > previous_recognised) {
        chunks++;
        previous_recognised = recognised;
        collect();
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    writer.join();

    // A partial guess at the tail changes nothing.
    {
      double before = speechwarp_listen_session_seconds_recognised(session);
      int error = -99;
      speechwarp_listen_result* partial = speechwarp_listen_session_partial(session, &error);
      CHECK(partial && error == SPEECHWARP_LISTEN_OK);
      if (partial) {
        std::printf("partial guess at the last %.1f s: \"%s\"\n",
                    speechwarp_listen_session_seconds_written(session) - before, speechwarp_listen_result_text(partial));
        double tail = speechwarp_listen_session_seconds_written(session) - before;
        if (tail > 2) CHECK(words_of(speechwarp_listen_result_text(partial)).size() > 0);
        if (speechwarp_listen_result_segment_count(partial) > 0) {
          CHECK(speechwarp_listen_result_segment_start(partial, 0) >= before - 0.01);
        }
        speechwarp_listen_result_free(partial);
      }
      CHECK(speechwarp_listen_session_seconds_recognised(session) == before);
    }

    int n = speechwarp_listen_session_finish(session);
    CHECK(n >= 0);
    collect();
    CHECK(speechwarp_listen_session_write(session, passage.samples.data(), 10) == SPEECHWARP_LISTEN_ERROR_FINISHED);
    CHECK(std::fabs(speechwarp_listen_session_seconds_written(session) - passage.seconds()) < 1e-6);
    CHECK(std::fabs(speechwarp_listen_session_seconds_recognised(session) - passage.seconds()) < 0.001);
    CHECK(speechwarp_listen_session_finish(session) == 0);  // again: nothing left

    std::vector<std::string> session_words = words_of(text);
    double against_once = word_error_rate(once_words, session_words);
    double against_text = word_error_rate(passage_words, session_words);
    std::printf("passage in a session (%.1f s): %d chunks before finishing, %d segments, WER %.3f against "
                "the text, %.3f against one call\n",
                now() - t, chunks, segments, against_text, against_once);
    CHECK(chunks >= 2);
    // The passage is in sentences, so no segment should be long (whisper alone sometimes gives 30 s).
    std::printf("longest segment %.1f s\n", longest);
    CHECK(longest <= 15);
    CHECK(against_text < 0.15);
    CHECK(against_once < 0.10);
    speechwarp_listen_session_free(session);
  }

  // ---- Cancelling a session loses nothing; a session at 16 kHz; partial with little audio.
  {
    speechwarp_listen_session* session = speechwarp_listen_session_create(model, passage.rate, options);
    size_t forty = size_t(passage.rate) * 40;
    CHECK(speechwarp_listen_session_write(session, passage.samples.data(), int64_t(forty)) == SPEECHWARP_LISTEN_OK);
    speechwarp_listen_session_cancel(session);
    CHECK(speechwarp_listen_session_process(session) == SPEECHWARP_LISTEN_ERROR_CANCELLED);
    CHECK(speechwarp_listen_session_seconds_recognised(session) == 0);
    // Cancel while it runs.
    std::thread canceller([&] {
      std::this_thread::sleep_for(std::chrono::milliseconds(30));
      speechwarp_listen_session_cancel(session);
    });
    int status = speechwarp_listen_session_process(session);
    canceller.join();
    std::printf("process cancelled while running: %s\n", speechwarp_listen_error_message(status));
    if (status == SPEECHWARP_LISTEN_ERROR_CANCELLED) {
      CHECK(speechwarp_listen_session_seconds_recognised(session) == 0);
    } else {
      speechwarp_listen_session_process(session);  // it finished first; clear the late cancel
    }
    CHECK(speechwarp_listen_session_process(session) >= 0);
    // Forty seconds make one chunk of 20 to 30 seconds, cut at the quietest point.
    double chunk = speechwarp_listen_session_seconds_recognised(session);
    std::printf("first chunk of forty seconds: %.2f s\n", chunk);
    CHECK(chunk >= 20 && chunk <= 30);
    speechwarp_listen_session_free(session);

    session = speechwarp_listen_session_create(model, 16000, nullptr);
    int error = -99;
    speechwarp_listen_result* partial = speechwarp_listen_session_partial(session, &error);
    CHECK(partial && speechwarp_listen_result_segment_count(partial) == 0);
    speechwarp_listen_result_free(partial);
    CHECK(speechwarp_listen_session_finish(session) == 0);
    speechwarp_listen_session_free(session);

    CHECK(speechwarp_listen_session_create(model, 1000, nullptr) == nullptr);
  }

  speechwarp_listen_options_free(options);
  speechwarp_listen_model_free(model);
  if (failures) {
    std::fprintf(stderr, "%d checks failed\n", failures);
    return 1;
  }
  std::printf("listen: all passed\n");
  return 0;
}
