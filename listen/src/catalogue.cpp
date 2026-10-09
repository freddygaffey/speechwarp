// speechwarp_listen - the models an app can offer for download.
//
// Licensed under the Apache License, Version 2.0. See LICENSE and NOTICE.
//
// Sizes and SHA-256 hashes are the Git LFS metadata of the files in https://huggingface.co/ggerganov/whisper.cpp
// (from https://huggingface.co/api/models/ggerganov/whisper.cpp/tree/main?expand=true, read 2026-10-09).
// The relative speeds are estimates from the arithmetic in each model's encoder and decoder on a CPU, where the
// encoder dominates; quantised models are about as fast as their originals there. They are for choosing, not
// promises.
#include <cstring>
#include <limits>

#include "speechwarp_listen.h"

namespace {

struct Model {
  const char* id;
  const char* name;
  const char* languages;
  int64_t size;
  const char* file;
  const char* sha256;
  double speed;
};

#define SPEECHWARP_LISTEN_HF "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/"

// Roughly from least to most accurate.
const Model kModels[] = {
    {"whisper-tiny.en", "Whisper tiny (English)", "en", 77704715, "ggml-tiny.en.bin",
     "921e4cf8686fdd993dcd081a5da5b6c365bfde1162e72b08d75ac75289920b1f", 1},
    {"whisper-tiny", "Whisper tiny", "", 77691713, "ggml-tiny.bin",
     "be07e048e1e599ad46341c8d2a135645097a538221678b7acdd1b1919c6e1b21", 1},
    {"whisper-base.en", "Whisper base (English)", "en", 147964211, "ggml-base.en.bin",
     "a03779c86df3323075f5e796cb2ce5029f00ec8869eee3fdfb897afe36c6d002", 2.5},
    {"whisper-base", "Whisper base", "", 147951465, "ggml-base.bin",
     "60ed5bc3dd14eea856493d334349b405782ddcaf0028d4b5df4088345fba2efe", 2.5},
    {"whisper-small.en-q5_1", "Whisper small (English, compressed)", "en", 190098681, "ggml-small.en-q5_1.bin",
     "bfdff4894dcb76bbf647d56263ea2a96645423f1669176f4844a1bf8e478ad30", 9},
    {"whisper-small.en", "Whisper small (English)", "en", 487614201, "ggml-small.en.bin",
     "c6138d6d58ecc8322097e0f987c32f1be8bb0a18532a3f88f734d1bbf9c41e5d", 9},
    {"whisper-small", "Whisper small", "", 487601967, "ggml-small.bin",
     "1be3a9b2063867b937e64e2ec7483364a79917e157fa98c5d94b5c1fffea987b", 9},
    {"whisper-medium.en-q5_0", "Whisper medium (English, compressed)", "en", 539225533, "ggml-medium.en-q5_0.bin",
     "76733e26ad8fe1c7a5bf7531a9d41917b2adc0f20f2e4f5531688a8c6cd88eb0", 30},
    {"whisper-medium.en", "Whisper medium (English)", "en", 1533774781, "ggml-medium.en.bin",
     "cc37e93478338ec7700281a7ac30a10128929eb8f427dda2e865faa8f6da4356", 30},
    {"whisper-medium-q5_0", "Whisper medium (compressed)", "", 539212467, "ggml-medium-q5_0.bin",
     "19fea4b380c3a618ec4723c3eef2eb785ffba0d0538cf43f8f235e7b3b34220f", 30},
    {"whisper-medium", "Whisper medium", "", 1533763059, "ggml-medium.bin",
     "6c14d5adee5f86394037b4e4e8b59f1673b6cee10e3cf0b11bbdbee79c156208", 30},
    {"whisper-large-v3-turbo-q5_0", "Whisper large v3 turbo (compressed)", "", 574041195,
     "ggml-large-v3-turbo-q5_0.bin", "394221709cd5ad1f40c46e6031ca61bce88931e6e088c188294c6d5a55ffa7e2", 45},
    {"whisper-large-v3-turbo", "Whisper large v3 turbo", "", 1624555275, "ggml-large-v3-turbo.bin",
     "1fc70f774d38eb169993ac391eea357ef47c88757ef72ee5943879b7e8e2bc69", 45},
};

// One URL per entry, built at compile time so the getters can return static strings.
const char* const kUrls[] = {
    SPEECHWARP_LISTEN_HF "ggml-tiny.en.bin",
    SPEECHWARP_LISTEN_HF "ggml-tiny.bin",
    SPEECHWARP_LISTEN_HF "ggml-base.en.bin",
    SPEECHWARP_LISTEN_HF "ggml-base.bin",
    SPEECHWARP_LISTEN_HF "ggml-small.en-q5_1.bin",
    SPEECHWARP_LISTEN_HF "ggml-small.en.bin",
    SPEECHWARP_LISTEN_HF "ggml-small.bin",
    SPEECHWARP_LISTEN_HF "ggml-medium.en-q5_0.bin",
    SPEECHWARP_LISTEN_HF "ggml-medium.en.bin",
    SPEECHWARP_LISTEN_HF "ggml-medium-q5_0.bin",
    SPEECHWARP_LISTEN_HF "ggml-medium.bin",
    SPEECHWARP_LISTEN_HF "ggml-large-v3-turbo-q5_0.bin",
    SPEECHWARP_LISTEN_HF "ggml-large-v3-turbo.bin",
};

constexpr int kCount = int(sizeof kModels / sizeof kModels[0]);
static_assert(sizeof kUrls / sizeof kUrls[0] == sizeof kModels / sizeof kModels[0], "one URL per model");

const Model* at(int index) { return index >= 0 && index < kCount ? &kModels[index] : nullptr; }

}  // namespace

extern "C" {

int speechwarp_listen_catalogue_count(void) { return kCount; }

int speechwarp_listen_catalogue_find(const char* id) {
  if (!id) return -1;
  for (int i = 0; i < kCount; i++) {
    if (std::strcmp(kModels[i].id, id) == 0) return i;
  }
  return -1;
}

const char* speechwarp_listen_catalogue_id(int index) { return at(index) ? at(index)->id : nullptr; }
const char* speechwarp_listen_catalogue_name(int index) { return at(index) ? at(index)->name : nullptr; }
const char* speechwarp_listen_catalogue_languages(int index) { return at(index) ? at(index)->languages : nullptr; }
int64_t speechwarp_listen_catalogue_size(int index) { return at(index) ? at(index)->size : 0; }
const char* speechwarp_listen_catalogue_file_name(int index) { return at(index) ? at(index)->file : nullptr; }
const char* speechwarp_listen_catalogue_url(int index) { return at(index) ? kUrls[index] : nullptr; }
const char* speechwarp_listen_catalogue_sha256(int index) { return at(index) ? at(index)->sha256 : nullptr; }

double speechwarp_listen_catalogue_relative_speed(int index) {
  return at(index) ? at(index)->speed : std::numeric_limits<double>::quiet_NaN();
}

}  // extern "C"
