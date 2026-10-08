// Speed up a 16-bit PCM WAV file, streaming it through in pieces the way a player would.
//
//	go run ./examples/go talk.wav talk-3x.wav 3
package main

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"os"
	"strconv"

	speechwarp "github.com/fredgaffey/speechwarp/bindings/go"
)

func main() {
	if len(os.Args) < 3 {
		fmt.Fprintln(os.Stderr, "usage: go run ./examples/go input.wav output.wav [speed]")
		os.Exit(2)
	}
	speed := 2.0
	if len(os.Args) > 3 {
		speed, _ = strconv.ParseFloat(os.Args[3], 32)
	}

	// A minimal WAV reader: find the "fmt " and "data" chunks.
	file, err := os.ReadFile(os.Args[1])
	check(err)
	le := binary.LittleEndian
	var sampleRate, channels, bits int
	var data []byte
	for at := 12; at+8 <= len(file); {
		size := int(le.Uint32(file[at+4:]))
		switch string(file[at : at+4]) {
		case "fmt ":
			channels = int(le.Uint16(file[at+10:]))
			sampleRate = int(le.Uint32(file[at+12:]))
			bits = int(le.Uint16(file[at+22:]))
		case "data":
			data = file[at+8 : min(at+8+size, len(file))]
		}
		at += 8 + size + size&1
	}
	if data == nil || bits != 16 {
		check(fmt.Errorf("this example reads 16-bit PCM WAV only"))
	}
	samples := make([]int16, len(data)/2)
	for i := range samples {
		samples[i] = int16(le.Uint16(data[2*i:]))
	}

	stream, err := speechwarp.NewStream(sampleRate, channels)
	check(err)
	defer stream.Close()
	stream.SetSpeed(float32(speed))

	var out []int16
	buffer := make([]int16, 4096*channels)
	drain := func() {
		for {
			frames := stream.ReadInt16(buffer) // frames, not samples
			if frames == 0 {
				return
			}
			out = append(out, buffer[:frames*channels]...)
		}
	}

	// Any piece size gives the same result; a player would write whatever its decoder hands it.
	piece := 8192 * channels
	for at := 0; at < len(samples); at += piece {
		check(stream.WriteInt16(samples[at:min(at+piece, len(samples))]))
		drain()
	}
	check(stream.Flush()) // the input has ended: let out what was held back
	drain()

	var wav bytes.Buffer
	wav.WriteString("RIFF")
	binary.Write(&wav, le, uint32(36+len(out)*2))
	wav.WriteString("WAVEfmt ")
	binary.Write(&wav, le, []any{uint32(16), uint16(1), uint16(channels), uint32(sampleRate),
		uint32(sampleRate * channels * 2), uint16(channels * 2), uint16(16)})
	wav.WriteString("data")
	binary.Write(&wav, le, uint32(len(out)*2))
	binary.Write(&wav, le, out)
	check(os.WriteFile(os.Args[2], wav.Bytes(), 0o644))

	seconds := func(n int) float64 { return float64(n) / float64(channels) / float64(sampleRate) }
	fmt.Printf("%.1f s in, %.1f s out, %.2fx\n", seconds(len(samples)), seconds(len(out)),
		float64(len(samples))/float64(len(out)))
}

func check(err error) {
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
}
