package speechwarp

import (
	"math"
	"testing"
)

func near(a, b float64) bool { return math.Abs(a-b) <= 1e-5*math.Max(1, math.Abs(b)) }

func TestHeardPauseAndFloorBlend(t *testing.T) {
	s, _ := NewStream(rate, 1)
	defer s.Close()
	if s.HeardPause() != 0 || s.FloorBlend() != 0 {
		t.Fatal("the rules should start off")
	}
	s.SetHeardPause(0.03, 3)
	if !near(float64(s.HeardPause()), 0.03) || s.HeardPauseFrom() != 3 {
		t.Fatalf("heard pause %v from %v", s.HeardPause(), s.HeardPauseFrom())
	}
	s.SetSpeed(5)
	if !near(float64(s.PauseCap()), 0.15) {
		t.Fatalf("pause cap %v at speed 5, want 0.15", s.PauseCap())
	}
	s.SetSpeed(2) // below fromSpeed
	if s.PauseCap() != 0 {
		t.Fatalf("pause cap %v below the from speed, want 0", s.PauseCap())
	}
	s.SetSpeed(5)
	s.SetPauseCap(0.1) // a fixed value turns the rule off
	if !near(float64(s.PauseCap()), 0.1) {
		t.Fatalf("pause cap %v, want 0.1", s.PauseCap())
	}
	s.SetFloorBlend(0.5, 4, 6)
	if s.FloorBlend() != 0.5 || s.FloorBlendFrom() != 4 || s.FloorBlendFull() != 6 {
		t.Fatal("floor blend should read back")
	}
	if !near(float64(s.SpeedFloor()), 0.25) {
		t.Fatalf("floor %v at speed 5, want 0.25", s.SpeedFloor())
	}
	s.SetSpeed(8)
	if !near(float64(s.SpeedFloor()), 0.5) {
		t.Fatalf("floor %v at speed 8, want 0.5", s.SpeedFloor())
	}
	s.SetSpeed(3)
	if s.SpeedFloor() != 0 {
		t.Fatalf("floor %v at speed 3, want 0", s.SpeedFloor())
	}
	s.SetHeardPause(0.03, 3)
	s.SetHeardPause(float32(math.NaN()), 3) // ignored
	if !near(float64(s.HeardPause()), 0.03) {
		t.Fatal("NaN should be ignored")
	}
}

func TestSyllableCounterMatchesTheStream(t *testing.T) {
	input := signal(30, 1)
	split := rate * 5
	c, err := NewSyllableCounter(rate, 1)
	if err != nil {
		t.Fatal(err)
	}
	defer c.Close()
	s, _ := NewStream(rate, 1)
	defer s.Close()
	c.Write(input[:split])
	s.Write(input[:split])
	if _, ok := c.Rate(60, 10); ok {
		t.Fatal("no rate before 10 s")
	}
	c.Write(input[split:])
	s.Write(input[split:])
	got, ok := c.Rate(60, 10)
	want, _ := s.SyllableRate()
	if !ok || got <= 0 || !near(got, want) {
		t.Fatalf("counter %v (%v), stream %v", got, ok, want)
	}
	if _, ok := c.Rate(30, 5); !ok {
		t.Fatal("expected a rate with a 5 s minimum")
	}
	shorts := make([]int16, len(input))
	for i, v := range input {
		shorts[i] = int16(v * 32767)
	}
	if err := c.WriteInt16(shorts); err != nil {
		t.Fatal(err)
	}
	c.Reset()
	if _, ok := c.Rate(60, 10); ok {
		t.Fatal("no rate after Reset")
	}
	if _, err := NewSyllableCounter(100, 1); err != ErrUnsupportedFormat {
		t.Fatalf("got %v", err)
	}
	stereo, _ := NewSyllableCounter(rate, 2)
	defer stereo.Close()
	if err := stereo.Write(make([]float32, 3)); err != ErrPartialFrame {
		t.Fatalf("got %v", err)
	}
	c.Close()
	c.Close()
	if err := c.Write(input[:10]); err != ErrClosed {
		t.Fatalf("got %v", err)
	}
}

