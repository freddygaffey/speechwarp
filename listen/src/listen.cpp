// speechwarp_listen - speech to text with whisper.cpp.
//
// Licensed under the Apache License, Version 2.0. See LICENSE and NOTICE.
//
// No exception leaves this file: every extern "C" function catches and returns an error code or NULL.
#include "speechwarp_listen.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <vector>

#include "resample.h"
#include "speechwarp.h"  // only for SPEECHWARP_VERSION: the two modules share a version
#include "whisper.h"

using speechwarp_listen::Resampler;

namespace {

constexpr int kRate = WHISPER_SAMPLE_RATE;  // 16000
constexpr double kChunkMin = 20.0;          // seconds: a session's chunks are cut between these two
constexpr double kChunkMax = 30.0;
constexpr double kPartialMin = 0.33;        // seconds below which a partial guess is not worth running
constexpr size_t kContextTokens = 160;      // tokens of previous text given as context (whisper takes 224)
constexpr double kSplitSeconds = 10.0;      // segments longer than this are split at sentence ends
constexpr double kMinSplitSeconds = 1.5;    // into parts no shorter than this
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

struct Word {
  std::string text;
  double start = 0, end = 0, probability = 0;
};

struct Segment {
  std::string text;
  double start = 0, end = 0;
  std::vector<Word> words;
};

struct Piece {
  whisper_token id;
  std::string text;
  double t0, t1, p;
};

struct Settings {
  std::string language;  // whisper code, or "" to detect
  bool words = true;
  std::string prompt;
  int preset = SPEECHWARP_LISTEN_PRESET_BALANCED;
  int threads = 0;
};

void quiet_log(enum ggml_log_level, const char*, void*) {}

void stderr_log(enum ggml_log_level, const char* text, void*) { std::fputs(text, stderr); }

std::once_flag log_once;

// whisper.cpp logs to standard error until told otherwise; be quiet unless asked.
void init_log() {
  std::call_once(log_once, [] { whisper_log_set(quiet_log, nullptr); });
}

bool abort_requested(void* flag) { return static_cast<std::atomic<bool>*>(flag)->load(); }

std::string trim(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) a++;
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) b--;
  return s.substr(a, b - a);
}

// "[BLANK_AUDIO]", "(music)", "*sighs*": whisper's notes on sounds, not speech.
bool is_annotation(const std::string& text) {
  if (text.size() < 2) return false;
  char first = text.front(), last = text.back();
  return (first == '[' && last == ']') || (first == '(' && last == ')') || (first == '*' && last == '*');
}

// Whether a word ends a sentence: "home." or "alone?\"" but not "Mr.".
bool ends_sentence(std::string word) {
  word = trim(word);
  while (!word.empty() && (word.back() == '"' || word.back() == '\'' || word.back() == ')')) word.pop_back();
  if (word.empty() || (word.back() != '.' && word.back() != '!' && word.back() != '?')) return false;
  std::string lower;
  for (char c : word) lower += char(std::tolower(static_cast<unsigned char>(c)));
  static const char* const abbreviations[] = {"mr.", "mrs.", "ms.", "dr.", "st.", "jr.", "sr.", "prof.", "mt.", "vs."};
  for (const char* a : abbreviations) {
    if (lower == a) return false;
  }
  return true;
}

// A BCP 47 tag or whisper code to the whisper code, or "" for detection. Returns false if whisper does not
// know the language.
bool whisper_language(const char* tag, std::string& code) {
  if (!tag || !*tag) {
    code.clear();
    return true;
  }
  std::string s;
  for (const char* p = tag; *p && *p != '-' && *p != '_'; p++) s += char(std::tolower(static_cast<unsigned char>(*p)));
  if (s == "auto") {
    code.clear();
    return true;
  }
  // Where BCP 47 and whisper disagree.
  if (s == "nb") s = "no";
  else if (s == "fil") s = "tl";
  else if (s == "jv") s = "jw";
  else if (s == "iw") s = "he";
  if (whisper_lang_id(s.c_str()) < 0) return false;
  code = s;
  return true;
}

int default_threads() {
  unsigned n = std::thread::hardware_concurrency();
  return n == 0 ? 4 : int(std::min(n, 8u));
}

}  // namespace

