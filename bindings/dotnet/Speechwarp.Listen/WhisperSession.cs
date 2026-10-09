using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using Speechwarp.Transcription;

namespace Speechwarp.Listen;

/// <summary>
/// A session on whisper.cpp. Writes go straight to the library, which only stores them. One worker thread does
/// all the recognising: it processes each chunk as it completes, works out the partial guess while someone is
/// reading it, and finishes. Finished segments stay in the library until <see cref="TakeSegments"/> or
/// <see cref="FinishAsync"/> collects them, which may happen on any thread.
/// </summary>
internal sealed class WhisperSession : ITranscriptionSession
{
    /// <summary>A partial guess is worked out only while it has been read within this time.</summary>
    private const long PartialInterestMilliseconds = 5000;

    /// <summary>...and only when at least this much new audio has arrived since the last guess.</summary>
    private const double PartialStepSeconds = 0.5;

    private sealed class FinishRequest(CancellationToken cancellation)
    {
        public CancellationToken Cancellation { get; } = cancellation;
        public TaskCompletionSource<Transcript> Completion { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);
        public CancellationTokenRegistration Registration { get; set; }
    }

    private readonly object _lock = new();
    private readonly SessionHandle _session;
    private readonly AutoResetEvent _wake = new(false);
    private readonly Thread _worker;
    private FinishRequest? _finish;
    private TranscriptSegment? _partial;
    private double _partialWritten = double.NegativeInfinity;
    private long _partialAsked = long.MinValue / 2; // long ago, without overflowing the subtraction
    private Exception? _failure;
    private bool _finished;
    private volatile bool _disposed;

    public WhisperSession(SessionHandle session)
    {
        _session = session;
        _worker = new Thread(Run) { IsBackground = true, Name = "Speechwarp.Listen session" };
        _worker.Start();
    }

    public event Action? SegmentsReady;

    public TranscriptSegment? Partial
    {
        get
        {
            bool wake;
            lock (_lock)
            {
                wake = Environment.TickCount64 - _partialAsked > PartialInterestMilliseconds;
                _partialAsked = Environment.TickCount64;
                if (_finished)
                    return null;
                // The first read in a while asks the worker for a guess now rather than at the next write.
                if (!wake)
                    return _partial;
            }
            Wake();
            lock (_lock)
                return _partial;
        }
    }

    public double SecondsWritten => Native.speechwarp_listen_session_seconds_written(_session);

    public double SecondsRecognised => Native.speechwarp_listen_session_seconds_recognised(_session);

