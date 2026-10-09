use speechwarp::{
    score_words, BlindTrials, Error, ListenerTrainer, Stream, SyllableCounter, TrainerMeasure, TrainerParam, TrainerPlan, MAX_SPEED,
    MIN_SPEED, WordScore,
};

const RATE: u32 = 22050;

/// Something for the library to chew on: a gliding tone in bursts, with gaps.
fn signal(seconds: f64, channels: usize) -> Vec<f32> {
    let frames = (RATE as f64 * seconds) as usize;
    let mut samples = Vec::with_capacity(frames * channels);
    let mut phase = 0.0f64;
    for i in 0..frames {
        let t = i as f64 / RATE as f64;
        phase += 2.0 * std::f64::consts::PI * (120.0 + 60.0 * (t * 3.0).sin()) / RATE as f64;
        let beat = t % 0.4;
        let envelope = if beat < 0.3 { (std::f64::consts::PI * beat / 0.3).sin() } else { 0.0 };
        let value = 0.4 * envelope * (phase.sin() + 0.5 * (2.0 * phase).sin() + 0.3 * (3.0 * phase).sin());
        samples.extend(std::iter::repeat(value as f32).take(channels));
    }
    samples
}

fn read_all(stream: &mut Stream) -> Vec<f32> {
    let mut out = vec![0.0; stream.available() * stream.channels()];
    assert_eq!(stream.read(&mut out), out.len() / stream.channels());
    out
}

fn speed_up(input: &[f32], speed: f32, nonlinear: f32) -> Vec<f32> {
    let mut stream = Stream::new(RATE, 1).unwrap();
    stream.set_speed(speed);
    stream.set_nonlinear(nonlinear);
    stream.write(input).unwrap();
    stream.flush().unwrap();
    read_all(&mut stream)
}

#[test]
fn version_matches_the_header_and_the_crate() {
    let header = include_str!("../../../include/speechwarp.h");
    assert!(header.contains(&format!("#define SPEECHWARP_VERSION \"{}\"", speechwarp::version())));
    assert_eq!(speechwarp::version(), env!("CARGO_PKG_VERSION"));
}

#[test]
fn defaults_and_arguments() {
    let mut stream = Stream::new(RATE, 2).unwrap();
    assert_eq!((stream.sample_rate(), stream.channels()), (RATE, 2));
    assert_eq!((stream.speed(), stream.nonlinear()), (1.0, 1.0));
    assert_eq!((stream.available(), stream.position()), (0, 0));

    assert_eq!(Stream::new(100, 1).err(), Some(Error::UnsupportedFormat));
    assert_eq!(Stream::new(RATE, 0).err(), Some(Error::UnsupportedFormat));
    assert_eq!(stream.write(&[0.0; 3]), Err(Error::PartialFrame));

    stream.set_speed(1000.0);
    assert_eq!(stream.speed(), MAX_SPEED);
    stream.set_speed(f32::NAN);
    stream.set_speed(-1.0);
    assert_eq!(stream.speed(), MAX_SPEED);
    stream.set_speed(1e-6);
    assert_eq!(stream.speed(), MIN_SPEED);
    stream.set_nonlinear(-3.0);
    assert_eq!(stream.nonlinear(), 0.0);
}

#[test]
fn output_is_shorter_by_the_speed() {
    let input = signal(20.0, 1);
    for (speed, nonlinear) in [(1.0, 1.0), (3.0, 1.0), (3.0, 0.0), (8.0, 1.0)] {
        let output = speed_up(&input, speed, nonlinear);
        let actual = input.len() as f32 / output.len() as f32;
        assert!((actual / speed - 1.0).abs() < 0.1, "speed {speed}: got {actual}");
    }
}

#[test]
fn stereo_and_i16() {
    let input: Vec<i16> = signal(5.0, 2).iter().map(|x| (x * 32767.0) as i16).collect();
    let mut stream = Stream::new(RATE, 2).unwrap();
    stream.set_speed(2.0);
    stream.write_i16(&input).unwrap();
    stream.flush().unwrap();

    let mut buffer = [0i16; 1000 * 2 + 1]; // one sample too many: only whole frames are written
    let mut total = 0;
    loop {
        let frames = stream.read_i16(&mut buffer);
        if frames == 0 {
            break;
        }
        assert!(frames <= 1000);
        assert!(buffer[..frames * 2].chunks(2).all(|frame| frame[0] == frame[1]));
        total += frames;
    }
    assert!(((input.len() / 2) as f64 / total as f64 - 2.0).abs() < 0.2);
}

