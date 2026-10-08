// Package speechwarp speeds up speech nonlinearly: listen faster and still follow it.
//
// Ordinary speed-up compresses everything by the same amount. People who talk fast do not: they hurry
// through vowels and pauses and keep consonants close to their normal length. This package does the same,
// using Google's Speedy algorithm and the Sonic library. It is not an official Google product.
//
//	stream, err := speechwarp.NewStream(44100, 2) // sample rate, channels
//	defer stream.Close()
//	stream.SetSpeed(3)
//
//	stream.Write(input)             // interleaved []float32
//	frames := stream.Read(output)   // frames, not samples
//
//	stream.Flush()                  // at the end of the input; then Read until it returns 0
//
// Samples are interleaved: a frame is one sample per channel. Slices are measured in samples, as slices
// are; what Read returns, Available and Position are in frames.
//
// The C library is compiled into the package by cgo, so a C compiler is needed and nothing else.
package speechwarp

/*
#cgo CFLAGS: -I${SRCDIR}/../../include -I${SRCDIR}/../../third_party/kissfft -DNDEBUG -w
#cgo !windows LDFLAGS: -lm
#include "speechwarp.h"
*/
import "C"

import (
	"errors"
	"math"
	"runtime"
	"unsafe"
)

// The slowest and fastest speeds that can be set.
const (
	MinSpeed = 0.05
	MaxSpeed = 20
)

var (
	// ErrUnsupportedFormat means the sample rate is not 4000 to 384000 or the channel count is not 1 to 32.
	ErrUnsupportedFormat = errors.New("speechwarp: the sample rate must be 4000 to 384000 and the channels 1 to 32")
	// ErrPartialFrame means a slice does not hold a whole number of frames.
	ErrPartialFrame = errors.New("speechwarp: the samples are not a whole number of frames")
	// ErrOutOfMemory means the C library could not allocate memory.
	ErrOutOfMemory = errors.New("speechwarp: out of memory")
	// ErrClosed means the stream was used after Close.
	ErrClosed = errors.New("speechwarp: the stream is closed")
)

// Version returns the version of the C library, such as "0.3.5".
func Version() string {
	return C.GoString(C.speechwarp_version())
}

// A Stream speeds up speech. Write audio in, read the faster audio out.
//
// A Stream is not safe for use by more than one goroutine at a time.
type Stream struct {
	raw        *C.speechwarp_stream
	sampleRate int
	channels   int
}

// NewStream creates a stream at speed 1 with nonlinear speed-up on. The sample rate must be 4000 to 384000
// and there may be 1 to 32 channels. Close it when done.
func NewStream(sampleRate, channels int) (*Stream, error) {
	if sampleRate < 4000 || sampleRate > 384000 || channels < 1 || channels > 32 {
		return nil, ErrUnsupportedFormat
	}
	raw := C.speechwarp_create(C.int(sampleRate), C.int(channels))
	if raw == nil {
		return nil, ErrOutOfMemory
	}
	s := &Stream{raw: raw, sampleRate: sampleRate, channels: channels}
	// A forgotten stream is freed when it is collected, though not promptly.
	runtime.SetFinalizer(s, func(s *Stream) { s.Close() })
	return s, nil
}

// Close frees the stream. Closing twice is harmless; methods of a closed stream do nothing and return zero
// values or ErrClosed.
func (s *Stream) Close() error {
	if s.raw != nil {
		C.speechwarp_destroy(s.raw)
		s.raw = nil
		runtime.SetFinalizer(s, nil)
	}
	return nil
}

// SampleRate returns the samples per second.
func (s *Stream) SampleRate() int { return s.sampleRate }

// Channels returns the samples in a frame.
func (s *Stream) Channels() int { return s.channels }

// Speed returns the overall speed.
func (s *Stream) Speed() float32 {
	if s.raw == nil {
		return 0
	}
	return float32(C.speechwarp_get_speed(s.raw))
}