func TestTrainerThresholdTest(t *testing.T) {
	tr, err := NewListenerTrainer(7)
	if err != nil {
		t.Fatal(err)
	}
	defer tr.Close()
	if tr.Threshold() != 0 || tr.TestDone() {
		t.Fatal("nothing before a test")
	}
	tr.TestBegin(12, 0)
	for step := 0; step < 40; step++ {
		r := tr.TestRate()
		if r < 3 || r > 60 {
			t.Fatalf("test rate %v", r)
		}
		score := 0.4
		if r < 14 {
			score = 0.95
		}
		if !tr.AddMeasure(TrainerMeasureIntelligibility, score, 8, r, float64(step)) {
			t.Fatal("measure rejected")
		}
		if tr.TestDone() {
			break
		}
	}
	threshold := tr.TestEnd(100)
	if threshold <= 0 || threshold != tr.Threshold() {
		t.Fatalf("threshold %v", threshold)
	}
	if !(tr.ThresholdLow() < threshold && threshold < tr.ThresholdHigh()) {
		t.Fatalf("interval %v < %v < %v", tr.ThresholdLow(), threshold, tr.ThresholdHigh())
	}
	if tr.AddMeasure(TrainerMeasureIntelligibility, 2, 8, 10, 0) {
		t.Fatal("a score of 2 should be rejected")
	}
}

func TestTrainerSettingsAndPlans(t *testing.T) {
	tr, _ := NewListenerTrainer(0)
	defer tr.Close()
	if !near(tr.Param(TrainerParamTarget), 0.75) {
		t.Fatalf("target %v", tr.Param(TrainerParamTarget))
	}
	tr.SetParam(TrainerParamMargin, 0.2)
	if !near(tr.Param(TrainerParamMargin), 0.2) {
		t.Fatal("param should read back")
	}
	if !near(tr.Weight(TrainerMeasureRating), 0.3) {
		t.Fatalf("rating weight %v", tr.Weight(TrainerMeasureRating))
	}
	tr.SetWeight(TrainerMeasureRating, 0.5)
	if tr.Weight(TrainerMeasureRating) != 0.5 {
		t.Fatal("weight should read back")
	}
	tr.SetParam(TrainerParamMargin, 0.1)
	// With no data every plan has no effect and no retention.
	for plan := TrainerPlanSteady; plan <= TrainerPlanTracking; plan++ {
		if tr.PlanEffect(plan) != 0 || !math.IsNaN(tr.PlanRetention(plan)) || !math.IsNaN(tr.PlanRetentionSd(plan)) ||
			tr.PlanSessions(plan) != 0 {
			t.Fatalf("plan %d should have no data", plan)
		}
		if p := tr.PlanBestProbability(plan); !(tr.PlanEffectSd(plan) >= 0) || !(p >= 0 && p <= 1) {
			t.Fatalf("plan %d: sd %v best probability %v", plan, tr.PlanEffectSd(plan), p)
		}
	}
	if !(tr.Trend() > 0) || !(tr.TrendSd() >= 0) {
		t.Fatalf("trend %v sd %v", tr.Trend(), tr.TrendSd())
	}
	if tr.SessionRate(0) != 0 {
		t.Fatal("no session, no rate")
	}
	tr.TestBegin(10, 0)
	for step := 0; step < 20; step++ {
		r := tr.TestRate()
		score := 0.55
		if r < 12 {
			score = 0.9
		}
		tr.AddMeasure(TrainerMeasureVerification, score, 6, r, float64(step))
	}
	threshold := tr.TestEnd(50)
	tr.SessionBegin(TrainerPlanSteady, 100)
	if !near(tr.SessionRate(100), threshold*1.1) {
		t.Fatalf("steady rate %v, want %v", tr.SessionRate(100), threshold*1.1)
	}
	if tr.SessionEnd(0.5, 3700) < 0 {
		t.Fatal("a session was running")
	}
	if tr.SessionEnd(0.5, 3800) != -1 {
		t.Fatal("no session was running")
	}
	tr.SessionBegin(TrainerPlanSteady, 4000)
	tr.TestBegin(0, 4100)
	tr.AddMeasure(TrainerMeasureIntelligibility, 0.8, 8, threshold, 4101)
	tr.TestEnd(4200)
	session := tr.SessionEnd(1, 4300)
	if session < 0 || !tr.AddRetention(session, 0.8, 5, 86400, 90000) {
		t.Fatalf("session %d", session)
	}
	if tr.PlanSessions(TrainerPlanSteady) != 1 {
		t.Fatalf("sessions %d", tr.PlanSessions(TrainerPlanSteady))
	}
}