#[test]
fn position_follows_what_is_read() {
    let input = signal(20.0, 1);
    let mut stream = Stream::new(RATE, 1).unwrap();
    stream.set_speed(4.0);
    let mut buffer = [0.0f32; 512];
    let (mut written, mut last) = (0, 0);
    loop {
        if written < input.len() && stream.available() < 512 {
            let end = (written + 2000).min(input.len());
            stream.write(&input[written..end]).unwrap();
            written = end;
            if written == input.len() {
                stream.flush().unwrap();
            }
            continue;
        }
        let position = stream.position();
        assert!(position >= last && position <= written as u64);
        last = position;
        if stream.read(&mut buffer) == 0 {
            break;
        }
    }
    assert_eq!(stream.position(), input.len() as u64);
}

#[test]
fn reset_discards_everything() {
    let input = signal(5.0, 1);
    let mut stream = Stream::new(RATE, 1).unwrap();
    stream.set_speed(3.0);
    stream.write(&input).unwrap();
    assert!(stream.available() > 0);
    stream.reset();
    assert_eq!((stream.available(), stream.position(), stream.speed()), (0, 0, 3.0));
    stream.write(&input).unwrap();
    stream.flush().unwrap();
    assert_eq!(read_all(&mut stream), speed_up(&input, 3.0, 1.0));
}

#[test]
fn a_stream_can_move_to_another_thread() {
    let mut stream = Stream::new(RATE, 1).unwrap();
    stream.set_speed(2.0);
    let frames = std::thread::spawn(move || {
        stream.write(&signal(2.0, 1)).unwrap();
        stream.flush().unwrap();
        stream.available()
    })
    .join()
    .unwrap();
    assert!(frames > 0);
}

#[test]
fn high_speed_options() {
    let mut stream = Stream::new(RATE, 1).unwrap();
    assert_eq!((stream.pause_cap(), stream.keep_speed(), stream.speed_floor()), (0.0, true, 0.0));
    assert_eq!((stream.rhythm_gap(), stream.rhythm_rate(), stream.syllable_rate()), (0.0, 5.0, None));
    stream.set_pause_cap(5.0);
    stream.set_speed_floor(0.5);
    stream.set_rhythm_gap(0.04);
    stream.set_rhythm_rate(100.0);
    stream.set_keep_speed(false);
    assert_eq!((stream.pause_cap(), stream.speed_floor(), stream.rhythm_rate()), (1.0, 0.5, 16.0));
    assert!((stream.rhythm_gap() - 0.04).abs() < 1e-6 && !stream.keep_speed());
}

#[test]
fn pause_cap_shortens_and_position_reaches_the_end() {
    let input = signal(12.0, 1);
    let mut stream = Stream::new(RATE, 1).unwrap();
    stream.set_nonlinear(0.0);
    stream.set_pause_cap(0.03);
    stream.set_keep_speed(false);
    stream.write(&input).unwrap();
    stream.flush().unwrap();
    assert!((read_all(&mut stream).len() as f64) < input.len() as f64 * 0.9);
    assert_eq!(stream.position(), input.len() as u64);
    assert!(stream.syllable_rate().is_some());
}

fn near(a: f64, b: f64) -> bool {
    (a - b).abs() < 1e-5 * b.abs().max(1.0)
}

#[test]
fn heard_pause_and_floor_blend() {
    let mut stream = Stream::new(RATE, 1).unwrap();
    assert_eq!((stream.heard_pause(), stream.floor_blend()), (0.0, 0.0));
    stream.set_heard_pause(0.03, 3.0);
    assert!(near(stream.heard_pause() as f64, 0.03) && stream.heard_pause_from() == 3.0);
    stream.set_speed(5.0);
    assert!(near(stream.pause_cap() as f64, 0.15));
    stream.set_speed(2.0); // below from_speed
    assert_eq!(stream.pause_cap(), 0.0);
    stream.set_speed(5.0);
    stream.set_pause_cap(0.1); // a fixed value turns the rule off
    assert!(near(stream.pause_cap() as f64, 0.1));
    stream.set_floor_blend(0.5, 4.0, 6.0);
    assert_eq!((stream.floor_blend(), stream.floor_blend_from(), stream.floor_blend_full()), (0.5, 4.0, 6.0));
    assert!(near(stream.speed_floor() as f64, 0.25));
    stream.set_speed(8.0);
    assert!(near(stream.speed_floor() as f64, 0.5));
    stream.set_speed(3.0);
    assert_eq!(stream.speed_floor(), 0.0);
    stream.set_heard_pause(0.03, 3.0);
    stream.set_heard_pause(f32::NAN, 3.0); // ignored
    assert!(near(stream.heard_pause() as f64, 0.03));
}

