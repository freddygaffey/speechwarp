#if os(iOS) || os(macOS)
import Foundation
import Speechwarp
import speechwarp_listen

/// A session on whisper.cpp, through the engine-independent `TranscriptionSession` interface. Writes go straight to
/// the library, which only stores them; a worker thread of the session's own does all the recognising.
///
/// Write after `finish` is ignored, as for Apple's sessions. Call `close` to stop at once (it is also done when the
/// session is released); a handler in `onSegmentsReady` that holds the session keeps it alive until `finish`
/// completes or `close` is called.
@available(macOS 13.3, iOS 16.4, *)
public final class WhisperSession: TranscriptionSession, @unchecked Sendable {
    private let core: SessionCore

    init(_ core: SessionCore) {
        self.core = core
        let thread = Thread { core.run() }
        thread.name = "SpeechwarpListen session"
        thread.qualityOfService = .userInitiated
        thread.start()
    }

    deinit {
        core.stop()
    }

    public func write(_ samples: [Float]) {
        guard !samples.isEmpty else { return }
        let status = samples.withUnsafeBufferPointer {
            speechwarp_listen_session_write(core.session, $0.baseAddress, Int64($0.count))
        }
        if status == SPEECHWARP_LISTEN_OK { core.wake() }
    }

    public func takeSegments() -> [TranscriptSegment] {
        core.take()
    }

    public var partial: TranscriptSegment? { core.partial }

    public var secondsWritten: Double { speechwarp_listen_session_seconds_written(core.session) }

    public var secondsRecognised: Double { speechwarp_listen_session_seconds_recognised(core.session) }

    public var onSegmentsReady: (@Sendable () -> Void)? {
        get { core.withLock { core.handler } }
        set { core.withLock { core.handler = newValue } }
    }

    /// Ends the input and returns everything not yet taken. Cancelling the task stops it soon, throwing
    /// `CancellationError`; nothing is lost, and calling `finish` again carries on.
    public func finish() async throws -> Transcript {
        let request = FinishRequest()
        return try await withTaskCancellationHandler {
            try await withCheckedThrowingContinuation { (continuation: CheckedContinuation<Transcript, Error>) in
                core.submit(request, continuation)
            }
        } onCancel: {
            core.cancel(request)
        }
    }

    /// Stops the worker, cancelling whatever it is recognising; a `finish` still running throws
    /// `WhisperError.closed`. The session can still be read (`takeSegments`, the seconds) but recognises nothing more.
    public func close() {
        core.stop()
    }
}

/// A finish waiting for the worker.
final class FinishRequest: @unchecked Sendable {
    var continuation: CheckedContinuation<Transcript, Error>?
    var cancelled = false
}

/// The session's state, shared by the caller and the worker thread, which holds it until it stops.
final class SessionCore: @unchecked Sendable {
    /// A partial guess is worked out only while it has been read within this many seconds...
    private static let partialInterest = 5.0
    /// ...and only when at least this much new audio has arrived since the last guess.
    private static let partialStep = 0.5

    let session: OpaquePointer
    private let model: ModelBox
    private let condition = NSCondition()
    private var signalled = false
    private var stopping = false
    private var finished = false
    private var failed = false
    private var request: FinishRequest?
    private var guess: TranscriptSegment?
    private var guessWritten = -Double.infinity
    private var guessAsked = -Double.infinity
    var handler: (@Sendable () -> Void)?

    init(session: OpaquePointer, model: ModelBox) {
        self.session = session
        self.model = model
    }

    deinit {
        // The model is released after this, so it outlives the session as the library requires.
        speechwarp_listen_session_free(session)
    }

    func withLock<T>(_ body: () throws -> T) rethrows -> T {
        condition.lock()
        defer { condition.unlock() }
        return try body()
    }

    func wake() {
        withLock {
            signalled = true
            condition.signal()
        }
    }

    func take() -> [TranscriptSegment] {
        guard let result = speechwarp_listen_session_take(session) else { return [] }
        return Results.take(result)
    }

    var partial: TranscriptSegment? {
        let now = ProcessInfo.processInfo.systemUptime
        let (value, wakeNow): (TranscriptSegment?, Bool) = withLock {
            let first = now - guessAsked > Self.partialInterest
            guessAsked = now
            return (finished ? nil : guess, first)
        }
        // The first read in a while asks the worker for a guess now rather than at the next write.
        if wakeNow { wake() }
        return value
    }

