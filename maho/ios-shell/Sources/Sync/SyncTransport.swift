import Foundation
import os.log

/// Outcome of one bounded V2 sync cycle.
///
/// `SyncManager` uses this to decide whether the background task completed
/// successfully and whether it is worth rescheduling promptly.
enum SyncCycleOutcome: Equatable {
    /// The cycle ran end to end (push + paginated pull) without error.
    case completed(pushed: Int, applied: Int)
    /// The cycle did not run because there is nothing to do (no session,
    /// sync disabled, or another cycle already in flight). Not an error.
    case skipped(reason: String)
    /// The cycle failed. `message` is user-presentable.
    case failed(message: String)

    var isSuccess: Bool {
        if case .completed = self { return true }
        return false
    }
}

/// Why sync is paused until the user enters their 24-word recovery phrase.
/// The relay no longer holds this account's seed, so the shell must never
/// generate or upload a replacement bootstrap seed.
enum SyncRecoveryRequirement: Equatable {
    /// `GET /sync/bootstrap` returned `key_mode: "client_held"` and this
    /// device has not joined the account's room yet.
    case clientHeldBootstrap
    /// A push or pull answered `410 room_retired`: the account switched to
    /// client-held keys after this device joined.
    case roomRetired
    /// A recovery phrase was entered but it derives a different room than the
    /// account's client-held room.
    case recoveryPhraseMismatch

    var message: String {
        switch self {
        case .clientHeldBootstrap:
            return "This account uses your own keys \u{2014} enter your 24-word recovery phrase to join sync."
        case .roomRetired:
            return "This account now uses your own keys - enter your recovery phrase to continue."
        case .recoveryPhraseMismatch:
            return "That recovery phrase does not match this account. Enter this account's 24-word recovery phrase to join sync."
        }
    }
}

/// Errors raised by the transport itself (as opposed to relay/core errors).
enum SyncTransportError: LocalizedError, Equatable {
    case notAuthenticated
    case sessionExpired
    case cancelled
    case protocolViolation(String)
    case recoveryPhraseRequired(SyncRecoveryRequirement)

    var errorDescription: String? {
        switch self {
        case .notAuthenticated:
            return "Sign in to sync."
        case .sessionExpired:
            return "Your relay session expired. Please sign in again."
        case .cancelled:
            return "Sync was cancelled."
        case .protocolViolation(let detail):
            return "Relay sync protocol error: \(detail)"
        case .recoveryPhraseRequired(let requirement):
            return requirement.message
        }
    }
}

/// Bounded, non-overlapping V2 REST sync cycle for iOS.
///
/// One cycle is:
/// 1. resolve the recovery-derived `room_id` from core,
/// 2. load (and refresh if needed) the relay session,
/// 3. lease a durable outbox batch from core and `POST /sync/push`,
/// 4. acknowledge each returned delivery ID back into core,
/// 5. paginate `GET /sync/pull?room_id=…&after_seq=…`, applying every V2
///    envelope through core, which advances the receive cursor atomically.
///
/// The transport never advances a cursor locally: core owns the cursor and only
/// moves it inside the same transaction that applied the entities. A failed
/// application therefore leaves the cursor behind and the envelope is safely
/// replayed on the next cycle. Duplicate deliveries are treated as success.
@MainActor
final class SyncTransport {
    static let shared = SyncTransport()

    /// Maximum envelopes accepted per `/sync/pull` page.
    static let pullPageLimit = 100
    /// Maximum pull pages a single bounded cycle will consume. Prevents an
    /// unbounded backlog from blowing the background execution budget; the
    /// remainder is picked up by the next cycle because the cursor advanced.
    static let maxPullPagesPerCycle = 20
    /// Core leases at most 64 envelopes per push (relay limit); a cycle drains
    /// the outbox in up to this many batches so offline bursts are not left
    /// pending behind a "Relay Up to Date" status.
    static let maxPushBatchesPerCycle = 20
    static let corePushBatchLimit = 64

    private let authStore: RelayAuthStoreProtocol
    private let apiClient: RelayAPIClientProtocol
    private let core: SyncCoreClient
    private let seedStore: SyncSeedStoreProtocol
    private let log = Logger(subsystem: "dev.maho.browser", category: "SyncTransport")