// SetSpeed sets the overall speed: 2 plays twice as fast. It is clamped to MinSpeed..MaxSpeed; zero,
// negative numbers and NaN are ignored.
//
// It takes effect on audio not yet processed, which includes the last 0.15 s or so written. With nonlinear
// speed-up the speed varies from moment to moment and its average is steered to this value; expect the
// result within a few percent.
func (s *Stream) SetSpeed(speed float32) {
	if s.raw != nil {
		C.speechwarp_set_speed(s.raw, C.float(speed))
	}
}

// Nonlinear returns how unevenly time is compressed, 0 to 1.
func (s *Stream) Nonlinear() float32 {
	if s.raw == nil {
		return 0
	}
	return float32(C.speechwarp_get_nonlinear(s.raw))
}

// SetNonlinear sets how unevenly time is compressed, 0 to 1. 1 (the default) slows consonants and hurries
// vowels and pauses, as a fast talker does. 0 compresses everything evenly. It may be changed during
// playback.
func (s *Stream) SetNonlinear(amount float32) {
	if s.raw != nil {
		C.speechwarp_set_nonlinear(s.raw, C.float(amount))
	}
}

// Options for very high speeds (5x to 8x). All are off by default; out-of-range values are clamped and NaN
// is ignored. See docs/how-it-works.md.

// PauseCap returns the pause cap, in seconds of input; 0 is off.
func (s *Stream) PauseCap() float32 {
	if s.raw == nil {
		return 0
	}
	return float32(C.speechwarp_get_pause_cap(s.raw))
}

// SetPauseCap shortens every pause to at most this many seconds of input before speeding up, so that the
// speed is spent on words. 0 (the default) is off. Sensible: 0.04 to 0.2. Allowed: 0, or 0.01 to 1.
// Position counts the frames left out.
func (s *Stream) SetPauseCap(seconds float32) {
	if s.raw != nil {
		C.speechwarp_set_pause_cap(s.raw, C.float(seconds))
	}
}

// KeepSpeed reports whether the overall speed is kept while the pause cap or rhythm is on.
func (s *Stream) KeepSpeed() bool {
	return s.raw != nil && C.speechwarp_get_keep_speed(s.raw) != 0
}

// SetKeepSpeed keeps the overall speed while the pause cap or rhythm is on (true, the default): time saved
// in pauses is spent playing the words slower, never below 1x, and time spent in gaps is made up by playing
// them faster. When false, trimmed pauses make playback faster than the speed.
func (s *Stream) SetKeepSpeed(enabled bool) {
	if s.raw != nil {
		value := C.int(0)
		if enabled {
			value = 1
		}
		C.speechwarp_set_keep_speed(s.raw, value)
	}
}

// SpeedFloor returns the speed floor, as a fraction of the speed; 0 is off.
func (s *Stream) SpeedFloor() float32 {
	if s.raw == nil {
		return 0
	}
	return float32(C.speechwarp_get_speed_floor(s.raw))
}

// SetSpeedFloor makes no 10 ms block of speech play slower than this fraction of the speed: at 8x, 0.5 keeps
// every block at 4x or more. 0 (the default) is off; 1 is the same as linear. Sensible: 0.3 to 0.7. It
// applies only with nonlinear speed-up above 1x.
func (s *Stream) SetSpeedFloor(fraction float32) {
	if s.raw != nil {
		C.speechwarp_set_speed_floor(s.raw, C.float(fraction))
	}
}

// RhythmGap returns the rhythm gap, in seconds; 0 is off.
func (s *Stream) RhythmGap() float32 {
	if s.raw == nil {
		return 0
	}
	return float32(C.speechwarp_get_rhythm_gap(s.raw))
}

