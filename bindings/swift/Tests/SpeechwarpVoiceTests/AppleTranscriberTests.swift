// Apple's speech recogniser exists only on Apple systems; the recognition tests also need macOS's `say`.
#if canImport(Speech) && (os(iOS) || os(macOS))
import AVFoundation
import Foundation
import Speechwarp
import XCTest
@testable import SpeechwarpVoice

final class AppleTranscriberTests: XCTestCase {
    // MARK: Plain logic

    private func word(_ text: String, _ start: Double, _ end: Double) -> TranscriptWord {
        TranscriptWord(text: text, start: start, end: end, confidence: 0.9)
    }

    func testGroupsWordsIntoSentencesAndAtPauses() {
        let words = [
            word("The", 0, 0.2), word("dog.", 0.2, 0.6),
            word("Then", 0.7, 0.9), word("it", 0.9, 1.0),
            word("ran", 2.0, 2.3), word("away", 2.3, 2.6), // after a 1 s pause
        ]
        let segments = TranscriptAssembly.segments(from: words, offset: 10)
        XCTAssertEqual(segments.map(\.text), ["The dog.", "Then it", "ran away"])
        XCTAssertEqual(segments[0].start, 10, accuracy: 1e-9)
        XCTAssertEqual(segments[2].end, 12.6, accuracy: 1e-9)
        XCTAssertEqual(segments[1].words.map(\.start), [10.7, 10.9])

        let bare = TranscriptAssembly.segments(from: words, keepWords: false)
        XCTAssertTrue(bare.allSatisfy(\.words.isEmpty))
        XCTAssertEqual(bare.count, 3)
    }

    func testLimitsSegmentLengthAndSkipsEmptyWords() {
        let words = (0..<100).map { word("w", Double($0) * 0.5, Double($0) * 0.5 + 0.4) } + [word("  ", 60, 61)]
        let segments = TranscriptAssembly.segments(from: words, maxSeconds: 10)
        XCTAssertGreaterThanOrEqual(segments.count, 5)
        XCTAssertTrue(segments.allSatisfy { $0.end - $0.start <= 10 })
        XCTAssertEqual(segments.flatMap(\.words).count, 100)
        XCTAssertTrue(TranscriptAssembly.endsSentence("dog?\""))
        XCTAssertFalse(TranscriptAssembly.endsSentence("Dr"))
    }

    func testCutsLongAudioInAPause() {
        let rate = 1000
        var cutter = ChunkCutter(sampleRate: rate, minSeconds: 2, maxSeconds: 5)
        // 3 s of tone, 0.5 s of silence, 3 s of tone.
        let tone = (0..<3000).map { Float(sin(Double($0) * 0.3)) * 0.5 }
        let input = tone + [Float](repeating: 0, count: 500) + tone
        var pieces: [ChunkCutter.Piece] = []
        for start in stride(from: 0, to: input.count, by: 333) {
            pieces += cutter.push(Array(input[start..<min(start + 333, input.count)]))
        }
        pieces += cutter.flush()

        var requests: [[Float]] = [[]]
        for piece in pieces {
            switch piece {
            case .audio(let samples): requests[requests.count - 1] += samples
            case .cut: requests.append([])
            }
        }
        XCTAssertEqual(requests.flatMap { $0 }, input, "nothing lost, nothing reordered")
        XCTAssertEqual(requests.count, 2)
        // The cut is in the middle of the first quiet block, just after the tone.
        XCTAssertEqual(requests[0].count, 3050)
    }

    func testCutsAtTheMaximumWithoutAPause() {
        let rate = 1000
        var cutter = ChunkCutter(sampleRate: rate, minSeconds: 2, maxSeconds: 5)
        let input = (0..<12000).map { Float(sin(Double($0) * 0.3)) * 0.5 }
        var lengths = [0]
        for piece in cutter.push(input) + cutter.flush() {
            switch piece {
            case .audio(let samples): lengths[lengths.count - 1] += samples.count
            case .cut: lengths.append(0)
            }
        }
        XCTAssertEqual(lengths, [5000, 5000, 2000])
    }

    func testModels() throws {
        let model = AppleTranscriber.model(language: "en-GB")
        XCTAssertEqual(model.id, "apple-en-GB")
        XCTAssertEqual(model.engine, .apple)
        XCTAssertEqual(model.languages, ["en-GB"])
        XCTAssertEqual(model.sizeBytes, 0)
        XCTAssertNil(model.downloadURL)
        XCTAssertTrue(model.name.contains("English"), model.name)
        XCTAssertTrue(AppleTranscriber.models.allSatisfy { $0.engine == .apple && $0.id.hasPrefix("apple-") })

        let whisper = TranscriptionModel(id: "whisper-base.en", engine: .whisper, name: "Whisper base",
                                         languages: ["en"], sizeBytes: 1, downloadURL: nil, sha256: nil,
                                         relativeSpeed: 1)
        XCTAssertThrowsError(try AppleTranscriber(model: whisper)) {
            XCTAssertEqual($0 as? AppleTranscriberError, .notAnAppleModel("whisper-base.en"))
        }
        let transcriber = try AppleTranscriber(model: model)
        XCTAssertFalse(transcriber.isReady)
        XCTAssertThrowsError(try transcriber.startSession(sampleRate: 16000, options: .init())) {
            XCTAssertEqual($0 as? AppleTranscriberError, .notPrepared)
        }
    }

