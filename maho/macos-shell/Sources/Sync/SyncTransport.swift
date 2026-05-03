import AppKit
import Foundation

final class SyncTransport {
    static let shared = SyncTransport()

    private let pollInterval: TimeInterval = 30
    private let maxBackoffDelay: TimeInterval = 300
    private let minimumManualSyncInterval: TimeInterval = 2
    private let pullPageLimit = 100
    private let queue = DispatchQueue(label: "com.maho.sync.transport", qos: .utility)
    private let stateQueue = DispatchQueue(label: "com.maho.sync.transport.state", qos: .userInitiated)

    private var serverURL: String?
    private var syncKey: String?
    private var pollTimer: Timer?
    private var retryWorkItem: DispatchWorkItem?
    private var isSyncEnabled = false
    private var isSyncInFlight = false
    private(set) var isConnected = false
    private var reconnectAttempts = 0
    private var lastServerSeq: Int = 0
    private var hasRecordedServerSeq = false
    private var lastSyncAttemptAt: Date?
    private static let seqKey = "SyncTransport.lastServerSeq"
    private static let hasSeqKey = "SyncTransport.hasLastServerSeq"

    private init() {
        lastServerSeq = UserDefaults.standard.integer(forKey: Self.seqKey)
        hasRecordedServerSeq = UserDefaults.standard.bool(forKey: Self.hasSeqKey)
        NotificationCenter.default.addObserver(
            self,
            selector: #selector(handleAppDidBecomeActive),
            name: NSApplication.didBecomeActiveNotification,
            object: nil
        )
    }

    deinit {
        NotificationCenter.default.removeObserver(self)
    }

    func connect(serverUrl: String, syncKey: String) {
        let trimmedKey = syncKey.trimmingCharacters(in: .whitespacesAndNewlines)
        guard RelayServerLocator.normalizedBaseURL(from: serverUrl) != nil else {
            publishError("Invalid sync server URL. Must be an http:// or https:// URL.")
            return
        }
        guard !trimmedKey.isEmpty else {
            publishError("Enter a sync key before starting sync.")
            return
        }
        guard RelayAuthManager.shared.hasSession(for: serverUrl) else {
            publishError("Log in to the relay before starting sync.")
            return
        }

        stateQueue.sync {
            serverURL = serverUrl
            self.syncKey = trimmedKey
            reconnectAttempts = 0
            isSyncEnabled = true
            cancelRetryLocked()
        }

        DispatchQueue.main.async { [weak self] in
            self?.startPollTimer()
        }
        scheduleImmediateSync(reason: "start")
    }

    func disconnect() {
        stateQueue.sync {
            isSyncEnabled = false
            isConnected = false
            isSyncInFlight = false
            reconnectAttempts = 0
            cancelRetryLocked()
        }
        DispatchQueue.main.async { [weak self] in
            self?.pollTimer?.invalidate()
            self?.pollTimer = nil
        }
        NotificationCenter.default.post(name: .syncStatusChanged, object: nil)
    }

    func triggerSync() {
        scheduleImmediateSync(reason: "manual")
    }

    func resumeIfPossible() {
        stateQueue.sync {
            guard isSyncEnabled else { return }
        }
        scheduleImmediateSync(reason: "resume")
    }

    private func startPollTimer() {
        pollTimer?.invalidate()
        pollTimer = Timer.scheduledTimer(withTimeInterval: pollInterval, repeats: true) { [weak self] _ in
            self?.scheduleImmediateSync(reason: "timer")
        }
        if let pollTimer {
            RunLoop.main.add(pollTimer, forMode: .common)
        }
    }

    private func scheduleImmediateSync(reason: String) {
        let shouldRunNow = stateQueue.sync { () -> Bool in
            guard isSyncEnabled else { return false }
            let now = Date()
            if reason == "manual",
               let lastSyncAttemptAt,
               now.timeIntervalSince(lastSyncAttemptAt) < minimumManualSyncInterval {
                return false
            }
            lastSyncAttemptAt = now
            return true
        }
        guard shouldRunNow else { return }

        queue.async { [weak self] in
            Task {
                await self?.performSyncCycle(reason: reason)
            }
        }
    }

    @objc private func handleAppDidBecomeActive() {
        resumeIfPossible()
    }

