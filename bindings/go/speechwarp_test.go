package speechwarp

import (
	"math"
	"os"
	"strings"
	"testing"
)

const rate = 22050

// Something for the library to chew on: a gliding tone in bursts, with gaps.
func signal(seconds float64, channels int) []float32 {
	frames := int(rate * seconds)
	samples := make([]float32, 0, frames*channels)
	phase := 0.0
	for i := 0; i < frames; i++ {
		t := float64(i) / rate
		phase += 2 * math.Pi * (120 + 60*math.Sin(t*3)) / rate
		beat := math.Mod(t, 0.4)
		envelope := 0.0
		if beat < 0.3 {
			envelope = math.Sin(math.Pi * beat / 0.3)
		}
		value := float32(0.4 * envelope * (math.Sin(phase) + 0.5*math.Sin(2*phase) + 0.3*math.Sin(3*phase)))
		for c := 0; c < channels; c++ {
			samples = append(samples, value)
		}
	}
	return samples
}

func readAll(t *testing.T, s *Stream) []float32 {
	out := make([]float32, s.Available()*s.Channels())
	if got := s.Read(out); got != len(out)/s.Channels() {
		t.Fatalf("read %d frames, want %d", got, len(out)/s.Channels())
	}
	return out
}

func speedUp(t *testing.T, input []float32, speed, nonlinear float32) []float32 {
	s, err := NewStream(rate, 1)
	if err != nil {
		t.Fatal(err)
	}
	defer s.Close()
	s.SetSpeed(speed)
	s.SetNonlinear(nonlinear)
	if err := s.Write(input); err != nil {
		t.Fatal(err)
	}
	if err := s.Flush(); err != nil {
		t.Fatal(err)
	}
	return readAll(t, s)
}

func TestVersionMatchesTheHeader(t *testing.T) {
	header, err := os.ReadFile("../../include/speechwarp.h")
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(string(header), `#define SPEECHWARP_VERSION "`+Version()+`"`) {
		t.Errorf("version %q is not the header's", Version())
	}
}

func TestDefaultsAndArguments(t *testing.T) {
	s, err := NewStream(rate, 2)
	if err != nil {
		t.Fatal(err)
	}
	if s.Speed() != 1 || s.Nonlinear() != 1 || s.Available() != 0 || s.Position() != 0 || s.Channels() != 2 {
		t.Error("wrong defaults")
	}
	if _, err := NewStream(100, 1); err != ErrUnsupportedFormat {
		t.Errorf("sample rate 100: %v", err)
	}
	if _, err := NewStream(rate, 0); err != ErrUnsupportedFormat {
		t.Errorf("0 channels: %v", err)
	}
	if err := s.Write(make([]float32, 3)); err != ErrPartialFrame {
		t.Errorf("partial frame: %v", err)
	}
	if err := s.Write(nil); err != nil {
		t.Errorf("empty write: %v", err)
	}
	if s.Read(nil) != 0 {
		t.Error("empty read")
	}

	s.SetSpeed(1000)
	s.SetSpeed(-1)
	s.SetSpeed(float32(math.NaN()))
	if s.Speed() != MaxSpeed {
		t.Errorf("speed %v", s.Speed())
	}
	s.SetNonlinear(-3)
	if s.Nonlinear() != 0 {
		t.Errorf("nonlinear %v", s.Nonlinear())
	}

	s.Close()
	s.Close()
	if err := s.Write(make([]float32, 4)); err != ErrClosed {
		t.Errorf("write after close: %v", err)
	}
	if err := s.Flush(); err != ErrClosed {
		t.Errorf("flush after close: %v", err)
	}
	if s.Available() != 0 || s.Read(make([]float32, 4)) != 0 {
		t.Error("a closed stream produced output")
	}
}

func TestOutputIsShorterByTheSpeed(t *testing.T) {
	input := signal(20, 1)
	for _, c := range [][2]float32{{1, 1}, {3, 1}, {3, 0}, {8, 1}} {
		output := speedUp(t, input, c[0], c[1])
		actual := float64(len(input)) / float64(len(output))
		if math.Abs(actual/float64(c[0])-1) > 0.1 {
			t.Errorf("speed %v nonlinear %v: got %.2f", c[0], c[1], actual)
		}
	}
}