    private var isStarted = false
    private var shouldEnableCoreSync = false
    private var runningCycle: Task<SyncCycleOutcome, Never>?
    private var hasHydratedSnapshot = false
    /// Bumped on every sign-out so a bootstrap fetch that was in flight when the
    /// session was cleared cannot re-persist its seed afterwards.
    private var sessionGeneration = 0

    private(set) var lastCycleOutcome: SyncCycleOutcome?
    /// Non-nil while sync is paused waiting for the recovery phrase. Cycles
    /// fail fast with the requirement message until `recoveryPhraseJoined()`.
    private(set) var recoveryRequirement: SyncRecoveryRequirement?
    /// Set by `recoveryPhraseJoined()` so a room mismatch on the next bootstrap
    /// check is reported as a wrong phrase rather than a missing one.
    private var awaitingRecoveryJoinVerification = false

    init(
        authStore: RelayAuthStoreProtocol = RelayAuthStore.shared,
        apiClient: RelayAPIClientProtocol = RelayAPIClient(),
        core: SyncCoreClient = MahoBridge.shared,
        seedStore: SyncSeedStoreProtocol = SyncSeedStore.shared
    ) {
        self.authStore = authStore
        self.apiClient = apiClient
        self.core = core
        self.seedStore = seedStore
    }

    // MARK: - Lifecycle

    /// Marks the transport eligible to run cycles. Does not start a timer:
    /// scheduling is owned by `SyncManager` (foreground activation, "Sync now",
    /// and `BGAppRefreshTask`).
    func connect() {
        isStarted = true
        shouldEnableCoreSync = true
    }

    /// Stops accepting new cycles and cancels any in-flight cycle. Unleased and
    /// unacknowledged outbox rows stay durable in core and are retried later.
    func disconnect() {
        isStarted = false
        shouldEnableCoreSync = false
        cancelCurrentCycle()
        postStatusChanged()
    }

    func cancelCurrentCycle() {
        runningCycle?.cancel()
    }

    var isRunning: Bool {
        runningCycle != nil
    }

    // MARK: - Session

    func currentRelaySession() -> RelayAuthSession? {
        authStore.loadSession()
    }

    func clearRelaySession() {
        hasHydratedSnapshot = false
        recoveryRequirement = nil
        awaitingRecoveryJoinVerification = false
        sessionGeneration += 1
        // The seed is removed even when clearing the session fails.
        do {
            try seedStore.clearSeed()
        } catch {
            log.error("Failed clearing stored Sync seed: \(error.localizedDescription)")
        }
        do {
            try authStore.clearSession()
            core.signOut()
            postStatusChanged()
        } catch {
            log.error("Failed clearing relay session: \(error.localizedDescription)")
        }
    }

    internal func replaceRelaySession(_ session: RelayAuthSession) throws {
        try authStore.saveSession(session)
        core.signIn(
            email: session.account.email,
            displayName: session.account.displayName,
            accessToken: session.accessToken,
            userId: session.account.id,
            deviceId: session.device?.id
        )
        try core.setSyncEnabled(shouldEnableCoreSync)
        postStatusChanged()
    }

    internal func activateAuthenticatedSession(_ session: RelayAuthSession) async throws {
        core.signIn(
            email: session.account.email,
            displayName: session.account.displayName,
            accessToken: session.accessToken,
            userId: session.account.id,
            deviceId: session.device?.id
        )

        do {
            // Interactive sign-in: always fetch the bootstrap from the relay.
            do {
                try await configureCoreBootstrap(session: session, useStoredSeed: false)
                try core.setSyncEnabled(true)
            } catch SyncTransportError.recoveryPhraseRequired(let requirement) {
                // Client-held account: stay signed in, but sync waits for the
                // recovery phrase instead of creating a new bootstrap seed.
                requireRecoveryPhrase(requirement)
            }
            try authStore.saveSession(session)
            shouldEnableCoreSync = true
            postStatusChanged()
        } catch {
            try? seedStore.clearSeed()
            core.signOut()
            throw error
        }
    }

    @discardableResult
    internal func replaceRefreshedRelaySession(
        _ refreshed: RelayAuthSession,
        previous: RelayAuthSession
    ) throws -> RelayAuthSession {
        let merged = try previous.mergingRefreshResponse(refreshed)
        try replaceRelaySession(merged)
        return merged
    }

    // MARK: - Cycle entry points

