using System.Text.RegularExpressions;
using Speechwarp;

namespace Speechwarp.Tests;

[TestClass]
public class StreamTests
{
    private const int Rate = 22050;

    /// <summary>Something for the library to chew on: a gliding tone in bursts, with gaps.</summary>
    private static float[] Signal(double seconds, int channels = 1)
    {
        int frames = (int)(Rate * seconds);
        var samples = new float[frames * channels];
        double phase = 0;
        for (int i = 0; i < frames; i++)
        {
            double t = (double)i / Rate;
            phase += 2 * Math.PI * (120 + 60 * Math.Sin(t * 3)) / Rate;
            double envelope = t % 0.4 < 0.3 ? Math.Sin(Math.PI * (t % 0.4) / 0.3) : 0;
            float value = (float)(0.4 * envelope * (Math.Sin(phase) + 0.5 * Math.Sin(2 * phase) + 0.3 * Math.Sin(3 * phase)));
            for (int c = 0; c < channels; c++)
                samples[i * channels + c] = value;
        }
        return samples;
    }

    private static float[] ReadAll(SpeechwarpStream stream)
    {
        var all = new List<float>();
        var buffer = new float[4096 * stream.Channels];
        int frames;
        while ((frames = stream.Read(buffer)) > 0)
            all.AddRange(buffer.AsSpan(0, frames * stream.Channels));
        return all.ToArray();
    }

    [TestMethod]
    public void NativeVersionMatchesTheHeaderAndThePackage()
    {
        string root = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", "..", "..", "..", "..", ".."));
        string header = File.ReadAllText(Path.Combine(root, "include", "speechwarp.h"));
        string project = File.ReadAllText(Path.Combine(root, "bindings", "dotnet", "Speechwarp", "Speechwarp.csproj"));
        string inHeader = Regex.Match(header, "#define SPEECHWARP_VERSION \"(.*)\"").Groups[1].Value;
        string inProject = Regex.Match(project, "<Version>(.*)</Version>").Groups[1].Value;

        Assert.AreEqual(inHeader, SpeechwarpStream.NativeVersion);
        Assert.AreEqual(inHeader, inProject);
    }

    [TestMethod]
    public void StartsAtSpeedOneWithNonlinearOn()
    {
        using var stream = new SpeechwarpStream(Rate, 2);
        Assert.AreEqual(1f, stream.Speed);
        Assert.AreEqual(1f, stream.Nonlinear);
        Assert.AreEqual(Rate, stream.SampleRate);
        Assert.AreEqual(2, stream.Channels);
        Assert.AreEqual(0, stream.FramesAvailable);
        Assert.AreEqual(0, stream.Position);
    }

    [TestMethod]
    public void RejectsBadArguments()
    {
        Assert.ThrowsExactly<ArgumentOutOfRangeException>(() => new SpeechwarpStream(100, 1));
        Assert.ThrowsExactly<ArgumentOutOfRangeException>(() => new SpeechwarpStream(Rate, 0));
        Assert.ThrowsExactly<ArgumentOutOfRangeException>(() => new SpeechwarpStream(Rate, 33));

        using var stream = new SpeechwarpStream(Rate, 2);
        Assert.ThrowsExactly<ArgumentOutOfRangeException>(() => stream.Speed = 0);
        Assert.ThrowsExactly<ArgumentOutOfRangeException>(() => stream.Speed = float.NaN);
        Assert.ThrowsExactly<ArgumentOutOfRangeException>(() => stream.Nonlinear = float.NaN);
        Assert.ThrowsExactly<ArgumentException>(() => stream.Write(new float[3]));
    }

    [TestMethod]
    public void ClampsSpeedAndNonlinear()
    {
        using var stream = new SpeechwarpStream(Rate, 1);
        stream.Speed = 1000;
        Assert.AreEqual(SpeechwarpStream.MaxSpeed, stream.Speed);
        stream.Speed = 0.0001f;
        Assert.AreEqual(SpeechwarpStream.MinSpeed, stream.Speed);
        stream.Nonlinear = 5;
        Assert.AreEqual(1f, stream.Nonlinear);
        stream.Nonlinear = -5;
        Assert.AreEqual(0f, stream.Nonlinear);
    }

    [TestMethod]
    [DataRow(1f, 1f)]
    [DataRow(3f, 1f)]
    [DataRow(3f, 0f)]
    [DataRow(8f, 1f)]
    public void OutputIsShorterByTheSpeed(float speed, float nonlinear)
    {
        float[] input = Signal(20);
        using var stream = new SpeechwarpStream(Rate, 1) { Speed = speed, Nonlinear = nonlinear };
        stream.Write(input);
        stream.Flush();
        float[] output = ReadAll(stream);

        Assert.AreEqual(speed, (double)input.Length / output.Length, 0.1 * speed);
        Assert.AreEqual(input.Length, stream.Position);
    }

