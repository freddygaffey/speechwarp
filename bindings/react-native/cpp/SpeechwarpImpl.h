#pragma once

#include <SpeechwarpSpecJSI.h>

#include <memory>
#include <unordered_map>

struct speechwarp_stream;
struct speechwarp_syllables;
struct speechwarp_trainer;
struct speechwarp_trials;

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

  double syllablesCreate(jsi::Runtime& rt, double sampleRate, double channels);
  void syllablesDestroy(jsi::Runtime& rt, double handle);
  bool syllablesWrite(jsi::Runtime& rt, double handle, jsi::Object buffer, double byteOffset, double frames);
  bool syllablesWriteI16(jsi::Runtime& rt, double handle, jsi::Object buffer, double byteOffset, double frames);
  double trainerCreate(jsi::Runtime& rt, double seed);
  void trainerDestroy(jsi::Runtime& rt, double handle);
  double trialsCreate(jsi::Runtime& rt, double seed);
  void trialsDestroy(jsi::Runtime& rt, double handle);

  void setHeardPause(jsi::Runtime& rt, double handle, double seconds, double fromSpeed);
  double getHeardPause(jsi::Runtime& rt, double handle);
  double getHeardPauseFrom(jsi::Runtime& rt, double handle);
  void setFloorBlend(jsi::Runtime& rt, double handle, double fraction, double fromSpeed, double fullSpeed);
  double getFloorBlend(jsi::Runtime& rt, double handle);
  double getFloorBlendFrom(jsi::Runtime& rt, double handle);
  double getFloorBlendFull(jsi::Runtime& rt, double handle);
  double syllablesRate(jsi::Runtime& rt, double handle, double windowSeconds, double minimumSeconds);
  void syllablesReset(jsi::Runtime& rt, double handle);
  void trainerSetWeight(jsi::Runtime& rt, double handle, double kind, double weight);
  double trainerGetWeight(jsi::Runtime& rt, double handle, double kind);
  void trainerSetParam(jsi::Runtime& rt, double handle, double param, double value);
  double trainerGetParam(jsi::Runtime& rt, double handle, double param);
  bool trainerAddMeasure(jsi::Runtime& rt, double handle, double kind, double score, double items, double rate, double time);
  void trainerTestBegin(jsi::Runtime& rt, double handle, double priorRate, double time);
  double trainerTestRate(jsi::Runtime& rt, double handle);
  bool trainerTestDone(jsi::Runtime& rt, double handle);
  double trainerTestEnd(jsi::Runtime& rt, double handle, double time);
  double trainerThreshold(jsi::Runtime& rt, double handle);
  double trainerThresholdLow(jsi::Runtime& rt, double handle);
  double trainerThresholdHigh(jsi::Runtime& rt, double handle);
  void trainerSessionBegin(jsi::Runtime& rt, double handle, double plan, double time);
  double trainerSessionRate(jsi::Runtime& rt, double handle, double time);
  double trainerSessionEnd(jsi::Runtime& rt, double handle, double listeningHours, double time);
  bool trainerAddRetention(jsi::Runtime& rt, double handle, double session, double score, double items, double delaySeconds, double time);
  double trainerNextPlan(jsi::Runtime& rt, double handle);
  double trainerPlanEffect(jsi::Runtime& rt, double handle, double plan);
  double trainerPlanEffectSd(jsi::Runtime& rt, double handle, double plan);
  double trainerPlanRetention(jsi::Runtime& rt, double handle, double plan);
  double trainerPlanRetentionSd(jsi::Runtime& rt, double handle, double plan);
  double trainerPlanSessions(jsi::Runtime& rt, double handle, double plan);
  double trainerPlanBestProbability(jsi::Runtime& rt, double handle, double plan);
  double trainerTrend(jsi::Runtime& rt, double handle);
  double trainerTrendSd(jsi::Runtime& rt, double handle);
  double trialsAddSetting(jsi::Runtime& rt, double handle);
  double trialsAddValue(jsi::Runtime& rt, double handle, double setting, double value);
  void trialsSetAvailable(jsi::Runtime& rt, double handle, double setting, bool available);
  bool trialsAdd(jsi::Runtime& rt, double handle, double setting, double speed, double firstValue, double secondValue, double firstScore, double secondScore, double preferred);
  double trialsNext(jsi::Runtime& rt, double handle, double speed);
  double trialsNextFirst(jsi::Runtime& rt, double handle);
  double trialsNextSecond(jsi::Runtime& rt, double handle);
  double trialsWon(jsi::Runtime& rt, double handle, double setting, double speed, double value);
  double trialsLost(jsi::Runtime& rt, double handle, double setting, double speed, double value);
  double trialsTied(jsi::Runtime& rt, double handle, double setting, double speed, double value);
  double trialsHeard(jsi::Runtime& rt, double handle, double setting, double speed, double value);
  double trialsMeanScore(jsi::Runtime& rt, double handle, double setting, double speed, double value);
  double trialsWinner(jsi::Runtime& rt, double handle, double setting, double speed);
  void trialsSetConfidence(jsi::Runtime& rt, double handle, double confidence);

private:
  struct Entry {
    speechwarp_stream* stream;
    int channels;
  };
  const Entry& find(jsi::Runtime& rt, double handle);
  float* samples(jsi::Runtime& rt, const Entry& entry, jsi::Object& buffer, double byteOffset, double frames);

  struct CounterEntry {
    speechwarp_syllables* counter;
    int channels;
  };
  const CounterEntry& findCounter(jsi::Runtime& rt, double handle);
  static void* bytes(jsi::Runtime& rt, jsi::Object& buffer, double byteOffset, double needed);

  std::unordered_map<int, Entry> streams_;
  std::unordered_map<int, CounterEntry> counters_;
  std::unordered_map<int, speechwarp_trainer*> trainers_;
  std::unordered_map<int, speechwarp_trials*> trials_;
  int next_ = 1;
};

}