    func testConvertsAStreamToAnotherRate() throws {
        let from = AudioSamples.monoFormat(sampleRate: 48000)
        let to = AudioSamples.monoFormat(sampleRate: 16000)
        let converter = try XCTUnwrap(AudioStreamConverter(from: from, to: to))
        let tone = (0..<48000).map { Float(sin(2 * Double.pi * 440 * Double($0) / 48000)) * 0.5 }
        var out: [Float] = []
        for start in stride(from: 0, to: tone.count, by: 4800) {
            let buffer = try XCTUnwrap(AudioSamples.buffer(Array(tone[start..<start + 4800]), sampleRate: 48000))
            if let converted = converter.convert(buffer) { out += AudioSamples.mono(converted) }
        }
        if let tail = converter.flush() { out += AudioSamples.mono(tail) }
        XCTAssertEqual(Double(out.count), 16000, accuracy: 100)
        XCTAssertEqual(Double(AudioSamples.rms(Array(out[1000..<15000]))), 0.5 / 2.squareRoot(), accuracy: 0.02)
    }

    func testMicrophoneStartsStopped() {
        let microphone = MicrophoneSource(sampleRate: 16000)
        XCTAssertFalse(microphone.isRunning)
        XCTAssertEqual(microphone.level, 0)
        microphone.stop() // harmless when stopped
    }

    // MARK: Recognition of speech made with `say`

    #if os(macOS)
    /// Mono float speech at 16 kHz from macOS's `say`.
    private func speech(_ text: String) throws -> [Float] {
        let url = FileManager.default.temporaryDirectory.appendingPathComponent("speechwarp-\(UUID().uuidString).wav")
        defer { try? FileManager.default.removeItem(at: url) }
        let say = Process()
        say.executableURL = URL(fileURLWithPath: "/usr/bin/say")
        say.arguments = ["-v", "Samantha", "-o", url.path, "--file-format=WAVE", "--data-format=LEF32@16000", text]
        try say.run()
        say.waitUntilExit()
        guard say.terminationStatus == 0 else { throw XCTSkip("say could not make speech (voice Samantha missing?)") }
        let file = try AVAudioFile(forReading: url)
        let buffer = try XCTUnwrap(AVAudioPCMBuffer(pcmFormat: file.processingFormat,
                                                    frameCapacity: AVAudioFrameCount(file.length)))
        try file.read(into: buffer)
        XCTAssertEqual(file.processingFormat.sampleRate, 16000)
        return AudioSamples.mono(buffer)
    }

    /// A prepared transcriber, or a skip saying why recognition cannot run here.
    private func prepared(_ recogniser: AppleTranscriber.Recogniser) async throws -> AppleTranscriber {
        let transcriber = try AppleTranscriber(model: AppleTranscriber.model(language: "en-US"), recogniser: recogniser)
        do {
            try await transcriber.prepare(progress: nil)
        } catch let error as AppleTranscriberError {
            switch error {
            case .notAuthorised:
                throw XCTSkip("Speech recognition is not authorised for this process (System Settings > Privacy)")
            case .unsupportedLanguage:
                throw XCTSkip("\(recogniser) cannot recognise en-US on this device")
            case .missingUsageDescription, .recognitionFailed:
                throw XCTSkip("Speech recognition is not available here: \(error)")
            default:
                throw error
            }
        }
        XCTAssertTrue(transcriber.isReady)
        return transcriber
    }

    private func resample(_ samples: [Float], from source: Int, to target: Int) -> [Float] {
        let count = samples.count * target / source
        return (0..<count).map { i in
            let x = Double(i) * Double(source) / Double(target)
            let j = Int(x), f = Float(x - Double(j))
            return j + 1 < samples.count ? samples[j] * (1 - f) + samples[j + 1] * f : samples[samples.count - 1]
        }
    }

    private func normalised(_ text: String) -> String {
        text.lowercased().filter { $0.isLetter || $0 == " " }
    }