struct speechwarp_listen_model {
  whisper_context* context = nullptr;
  int threads = 0;
  std::mutex pool_mutex;
  std::vector<whisper_state*> pool;  // working memory left by finished transcriptions, for the next

  ~speechwarp_listen_model() {
    for (whisper_state* state : pool) whisper_free_state(state);
    if (context) whisper_free(context);
  }

  whisper_state* acquire() {
    {
      std::lock_guard<std::mutex> lock(pool_mutex);
      if (!pool.empty()) {
        whisper_state* state = pool.back();
        pool.pop_back();
        return state;
      }
    }
    return whisper_init_state(context);
  }

  void release(whisper_state* state) {
    if (!state) return;
    std::lock_guard<std::mutex> lock(pool_mutex);
    if (pool.size() < 2) {
      pool.push_back(state);
      return;
    }
    whisper_free_state(state);
  }
};

struct speechwarp_listen_options {
  Settings settings;
  std::atomic<bool> cancelled{false};
};

struct speechwarp_listen_result {
  std::vector<Segment> segments;
  std::string language;
  std::string text;

  void join() {
    text.clear();
    for (const Segment& segment : segments) {
      if (segment.text.empty()) continue;
      if (!text.empty()) text += ' ';
      text += segment.text;
    }
  }
};

