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
    fn speechwarp_set_pause_cap(stream: *mut Raw, seconds: f32);
    fn speechwarp_get_pause_cap(stream: *const Raw) -> f32;
    fn speechwarp_set_keep_speed(stream: *mut Raw, enabled: c_int);
    fn speechwarp_get_keep_speed(stream: *const Raw) -> c_int;
    fn speechwarp_set_speed_floor(stream: *mut Raw, fraction: f32);
    fn speechwarp_get_speed_floor(stream: *const Raw) -> f32;
    fn speechwarp_set_rhythm_gap(stream: *mut Raw, seconds: f32);
    fn speechwarp_get_rhythm_gap(stream: *const Raw) -> f32;
    fn speechwarp_set_rhythm_rate(stream: *mut Raw, per_second: f32);
    fn speechwarp_get_rhythm_rate(stream: *const Raw) -> f32;
    fn speechwarp_syllable_rate(stream: *const Raw) -> f64;
    fn speechwarp_set_heard_pause(stream: *mut Raw, seconds: f32, from_speed: f32);
    fn speechwarp_get_heard_pause(stream: *const Raw) -> f32;
    fn speechwarp_get_heard_pause_from(stream: *const Raw) -> f32;
    fn speechwarp_set_floor_blend(stream: *mut Raw, fraction: f32, from_speed: f32, full_speed: f32);
    fn speechwarp_get_floor_blend(stream: *const Raw) -> f32;
    fn speechwarp_get_floor_blend_from(stream: *const Raw) -> f32;
    fn speechwarp_get_floor_blend_full(stream: *const Raw) -> f32;
    fn speechwarp_write(stream: *mut Raw, samples: *const f32, frames: c_int) -> c_int;
    fn speechwarp_write_i16(stream: *mut Raw, samples: *const i16, frames: c_int) -> c_int;
    fn speechwarp_read(stream: *mut Raw, samples: *mut f32, max_frames: c_int) -> c_int;
    fn speechwarp_read_i16(stream: *mut Raw, samples: *mut i16, max_frames: c_int) -> c_int;
    fn speechwarp_available(stream: *const Raw) -> c_int;
    fn speechwarp_flush(stream: *mut Raw) -> c_int;
    fn speechwarp_reset(stream: *mut Raw);
    fn speechwarp_position(stream: *const Raw) -> i64;
}

#[repr(C)]
struct RawSyllables {
    _private: [u8; 0],
}

#[repr(C)]
struct RawTrainer {
    _private: [u8; 0],
}

#[repr(C)]
struct RawTrials {
    _private: [u8; 0],
}