    /// Runs one bounded cycle, or joins the cycle already in flight. Cycles
    /// never overlap, so foreground activation, "Sync now", and a background
    /// refresh task can all call this safely.
    @discardableResult
    func runCycle() async -> SyncCycleOutcome {
        if let runningCycle {
            return await runningCycle.value
        }

        guard isStarted else {
            return finish(.skipped(reason: "Sync is not enabled."))
        }

        if let recoveryRequirement {
            return finish(.failed(message: recoveryRequirement.message))
        }

        let task = Task { @MainActor [weak self] in
            guard let self else { return SyncCycleOutcome.skipped(reason: "Transport released.") }
            return await self.performCycle()
        }
        runningCycle = task
        let outcome = await task.value
        runningCycle = nil
        return outcome
    }

    // MARK: - Recovery phrase gate

    /// Pauses sync until the user enters the account recovery phrase. Drops the
    /// device-local escrow seed: it either never existed (client-held) or now
    /// points at a retired room.
    func requireRecoveryPhrase(_ requirement: SyncRecoveryRequirement) {
        recoveryRequirement = requirement
        awaitingRecoveryJoinVerification = false
        hasHydratedSnapshot = false
        do {
            try seedStore.clearSeed()
        } catch {
            log.error("Failed clearing stored Sync seed: \(error.localizedDescription)")
        }
        postStatusChanged()
    }

    /// Call after core joined sync with the user's recovery phrase. Lifts the
    /// pause; the next cycle verifies the joined room against the relay's
    /// client-held room before any push or pull.
    func recoveryPhraseJoined() {
        recoveryRequirement = nil
        awaitingRecoveryJoinVerification = true
        hasHydratedSnapshot = false
        postStatusChanged()
    }

    /// Fire-and-forget cycle for UI call sites that must not await.
    func triggerSync() {
        guard isStarted, runningCycle == nil else { return }
        Task { @MainActor [weak self] in
            await self?.runCycle()
        }
    }

    // MARK: - Cycle implementation

    private func performCycle() async -> SyncCycleOutcome {
        core.reportTransportState(kind: "syncing", lastError: nil)
        postStatusChanged()

        guard var session = authStore.loadSession() else {
            return finish(.skipped(reason: SyncTransportError.notAuthenticated.localizedDescription))
        }

        if session.isRefreshTokenExpired {
            clearRelaySession()
            return finish(.failed(message: SyncTransportError.sessionExpired.localizedDescription))
        }

        do {
            if session.isAccessTokenExpired {
                session = try await refreshSession(session)
            }
            // A stored access token can be revoked or expired server-side even when
            // its local expiry has not passed; refresh once on 401 so sync resumes
            // after a relaunch instead of failing every cycle.
            session = try await withOneRefreshRetry(session: session) { activeSession in
                try await self.prepareCore(session: activeSession)
                return activeSession
            }
        } catch SyncTransportError.recoveryPhraseRequired(let requirement) {
            requireRecoveryPhrase(requirement)
            return finish(.failed(message: requirement.message))
        } catch {
            return finish(.failed(message: message(for: error)))
        }

        let roomID: String
        do {
            roomID = try core.syncRoomID()
        } catch {
            return finish(.failed(message: message(for: error)))
        }

        guard authStore.loadSession() != nil else {
            return finish(.skipped(reason: SyncTransportError.notAuthenticated.localizedDescription))
        }

        do {
            try await self.hydrateSnapshotIfNeeded(roomID: roomID, session: session)

            var pushed = 0
            for _ in 0 ..< Self.maxPushBatchesPerCycle {
                try Task.checkCancellation()
                let batch = try await pushOutbox(roomID: roomID, session: currentSession(fallback: session))
                pushed += batch
                if batch < Self.corePushBatchLimit { break }
            }

            let applied = try await withOneRefreshRetry(session: currentSession(fallback: session)) { activeSession in
                try await self.pullAndApply(roomID: roomID, session: activeSession)
            }

            return finish(.completed(pushed: pushed, applied: applied))
        } catch is CancellationError {
            return finish(.skipped(reason: SyncTransportError.cancelled.localizedDescription))
        } catch RelayAPIError.roomRetired {
            requireRecoveryPhrase(.roomRetired)
            return finish(.failed(message: SyncRecoveryRequirement.roomRetired.message))
        } catch {
            return finish(.failed(message: message(for: error)))
        }
    }