namespace {

// Recognise `count` samples at 16 kHz that start `offset` seconds into the audio, and append the segments.
// Times are clamped to the audio given and kept in order.
int recognise(speechwarp_listen_model* model, whisper_state* state, const Settings& settings, int preset,
              const std::vector<whisper_token>* prompt, const float* samples, size_t count, double offset,
              std::atomic<bool>* cancel, std::vector<Segment>& segments, std::string* language,
              std::vector<whisper_token>* tokens) {
  // whisper.cpp needs at least 0.1 s; pad anything short with silence.
  std::vector<float> padded;
  const size_t minimum = kRate / 2;
  if (count < minimum) {
    padded.assign(samples, samples + count);
    padded.resize(minimum, 0.0f);
    samples = padded.data();
  }
  const size_t given = count;
  count = std::max(count, minimum);

  whisper_full_params params = whisper_full_default_params(
      preset == SPEECHWARP_LISTEN_PRESET_ACCURATE ? WHISPER_SAMPLING_BEAM_SEARCH : WHISPER_SAMPLING_GREEDY);
  params.n_threads = settings.threads > 0 ? settings.threads : model->threads;
  params.print_special = false;
  params.print_progress = false;
  params.print_realtime = false;
  params.print_timestamps = false;
  params.no_context = true;  // context comes only from the prompt, so a reused state carries nothing over
  params.token_timestamps = true;  // for word times, and for splitting long segments
  params.language = settings.language.empty() ? "auto" : settings.language.c_str();
  params.detect_language = false;
  params.suppress_nst = true;
  if (prompt && !prompt->empty()) {
    // A session's: the caller's prompt and the tokens of the previous chunk, timestamps included, as whisper
    // conditions on its own previous window.
    params.prompt_tokens = prompt->data();
    params.prompt_n_tokens = int(prompt->size());
  } else if (!settings.prompt.empty()) {
    // One call: the caller's prompt before every 30-second window, then whisper's own previous text.
    params.initial_prompt = settings.prompt.c_str();
    params.carry_initial_prompt = true;
  }
  params.abort_callback = abort_requested;
  params.abort_callback_user_data = cancel;
  if (preset == SPEECHWARP_LISTEN_PRESET_FAST) {
    params.temperature_inc = 0.0f;  // no retries
    params.greedy.best_of = 1;
  } else if (preset == SPEECHWARP_LISTEN_PRESET_ACCURATE) {
    params.beam_search.beam_size = 5;
  }

  if (cancel->load()) return SPEECHWARP_LISTEN_ERROR_CANCELLED;
  int status = whisper_full_with_state(model->context, state, params, samples, int(count));
  if (cancel->load()) return SPEECHWARP_LISTEN_ERROR_CANCELLED;
  if (status != 0) return SPEECHWARP_LISTEN_ERROR_ENGINE;

  if (language) {
    int id = whisper_full_lang_id_from_state(state);
    const char* code = id >= 0 ? whisper_lang_str(id) : nullptr;
    *language = code ? code : "";
  }

  const double limit = offset + double(given) / kRate;
  double floor = segments.empty() ? offset : std::max(offset, segments.back().start);
  auto clamp = [&](double t, double low) { return std::min(std::max(t, low), limit); };
  const whisper_token eot = whisper_token_eot(model->context);
  const whisper_token beg = whisper_token_beg(model->context);
  // The timestamp token for a time in this call's audio: 0 to 30 s in steps of 20 ms.
  auto timestamp = [&](double t) {
    return beg + whisper_token(std::min(std::max(std::lround((t - offset) * 50), 0L), 1500L));
  };

  int n = whisper_full_n_segments_from_state(state);
  for (int i = 0; i < n; i++) {
    const char* text = whisper_full_get_segment_text_from_state(state, i);
    if (trim(text ? text : "").empty() || is_annotation(trim(text))) continue;
    const double segment_start = offset + whisper_full_get_segment_t0_from_state(state, i) / 100.0;
    const double segment_end = offset + whisper_full_get_segment_t1_from_state(state, i) / 100.0;

    std::vector<Piece> pieces;
    int token_count = whisper_full_n_tokens_from_state(state, i);
    for (int j = 0; j < token_count; j++) {
      whisper_token_data data = whisper_full_get_token_data_from_state(state, i, j);
      if (data.id >= eot) continue;  // special and timestamp tokens
      const char* piece = whisper_full_get_token_text_from_state(model->context, state, i, j);
      if (!piece || !*piece) continue;
      pieces.push_back({data.id, piece, offset + data.t0 / 100.0, offset + data.t1 / 100.0, data.p});
    }
    if (pieces.empty()) continue;

    // Whisper sometimes gives half a minute as one segment; split those at the ends of sentences.
    std::vector<size_t> parts{0};
    if (segment_end - segment_start > kSplitSeconds) {
      std::string word;
      for (size_t k = 0; k < pieces.size(); k++) {
        const std::string& piece = pieces[k].text;
        if (piece[0] == ' ' && k > 0) {
          bool capital = piece.size() > 1 && (std::isupper(static_cast<unsigned char>(piece[1])) ||
                                              std::isdigit(static_cast<unsigned char>(piece[1])));
          if (capital && ends_sentence(word) && pieces[k].t0 - pieces[parts.back()].t0 >= kMinSplitSeconds &&
              segment_end - pieces[k].t0 >= kMinSplitSeconds) {
            parts.push_back(k);
          }
          word.clear();
        }
        word += piece;
      }
    }
    parts.push_back(pieces.size());

    for (size_t part = 0; part + 1 < parts.size(); part++) {
      const size_t first = parts[part], last = parts[part + 1];
      Segment segment;
      for (size_t k = first; k < last; k++) segment.text += pieces[k].text;
      segment.text = trim(segment.text);
      if (segment.text.empty()) continue;
      double start = part == 0 ? segment_start : pieces[first].t0;
      double end = part + 2 == parts.size() ? segment_end : pieces[last - 1].t1;
      segment.start = clamp(start, floor);
      segment.end = clamp(end, segment.start);

      if (settings.words) {
        Word word;
        int word_tokens = 0;
        double word_floor = segment.start;
        auto close = [&] {
          if (word_tokens == 0) return;
          word.probability /= word_tokens;
          word.text = trim(word.text);
          if (!word.text.empty()) {
            word.start = std::min(std::max(word.start, word_floor), segment.end);
            word.end = std::min(std::max(word.end, word.start), segment.end);
            word_floor = word.start;
            segment.words.push_back(word);
          }
          word = Word();
          word_tokens = 0;
        };
        for (size_t k = first; k < last; k++) {
          // A token starting with a space starts a word; others (the rest of a word, punctuation) join it.
          if (pieces[k].text[0] == ' ' && word_tokens > 0) close();
          if (word_tokens == 0) word.start = pieces[k].t0;
          word.text += pieces[k].text;
          word.end = pieces[k].t1;
          word.probability += pieces[k].p;
          word_tokens++;
        }
        close();
      }

      if (tokens) {
        // For the next chunk's context, as whisper would have decoded it: <|start|> text <|end|>.
        tokens->push_back(timestamp(segment.start));
        for (size_t k = first; k < last; k++) tokens->push_back(pieces[k].id);
        tokens->push_back(timestamp(segment.end));
      }
      floor = segment.start;
      segments.push_back(std::move(segment));
    }
  }
  return SPEECHWARP_LISTEN_OK;
}

// Tokens of `text`, or none if it is empty or cannot be tokenised.
std::vector<whisper_token> tokenize(whisper_context* context, const std::string& text) {
  std::vector<whisper_token> tokens;
  if (text.empty()) return tokens;
  tokens.resize(text.size() + 8);
  int n = whisper_tokenize(context, text.c_str(), tokens.data(), int(tokens.size()));
  if (n < 0) {
    tokens.resize(size_t(-n));
    n = whisper_tokenize(context, text.c_str(), tokens.data(), int(tokens.size()));
  }
  tokens.resize(size_t(std::max(n, 0)));
  return tokens;
}

// Where to end a chunk of `available` samples: at the quietest 50 ms between kChunkMin and kChunkMax seconds.
size_t quietest_cut(const float* samples, size_t available) {
  const size_t frame = kRate / 100;  // 10 ms
  const size_t window = 5;           // frames
  size_t first = size_t(kChunkMin * kRate) / frame;
  size_t last = std::min(size_t(kChunkMax * kRate), available) / frame;
  if (last < first + window) return std::min(size_t(kChunkMax * kRate), available);

  std::vector<double> energy(last - first);
  for (size_t f = first; f < last; f++) {
    double sum = 0;
    for (size_t i = f * frame; i < (f + 1) * frame; i++) sum += double(samples[i]) * samples[i];
    energy[f - first] = sum;
  }
  double best = std::numeric_limits<double>::infinity();
  size_t best_frame = last - window;
  double run = 0;
  for (size_t f = 0; f < energy.size(); f++) {
    run += energy[f];
    if (f >= window) run -= energy[f - window];
    if (f + 1 >= window && run < best) {
      best = run;
      best_frame = first + f + 1 - window;
    }
  }
  return (best_frame + window / 2) * frame;
}

}  // namespace