// SetRhythmGap puts this many seconds of silence into the output RhythmRate times a second, at the quietest
// point nearby, which can help the listener keep up at very high speeds. 0 (the default) is off. Sensible:
// 0.02 to 0.06. Allowed: 0, or 0.005 to 0.2. Position holds still during a gap.
func (s *Stream) SetRhythmGap(seconds float32) {
	if s.raw != nil {
		C.speechwarp_set_rhythm_gap(s.raw, C.float(seconds))
	}
}

// RhythmRate returns the rhythm gaps a second of output.
func (s *Stream) RhythmRate() float32 {
	if s.raw == nil {
		return 0
	}
	return float32(C.speechwarp_get_rhythm_rate(s.raw))
}

// SetRhythmRate sets the rhythm gaps a second of output, 1 to 16; default 5. Sensible: 4 to 8. Zero,
// negative numbers and NaN are ignored.
func (s *Stream) SetRhythmRate(perSecond float32) {
	if s.raw != nil {
		C.speechwarp_set_rhythm_rate(s.raw, C.float(perSecond))
	}
}

// HeardPause returns the seconds last given to SetHeardPause, also while the rule is off.
func (s *Stream) HeardPause() float32 {
	if s.raw == nil {
		return 0
	}
	return float32(C.speechwarp_get_heard_pause(s.raw))
}

// HeardPauseFrom returns the fromSpeed last given to SetHeardPause.
func (s *Stream) HeardPauseFrom() float32 {
	if s.raw == nil {
		return 0
	}
	return float32(C.speechwarp_get_heard_pause_from(s.raw))
}

// SetHeardPause keeps each pause about seconds long in the output, from fromSpeed upward. The pause cap in
// force is seconds times the current speed, clamped to 0.03 to 0.4 s of input; below fromSpeed pauses are
// left alone.
//
// seconds: 0 turns the rule off (and the pause cap with it), otherwise 0.002 to 0.4. fromSpeed: 1 to 20.
// Sensible: 0.015 to 0.06 heard, from 3x. SetPauseCap turns the rule off. Out-of-range values are clamped; if
// any argument is NaN the call is ignored.
func (s *Stream) SetHeardPause(seconds, fromSpeed float32) {
	if s.raw != nil {
		C.speechwarp_set_heard_pause(s.raw, C.float(seconds), C.float(fromSpeed))
	}
}

// FloorBlend returns the fraction last given to SetFloorBlend, also while the rule is off.
func (s *Stream) FloorBlend() float32 {
	if s.raw == nil {
		return 0
	}
	return float32(C.speechwarp_get_floor_blend(s.raw))
}

// FloorBlendFrom returns the fromSpeed last given to SetFloorBlend.
func (s *Stream) FloorBlendFrom() float32 {
	if s.raw == nil {
		return 0
	}
	return float32(C.speechwarp_get_floor_blend_from(s.raw))
}

// FloorBlendFull returns the fullSpeed last given to SetFloorBlend.
func (s *Stream) FloorBlendFull() float32 {
	if s.raw == nil {
		return 0
	}
	return float32(C.speechwarp_get_floor_blend_full(s.raw))
}

// SetFloorBlend makes the speed floor in force 0 below fromSpeed, rising linearly to fraction at fullSpeed,
// and fraction above it, so that a speed ramp never changes the sound in a jump.
//
// fraction: 0 turns the rule off (and the floor with it), otherwise up to 1. Speeds 1 to 20; if fullSpeed is
// not above fromSpeed the floor steps to fraction at fromSpeed. Sensible: 0.5 from 4x, full at 6x.
// SetSpeedFloor turns the rule off. Out-of-range values are clamped; if any argument is NaN the call is
// ignored.
func (s *Stream) SetFloorBlend(fraction, fromSpeed, fullSpeed float32) {
	if s.raw != nil {
		C.speechwarp_set_floor_blend(s.raw, C.float(fraction), C.float(fromSpeed), C.float(fullSpeed))
	}
}

