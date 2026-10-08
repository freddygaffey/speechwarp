package speechwarp

/*
#include "speechwarp.h"
*/
import "C"

import (
	"math"
	"runtime"
	"unsafe"
)

// A SyllableCounter measures syllables a second in audio that does not go through a Stream.
//
// It is the estimator behind Stream.SyllableRate, on its own, for a player that uses some other speed-up or
// measures a file. Given the same input it reports the same rate as a stream does with a window of 60 s and a
// minimum of 10 s. Float input is converted to 16-bit exactly as the stream converts it.
//
// A SyllableCounter is not safe for use by more than one goroutine at a time.
type SyllableCounter struct {
	raw      *C.speechwarp_syllables
	channels int
}

// NewSyllableCounter creates a counter. The sample rate must be 4000 to 384000 and there may be 1 to 32
// channels. Close it when done.
func NewSyllableCounter(sampleRate, channels int) (*SyllableCounter, error) {
	if sampleRate < 4000 || sampleRate > 384000 || channels < 1 || channels > 32 {
		return nil, ErrUnsupportedFormat
	}
	raw := C.speechwarp_syllables_create(C.int(sampleRate), C.int(channels))
	if raw == nil {
		return nil, ErrOutOfMemory
	}
	c := &SyllableCounter{raw: raw, channels: channels}
	runtime.SetFinalizer(c, func(c *SyllableCounter) { c.Close() })
	return c, nil
}

// Close frees the counter. Closing twice is harmless; methods of a closed counter do nothing and return zero
// values or ErrClosed.
func (c *SyllableCounter) Close() error {
	if c.raw != nil {
		C.speechwarp_syllables_destroy(c.raw)
		c.raw = nil
		runtime.SetFinalizer(c, nil)
	}
	return nil
}

func (c *SyllableCounter) wholeFrames(samples int) (C.int, error) {
	if c.raw == nil {
		return 0, ErrClosed
	}
	if samples%c.channels != 0 {
		return 0, ErrPartialFrame
	}
	if samples/c.channels > math.MaxInt32 {
		return 0, ErrOutOfMemory
	}
	return C.int(samples / c.channels), nil
}

// Write adds interleaved input.
func (c *SyllableCounter) Write(samples []float32) error {
	frames, err := c.wholeFrames(len(samples))
	if err != nil || frames == 0 {
		return err
	}
	if C.speechwarp_syllables_write(c.raw, (*C.float)(unsafe.Pointer(&samples[0])), frames) == 0 {
		return ErrOutOfMemory
	}
	runtime.KeepAlive(samples)
	return nil
}

// WriteInt16 adds interleaved 16-bit input.
func (c *SyllableCounter) WriteInt16(samples []int16) error {
	frames, err := c.wholeFrames(len(samples))
	if err != nil || frames == 0 {
		return err
	}
	if C.speechwarp_syllables_write_i16(c.raw, (*C.int16_t)(unsafe.Pointer(&samples[0])), frames) == 0 {
		return ErrOutOfMemory
	}
	runtime.KeepAlive(samples)
	return nil
}

// Rate returns the syllables a second over the last windowSeconds written (or all of it, if less), and false
// until minimumSeconds have been written. The window is clamped to 1 to 120 s and the minimum to 0 up to the
// window. Stream.SyllableRate uses 60 and 10.
func (c *SyllableCounter) Rate(windowSeconds, minimumSeconds float64) (float64, bool) {
	if c.raw == nil {
		return 0, false
	}
	rate := float64(C.speechwarp_syllables_rate(c.raw, C.double(windowSeconds), C.double(minimumSeconds)))
	if rate < 0 {
		return 0, false
	}
	return rate, true
}

// Reset forgets everything written.
func (c *SyllableCounter) Reset() {
	if c.raw != nil {
		C.speechwarp_syllables_reset(c.raw)
	}
}
