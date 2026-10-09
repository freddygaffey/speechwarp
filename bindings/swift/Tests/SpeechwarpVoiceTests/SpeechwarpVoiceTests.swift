// Apple's voices exist only on Apple systems; on Linux there is nothing to test.
#if canImport(AVFoundation)
import Foundation
import XCTest
@testable import SpeechwarpVoice

final class SpeechwarpVoiceTests: XCTestCase {
    private func reed() throws -> SpeechVoice {
        guard let voice = SpeechVoice.eloquence(language: "en-US").first(where: { $0.name == "Reed" })
            ?? SpeechVoice.eloquence(language: "en").first else {
            throw XCTSkip("No Eloquence voice is installed here")
        }
        return voice
    }

    func testSplitsIntoSentencesWithTheirPlaceInTheText() {
        let text = "Dr. Smith arrived at 3 p.m. on Monday. \"Is it late?\" she asked. It was."
        let sentences = SpokenText.split(text)
        // The system tokenizer decides; it may also break after "p.m.", which only moves a pause.
        XCTAssertGreaterThanOrEqual(sentences.count, 3)
        XCTAssertTrue(sentences.last?.text.hasPrefix("It was") ?? false)
        for sentence in sentences {
            XCTAssertEqual((text as NSString).substring(with: sentence.range), sentence.text)
        }
        XCTAssertTrue(sentences[0].text.hasPrefix("Dr. Smith"))
    }

    func testCutsVeryLongSentences() {
        let text = Array(repeating: "and then another clause follows,", count: 40).joined(separator: " ") + "."
        let sentences = SpokenText.split(text)
        XCTAssertGreaterThan(sentences.count, 2)
        XCTAssertTrue(sentences.allSatisfy { $0.range.length <= 400 })
        XCTAssertEqual(sentences.map(\.text).joined(), text.trimmingCharacters(in: .whitespaces))
    }

    func testListsEloquenceVoices() throws {
        let voice = try reed()
        XCTAssertTrue(voice.isEloquence)
        XCTAssertTrue(SpeechVoice.eloquence().allSatisfy(\.isEloquence))
        XCTAssertEqual(SpeechVoice(identifier: voice.identifier), voice)
        XCTAssertNil(SpeechVoice(identifier: "no.such.voice"))
    }

    func testRendersAndTheMaximumRateIsFaster() async throws {
        let renderer = try SpeechRenderer(voice: try reed())
        let text = "The quick brown fox jumps over the lazy dog."
        let normal = try await renderer.render(text, rate: SpeechRate.normal)
        let fast = try await renderer.render(text, rate: SpeechRate.maximum)
        XCTAssertGreaterThan(normal.duration, 1)
        XCTAssertGreaterThan(normal.sampleRate, 8000)
        XCTAssertLessThan(fast.duration, normal.duration / 2)
        XCTAssertGreaterThan(normal.samples.map(abs).max() ?? 0, 0.05)
        let empty = try await renderer.render("   ")
        XCTAssertTrue(empty.samples.isEmpty)
    }

    func testReadsAWholeTextSpedUpAndFollowsThePosition() async throws {
        let text = "This is the first sentence. Here is a second one, a little longer than the first. " +
            "And a third to finish."
        let spoken = try SpokenText(text, voice: try reed())
        spoken.speed = 2
        var output = 0
        var positions: [Int] = []
        let deadline = Date().addingTimeInterval(60)
        while !spoken.isFinished && Date() < deadline {
            let samples = spoken.read(maxFrames: 2048)
            output += samples.count
            positions.append(spoken.characterPosition)
            if samples.isEmpty { try await Task.sleep(nanoseconds: 20_000_000) } // let rendering run
        }
        XCTAssertTrue(spoken.isFinished)
        XCTAssertNil(spoken.lastError)
        XCTAssertEqual(positions, positions.sorted(), "the position never goes backwards")
        XCTAssertGreaterThanOrEqual(positions.last ?? 0, (text as NSString).length - 30)

        // At speed 2 the output is about half the rendered speech.
        let renderer = try SpeechRenderer(voice: try reed())
        var rendered = 0
        for sentence in spoken.sentences {
            rendered += try await renderer.render(sentence.text).samples.count
        }
        let ratio = Double(rendered) / Double(output)
        XCTAssertEqual(ratio, 2, accuracy: 0.4, "speed-up \(ratio)")
    }

    func testSeeksToASentence() async throws {
        let text = "One. Two is here. Three is the last sentence of this short text."
        let spoken = try SpokenText(text, voice: try reed())
        let third = spoken.sentences[2].range.location
        spoken.seek(toCharacter: third + 3)
        XCTAssertEqual(spoken.sentenceIndex, 2)
        var output = 0
        let deadline = Date().addingTimeInterval(30)
        while !spoken.isFinished && Date() < deadline {
            let samples = spoken.read(maxFrames: 2048)
            output += samples.count
            XCTAssertGreaterThanOrEqual(spoken.characterPosition, third)
            if samples.isEmpty { try await Task.sleep(nanoseconds: 20_000_000) }
        }
        XCTAssertTrue(spoken.isFinished)
        XCTAssertGreaterThan(output, 0)
    }
}
#endif
