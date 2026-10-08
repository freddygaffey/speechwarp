package speechwarp

/*
#include "speechwarp.h"
*/
import "C"

import (
	"math"
	"runtime"
)

// BlindTrials designs the listener's own blind A/B comparisons: which setting to compare next at a speed,
// which two of its values, in what order, and how results add up in each speed band (whole numbers: 4 to 5,
// 5 to 6, ...). The caller names the settings; here they are numbers. It is deterministic for a given seed.
//
// BlindTrials is not safe for use by more than one goroutine at a time.
type BlindTrials struct {
	raw *C.speechwarp_trials
}

// NewBlindTrials creates a set of trials with a seed for its random choices. Close it when done.
func NewBlindTrials(seed uint64) (*BlindTrials, error) {
	raw := C.speechwarp_trials_create(C.uint64_t(seed))
	if raw == nil {
		return nil, ErrOutOfMemory
	}
	b := &BlindTrials{raw: raw}
	runtime.SetFinalizer(b, func(b *BlindTrials) { b.Close() })
	return b, nil
}

// Close frees the trials. Closing twice is harmless; methods of closed trials do nothing and return zero
// values.
func (b *BlindTrials) Close() error {
	if b.raw != nil {
		C.speechwarp_trials_destroy(b.raw)
		b.raw = nil
		runtime.SetFinalizer(b, nil)
	}
	return nil
}

// AddSetting adds a setting and returns its number (0, 1, ...), or -1.
func (b *BlindTrials) AddSetting() int {
	if b.raw == nil {
		return -1
	}
	return int(C.speechwarp_trials_add_setting(b.raw))
}

// AddValue adds a value to compare and returns its number within the setting, or -1 (duplicate, bad
// setting).
func (b *BlindTrials) AddValue(setting int, value float64) int {
	if b.raw == nil {
		return -1
	}
	return int(C.speechwarp_trials_add_value(b.raw, C.int(setting), C.double(value)))
}

// SetAvailable leaves a setting out of Next while false (say, when its method is not available). Settings are
// available by default.
func (b *BlindTrials) SetAvailable(setting int, available bool) {
	if b.raw != nil {
		value := C.int(0)
		if available {
			value = 1
		}
		C.speechwarp_trials_set_available(b.raw, C.int(setting), value)
	}
}

// Add records a trial at speed: the two values in the order heard, each one's score 0..1, and preferred: -1
// the first, 1 the second, 0 neither. Values must be ones added. It returns false if the trial was rejected.
func (b *BlindTrials) Add(setting int, speed, firstValue, secondValue, firstScore, secondScore float64, preferred int) bool {
	return b.raw != nil && C.speechwarp_trials_add(b.raw, C.int(setting), C.double(speed), C.double(firstValue),
		C.double(secondValue), C.double(firstScore), C.double(secondScore), C.int(preferred)) != 0
}

// Next chooses the next trial at speed: the available setting with the fewest trials in its band (ties at
// random), its pair of values compared least (ties at random), in random order. It returns the setting and
// the values to play first and second, and false if no setting has two values.
func (b *BlindTrials) Next(speed float64) (setting int, first, second float64, ok bool) {
	if b.raw == nil {
		return 0, 0, 0, false
	}
	setting = int(C.speechwarp_trials_next(b.raw, C.double(speed)))
	if setting < 0 {
		return 0, 0, 0, false
	}
	return setting, float64(C.speechwarp_trials_next_first(b.raw)), float64(C.speechwarp_trials_next_second(b.raw)), true
}

// Won returns the comparisons the value (its number within the setting) won in the band of speed.
func (b *BlindTrials) Won(setting int, speed float64, value int) int {
	if b.raw == nil {
		return 0
	}
	return int(C.speechwarp_trials_won(b.raw, C.int(setting), C.double(speed), C.int(value)))
}

// Lost returns the comparisons the value lost in the band of speed.
func (b *BlindTrials) Lost(setting int, speed float64, value int) int {
	if b.raw == nil {
		return 0
	}
	return int(C.speechwarp_trials_lost(b.raw, C.int(setting), C.double(speed), C.int(value)))
}

// Tied returns the comparisons the value tied in the band of speed.
func (b *BlindTrials) Tied(setting int, speed float64, value int) int {
	if b.raw == nil {
		return 0
	}
	return int(C.speechwarp_trials_tied(b.raw, C.int(setting), C.double(speed), C.int(value)))
}

// Heard returns the trials the value was heard in, in the band of speed.
func (b *BlindTrials) Heard(setting int, speed float64, value int) int {
	if b.raw == nil {
		return 0
	}
	return int(C.speechwarp_trials_heard(b.raw, C.int(setting), C.double(speed), C.int(value)))
}

// MeanScore returns the value's mean score in the band of speed, and false if it was never heard.
func (b *BlindTrials) MeanScore(setting int, speed float64, value int) (float64, bool) {
	if b.raw == nil {
		return 0, false
	}
	score := float64(C.speechwarp_trials_mean_score(b.raw, C.int(setting), C.double(speed), C.int(value)))
	if math.IsNaN(score) {
		return 0, false
	}
	return score, true
}

// Winner returns the number of the value with a reliable win in the band of speed, and false if there is
// none. A value wins when it has been heard in at least 5 trials, has met every other value in at least 3,
// and against each the Bayes factor for "preferred" over "no preference" is at least 1 / (1 - confidence): 20
// at the default 0.95. However often this is asked, the chance of ever naming a winner between two values
// that are really alike is at most 1 - confidence each way.
func (b *BlindTrials) Winner(setting int, speed float64) (int, bool) {
	if b.raw == nil {
		return 0, false
	}
	winner := int(C.speechwarp_trials_winner(b.raw, C.int(setting), C.double(speed)))
	if winner < 0 {
		return 0, false
	}
	return winner, true
}

// SetConfidence sets the confidence Winner asks for (default 0.95).
func (b *BlindTrials) SetConfidence(confidence float64) {
	if b.raw != nil {
		C.speechwarp_trials_set_confidence(b.raw, C.double(confidence))
	}
}
