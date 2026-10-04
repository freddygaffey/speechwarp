use speechwarp::{Error, Stream, MAX_SPEED, MIN_SPEED};

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
