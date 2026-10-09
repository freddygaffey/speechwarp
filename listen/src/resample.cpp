// speechwarp_listen - streaming resampler to whisper's 16 kHz.
//
// Licensed under the Apache License, Version 2.0. See LICENSE and NOTICE.
#include "resample.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace speechwarp_listen {

namespace {

constexpr int kZeroCrossings = 16;
constexpr int kTableResolution = 512;  // table entries per zero crossing
constexpr size_t kMaxBank = size_t(1) << 20;  // floats: 4 MB
constexpr double kPi = 3.14159265358979323846;

}  // namespace

Resampler::Resampler(int in_rate, int out_rate) : in_rate_(in_rate), out_rate_(out_rate) {
  int64_t g = std::gcd(in_rate_, out_rate_);
  step_ = in_rate_ / g;
  phases_ = out_rate_ / g;
  passthrough_ = in_rate_ == out_rate_;
  if (passthrough_) return;

  scale_ = std::min(1.0, double(out_rate_) / double(in_rate_)) * 0.92;
  half_ = int(std::ceil(kZeroCrossings / scale_));

  // g(u) = sinc(u) x Blackman(u / Z) for 0 <= u <= Z, and one entry past the end for interpolation.
  table_.resize(size_t(kZeroCrossings) * kTableResolution + 2);
  for (size_t i = 0; i < table_.size(); i++) {
    double u = double(i) / kTableResolution;
    if (u >= kZeroCrossings) {
      table_[i] = 0;
      continue;
    }
    double sinc = u == 0 ? 1.0 : std::sin(kPi * u) / (kPi * u);
    double w = u / kZeroCrossings;
    double window = 0.42 + 0.5 * std::cos(kPi * w) + 0.08 * std::cos(2 * kPi * w);
    table_[i] = float(sinc * window);
  }

  size_t taps = size_t(2 * half_);
  if (size_t(phases_) * taps <= kMaxBank) {
    bank_.resize(size_t(phases_) * taps);
    for (int64_t p = 0; p < phases_; p++) {
      double frac = double(p) / double(phases_);
      float* w = &bank_[size_t(p) * taps];
      double sum = 0;
      for (size_t j = 0; j < taps; j++) {
        w[j] = coefficient(double(half_ - 1 - int(j)) + frac);
        sum += w[j];
      }
      // Unity gain at DC whatever the phase.
      for (size_t j = 0; j < taps; j++) w[j] = float(w[j] / sum);
    }
  }
}

float Resampler::coefficient(double x) const {
  double u = std::fabs(x) * scale_ * kTableResolution;
  size_t i = size_t(u);
  if (i + 1 >= table_.size()) return 0;
  double f = u - double(i);
  return float(scale_ * (table_[i] + (table_[i + 1] - table_[i]) * f));
}

void Resampler::write(const float* in, size_t count, std::vector<float>& out) {
  if (finished_ || count == 0) return;
  if (passthrough_) {
    out.insert(out.end(), in, in + count);
    in_total_ += int64_t(count);
    out_next_ = in_total_;
    return;
  }
  history_.insert(history_.end(), in, in + count);
  in_total_ += int64_t(count);
  emit(out, false);
}

void Resampler::finish(std::vector<float>& out) {
  if (finished_) return;
  finished_ = true;
  if (!passthrough_) emit(out, true);
}

void Resampler::emit(std::vector<float>& out, bool final) {
  const size_t taps = size_t(2 * half_);
  std::vector<float> scratch;
  for (;;) {
    int64_t position = out_next_ * step_;
    int64_t i0 = position / phases_;
    int64_t phase = position % phases_;
    if (final) {
      if (position >= in_total_ * phases_) break;  // past the last input sample
    } else if (i0 + half_ >= in_total_) {
      break;  // needs input not yet written
    }

    const float* w;
    if (!bank_.empty()) {
      w = &bank_[size_t(phase) * taps];
    } else {
      scratch.resize(taps);
      double frac = double(phase) / double(phases_), sum = 0;
      for (size_t j = 0; j < taps; j++) {
        scratch[j] = coefficient(double(half_ - 1 - int(j)) + frac);
        sum += scratch[j];
      }
      for (size_t j = 0; j < taps; j++) scratch[j] = float(scratch[j] / sum);
      w = scratch.data();
    }

    int64_t first = i0 - half_ + 1;
    double acc = 0;
    for (size_t j = 0; j < taps; j++) {
      int64_t k = first + int64_t(j);
      if (k < history_start_ || k >= in_total_) continue;  // before the start, or the silence after the end
      acc += double(w[j]) * history_[size_t(k - history_start_)];
    }
    out.push_back(float(acc));
    out_next_++;
  }

  // Drop input no later output needs.
  int64_t keep_from = (out_next_ * step_) / phases_ - half_ + 1;
  if (keep_from > history_start_) {
    size_t drop = size_t(std::min<int64_t>(keep_from - history_start_, int64_t(history_.size())));
    history_.erase(history_.begin(), history_.begin() + std::ptrdiff_t(drop));
    history_start_ += int64_t(drop);
  }
}

}  // namespace speechwarp_listen