#[test]
fn syllable_counter_matches_the_stream() {
    let input = signal(30.0, 1);
    let split = RATE as usize * 5;
    let mut counter = SyllableCounter::new(RATE, 1).unwrap();
    let mut stream = Stream::new(RATE, 1).unwrap();
    counter.write(&input[..split]).unwrap();
    stream.write(&input[..split]).unwrap();
    assert_eq!((counter.rate_default(), stream.syllable_rate()), (None, None));
    counter.write(&input[split..]).unwrap();
    stream.write(&input[split..]).unwrap();
    let rate = counter.rate_default().unwrap();
    assert!(rate > 0.0);
    assert!(near(rate, stream.syllable_rate().unwrap()));
    assert!(counter.rate(30.0, 5.0).is_some());
    let shorts: Vec<i16> = input.iter().map(|v| (v * 32767.0) as i16).collect();
    counter.write_i16(&shorts).unwrap();
    counter.reset();
    assert_eq!(counter.rate_default(), None);
    assert_eq!(counter.write(&[0.0; 3]), Ok(()));
    assert!(SyllableCounter::new(100, 1).is_err());
    let mut stereo = SyllableCounter::new(RATE, 2).unwrap();
    assert_eq!(stereo.write(&[0.0; 3]), Err(Error::PartialFrame));
}

#[test]
fn trainer_threshold_test() {
    let mut trainer = ListenerTrainer::new(7).unwrap();
    assert_eq!(trainer.threshold(), 0.0);
    assert!(!trainer.test_done());
    trainer.test_begin(12.0, 0.0);
    for step in 0..40 {
        let rate = trainer.test_rate();
        assert!((3.0..=60.0).contains(&rate));
        let score = if rate < 14.0 { 0.95 } else { 0.4 };
        assert!(trainer.add_measure(TrainerMeasure::Intelligibility, score, 8.0, rate, step as f64));
        if trainer.test_done() {
            break;
        }
    }
    let threshold = trainer.test_end(100.0);
    assert!(threshold > 0.0 && threshold == trainer.threshold());
    assert!(trainer.threshold_low() < threshold && threshold < trainer.threshold_high());
    assert!(!trainer.add_measure(TrainerMeasure::Intelligibility, 2.0, 8.0, 10.0, 0.0)); // score out of range
}

#[test]
fn trainer_settings_and_plans() {
    let mut trainer = ListenerTrainer::new(0).unwrap();
    assert!(near(trainer.param(TrainerParam::Target), 0.75));
    trainer.set_param(TrainerParam::Margin, 0.2);
    assert!(near(trainer.param(TrainerParam::Margin), 0.2));
    assert!(near(trainer.weight(TrainerMeasure::Rating), 0.3));
    trainer.set_weight(TrainerMeasure::Rating, 0.5);
    assert_eq!(trainer.weight(TrainerMeasure::Rating), 0.5);
    trainer.set_param(TrainerParam::Margin, 0.1);
    // With no data every plan has no effect and no retention.
    for plan in TrainerPlan::ALL {
        assert_eq!(trainer.plan_effect(plan), 0.0);
        assert!(trainer.plan_retention(plan).is_nan() && trainer.plan_retention_sd(plan).is_nan());
        assert_eq!(trainer.plan_sessions(plan), 0);
        assert!(trainer.plan_effect_sd(plan) >= 0.0);
        assert!((0.0..=1.0).contains(&trainer.plan_best_probability(plan)));
    }
    assert!(trainer.trend() > 0.0 && trainer.trend_sd() >= 0.0);
    assert_eq!(trainer.session_rate(0.0), 0.0); // no session
    trainer.test_begin(10.0, 0.0);
    for step in 0..20 {
        let rate = trainer.test_rate();
        let score = if rate < 12.0 { 0.9 } else { 0.55 };
        trainer.add_measure(TrainerMeasure::Verification, score, 6.0, rate, step as f64);
    }
    let threshold = trainer.test_end(50.0);
    trainer.session_begin(TrainerPlan::Steady, 100.0);
    assert!(near(trainer.session_rate(100.0), threshold * 1.1));
    assert!(trainer.session_end(0.5, 3700.0).is_some());
    assert_eq!(trainer.session_end(0.5, 3800.0), None); // no session running
    trainer.session_begin(TrainerPlan::Steady, 4000.0);
    trainer.test_begin(0.0, 4100.0);
    trainer.add_measure(TrainerMeasure::Intelligibility, 0.8, 8.0, threshold, 4101.0);
    trainer.test_end(4200.0);
    let session = trainer.session_end(1.0, 4300.0).unwrap();
    assert!(trainer.add_retention(session, 0.8, 5.0, 86400.0, 90000.0));
    assert_eq!(trainer.plan_sessions(TrainerPlan::Steady), 1);
}

