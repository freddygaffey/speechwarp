/* speechwarp_listen - speech to text with whisper.cpp, for speechwarp.
 *
 * An optional module, separate from the speechwarp library: it is large (it carries whisper.cpp and ggml)
 * and needs a model file, which the app downloads. Licensed under the Apache License, Version 2.0; whisper.cpp
 * and ggml are under the MIT licence. See LICENSE, NOTICE and third_party/README.md.
 *
 * A passage given at once (a sentence said back, a clip):
 *     speechwarp_listen_model* model = speechwarp_listen_model_load("ggml-base.en.bin", 0, 0);
 *     speechwarp_listen_options* options = speechwarp_listen_options_create();
 *     speechwarp_listen_options_set_language(options, "en");
 *     speechwarp_listen_result* result = speechwarp_listen_transcribe(model, samples, count, 44100, options, &error);
 *     for (i = 0; i < speechwarp_listen_result_segment_count(result); i++)
 *         puts(speechwarp_listen_result_segment_text(result, i));
 *     speechwarp_listen_result_free(result);
 *
 * Audio that arrives over time (a book decoded in chunks, a microphone), of any length:
 *     speechwarp_listen_session* session = speechwarp_listen_session_create(model, 44100, options);
 *     for each chunk:
 *         speechwarp_listen_session_write(session, chunk, frames);
 *         if (speechwarp_listen_session_process(session) > 0) {
 *             result = speechwarp_listen_session_take(session);  use it, then free it
 *         }
 *     speechwarp_listen_session_finish(session);  then take what is left
 *     speechwarp_listen_session_free(session);
 *
 * Samples are mono 32-bit floats in [-1, 1] at any rate from 4000 to 384000 Hz; they are resampled to the
 * 16 kHz whisper needs. Times are in seconds from the start of what was given. Text is UTF-8.
 *
 * Handles and strings: every handle is freed by its own _free function. A string returned by a getter
 * belongs to the handle it came from and stays valid until that handle is freed. Functions given a NULL
 * handle or an index out of range return 0, NaN, NULL or an error as their comment says, and never crash.
 *
 * Threads: a model may be shared by any number of threads, and each may transcribe or run a session at the
 * same time (each holds its own working memory, a few tens of MB for small models and several hundred for
 * large ones). Options and results are plain values: do not change or free one while another thread uses it.
 * For sessions, see speechwarp_listen_session_create.
 */
#ifndef SPEECHWARP_LISTEN_H_
#define SPEECHWARP_LISTEN_H_

#include <stdint.h>

#if defined(_WIN32)
#  if defined(SPEECHWARP_LISTEN_BUILD)
#    define SPEECHWARP_LISTEN_API __declspec(dllexport)
#  elif defined(SPEECHWARP_LISTEN_SHARED)
#    define SPEECHWARP_LISTEN_API __declspec(dllimport)
#  else
#    define SPEECHWARP_LISTEN_API
#  endif
#else
#  define SPEECHWARP_LISTEN_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Results of the functions that return an int status. Errors are negative. */
#define SPEECHWARP_LISTEN_OK 0
#define SPEECHWARP_LISTEN_ERROR_ARGUMENT (-1)  /* a NULL handle, a bad sample rate, an unknown language */
#define SPEECHWARP_LISTEN_ERROR_MEMORY (-2)    /* memory ran out */
#define SPEECHWARP_LISTEN_ERROR_ENGINE (-3)    /* whisper.cpp failed */
#define SPEECHWARP_LISTEN_ERROR_CANCELLED (-4) /* stopped by a _cancel function; nothing was lost */
#define SPEECHWARP_LISTEN_ERROR_FINISHED (-5)  /* audio written to a session after speechwarp_listen_session_finish */

