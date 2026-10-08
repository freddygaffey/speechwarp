#include "SpeechwarpImpl.h"

#include "speechwarp.h"

namespace facebook::react {

namespace {

template <class T>
T* lookup(jsi::Runtime& rt, const std::unordered_map<int, T*>& table, double handle, const char* what) {
  auto found = table.find(static_cast<int>(handle));
  if (found == table.end()) {
    throw jsi::JSError(rt, std::string("speechwarp: no such ") + what);
  }
  return found->second;
}

}  // namespace

SpeechwarpImpl::SpeechwarpImpl(std::shared_ptr<CallInvoker> jsInvoker)
  : NativeSpeechwarpCxxSpec(std::move(jsInvoker)) {}

SpeechwarpImpl::~SpeechwarpImpl() {
  for (auto& [handle, entry] : streams_) {
    speechwarp_destroy(entry.stream);
  }
  for (auto& [handle, entry] : counters_) {
    speechwarp_syllables_destroy(entry.counter);
  }
  for (auto& [handle, trainer] : trainers_) {
    speechwarp_trainer_destroy(trainer);
  }
  for (auto& [handle, trials] : trials_) {
    speechwarp_trials_destroy(trials);
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

const SpeechwarpImpl::CounterEntry& SpeechwarpImpl::findCounter(jsi::Runtime& rt, double handle) {
  auto found = counters_.find(static_cast<int>(handle));
  if (found == counters_.end()) {
    throw jsi::JSError(rt, "speechwarp: no such syllable counter");
  }
  return found->second;
}

// `needed` bytes of `buffer` from `byteOffset`, checked.
void* SpeechwarpImpl::bytes(jsi::Runtime& rt, jsi::Object& buffer, double byteOffset, double needed) {
  if (!buffer.isArrayBuffer(rt)) {
    throw jsi::JSError(rt, "speechwarp: expected an ArrayBuffer");
  }
  jsi::ArrayBuffer array = buffer.getArrayBuffer(rt);
  if (byteOffset < 0 || needed < 0 || byteOffset + needed > static_cast<double>(array.size(rt))) {
    throw jsi::JSError(rt, "speechwarp: the buffer is too small");
  }
  return array.data(rt) + static_cast<size_t>(byteOffset);
}

double SpeechwarpImpl::syllablesCreate(jsi::Runtime& rt, double sampleRate, double channels) {
  speechwarp_syllables* counter =
    speechwarp_syllables_create(static_cast<int>(sampleRate), static_cast<int>(channels));
  if (!counter) {
    return 0;
  }
  int handle = next_++;
  counters_[handle] = CounterEntry{counter, static_cast<int>(channels)};
  return handle;
}

void SpeechwarpImpl::syllablesDestroy(jsi::Runtime& rt, double handle) {
  auto found = counters_.find(static_cast<int>(handle));
  if (found != counters_.end()) {
    speechwarp_syllables_destroy(found->second.counter);
    counters_.erase(found);
  }
}

bool SpeechwarpImpl::syllablesWrite(jsi::Runtime& rt, double handle, jsi::Object buffer, double byteOffset,
                                    double frames) {
  const CounterEntry& entry = findCounter(rt, handle);
  const float* in = static_cast<const float*>(bytes(rt, buffer, byteOffset, frames * entry.channels * sizeof(float)));
  return speechwarp_syllables_write(entry.counter, in, static_cast<int>(frames)) != 0;
}

bool SpeechwarpImpl::syllablesWriteI16(jsi::Runtime& rt, double handle, jsi::Object buffer, double byteOffset,
                                       double frames) {
  const CounterEntry& entry = findCounter(rt, handle);
  const int16_t* in =
    static_cast<const int16_t*>(bytes(rt, buffer, byteOffset, frames * entry.channels * sizeof(int16_t)));
  return speechwarp_syllables_write_i16(entry.counter, in, static_cast<int>(frames)) != 0;
}

double SpeechwarpImpl::trainerCreate(jsi::Runtime& rt, double seed) {
  speechwarp_trainer* trainer = speechwarp_trainer_create(static_cast<uint64_t>(seed));
  if (!trainer) {
    return 0;
  }
  int handle = next_++;
  trainers_[handle] = trainer;
  return handle;
}

void SpeechwarpImpl::trainerDestroy(jsi::Runtime& rt, double handle) {
  auto found = trainers_.find(static_cast<int>(handle));
  if (found != trainers_.end()) {
    speechwarp_trainer_destroy(found->second);
    trainers_.erase(found);
  }
}

double SpeechwarpImpl::trialsCreate(jsi::Runtime& rt, double seed) {
  speechwarp_trials* trials = speechwarp_trials_create(static_cast<uint64_t>(seed));
  if (!trials) {
    return 0;
  }
  int handle = next_++;
  trials_[handle] = trials;
  return handle;
}

void SpeechwarpImpl::trialsDestroy(jsi::Runtime& rt, double handle) {
  auto found = trials_.find(static_cast<int>(handle));
  if (found != trials_.end()) {
    speechwarp_trials_destroy(found->second);
    trials_.erase(found);
  }
}

void SpeechwarpImpl::setHeardPause(jsi::Runtime& rt, double handle, double seconds, double fromSpeed) {
  speechwarp_set_heard_pause(find(rt, handle).stream, seconds, fromSpeed);
}

double SpeechwarpImpl::getHeardPause(jsi::Runtime& rt, double handle) {
  return speechwarp_get_heard_pause(find(rt, handle).stream);
}

double SpeechwarpImpl::getHeardPauseFrom(jsi::Runtime& rt, double handle) {
  return speechwarp_get_heard_pause_from(find(rt, handle).stream);
}

void SpeechwarpImpl::setFloorBlend(jsi::Runtime& rt, double handle, double fraction, double fromSpeed, double fullSpeed) {
  speechwarp_set_floor_blend(find(rt, handle).stream, fraction, fromSpeed, fullSpeed);
}

double SpeechwarpImpl::getFloorBlend(jsi::Runtime& rt, double handle) {
  return speechwarp_get_floor_blend(find(rt, handle).stream);
}

double SpeechwarpImpl::getFloorBlendFrom(jsi::Runtime& rt, double handle) {
  return speechwarp_get_floor_blend_from(find(rt, handle).stream);
}

double SpeechwarpImpl::getFloorBlendFull(jsi::Runtime& rt, double handle) {
  return speechwarp_get_floor_blend_full(find(rt, handle).stream);
}

double SpeechwarpImpl::syllablesRate(jsi::Runtime& rt, double handle, double windowSeconds, double minimumSeconds) {
  return speechwarp_syllables_rate(findCounter(rt, handle).counter, windowSeconds, minimumSeconds);
}

void SpeechwarpImpl::syllablesReset(jsi::Runtime& rt, double handle) {
  speechwarp_syllables_reset(findCounter(rt, handle).counter);
}

void SpeechwarpImpl::trainerSetWeight(jsi::Runtime& rt, double handle, double kind, double weight) {
  speechwarp_trainer_set_weight(lookup(rt, trainers_, handle, "trainer"), static_cast<int>(kind), weight);
}

double SpeechwarpImpl::trainerGetWeight(jsi::Runtime& rt, double handle, double kind) {
  return speechwarp_trainer_get_weight(lookup(rt, trainers_, handle, "trainer"), static_cast<int>(kind));
}

void SpeechwarpImpl::trainerSetParam(jsi::Runtime& rt, double handle, double param, double value) {
  speechwarp_trainer_set_param(lookup(rt, trainers_, handle, "trainer"), static_cast<int>(param), value);
}

double SpeechwarpImpl::trainerGetParam(jsi::Runtime& rt, double handle, double param) {
  return speechwarp_trainer_get_param(lookup(rt, trainers_, handle, "trainer"), static_cast<int>(param));
}

bool SpeechwarpImpl::trainerAddMeasure(jsi::Runtime& rt, double handle, double kind, double score, double items, double rate, double time) {
  return speechwarp_trainer_add_measure(lookup(rt, trainers_, handle, "trainer"), static_cast<int>(kind), score, items, rate, time) != 0;
}

void SpeechwarpImpl::trainerTestBegin(jsi::Runtime& rt, double handle, double priorRate, double time) {
  speechwarp_trainer_test_begin(lookup(rt, trainers_, handle, "trainer"), priorRate, time);
}

double SpeechwarpImpl::trainerTestRate(jsi::Runtime& rt, double handle) {
  return speechwarp_trainer_test_rate(lookup(rt, trainers_, handle, "trainer"));
}

bool SpeechwarpImpl::trainerTestDone(jsi::Runtime& rt, double handle) {
  return speechwarp_trainer_test_done(lookup(rt, trainers_, handle, "trainer")) != 0;
}

double SpeechwarpImpl::trainerTestEnd(jsi::Runtime& rt, double handle, double time) {
  return speechwarp_trainer_test_end(lookup(rt, trainers_, handle, "trainer"), time);
}

double SpeechwarpImpl::trainerThreshold(jsi::Runtime& rt, double handle) {
  return speechwarp_trainer_threshold(lookup(rt, trainers_, handle, "trainer"));
}

double SpeechwarpImpl::trainerThresholdLow(jsi::Runtime& rt, double handle) {
  return speechwarp_trainer_threshold_low(lookup(rt, trainers_, handle, "trainer"));
}

double SpeechwarpImpl::trainerThresholdHigh(jsi::Runtime& rt, double handle) {
  return speechwarp_trainer_threshold_high(lookup(rt, trainers_, handle, "trainer"));
}

void SpeechwarpImpl::trainerSessionBegin(jsi::Runtime& rt, double handle, double plan, double time) {
  speechwarp_trainer_session_begin(lookup(rt, trainers_, handle, "trainer"), static_cast<int>(plan), time);
}

double SpeechwarpImpl::trainerSessionRate(jsi::Runtime& rt, double handle, double time) {
  return speechwarp_trainer_session_rate(lookup(rt, trainers_, handle, "trainer"), time);
}

double SpeechwarpImpl::trainerSessionEnd(jsi::Runtime& rt, double handle, double listeningHours, double time) {
  return speechwarp_trainer_session_end(lookup(rt, trainers_, handle, "trainer"), listeningHours, time);
}

bool SpeechwarpImpl::trainerAddRetention(jsi::Runtime& rt, double handle, double session, double score, double items, double delaySeconds, double time) {
  return speechwarp_trainer_add_retention(lookup(rt, trainers_, handle, "trainer"), static_cast<int>(session), score, items, delaySeconds, time) != 0;
}

double SpeechwarpImpl::trainerNextPlan(jsi::Runtime& rt, double handle) {
  return speechwarp_trainer_next_plan(lookup(rt, trainers_, handle, "trainer"));
}

double SpeechwarpImpl::trainerPlanEffect(jsi::Runtime& rt, double handle, double plan) {
  return speechwarp_trainer_plan_effect(lookup(rt, trainers_, handle, "trainer"), static_cast<int>(plan));
}

double SpeechwarpImpl::trainerPlanEffectSd(jsi::Runtime& rt, double handle, double plan) {
  return speechwarp_trainer_plan_effect_sd(lookup(rt, trainers_, handle, "trainer"), static_cast<int>(plan));
}

double SpeechwarpImpl::trainerPlanRetention(jsi::Runtime& rt, double handle, double plan) {
  return speechwarp_trainer_plan_retention(lookup(rt, trainers_, handle, "trainer"), static_cast<int>(plan));
}

double SpeechwarpImpl::trainerPlanRetentionSd(jsi::Runtime& rt, double handle, double plan) {
  return speechwarp_trainer_plan_retention_sd(lookup(rt, trainers_, handle, "trainer"), static_cast<int>(plan));
}

double SpeechwarpImpl::trainerPlanSessions(jsi::Runtime& rt, double handle, double plan) {
  return speechwarp_trainer_plan_sessions(lookup(rt, trainers_, handle, "trainer"), static_cast<int>(plan));
}

double SpeechwarpImpl::trainerPlanBestProbability(jsi::Runtime& rt, double handle, double plan) {
  return speechwarp_trainer_plan_best_probability(lookup(rt, trainers_, handle, "trainer"), static_cast<int>(plan));
}

double SpeechwarpImpl::trainerTrend(jsi::Runtime& rt, double handle) {
  return speechwarp_trainer_trend(lookup(rt, trainers_, handle, "trainer"));
}

double SpeechwarpImpl::trainerTrendSd(jsi::Runtime& rt, double handle) {
  return speechwarp_trainer_trend_sd(lookup(rt, trainers_, handle, "trainer"));
}

double SpeechwarpImpl::trialsAddSetting(jsi::Runtime& rt, double handle) {
  return speechwarp_trials_add_setting(lookup(rt, trials_, handle, "trials"));
}

double SpeechwarpImpl::trialsAddValue(jsi::Runtime& rt, double handle, double setting, double value) {
  return speechwarp_trials_add_value(lookup(rt, trials_, handle, "trials"), static_cast<int>(setting), value);
}

void SpeechwarpImpl::trialsSetAvailable(jsi::Runtime& rt, double handle, double setting, bool available) {
  speechwarp_trials_set_available(lookup(rt, trials_, handle, "trials"), static_cast<int>(setting), available ? 1 : 0);
}

bool SpeechwarpImpl::trialsAdd(jsi::Runtime& rt, double handle, double setting, double speed, double firstValue, double secondValue, double firstScore, double secondScore, double preferred) {
  return speechwarp_trials_add(lookup(rt, trials_, handle, "trials"), static_cast<int>(setting), speed, firstValue, secondValue, firstScore, secondScore, static_cast<int>(preferred)) != 0;
}

double SpeechwarpImpl::trialsNext(jsi::Runtime& rt, double handle, double speed) {
  return speechwarp_trials_next(lookup(rt, trials_, handle, "trials"), speed);
}

double SpeechwarpImpl::trialsNextFirst(jsi::Runtime& rt, double handle) {
  return speechwarp_trials_next_first(lookup(rt, trials_, handle, "trials"));
}

double SpeechwarpImpl::trialsNextSecond(jsi::Runtime& rt, double handle) {
  return speechwarp_trials_next_second(lookup(rt, trials_, handle, "trials"));
}

double SpeechwarpImpl::trialsWon(jsi::Runtime& rt, double handle, double setting, double speed, double value) {
  return speechwarp_trials_won(lookup(rt, trials_, handle, "trials"), static_cast<int>(setting), speed, static_cast<int>(value));
}

double SpeechwarpImpl::trialsLost(jsi::Runtime& rt, double handle, double setting, double speed, double value) {
  return speechwarp_trials_lost(lookup(rt, trials_, handle, "trials"), static_cast<int>(setting), speed, static_cast<int>(value));
}

double SpeechwarpImpl::trialsTied(jsi::Runtime& rt, double handle, double setting, double speed, double value) {
  return speechwarp_trials_tied(lookup(rt, trials_, handle, "trials"), static_cast<int>(setting), speed, static_cast<int>(value));
}

double SpeechwarpImpl::trialsHeard(jsi::Runtime& rt, double handle, double setting, double speed, double value) {
  return speechwarp_trials_heard(lookup(rt, trials_, handle, "trials"), static_cast<int>(setting), speed, static_cast<int>(value));
}

double SpeechwarpImpl::trialsMeanScore(jsi::Runtime& rt, double handle, double setting, double speed, double value) {
  return speechwarp_trials_mean_score(lookup(rt, trials_, handle, "trials"), static_cast<int>(setting), speed, static_cast<int>(value));
}

double SpeechwarpImpl::trialsWinner(jsi::Runtime& rt, double handle, double setting, double speed) {
  return speechwarp_trials_winner(lookup(rt, trials_, handle, "trials"), static_cast<int>(setting), speed);
}

void SpeechwarpImpl::trialsSetConfidence(jsi::Runtime& rt, double handle, double confidence) {
  speechwarp_trials_set_confidence(lookup(rt, trials_, handle, "trials"), confidence);
}

}