    private func checkPassage(_ recogniser: AppleTranscriber.Recogniser) async throws {
        let samples = try speech("The quick brown fox jumps over the lazy dog. Then it ran away into the forest.")
        let transcriber = try await prepared(recogniser)
        XCTAssertEqual(transcriber.recogniserInUse, recogniser)
        let transcript = try await transcriber.transcribe(samples, sampleRate: 16000, options: .init())
        let text = normalised(transcript.text)
        XCTAssertTrue(text.contains("quick brown fox"), transcript.text)
        XCTAssertTrue(text.contains("forest"), transcript.text)

        let words = transcript.segments.flatMap(\.words)
        XCTAssertGreaterThanOrEqual(words.count, 12, "\(words)")
        XCTAssertEqual(words.map(\.start), words.map(\.start).sorted(), "words in order")
        let duration = Double(samples.count) / 16000
        XCTAssertLessThanOrEqual(words.last?.end ?? 99, duration + 0.1)
        let fox = try XCTUnwrap(words.first { normalised($0.text) == "fox" })
        XCTAssertEqual(fox.start, 0.7, accuracy: 0.4, "fox is about 0.7 s in")

        // Without word timestamps, segments still have times.
        let bare = try await transcriber.transcribe(samples, sampleRate: 16000,
                                                    options: TranscriptionOptions(wordTimestamps: false))
        XCTAssertTrue(bare.segments.allSatisfy(\.words.isEmpty))
        XCTAssertFalse(bare.segments.isEmpty)
    }

    func testTranscribesAPassageWithSpeechAnalyzer() async throws {
        guard #available(macOS 26, *) else { throw XCTSkip("SpeechAnalyzer needs macOS 26") }
        try await checkPassage(.speechAnalyzer)
    }

    func testTranscribesAPassageWithSFSpeechRecognizer() async throws {
        try await checkPassage(.speechRecognizer)
    }

    /// Audio given in small pieces, as a microphone or a decoder does, at another rate, and longer than one
    /// request of the older recogniser, so that it is cut and the times are carried across the cut.
    private func checkStream(_ recogniser: AppleTranscriber.Recogniser) async throws {
        let sentences = [
            "It was a bright cold day in April, and the clocks were striking thirteen.",
            "The hallway smelt of boiled cabbage and old rag mats.",
            "At one end of it a coloured poster, too large for indoor display, had been tacked to the wall.",
            "It depicted simply an enormous face, more than a metre wide.",
            "It was the face of a man of about forty five, with a heavy black moustache.",
            "Winston made for the stairs. It was no use trying the lift.",
            "Even at the best of times it was seldom working, and at present the electric current was cut off.",
            "It was part of the economy drive in preparation for Hate Week.",
            "The flat was seven flights up, and Winston, who was thirty nine, went slowly.",
            "On each landing, opposite the lift shaft, the poster with the enormous face gazed from the wall.",
            "It was one of those pictures which are so contrived that the eyes follow you about when you move.",
        ]
        let speech16k = try speech(sentences.joined(separator: " [[slnc 700]] "))
        // Up to 22.05 kHz, so the session resamples.
        let samples = resample(speech16k, from: 16000, to: 22050)
        let duration = Double(samples.count) / 22050
        let transcriber = try await prepared(recogniser)
        let session = try transcriber.startSession(sampleRate: 22050, options: .init())
        let notified = Notified()
        session.onSegmentsReady = { notified.mark() }

        var taken: [TranscriptSegment] = []
        for start in stride(from: 0, to: samples.count, by: 2205) {
            session.write(Array(samples[start..<min(start + 2205, samples.count)]))
            taken += session.takeSegments()
        }
        XCTAssertEqual(session.secondsWritten, duration, accuracy: 1e-6)
        let rest = try await session.finish()
        let all = taken + rest.segments
        XCTAssertEqual(session.secondsRecognised, duration, accuracy: 1e-6)
        XCTAssertNil(session.partial)

        let text = normalised(Transcript(segments: all).text)
        // Phrases both recognisers got right in a first run (they spell "moustache" the American way).
        for phrase in ["clocks were striking", "boiled cabbage", "heavy black", "lift shaft", "eyes follow you"] {
            XCTAssertTrue(text.contains(phrase), "\(phrase) in \(text)")
        }
        XCTAssertEqual(all.map(\.start), all.map(\.start).sorted(), "segments in order")
        XCTAssertLessThanOrEqual(all.last?.end ?? 999, duration + 0.1)
        // The last sentence starts near the end, so times survived any cuts.
        let last = try XCTUnwrap(all.last(where: { normalised($0.text).contains("follow") }))
        XCTAssertGreaterThan(last.start, duration - 12)
        XCTAssertTrue(notified.happened)
    }

    func testTranscribesAStreamWithSpeechAnalyzer() async throws {
        guard #available(macOS 26, *) else { throw XCTSkip("SpeechAnalyzer needs macOS 26") }
        try await checkStream(.speechAnalyzer)
    }

    func testTranscribesAStreamWithSFSpeechRecognizer() async throws {
        try await checkStream(.speechRecognizer)
    }

    func testSessionWithNoAudio() async throws {
        let transcriber = try await prepared(.automatic)
        let session = try transcriber.startSession(sampleRate: 16000, options: .init())
        let transcript = try await session.finish()
        XCTAssertTrue(transcript.segments.isEmpty)
        XCTAssertEqual(session.secondsWritten, 0)
    }
    #endif
}

private final class Notified: @unchecked Sendable {
    private let lock = NSLock()
    private var value = false
    func mark() { lock.lock(); value = true; lock.unlock() }
    var happened: Bool { lock.lock(); defer { lock.unlock() }; return value }
}
#endif