extern "C" {
    fn speechwarp_syllables_create(sample_rate: c_int, channels: c_int) -> *mut RawSyllables;
    fn speechwarp_syllables_destroy(counter: *mut RawSyllables);
    fn speechwarp_syllables_write(counter: *mut RawSyllables, samples: *const f32, frames: c_int) -> c_int;
    fn speechwarp_syllables_write_i16(counter: *mut RawSyllables, samples: *const i16, frames: c_int) -> c_int;
    fn speechwarp_syllables_rate(counter: *const RawSyllables, window_seconds: f64, minimum_seconds: f64) -> f64;
    fn speechwarp_syllables_reset(counter: *mut RawSyllables);

    fn speechwarp_trainer_create(seed: u64) -> *mut RawTrainer;
    fn speechwarp_trainer_destroy(trainer: *mut RawTrainer);
    fn speechwarp_trainer_set_weight(trainer: *mut RawTrainer, kind: c_int, weight: f64);
    fn speechwarp_trainer_get_weight(trainer: *const RawTrainer, kind: c_int) -> f64;
    fn speechwarp_trainer_set_param(trainer: *mut RawTrainer, param: c_int, value: f64);
    fn speechwarp_trainer_get_param(trainer: *const RawTrainer, param: c_int) -> f64;
    fn speechwarp_trainer_add_measure(
        trainer: *mut RawTrainer,
        kind: c_int,
        score: f64,
        items: f64,
        rate: f64,
        time: f64,
    ) -> c_int;
    fn speechwarp_trainer_test_begin(trainer: *mut RawTrainer, prior_rate: f64, time: f64);
    fn speechwarp_trainer_test_rate(trainer: *mut RawTrainer) -> f64;
    fn speechwarp_trainer_test_done(trainer: *const RawTrainer) -> c_int;
    fn speechwarp_trainer_test_end(trainer: *mut RawTrainer, time: f64) -> f64;
    fn speechwarp_trainer_threshold(trainer: *const RawTrainer) -> f64;
    fn speechwarp_trainer_threshold_low(trainer: *const RawTrainer) -> f64;
    fn speechwarp_trainer_threshold_high(trainer: *const RawTrainer) -> f64;
    fn speechwarp_trainer_session_begin(trainer: *mut RawTrainer, plan: c_int, time: f64);
    fn speechwarp_trainer_session_rate(trainer: *mut RawTrainer, time: f64) -> f64;
    fn speechwarp_trainer_session_end(trainer: *mut RawTrainer, listening_hours: f64, time: f64) -> c_int;
    fn speechwarp_trainer_add_retention(
        trainer: *mut RawTrainer,
        session: c_int,
        score: f64,
        items: f64,
        delay_seconds: f64,
        time: f64,
    ) -> c_int;
    fn speechwarp_trainer_next_plan(trainer: *mut RawTrainer) -> c_int;
    fn speechwarp_trainer_plan_effect(trainer: *const RawTrainer, plan: c_int) -> f64;
    fn speechwarp_trainer_plan_effect_sd(trainer: *const RawTrainer, plan: c_int) -> f64;
    fn speechwarp_trainer_plan_retention(trainer: *const RawTrainer, plan: c_int) -> f64;
    fn speechwarp_trainer_plan_retention_sd(trainer: *const RawTrainer, plan: c_int) -> f64;
    fn speechwarp_trainer_plan_sessions(trainer: *const RawTrainer, plan: c_int) -> c_int;
    fn speechwarp_trainer_plan_best_probability(trainer: *const RawTrainer, plan: c_int) -> f64;
    fn speechwarp_trainer_trend(trainer: *const RawTrainer) -> f64;
    fn speechwarp_trainer_trend_sd(trainer: *const RawTrainer) -> f64;

    fn speechwarp_trials_create(seed: u64) -> *mut RawTrials;
    fn speechwarp_trials_destroy(trials: *mut RawTrials);
    fn speechwarp_trials_add_setting(trials: *mut RawTrials) -> c_int;
    fn speechwarp_trials_add_value(trials: *mut RawTrials, setting: c_int, value: f64) -> c_int;
    fn speechwarp_trials_set_available(trials: *mut RawTrials, setting: c_int, available: c_int);
    fn speechwarp_trials_add(
        trials: *mut RawTrials,
        setting: c_int,
        speed: f64,
        first_value: f64,
        second_value: f64,
        first_score: f64,
        second_score: f64,
        preferred: c_int,
    ) -> c_int;
    fn speechwarp_trials_next(trials: *mut RawTrials, speed: f64) -> c_int;
    fn speechwarp_trials_next_first(trials: *const RawTrials) -> f64;
    fn speechwarp_trials_next_second(trials: *const RawTrials) -> f64;
    fn speechwarp_trials_won(trials: *const RawTrials, setting: c_int, speed: f64, value: c_int) -> c_int;
    fn speechwarp_trials_lost(trials: *const RawTrials, setting: c_int, speed: f64, value: c_int) -> c_int;
    fn speechwarp_trials_tied(trials: *const RawTrials, setting: c_int, speed: f64, value: c_int) -> c_int;
    fn speechwarp_trials_heard(trials: *const RawTrials, setting: c_int, speed: f64, value: c_int) -> c_int;
    fn speechwarp_trials_mean_score(trials: *const RawTrials, setting: c_int, speed: f64, value: c_int) -> f64;
    fn speechwarp_trials_winner(trials: *const RawTrials, setting: c_int, speed: f64) -> c_int;
    fn speechwarp_trials_set_confidence(trials: *mut RawTrials, confidence: f64);
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

/// The version of the C library, such as "0.3.4".
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

    // Options for very high speeds (5x to 8x). All off by default; out-of-range values are clamped and NaN
    // is ignored. See docs/how-it-works.md.

    /// The pause cap, in seconds of input; 0 is off.
    pub fn pause_cap(&self) -> f32 {
        unsafe { speechwarp_get_pause_cap(self.raw.as_ptr()) }
    }

    /// Shortens every pause to at most this many seconds of input before speeding up, so that the speed is
    /// spent on words. 0 (the default) is off. Sensible: 0.04 to 0.2. Allowed: 0, or 0.01 to 1.
    /// [`position`](Stream::position) counts the frames left out.
    pub fn set_pause_cap(&mut self, seconds: f32) {
        unsafe { speechwarp_set_pause_cap(self.raw.as_ptr(), seconds) }
    }

    /// Whether the overall speed is kept while the pause cap or rhythm is on.
    pub fn keep_speed(&self) -> bool {
        unsafe { speechwarp_get_keep_speed(self.raw.as_ptr()) != 0 }
    }

    /// While the pause cap or rhythm is on, keeps the overall speed (true, the default): time saved in pauses
    /// is spent playing the words slower, never below 1x, and time spent in gaps is made up by playing them
    /// faster. When false, trimmed pauses make playback faster than the speed.
    pub fn set_keep_speed(&mut self, enabled: bool) {
        unsafe { speechwarp_set_keep_speed(self.raw.as_ptr(), enabled as c_int) }
    }

    /// The speed floor, as a fraction of the speed; 0 is off.
    pub fn speed_floor(&self) -> f32 {
        unsafe { speechwarp_get_speed_floor(self.raw.as_ptr()) }
    }

    /// No 10 ms block of speech plays slower than this fraction of the speed: at 8x, 0.5 keeps every block at
    /// 4x or more. 0 (the default) is off; 1 is the same as linear. Sensible: 0.3 to 0.7. Applies only with
    /// nonlinear speed-up above 1x.
    pub fn set_speed_floor(&mut self, fraction: f32) {
        unsafe { speechwarp_set_speed_floor(self.raw.as_ptr(), fraction) }
    }

    /// The rhythm gap, in seconds; 0 is off.
    pub fn rhythm_gap(&self) -> f32 {
        unsafe { speechwarp_get_rhythm_gap(self.raw.as_ptr()) }
    }

    /// Puts this many seconds of silence into the output [`rhythm_rate`](Stream::rhythm_rate) times a second,
    /// at the quietest point nearby, which can help the listener keep up at very high speeds. 0 (the default)
    /// is off. Sensible: 0.02 to 0.06. Allowed: 0, or 0.005 to 0.2. [`position`](Stream::position) holds
    /// still during a gap.
    pub fn set_rhythm_gap(&mut self, seconds: f32) {
        unsafe { speechwarp_set_rhythm_gap(self.raw.as_ptr(), seconds) }
    }

    /// Rhythm gaps a second of output.
    pub fn rhythm_rate(&self) -> f32 {
        unsafe { speechwarp_get_rhythm_rate(self.raw.as_ptr()) }
    }

    /// Sets the rhythm gaps a second of output, 1 to 16; default 5. Sensible: 4 to 8. Zero, negative numbers
    /// and NaN are ignored.
    pub fn set_rhythm_rate(&mut self, per_second: f32) {
        unsafe { speechwarp_set_rhythm_rate(self.raw.as_ptr(), per_second) }
    }

    /// The `seconds` last given to [`set_heard_pause`](Stream::set_heard_pause), also while the rule is off.
    pub fn heard_pause(&self) -> f32 {
        unsafe { speechwarp_get_heard_pause(self.raw.as_ptr()) }
    }

    /// The `from_speed` last given to [`set_heard_pause`](Stream::set_heard_pause).
    pub fn heard_pause_from(&self) -> f32 {
        unsafe { speechwarp_get_heard_pause_from(self.raw.as_ptr()) }
    }

    /// Keeps each pause about `seconds` long in the output, from `from_speed` upward. The pause cap in force
    /// is `seconds` times the current speed, clamped to 0.03 to 0.4 s of input; below `from_speed` pauses are
    /// left alone.
    ///
    /// `seconds`: 0 turns the rule off (and the pause cap with it), otherwise 0.002 to 0.4. `from_speed`: 1 to
    /// 20. Sensible: 0.015 to 0.06 heard, from 3x. Setting [`set_pause_cap`](Stream::set_pause_cap) turns the
    /// rule off. Out-of-range values are clamped; if any argument is NaN the call is ignored.
    pub fn set_heard_pause(&mut self, seconds: f32, from_speed: f32) {
        unsafe { speechwarp_set_heard_pause(self.raw.as_ptr(), seconds, from_speed) }
    }

    /// The `fraction` last given to [`set_floor_blend`](Stream::set_floor_blend), also while the rule is off.
    pub fn floor_blend(&self) -> f32 {
        unsafe { speechwarp_get_floor_blend(self.raw.as_ptr()) }
    }

    /// The `from_speed` last given to [`set_floor_blend`](Stream::set_floor_blend).
    pub fn floor_blend_from(&self) -> f32 {
        unsafe { speechwarp_get_floor_blend_from(self.raw.as_ptr()) }
    }

    /// The `full_speed` last given to [`set_floor_blend`](Stream::set_floor_blend).
    pub fn floor_blend_full(&self) -> f32 {
        unsafe { speechwarp_get_floor_blend_full(self.raw.as_ptr()) }
    }

    /// The speed floor in force is 0 below `from_speed`, rises linearly to `fraction` at `full_speed`, and
    /// stays at `fraction` above it, so that a speed ramp never changes the sound in a jump.
    ///
    /// `fraction`: 0 turns the rule off (and the floor with it), otherwise up to 1. Speeds 1 to 20; if
    /// `full_speed` is not above `from_speed` the floor steps to `fraction` at `from_speed`. Sensible: 0.5
    /// from 4x, full at 6x. Setting [`set_speed_floor`](Stream::set_speed_floor) turns the rule off.
    /// Out-of-range values are clamped; if any argument is NaN the call is ignored.
    pub fn set_floor_blend(&mut self, fraction: f32, from_speed: f32, full_speed: f32) {
        unsafe { speechwarp_set_floor_blend(self.raw.as_ptr(), fraction, from_speed, full_speed) }
    }

    /// Syllables a second in the input, pauses included, over about the last 60 s written; `None` until 10 s
    /// have been written since creation or [`reset`](Stream::reset). Multiply by the speed for the rate heard.
    /// An estimate, typically within about 10%.
    pub fn syllable_rate(&self) -> Option<f64> {
        let rate = unsafe { speechwarp_syllable_rate(self.raw.as_ptr()) };
        (rate >= 0.0).then_some(rate)
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
        whole_frames(samples, self.channels)
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

fn whole_frames(samples: usize, channels: usize) -> Result<c_int, Error> {
    if samples % channels != 0 {
        return Err(Error::PartialFrame);
    }
    // More than c_int frames in one slice cannot be passed in one call.
    c_int::try_from(samples / channels).map_err(|_| Error::OutOfMemory)
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

/// Syllables a second in audio that does not go through a [`Stream`].
///
/// The estimator behind [`Stream::syllable_rate`], on its own, for a player that uses some other speed-up or
/// measures a file. Given the same input it reports the same rate as a stream does with a window of 60 s and a
/// minimum of 10 s. Float input is converted to 16-bit exactly as the stream converts it.
pub struct SyllableCounter {
    raw: NonNull<RawSyllables>,
    channels: usize,
}

// SAFETY: the C counter owns all its memory and has no thread affinity.
unsafe impl Send for SyllableCounter {}

impl SyllableCounter {
    /// Creates a counter. The sample rate must be 4000 to 384000 and there may be 1 to 32 channels.
    pub fn new(sample_rate: u32, channels: u32) -> Result<SyllableCounter, Error> {
        if !(4000..=384000).contains(&sample_rate) || !(1..=32).contains(&channels) {
            return Err(Error::UnsupportedFormat);
        }
        // SAFETY: the arguments have been checked, and a null result is handled.
        let raw = unsafe { speechwarp_syllables_create(sample_rate as c_int, channels as c_int) };
        Ok(SyllableCounter { raw: NonNull::new(raw).ok_or(Error::OutOfMemory)?, channels: channels as usize })
    }

    /// Adds interleaved input.
    pub fn write(&mut self, samples: &[f32]) -> Result<(), Error> {
        let frames = whole_frames(samples.len(), self.channels)?;
        // SAFETY: `raw` is live, and the slice holds `frames` whole frames.
        check(unsafe { speechwarp_syllables_write(self.raw.as_ptr(), samples.as_ptr(), frames) })
    }

    /// Adds interleaved 16-bit input.
    pub fn write_i16(&mut self, samples: &[i16]) -> Result<(), Error> {
        let frames = whole_frames(samples.len(), self.channels)?;
        // SAFETY: as in `write`.
        check(unsafe { speechwarp_syllables_write_i16(self.raw.as_ptr(), samples.as_ptr(), frames) })
    }

    /// Syllables a second over the last `window_seconds` written (or all of it, if less), or `None` until
    /// `minimum_seconds` have been written. The window is clamped to 1 to 120 s and the minimum to 0 up to the
    /// window. [`Stream::syllable_rate`] uses 60 and 10.
    pub fn rate(&self, window_seconds: f64, minimum_seconds: f64) -> Option<f64> {
        let rate = unsafe { speechwarp_syllables_rate(self.raw.as_ptr(), window_seconds, minimum_seconds) };
        (rate >= 0.0).then_some(rate)
    }

    /// [`rate`](SyllableCounter::rate) with a window of 60 s and a minimum of 10 s.
    pub fn rate_default(&self) -> Option<f64> {
        self.rate(60.0, 10.0)
    }

    /// Forgets everything written.
    pub fn reset(&mut self) {
        unsafe { speechwarp_syllables_reset(self.raw.as_ptr()) }
    }
}

impl Drop for SyllableCounter {
    fn drop(&mut self) {
        unsafe { speechwarp_syllables_destroy(self.raw.as_ptr()) }
    }
}

impl fmt::Debug for SyllableCounter {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("SyllableCounter").field("channels", &self.channels).finish()
    }
}

/// What a score in 0..1 measures.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
#[repr(i32)]
pub enum TrainerMeasure {
    /// Share of the words said back correctly from a sentence heard once.
    Intelligibility = 0,
    /// Share right on "was this sentence in what you just heard?" items (the sentence verification
    /// technique): originals and paraphrases against changed-meaning and unrelated sentences. Chance is 0.5.
    Verification = 1,
    /// Verification items about a session's material, answered after a delay: see
    /// [`ListenerTrainer::add_retention`]. Not used for thresholds.
    Retention = 2,
    /// The listener's own "how well did you follow?", 1 to 5 scaled to 0..1 as (r - 1) / 4. Subjective, so
    /// weighted less by default.
    Rating = 3,
}

/// Session plans: how the rate moves during a session.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
#[repr(i32)]
pub enum TrainerPlan {
    /// The threshold plus a margin, all session.
    Steady = 0,
    /// Start below the threshold and step up to threshold plus margin.
    Ramp = 1,
    /// Alternate periods above and below the threshold.
    Interval = 2,
    /// Move up or down after each in-session check, to stay at the target.
    Tracking = 3,
}

impl TrainerPlan {
    /// Every plan, in order.
    pub const ALL: [TrainerPlan; 4] =
        [TrainerPlan::Steady, TrainerPlan::Ramp, TrainerPlan::Interval, TrainerPlan::Tracking];

    fn from_c(value: c_int) -> TrainerPlan {
        match value {
            1 => TrainerPlan::Ramp,
            2 => TrainerPlan::Interval,
            3 => TrainerPlan::Tracking,
            _ => TrainerPlan::Steady,
        }
    }
}

/// Tunable numbers of the trainer, with their defaults. Set with [`ListenerTrainer::set_param`].
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
#[repr(i32)]
pub enum TrainerParam {
    /// Share understood that defines the threshold: 0.75 (0.5 to 0.95).
    Target = 0,
    /// Steady and ramp: aim this fraction above the threshold: 0.10.
    Margin = 1,
    /// Ramp: start at this fraction of the target rate: 0.8.
    RampStart = 2,
    /// Ramp: step by this fraction of the target rate: 0.02.
    RampStep = 3,
    /// Ramp: minutes between steps: 2.
    RampMinutes = 4,
    /// Interval: this fraction above, then below, the threshold: 0.15.
    IntervalSpread = 5,
    /// Interval: minutes in each period: 10.
    IntervalMinutes = 6,
    /// Tracking: ln(rate) moves by gain x (score - target) per check: 0.4.
    TrackingGain = 7,
    /// Plans: threshold gain an hour (as a fraction, before practice slows it) worth a whole unit of
    /// retention: 0.2, so 10 points of retention are worth 2% an hour.
    RetentionCost = 8,
    /// Threshold test: most presentations: 40.
    TestMax = 9,
    /// Threshold test: done when the 95% interval's high / low is below this: 1.25.
    TestPrecision = 10,
}

/// Logic for training a listener to follow faster speech: no audio, no clock, no storage.
///
/// The caller passes plain numbers in (scores, rates, its own timestamps) and gets rates and plans out.
/// Deterministic: two trainers created with the same seed and given the same calls give the same answers, so a
/// caller keeps its own log of calls and replays it into a new trainer to restore state.
///
/// The unit of rate everywhere is syllables a second heard: the source's syllable rate times the speed. Times
/// are seconds on any clock the caller likes (Unix time, say), and only differences are used. NaN arguments
/// are ignored.
///
/// The protocol: a threshold test ([`test_begin`](ListenerTrainer::test_begin) ...
/// [`test_end`](ListenerTrainer::test_end)), [`session_begin`](ListenerTrainer::session_begin), listening with
/// a check every ten minutes or so ([`add_measure`](ListenerTrainer::add_measure)), a second test,
/// [`session_end`](ListenerTrainer::session_end); retention items a day and a week later
/// ([`add_retention`](ListenerTrainer::add_retention)).
pub struct ListenerTrainer {
    raw: NonNull<RawTrainer>,
}

// SAFETY: the C trainer owns all its memory and has no thread affinity.
unsafe impl Send for ListenerTrainer {}

impl ListenerTrainer {
    /// Creates a trainer with a seed for its random choices.
    pub fn new(seed: u64) -> Result<ListenerTrainer, Error> {
        // SAFETY: a null result is handled.
        let raw = unsafe { speechwarp_trainer_create(seed) };
        Ok(ListenerTrainer { raw: NonNull::new(raw).ok_or(Error::OutOfMemory)? })
    }

    /// How much a measure of each kind counts, per item, against the others.
    pub fn weight(&self, kind: TrainerMeasure) -> f64 {
        unsafe { speechwarp_trainer_get_weight(self.raw.as_ptr(), kind as c_int) }
    }

    /// Sets how much a measure of each kind counts, per item, against the others. Defaults: intelligibility
    /// 0.5 (a word, and the words of one sentence are not independent), verification 1, retention 1, rating
    /// 0.3 (subjective). 0 ignores the kind; negative and NaN are ignored.
    pub fn set_weight(&mut self, kind: TrainerMeasure, weight: f64) {
        unsafe { speechwarp_trainer_set_weight(self.raw.as_ptr(), kind as c_int, weight) }
    }

    /// The value of a tunable number.
    pub fn param(&self, param: TrainerParam) -> f64 {
        unsafe { speechwarp_trainer_get_param(self.raw.as_ptr(), param as c_int) }
    }

    /// Sets a tunable number; see [`TrainerParam`] for the meanings and defaults.
    pub fn set_param(&mut self, param: TrainerParam, value: f64) {
        unsafe { speechwarp_trainer_set_param(self.raw.as_ptr(), param as c_int, value) }
    }

    /// Adds a score: `kind` (not [`Retention`](TrainerMeasure::Retention)), `score` 0..1, from `items` items
    /// (for a sentence repeated back, the number of words scored; for verification, the number of questions;
    /// for a rating, 1), heard at `rate` syllables a second, at `time`. During a threshold test it updates the
    /// estimate; during a session it is an in-session check, and the tracking plan reacts to it. Returns false
    /// if an argument is invalid.
    pub fn add_measure(&mut self, kind: TrainerMeasure, score: f64, items: f64, rate: f64, time: f64) -> bool {
        unsafe { speechwarp_trainer_add_measure(self.raw.as_ptr(), kind as c_int, score, items, rate, time) != 0 }
    }

    /// Starts a threshold test: it estimates the rate understood [`Target`](TrainerParam::Target) (75%) of the
    /// time, lapses aside, by the psi method (Kontsevich and Tyler, 1999). The prior is log-normal around
    /// `prior_rate` or, if that is 0, around the last estimate, or failing that around 10 syllables a second,
    /// within 3 to 60.
    pub fn test_begin(&mut self, prior_rate: f64, time: f64) {
        unsafe { speechwarp_trainer_test_begin(self.raw.as_ptr(), prior_rate, time) }
    }

    /// The rate to present next, in syllables a second.
    pub fn test_rate(&mut self) -> f64 {
        unsafe { speechwarp_trainer_test_rate(self.raw.as_ptr()) }
    }

    /// True once the 95% interval is narrower than [`TestPrecision`](TrainerParam::TestPrecision) (at least 8
    /// presentations) or [`TestMax`](TrainerParam::TestMax) presentations have been scored; false otherwise or
    /// if no test is running.
    pub fn test_done(&self) -> bool {
        unsafe { speechwarp_trainer_test_done(self.raw.as_ptr()) != 0 }
    }

    /// Finishes the test; the estimate becomes the current threshold. Returns it (0 if no test was running).
    pub fn test_end(&mut self, time: f64) -> f64 {
        unsafe { speechwarp_trainer_test_end(self.raw.as_ptr(), time) }
    }

    /// The current estimate (posterior median): of the running test, else of the last one finished; 0 if none.
    pub fn threshold(&self) -> f64 {
        unsafe { speechwarp_trainer_threshold(self.raw.as_ptr()) }
    }

    /// The low end of the 95% interval of [`threshold`](ListenerTrainer::threshold).
    pub fn threshold_low(&self) -> f64 {
        unsafe { speechwarp_trainer_threshold_low(self.raw.as_ptr()) }
    }

    /// The high end of the 95% interval of [`threshold`](ListenerTrainer::threshold).
    pub fn threshold_high(&self) -> f64 {
        unsafe { speechwarp_trainer_threshold_high(self.raw.as_ptr()) }
    }

    /// Begins a session under `plan`.
    pub fn session_begin(&mut self, plan: TrainerPlan, time: f64) {
        unsafe { speechwarp_trainer_session_begin(self.raw.as_ptr(), plan as c_int, time) }
    }

    /// The rate to play at now, under the session's plan, from the threshold at
    /// [`session_begin`](ListenerTrainer::session_begin). 0 if no session.
    pub fn session_rate(&mut self, time: f64) -> f64 {
        unsafe { speechwarp_trainer_session_rate(self.raw.as_ptr(), time) }
    }

    /// Ends the session after `listening_hours` of listening in it. It is recorded for comparing plans if a
    /// threshold test ended after it began. Returns the session's number (0, 1, ...) for
    /// [`add_retention`](ListenerTrainer::add_retention), or `None` if no session was running.
    pub fn session_end(&mut self, listening_hours: f64, time: f64) -> Option<usize> {
        let session = unsafe { speechwarp_trainer_session_end(self.raw.as_ptr(), listening_hours, time) };
        usize::try_from(session).ok()
    }

    /// Adds retention for a recorded session: `score` 0..1 from `items` items, answered `delay_seconds` after
    /// it ended. Returns false if an argument is invalid.
    pub fn add_retention(&mut self, session: usize, score: f64, items: f64, delay_seconds: f64, time: f64) -> bool {
        let session = c_int::try_from(session).unwrap_or(-1);
        unsafe { speechwarp_trainer_add_retention(self.raw.as_ptr(), session, score, items, delay_seconds, time) != 0 }
    }

    /// The plan to run next: a Thompson draw over each recorded session's threshold gain and retention
    /// (advances the random source).
    pub fn next_plan(&mut self) -> TrainerPlan {
        TrainerPlan::from_c(unsafe { speechwarp_trainer_next_plan(self.raw.as_ptr()) })
    }

    /// A plan's estimated threshold gain an hour now, at the practice so far (as a fraction: 0.01 is 1% an
    /// hour). 0 without data.
    pub fn plan_effect(&self, plan: TrainerPlan) -> f64 {
        unsafe { speechwarp_trainer_plan_effect(self.raw.as_ptr(), plan as c_int) }
    }

    /// The standard deviation of [`plan_effect`](ListenerTrainer::plan_effect).
    pub fn plan_effect_sd(&self, plan: TrainerPlan) -> f64 {
        unsafe { speechwarp_trainer_plan_effect_sd(self.raw.as_ptr(), plan as c_int) }
    }

    /// A plan's retention; NaN without data.
    pub fn plan_retention(&self, plan: TrainerPlan) -> f64 {
        unsafe { speechwarp_trainer_plan_retention(self.raw.as_ptr(), plan as c_int) }
    }

    /// The standard deviation of [`plan_retention`](ListenerTrainer::plan_retention); NaN without data.
    pub fn plan_retention_sd(&self, plan: TrainerPlan) -> f64 {
        unsafe { speechwarp_trainer_plan_retention_sd(self.raw.as_ptr(), plan as c_int) }
    }

    /// Sessions recorded for a plan.
    pub fn plan_sessions(&self, plan: TrainerPlan) -> usize {
        unsafe { speechwarp_trainer_plan_sessions(self.raw.as_ptr(), plan as c_int) }.max(0) as usize
    }

    /// The probability that a plan is the best by utility, from a fixed number of draws (does not advance the
    /// random source).
    pub fn plan_best_probability(&self, plan: TrainerPlan) -> f64 {
        unsafe { speechwarp_trainer_plan_best_probability(self.raw.as_ptr(), plan as c_int) }
    }

    /// The trend: H, the hours of listening by which gains have halved (1000 stands for "not slowing").
    pub fn trend(&self) -> f64 {
        unsafe { speechwarp_trainer_trend(self.raw.as_ptr()) }
    }

    /// The uncertainty of [`trend`](ListenerTrainer::trend) as a standard deviation of ln H.
    pub fn trend_sd(&self) -> f64 {
        unsafe { speechwarp_trainer_trend_sd(self.raw.as_ptr()) }
    }
}

impl Drop for ListenerTrainer {
    fn drop(&mut self) {
        unsafe { speechwarp_trainer_destroy(self.raw.as_ptr()) }
    }
}

impl fmt::Debug for ListenerTrainer {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("ListenerTrainer").field("threshold", &self.threshold()).finish()
    }
}

/// One trial chosen by [`BlindTrials::next`].
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Trial {
    /// The setting to compare.
    pub setting: usize,
    /// The value to play first.
    pub first: f64,
    /// The value to play second.
    pub second: f64,
}

/// Designs the listener's own blind A/B comparisons: which setting to compare next at a speed, which two of
/// its values, in what order, and how results add up in each speed band (whole numbers: 4 to 5, 5 to 6, ...).
/// The caller names the settings; here they are numbers. Deterministic for a given seed.
pub struct BlindTrials {
    raw: NonNull<RawTrials>,
}

// SAFETY: the C object owns all its memory and has no thread affinity.
unsafe impl Send for BlindTrials {}

fn index(value: usize) -> c_int {
    c_int::try_from(value).unwrap_or(-1)
}

impl BlindTrials {
    /// Creates a set of trials with a seed for its random choices.
    pub fn new(seed: u64) -> Result<BlindTrials, Error> {
        // SAFETY: a null result is handled.
        let raw = unsafe { speechwarp_trials_create(seed) };
        Ok(BlindTrials { raw: NonNull::new(raw).ok_or(Error::OutOfMemory)? })
    }

    /// Adds a setting; returns its number (0, 1, ...), or `None` if it could not be added.
    pub fn add_setting(&mut self) -> Option<usize> {
        usize::try_from(unsafe { speechwarp_trials_add_setting(self.raw.as_ptr()) }).ok()
    }

    /// Adds a value to compare; returns its number within the setting, or `None` (duplicate, bad setting).
    pub fn add_value(&mut self, setting: usize, value: f64) -> Option<usize> {
        usize::try_from(unsafe { speechwarp_trials_add_value(self.raw.as_ptr(), index(setting), value) }).ok()
    }

    /// Leaves a setting out of [`next`](BlindTrials::next) while false (say, when its method is not
    /// available). Settings are available by default.
    pub fn set_available(&mut self, setting: usize, available: bool) {
        unsafe { speechwarp_trials_set_available(self.raw.as_ptr(), index(setting), available as c_int) }
    }

    /// Records a trial at `speed`: the two values in the order heard, each one's score 0..1, and `preferred`:
    /// -1 the first, 1 the second, 0 neither. Values must be ones added. Returns false if it was rejected.
    #[allow(clippy::too_many_arguments)]
    pub fn add(
        &mut self,
        setting: usize,
        speed: f64,
        first_value: f64,
        second_value: f64,
        first_score: f64,
        second_score: f64,
        preferred: i32,
    ) -> bool {
        unsafe {
            speechwarp_trials_add(
                self.raw.as_ptr(),
                index(setting),
                speed,
                first_value,
                second_value,
                first_score,
                second_score,
                preferred,
            ) != 0
        }
    }

    /// Chooses the next trial at `speed`: the available setting with the fewest trials in its band (ties at
    /// random), its pair of values compared least (ties at random), in random order. `None` if no setting has
    /// two values.
    pub fn next(&mut self, speed: f64) -> Option<Trial> {
        let setting = unsafe { speechwarp_trials_next(self.raw.as_ptr(), speed) };
        let setting = usize::try_from(setting).ok()?;
        Some(Trial {
            setting,
            first: unsafe { speechwarp_trials_next_first(self.raw.as_ptr()) },
            second: unsafe { speechwarp_trials_next_second(self.raw.as_ptr()) },
        })
    }

    /// Comparisons the value (by its number within the setting) won in the band of `speed`.
    pub fn won(&self, setting: usize, speed: f64, value: usize) -> usize {
        unsafe { speechwarp_trials_won(self.raw.as_ptr(), index(setting), speed, index(value)) }.max(0) as usize
    }

    /// Comparisons the value lost in the band of `speed`.
    pub fn lost(&self, setting: usize, speed: f64, value: usize) -> usize {
        unsafe { speechwarp_trials_lost(self.raw.as_ptr(), index(setting), speed, index(value)) }.max(0) as usize
    }

    /// Comparisons the value tied in the band of `speed`.
    pub fn tied(&self, setting: usize, speed: f64, value: usize) -> usize {
        unsafe { speechwarp_trials_tied(self.raw.as_ptr(), index(setting), speed, index(value)) }.max(0) as usize
    }

    /// Trials the value was heard in, in the band of `speed`.
    pub fn heard(&self, setting: usize, speed: f64, value: usize) -> usize {
        unsafe { speechwarp_trials_heard(self.raw.as_ptr(), index(setting), speed, index(value)) }.max(0) as usize
    }

    /// The value's mean score in the band of `speed`, or `None` if never heard.
    pub fn mean_score(&self, setting: usize, speed: f64, value: usize) -> Option<f64> {
        let score = unsafe { speechwarp_trials_mean_score(self.raw.as_ptr(), index(setting), speed, index(value)) };
        (!score.is_nan()).then_some(score)
    }

    /// The value (its number within the setting) with a reliable win in the band of `speed`, or `None`. A
    /// value wins when it has been heard in at least 5 trials, has met every other value in at least 3, and
    /// against each the Bayes factor for "preferred" over "no preference" is at least 1 / (1 - confidence): 20
    /// at the default 0.95. However often this is asked, the chance of ever naming a winner between two values
    /// that are really alike is at most 1 - confidence each way.
    pub fn winner(&self, setting: usize, speed: f64) -> Option<usize> {
        usize::try_from(unsafe { speechwarp_trials_winner(self.raw.as_ptr(), index(setting), speed) }).ok()
    }

    /// Sets the confidence [`winner`](BlindTrials::winner) asks for (default 0.95).
    pub fn set_confidence(&mut self, confidence: f64) {
        unsafe { speechwarp_trials_set_confidence(self.raw.as_ptr(), confidence) }
    }
}

impl Drop for BlindTrials {
    fn drop(&mut self) {
        unsafe { speechwarp_trials_destroy(self.raw.as_ptr()) }
    }
}

impl fmt::Debug for BlindTrials {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("BlindTrials").finish()
    }
}