/* Speed against accuracy. The values match TranscriptionPreset in the C# and Swift bindings. */
#define SPEECHWARP_LISTEN_PRESET_FAST 0     /* greedy decoding without retries: live microphone, quick checks */
#define SPEECHWARP_LISTEN_PRESET_BALANCED 1 /* greedy decoding, retried warmer when it looks wrong (default) */
#define SPEECHWARP_LISTEN_PRESET_ACCURATE 2 /* beam search, five beams: about twice as slow */

/* Flags for speechwarp_listen_model_load. */
#define SPEECHWARP_LISTEN_LOAD_GPU 1 /* use the GPU if the library was built with one (Metal); else ignored */

typedef struct speechwarp_listen_model speechwarp_listen_model;
typedef struct speechwarp_listen_options speechwarp_listen_options;
typedef struct speechwarp_listen_result speechwarp_listen_result;
typedef struct speechwarp_listen_session speechwarp_listen_session;

/* ---- About the library ---------------------------------------------------------------------------------- */

/* The version of this module, the same as speechwarp's, e.g. "0.3.7". */
SPEECHWARP_LISTEN_API const char* speechwarp_listen_version(void);

/* The version of whisper.cpp built in, e.g. "1.9.5". */
SPEECHWARP_LISTEN_API const char* speechwarp_listen_engine_version(void);

/* What the build can use on this machine (CPU features, GPU), as one line of text, for diagnostics. */
SPEECHWARP_LISTEN_API const char* speechwarp_listen_system_info(void);

/* A sentence describing an error code, in English; never NULL. */
SPEECHWARP_LISTEN_API const char* speechwarp_listen_error_message(int code);

/* whisper.cpp's own log goes to standard error when on, and nowhere when off (the default). Applies to the
 * whole process. */
SPEECHWARP_LISTEN_API void speechwarp_listen_set_log(int on);

/* ---- Model catalogue ------------------------------------------------------------------------------------ */
/* The models an app can offer for download, from the whisper.cpp project on Hugging Face. Entries are
 * ordered roughly from least to most accurate and their order may change between versions: keep the id, not the
 * index. Out-of-range indexes give NULL, 0 or NaN. */

/* How many models there are. */
SPEECHWARP_LISTEN_API int speechwarp_listen_catalogue_count(void);

/* The index of the model with this id, or -1. */
SPEECHWARP_LISTEN_API int speechwarp_listen_catalogue_find(const char* id);

/* Stable identifier, e.g. "whisper-base.en". */
SPEECHWARP_LISTEN_API const char* speechwarp_listen_catalogue_id(int index);

/* A name to show, e.g. "Whisper base (English)". */
SPEECHWARP_LISTEN_API const char* speechwarp_listen_catalogue_name(int index);

/* The languages it understands as comma-separated BCP 47 tags, e.g. "en", or "" for a multilingual model
 * (about a hundred languages, detected or chosen with speechwarp_listen_options_set_language). */
SPEECHWARP_LISTEN_API const char* speechwarp_listen_catalogue_languages(int index);

/* Size of the download in bytes. */
SPEECHWARP_LISTEN_API int64_t speechwarp_listen_catalogue_size(int index);

/* The file name, e.g. "ggml-base.en.bin". */
SPEECHWARP_LISTEN_API const char* speechwarp_listen_catalogue_file_name(int index);

/* Where to download it (HTTPS). */
SPEECHWARP_LISTEN_API const char* speechwarp_listen_catalogue_url(int index);

/* SHA-256 of the download, 64 lower-case hex digits, to check it. */
SPEECHWARP_LISTEN_API const char* speechwarp_listen_catalogue_sha256(int index);

/* Rough time to transcribe against the fastest model, which is 1: 4 means about four times as long. An
 * estimate from the model's size and shape, for choosing; real speed depends on the machine. */
SPEECHWARP_LISTEN_API double speechwarp_listen_catalogue_relative_speed(int index);

/* ---- Models --------------------------------------------------------------------------------------------- */

