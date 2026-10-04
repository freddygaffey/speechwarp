//! Nonlinear speed-up for speech: listen faster and still follow it.
//!
//! Ordinary speed-up compresses everything by the same amount. People who talk fast do not: they hurry
//! through vowels and pauses and keep consonants close to their normal length. This crate does the same,
//! using Google's Speedy algorithm and the Sonic library. It is not an official Google product.
//!
//! ```
//! use speechwarp::Stream;
//!
//! let mut stream = Stream::new(44100, 2)?;          // sample rate, channels
//! stream.set_speed(3.0);
//!
//! let input = vec![0.0f32; 44100 * 2];              // one second of interleaved stereo
//! stream.write(&input)?;
//! stream.flush()?;                                  // at the end of the input
//!
//! let mut output = vec![0.0f32; 4096 * 2];
//! loop {
//!     let frames = stream.read(&mut output);        // frames, not samples
//!     if frames == 0 { break; }
//!     // play &output[..frames * 2]
//! }
//! # Ok::<(), speechwarp::Error>(())
//! ```
//!
//! Samples are interleaved: a frame is one sample per channel. Slices are measured in samples, as slices
//! are; what [`Stream::read`] returns, [`Stream::available`] and [`Stream::position`] are in frames.
//!
//! The C library is compiled into the crate, so there is nothing else to install.
#![warn(missing_docs)]

use std::ffi::CStr;
use std::fmt;
use std::os::raw::{c_char, c_int};
use std::ptr::NonNull;

/// The slowest speed that can be set.
pub const MIN_SPEED: f32 = 0.05;
/// The fastest speed that can be set.
pub const MAX_SPEED: f32 = 20.0;

#[repr(C)]
struct Raw {
    _private: [u8; 0],
}

extern "C" {
    fn speechwarp_version() -> *const c_char;
    fn speechwarp_create(sample_rate: c_int, channels: c_int) -> *mut Raw;
    fn speechwarp_destroy(stream: *mut Raw);
    fn speechwarp_set_speed(stream: *mut Raw, speed: f32);
    fn speechwarp_get_speed(stream: *const Raw) -> f32;
    fn speechwarp_set_nonlinear(stream: *mut Raw, amount: f32);
    fn speechwarp_get_nonlinear(stream: *const Raw) -> f32;
    fn speechwarp_write(stream: *mut Raw, samples: *const f32, frames: c_int) -> c_int;
    fn speechwarp_write_i16(stream: *mut Raw, samples: *const i16, frames: c_int) -> c_int;
    fn speechwarp_read(stream: *mut Raw, samples: *mut f32, max_frames: c_int) -> c_int;
    fn speechwarp_read_i16(stream: *mut Raw, samples: *mut i16, max_frames: c_int) -> c_int;
    fn speechwarp_available(stream: *const Raw) -> c_int;
    fn speechwarp_flush(stream: *mut Raw) -> c_int;
    fn speechwarp_reset(stream: *mut Raw);
    fn speechwarp_position(stream: *const Raw) -> i64;
}

/// What can go wrong.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Error {
    /// The sample rate is not 4000 to 384000, or the channel count is not 1 to 32.
    UnsupportedFormat,
    /// The slice does not hold a whole number of frames.
    PartialFrame,
    /// The C library could not allocate memory.
    OutOfMemory,
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(match self {
            Error::UnsupportedFormat => "the sample rate must be 4000 to 384000 and the channels 1 to 32",
            Error::PartialFrame => "the samples are not a whole number of frames",
            Error::OutOfMemory => "speechwarp ran out of memory",
        })
    }
}

impl std::error::Error for Error {}

/// The version of the C library, such as "0.2.0".
pub fn version() -> &'static str {
    // SAFETY: the library returns a pointer to a string literal.
    unsafe { CStr::from_ptr(speechwarp_version()) }.to_str().unwrap_or("")
}

/// Speeds up speech. Write audio in, read the faster audio out.
///
/// A stream can be moved to another thread but not shared between threads without a lock: every method that
/// changes it takes `&mut self`.
pub struct Stream {
    raw: NonNull<Raw>,
    sample_rate: u32,
    channels: usize,
}

// SAFETY: the C stream owns all its memory and has no thread affinity.
unsafe impl Send for Stream {}

impl Stream {
    /// Creates a stream at speed 1 with nonlinear speed-up on.
    ///
    /// The sample rate must be 4000 to 384000 and there may be 1 to 32 channels.
    pub fn new(sample_rate: u32, channels: u32) -> Result<Stream, Error> {
        if !(4000..=384000).contains(&sample_rate) || !(1..=32).contains(&channels) {
            return Err(Error::UnsupportedFormat);
        }
        // SAFETY: the arguments have been checked, and a null result is handled.
        let raw = unsafe { speechwarp_create(sample_rate as c_int, channels as c_int) };
        Ok(Stream {
            raw: NonNull::new(raw).ok_or(Error::OutOfMemory)?,
            sample_rate,
            channels: channels as usize,
        })
    }

    /// Samples per second.
    pub fn sample_rate(&self) -> u32 {
        self.sample_rate
    }

