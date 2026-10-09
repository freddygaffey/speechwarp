package speechwarp

/*
#include <stdlib.h>
#include "speechwarp.h"
*/
import "C"

import (
	"strings"
	"unsafe"
)

// WordScore is how well a listener repeated a sentence back: the reference sentence and what was heard
// (typed, or from speech-to-text) aligned word by word. Right + Missed + Wrong is the reference's word count
// and Right + Wrong + Extra the heard one's.
type WordScore struct {
	// Share is the words right as a share of the reference's words, 0 to 1. With no words in the reference
	// it is 1 if nothing was heard either and 0 otherwise.
	Share float64
	// Right is the reference words heard as they are.
	Right int
	// Missed is the reference words not heard at all.
	Missed int
	// Wrong is the reference words heard as another word.
	Wrong int
	// Extra is the heard words that are not in the reference.
	Extra int
}

// ScoreWords scores heard (what the listener said or typed) against reference (the sentence played).
//
// Both are split into words the same way: letters folded to lower case (ASCII and the Latin letters),
// punctuation dropped, an apostrophe inside a word kept (' and U+2019 alike, so "Don't" matches "don’t"), and
// numbers left as digits ("3" and "three" differ). The two are aligned by word-level edit distance; among the
// cheapest alignments the one with the most words right is taken. The rules in full are at
// speechwarp_score_words in include/speechwarp.h. The standard library has no Unicode normaliser, so give
// both in the same form (NFC, as most text already is). Text after a NUL character is ignored, as in C.
//
// The error is ErrOutOfMemory if the C library could not allocate its working space.
func ScoreWords(reference, heard string) (WordScore, error) {
	cReference := C.CString(beforeNul(reference))
	defer C.free(unsafe.Pointer(cReference))
	cHeard := C.CString(beforeNul(heard))
	defer C.free(unsafe.Pointer(cHeard))
	var counts [4]C.int
	share := float64(C.speechwarp_score_words(cReference, cHeard, &counts[0]))
	if share < 0 {
		return WordScore{}, ErrOutOfMemory
	}
	return WordScore{share, int(counts[0]), int(counts[1]), int(counts[2]), int(counts[3])}, nil
}

func beforeNul(text string) string {
	if i := strings.IndexByte(text, 0); i >= 0 {
		return text[:i]
	}
	return text
}
