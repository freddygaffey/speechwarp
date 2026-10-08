import Foundation
import XCTest

import CSpeechwarp

@testable import Speechwarp

final class SpeechwarpTests: XCTestCase {
    let rate = 22050

    /// Something for the library to chew on: a gliding tone in bursts, with gaps.
    func signal(seconds: Double, channels: Int = 1) -> [Float] {
        let frames = Int(Double(rate) * seconds)
        var samples = [Float]()
        samples.reserveCapacity(frames * channels)
        var phase = 0.0
        for i in 0..<frames {
            let t = Double(i) / Double(rate)
            phase += 2 * Double.pi * (120 + 60 * sin(t * 3)) / Double(rate)
            let beat = t.truncatingRemainder(dividingBy: 0.4)
            let envelope = beat < 0.3 ? sin(Double.pi * beat / 0.3) : 0
            let value = Float(0.4 * envelope * (sin(phase) + 0.5 * sin(2 * phase) + 0.3 * sin(3 * phase)))
            samples.append(contentsOf: repeatElement(value, count: channels))
        }
        return samples
    }

    func speedUp(_ input: [Float], channels: Int = 1, speed: Float, nonlinear: Float = 1) throws -> [Float] {
        let stream = try SpeechwarpStream(sampleRate: rate, channels: channels)
        stream.speed = speed
        stream.nonlinear = nonlinear
        try stream.write(input)
        try stream.flush()
        return stream.read()
    }