    private func hydrateSnapshotIfNeeded(roomID: String, session: RelayAuthSession) async throws {
        guard !hasHydratedSnapshot else { return }
        do {
            let response = try await apiClient.getLatestSnapshot(roomID: roomID, session: session)
            let jsonData = try RelayCoding.encoder.encode(response)
            if let jsonString = String(data: jsonData, encoding: .utf8) {
                let count = try core.applySyncSnapshot(jsonString)
                if count > 0 {
                    log.info("Hydrated \(count) entities from latest snapshot")
                    await MainActor.run {
                        self.postStatusChanged()
                        NotificationCenter.default.post(
                            name: NSNotification.Name("MahoActiveProfileChanged"),
                            object: nil
                        )
                    }
                }
            }
            hasHydratedSnapshot = true
        } catch RelayAPIError.roomRetired {
            throw RelayAPIError.roomRetired
        } catch RelayAPIError.http(let statusCode, _) where statusCode == 404 {
            // No snapshot on relay; proceed with incremental sync
            hasHydratedSnapshot = true
        } catch {
            log.warning("Snapshot hydration skipped: \(error.localizedDescription)")
        }
    }

    private func prepareCore(session: RelayAuthSession) async throws {
        core.signIn(
            email: session.account.email,
            displayName: session.account.displayName,
            accessToken: session.accessToken,
            userId: session.account.id,
            deviceId: session.device?.id
        )

        try await configureCoreBootstrap(session: session, useStoredSeed: true)
        try core.setSyncEnabled(shouldEnableCoreSync)
    }

    /// Configures core with the account Sync seed. Sync restarts reuse the
    /// device-local seed; the relay bootstrap is fetched only after an
    /// interactive sign-in or when no usable seed is stored.
    private func configureCoreBootstrap(session: RelayAuthSession, useStoredSeed: Bool) async throws {
        let serverURL = RelayAPIClient.currentBaseURLString()
        let generation = sessionGeneration
        if useStoredSeed, let seed = seedStore.loadSeed(accountID: session.account.id) {
            do {
                try core.configureSyncBootstrap(serverURL: serverURL, seed: seed)
                return
            } catch {
                log.warning("Stored Sync seed rejected by core; fetching bootstrap: \(error.localizedDescription, privacy: .public)")
                try? seedStore.clearSeed()
            }
        }

        let bootstrap: RelaySyncBootstrap
        do {
            bootstrap = try await apiClient.getSyncBootstrap(session: session)
        } catch RelayAPIError.http(let statusCode, _) where statusCode == 404 {
            let generated = try core.generateSyncBootstrap()
            do {
                bootstrap = try await apiClient.putSyncBootstrap(generated, session: session)
            } catch RelayAPIError.http(let statusCode, _) where statusCode == 409 {
                bootstrap = try await apiClient.getSyncBootstrap(session: session)
            }
        }

        // Signed out while the fetch was in flight: discard the fetched seed.
        guard generation == sessionGeneration else {
            throw SyncTransportError.notAuthenticated
        }

        if bootstrap.isClientHeld {
            // The relay holds no seed. Only a recovery-phrase join that derived
            // this exact room may sync; never generate or upload a new seed.
            try? seedStore.clearSeed()
            if let joinedRoom = try? core.syncRoomID(), joinedRoom == bootstrap.roomId {
                awaitingRecoveryJoinVerification = false
                return
            }
            let requirement: SyncRecoveryRequirement = awaitingRecoveryJoinVerification
                ? .recoveryPhraseMismatch
                : .clientHeldBootstrap
            awaitingRecoveryJoinVerification = false
            throw SyncTransportError.recoveryPhraseRequired(requirement)
        }

        guard let seed = bootstrap.seed, !seed.isEmpty else {
            throw SyncTransportError.protocolViolation("escrow bootstrap is missing its seed")
        }
        awaitingRecoveryJoinVerification = false
        try core.configureSyncBootstrap(serverURL: serverURL, seed: seed)
        try seedStore.saveSeed(seed, accountID: session.account.id)
    }

