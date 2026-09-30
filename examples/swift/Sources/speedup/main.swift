// Speed up an audio file with AVFoundation and Speechwarp.
//
//     cd examples/swift
//     swift run speedup talk.wav talk-3x.wav 3
//
// AVAudioFile reads any format the system can decode. The output is 16-bit WAV.
import AVFoundation
import Speechwarp

let arguments = CommandLine.arguments
guard arguments.count >= 3 else {
    print("usage: speedup input output.wav [speed]")
    exit(2)
}

// Ask AVFoundation for interleaved floats, which is what Speechwarp takes.
let reader = try AVAudioFile(
    forReading: URL(fileURLWithPath: arguments[1]), commonFormat: .pcmFormatFloat32, interleaved: true)
let format = reader.processingFormat
let sampleRate = Int(format.sampleRate)
let channels = Int(format.channelCount)

var output: AVAudioFile? = try AVAudioFile(
    forWriting: URL(fileURLWithPath: arguments[2]),
    settings: [
        AVFormatIDKey: kAudioFormatLinearPCM, AVSampleRateKey: format.sampleRate,
        AVNumberOfChannelsKey: channels, AVLinearPCMBitDepthKey: 16,
    ],
    commonFormat: .pcmFormatFloat32, interleaved: true)

let stream = try SpeechwarpStream(sampleRate: sampleRate, channels: channels)
stream.speed = arguments.count > 3 ? Float(arguments[3]) ?? 2 : 2

let inBuffer = AVAudioPCMBuffer(pcmFormat: format, frameCapacity: 8192)!
let outBuffer = AVAudioPCMBuffer(pcmFormat: format, frameCapacity: 8192)!
var framesOut = 0

/// Move whatever the stream has ready into the output file.
func drain() throws {
    while true {
        let samples = UnsafeMutableBufferPointer(
            start: outBuffer.floatChannelData![0], count: Int(outBuffer.frameCapacity) * channels)
        let frames = stream.read(into: samples)  // frames, not samples
        if frames == 0 { return }
        outBuffer.frameLength = AVAudioFrameCount(frames)
        try output?.write(from: outBuffer)
        framesOut += frames
    }
}

while reader.framePosition < reader.length {
    try reader.read(into: inBuffer)
    let samples = UnsafeBufferPointer(
        start: inBuffer.floatChannelData![0], count: Int(inBuffer.frameLength) * channels)
    try stream.write(samples)
    try drain()
}
try stream.flush()  // the input has ended: let out what was held back
try drain()
output = nil  // AVAudioFile finishes the file when it is released

let secondsIn = Double(reader.length) / Double(sampleRate)
let secondsOut = Double(framesOut) / Double(sampleRate)
print(String(format: "%.1f s in, %.1f s out, %.2fx", secondsIn, secondsOut, secondsIn / secondsOut))
