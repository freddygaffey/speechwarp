package speechwarp

/*
#include "speechwarp.h"
*/
import "C"

import "runtime"

// A TrainerMeasure says what a score in 0..1 measures.
type TrainerMeasure int

const (
	// TrainerMeasureIntelligibility is the share of the words said back correctly from a sentence heard once.
	TrainerMeasureIntelligibility TrainerMeasure = 0
	// TrainerMeasureVerification is the share right on "was this sentence in what you just heard?" items (the
	// sentence verification technique): originals and paraphrases against changed-meaning and unrelated
	// sentences. Chance is 0.5.
	TrainerMeasureVerification TrainerMeasure = 1
	// TrainerMeasureRetention is verification items about a session's material, answered after a delay: see
	// ListenerTrainer.AddRetention. It is not used for thresholds.
	TrainerMeasureRetention TrainerMeasure = 2
	// TrainerMeasureRating is the listener's own "how well did you follow?", 1 to 5 scaled to 0..1 as
	// (r - 1) / 4. It is subjective, so weighted less by default.
	TrainerMeasureRating TrainerMeasure = 3
)

// A TrainerPlan says how the rate moves during a session.
type TrainerPlan int

const (
	// TrainerPlanSteady is the threshold plus a margin, all session.
	TrainerPlanSteady TrainerPlan = 0
	// TrainerPlanRamp starts below the threshold and steps up to threshold plus margin.
	TrainerPlanRamp TrainerPlan = 1
	// TrainerPlanInterval alternates periods above and below the threshold.
	TrainerPlanInterval TrainerPlan = 2
	// TrainerPlanTracking moves up or down after each in-session check, to stay at the target.
	TrainerPlanTracking TrainerPlan = 3
)

// A TrainerParam is a tunable number of the trainer. The defaults are given with each.
type TrainerParam int

const (
	// TrainerParamTarget is the share understood that defines the threshold: 0.75 (0.5 to 0.95).
	TrainerParamTarget TrainerParam = 0
	// TrainerParamMargin is, for steady and ramp, how far above the threshold to aim, as a fraction: 0.10.
	TrainerParamMargin TrainerParam = 1
	// TrainerParamRampStart is, for ramp, where to start, as a fraction of the target rate: 0.8.
	TrainerParamRampStart TrainerParam = 2
	// TrainerParamRampStep is, for ramp, the step as a fraction of the target rate: 0.02.
	TrainerParamRampStep TrainerParam = 3
	// TrainerParamRampMinutes is, for ramp, the minutes between steps: 2.
	TrainerParamRampMinutes TrainerParam = 4
	// TrainerParamIntervalSpread is, for interval, the fraction above, then below, the threshold: 0.15.
	TrainerParamIntervalSpread TrainerParam = 5
	// TrainerParamIntervalMinutes is, for interval, the minutes in each period: 10.
	TrainerParamIntervalMinutes TrainerParam = 6
	// TrainerParamTrackingGain is, for tracking, how far ln(rate) moves per check: gain x (score - target): 0.4.
	TrainerParamTrackingGain TrainerParam = 7
	// TrainerParamRetentionCost is, for plans, the threshold gain an hour (as a fraction, before practice slows
	// it) worth a whole unit of retention: 0.2, so 10 points of retention are worth 2% an hour.
	TrainerParamRetentionCost TrainerParam = 8
	// TrainerParamTestMax is, for the threshold test, the most presentations: 40.
	TrainerParamTestMax TrainerParam = 9
	// TrainerParamTestPrecision is, for the threshold test, how narrow the 95% interval must be (its high
	// divided by its low) for the test to be done: 1.25.
	TrainerParamTestPrecision TrainerParam = 10
)

// A ListenerTrainer holds the logic for training a listener to follow faster speech: no audio, no clock, no
// storage.
//
// The caller passes plain numbers in (scores, rates, its own timestamps) and gets rates and plans out.
// Deterministic: two trainers created with the same seed and given the same calls give the same answers, so a
// caller keeps its own log of calls and replays it into a new trainer to restore state.
//
// The unit of rate everywhere is syllables a second heard: the source's syllable rate times the speed. Times
// are seconds on any clock the caller likes (Unix time, say), and only differences are used. NaN arguments are
// ignored.
//
// The protocol: a threshold test (TestBegin ... TestEnd), SessionBegin, listening with a check every ten
// minutes or so (AddMeasure), a second test, SessionEnd; retention items a day and a week later
// (AddRetention).
//
// A ListenerTrainer is not safe for use by more than one goroutine at a time.
type ListenerTrainer struct {
	raw *C.speechwarp_trainer
}

