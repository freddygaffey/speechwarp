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

// Version returns the version of the C library, such as "0.1.0".
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