func TestTrainerIsDeterministic(t *testing.T) {
	plans := func(seed uint64) []TrainerPlan {
		tr, _ := NewListenerTrainer(seed)
		defer tr.Close()
		out := make([]TrainerPlan, 20)
		for i := range out {
			out[i] = tr.NextPlan()
		}
		return out
	}
	a, b, c := plans(5), plans(5), plans(6)
	same := true
	for i := range a {
		if a[i] != b[i] {
			t.Fatal("same seed, different plans")
		}
		same = same && a[i] == c[i]
	}
	if same {
		t.Fatal("different seeds should differ")
	}
}

func TestBlindTrials(t *testing.T) {
	b, err := NewBlindTrials(3)
	if err != nil {
		t.Fatal(err)
	}
	defer b.Close()
	if _, _, _, ok := b.Next(5); ok {
		t.Fatal("nothing to compare yet")
	}
	setting := b.AddSetting()
	if setting != 0 || b.AddValue(setting, 0) != 0 || b.AddValue(setting, 0.06) != 1 || b.AddValue(setting, 0.06) != -1 {
		t.Fatal("settings and values should be numbered in order, duplicates refused")
	}
	if _, ok := b.MeanScore(setting, 5, 0); ok {
		t.Fatal("never heard")
	}
	if _, ok := b.Winner(setting, 5); ok {
		t.Fatal("no winner yet")
	}
	chosen, first, second, ok := b.Next(5)
	if !ok || chosen != setting || !((first == 0 && near(second, 0.06)) || (near(first, 0.06) && second == 0)) {
		t.Fatalf("next: %v %v %v %v", chosen, first, second, ok)
	}
	for i := 0; i < 4; i++ {
		if !b.Add(setting, 5.5, 0, 0.06, 0.6, 0.6, 0) {
			t.Fatal("trial rejected")
		}
	}
	if !b.Add(setting, 5.2, 0.06, 0, 0.8, 0.4, -1) {
		t.Fatal("trial rejected")
	}
	if b.Add(setting, 5.5, 0, 0.5, 0.6, 0.6, 0) {
		t.Fatal("0.5 was never added")
	}
	if b.Won(setting, 5, 0) != 0 || b.Lost(setting, 5, 0) != 1 || b.Tied(setting, 5, 0) != 4 ||
		b.Won(setting, 5, 1) != 1 || b.Lost(setting, 5, 1) != 0 || b.Tied(setting, 5, 1) != 4 {
		t.Fatal("counts do not add up")
	}
	if b.Heard(setting, 5, 0) != 5 || b.Heard(setting, 7, 0) != 0 {
		t.Fatal("heard counts")
	}
	if mean, ok := b.MeanScore(setting, 5, 1); !ok || !near(mean, (0.6*4+0.8)/5) {
		t.Fatalf("mean %v", mean)
	}
	if _, ok := b.Winner(setting, 5); ok {
		t.Fatal("tied, no winner")
	}
	b.SetAvailable(setting, false)
	if _, _, _, ok := b.Next(5); ok {
		t.Fatal("unavailable setting chosen")
	}
	b.SetAvailable(setting, true)
	b.SetConfidence(0.9)
}

func TestBlindTrialsNameAWinner(t *testing.T) {
	b, _ := NewBlindTrials(0)
	defer b.Close()
	setting := b.AddSetting()
	b.AddValue(setting, 1)
	b.AddValue(setting, 2)
	for i := 0; i < 12; i++ {
		b.Add(setting, 6, 1, 2, 0.5, 0.9, 1)
	}
	if w, ok := b.Winner(setting, 6); !ok || w != 1 {
		t.Fatalf("winner %v %v", w, ok)
	}
}
