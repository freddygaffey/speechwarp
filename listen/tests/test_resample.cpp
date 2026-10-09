// The resampler: output length, chunking, gain, and that it removes what 16 kHz cannot hold.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "resample.h"

using speechwarp_listen::Resampler;

static int failures = 0;

#define CHECK(condition)                                                     \
  do {                                                                       \
    if (!(condition)) {                                                      \
      std::fprintf(stderr, "%s:%d: failed: %s\n", __FILE__, __LINE__, #condition); \
      failures++;                                                            \
    }                                                                        \
  } while (0)

static std::vector<float> tone(int rate, double frequency, double seconds, double amplitude = 0.5) {
  std::vector<float> out(size_t(rate * seconds));
  for (size_t i = 0; i < out.size(); i++) out[i] = float(amplitude * std::sin(2 * M_PI * frequency * double(i) / rate));
  return out;
}

static std::vector<float> resample(int in_rate, const std::vector<float>& in, size_t chunk) {
  Resampler resampler(in_rate, 16000);
  std::vector<float> out;
  for (size_t i = 0; i < in.size(); i += chunk) {
    resampler.write(in.data() + i, std::min(chunk, in.size() - i), out);
  }
  resampler.finish(out);
  return out;
}

// RMS over the middle, away from the edges.
static double rms(const std::vector<float>& x) {
  double sum = 0;
  size_t a = x.size() / 4, b = x.size() * 3 / 4;
  for (size_t i = a; i < b; i++) sum += double(x[i]) * x[i];
  return std::sqrt(sum / double(b - a));
}

int main() {
  const int rates[] = {8000, 11025, 16000, 22050, 32000, 44100, 48000, 96000, 44056, 384000};
  for (int rate : rates) {
    std::vector<float> in = tone(rate, 440, 1.37);
    std::vector<float> whole = resample(rate, in, in.size());
    // ceil(n x 16000 / rate) samples.
    size_t expected = size_t((int64_t(in.size()) * 16000 + rate - 1) / rate);
    CHECK(whole.size() == expected);

    // The same whatever the chunks.
    for (size_t chunk : {size_t(1), size_t(7), size_t(1000), size_t(4097)}) {
      std::vector<float> pieces = resample(rate, in, chunk);
      bool same = pieces.size() == whole.size();
      for (size_t i = 0; same && i < whole.size(); i++) same = pieces[i] == whole[i];
      if (!same) std::fprintf(stderr, "rate %d chunk %zu differs\n", rate, chunk);
      CHECK(same);
    }

    // A 440 Hz tone keeps its level and frequency.
    double level = rms(whole) / rms(in);
    if (std::fabs(level - 1) > 0.01) std::fprintf(stderr, "rate %d: 440 Hz gain %.4f\n", rate, level);
    CHECK(std::fabs(level - 1) < 0.01);
    size_t crossings = 0;
    for (size_t i = 1; i < whole.size(); i++) crossings += (whole[i - 1] < 0) != (whole[i] < 0);
    double frequency = crossings / 2.0 / (double(whole.size()) / 16000);
    CHECK(std::fabs(frequency - 440) < 5);

    // A tone above 8 kHz would alias; it must be gone (below -40 dB).
    if (rate > 20000) {
      std::vector<float> high = tone(rate, 10000, 1.0);
      double left = rms(resample(rate, high, 4096)) / rms(high);
      if (left > 0.01) std::fprintf(stderr, "rate %d: 10 kHz left at %.4f\n", rate, left);
      CHECK(left < 0.01);
    }
  }

  // 16 kHz passes through untouched.
  {
    std::vector<float> in = tone(16000, 1000, 0.5);
    std::vector<float> out = resample(16000, in, 333);
    CHECK(out == in);
  }

  // A long stream keeps its timing exactly: an impulse at 3600 s comes out at 3600 s.
  {
    const int rate = 44100;
    Resampler resampler(rate, 16000);
    std::vector<float> block(size_t(rate), 0.0f), out;
    int64_t produced = 0, peak_at = -1;
    float peak = 0;
    for (int second = 0; second <= 3600; second++) {
      block[0] = second == 3600 ? 1.0f : 0.0f;
      out.clear();
      resampler.write(block.data(), block.size(), out);
      if (second == 3600) resampler.finish(out);
      for (size_t i = 0; i < out.size(); i++) {
        if (std::fabs(out[i]) > peak) {
          peak = std::fabs(out[i]);
          peak_at = produced + int64_t(i);
        }
      }
      produced += int64_t(out.size());
    }
    CHECK(produced == int64_t(16000) * 3601);
    CHECK(peak_at == int64_t(16000) * 3600);
  }

  if (failures) {
    std::fprintf(stderr, "%d checks failed\n", failures);
    return 1;
  }
  std::printf("resample: all passed\n");
  return 0;
}
