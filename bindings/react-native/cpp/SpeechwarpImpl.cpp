#include "SpeechwarpImpl.h"

#include "speechwarp.h"

namespace facebook::react {

SpeechwarpImpl::SpeechwarpImpl(std::shared_ptr<CallInvoker> jsInvoker)
  : NativeSpeechwarpCxxSpec(std::move(jsInvoker)) {}

SpeechwarpImpl::~SpeechwarpImpl() {
  for (auto& [handle, entry] : streams_) {
    speechwarp_destroy(entry.stream);
  }
}

const SpeechwarpImpl::Entry& SpeechwarpImpl::find(jsi::Runtime& rt, double handle) {
  auto found = streams_.find(static_cast<int>(handle));
  if (found == streams_.end()) {
    throw jsi::JSError(rt, "speechwarp: no such stream");
  }
  return found->second;
}

// The floats in `buffer` from `byteOffset`, checked to hold `frames` frames.
float* SpeechwarpImpl::samples(jsi::Runtime& rt, const Entry& entry, jsi::Object& buffer, double byteOffset,
                               double frames) {
  if (!buffer.isArrayBuffer(rt)) {
    throw jsi::JSError(rt, "speechwarp: expected an ArrayBuffer");
  }
  jsi::ArrayBuffer array = buffer.getArrayBuffer(rt);
  double needed = byteOffset + frames * entry.channels * sizeof(float);
  if (byteOffset < 0 || frames < 0 || needed > static_cast<double>(array.size(rt))) {
    throw jsi::JSError(rt, "speechwarp: the buffer is too small");
  }
  return reinterpret_cast<float*>(array.data(rt) + static_cast<size_t>(byteOffset));
}

jsi::String SpeechwarpImpl::version(jsi::Runtime& rt) {
  return jsi::String::createFromUtf8(rt, speechwarp_version());
}

double SpeechwarpImpl::createStream(jsi::Runtime& rt, double sampleRate, double channels) {
  speechwarp_stream* stream = speechwarp_create(static_cast<int>(sampleRate), static_cast<int>(channels));
  if (!stream) {
    return 0;
  }
  int handle = next_++;
  streams_[handle] = Entry{stream, static_cast<int>(channels)};
  return handle;
}

void SpeechwarpImpl::destroyStream(jsi::Runtime& rt, double handle) {
  auto found = streams_.find(static_cast<int>(handle));
  if (found != streams_.end()) {
    speechwarp_destroy(found->second.stream);
    streams_.erase(found);
  }
}

void SpeechwarpImpl::setSpeed(jsi::Runtime& rt, double handle, double speed) {
  speechwarp_set_speed(find(rt, handle).stream, static_cast<float>(speed));
}

double SpeechwarpImpl::getSpeed(jsi::Runtime& rt, double handle) {
  return speechwarp_get_speed(find(rt, handle).stream);
}

void SpeechwarpImpl::setNonlinear(jsi::Runtime& rt, double handle, double amount) {
  speechwarp_set_nonlinear(find(rt, handle).stream, static_cast<float>(amount));
}

double SpeechwarpImpl::getNonlinear(jsi::Runtime& rt, double handle) {
  return speechwarp_get_nonlinear(find(rt, handle).stream);
}

void SpeechwarpImpl::setPauseCap(jsi::Runtime& rt, double handle, double value) {
  speechwarp_set_pause_cap(find(rt, handle).stream, static_cast<float>(value));
}

double SpeechwarpImpl::getPauseCap(jsi::Runtime& rt, double handle) {
  return speechwarp_get_pause_cap(find(rt, handle).stream);
}

void SpeechwarpImpl::setSpeedFloor(jsi::Runtime& rt, double handle, double value) {
  speechwarp_set_speed_floor(find(rt, handle).stream, static_cast<float>(value));
}

double SpeechwarpImpl::getSpeedFloor(jsi::Runtime& rt, double handle) {
  return speechwarp_get_speed_floor(find(rt, handle).stream);
}

void SpeechwarpImpl::setRhythmGap(jsi::Runtime& rt, double handle, double value) {
  speechwarp_set_rhythm_gap(find(rt, handle).stream, static_cast<float>(value));
}

double SpeechwarpImpl::getRhythmGap(jsi::Runtime& rt, double handle) {
  return speechwarp_get_rhythm_gap(find(rt, handle).stream);
}

void SpeechwarpImpl::setRhythmRate(jsi::Runtime& rt, double handle, double value) {
  speechwarp_set_rhythm_rate(find(rt, handle).stream, static_cast<float>(value));
}

double SpeechwarpImpl::getRhythmRate(jsi::Runtime& rt, double handle) {
  return speechwarp_get_rhythm_rate(find(rt, handle).stream);
}

void SpeechwarpImpl::setKeepSpeed(jsi::Runtime& rt, double handle, bool enabled) {
  speechwarp_set_keep_speed(find(rt, handle).stream, enabled ? 1 : 0);
}

bool SpeechwarpImpl::getKeepSpeed(jsi::Runtime& rt, double handle) {
  return speechwarp_get_keep_speed(find(rt, handle).stream) != 0;
}

double SpeechwarpImpl::syllableRate(jsi::Runtime& rt, double handle) {
  return speechwarp_syllable_rate(find(rt, handle).stream);
}

bool SpeechwarpImpl::write(jsi::Runtime& rt, double handle, jsi::Object buffer, double byteOffset,
                           double frames) {
  const Entry& entry = find(rt, handle);
  const float* in = samples(rt, entry, buffer, byteOffset, frames);
  return speechwarp_write(entry.stream, in, static_cast<int>(frames)) != 0;
}

double SpeechwarpImpl::read(jsi::Runtime& rt, double handle, jsi::Object buffer, double byteOffset,
                            double maxFrames) {
  const Entry& entry = find(rt, handle);
  float* out = samples(rt, entry, buffer, byteOffset, maxFrames);
  return speechwarp_read(entry.stream, out, static_cast<int>(maxFrames));
}

double SpeechwarpImpl::available(jsi::Runtime& rt, double handle) {
  return speechwarp_available(find(rt, handle).stream);
}

bool SpeechwarpImpl::flush(jsi::Runtime& rt, double handle) {
  return speechwarp_flush(find(rt, handle).stream) != 0;
}

void SpeechwarpImpl::reset(jsi::Runtime& rt, double handle) {
  speechwarp_reset(find(rt, handle).stream);
}

double SpeechwarpImpl::position(jsi::Runtime& rt, double handle) {
  return static_cast<double>(speechwarp_position(find(rt, handle).stream));
}

}
