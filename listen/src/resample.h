// speechwarp_listen - streaming resampler to whisper's 16 kHz.
//
// Licensed under the Apache License, Version 2.0. See LICENSE and NOTICE.
#ifndef SPEECHWARP_LISTEN_RESAMPLE_H_
#define SPEECHWARP_LISTEN_RESAMPLE_H_

#include <cstddef>
#include <cstdint>
#include <vector>

namespace speechwarp_listen {

// Converts mono audio from one sample rate to another with a windowed-sinc filter (16 zero crossings, a
// Blackman window, cut-off at 92% of the lower Nyquist frequency). The output does not depend on how the
// input is divided between calls, and output sample n is at input time n * in_rate / out_rate exactly, so
// timestamps do not drift over hours. Equal rates pass the audio through untouched.
class Resampler {
 public:
  Resampler(int in_rate, int out_rate);

  // Appends to `out` every output sample that the input so far determines. Throws std::bad_alloc.
  void write(const float* in, size_t count, std::vector<float>& out);

  // Ends the input (treated as followed by silence) and appends the rest of the output: in total
  // ceil(input samples * out_rate / in_rate) samples. Later writes are ignored.
  void finish(std::vector<float>& out);

  int64_t input_count() const { return in_total_; }

 private:
  float coefficient(double x) const;
  void emit(std::vector<float>& out, bool final);

  int64_t in_rate_, out_rate_;
  int64_t step_;    // in_rate / gcd
  int64_t phases_;  // out_rate / gcd: output n has phase (n * step_) % phases_
  bool passthrough_;
  bool finished_ = false;
  double scale_ = 1;  // filter cut-off as a fraction of the input rate, times two
  int half_ = 0;      // taps either side of the centre, in input samples
  std::vector<float> table_;  // the windowed sinc at fine resolution
  std::vector<float> bank_;   // per-phase taps, when they fit; else computed from table_
  std::vector<float> history_;
  int64_t history_start_ = 0;  // input index of history_[0]
  int64_t in_total_ = 0;
  int64_t out_next_ = 0;
};

}  // namespace speechwarp_listen

#endif  // SPEECHWARP_LISTEN_RESAMPLE_H_
