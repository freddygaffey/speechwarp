#pragma once

#include <SpeechwarpSpecJSI.h>

#include <memory>
#include <unordered_map>

struct speechwarp_stream;

namespace facebook::react {

// The native half of src/NativeSpeechwarp.ts. Streams are kept in a table and JavaScript holds their keys,
// so a stale or made-up handle finds nothing instead of becoming a wild pointer.
class SpeechwarpImpl : public NativeSpeechwarpCxxSpec<SpeechwarpImpl> {
public:
  SpeechwarpImpl(std::shared_ptr<CallInvoker> jsInvoker);
  ~SpeechwarpImpl();

  jsi::String version(jsi::Runtime& rt);
  double createStream(jsi::Runtime& rt, double sampleRate, double channels);
  void destroyStream(jsi::Runtime& rt, double handle);
  void setSpeed(jsi::Runtime& rt, double handle, double speed);
  double getSpeed(jsi::Runtime& rt, double handle);
  void setNonlinear(jsi::Runtime& rt, double handle, double amount);
  double getNonlinear(jsi::Runtime& rt, double handle);
  void setPauseCap(jsi::Runtime& rt, double handle, double value);
  double getPauseCap(jsi::Runtime& rt, double handle);
  void setSpeedFloor(jsi::Runtime& rt, double handle, double value);
  double getSpeedFloor(jsi::Runtime& rt, double handle);
  void setRhythmGap(jsi::Runtime& rt, double handle, double value);
  double getRhythmGap(jsi::Runtime& rt, double handle);
  void setRhythmRate(jsi::Runtime& rt, double handle, double value);
  double getRhythmRate(jsi::Runtime& rt, double handle);
  void setKeepSpeed(jsi::Runtime& rt, double handle, bool enabled);
  bool getKeepSpeed(jsi::Runtime& rt, double handle);
  double syllableRate(jsi::Runtime& rt, double handle);
  bool write(jsi::Runtime& rt, double handle, jsi::Object buffer, double byteOffset, double frames);
  double read(jsi::Runtime& rt, double handle, jsi::Object buffer, double byteOffset, double maxFrames);
  double available(jsi::Runtime& rt, double handle);
  bool flush(jsi::Runtime& rt, double handle);
  void reset(jsi::Runtime& rt, double handle);
  double position(jsi::Runtime& rt, double handle);

private:
  struct Entry {
    speechwarp_stream* stream;
    int channels;
  };
  const Entry& find(jsi::Runtime& rt, double handle);
  float* samples(jsi::Runtime& rt, const Entry& entry, jsi::Object& buffer, double byteOffset, double frames);

  std::unordered_map<int, Entry> streams_;
  int next_ = 1;
};

}