/* Load a ggml model file (one of the catalogue's or any other whisper.cpp model). `threads` is the default
 * number of CPU threads for work with this model, 0 to choose (the number of cores, at most 8). `flags` is 0
 * or SPEECHWARP_LISTEN_LOAD_GPU. Returns NULL if the file is missing or is not a whisper model, or memory runs
 * out. Takes from a fraction of a second (tiny) to several seconds (large). */
SPEECHWARP_LISTEN_API speechwarp_listen_model* speechwarp_listen_model_load(const char* path, int threads, int flags);

/* Free a model. Every session using it must be freed first. NULL is ignored. */
SPEECHWARP_LISTEN_API void speechwarp_listen_model_free(speechwarp_listen_model* model);

/* 1 if the model understands many languages, 0 if English only (the ".en" models). */
SPEECHWARP_LISTEN_API int speechwarp_listen_model_multilingual(const speechwarp_listen_model* model);

/* The model's size class: "tiny", "base", "small", "medium" or "large", or NULL for a NULL model. */
SPEECHWARP_LISTEN_API const char* speechwarp_listen_model_type(const speechwarp_listen_model* model);

/* ---- Options -------------------------------------------------------------------------------------------- */
/* Settings for a transcription or a session. A session copies them when it is created. */

/* New options with the defaults: language detected, word times on, no prompt, balanced preset, the model's
 * threads. Returns NULL if memory runs out. */
SPEECHWARP_LISTEN_API speechwarp_listen_options* speechwarp_listen_options_create(void);

/* Free options. NULL is ignored. */
SPEECHWARP_LISTEN_API void speechwarp_listen_options_free(speechwarp_listen_options* options);

/* The language spoken, as a BCP 47 tag ("en", "en-GB", "de") or a whisper code; only the primary language
 * matters. NULL, "" or "auto" detects it. Returns SPEECHWARP_LISTEN_OK, or SPEECHWARP_LISTEN_ERROR_ARGUMENT
 * (and leaves the setting alone) if whisper does not know the language. English-only models ignore it. */
SPEECHWARP_LISTEN_API int speechwarp_listen_options_set_language(speechwarp_listen_options* options,
                                                                 const char* language);

/* Times for each word as well as each segment, 1 (the default) or 0. Costs a little speed. Word times come
 * from whisper's token timestamps and are good to a tenth of a second or two. */
SPEECHWARP_LISTEN_API void speechwarp_listen_options_set_word_timestamps(speechwarp_listen_options* options, int on);

/* Text to steer the recogniser: words likely to appear (names, invented words) or the style of the text,
 * e.g. "Hermione, Hogwarts, Quidditch." Whisper treats it as the text spoken just before, so a list of
 * words separated by commas works. At most about 100 words are used. NULL or "" for none (the default). */
SPEECHWARP_LISTEN_API void speechwarp_listen_options_set_prompt(speechwarp_listen_options* options, const char* prompt);

/* One of SPEECHWARP_LISTEN_PRESET_*; anything else is ignored. */
SPEECHWARP_LISTEN_API void speechwarp_listen_options_set_preset(speechwarp_listen_options* options, int preset);

/* CPU threads, 0 for the model's setting (the default). */
SPEECHWARP_LISTEN_API void speechwarp_listen_options_set_threads(speechwarp_listen_options* options, int threads);

/* Stop every speechwarp_listen_transcribe using these options, now or later, as soon as it can: it returns
 * NULL with SPEECHWARP_LISTEN_ERROR_CANCELLED. Safe to call from any thread while a transcription runs. It
 * cannot be undone: create new options for the next transcription. */
SPEECHWARP_LISTEN_API void speechwarp_listen_options_cancel(speechwarp_listen_options* options);

/* ---- A passage given at once ---------------------------------------------------------------------------- */

