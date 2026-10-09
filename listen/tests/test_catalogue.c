/* The model catalogue, versions and error messages: no model needed. Compiled as C to check the header is. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "speechwarp.h"
#include "speechwarp_listen.h"

static int failures = 0;

#define CHECK(condition)                                                     \
  do {                                                                       \
    if (!(condition)) {                                                      \
      fprintf(stderr, "%s:%d: failed: %s\n", __FILE__, __LINE__, #condition); \
      failures++;                                                            \
    }                                                                        \
  } while (0)

static int ends_with(const char* s, const char* end) {
  size_t a = strlen(s), b = strlen(end);
  return a >= b && strcmp(s + a - b, end) == 0;
}

int main(void) {
  int count = speechwarp_listen_catalogue_count(), i, j, english = 0, multilingual = 0;
  const char* wanted[] = {"whisper-tiny", "whisper-tiny.en", "whisper-base", "whisper-base.en", "whisper-small",
                          "whisper-small.en", "whisper-medium", "whisper-large-v3-turbo",
                          "whisper-large-v3-turbo-q5_0"};

  CHECK(strcmp(speechwarp_listen_version(), SPEECHWARP_VERSION) == 0);
  CHECK(strlen(speechwarp_listen_engine_version()) > 0);
  CHECK(speechwarp_listen_system_info() != NULL);
  CHECK(strcmp(speechwarp_listen_error_message(SPEECHWARP_LISTEN_ERROR_CANCELLED), "Cancelled.") == 0);
  CHECK(strlen(speechwarp_listen_error_message(12345)) > 0);

  CHECK(count >= (int)(sizeof wanted / sizeof wanted[0]));
  for (i = 0; i < (int)(sizeof wanted / sizeof wanted[0]); i++) {
    CHECK(speechwarp_listen_catalogue_find(wanted[i]) >= 0);
  }
  for (i = 0; i < count; i++) {
    const char* id = speechwarp_listen_catalogue_id(i);
    const char* url = speechwarp_listen_catalogue_url(i);
    const char* sha = speechwarp_listen_catalogue_sha256(i);
    const char* file = speechwarp_listen_catalogue_file_name(i);
    const char* languages = speechwarp_listen_catalogue_languages(i);
    double speed = speechwarp_listen_catalogue_relative_speed(i);

    CHECK(id && strncmp(id, "whisper-", 8) == 0);
    CHECK(speechwarp_listen_catalogue_find(id) == i);
    for (j = 0; j < i; j++) CHECK(strcmp(speechwarp_listen_catalogue_id(j), id) != 0);
    CHECK(strlen(speechwarp_listen_catalogue_name(i)) > 0);
    CHECK(strncmp(url, "https://huggingface.co/", 23) == 0 && ends_with(url, file));
    /* The file is the id without "whisper-", as ggml-<id>.bin. */
    {
      char expected[128];
      snprintf(expected, sizeof expected, "ggml-%s.bin", id + 8);
      CHECK(strcmp(file, expected) == 0);
    }
    CHECK(strlen(sha) == 64);
    for (j = 0; j < 64 && sha[j]; j++) CHECK((sha[j] >= '0' && sha[j] <= '9') || (sha[j] >= 'a' && sha[j] <= 'f'));
    CHECK(speechwarp_listen_catalogue_size(i) > 30000000);
    CHECK(speed >= 1 && speed < 100);
    if (strcmp(languages, "en") == 0) {
      english++;
      CHECK(strstr(id, ".en") != NULL);
    } else {
      multilingual++;
      CHECK(strcmp(languages, "") == 0 && strstr(id, ".en") == NULL);
    }
  }
  CHECK(english > 0 && multilingual > 0);
  CHECK(speechwarp_listen_catalogue_relative_speed(0) == 1);

  CHECK(speechwarp_listen_catalogue_id(-1) == NULL && speechwarp_listen_catalogue_id(count) == NULL);
  CHECK(speechwarp_listen_catalogue_url(count) == NULL && speechwarp_listen_catalogue_sha256(count) == NULL);
  CHECK(speechwarp_listen_catalogue_size(count) == 0);
  CHECK(isnan(speechwarp_listen_catalogue_relative_speed(count)));
  CHECK(speechwarp_listen_catalogue_find("nonsense") == -1 && speechwarp_listen_catalogue_find(NULL) == -1);

  /* Handles that need no model. */
  {
    speechwarp_listen_options* options = speechwarp_listen_options_create();
    CHECK(options != NULL);
    CHECK(speechwarp_listen_options_set_language(options, "en-GB") == SPEECHWARP_LISTEN_OK);
    CHECK(speechwarp_listen_options_set_language(options, "auto") == SPEECHWARP_LISTEN_OK);
    CHECK(speechwarp_listen_options_set_language(options, NULL) == SPEECHWARP_LISTEN_OK);
    CHECK(speechwarp_listen_options_set_language(options, "nb-NO") == SPEECHWARP_LISTEN_OK);
    CHECK(speechwarp_listen_options_set_language(options, "xq") == SPEECHWARP_LISTEN_ERROR_ARGUMENT);
    CHECK(speechwarp_listen_options_set_language(NULL, "en") == SPEECHWARP_LISTEN_ERROR_ARGUMENT);
    speechwarp_listen_options_set_preset(options, 7);
    speechwarp_listen_options_set_prompt(options, NULL);
    speechwarp_listen_options_free(options);
    speechwarp_listen_options_free(NULL);
  }
  CHECK(speechwarp_listen_model_load("/nonexistent/ggml-none.bin", 0, 0) == NULL);
  CHECK(speechwarp_listen_model_load(NULL, 0, 0) == NULL);
  CHECK(speechwarp_listen_session_create(NULL, 16000, NULL) == NULL);
  CHECK(speechwarp_listen_result_segment_count(NULL) == 0);
  CHECK(isnan(speechwarp_listen_result_segment_start(NULL, 0)));
  CHECK(speechwarp_listen_session_write(NULL, NULL, 0) == SPEECHWARP_LISTEN_ERROR_ARGUMENT);
  {
    int error = 0;
    float sample = 0;
    CHECK(speechwarp_listen_transcribe(NULL, &sample, 1, 16000, NULL, &error) == NULL);
    CHECK(error == SPEECHWARP_LISTEN_ERROR_ARGUMENT);
  }

  if (failures) {
    fprintf(stderr, "%d checks failed\n", failures);
    return 1;
  }
  printf("catalogue: %d models, all well formed\n", count);
  return 0;
}
