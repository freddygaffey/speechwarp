//! Speed up a 16-bit PCM WAV file, streaming it through in pieces the way a player would.
//!
//!     cargo run --release --example speed_up_wav -- talk.wav talk-3x.wav 3
use std::error::Error;
use std::fs;

use speechwarp::Stream;

fn u32_at(bytes: &[u8], at: usize) -> u32 {
    u32::from_le_bytes([bytes[at], bytes[at + 1], bytes[at + 2], bytes[at + 3]])
}

fn u16_at(bytes: &[u8], at: usize) -> u16 {
    u16::from_le_bytes([bytes[at], bytes[at + 1]])
}

fn main() -> Result<(), Box<dyn Error>> {
    let args: Vec<String> = std::env::args().collect();
    if args.len() < 3 {
        eprintln!("usage: speed_up_wav input.wav output.wav [speed]");
        std::process::exit(2);
    }
    let speed: f32 = args.get(3).map_or(Ok(2.0), |s| s.parse())?;

    // A minimal WAV reader: find the "fmt " and "data" chunks.
    let file = fs::read(&args[1])?;
    let (mut sample_rate, mut channels, mut bits, mut data) = (0, 0, 0, &file[..0]);
    let mut at = 12;
    while at + 8 <= file.len() {
        let size = u32_at(&file, at + 4) as usize;
        match &file[at..at + 4] {
            b"fmt " => {
                channels = u16_at(&file, at + 10) as u32;
                sample_rate = u32_at(&file, at + 12);
                bits = u16_at(&file, at + 22);
            }
            b"data" => data = &file[at + 8..(at + 8 + size).min(file.len())],
            _ => {}
        }
        at += 8 + size + (size & 1);
    }
    if data.is_empty() || bits != 16 {
        return Err("this example reads 16-bit PCM WAV only".into());
    }
    let samples: Vec<i16> = data.chunks_exact(2).map(|b| i16::from_le_bytes([b[0], b[1]])).collect();

    let mut stream = Stream::new(sample_rate, channels)?;
    stream.set_speed(speed);
    let mut out: Vec<i16> = Vec::new();
    let mut buffer = vec![0i16; 4096 * channels as usize];
    let mut drain = |stream: &mut Stream, out: &mut Vec<i16>| loop {
        let frames = stream.read_i16(&mut buffer); // frames, not samples
        if frames == 0 {
            break;
        }
        out.extend_from_slice(&buffer[..frames * channels as usize]);
    };

    // Any piece size gives the same result; a player would write whatever its decoder hands it.
    for piece in samples.chunks(8192 * channels as usize) {
        stream.write_i16(piece)?;
        drain(&mut stream, &mut out);
    }
    stream.flush()?; // the input has ended: let out what was held back
    drain(&mut stream, &mut out);

    let bytes = (out.len() * 2) as u32;
    let mut wav = Vec::with_capacity(44 + bytes as usize);
    wav.extend_from_slice(b"RIFF");
    wav.extend_from_slice(&(36 + bytes).to_le_bytes());
    wav.extend_from_slice(b"WAVEfmt ");
    wav.extend_from_slice(&16u32.to_le_bytes());
    wav.extend_from_slice(&1u16.to_le_bytes());
    wav.extend_from_slice(&(channels as u16).to_le_bytes());
    wav.extend_from_slice(&sample_rate.to_le_bytes());
    wav.extend_from_slice(&(sample_rate * channels * 2).to_le_bytes());
    wav.extend_from_slice(&((channels * 2) as u16).to_le_bytes());
    wav.extend_from_slice(&16u16.to_le_bytes());
    wav.extend_from_slice(b"data");
    wav.extend_from_slice(&bytes.to_le_bytes());
    for sample in &out {
        wav.extend_from_slice(&sample.to_le_bytes());
    }
    fs::write(&args[2], wav)?;

    let seconds = |n: usize| n as f64 / channels as f64 / sample_rate as f64;
    println!(
        "{:.1} s in, {:.1} s out, {:.2}x",
        seconds(samples.len()),
        seconds(out.len()),
        samples.len() as f64 / out.len() as f64
    );
    Ok(())
}