    [TestMethod]
    public void StereoAndShortSamplesWork()
    {
        float[] input = Signal(5, channels: 2);
        short[] shorts = Array.ConvertAll(input, x => (short)(x * 32767));
        using var stream = new SpeechwarpStream(Rate, 2) { Speed = 2 };
        stream.Write(shorts);
        stream.Flush();

        var buffer = new short[1000 * 2 + 1]; // one sample too many: only whole frames are written
        int total = 0, frames;
        while ((frames = stream.Read(buffer)) > 0)
        {
            Assert.IsLessThanOrEqualTo(1000, frames);
            for (int i = 0; i < frames; i++)
                Assert.AreEqual(buffer[2 * i], buffer[2 * i + 1]);
            total += frames;
        }
        Assert.AreEqual(2.0, (double)(input.Length / 2) / total, 0.2);
    }

    [TestMethod]
    public void PositionFollowsWhatIsRead()
    {
        float[] input = Signal(20);
        using var stream = new SpeechwarpStream(Rate, 1) { Speed = 4 };
        var buffer = new float[512];
        long last = 0;
        int written = 0;

        while (true)
        {
            if (written < input.Length && stream.FramesAvailable < 512)
            {
                int n = Math.Min(2000, input.Length - written);
                stream.Write(input.AsSpan(written, n));
                written += n;
                if (written == input.Length)
                    stream.Flush();
                continue;
            }
            long position = stream.Position;
            Assert.IsGreaterThanOrEqualTo(last, position);
            Assert.IsLessThanOrEqualTo(written, position);
            last = position;
            if (stream.Read(buffer) == 0)
                break;
        }
        Assert.AreEqual(input.Length, stream.Position);
    }

    [TestMethod]
    public void ResetDiscardsEverything()
    {
        float[] input = Signal(5);
        using var stream = new SpeechwarpStream(Rate, 1) { Speed = 3 };
        stream.Write(input);
        Assert.IsGreaterThan(0, stream.FramesAvailable);

        stream.Reset();
        Assert.AreEqual(0, stream.FramesAvailable);
        Assert.AreEqual(0, stream.Position);
        Assert.AreEqual(3f, stream.Speed);

        stream.Write(input);
        stream.Flush();
        float[] afterReset = ReadAll(stream);

        using var fresh = new SpeechwarpStream(Rate, 1) { Speed = 3 };
        fresh.Write(input);
        fresh.Flush();
        CollectionAssert.AreEqual(ReadAll(fresh), afterReset);
    }

    [TestMethod]
    public void UsingAStreamAfterDisposeThrows()
    {
        var stream = new SpeechwarpStream(Rate, 1);
        stream.Dispose();
        stream.Dispose();
        Assert.ThrowsExactly<ObjectDisposedException>(() => stream.Speed = 2);
        Assert.ThrowsExactly<ObjectDisposedException>(() => stream.Write(new float[10]));
    }
    [TestMethod]
    public void HighSpeedOptionsStartOffAndClamp()
    {
        using var stream = new SpeechwarpStream(Rate, 1);
        Assert.AreEqual(0f, stream.PauseCap);
        Assert.IsTrue(stream.KeepSpeed);
        Assert.AreEqual(0f, stream.SpeedFloor);
        Assert.AreEqual(0f, stream.RhythmGap);
        Assert.AreEqual(5f, stream.RhythmRate);
        Assert.IsNull(stream.SyllableRate);

        stream.PauseCap = 5;
        stream.SpeedFloor = 0.5f;
        stream.RhythmGap = 0.04f;
        stream.RhythmRate = 100;
        stream.KeepSpeed = false;
        Assert.AreEqual(1f, stream.PauseCap);
        Assert.AreEqual(0.5f, stream.SpeedFloor);
        Assert.AreEqual(0.04f, stream.RhythmGap, 1e-6f);
        Assert.AreEqual(16f, stream.RhythmRate);
        Assert.IsFalse(stream.KeepSpeed);
        Assert.ThrowsExactly<ArgumentOutOfRangeException>(() => stream.PauseCap = float.NaN);
        Assert.ThrowsExactly<ArgumentOutOfRangeException>(() => stream.RhythmRate = 0);
    }

    [TestMethod]
    public void PauseCapShortensPausesAndPositionStillReachesTheEnd()
    {
        float[] input = Signal(10); // 0.1 s gaps every 0.4 s
        using var stream = new SpeechwarpStream(Rate, 1) { Nonlinear = 0, PauseCap = 0.03f, KeepSpeed = false };
        stream.Write(input);
        stream.Flush();
        float[] output = ReadAll(stream);
        Assert.IsLessThan(input.Length * 0.9, output.Length);
        Assert.AreEqual(input.Length, stream.Position);
    }

    [TestMethod]
    public void SyllableRateAppearsAfterTenSeconds()
    {
        using var stream = new SpeechwarpStream(Rate, 1) { RhythmGap = 0.04f };
        stream.Write(Signal(12)); // a burst every 0.4 s: 2.5 a second
        Assert.AreEqual(2.5, stream.SyllableRate!.Value, 0.5);
        stream.Reset();
        Assert.IsNull(stream.SyllableRate);
    }
}