struct speechwarp_listen_session {
  speechwarp_listen_model* model = nullptr;
  Settings settings;
  int sample_rate = 0;

  // Guarded by `mutex`: what _write and the getters touch.
  std::mutex mutex;
  std::unique_ptr<Resampler> resampler;
  std::vector<float> audio;  // 16 kHz audio not yet recognised, from audio[consumed]
  size_t consumed = 0;
  int64_t recognised = 0;    // 16 kHz samples recognised: the time of audio[consumed]
  bool ended = false;
  std::vector<Segment> finished;  // not yet taken
  double last_start = 0;          // start of the latest finished segment, to keep times in order
  std::string language;           // as recognised in the latest chunk with speech

  // Guarded by `work`: one recognition at a time.
  std::mutex work;
  whisper_state* state = nullptr;
  std::vector<whisper_token> hints;    // the caller's prompt, tokenised
  std::vector<whisper_token> context;  // the latest chunk's tokens
  std::atomic<bool> cancel{false};

  // The caller's prompt, then as much of the latest chunk as fits.
  std::vector<whisper_token> prompt() const {
    std::vector<whisper_token> tokens(hints);
    size_t room = kContextTokens > hints.size() ? kContextTokens - hints.size() : 0;
    size_t take = std::min(room, context.size());
    tokens.insert(tokens.end(), context.end() - std::ptrdiff_t(take), context.end());
    return tokens;
  }

  ~speechwarp_listen_session() {
    if (state) whisper_free_state(state);
  }

  size_t waiting() const { return audio.size() - consumed; }

  // Called with `mutex` held.
  void drop(size_t count) {
    consumed += count;
    recognised += int64_t(count);
    if (consumed > audio.size() / 2 && consumed > size_t(kRate) * 10) {
      audio.erase(audio.begin(), audio.begin() + std::ptrdiff_t(consumed));
      consumed = 0;
    }
  }