// SyllableRate returns the syllables a second in the input, pauses included, over about the last 60 s
// written, and false until 10 s have been written since creation or Reset. Multiply by the speed for the
// rate heard. It is an estimate, typically within about 10%.
func (s *Stream) SyllableRate() (float64, bool) {
	if s.raw == nil {
		return 0, false
	}
	rate := float64(C.speechwarp_syllable_rate(s.raw))
	if rate < 0 {
		return 0, false
	}
	return rate, true
}

// Write adds interleaved input. The output does not depend on how the input is divided between calls.
func (s *Stream) Write(samples []float32) error {
	frames, err := s.wholeFrames(len(samples))
	if err != nil || frames == 0 {
		return err
	}
	if C.speechwarp_write(s.raw, (*C.float)(unsafe.Pointer(&samples[0])), frames) == 0 {
		return ErrOutOfMemory
	}
	runtime.KeepAlive(samples)
	return nil
}

// WriteInt16 adds interleaved 16-bit input.
func (s *Stream) WriteInt16(samples []int16) error {
	frames, err := s.wholeFrames(len(samples))
	if err != nil || frames == 0 {
		return err
	}
	if C.speechwarp_write_i16(s.raw, (*C.int16_t)(unsafe.Pointer(&samples[0])), frames) == 0 {
		return ErrOutOfMemory
	}
	runtime.KeepAlive(samples)
	return nil
}

// Read takes processed output, as many whole frames as fit in samples. It returns the number of frames
// written, which is the number of samples divided by Channels. It may be 0: output lags input by a short
// look-ahead.
func (s *Stream) Read(samples []float32) int {
	max := s.maxFrames(len(samples))
	if max == 0 {
		return 0
	}
	return int(C.speechwarp_read(s.raw, (*C.float)(unsafe.Pointer(&samples[0])), max))
}

// ReadInt16 takes processed output as 16-bit samples. See Read.
func (s *Stream) ReadInt16(samples []int16) int {
	max := s.maxFrames(len(samples))
	if max == 0 {
		return 0
	}
	return int(C.speechwarp_read_i16(s.raw, (*C.int16_t)(unsafe.Pointer(&samples[0])), max))
}

// Available returns the frames of output ready to read.
func (s *Stream) Available() int {
	if s.raw == nil {
		return 0
	}
	return int(C.speechwarp_available(s.raw))
}

// Flush processes everything written so far, at the end of the input. Read until empty afterwards. Writing
// more starts a new stretch of audio, and Position carries on counting.
func (s *Stream) Flush() error {
	if s.raw == nil {
		return ErrClosed
	}
	if C.speechwarp_flush(s.raw) == 0 {
		return ErrOutOfMemory
	}
	return nil
}

// Reset discards all buffered input and output, keeping the speed and nonlinear settings, and starts
// Position again from zero. Use it after seeking.
func (s *Stream) Reset() {
	if s.raw != nil {
		C.speechwarp_reset(s.raw)
	}
}

// Position returns the input frame, counted from creation or the last Reset, that the next output frame to
// be read was made from. This is how a player maps what is being heard back to a place in the source.
//
// It never goes backwards, it is approximate (within about 0.05 s of input), and once everything after a
// Flush has been read it equals the number of frames written.
func (s *Stream) Position() int64 {
	if s.raw == nil {
		return 0
	}
	return int64(C.speechwarp_position(s.raw))
}

func (s *Stream) wholeFrames(samples int) (C.int, error) {
	if s.raw == nil {
		return 0, ErrClosed
	}
	if samples%s.channels != 0 {
		return 0, ErrPartialFrame
	}
	if samples/s.channels > math.MaxInt32 {
		return 0, ErrOutOfMemory
	}
	return C.int(samples / s.channels), nil
}

func (s *Stream) maxFrames(samples int) C.int {
	if s.raw == nil {
		return 0
	}
	frames := samples / s.channels
	if frames > math.MaxInt32 {
		frames = math.MaxInt32
	}
	return C.int(frames)
}