func TestStereoAndInt16(t *testing.T) {
	floats := signal(5, 2)
	input := make([]int16, len(floats))
	for i, x := range floats {
		input[i] = int16(x * 32767)
	}
	s, _ := NewStream(rate, 2)
	defer s.Close()
	s.SetSpeed(2)
	if err := s.WriteInt16(input); err != nil {
		t.Fatal(err)
	}
	s.Flush()

	buffer := make([]int16, 1000*2+1) // one sample too many: only whole frames are written
	total := 0
	for {
		frames := s.ReadInt16(buffer)
		if frames == 0 {
			break
		}
		if frames > 1000 {
			t.Fatalf("read %d frames into room for 1000", frames)
		}
		for i := 0; i < frames; i++ {
			if buffer[2*i] != buffer[2*i+1] {
				t.Fatal("the channels differ")
			}
		}
		total += frames
	}
	if math.Abs(float64(len(input)/2)/float64(total)-2) > 0.2 {
		t.Errorf("%d frames in, %d out", len(input)/2, total)
	}
}

func TestPositionFollowsWhatIsRead(t *testing.T) {
	input := signal(20, 1)
	s, _ := NewStream(rate, 1)
	defer s.Close()
	s.SetSpeed(4)
	buffer := make([]float32, 512)
	written, last := 0, int64(0)
	for {
		if written < len(input) && s.Available() < 512 {
			end := written + 2000
			if end > len(input) {
				end = len(input)
			}
			s.Write(input[written:end])
			written = end
			if written == len(input) {
				s.Flush()
			}
			continue
		}
		position := s.Position()
		if position < last || position > int64(written) {
			t.Fatalf("position %d after %d, with %d written", position, last, written)
		}
		last = position
		if s.Read(buffer) == 0 {
			break
		}
	}
	if s.Position() != int64(len(input)) {
		t.Errorf("ended at %d of %d", s.Position(), len(input))
	}
}

func TestResetDiscardsEverything(t *testing.T) {
	input := signal(5, 1)
	s, _ := NewStream(rate, 1)
	defer s.Close()
	s.SetSpeed(3)
	s.Write(input)
	if s.Available() == 0 {
		t.Fatal("nothing to discard")
	}
	s.Reset()
	if s.Available() != 0 || s.Position() != 0 || s.Speed() != 3 {
		t.Error("reset left something behind or lost the speed")
	}
	s.Write(input)
	s.Flush()
	got, want := readAll(t, s), speedUp(t, input, 3, 1)
	if len(got) != len(want) {
		t.Fatalf("%d samples, want %d", len(got), len(want))
	}
	for i := range got {
		if got[i] != want[i] {
			t.Fatalf("sample %d differs", i)
		}
	}
}

func TestHighSpeedOptions(t *testing.T) {
	s, _ := NewStream(rate, 1)
	defer s.Close()
	if s.PauseCap() != 0 || !s.KeepSpeed() || s.SpeedFloor() != 0 || s.RhythmGap() != 0 || s.RhythmRate() != 5 {
		t.Fatal("the options should start off")
	}
	if _, ok := s.SyllableRate(); ok {
		t.Fatal("no syllable rate before 10 s")
	}
	s.SetPauseCap(5)
	s.SetSpeedFloor(0.5)
	s.SetRhythmGap(0.04)
	s.SetRhythmRate(100)
	s.SetKeepSpeed(false)
	if s.PauseCap() != 1 || s.SpeedFloor() != 0.5 || math.Abs(float64(s.RhythmGap())-0.04) > 1e-6 ||
		s.RhythmRate() != 16 || s.KeepSpeed() {
		t.Fatal("the options should be clamped")
	}
}

func TestPauseCapShortensAndPositionReachesTheEnd(t *testing.T) {
	input := signal(12, 1)
	s, _ := NewStream(rate, 1)
	defer s.Close()
	s.SetNonlinear(0)
	s.SetPauseCap(0.03)
	s.SetKeepSpeed(false)
	if err := s.Write(input); err != nil {
		t.Fatal(err)
	}
	if err := s.Flush(); err != nil {
		t.Fatal(err)
	}
	if out := readAll(t, s); float64(len(out)) >= float64(len(input))*0.9 {
		t.Fatalf("%d frames out of %d in", len(out), len(input))
	}
	if s.Position() != int64(len(input)) {
		t.Fatalf("position %d, want %d", s.Position(), len(input))
	}
	if _, ok := s.SyllableRate(); !ok {
		t.Fatal("expected a syllable rate after 12 s")
	}
}