  // Recognise one chunk. With `all`, the chunk may be shorter than kChunkMax (the input has ended).
  // Returns the number of segments added, 0 when there is nothing to do, or an error.
  int step(bool all, bool* done) {
    std::vector<float> chunk;
    double offset;
    {
      std::lock_guard<std::mutex> lock(mutex);
      size_t available = waiting();
      size_t full = size_t(kChunkMax * kRate);
      if (available == 0 || (!all && available < full)) {
        *done = true;
        return 0;
      }
      size_t take = available <= full ? available : quietest_cut(audio.data() + consumed, available);
      chunk.assign(audio.begin() + std::ptrdiff_t(consumed), audio.begin() + std::ptrdiff_t(consumed + take));
      offset = double(recognised) / kRate;
    }
    if (!state) {
      state = whisper_init_state(model->context);
      if (!state) return SPEECHWARP_LISTEN_ERROR_MEMORY;
    }
    std::vector<Segment> segments;
    std::string detected;
    std::vector<whisper_token> tokens, prompt_tokens = prompt();
    int status = recognise(model, state, settings, settings.preset, &prompt_tokens, chunk.data(), chunk.size(),
                           offset, &cancel, segments, &detected, &tokens);
    if (status != SPEECHWARP_LISTEN_OK) return status;

    std::lock_guard<std::mutex> lock(mutex);
    for (Segment& segment : segments) {
      // Keep starts in order across chunks too.
      if (segment.start < last_start) {
        segment.start = last_start;
        segment.end = std::max(segment.end, segment.start);
        for (Word& word : segment.words) {
          word.start = std::max(word.start, segment.start);
          word.end = std::max(word.end, word.start);
        }
      }
      last_start = segment.start;
    }
    // A chunk of silence keeps the context it had.
    if (!tokens.empty()) context.swap(tokens);
    if (!segments.empty()) language = detected;
    drop(chunk.size());
    int added = int(segments.size());
    for (Segment& segment : segments) finished.push_back(std::move(segment));
    *done = false;
    return added;
  }
};

namespace {

// Run `body` and turn exceptions into error codes.
template <typename F>
int guarded(F body) {
  try {
    return body();
  } catch (const std::bad_alloc&) {
    return SPEECHWARP_LISTEN_ERROR_MEMORY;
  } catch (...) {
    return SPEECHWARP_LISTEN_ERROR_ENGINE;
  }
}

void set_error(int* error, int code) {
  if (error) *error = code;
}

bool valid_rate(int rate) { return rate >= 4000 && rate <= 384000; }

// After a call that saw the cancel flag, clear it so that the next call runs.
int consume_cancel(speechwarp_listen_session* session, int status) {
  if (status == SPEECHWARP_LISTEN_ERROR_CANCELLED) session->cancel.store(false);
  return status;
}

}  // namespace