    @MainActor
    private func performSyncCycle(reason: String) async {
        let config = stateQueue.sync { () -> (String, String)? in
            guard isSyncEnabled,
                  let serverURL,
                  let syncKey else {
                return nil
            }
            guard !isSyncInFlight else { return nil }
            isSyncInFlight = true
            return (serverURL, syncKey)
        }
        guard let (serverURL, syncKey) = config else { return }

        NotificationCenter.default.post(name: .syncStatusChanged, object: nil)

        do {
            try await pushChanges(serverURL: serverURL, syncKey: syncKey)
            try await pullChanges(serverURL: serverURL, syncKey: syncKey)
            stateQueue.sync {
                isConnected = true
                reconnectAttempts = 0
                isSyncInFlight = false
                cancelRetryLocked()
            }
            NotificationCenter.default.post(name: .syncStatusChanged, object: nil)
        } catch {
            let message = (error as? LocalizedError)?.errorDescription ?? error.localizedDescription
            stateQueue.sync {
                isConnected = false
                isSyncInFlight = false
            }
            scheduleRetry(afterError: message, reason: reason)
            publishError(message)
        }
    }

    @MainActor
    private func pushChanges(serverURL: String, syncKey _: String) async throws {
        guard let outgoing = MahoBridge.shared.drainOutgoing() else {
            return
        }

        let blobs = RelayPayloadCodec.pushBlobs(from: outgoing)
        guard !blobs.isEmpty else { return }

        let body: [String: Any] = ["blobs": blobs]
        _ = try await RelayAuthManager.shared.performAuthorizedJSONRequest(
            serverURL: serverURL,
            endpointVariants: ["sync/push"],
            jsonBody: body
        )
    }

    @MainActor
    private func pullChanges(serverURL: String, syncKey _: String) async throws {
        var nextSinceSeq = stateQueue.sync { lastServerSeq }
        var shouldContinue = true

        while shouldContinue {
            let queryItems = [
                URLQueryItem(name: "since_seq", value: String(nextSinceSeq)),
                URLQueryItem(name: "limit", value: String(pullPageLimit)),
            ]
            let (data, _) = try await RelayAuthManager.shared.performAuthorizedGETRequest(
                serverURL: serverURL,
                endpointVariants: ["sync/pull"],
                queryItems: queryItems
            )

            guard let envelope = RelayPayloadCodec.pullEnvelope(from: data) else {
                throw RelayAuthError.invalidResponse
            }

            for message in envelope.messages {
                if let decodedBlob = RelayPayloadCodec.decodedBlobString(from: message.blob) {
                    applyEntities(decodedBlob)
                }
                updateLastServerSeq(message.seq)
                nextSinceSeq = message.seq
            }

            shouldContinue = envelope.hasMore
        }
    }

    private func applyEntities(_ json: String) {
        let result = MahoBridge.shared.applyRemoteEntities(entitiesJson: json)
        if result == nil {
            publishError("Failed to apply incoming sync data. Some changes may not be reflected.")
        }
    }

    private func updateLastServerSeq(_ seq: Int) {
        stateQueue.sync {
            if !hasRecordedServerSeq || seq > lastServerSeq {
                lastServerSeq = seq
                hasRecordedServerSeq = true
                UserDefaults.standard.set(seq, forKey: Self.seqKey)
                UserDefaults.standard.set(true, forKey: Self.hasSeqKey)
            }
        }
    }

    private func scheduleRetry(afterError message: String, reason: String) {
        let delay = stateQueue.sync { () -> TimeInterval in
            guard isSyncEnabled else { return 0 }
            reconnectAttempts += 1
            let baseDelay = min(pow(2.0, Double(max(reconnectAttempts - 1, 0))), maxBackoffDelay)
            let jittered = baseDelay * (0.5 + Double.random(in: 0 ... 0.5))
            cancelRetryLocked()
            let workItem = DispatchWorkItem { [weak self] in
                self?.scheduleImmediateSync(reason: "retry-\(reason)")
            }
            retryWorkItem = workItem
            queue.asyncAfter(deadline: .now() + jittered, execute: workItem)
            return jittered
        }

        guard delay > 0 else { return }
        NSLog("[SyncTransport] Polling retry in \(delay)s after error: \(message)")
    }

    private func cancelRetryLocked() {
        retryWorkItem?.cancel()
        retryWorkItem = nil
    }

    private func publishError(_ message: String) {
        NotificationCenter.default.post(
            name: .syncStatusChanged,
            object: nil,
            userInfo: ["error": message]
        )
    }
}
