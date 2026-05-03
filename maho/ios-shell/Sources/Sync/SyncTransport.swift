import Foundation
import UIKit
import os.log

@MainActor
final class SyncTransport {
    static let shared = SyncTransport()

    internal static let seqKey = "SyncTransport.lastServerSeq"

    private let authStore: RelayAuthStoreProtocol
    private let apiClient: RelayAPIClientProtocol
    private let bridge: MahoBridge
    private let drainOutgoing: () -> String?
    private let applyRemoteEntities: (String) -> String?
    private let log = Logger(subsystem: "dev.maho.browser", category: "SyncTransport")

    private var pollTask: Task<Void, Never>?
    private var isStarted = false
    private var isSyncing = false
    private var isPolling = false
    private var consecutiveFailures = 0
    private let maxBackoffSeconds: TimeInterval = 60
    internal private(set) var lastServerSeq: Int = 0

    init(
        authStore: RelayAuthStoreProtocol = RelayAuthStore.shared,
        apiClient: RelayAPIClientProtocol = RelayAPIClient(),
        bridge: MahoBridge = .shared,
        drainOutgoing: (() -> String?)? = nil,
        applyRemoteEntities: ((String) -> String?)? = nil
    ) {
        self.authStore = authStore
        self.apiClient = apiClient
        self.bridge = bridge
        self.drainOutgoing = drainOutgoing ?? { bridge.drainOutgoing() }
        self.applyRemoteEntities = applyRemoteEntities ?? { bridge.applyRemoteEntities(entitiesJson: $0) }
        lastServerSeq = Self.loadPersistedLastServerSeq()

        NotificationCenter.default.addObserver(
            self,
            selector: #selector(appDidBecomeActive),
            name: UIApplication.didBecomeActiveNotification,
            object: nil
        )
    }

    deinit {
        NotificationCenter.default.removeObserver(self)
    }

    @objc private func appDidBecomeActive() {
        guard isStarted else { return }
        triggerSync()
    }

    func connect() {
        isStarted = true
        ensurePollingTask()
        triggerSync()
    }

    func disconnect() {
        isStarted = false
        isPolling = false
        isSyncing = false
        consecutiveFailures = 0
        pollTask?.cancel()
        pollTask = nil
        NotificationCenter.default.post(name: .syncStatusChanged, object: nil)
    }

    func triggerSync() {
        guard isStarted, !isPolling else { return }
        ensurePollingTask()
    }

    func currentRelaySession() -> RelayAuthSession? {
        authStore.loadSession()
    }

    func clearRelaySession() {
        do {
            try authStore.clearSession()
            bridge.signOut()
            NotificationCenter.default.post(name: .syncStatusChanged, object: nil)
        } catch {
            log.error("Failed clearing relay session: \(error.localizedDescription)")
        }
    }

    internal func replaceRelaySession(_ session: RelayAuthSession) throws {
        try authStore.saveSession(session)
        bridge.signIn(
            email: session.account.email,
            displayName: session.account.displayName,
            password: nil,
            accessToken: session.accessToken,
            userId: session.account.id,
            deviceId: session.device?.id
        )
        NotificationCenter.default.post(name: .syncStatusChanged, object: nil)
    }

    internal func updateLastServerSeq(_ seq: Int) {
        guard seq >= lastServerSeq else { return }
        lastServerSeq = seq
        UserDefaults.standard.set(seq, forKey: Self.seqKey)
    }

    internal func applyEntities(_ json: String) {
        let result = applyRemoteEntities(json)
        if result == nil {
            log.error("Failed to apply remote sync entities")
            NotificationCenter.default.post(
                name: .syncStatusChanged,
                object: nil,
                userInfo: ["error": "Failed to apply incoming sync data. Some changes may not be reflected."]
            )
        }
    }

    private static func loadPersistedLastServerSeq() -> Int {
        let defaults = UserDefaults.standard
        guard defaults.object(forKey: seqKey) != nil else { return 0 }
        return defaults.integer(forKey: seqKey)
    }