// NewListenerTrainer creates a trainer with a seed for its random choices. Close it when done.
func NewListenerTrainer(seed uint64) (*ListenerTrainer, error) {
	raw := C.speechwarp_trainer_create(C.uint64_t(seed))
	if raw == nil {
		return nil, ErrOutOfMemory
	}
	t := &ListenerTrainer{raw: raw}
	runtime.SetFinalizer(t, func(t *ListenerTrainer) { t.Close() })
	return t, nil
}

// Close frees the trainer. Closing twice is harmless; methods of a closed trainer do nothing and return zero
// values.
func (t *ListenerTrainer) Close() error {
	if t.raw != nil {
		C.speechwarp_trainer_destroy(t.raw)
		t.raw = nil
		runtime.SetFinalizer(t, nil)
	}
	return nil
}

// Weight returns how much a measure of the kind counts, per item, against the others.
func (t *ListenerTrainer) Weight(kind TrainerMeasure) float64 {
	if t.raw == nil {
		return 0
	}
	return float64(C.speechwarp_trainer_get_weight(t.raw, C.int(kind)))
}

// SetWeight sets how much a measure of the kind counts, per item, against the others. Defaults:
// intelligibility 0.5 (a word, and the words of one sentence are not independent), verification 1, retention
// 1, rating 0.3 (subjective). 0 ignores the kind; negative and NaN are ignored.
func (t *ListenerTrainer) SetWeight(kind TrainerMeasure, weight float64) {
	if t.raw != nil {
		C.speechwarp_trainer_set_weight(t.raw, C.int(kind), C.double(weight))
	}
}

// Param returns the value of a tunable number.
func (t *ListenerTrainer) Param(param TrainerParam) float64 {
	if t.raw == nil {
		return 0
	}
	return float64(C.speechwarp_trainer_get_param(t.raw, C.int(param)))
}

// SetParam sets a tunable number; see TrainerParam for the meanings and defaults.
func (t *ListenerTrainer) SetParam(param TrainerParam, value float64) {
	if t.raw != nil {
		C.speechwarp_trainer_set_param(t.raw, C.int(param), C.double(value))
	}
}

// AddMeasure adds a score: kind (not TrainerMeasureRetention), score 0..1, from items items (for a sentence
// repeated back, the number of words scored; for verification, the number of questions; for a rating, 1),
// heard at rate syllables a second, at time. During a threshold test it updates the estimate; during a
// session it is an in-session check, and the tracking plan reacts to it. It returns false if an argument is
// invalid.
func (t *ListenerTrainer) AddMeasure(kind TrainerMeasure, score, items, rate, time float64) bool {
	return t.raw != nil && C.speechwarp_trainer_add_measure(t.raw, C.int(kind), C.double(score), C.double(items),
		C.double(rate), C.double(time)) != 0
}

// TestBegin starts a threshold test. It estimates the rate understood TrainerParamTarget (75%) of the time,
// lapses aside, by the psi method (Kontsevich and Tyler, 1999). The prior is log-normal around priorRate or,
// if that is 0, around the last estimate, or failing that around 10 syllables a second, within 3 to 60.
func (t *ListenerTrainer) TestBegin(priorRate, time float64) {
	if t.raw != nil {
		C.speechwarp_trainer_test_begin(t.raw, C.double(priorRate), C.double(time))
	}
}

// TestRate returns the rate to present next, in syllables a second.
func (t *ListenerTrainer) TestRate() float64 {
	if t.raw == nil {
		return 0
	}
	return float64(C.speechwarp_trainer_test_rate(t.raw))
}

// TestDone reports whether the 95% interval is narrower than TrainerParamTestPrecision (at least 8
// presentations) or TrainerParamTestMax presentations have been scored; false otherwise or if no test is
// running.
func (t *ListenerTrainer) TestDone() bool {
	return t.raw != nil && C.speechwarp_trainer_test_done(t.raw) != 0
}

// TestEnd finishes the test; the estimate becomes the current threshold. It returns it (0 if no test was
// running).
func (t *ListenerTrainer) TestEnd(time float64) float64 {
	if t.raw == nil {
		return 0
	}
	return float64(C.speechwarp_trainer_test_end(t.raw, C.double(time)))
}

// Threshold returns the current estimate (posterior median): of the running test, else of the last one
// finished; 0 if none.
func (t *ListenerTrainer) Threshold() float64 {
	if t.raw == nil {
		return 0
	}
	return float64(C.speechwarp_trainer_threshold(t.raw))
}

// ThresholdLow returns the low end of the 95% interval of Threshold.
func (t *ListenerTrainer) ThresholdLow() float64 {
	if t.raw == nil {
		return 0
	}
	return float64(C.speechwarp_trainer_threshold_low(t.raw))
}