    /// Runs `operation`; on relay 401 refreshes the session exactly once,
    /// persists it, and retries the same operation. A second failure propagates.
    private func withOneRefreshRetry<T>(
        session: RelayAuthSession,
        operation: (RelayAuthSession) async throws -> T
    ) async throws -> T {
        do {
            return try await operation(session)
        } catch RelayAPIError.unauthorized {
            let refreshed = try await refreshSession(session)
            return try await operation(refreshed)
        }
    }

    private func refreshSession(_ session: RelayAuthSession) async throws -> RelayAuthSession {
        let refreshed = try await apiClient.refresh(using: session.refreshToken)
        return try replaceRefreshedRelaySession(refreshed, previous: session)
    }

    private func currentSession(fallback: RelayAuthSession) -> RelayAuthSession {
        authStore.loadSession() ?? fallback
    }

    /// Leases the durable outbox once and pushes it. Rows only leave the outbox
    /// when their exact delivery ID comes back in the relay ACK list. A 401
    /// retry re-pushes the same leased envelopes; any failure before every ACK
    /// is persisted returns the lease to core so the next cycle re-sends it.
    private func pushOutbox(roomID: String, session: RelayAuthSession) async throws -> Int {
        let envelopes = try core.leaseOutgoingEnvelopes()
        guard !envelopes.isEmpty else { return 0 }

        do {
            try Task.checkCancellation()
            let response = try await withOneRefreshRetry(session: session) { activeSession in
                try await self.apiClient.push(roomID: roomID, envelopes: envelopes, session: activeSession)
            }

            let leased = Set(envelopes.map(\.deliveryId))
            let acknowledged = Set(response.acks.map(\.deliveryId))
            guard acknowledged == leased, acknowledged.count == response.acks.count else {
                throw SyncTransportError.protocolViolation(
                    "relay ACK set did not exactly match the leased delivery IDs"
                )
            }
            for ack in response.acks {
                try core.acknowledge(ack)
            }
            return response.acks.count
        } catch {
            do {
                try core.requeueLeasedEnvelopes()
            } catch let requeueError {
                log.error("Failed requeueing sync lease: \(requeueError.localizedDescription, privacy: .public)")
            }
            throw error
        }
    }

    /// Pulls pages after the core-owned receive cursor and applies each envelope.
    /// The cursor is re-read from core between pages so it always reflects what
    /// core actually committed.
    private func pullAndApply(roomID: String, session: RelayAuthSession) async throws -> Int {
        var applied = 0

        for _ in 0 ..< Self.maxPullPagesPerCycle {
            try Task.checkCancellation()
            let cursor = try core.receiveCursor(roomID: roomID)
            let response = try await apiClient.pull(
                roomID: roomID,
                afterSeq: cursor,
                limit: Self.pullPageLimit,
                session: session
            )

            if response.messages.isEmpty { break }

            for envelope in response.messages {
                try Task.checkCancellation()
                try core.apply(envelope, roomID: roomID)
                applied += 1
            }

            let advanced = try core.receiveCursor(roomID: roomID)
            if advanced <= cursor {
                // Core did not commit anything for this page. Stopping avoids an
                // infinite re-request of the same page; the next cycle retries.
                log.error("Receive cursor did not advance past \(cursor, privacy: .public); ending pull")
                break
            }

            if !response.hasMore { break }
        }

        return applied
    }

    // MARK: - Status plumbing

    @discardableResult
    private func finish(_ outcome: SyncCycleOutcome) -> SyncCycleOutcome {
        lastCycleOutcome = outcome
        switch outcome {
        case .completed:
            core.reportTransportState(kind: "synced", lastError: nil)
            postStatusChanged()
        case .skipped:
            core.reportTransportState(kind: "idle", lastError: nil)
            postStatusChanged()
        case .failed(let message):
            // Every failed cycle is logged here, including failures before the
            // core sync manager exists (auth/bootstrap), which core cannot record.
            log.error("Sync cycle failed: \(message, privacy: .public)")
            core.reportTransportState(kind: "error", lastError: message)
            NotificationCenter.default.post(
                name: .syncStatusChanged,
                object: nil,
                userInfo: ["error": message]
            )
        }
        return outcome
    }

    private func postStatusChanged() {
        NotificationCenter.default.post(name: .syncStatusChanged, object: nil)
    }

    private func message(for error: Error) -> String {
        if let localized = error as? LocalizedError,
           let description = localized.errorDescription,
           !description.isEmpty {
            return description
        }
        return error.localizedDescription
    }
}