    public unsafe void Write(ReadOnlySpan<float> samples)
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        if (samples.IsEmpty)
            return;
        int status;
        fixed (float* pointer = samples)
            status = Native.speechwarp_listen_session_write(_session, pointer, samples.Length);
        if (status == Native.ErrorFinished)
            throw new InvalidOperationException("The session has finished.");
        if (status != Native.Ok)
            throw Native.Failure(status);
        Wake();
    }

    public IReadOnlyList<TranscriptSegment> TakeSegments()
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        return Take();
    }

    public Task<Transcript> FinishAsync(CancellationToken cancellation = default)
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        if (cancellation.IsCancellationRequested)
            return Task.FromCanceled<Transcript>(cancellation);
        var request = new FinishRequest(cancellation);
        // Registered before the worker can see the request, so that it is there for the worker to dispose.
        request.Registration = cancellation.Register(() =>
        {
            try
            {
                Native.speechwarp_listen_session_cancel(_session);
                Wake();
            }
            catch (ObjectDisposedException)
            {
                // The session was disposed meanwhile, which ended the request anyway.
            }
        });
        lock (_lock)
        {
            if (_finish is not null)
            {
                request.Registration.Dispose();
                throw new InvalidOperationException("The session is already finishing.");
            }
            _finish = request;
        }
        Wake();
        return request.Completion.Task;
    }

    /// <summary>
    /// Stops the worker (cancelling whatever it is recognising) and frees the session. A finish still waiting fails
    /// with <see cref="ObjectDisposedException"/>.
    /// </summary>
    public void Dispose()
    {
        lock (_lock)
        {
            if (_disposed)
                return;
            _disposed = true;
        }
        try
        {
            Native.speechwarp_listen_session_cancel(_session);
        }
        catch (ObjectDisposedException)
        {
        }
        // Through Wake: a worker that was busy may already have seen _disposed, stopped and disposed the event.
        Wake();
        // From a SegmentsReady handler, on the worker itself, the worker cleans up once the handler returns.
        if (Thread.CurrentThread != _worker)
            _worker.Join();
    }

    private void Wake()
    {
        try
        {
            _wake.Set();
        }
        catch (ObjectDisposedException)
        {
            // The worker has stopped.
        }
    }

    private List<TranscriptSegment> Take()
    {
        using var result = new ResultHandle(Native.speechwarp_listen_session_take(_session));
        if (result.IsInvalid)
            throw new OutOfMemoryException("The finished segments could not be collected.");
        return Results.Segments(result);
    }

    private void Run()
    {
        try
        {
            // Checked before every wait as well as after: a Finish cut short by Dispose has used up its signal.
            while (!_disposed)
            {
                _wake.WaitOne();
                if (_disposed)
                    break;
                FinishRequest? finish;
                lock (_lock)
                    finish = _finish;
                if (finish is not null)
                {
                    Finish(finish);
                    continue;
                }
                bool stopped;
                lock (_lock)
                    stopped = _finished || _failure is not null;
                if (stopped)
                    continue;
                Process();
                UpdatePartial();
            }
        }
        finally
        {
            FinishRequest? left;
            lock (_lock)
            {
                left = _finish;
                _finish = null;
            }
            if (left is not null)
            {
                left.Registration.Dispose();
                left.Completion.TrySetException(new ObjectDisposedException(nameof(WhisperSession)));
            }
            _session.Dispose();
            _wake.Dispose();
        }
    }

    private void Process()
    {
        int status = Native.speechwarp_listen_session_process(_session);
        if (status == Native.ErrorCancelled)
        {
            // A cancel meant for a finish that has since completed, or for the session being disposed: nothing is
            // lost, so try again (the loop stops first if the session is being disposed).
            _wake.Set();
            return;
        }
        if (status < 0)
        {
            // Kept for FinishAsync, which tries the chunk again and reports the error if it fails again.
            lock (_lock)
                _failure = Native.Failure(status);
            return;
        }
        if (status > 0)
        {
            lock (_lock)
                _partial = null;
            SegmentsReady?.Invoke();
        }
    }

    private unsafe void UpdatePartial()
    {
        double written = SecondsWritten;
        lock (_lock)
        {
            if (Environment.TickCount64 - _partialAsked > PartialInterestMilliseconds
                || written - _partialWritten < PartialStepSeconds)
                return;
        }
        int error = Native.Ok;
        using var result = new ResultHandle(Native.speechwarp_listen_session_partial(_session, &error));
        if (result.IsInvalid)
            return; // cancelled, or memory ran out: no guess this time
        var guess = Results.Merge(Results.Segments(result));
        lock (_lock)
        {
            _partial = guess;
            _partialWritten = written;
        }
    }

    private void Finish(FinishRequest request)
    {
        Transcript? transcript = null;
        Exception? failure = null;
        bool cancelled = false;
        if (request.Cancellation.IsCancellationRequested)
        {
            cancelled = true;
        }
        else
        {
            int status = Native.speechwarp_listen_session_finish(_session);
            if (status == Native.ErrorCancelled)
            {
                if (_disposed)
                    return; // the loop ends and fails the request
                if (!request.Cancellation.IsCancellationRequested)
                {
                    // A cancel left over from an earlier request: try again.
                    _wake.Set();
                    return;
                }
                cancelled = true;
            }
            else if (status < 0)
            {
                failure = Native.Failure(status);
            }
            else
            {
                try
                {
                    transcript = new Transcript(Take());
                }
                catch (Exception e)
                {
                    failure = e;
                }
            }
        }
        lock (_lock)
        {
            _finish = null;
            if (transcript is not null)
            {
                _finished = true;
                _failure = null;
                _partial = null;
            }
        }
        request.Registration.Dispose();
        if (transcript is not null)
            request.Completion.TrySetResult(transcript);
        else if (cancelled)
            request.Completion.TrySetCanceled(request.Cancellation);
        else
            request.Completion.TrySetException(failure!);
    }
}