    private func ensurePollingTask() {
        guard pollTask == nil || pollTask?.isCancelled == true else { return }
        pollTask = Task { [weak self] in
            guard let self else { return }
            while !Task.isCancelled, self.isStarted {
                if self.isPolling {
                    try? await Task.sleep(nanoseconds: 250_000_000)
                    continue
                }

                await self.performSyncCycle()
                if !self.isStarted { break }

                let delay = self.nextPollDelay()
                try? await Task.sleep(nanoseconds: UInt64(delay * 1_000_000_000))
            }
            self.pollTask = nil
        }
    }

    private func nextPollDelay() -> TimeInterval {
        if consecutiveFailures == 0 {
            return 30
        }
        let exponent = min(consecutiveFailures - 1, 5)
        return min(pow(2.0, Double(exponent)), maxBackoffSeconds)
    }

    private func performSyncCycle() async {
        guard isStarted else { return }
        guard var session = authStore.loadSession() else {
            NotificationCenter.default.post(
                name: .syncStatusChanged,
                object: nil,
                userInfo: ["error": "Sign in to start sync polling."]
            )
            return
        }

        if session.isRefreshTokenExpired {
            clearRelaySession()
            NotificationCenter.default.post(
                name: .syncStatusChanged,
                object: nil,
                userInfo: ["error": "Your relay session expired. Please sign in again."]
            )
            return
        }

        isPolling = true
        isSyncing = true
        NotificationCenter.default.post(name: .syncStatusChanged, object: nil)

        defer {
            isPolling = false
            isSyncing = false
            NotificationCenter.default.post(name: .syncStatusChanged, object: nil)
        }

        do {
            session = try await refreshedSessionIfNeeded(session)
            try await pushOutgoing(using: session)
            try await pullIncoming(using: session)
            consecutiveFailures = 0
        } catch RelayAPIError.unauthorized {
            do {
                let refreshed = try await apiClient.refresh(using: session.refreshToken)
                try replaceRelaySession(refreshed)
                try await pushOutgoing(using: refreshed)
                try await pullIncoming(using: refreshed)
                consecutiveFailures = 0
            } catch {
                handleSyncFailure(error)
            }
        } catch {
            handleSyncFailure(error)
        }
    }

    private func refreshedSessionIfNeeded(_ session: RelayAuthSession) async throws -> RelayAuthSession {
        guard session.isAccessTokenExpired else { return session }
        let refreshed = try await apiClient.refresh(using: session.refreshToken)
        try replaceRelaySession(refreshed)
        return refreshed
    }

    private func pushOutgoing(using session: RelayAuthSession) async throws {
        guard let outgoing = drainOutgoing(), outgoing != "[]" else { return }

        let data = Data(outgoing.utf8)
        let encodedBlob = data.base64EncodedString()
        let response = try await apiClient.push(blobs: [encodedBlob], session: session)

        for seq in response.seqs {
            updateLastServerSeq(seq)
        }
    }

    private func pullIncoming(using session: RelayAuthSession) async throws {
        var cursor = lastServerSeq

        while true {
            let response = try await apiClient.pull(sinceSeq: cursor, limit: 100, session: session)

            for message in response.messages {
                guard let data = Data(base64Encoded: message.blob),
                      let json = String(data: data, encoding: .utf8) else {
                    continue
                }

                applyEntities(json)
                cursor = max(cursor, message.seq)
                updateLastServerSeq(message.seq)
            }

            if !response.hasMore || response.messages.isEmpty {
                break
            }
        }
    }

    private func handleSyncFailure(_ error: Error) {
        consecutiveFailures += 1

        let message: String
        if let relayError = error as? LocalizedError,
           let description = relayError.errorDescription,
           !description.isEmpty {
            message = description
        } else {
            message = error.localizedDescription
        }

        log.error("Sync polling failed: \(message)")
        NotificationCenter.default.post(
            name: .syncStatusChanged,
            object: nil,
            userInfo: ["error": message]
        )
    }
}