/* Transcribe `count` mono samples at `sample_rate` (4000 to 384000 Hz). Any length, but the audio and its
 * 16 kHz copy are all in memory: use a session for more than a few minutes. `options` may be NULL for the
 * defaults. Blocks until done: from a fraction of a second for a sentence with a small model to hours for a
 * book with a large one. Returns a result to free with speechwarp_listen_result_free, or NULL with the reason
 * in `*error` (which may be NULL; it is set to SPEECHWARP_LISTEN_OK on success). Silence gives a result with
 * no segments. */
SPEECHWARP_LISTEN_API speechwarp_listen_result* speechwarp_listen_transcribe(speechwarp_listen_model* model,
                                                                             const float* samples, int64_t count,
                                                                             int sample_rate,
                                                                             const speechwarp_listen_options* options,
                                                                             int* error);

/* ---- Results -------------------------------------------------------------------------------------------- */
/* What was recognised: segments (usually a phrase or sentence each), each with its words. Segment and word
 * indexes start at 0. Words are empty unless word times were asked for. A word's text carries the
 * punctuation that follows it and no leading space, e.g. "world,". A segment whisper made longer than 10
 * seconds is split at the ends of its sentences, so subtitles and highlighting stay usable. */

/* Free a result. NULL is ignored. */
SPEECHWARP_LISTEN_API void speechwarp_listen_result_free(speechwarp_listen_result* result);

/* How many segments. 0 for NULL. */
SPEECHWARP_LISTEN_API int speechwarp_listen_result_segment_count(const speechwarp_listen_result* result);

/* All the segments' text, trimmed and joined with single spaces. "" if there are none. */
SPEECHWARP_LISTEN_API const char* speechwarp_listen_result_text(const speechwarp_listen_result* result);

/* The language recognised, as whisper's code ("en", "de"), or "" if unknown. */
SPEECHWARP_LISTEN_API const char* speechwarp_listen_result_language(const speechwarp_listen_result* result);

/* A segment's text, trimmed. */
SPEECHWARP_LISTEN_API const char* speechwarp_listen_result_segment_text(const speechwarp_listen_result* result,
                                                                        int segment);

/* When a segment starts and ends, in seconds. Segments are in order and do not go backwards: each starts no
 * earlier than the one before. NaN if out of range. */
SPEECHWARP_LISTEN_API double speechwarp_listen_result_segment_start(const speechwarp_listen_result* result,
                                                                    int segment);
SPEECHWARP_LISTEN_API double speechwarp_listen_result_segment_end(const speechwarp_listen_result* result,
                                                                  int segment);

/* How many words a segment has. 0 if out of range. */
SPEECHWARP_LISTEN_API int speechwarp_listen_result_word_count(const speechwarp_listen_result* result, int segment);

/* A word's text. */
SPEECHWARP_LISTEN_API const char* speechwarp_listen_result_word_text(const speechwarp_listen_result* result,
                                                                     int segment, int word);

/* When a word starts and ends, in seconds, within its segment's times and in order. NaN if out of range. */
SPEECHWARP_LISTEN_API double speechwarp_listen_result_word_start(const speechwarp_listen_result* result,
                                                                 int segment, int word);
SPEECHWARP_LISTEN_API double speechwarp_listen_result_word_end(const speechwarp_listen_result* result,
                                                               int segment, int word);

/* How sure the recogniser is of a word, 0 to 1: the mean probability of its tokens. NaN if out of range. */
SPEECHWARP_LISTEN_API double speechwarp_listen_result_word_probability(const speechwarp_listen_result* result,
                                                                       int segment, int word);

/* ---- Sessions: audio that arrives over time ------------------------------------------------------------- */
/* Audio goes in with _write, which is cheap and never recognises anything. _process recognises the audio
 * in chunks of 20 to 30 seconds, each cut at the quietest moment in that range so that no word is split,
 * with the text before it as context. _partial gives a quick guess at the unfinished tail for live display.
 * _finish ends the input and recognises the rest. Finished segments are collected with _take.
 *
 * Memory: audio not yet recognised is held at 16 kHz (about 230 MB an hour), so a caller with a whole book
 * should write about 30 seconds at a time and call _process between writes, which keeps memory flat. A
 * microphone needs no care: write as audio arrives and process on another thread.
 *
 * Threads: _write, _take, _cancel and the _seconds getters may be called from any thread at any time, even
 * while another thread is in _process, _partial or _finish. _process, _partial and _finish take turns: a
 * second caller waits for the first. So the usual arrangement is one thread (the decoder or the audio
 * callback) writing and one worker thread processing. _free must not overlap any other call. */

