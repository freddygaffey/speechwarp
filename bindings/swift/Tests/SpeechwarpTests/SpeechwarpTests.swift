import Foundation
import XCTest

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
}