    /// Samples in a frame.
    pub fn channels(&self) -> usize {
        self.channels
    }

    /// The overall speed.
    pub fn speed(&self) -> f32 {
        // SAFETY: `raw` is a live stream for as long as `self` exists; the same holds in every method below.
        unsafe { speechwarp_get_speed(self.raw.as_ptr()) }
    }

    /// Sets the overall speed: 2 plays twice as fast. Clamped to [`MIN_SPEED`]..[`MAX_SPEED`]; zero,
    /// negative numbers and NaN are ignored.
    ///
    /// Takes effect on audio not yet processed, which includes the last 0.15 s or so written. With nonlinear
    /// speed-up the speed varies from moment to moment and its average is steered to this value; expect the
    /// result within a few percent.
    pub fn set_speed(&mut self, speed: f32) {
        unsafe { speechwarp_set_speed(self.raw.as_ptr(), speed) }
    }

    /// How unevenly time is compressed, 0 to 1.
    pub fn nonlinear(&self) -> f32 {
        unsafe { speechwarp_get_nonlinear(self.raw.as_ptr()) }
    }

    /// Sets how unevenly time is compressed, 0 to 1. 1 (the default) slows consonants and hurries vowels and
    /// pauses, as a fast talker does. 0 compresses everything evenly. May be changed during playback.
    pub fn set_nonlinear(&mut self, amount: f32) {
        unsafe { speechwarp_set_nonlinear(self.raw.as_ptr(), amount) }
    }

    /// Adds interleaved input. The output does not depend on how the input is divided between calls.
    pub fn write(&mut self, samples: &[f32]) -> Result<(), Error> {
        let frames = self.whole_frames(samples.len())?;
        check(unsafe { speechwarp_write(self.raw.as_ptr(), samples.as_ptr(), frames) })
    }

    /// Adds interleaved 16-bit input.
    pub fn write_i16(&mut self, samples: &[i16]) -> Result<(), Error> {
        let frames = self.whole_frames(samples.len())?;
        check(unsafe { speechwarp_write_i16(self.raw.as_ptr(), samples.as_ptr(), frames) })
    }

    /// Takes processed output, as many whole frames as fit in `samples`.
    ///
    /// Returns the number of frames written, which is the number of samples divided by
    /// [`channels`](Stream::channels). It may be 0: output lags input by a short look-ahead.
    pub fn read(&mut self, samples: &mut [f32]) -> usize {
        let max = clamp_frames(samples.len() / self.channels);
        unsafe { speechwarp_read(self.raw.as_ptr(), samples.as_mut_ptr(), max) as usize }
    }

    /// Takes processed output as 16-bit samples. See [`read`](Stream::read).
    pub fn read_i16(&mut self, samples: &mut [i16]) -> usize {
        let max = clamp_frames(samples.len() / self.channels);
        unsafe { speechwarp_read_i16(self.raw.as_ptr(), samples.as_mut_ptr(), max) as usize }
    }

    /// Frames of output ready to read.
    pub fn available(&self) -> usize {
        unsafe { speechwarp_available(self.raw.as_ptr()) as usize }
    }

    /// Processes everything written so far, at the end of the input. Read until empty afterwards. Writing
    /// more starts a new stretch of audio, and [`position`](Stream::position) carries on counting.
    pub fn flush(&mut self) -> Result<(), Error> {
        check(unsafe { speechwarp_flush(self.raw.as_ptr()) })
    }

    /// Discards all buffered input and output, keeping the speed and nonlinear settings, and starts
    /// [`position`](Stream::position) again from zero. Use after seeking.
    pub fn reset(&mut self) {
        unsafe { speechwarp_reset(self.raw.as_ptr()) }
    }

    /// The input frame, counted from creation or the last [`reset`](Stream::reset), that the next output
    /// frame to be read was made from. This is how a player maps what is being heard back to a place in the
    /// source.
    ///
    /// It never goes backwards, it is approximate (within about 0.05 s of input), and once everything after
    /// a [`flush`](Stream::flush) has been read it equals the number of frames written.
    pub fn position(&self) -> u64 {
        unsafe { speechwarp_position(self.raw.as_ptr()) as u64 }
    }

    fn whole_frames(&self, samples: usize) -> Result<c_int, Error> {
        if samples % self.channels != 0 {
            return Err(Error::PartialFrame);
        }
        // More than c_int frames in one slice cannot be passed in one call.
        c_int::try_from(samples / self.channels).map_err(|_| Error::OutOfMemory)
    }
}

impl Drop for Stream {
    fn drop(&mut self) {
        unsafe { speechwarp_destroy(self.raw.as_ptr()) }
    }
}

impl fmt::Debug for Stream {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("Stream")
            .field("sample_rate", &self.sample_rate)
            .field("channels", &self.channels)
            .field("speed", &self.speed())
            .field("nonlinear", &self.nonlinear())
            .finish()
    }
}

fn check(result: c_int) -> Result<(), Error> {
    if result != 0 {
        Ok(())
    } else {
        Err(Error::OutOfMemory)
    }
}

fn clamp_frames(frames: usize) -> c_int {
    frames.min(c_int::MAX as usize) as c_int
}