extern "C" {

const char* speechwarp_listen_version(void) { return SPEECHWARP_VERSION; }

const char* speechwarp_listen_engine_version(void) { return whisper_version(); }

const char* speechwarp_listen_system_info(void) { return whisper_print_system_info(); }

const char* speechwarp_listen_error_message(int code) {
  switch (code) {
    case SPEECHWARP_LISTEN_OK: return "No error.";
    case SPEECHWARP_LISTEN_ERROR_ARGUMENT: return "An argument is invalid.";
    case SPEECHWARP_LISTEN_ERROR_MEMORY: return "Memory ran out.";
    case SPEECHWARP_LISTEN_ERROR_ENGINE: return "The speech recogniser failed.";
    case SPEECHWARP_LISTEN_ERROR_CANCELLED: return "Cancelled.";
    case SPEECHWARP_LISTEN_ERROR_FINISHED: return "The session has finished; it takes no more audio.";
    default: return "Unknown error.";
  }
}

void speechwarp_listen_set_log(int on) {
  init_log();
  whisper_log_set(on ? stderr_log : quiet_log, nullptr);
}

// ---- Models

speechwarp_listen_model* speechwarp_listen_model_load(const char* path, int threads, int flags) {
  if (!path) return nullptr;
  init_log();
  try {
    std::unique_ptr<speechwarp_listen_model> model(new speechwarp_listen_model);
    whisper_context_params params = whisper_context_default_params();
    params.use_gpu = (flags & SPEECHWARP_LISTEN_LOAD_GPU) != 0;
    // Flash attention speeds up the GPU; on the CPU whisper.cpp's default is fine either way.
    params.flash_attn = params.use_gpu;
    model->context = whisper_init_from_file_with_params_no_state(path, params);
    if (!model->context) return nullptr;
    model->threads = threads > 0 ? threads : default_threads();
    return model.release();
  } catch (...) {
    return nullptr;
  }
}

void speechwarp_listen_model_free(speechwarp_listen_model* model) { delete model; }

int speechwarp_listen_model_multilingual(const speechwarp_listen_model* model) {
  return model && whisper_is_multilingual(model->context) ? 1 : 0;
}

const char* speechwarp_listen_model_type(const speechwarp_listen_model* model) {
  if (!model) return nullptr;
  // whisper.cpp's readable names are "tiny", "base", ...; it takes a non-const context.
  return whisper_model_type_readable(model->context);
}

// ---- Options

speechwarp_listen_options* speechwarp_listen_options_create(void) {
  return new (std::nothrow) speechwarp_listen_options;
}

void speechwarp_listen_options_free(speechwarp_listen_options* options) { delete options; }

int speechwarp_listen_options_set_language(speechwarp_listen_options* options, const char* language) {
  if (!options) return SPEECHWARP_LISTEN_ERROR_ARGUMENT;
  return guarded([&] {
    std::string code;
    if (!whisper_language(language, code)) return SPEECHWARP_LISTEN_ERROR_ARGUMENT;
    options->settings.language = code;
    return SPEECHWARP_LISTEN_OK;
  });
}

void speechwarp_listen_options_set_word_timestamps(speechwarp_listen_options* options, int on) {
  if (options) options->settings.words = on != 0;
}

void speechwarp_listen_options_set_prompt(speechwarp_listen_options* options, const char* prompt) {
  if (!options) return;
  try {
    options->settings.prompt = prompt ? trim(prompt) : std::string();
  } catch (...) {
  }
}

void speechwarp_listen_options_set_preset(speechwarp_listen_options* options, int preset) {
  if (options && preset >= SPEECHWARP_LISTEN_PRESET_FAST && preset <= SPEECHWARP_LISTEN_PRESET_ACCURATE) {
    options->settings.preset = preset;
  }
}

void speechwarp_listen_options_set_threads(speechwarp_listen_options* options, int threads) {
  if (options) options->settings.threads = threads > 0 ? threads : 0;
}

void speechwarp_listen_options_cancel(speechwarp_listen_options* options) {
  if (options) options->cancelled.store(true);
}

// ---- A passage given at once

speechwarp_listen_result* speechwarp_listen_transcribe(speechwarp_listen_model* model, const float* samples,
                                                       int64_t count, int sample_rate,
                                                       const speechwarp_listen_options* options, int* error) {
  set_error(error, SPEECHWARP_LISTEN_OK);
  if (!model || (!samples && count > 0) || count < 0 || !valid_rate(sample_rate)) {
    set_error(error, SPEECHWARP_LISTEN_ERROR_ARGUMENT);
    return nullptr;
  }
  speechwarp_listen_result* out = nullptr;
  int status = guarded([&] {
    Settings settings = options ? options->settings : Settings();
    std::atomic<bool> never{false};
    // The flag lives in the options; transcriptions only read it.
    auto* cancel = options ? const_cast<std::atomic<bool>*>(&options->cancelled) : &never;
    if (cancel->load()) return SPEECHWARP_LISTEN_ERROR_CANCELLED;

    std::vector<float> audio;
    if (sample_rate == kRate) {
      audio.assign(samples, samples + count);
    } else {
      Resampler resampler(sample_rate, kRate);
      audio.reserve(size_t(double(count) * kRate / sample_rate) + 16);
      resampler.write(samples, size_t(count), audio);
      resampler.finish(audio);
    }
    std::unique_ptr<speechwarp_listen_result> result(new speechwarp_listen_result);
    if (!audio.empty()) {
      whisper_state* state = model->acquire();
      if (!state) return SPEECHWARP_LISTEN_ERROR_MEMORY;
      int code = recognise(model, state, settings, settings.preset, nullptr, audio.data(), audio.size(), 0.0, cancel,
                           result->segments, &result->language, nullptr);
      model->release(state);
      if (code != SPEECHWARP_LISTEN_OK) return code;
    }
    result->join();
    out = result.release();
    return SPEECHWARP_LISTEN_OK;
  });
  set_error(error, status);
  return out;
}

// ---- Results

namespace {

const Segment* segment_at(const speechwarp_listen_result* result, int segment) {
  if (!result || segment < 0 || size_t(segment) >= result->segments.size()) return nullptr;
  return &result->segments[size_t(segment)];
}

const Word* word_at(const speechwarp_listen_result* result, int segment, int word) {
  const Segment* s = segment_at(result, segment);
  if (!s || word < 0 || size_t(word) >= s->words.size()) return nullptr;
  return &s->words[size_t(word)];
}

}  // namespace

void speechwarp_listen_result_free(speechwarp_listen_result* result) { delete result; }

int speechwarp_listen_result_segment_count(const speechwarp_listen_result* result) {
  return result ? int(result->segments.size()) : 0;
}

const char* speechwarp_listen_result_text(const speechwarp_listen_result* result) {
  return result ? result->text.c_str() : nullptr;
}

const char* speechwarp_listen_result_language(const speechwarp_listen_result* result) {
  return result ? result->language.c_str() : nullptr;
}

const char* speechwarp_listen_result_segment_text(const speechwarp_listen_result* result, int segment) {
  const Segment* s = segment_at(result, segment);
  return s ? s->text.c_str() : nullptr;
}

double speechwarp_listen_result_segment_start(const speechwarp_listen_result* result, int segment) {
  const Segment* s = segment_at(result, segment);
  return s ? s->start : kNaN;
}

double speechwarp_listen_result_segment_end(const speechwarp_listen_result* result, int segment) {
  const Segment* s = segment_at(result, segment);
  return s ? s->end : kNaN;
}

int speechwarp_listen_result_word_count(const speechwarp_listen_result* result, int segment) {
  const Segment* s = segment_at(result, segment);
  return s ? int(s->words.size()) : 0;
}

const char* speechwarp_listen_result_word_text(const speechwarp_listen_result* result, int segment, int word) {
  const Word* w = word_at(result, segment, word);
  return w ? w->text.c_str() : nullptr;
}

double speechwarp_listen_result_word_start(const speechwarp_listen_result* result, int segment, int word) {
  const Word* w = word_at(result, segment, word);
  return w ? w->start : kNaN;
}

double speechwarp_listen_result_word_end(const speechwarp_listen_result* result, int segment, int word) {
  const Word* w = word_at(result, segment, word);
  return w ? w->end : kNaN;
}

double speechwarp_listen_result_word_probability(const speechwarp_listen_result* result, int segment, int word) {
  const Word* w = word_at(result, segment, word);
  return w ? w->probability : kNaN;
}

// ---- Sessions

speechwarp_listen_session* speechwarp_listen_session_create(speechwarp_listen_model* model, int sample_rate,
                                                            const speechwarp_listen_options* options) {
  if (!model || !valid_rate(sample_rate)) return nullptr;
  try {
    std::unique_ptr<speechwarp_listen_session> session(new speechwarp_listen_session);
    session->model = model;
    session->sample_rate = sample_rate;
    if (options) session->settings = options->settings;
    session->resampler.reset(new Resampler(sample_rate, kRate));
    session->hints = tokenize(model->context, session->settings.prompt);
    if (session->hints.size() > kContextTokens / 2) {
      session->hints.erase(session->hints.begin(), session->hints.end() - std::ptrdiff_t(kContextTokens / 2));
    }
    return session.release();
  } catch (...) {
    return nullptr;
  }
}

void speechwarp_listen_session_free(speechwarp_listen_session* session) { delete session; }

int speechwarp_listen_session_write(speechwarp_listen_session* session, const float* samples, int64_t count) {
  if (!session || count < 0 || (!samples && count > 0)) return SPEECHWARP_LISTEN_ERROR_ARGUMENT;
  return guarded([&] {
    std::lock_guard<std::mutex> lock(session->mutex);
    if (session->ended) return SPEECHWARP_LISTEN_ERROR_FINISHED;
    session->resampler->write(samples, size_t(count), session->audio);
    return SPEECHWARP_LISTEN_OK;
  });
}

int speechwarp_listen_session_process(speechwarp_listen_session* session) {
  if (!session) return SPEECHWARP_LISTEN_ERROR_ARGUMENT;
  return guarded([&] {
    std::lock_guard<std::mutex> work(session->work);
    int added = 0;
    for (;;) {
      bool done = false;
      int status = session->step(false, &done);
      if (status < 0) return consume_cancel(session, status);
      added += status;
      if (done) return added;
    }
  });
}

int speechwarp_listen_session_finish(speechwarp_listen_session* session) {
  if (!session) return SPEECHWARP_LISTEN_ERROR_ARGUMENT;
  return guarded([&] {
    std::lock_guard<std::mutex> work(session->work);
    {
      std::lock_guard<std::mutex> lock(session->mutex);
      if (!session->ended) {
        session->resampler->finish(session->audio);
        session->ended = true;
      }
    }
    int added = 0;
    for (;;) {
      bool done = false;
      int status = session->step(true, &done);
      if (status < 0) return consume_cancel(session, status);
      added += status;
      if (done) return added;
    }
  });
}

speechwarp_listen_result* speechwarp_listen_session_partial(speechwarp_listen_session* session, int* error) {
  set_error(error, SPEECHWARP_LISTEN_OK);
  if (!session) {
    set_error(error, SPEECHWARP_LISTEN_ERROR_ARGUMENT);
    return nullptr;
  }
  speechwarp_listen_result* out = nullptr;
  int status = guarded([&] {
    std::lock_guard<std::mutex> work(session->work);
    std::unique_ptr<speechwarp_listen_result> result(new speechwarp_listen_result);
    std::vector<float> tail;
    double offset;
    {
      std::lock_guard<std::mutex> lock(session->mutex);
      size_t available = session->waiting();
      size_t take = std::min(available, size_t(kChunkMax * kRate));
      size_t start = session->audio.size() - take;
      tail.assign(session->audio.begin() + std::ptrdiff_t(start), session->audio.end());
      offset = double(session->recognised + int64_t(available - take)) / kRate;
    }
    if (double(tail.size()) / kRate >= kPartialMin) {
      if (!session->state) {
        session->state = whisper_init_state(session->model->context);
        if (!session->state) return SPEECHWARP_LISTEN_ERROR_MEMORY;
      }
      std::vector<whisper_token> prompt_tokens = session->prompt();
      int code = recognise(session->model, session->state, session->settings, SPEECHWARP_LISTEN_PRESET_FAST,
                           &prompt_tokens, tail.data(), tail.size(), offset, &session->cancel, result->segments,
                           &result->language, nullptr);
      if (code != SPEECHWARP_LISTEN_OK) return consume_cancel(session, code);
    } else if (session->cancel.load()) {
      return consume_cancel(session, SPEECHWARP_LISTEN_ERROR_CANCELLED);
    }
    result->join();
    out = result.release();
    return SPEECHWARP_LISTEN_OK;
  });
  set_error(error, status);
  return out;
}

speechwarp_listen_result* speechwarp_listen_session_take(speechwarp_listen_session* session) {
  if (!session) return nullptr;
  try {
    std::unique_ptr<speechwarp_listen_result> result(new speechwarp_listen_result);
    {
      std::lock_guard<std::mutex> lock(session->mutex);
      result->segments.swap(session->finished);
      result->language = session->language;
    }
    result->join();
    return result.release();
  } catch (...) {
    return nullptr;
  }
}

void speechwarp_listen_session_cancel(speechwarp_listen_session* session) {
  if (session) session->cancel.store(true);
}

double speechwarp_listen_session_seconds_written(speechwarp_listen_session* session) {
  if (!session) return 0;
  std::lock_guard<std::mutex> lock(session->mutex);
  return double(session->resampler->input_count()) / session->sample_rate;
}

double speechwarp_listen_session_seconds_recognised(speechwarp_listen_session* session) {
  if (!session) return 0;
  std::lock_guard<std::mutex> lock(session->mutex);
  return double(session->recognised) / kRate;
}

}  // extern "C"