#[test]
fn trainer_is_deterministic() {
    fn plans(seed: u64) -> Vec<TrainerPlan> {
        let mut trainer = ListenerTrainer::new(seed).unwrap();
        (0..20).map(|_| trainer.next_plan()).collect()
    }
    assert_eq!(plans(5), plans(5));
    assert_ne!(plans(5), plans(6));
}

#[test]
fn blind_trials() {
    let mut trials = BlindTrials::new(3).unwrap();
    assert_eq!(trials.next(5.0), None);
    let setting = trials.add_setting().unwrap();
    assert_eq!(setting, 0);
    assert_eq!((trials.add_value(setting, 0.0), trials.add_value(setting, 0.06)), (Some(0), Some(1)));
    assert_eq!(trials.add_value(setting, 0.06), None); // duplicate
    assert_eq!((trials.mean_score(setting, 5.0, 0), trials.winner(setting, 5.0)), (None, None));
    let trial = trials.next(5.0).unwrap();
    assert_eq!(trial.setting, setting);
    assert!((trial.first == 0.0 && near(trial.second, 0.06)) || (near(trial.first, 0.06) && trial.second == 0.0));
    for _ in 0..4 {
        assert!(trials.add(setting, 5.5, 0.0, 0.06, 0.6, 0.6, 0));
    }
    assert!(trials.add(setting, 5.2, 0.06, 0.0, 0.8, 0.4, -1));
    assert!(!trials.add(setting, 5.5, 0.0, 0.5, 0.6, 0.6, 0)); // not a value that was added
    assert_eq!((trials.won(setting, 5.0, 0), trials.lost(setting, 5.0, 0), trials.tied(setting, 5.0, 0)), (0, 1, 4));
    assert_eq!((trials.won(setting, 5.0, 1), trials.lost(setting, 5.0, 1), trials.tied(setting, 5.0, 1)), (1, 0, 4));
    assert_eq!(trials.heard(setting, 5.0, 0), 5);
    assert!(near(trials.mean_score(setting, 5.0, 1).unwrap(), (0.6 * 4.0 + 0.8) / 5.0));
    assert_eq!(trials.winner(setting, 5.0), None);
    assert_eq!(trials.heard(setting, 7.0, 0), 0); // another band
    trials.set_available(setting, false);
    assert_eq!(trials.next(5.0), None);
    trials.set_available(setting, true);
    trials.set_confidence(0.9);
}

#[test]
fn blind_trials_name_a_winner() {
    let mut trials = BlindTrials::new(0).unwrap();
    let setting = trials.add_setting().unwrap();
    trials.add_value(setting, 1.0);
    trials.add_value(setting, 2.0);
    for _ in 0..12 {
        trials.add(setting, 6.0, 1.0, 2.0, 0.5, 0.9, 1);
    }
    assert_eq!(trials.winner(setting, 6.0), Some(1));
}

#[test]
fn score_words_aligns_the_words_heard_with_the_sentence() {
    let score = |reference: &str, heard: &str| score_words(reference, heard).unwrap();
    let five = |share, right, missed, wrong, extra| WordScore { share, right, missed, wrong, extra };
    assert_eq!(score("The cat sat on the mat.", "\"the CAT, sat on the mat!\""), five(1.0, 6, 0, 0, 0));
    assert_eq!(score("one two three four", "one too tree four five"), five(0.5, 2, 0, 2, 1));
    assert_eq!(score("a b", "b a"), five(0.5, 1, 1, 0, 1));
    assert_eq!(score("Don't stop", "don\u{2019}t stop").share, 1.0);
    assert_eq!(score("Café au lait", "CAFÉ au lait").share, 1.0);
    assert_eq!(score("", ""), five(1.0, 0, 0, 0, 0));
    assert_eq!(score("", "hello"), five(0.0, 0, 0, 0, 1));
    assert_eq!(score("hello world", ""), five(0.0, 0, 2, 0, 0));
    assert_eq!(score("hello\0 world", "hello"), five(1.0, 1, 0, 0, 0));
}
