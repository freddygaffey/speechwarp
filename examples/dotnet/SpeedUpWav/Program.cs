// Speed up a 16-bit PCM WAV file, streaming it through in pieces the way a player would.
//
//     bindings/dotnet/build-native.sh
//     dotnet run --project examples/dotnet/SpeedUpWav -- talk.wav talk-3x.wav 3
using System.Buffers.Binary;
using System.Runtime.InteropServices;
using Speechwarp;

if (args.Length < 2)
{
    Console.Error.WriteLine("usage: SpeedUpWav input.wav output.wav [speed]");
    return 2;
}
float speed = args.Length > 2 ? float.Parse(args[2], System.Globalization.CultureInfo.InvariantCulture) : 2;

// A minimal WAV reader: find the "fmt " and "data" chunks.
byte[] file = File.ReadAllBytes(args[0]);
int sampleRate = 0, channels = 0, bits = 0;
ReadOnlySpan<byte> data = default;
for (int at = 12; at + 8 <= file.Length;)
{
    string id = System.Text.Encoding.ASCII.GetString(file, at, 4);
    int size = BinaryPrimitives.ReadInt32LittleEndian(file.AsSpan(at + 4));
    if (id == "fmt ")
    {
        channels = BinaryPrimitives.ReadInt16LittleEndian(file.AsSpan(at + 10));
        sampleRate = BinaryPrimitives.ReadInt32LittleEndian(file.AsSpan(at + 12));
        bits = BinaryPrimitives.ReadInt16LittleEndian(file.AsSpan(at + 22));
    }
    else if (id == "data")
    {
        data = file.AsSpan(at + 8, Math.Min(size, file.Length - at - 8));
    }
    at += 8 + size + (size & 1);
}
if (data.IsEmpty || bits != 16 || !BitConverter.IsLittleEndian)
{
    Console.Error.WriteLine("this example reads 16-bit PCM WAV only");
    return 1;
}
ReadOnlySpan<short> samples = MemoryMarshal.Cast<byte, short>(data);

using var stream = new SpeechwarpStream(sampleRate, channels) { Speed = speed };
using var output = new MemoryStream();
var buffer = new short[4096 * channels];

void Drain()
{
    int frames;
    while ((frames = stream.Read(buffer)) > 0) // frames, not samples
        output.Write(MemoryMarshal.AsBytes(buffer.AsSpan(0, frames * channels)));
}

// Any piece size gives the same result; a player would write whatever its decoder hands it.
int piece = 8192 * channels;
for (int at = 0; at < samples.Length; at += piece)
{
    stream.Write(samples.Slice(at, Math.Min(piece, samples.Length - at)));
    Drain();
}
stream.Flush(); // the input has ended: let out what was held back
Drain();

using (var wav = new BinaryWriter(File.Create(args[1])))
{
    wav.Write("RIFF"u8);
    wav.Write((int)(36 + output.Length));
    wav.Write("WAVEfmt "u8);
    wav.Write(16);
    wav.Write((short)1);
    wav.Write((short)channels);
    wav.Write(sampleRate);
    wav.Write(sampleRate * channels * 2);
    wav.Write((short)(channels * 2));
    wav.Write((short)16);
    wav.Write("data"u8);
    wav.Write((int)output.Length);
    wav.Write(output.GetBuffer(), 0, (int)output.Length);
}

double secondsIn = (double)samples.Length / channels / sampleRate;
double secondsOut = (double)output.Length / 2 / channels / sampleRate;
Console.WriteLine($"{secondsIn:F1} s in, {secondsOut:F1} s out, {secondsIn / secondsOut:F2}x");
return 0;