    func testVersionMatchesTheHeader() throws {
        let root = URL(fileURLWithPath: #filePath).deletingLastPathComponent().deletingLastPathComponent()
            .deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
        let header = try String(contentsOf: root.appendingPathComponent("include/speechwarp.h"), encoding: .utf8)
        XCTAssertTrue(header.contains("#define SPEECHWARP_VERSION \"\(SpeechwarpStream.libraryVersion)\""))
    }

    func testDefaultsAndArguments() throws {
        let stream = try SpeechwarpStream(sampleRate: rate, channels: 2)
        XCTAssertEqual(stream.speed, 1)
        XCTAssertEqual(stream.nonlinear, 1)
        XCTAssertEqual(stream.framesAvailable, 0)
        XCTAssertEqual(stream.position, 0)
        XCTAssertEqual(stream.read(), [])

        XCTAssertThrowsError(try SpeechwarpStream(sampleRate: 100)) {
            XCTAssertEqual($0 as? SpeechwarpError, .unsupportedFormat)
        }
        XCTAssertThrowsError(try SpeechwarpStream(sampleRate: rate, channels: 0))
        XCTAssertThrowsError(try stream.write([Float](repeating: 0, count: 3))) {
            XCTAssertEqual($0 as? SpeechwarpError, .partialFrame)
        }

        stream.speed = 1000
        XCTAssertEqual(stream.speed, SpeechwarpStream.speedRange.upperBound)
        stream.speed = -1
        XCTAssertEqual(stream.speed, SpeechwarpStream.speedRange.upperBound)
        stream.nonlinear = 7
        XCTAssertEqual(stream.nonlinear, 1)
    }

    func testOutputIsShorterByTheSpeed() throws {
        let input = signal(seconds: 20)
        for (speed, nonlinear) in [(1, 1), (3, 1), (3, 0), (8, 1)] as [(Float, Float)] {
            let output = try speedUp(input, speed: speed, nonlinear: nonlinear)
            XCTAssertEqual(Float(input.count) / Float(output.count), speed, accuracy: 0.1 * speed)
        }
    }

    func testStereoAndInt16() throws {
        let input = signal(seconds: 5, channels: 2)
        let stream = try SpeechwarpStream(sampleRate: rate, channels: 2)
        stream.speed = 2
        try stream.write(input.map { Int16($0 * 32767) })
        try stream.flush()

        var buffer = [Int16](repeating: 0, count: 1000 * 2 + 1)  // one sample too many
        var total = 0
        while true {
            let frames = buffer.withUnsafeMutableBufferPointer { stream.read(into: $0) }
            if frames == 0 { break }
            XCTAssertLessThanOrEqual(frames, 1000)
            for i in 0..<frames { XCTAssertEqual(buffer[2 * i], buffer[2 * i + 1]) }
            total += frames
        }
        XCTAssertEqual(Double(input.count / 2) / Double(total), 2, accuracy: 0.2)
    }

    func testPositionFollowsWhatIsRead() throws {
        let input = signal(seconds: 20)
        let stream = try SpeechwarpStream(sampleRate: rate)
        stream.speed = 4
        var written = 0
        var last: Int64 = 0
        while true {
            if written < input.count && stream.framesAvailable < 512 {
                let end = min(written + 2000, input.count)
                try stream.write(Array(input[written..<end]))
                written = end
                if written == input.count { try stream.flush() }
                continue
            }
            let position = stream.position
            XCTAssertGreaterThanOrEqual(position, last)
            XCTAssertLessThanOrEqual(position, Int64(written))
            last = position
            if stream.read(maxFrames: 512).isEmpty { break }
        }
        XCTAssertEqual(stream.position, Int64(input.count))
    }

    func testResetDiscardsEverything() throws {
        let input = signal(seconds: 5)
        let stream = try SpeechwarpStream(sampleRate: rate)
        stream.speed = 3
        try stream.write(input)
        XCTAssertGreaterThan(stream.framesAvailable, 0)

        stream.reset()
        XCTAssertEqual(stream.framesAvailable, 0)
        XCTAssertEqual(stream.position, 0)
        XCTAssertEqual(stream.speed, 3)

        try stream.write(input)
        try stream.flush()
        XCTAssertEqual(stream.read(), try speedUp(input, speed: 3))
    }

    func testHighSpeedOptions() throws {
        let stream = try SpeechwarpStream(sampleRate: rate)
        XCTAssertEqual(stream.pauseCap, 0)
        XCTAssertTrue(stream.keepSpeed)
        XCTAssertEqual(stream.speedFloor, 0)
        XCTAssertEqual(stream.rhythmGap, 0)
        XCTAssertEqual(stream.rhythmRate, 5)
        XCTAssertNil(stream.syllableRate)
        stream.pauseCap = 5
        stream.speedFloor = 0.5
        stream.rhythmGap = 0.04
        stream.rhythmRate = 100
        stream.keepSpeed = false
        XCTAssertEqual(stream.pauseCap, 1)
        XCTAssertEqual(stream.speedFloor, 0.5)
        XCTAssertEqual(stream.rhythmGap, 0.04, accuracy: 1e-6)
        XCTAssertEqual(stream.rhythmRate, 16)
        XCTAssertFalse(stream.keepSpeed)
    }

    func testPauseCapShortensAndPositionReachesTheEnd() throws {
        let input = signal(seconds: 12)
        let stream = try SpeechwarpStream(sampleRate: rate)
        stream.nonlinear = 0
        stream.pauseCap = 0.03
        stream.keepSpeed = false
        try stream.write(input)
        try stream.flush()
        XCTAssertLessThan(Double(stream.read().count), Double(input.count) * 0.9)
        XCTAssertEqual(stream.position, Int64(input.count))
        XCTAssertNotNil(stream.syllableRate)
    }

    func testHeardPauseAndFloorBlendFollowTheSpeed() throws {
        let stream = try SpeechwarpStream(sampleRate: rate)
        XCTAssertEqual(stream.heardPause, 0)
        XCTAssertEqual(stream.floorBlend, 0)

        stream.setHeardPause(0.03, fromSpeed: 3)
        XCTAssertEqual(stream.heardPause, 0.03, accuracy: 1e-6)
        XCTAssertEqual(stream.heardPauseFrom, 3)
        stream.speed = 2
        XCTAssertEqual(stream.pauseCap, 0)
        stream.speed = 5
        XCTAssertEqual(stream.pauseCap, 0.15, accuracy: 1e-6)
        stream.speed = 20
        XCTAssertEqual(stream.pauseCap, 0.4, accuracy: 1e-6)  // clamped to 0.4 s of input

        stream.setFloorBlend(0.5, fromSpeed: 4, fullSpeed: 6)
        XCTAssertEqual(stream.floorBlend, 0.5)
        XCTAssertEqual(stream.floorBlendFrom, 4)
        XCTAssertEqual(stream.floorBlendFull, 6)
        stream.speed = 3
        XCTAssertEqual(stream.speedFloor, 0)
        stream.speed = 5
        XCTAssertEqual(stream.speedFloor, 0.25, accuracy: 1e-6)
        stream.speed = 8
        XCTAssertEqual(stream.speedFloor, 0.5, accuracy: 1e-6)

        // Setting the fixed options turns the rules off: the speed no longer moves them.
        stream.pauseCap = 0.1
        stream.speedFloor = 0.2
        stream.speed = 6
        XCTAssertEqual(stream.pauseCap, 0.1, accuracy: 1e-6)
        XCTAssertEqual(stream.speedFloor, 0.2, accuracy: 1e-6)
        XCTAssertEqual(stream.heardPause, 0)
        XCTAssertEqual(stream.floorBlend, 0)

        // NaN is ignored.
        stream.setHeardPause(0.03, fromSpeed: 3)
        stream.setHeardPause(.nan, fromSpeed: 5)
        XCTAssertEqual(stream.heardPause, 0.03, accuracy: 1e-6)
        XCTAssertEqual(stream.heardPauseFrom, 3)
    }

    func testSyllableCounterMatchesTheStream() throws {
        let input = signal(seconds: 25)
        let stream = try SpeechwarpStream(sampleRate: rate)
        let counter = try SyllableCounter(sampleRate: rate)
        XCTAssertNil(counter.rate())

        let head = rate * 5
        try stream.write(Array(input[..<head]))
        try counter.write(Array(input[..<head]))
        XCTAssertNil(stream.syllableRate)
        XCTAssertNil(counter.rate())

        try stream.write(Array(input[head...]))
        try counter.write(Array(input[head...]))
        let expected = try XCTUnwrap(stream.syllableRate)
        XCTAssertGreaterThan(expected, 0)
        XCTAssertEqual(try XCTUnwrap(counter.rate()), expected)
        XCTAssertNil(counter.rate(windowSeconds: 120, minimumSeconds: 40))  // 25 s written, 40 s needed
        XCTAssertNotNil(counter.rate(windowSeconds: 60, minimumSeconds: 0))

        counter.reset()
        XCTAssertNil(counter.rate())
    }

    func testSyllableCounterTakesInt16AndChecksArguments() throws {
        let input = signal(seconds: 12, channels: 2)
        let shorts = input.map { Int16($0 * 32767) }
        let stream = try SpeechwarpStream(sampleRate: rate, channels: 2)
        let counter = try SyllableCounter(sampleRate: rate, channels: 2)
        try stream.write(shorts)
        try counter.write(shorts)
        XCTAssertEqual(try XCTUnwrap(counter.rate()), try XCTUnwrap(stream.syllableRate))
        XCTAssertThrowsError(try counter.write([Float](repeating: 0, count: 3))) {
            XCTAssertEqual($0 as? SpeechwarpError, .partialFrame)
        }
        XCTAssertThrowsError(try SyllableCounter(sampleRate: 100)) {
            XCTAssertEqual($0 as? SpeechwarpError, .unsupportedFormat)
        }
        XCTAssertThrowsError(try SyllableCounter(sampleRate: rate, channels: 33))
    }

    // MARK: Listener trainer and blind trials

    /// A listener who follows 14 syllables a second half the time; deterministic, no noise.
    func listener(_ rate: Double) -> Double { 1 / (1 + exp((rate - 14) / 2)) }

    func runThresholdTest(_ trainer: ListenerTrainer, time: Double) -> Double {
        trainer.testBegin(priorRate: 10, time: time)
        var presented = 0.0
        while !trainer.testDone && presented < 60 {
            let rate = trainer.testRate()
            XCTAssertTrue((3.0...60.0).contains(rate))
            XCTAssertTrue(trainer.addMeasure(.intelligibility, score: listener(rate), items: 8, rate: rate, time: time + presented))
            presented += 1
        }
        return trainer.testEnd(time: time + presented)
    }

    func testTrainerEnumsHaveTheCValues() {
        XCTAssertEqual(TrainerMeasure.allCases.map { Int($0.rawValue) },
                       [SPEECHWARP_MEASURE_INTELLIGIBILITY, SPEECHWARP_MEASURE_VERIFICATION,
                        SPEECHWARP_MEASURE_RETENTION, SPEECHWARP_MEASURE_RATING].map { Int($0) })
        XCTAssertEqual(TrainerPlan.allCases.map { Int($0.rawValue) },
                       [SPEECHWARP_PLAN_STEADY, SPEECHWARP_PLAN_RAMP, SPEECHWARP_PLAN_INTERVAL,
                        SPEECHWARP_PLAN_TRACKING].map { Int($0) })
        XCTAssertEqual(Int(SPEECHWARP_PLAN_COUNT), TrainerPlan.allCases.count)
        XCTAssertEqual(Int(SPEECHWARP_PARAM_COUNT), TrainerParam.allCases.count)
        XCTAssertEqual(TrainerParam.allCases.map { Int($0.rawValue) }, Array(0..<11))
        XCTAssertEqual(Int(TrainerParam.testPrecision.rawValue), Int(SPEECHWARP_PARAM_TEST_PRECISION))
        XCTAssertEqual(Int(TrainerParam.trackingGain.rawValue), Int(SPEECHWARP_PARAM_TRACKING_GAIN))
    }

    func testTrainerWeightsAndParametersReadBack() throws {
        let trainer = try ListenerTrainer()
        XCTAssertEqual(trainer.getWeight(.intelligibility), 0.5)
        XCTAssertEqual(trainer.getWeight(.verification), 1)
        XCTAssertEqual(trainer.getWeight(.rating), 0.3, accuracy: 1e-12)
        XCTAssertEqual(trainer.getParam(.target), 0.75)
        XCTAssertEqual(trainer.getParam(.testMax), 40)
        trainer.setWeight(.rating, to: 0.1)
        trainer.setParam(.margin, to: 0.2)
        XCTAssertEqual(trainer.getWeight(.rating), 0.1)
        XCTAssertEqual(trainer.getParam(.margin), 0.2)
        XCTAssertFalse(trainer.addMeasure(.retention, score: 0.5, items: 1, rate: 10, time: 0))
        XCTAssertFalse(trainer.testDone)
        XCTAssertEqual(trainer.threshold, 0)
    }

    func testAThresholdTestFindsAThreshold() throws {
        let trainer = try ListenerTrainer(seed: 7)
        let threshold = runThresholdTest(trainer, time: 1000)
        XCTAssertGreaterThan(threshold, 0)
        XCTAssertEqual(trainer.threshold, threshold)
        XCTAssertLessThan(trainer.thresholdLow, trainer.threshold)
        XCTAssertLessThan(trainer.threshold, trainer.thresholdHigh)
        // The listener understands 75% at 14 - 2 ln 3 = 11.8 syllables a second.
        XCTAssertEqual(threshold, 11.8, accuracy: 2)
    }

    func testSessionsAndPlansWithNoData() throws {
        let trainer = try ListenerTrainer(seed: 1)
        XCTAssertEqual(trainer.sessionRate(time: 0), 0)
        XCTAssertEqual(trainer.sessionEnd(listeningHours: 1, time: 10), -1)
        for plan in TrainerPlan.allCases {
            XCTAssertEqual(trainer.planEffect(plan), 0)
            XCTAssertGreaterThan(trainer.planEffectSd(plan), 0)
            XCTAssertTrue(trainer.planRetention(plan).isNaN)
            XCTAssertTrue(trainer.planRetentionSd(plan).isNaN)
            XCTAssertEqual(trainer.planSessions(plan), 0)
        }
        XCTAssertGreaterThan(trainer.trend, 0)
        XCTAssertGreaterThan(trainer.trendSd, 0)
        var bestTotal = 0.0
        for plan in TrainerPlan.allCases {
            let p = trainer.planBestProbability(plan)
            XCTAssertTrue(p >= 0 && p <= 1)
            bestTotal += p
        }
        XCTAssertEqual(bestTotal, 1, accuracy: 1e-6)

        let threshold = runThresholdTest(trainer, time: 1000)
        trainer.sessionBegin(.steady, time: 5000)
        XCTAssertEqual(trainer.sessionRate(time: 5000), threshold * 1.1, accuracy: 1e-9 * threshold)
        XCTAssertEqual(trainer.sessionRate(time: 5600), threshold * 1.1, accuracy: 1e-9 * threshold)
        XCTAssertTrue(trainer.addMeasure(.rating, score: 0.75, items: 1, rate: threshold * 1.1, time: 5600))
        _ = runThresholdTest(trainer, time: 6000)
        let session = trainer.sessionEnd(listeningHours: 1, time: 6100)
        XCTAssertGreaterThanOrEqual(session, 0)
        XCTAssertTrue(trainer.addRetention(session: session, score: 0.8, items: 5, delaySeconds: 86400, time: 92000))
        XCTAssertFalse(trainer.addRetention(session: session, score: 2, items: 5, delaySeconds: 86400, time: 92000))
    }

    func testSameSeedSamePlans() throws {
        func plans(_ trainer: ListenerTrainer) -> [TrainerPlan] { (0..<20).map { _ in trainer.nextPlan() } }
        let a = plans(try ListenerTrainer(seed: 42))
        XCTAssertEqual(a, plans(try ListenerTrainer(seed: 42)))
        XCTAssertGreaterThan(Set(a).count, 1)
        XCTAssertEqual(plans(try ListenerTrainer(seed: 0)), plans(try ListenerTrainer()))
    }

    func testBlindTrialsChooseAndCount() throws {
        let trials = try BlindTrials(seed: 3)
        XCTAssertNil(trials.next(speed: 5))
        let a = trials.addSetting()
        let b = trials.addSetting()
        XCTAssertEqual(a, 0)
        XCTAssertEqual(b, 1)
        XCTAssertEqual(trials.addValue(setting: a, value: 0), 0)
        XCTAssertEqual(trials.addValue(setting: a, value: 0.06), 1)
        XCTAssertEqual(trials.addValue(setting: a, value: 0.06), -1)
        XCTAssertEqual(trials.addValue(setting: 9, value: 1), -1)
        trials.setAvailable(setting: b, false)

        let first = try XCTUnwrap(trials.next(speed: 5.5))
        XCTAssertEqual(first.setting, a)
        XCTAssertEqual(Set([first.first, first.second]), [0, 0.06])
        XCTAssertNil(trials.meanScore(setting: a, speed: 5.5, value: 0))
        XCTAssertNil(trials.winner(setting: a, speed: 5.5))

        // Five trials in the 5 to 6 band, each preferring neither: ties.
        for _ in 0..<5 {
            let trial = try XCTUnwrap(trials.next(speed: 5.5))
            XCTAssertTrue(trials.add(setting: trial.setting, speed: 5.5, firstValue: trial.first,
                                     secondValue: trial.second, firstScore: 0.6, secondScore: 0.6, preferred: 0))
        }
        XCTAssertEqual(trials.heard(setting: a, speed: 5.9, value: 0), 5)
        XCTAssertEqual(trials.heard(setting: a, speed: 5.0, value: 1), 5)
        XCTAssertEqual(trials.tied(setting: a, speed: 5.5, value: 0), 5)
        XCTAssertEqual(trials.won(setting: a, speed: 5.5, value: 0), 0)
        XCTAssertEqual(trials.lost(setting: a, speed: 5.5, value: 1), 0)
        XCTAssertEqual(try XCTUnwrap(trials.meanScore(setting: a, speed: 5.5, value: 0)), 0.6, accuracy: 1e-12)
        XCTAssertNil(trials.winner(setting: a, speed: 5.5))
        XCTAssertEqual(trials.heard(setting: a, speed: 7.5, value: 0), 0)  // another band
        XCTAssertFalse(trials.add(setting: a, speed: 5.5, firstValue: 0, secondValue: 0.5, firstScore: 0.5,
                                  secondScore: 0.5, preferred: 0))  // 0.5 is not a value added
    }

    func testBlindTrialsNameAWinner() throws {
        let trials = try BlindTrials(seed: 11)
        let a = trials.addSetting()
        trials.addValue(setting: a, value: 0)
        trials.addValue(setting: a, value: 1)
        trials.setConfidence(0.9)
        for _ in 0..<12 {
            XCTAssertTrue(trials.add(setting: a, speed: 6.5, firstValue: 0, secondValue: 1, firstScore: 0.4,
                                     secondScore: 0.8, preferred: 1))
        }
        XCTAssertEqual(trials.winner(setting: a, speed: 6.5), 1)
        XCTAssertEqual(trials.won(setting: a, speed: 6.5, value: 1), 12)
        XCTAssertEqual(trials.lost(setting: a, speed: 6.5, value: 0), 12)
        XCTAssertNotNil(trials.meanScore(setting: a, speed: 6.5, value: 1))
    }

    func testSameSeedSameTrials() throws {
        func run(_ seed: UInt64) throws -> [BlindTrial] {
            let trials = try BlindTrials(seed: seed)
            let a = trials.addSetting()
            for value in [1.0, 2, 3] { trials.addValue(setting: a, value: value) }
            return try (0..<10).map { _ in
                let trial = try XCTUnwrap(trials.next(speed: 4.5))
                trials.add(setting: a, speed: 4.5, firstValue: trial.first, secondValue: trial.second,
                           firstScore: 0.5, secondScore: 0.5, preferred: 0)
                return trial
            }
        }
        XCTAssertEqual(try run(5), try run(5))
    }
}