/* Start a session for mono audio at `sample_rate` (4000 to 384000 Hz). The options are copied; NULL means the
 * defaults. The model must outlive the session. Returns NULL for a bad argument or if memory runs out. */
SPEECHWARP_LISTEN_API speechwarp_listen_session* speechwarp_listen_session_create(
    speechwarp_listen_model* model, int sample_rate, const speechwarp_listen_options* options);

/* Free a session and whatever it holds. NULL is ignored. */
SPEECHWARP_LISTEN_API void speechwarp_listen_session_free(speechwarp_listen_session* session);

/* Add `count` mono samples. Never waits for recognition. Returns SPEECHWARP_LISTEN_OK, or
 * SPEECHWARP_LISTEN_ERROR_FINISHED after _finish, SPEECHWARP_LISTEN_ERROR_MEMORY or _ARGUMENT. */
SPEECHWARP_LISTEN_API int speechwarp_listen_session_write(speechwarp_listen_session* session, const float* samples,
                                                          int64_t count);

/* Recognise every complete chunk written so far: while 30 seconds or more wait, cut a chunk and recognise
 * it. Does nothing, quickly, when less is waiting. Returns the number of segments newly finished (0 is
 * normal: silence has none, and so does a call with too little audio waiting), or a negative error. On an
 * error or a cancel, the chunk being worked on is kept and the next call tries it again. */
SPEECHWARP_LISTEN_API int speechwarp_listen_session_process(speechwarp_listen_session* session);

/* A quick guess at the audio written but not yet in a finished segment (its last 30 seconds at most), for
 * live display. Uses the fast preset whatever the session's options, and changes nothing in the session.
 * Returns a result to free (with no segments when less than a third of a second is waiting), or NULL with
 * the reason in `*error` (which may be NULL). Its times are from the start of the session. */
SPEECHWARP_LISTEN_API speechwarp_listen_result* speechwarp_listen_session_partial(speechwarp_listen_session* session,
                                                                                  int* error);

/* End the input and recognise everything left. Returns the number of segments newly finished, or a
 * negative error; after an error or a cancel, call it again to carry on. Later writes fail with
 * SPEECHWARP_LISTEN_ERROR_FINISHED. */
SPEECHWARP_LISTEN_API int speechwarp_listen_session_finish(speechwarp_listen_session* session);

/* The segments finished since the last call, in order, with times from the start of the session. Returns a
 * result to free (with no segments if nothing new), or NULL if memory runs out or `session` is NULL. */
SPEECHWARP_LISTEN_API speechwarp_listen_result* speechwarp_listen_session_take(speechwarp_listen_session* session);

/* Make the _process, _partial or _finish running now, or the next one if none is, return
 * SPEECHWARP_LISTEN_ERROR_CANCELLED as soon as it can. No audio is lost. */
SPEECHWARP_LISTEN_API void speechwarp_listen_session_cancel(speechwarp_listen_session* session);

/* Seconds of audio written so far. */
SPEECHWARP_LISTEN_API double speechwarp_listen_session_seconds_written(speechwarp_listen_session* session);

/* Seconds of the audio written that have been recognised: their ratio to the seconds written is the
 * progress. */
SPEECHWARP_LISTEN_API double speechwarp_listen_session_seconds_recognised(speechwarp_listen_session* session);

#ifdef __cplusplus
}
#endif

#endif /* SPEECHWARP_LISTEN_H_ */
