package speechwarp

import "testing"

func TestScoreWords(t *testing.T) {
	cases := []struct {
		reference, heard string
		want             WordScore
	}{
		{"The cat sat on the mat.", `"the CAT, sat on the mat!"`, WordScore{1, 6, 0, 0, 0}},
		{"one two three four", "one too tree four five", WordScore{0.5, 2, 0, 2, 1}},
		{"a b", "b a", WordScore{0.5, 1, 1, 0, 1}},
		{"Don't stop", "don’t stop", WordScore{1, 2, 0, 0, 0}},
		{"Café au lait", "CAFÉ au lait", WordScore{1, 3, 0, 0, 0}},
		{"", "", WordScore{1, 0, 0, 0, 0}},
		{"", "hello", WordScore{0, 0, 0, 0, 1}},
		{"hello world", "", WordScore{0, 0, 2, 0, 0}},
		{"hello\x00 world", "hello", WordScore{1, 1, 0, 0, 0}},
	}
	for _, c := range cases {
		got, err := ScoreWords(c.reference, c.heard)
		if err != nil || got != c.want {
			t.Errorf("ScoreWords(%q, %q) = %+v, %v; want %+v", c.reference, c.heard, got, err, c.want)
		}
	}
}