// ThresholdHigh returns the high end of the 95% interval of Threshold.
func (t *ListenerTrainer) ThresholdHigh() float64 {
	if t.raw == nil {
		return 0
	}
	return float64(C.speechwarp_trainer_threshold_high(t.raw))
}

// SessionBegin begins a session under the plan.
func (t *ListenerTrainer) SessionBegin(plan TrainerPlan, time float64) {
	if t.raw != nil {
		C.speechwarp_trainer_session_begin(t.raw, C.int(plan), C.double(time))
	}
}

// SessionRate returns the rate to play at now, under the session's plan, from the threshold at SessionBegin.
// It is 0 if there is no session.
func (t *ListenerTrainer) SessionRate(time float64) float64 {
	if t.raw == nil {
		return 0
	}
	return float64(C.speechwarp_trainer_session_rate(t.raw, C.double(time)))
}

// SessionEnd ends the session after listeningHours of listening in it. It is recorded for comparing plans if
// a threshold test ended after it began. It returns the session's number (0, 1, ...) for AddRetention, or -1
// if no session was running.
func (t *ListenerTrainer) SessionEnd(listeningHours, time float64) int {
	if t.raw == nil {
		return -1
	}
	return int(C.speechwarp_trainer_session_end(t.raw, C.double(listeningHours), C.double(time)))
}

// AddRetention adds retention for a recorded session: score 0..1 from items items, answered delaySeconds
// after it ended. It returns false if an argument is invalid.
func (t *ListenerTrainer) AddRetention(session int, score, items, delaySeconds, time float64) bool {
	return t.raw != nil && C.speechwarp_trainer_add_retention(t.raw, C.int(session), C.double(score),
		C.double(items), C.double(delaySeconds), C.double(time)) != 0
}

// NextPlan returns the plan to run next: a Thompson draw over each recorded session's threshold gain and
// retention (it advances the random source).
func (t *ListenerTrainer) NextPlan() TrainerPlan {
	if t.raw == nil {
		return TrainerPlanSteady
	}
	return TrainerPlan(C.speechwarp_trainer_next_plan(t.raw))
}

// PlanEffect returns a plan's estimated threshold gain an hour now, at the practice so far (as a fraction:
// 0.01 is 1% an hour). It is 0 without data.
func (t *ListenerTrainer) PlanEffect(plan TrainerPlan) float64 {
	if t.raw == nil {
		return 0
	}
	return float64(C.speechwarp_trainer_plan_effect(t.raw, C.int(plan)))
}

// PlanEffectSd returns the standard deviation of PlanEffect.
func (t *ListenerTrainer) PlanEffectSd(plan TrainerPlan) float64 {
	if t.raw == nil {
		return 0
	}
	return float64(C.speechwarp_trainer_plan_effect_sd(t.raw, C.int(plan)))
}

// PlanRetention returns a plan's retention; NaN without data.
func (t *ListenerTrainer) PlanRetention(plan TrainerPlan) float64 {
	if t.raw == nil {
		return 0
	}
	return float64(C.speechwarp_trainer_plan_retention(t.raw, C.int(plan)))
}

// PlanRetentionSd returns the standard deviation of PlanRetention; NaN without data.
func (t *ListenerTrainer) PlanRetentionSd(plan TrainerPlan) float64 {
	if t.raw == nil {
		return 0
	}
	return float64(C.speechwarp_trainer_plan_retention_sd(t.raw, C.int(plan)))
}

// PlanSessions returns the sessions recorded for a plan.
func (t *ListenerTrainer) PlanSessions(plan TrainerPlan) int {
	if t.raw == nil {
		return 0
	}
	return int(C.speechwarp_trainer_plan_sessions(t.raw, C.int(plan)))
}

// PlanBestProbability returns the probability that a plan is the best by utility, from a fixed number of
// draws (it does not advance the random source).
func (t *ListenerTrainer) PlanBestProbability(plan TrainerPlan) float64 {
	if t.raw == nil {
		return 0
	}
	return float64(C.speechwarp_trainer_plan_best_probability(t.raw, C.int(plan)))
}

// Trend returns H, the hours of listening by which gains have halved (1000 stands for "not slowing").
func (t *ListenerTrainer) Trend() float64 {
	if t.raw == nil {
		return 0
	}
	return float64(C.speechwarp_trainer_trend(t.raw))
}

// TrendSd returns the uncertainty of Trend as a standard deviation of ln H.
func (t *ListenerTrainer) TrendSd() float64 {
	if t.raw == nil {
		return 0
	}
	return float64(C.speechwarp_trainer_trend_sd(t.raw))
}