    func submit(_ request: FinishRequest, _ continuation: CheckedContinuation<Transcript, Error>) {
        let (refusal, alreadyFinished): (Error?, Bool) = withLock {
            // The worker has stopped after finishing: answer here, as the C# session does, with what is left.
            if finished && self.request == nil { return (nil, true) }
            if stopping { return (WhisperError.closed, false) }
            if self.request != nil { return (WhisperError.alreadyFinishing, false) }
            request.continuation = continuation
            self.request = request
            signalled = true
            condition.signal()
            return (nil, false)
        }
        if let refusal {
            continuation.resume(throwing: refusal)
        } else if alreadyFinished {
            continuation.resume(returning: Transcript(segments: take()))
        }
    }

    func cancel(_ request: FinishRequest) {
        let current: Bool = withLock {
            request.cancelled = true
            return self.request === request || request.continuation == nil
        }
        if current {
            speechwarp_listen_session_cancel(session)
            wake()
        }
    }

    func stop() {
        withLock {
            stopping = true
            signalled = true
            condition.signal()
        }
        speechwarp_listen_session_cancel(session)
    }

    /// The worker: waits for audio or a request, and recognises.
    func run() {
        while true {
            let (stop, pending, idle): (Bool, FinishRequest?, Bool) = withLock {
                while !signalled && !stopping { condition.wait() }
                signalled = false
                return (stopping, request, finished || failed)
            }
            if stop { break }
            if let pending {
                if finish(pending) { break }
                continue
            }
            if idle { continue }
            process()
            updatePartial()
        }
        let left: FinishRequest? = withLock {
            defer { request = nil }
            return request
        }
        left?.continuation?.resume(throwing: WhisperError.closed)
    }

    private func process() {
        let status = speechwarp_listen_session_process(session)
        if status == SPEECHWARP_LISTEN_ERROR_CANCELLED {
            // A cancel meant for a finish that has since completed, or for stopping: nothing is lost, so try again
            // (the loop ends first when stopping).
            wake()
        } else if status < 0 {
            // finish tries the chunk again and reports the error if it fails again.
            withLock { failed = true }
        } else if status > 0 {
            let handler: (@Sendable () -> Void)? = withLock {
                guess = nil
                return self.handler
            }
            handler?()
        }
    }

    private func updatePartial() {
        let written = speechwarp_listen_session_seconds_written(session)
        let wanted: Bool = withLock {
            ProcessInfo.processInfo.systemUptime - guessAsked <= Self.partialInterest
                && written - guessWritten >= Self.partialStep
        }
        guard wanted else { return }
        var error: Int32 = SPEECHWARP_LISTEN_OK
        guard let result = speechwarp_listen_session_partial(session, &error) else { return }
        let segment = Results.merge(Results.take(result))
        withLock {
            guess = segment
            guessWritten = written
        }
    }

    /// Finishes for a request. Returns true when the session is done and the worker can stop.
    private func finish(_ pending: FinishRequest) -> Bool {
        if withLock({ pending.cancelled }) {
            complete(pending, with: .failure(CancellationError()))
            return false
        }
        let status = speechwarp_listen_session_finish(session)
        if status == SPEECHWARP_LISTEN_ERROR_CANCELLED {
            let (stop, cancelled) = withLock { (stopping, pending.cancelled) }
            if stop { return true } // the request fails as the loop ends
            if !cancelled {
                wake() // a cancel left over from an earlier request: try again
                return false
            }
            complete(pending, with: .failure(CancellationError()))
            return false
        }
        if status < 0 {
            complete(pending, with: .failure(WhisperError.code(status)))
            return false
        }
        let transcript = Transcript(segments: take())
        withLock {
            finished = true
            failed = false
            guess = nil
        }
        complete(pending, with: .success(transcript))
        return true
    }

    private func complete(_ pending: FinishRequest, with result: Result<Transcript, Error>) {
        let continuation: CheckedContinuation<Transcript, Error>? = withLock {
            if request === pending { request = nil }
            defer { pending.continuation = nil }
            return pending.continuation
        }
        continuation?.resume(with: result)
    }
}
#endif
