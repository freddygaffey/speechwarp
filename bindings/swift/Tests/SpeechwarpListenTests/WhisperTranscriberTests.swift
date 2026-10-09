#if os(iOS) || os(macOS)
import Foundation
import Speechwarp
import SpeechwarpListen
import XCTest

/// Tests of the binding. Those that recognise speech need a whisper.cpp model, named by the environment variable
/// SPEECHWARP_LISTEN_MODEL (ggml-tiny.en.bin is enough), and are skipped without one. The speech is the sample that
/// comes with whisper.cpp (third_party/whisper.cpp/samples/jfk.wav).
@available(macOS 13.3, iOS 16.4, *)
final class WhisperTranscriberTests: XCTestCase {
    private static let root = URL(fileURLWithPath: #filePath)
        .deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
        .deletingLastPathComponent().deletingLastPathComponent()

    private func modelPath() throws -> String {
        guard let path = ProcessInfo.processInfo.environment["SPEECHWARP_LISTEN_MODEL"], !path.isEmpty else {
            throw XCTSkip("Set SPEECHWARP_LISTEN_MODEL to a whisper.cpp model to run this test.")
        }
        XCTAssertTrue(FileManager.default.fileExists(atPath: path), "missing model \(path)")
        return path
    }

    /// The whisper.cpp sample: 11 s of President Kennedy at 16 kHz, 16-bit.
    private func speech() throws -> [Float] {
        let url = Self.root.appendingPathComponent("third_party/whisper.cpp/samples/jfk.wav")
        guard let data = try? Data(contentsOf: url) else { throw XCTSkip("The whisper.cpp submodule is not checked out.") }
        let bytes = [UInt8](data)
        func int32(_ at: Int) -> Int { Int(bytes[at]) | Int(bytes[at + 1]) << 8 | Int(bytes[at + 2]) << 16 | Int(bytes[at + 3]) << 24 }
        var at = 12
        while String(bytes: bytes[at..<at + 4], encoding: .ascii) != "data" { at += 8 + int32(at + 4) }
        let count = int32(at + 4) / 2
        return (0..<count).map { i in
            Float(Int16(bitPattern: UInt16(bytes[at + 8 + 2 * i]) | UInt16(bytes[at + 9 + 2 * i]) << 8)) / 32768
        }
    }

    private func silence(_ seconds: Double) -> [Float] { [Float](repeating: 0, count: Int(seconds * 16000)) }

    /// Lower case, no punctuation, single spaces.
    private func plain(_ text: String) -> String {
        String(text.lowercased().map { $0.isLetter || $0.isNumber || $0 == "'" ? $0 : " " })
            .split(separator: " ").joined(separator: " ")
    }

    private func count(_ text: String, _ phrase: String) -> Int {
        plain(text).components(separatedBy: phrase).count - 1
    }

    private func prepared() async throws -> WhisperTranscriber {
        let transcriber = try WhisperTranscriber(modelPath: try modelPath())
        try await transcriber.prepare(progress: nil)
        return transcriber
    }

    // MARK: Without a model

    func testVersionsMatchTheHeader() throws {
        let header = try String(contentsOf: Self.root.appendingPathComponent("include/speechwarp.h"), encoding: .utf8)
        let line = header.split(separator: "\n").first { $0.hasPrefix("#define SPEECHWARP_VERSION \"") }!
        XCTAssertEqual(WhisperTranscriber.nativeVersion, line.split(separator: "\"")[1].description)
        XCTAssertFalse(WhisperTranscriber.engineVersion.isEmpty)
        XCTAssertFalse(WhisperTranscriber.systemInfo.isEmpty)
    }

    func testCatalogueDescribesEveryModel() {
        let models = WhisperModels.all
        XCTAssertGreaterThanOrEqual(models.count, 10)
        XCTAssertEqual(Set(models.map(\.id)).count, models.count)
        for model in models {
            XCTAssertEqual(model.engine, .whisper)
            XCTAssertTrue(model.id.hasPrefix("whisper-"))
            XCTAssertGreaterThan(model.sizeBytes, 10_000_000)
            XCTAssertEqual(model.sha256?.count, 64)
            let fileName = WhisperModels.fileName(model)
            XCTAssertNotNil(fileName)
            XCTAssertTrue(model.downloadURL!.hasPrefix("https://") && model.downloadURL!.hasSuffix("/" + fileName!))
            XCTAssertGreaterThanOrEqual(model.relativeSpeed, 1)
        }
        XCTAssertEqual(WhisperModels.find("whisper-tiny.en")?.languages, ["en"])
        XCTAssertEqual(WhisperModels.find("whisper-tiny")?.languages, [])
        XCTAssertNil(WhisperModels.find("whisper-enormous"))
        XCTAssertEqual(WhisperModels.forFile("/models/ggml-base.en.bin").id, "whisper-base.en")
        let other = WhisperModels.forFile("/models/mine.bin")
        XCTAssertEqual(other.id, "whisper-file:mine.bin")
        XCTAssertTrue(other.relativeSpeed.isNaN)
    }

    func testRefusesOtherModelsAndMissingFiles() async throws {
        let apple = TranscriptionModel(id: "apple-en-GB", engine: .apple, name: "Apple", languages: ["en-GB"],
                                       sizeBytes: 0, downloadURL: nil, sha256: nil, relativeSpeed: 1)
        XCTAssertThrowsError(try WhisperTranscriber(modelPath: "x.bin", model: apple))

        let missing = try WhisperTranscriber(modelPath: NSTemporaryDirectory() + "no-such-model.bin")
        do {
            try await missing.prepare(progress: nil)
            XCTFail("loaded a missing file")
        } catch let error as WhisperError {
            XCTAssertEqual(error, .modelFileMissing(missing.modelPath))
        }
        XCTAssertFalse(missing.isReady)
        XCTAssertThrowsError(try missing.startSession(sampleRate: 16000, options: .init())) {
            XCTAssertEqual($0 as? WhisperError, .notPrepared)
        }
    }

    // MARK: With a model

    func testTranscribesASentenceWithWordTimes() async throws {
        let transcriber = try await prepared()
        XCTAssertTrue(transcriber.isReady)
        let transcript = try await transcriber.transcribe(try speech(), sampleRate: 16000,
                                                          options: .init(language: "en"))
        XCTAssertTrue(plain(transcript.text).contains("ask not what your country can do for you"), transcript.text)
        var previous = 0.0
        for segment in transcript.segments {
            XCTAssertGreaterThanOrEqual(segment.start, previous - 0.01)
            XCTAssertGreaterThanOrEqual(segment.end, segment.start)
            XCTAssertLessThanOrEqual(segment.end, 11.5)
            XCTAssertFalse(segment.words.isEmpty)
            for word in segment.words {
                XCTAssertTrue((0...1).contains(word.confidence))
                XCTAssertGreaterThanOrEqual(word.start, segment.start - 0.01)
                XCTAssertLessThanOrEqual(word.end, segment.end + 0.01)
            }
            previous = segment.start
        }
        let empty = try await transcriber.transcribe(silence(3), sampleRate: 16000, options: .init())
        XCTAssertTrue(empty.segments.isEmpty)
    }

    func testRefusesAnUnknownLanguageAndRate() async throws {
        let transcriber = try await prepared()
        do {
            _ = try await transcriber.transcribe(silence(1), sampleRate: 16000, options: .init(language: "qq"))
            XCTFail("accepted language qq")
        } catch let error as WhisperError {
            XCTAssertEqual(error, .unknownLanguage("qq"))
        }
        XCTAssertThrowsError(try transcriber.startSession(sampleRate: 1000, options: .init()))
    }

    func testCancelsATranscription() async throws {
        let transcriber = try await prepared()
        let speech = try speech()
        let long = Array((0..<20).map { _ in speech + silence(1) }.joined())
        let task = Task { try await transcriber.transcribe(long, sampleRate: 16000, options: .init()) }
        try await Task.sleep(nanoseconds: 200_000_000)
        task.cancel()
        do {
            _ = try await task.value
            XCTFail("four minutes of speech were not cancelled")
        } catch is CancellationError {
        }
        let again = try await transcriber.transcribe(speech, sampleRate: 16000, options: .init())
        XCTAssertTrue(plain(again.text).contains("your country"))
    }

    func testASessionRecognisesInChunksAsTheAudioArrives() async throws {
        let transcriber = try await prepared()
        let speech = try speech()
        let audio = speech + silence(14) + speech + silence(14) + speech + silence(14)
        let session = try transcriber.startSession(sampleRate: 16000, options: .init(language: "en"))
        let ready = expectation(description: "segments ready before finish")
        ready.assertForOverFulfill = false
        session.onSegmentsReady = { ready.fulfill() }
        for start in stride(from: 0, to: audio.count, by: 1600) {
            session.write(Array(audio[start..<min(start + 1600, audio.count)]))
        }
        XCTAssertEqual(session.secondsWritten, Double(audio.count) / 16000, accuracy: 1e-6)
        await fulfillment(of: [ready], timeout: 60)
        let early = session.takeSegments()
        XCTAssertFalse(early.isEmpty)
        XCTAssertGreaterThan(session.secondsRecognised, 0)

        let rest = try await session.finish()
        let all = early + rest.segments
        XCTAssertEqual(session.secondsRecognised, session.secondsWritten, accuracy: 0.01)
        XCTAssertTrue(session.takeSegments().isEmpty)
        let text = all.map(\.text).joined(separator: " ")
        // As in the C# tests: each of the three is recognised, at least in part (small models shorten a sentence
        // repeated word for word when the text before is the context).
        XCTAssertGreaterThanOrEqual(count(text, "do for your country"), 3, text)
        for (previous, next) in zip(all, all.dropFirst()) { XCTAssertGreaterThanOrEqual(next.start, previous.start - 0.01) }
        XCTAssertLessThan(all.first!.start, 1)
        XCTAssertGreaterThan(all.last!.end, 55)

        session.write(silence(1)) // ignored after finish
        XCTAssertEqual(session.secondsWritten, Double(audio.count) / 16000, accuracy: 1e-6)
    }

    func testASessionGivesAPartialGuessWhileItIsRead() async throws {
        let transcriber = try await prepared()
        let speech = try speech()
        let session = try transcriber.startSession(sampleRate: 16000, options: .init(preset: .fast))
        XCTAssertNil(session.partial)
        for start in stride(from: 0, to: speech.count, by: 1600) {
            session.write(Array(speech[start..<min(start + 1600, speech.count)]))
            _ = session.partial
        }
        var guess: TranscriptSegment?
        let deadline = Date().addingTimeInterval(30)
        while Date() < deadline {
            guess = session.partial
            if let guess, plain(guess.text).contains("your country") { break }
            try await Task.sleep(nanoseconds: 50_000_000)
        }
        XCTAssertTrue(plain(guess?.text ?? "").contains("your country"), guess?.text ?? "no guess")
        let transcript = try await session.finish()
        XCTAssertTrue(plain(transcript.text).contains("ask not what your country can do for you"))
        XCTAssertNil(session.partial)
        // Finishing again gives what is left (nothing), as in C#, rather than waiting for a worker that has stopped.
        let again = try await session.finish()
        XCTAssertTrue(again.segments.isEmpty)
    }

    func testFinishingCanBeCancelledAndCarriedOn() async throws {
        let transcriber = try await prepared()
        let speech = try speech()
        let session = try transcriber.startSession(sampleRate: 16000, options: .init())
        session.write(Array((0..<8).map { _ in speech + silence(1) }.joined()))
        let finishing = Task { try await session.finish() }
        try await Task.sleep(nanoseconds: 100_000_000)
        finishing.cancel()
        do {
            _ = try await finishing.value
            XCTFail("96 s were not cancelled")
        } catch is CancellationError {
        }
        let transcript = try await session.finish()
        let text = (session.takeSegments() + transcript.segments).map(\.text).joined(separator: " ")
        XCTAssertGreaterThanOrEqual(count(text, "do for your country"), 8, text)
    }

    func testClosingASessionStopsItPromptly() async throws {
        let transcriber = try await prepared()
        let speech = try speech()
        let session = try transcriber.startSession(sampleRate: 16000, options: .init()) as! WhisperSession
        session.write(Array((0..<10).map { _ in speech + silence(1) }.joined()))
        let finishing = Task { try await session.finish() }
        try await Task.sleep(nanoseconds: 50_000_000)
        let start = Date()
        session.close()
        do {
            _ = try await finishing.value
            XCTFail("finish completed after close")
        } catch let error as WhisperError {
            XCTAssertEqual(error, .closed)
        }
        XCTAssertLessThan(Date().timeIntervalSince(start), 10)
    }
}
#endif
