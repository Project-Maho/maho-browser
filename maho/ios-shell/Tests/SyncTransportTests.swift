import XCTest
#if canImport(Maho)
@testable import Maho
#endif

private final class InMemoryRelayAuthStore: RelayAuthStoreProtocol {
    var session: RelayAuthSession?

    func loadSession() -> RelayAuthSession? { session }
    func saveSession(_ session: RelayAuthSession) throws { self.session = session }
    func clearSession() throws { session = nil }
}

private final class InMemorySyncSeedStore: SyncSeedStoreProtocol {
    var seed: String?
    var accountID: String?
    var clearCount = 0

    func loadSeed(accountID: String?) -> String? {
        guard self.accountID == accountID else { return nil }
        return seed
    }

    func saveSeed(_ seed: String, accountID: String?) throws {
        self.seed = seed
        self.accountID = accountID
    }

    func clearSeed() throws {
        clearCount += 1
        seed = nil
        accountID = nil
    }
}

private enum RelayAuthStoreTestError: Error {
    case persistenceFailed
}

private final class ClearFailingRelayAuthStore: RelayAuthStoreProtocol {
    var session: RelayAuthSession?

    func loadSession() -> RelayAuthSession? { session }
    func saveSession(_ session: RelayAuthSession) throws { self.session = session }
    func clearSession() throws { throw RelayAuthStoreTestError.persistenceFailed }
}

private final class FailingRelayAuthStore: RelayAuthStoreProtocol {
    func loadSession() -> RelayAuthSession? { nil }

    func saveSession(_ session: RelayAuthSession) throws {
        throw RelayAuthStoreTestError.persistenceFailed
    }

    func clearSession() throws {}
}

/// Models core's outbox lease honestly: a lease removes envelopes from the
/// queue until they are acknowledged or requeued, and a new lease first
/// requeues any outstanding one (as core does).
private final class FakeSyncCore: SyncCoreClient {
    var roomID = "0123456789abcdef0123456789abcdef"
    var outgoing = [SyncEnvelopeV2]()
    var leased = [SyncEnvelopeV2]()
    var requeueCount = 0
    var reportedStates = [String]()
    var cursor = 0
    var acknowledged = [RelayAckV2]()
    var applied = [SyncEnvelopeV2]()
    var applyError: Error?
    var syncEnabled = false
    var configuredBootstrap: (serverURL: String, seed: String)?
    var calls = [String]()

    func signIn(
        email: String,
        displayName: String?,
        accessToken: String?,
        userId: String?,
        deviceId: String?
    ) {
        calls.append("signIn")
    }

    func signOut() {
        calls.append("signOut")
    }

    func setSyncEnabled(_ enabled: Bool) throws {
        calls.append("setSyncEnabled:\(enabled)")
        syncEnabled = enabled
    }

    func generateSyncBootstrap() throws -> RelaySyncBootstrap {
        throw SyncCoreError.operationFailed("unexpected bootstrap generation")
    }

    func configureSyncBootstrap(serverURL: String, seed: String) throws {
        calls.append("configureBootstrap")
        configuredBootstrap = (serverURL, seed)
    }

    func syncState() throws -> SyncStateResponse {
        SyncStateResponse(
            kind: .idle,
            lastSuccessAt: nil,
            pendingOutboxCount: outgoing.count,
            lastError: nil
        )
    }

    func syncRoomID() throws -> String {
        calls.append("roomID")
        return roomID
    }
    func leaseOutgoingEnvelopes() throws -> [SyncEnvelopeV2] {
        calls.append("lease")
        outgoing.insert(contentsOf: leased, at: 0)
        // Mirrors core: at most 64 envelopes per lease (relay push limit).
        leased = Array(outgoing.prefix(64))
        outgoing.removeFirst(leased.count)
        return leased
    }

    func reportTransportState(kind: String, lastError: String?) {
        reportedStates.append(kind)
    }

    func requeueLeasedEnvelopes() throws {
        calls.append("requeue")
        requeueCount += 1
        outgoing.insert(contentsOf: leased, at: 0)
        leased = []
    }

    func acknowledge(_ ack: RelayAckV2) throws {
        guard let index = leased.firstIndex(where: { $0.deliveryId == ack.deliveryId }) else {
            throw SyncCoreError.operationFailed("persist delivery ACK")
        }
        leased.remove(at: index)
        acknowledged.append(ack)
    }

    func receiveCursor(roomID: String) throws -> Int { cursor }

    func apply(_ envelope: SyncEnvelopeV2, roomID: String) throws {
        if let applyError { throw applyError }
        applied.append(envelope)
        if let seq = envelope.relaySeq {
            cursor = max(cursor, seq)
        }
    }

    var appliedSnapshots = [String]()
    func applySyncSnapshot(_ json: String) throws -> Int {
        appliedSnapshots.append(json)
        return 1
    }
}

private final class StubRelayAPIClient: RelayAPIClientProtocol {
    var refreshSession: RelayAuthSession?
    var pushResponses = [Result<RelayPushResponse, Error>]()
    var pullResponses = [Result<RelayPullResponse, Error>]()
    var pushRequests = [(roomID: String, envelopes: [SyncEnvelopeV2])]()
    var pullAfterSequences = [Int]()
    var refreshCount = 0
    var bootstrap = RelaySyncBootstrap(
        version: 1,
        roomId: "0123456789abcdef0123456789abcdef",
        seed: "restart-bootstrap-seed",
        createdAt: nil
    )
    var bootstrapRequestCount = 0
    var putBootstrapCount = 0
    var bootstrapUnauthorizedOnce = false
    var onBootstrapFetch: (() async -> Void)?

    func authenticate(
        mode: RelayAuthMode,
        email: String,
        password: String,
        displayName: String?
    ) async throws -> RelayAuthSession {
        makeSession(accessToken: "access")
    }

    func authenticateWithGoogle(idToken: String, nonce: String) async throws -> RelayAuthSession {
        makeSession(accessToken: "access")
    }

    func refresh(using refreshToken: String) async throws -> RelayAuthSession {
        refreshCount += 1
        return refreshSession ?? makeSession(accessToken: "refreshed")
    }

    func logout(session: RelayAuthSession) async throws {}

    func getSyncBootstrap(session: RelayAuthSession) async throws -> RelaySyncBootstrap {
        bootstrapRequestCount += 1
        if let onBootstrapFetch {
            self.onBootstrapFetch = nil
            await onBootstrapFetch()
        }
        if bootstrapUnauthorizedOnce {
            bootstrapUnauthorizedOnce = false
            throw RelayAPIError.unauthorized
        }
        return bootstrap
    }

    func putSyncBootstrap(
        _ bootstrap: RelaySyncBootstrap,
        session: RelayAuthSession
    ) async throws -> RelaySyncBootstrap {
        putBootstrapCount += 1
        return bootstrap
    }

    func push(
        roomID: String,
        envelopes: [SyncEnvelopeV2],
        session: RelayAuthSession
    ) async throws -> RelayPushResponse {
        pushRequests.append((roomID, envelopes))
        guard !pushResponses.isEmpty else { return RelayPushResponse(acks: []) }
        return try pushResponses.removeFirst().get()
    }

    func pull(
        roomID: String,
        afterSeq: Int,
        limit: Int,
        session: RelayAuthSession
    ) async throws -> RelayPullResponse {
        pullAfterSequences.append(afterSeq)
        guard !pullResponses.isEmpty else {
            return RelayPullResponse(messages: [], hasMore: false)
        }
        return try pullResponses.removeFirst().get()
    }

    var latestSnapshot: DownloadSnapshotResponse?
    func getLatestSnapshot(roomID: String, session: RelayAuthSession) async throws -> DownloadSnapshotResponse {
        if let latestSnapshot {
            return latestSnapshot
        }
        throw RelayAPIError.http(statusCode: 404, message: "not found")
    }

    private func makeSession(accessToken: String) -> RelayAuthSession {
        RelayAuthSession(
            accessToken: accessToken,
            refreshToken: "refresh",
            tokenType: "Bearer",
            expiresAt: Date().addingTimeInterval(3_600),
            refreshExpiresAt: Date().addingTimeInterval(86_400),
            account: RelayAccount(email: "sync@example.com", displayName: "Sync"),
            device: RelayDevice(id: "device-1", name: "Test Device", deviceType: "ios"),
            session: RelaySessionInfo(id: "session-1")
        )
    }
}

@MainActor
final class SyncTransportTests: XCTestCase {
    private var authStore: InMemoryRelayAuthStore!
    private var api: StubRelayAPIClient!
    private var core: FakeSyncCore!
    private var seedStore: InMemorySyncSeedStore!
    private var transport: SyncTransport!

    override func setUp() {
        super.setUp()
        authStore = InMemoryRelayAuthStore()
        api = StubRelayAPIClient()
        core = FakeSyncCore()
        seedStore = InMemorySyncSeedStore()
        authStore.session = makeSession(accessToken: "access")
        transport = SyncTransport(authStore: authStore, apiClient: api, core: core, seedStore: seedStore)
        transport.connect()
    }

    override func tearDown() {
        transport.disconnect()
        super.tearDown()
    }

    func testSharedV2FixtureUsesIOSRelaySchema() throws {
        // Wire sample of the relay's v2 push/ack. The shared fixture file stays in the
        // private relay repo, so the client keeps its own copy (same as Android).
        let data = Data("""
        {
          "push": {
            "room_id": "0123456789abcdef0123456789abcdef",
            "messages": [
              {
                "protocol_version": 2,
                "delivery_id": "550e8400-e29b-41d4-a716-446655440000",
                "payload": "AA==",
                "relay_seq": null
              }
            ]
          },
          "ack": {
            "delivery_id": "550e8400-e29b-41d4-a716-446655440000",
            "seq": 5
          }
        }
        """.utf8)
        let root = try JSONSerialization.jsonObject(with: data) as! [String: Any]
        let pushData = try JSONSerialization.data(withJSONObject: root["push"]!)
        let ackData = try JSONSerialization.data(withJSONObject: root["ack"]!)
        let push = try RelayCoding.decoder.decode(RelayPushRequest.self, from: pushData)
        let ack = try RelayCoding.decoder.decode(RelayAckV2.self, from: ackData)

        XCTAssertEqual(push.roomId, "0123456789abcdef0123456789abcdef")
        XCTAssertEqual(push.messages.count, 1)
        XCTAssertEqual(push.messages.first?.deliveryId, ack.deliveryId)
    }

    func testRestartRestoresCoreAccountBootstrapAndEnabledStateBeforeUsingRoom() async {
        core.syncEnabled = false

        let outcome = await transport.runCycle()

        XCTAssertEqual(outcome, .completed(pushed: 0, applied: 0))
        XCTAssertEqual(api.bootstrapRequestCount, 1)
        XCTAssertEqual(core.configuredBootstrap?.seed, api.bootstrap.seed)
        XCTAssertTrue(core.syncEnabled)
        XCTAssertEqual(
            Array(core.calls.prefix(5)),
            ["signIn", "configureBootstrap", "setSyncEnabled:true", "roomID", "lease"]
        )
    }

    func testSecondSyncStartAfterRefreshUsesStoredSeedWithoutBootstrapFetch() async {
        let first = await transport.runCycle()
        XCTAssertEqual(first, .completed(pushed: 0, applied: 0))
        XCTAssertEqual(api.bootstrapRequestCount, 1)
        XCTAssertEqual(seedStore.seed, api.bootstrap.seed)

        // Relaunch: a fresh transport with an expired access token refreshes first.
        api.bootstrapRequestCount = 0
        core.calls = []
        authStore.session = makeSession(accessToken: "stale", expiresAt: Date().addingTimeInterval(-60))
        let relaunched = SyncTransport(authStore: authStore, apiClient: api, core: core, seedStore: seedStore)
        relaunched.connect()

        let second = await relaunched.runCycle()

        XCTAssertEqual(second, .completed(pushed: 0, applied: 0))
        XCTAssertEqual(api.refreshCount, 1)
        XCTAssertEqual(api.bootstrapRequestCount, 0)
        XCTAssertEqual(core.configuredBootstrap?.seed, api.bootstrap.seed)
        XCTAssertEqual(core.calls.filter { $0 == "configureBootstrap" }.count, 1)
        relaunched.disconnect()
    }

    func testMissingStoredSeedFetchesBootstrapExactlyOnceAndStoresIt() async {
        XCTAssertNil(seedStore.seed)

        let outcome = await transport.runCycle()

        XCTAssertEqual(outcome, .completed(pushed: 0, applied: 0))
        XCTAssertEqual(api.bootstrapRequestCount, 1)
        XCTAssertEqual(seedStore.seed, api.bootstrap.seed)
        XCTAssertEqual(seedStore.accountID, makeSession(accessToken: "access").account.id)
    }

    func testStoredSeedForAnotherAccountIsIgnoredAndRefetched() async {
        try? seedStore.saveSeed("other-account-seed", accountID: "other-account")

        let outcome = await transport.runCycle()

        XCTAssertEqual(outcome, .completed(pushed: 0, applied: 0))
        XCTAssertEqual(api.bootstrapRequestCount, 1)
        XCTAssertEqual(core.configuredBootstrap?.seed, api.bootstrap.seed)
    }

    func testInteractiveSignInFetchesBootstrapEvenWithStoredSeed() async throws {
        try seedStore.saveSeed("old-seed", accountID: makeSession(accessToken: "x").account.id)
        authStore.session = nil

        try await transport.activateAuthenticatedSession(makeSession(accessToken: "fresh-access"))

        XCTAssertEqual(api.bootstrapRequestCount, 1)
        XCTAssertEqual(core.configuredBootstrap?.seed, api.bootstrap.seed)
        XCTAssertEqual(seedStore.seed, api.bootstrap.seed)
    }

    func testSignOutRemovesStoredSeed() async {
        _ = await transport.runCycle()
        XCTAssertNotNil(seedStore.seed)

        transport.clearRelaySession()

        XCTAssertNil(seedStore.seed)
        XCTAssertNil(authStore.session)
        XCTAssertTrue(core.calls.contains("signOut"))
    }

    func testSignOutDuringInFlightBootstrapFetchDiscardsFetchedSeed() async {
        api.onBootstrapFetch = { [transport] in
            await transport?.clearRelaySession()
        }

        let outcome = await transport.runCycle()

        XCTAssertEqual(api.bootstrapRequestCount, 1)
        XCTAssertNil(seedStore.seed, "a fetch that outlived sign-out must not re-persist the seed")
        XCTAssertNil(core.configuredBootstrap)
        XCTAssertNotEqual(outcome, .completed(pushed: 0, applied: 0))
    }

    func testSignOutClearsSeedEvenWhenSessionClearFails() throws {
        let failingStore = ClearFailingRelayAuthStore()
        failingStore.session = makeSession(accessToken: "access")
        try seedStore.saveSeed("seed", accountID: failingStore.session?.account.id)
        let failingTransport = SyncTransport(
            authStore: failingStore,
            apiClient: api,
            core: core,
            seedStore: seedStore
        )

        failingTransport.clearRelaySession()

        XCTAssertNil(seedStore.seed)
        XCTAssertNotNil(failingStore.session)
    }

    func testKeychainSeedStoreRoundTripsBindsAccountAndClears() throws {
        let store = SyncSeedStore(service: "dev.maho.browser.sync-seed.tests.\(UUID().uuidString)")
        defer { try? store.clearSeed() }

        XCTAssertNil(store.loadSeed(accountID: "account-1"))
        try store.saveSeed("seed-1", accountID: "account-1")
        XCTAssertEqual(store.loadSeed(accountID: "account-1"), "seed-1")
        XCTAssertNil(store.loadSeed(accountID: "account-2"))
        try store.saveSeed("seed-2", accountID: "account-1")
        XCTAssertEqual(store.loadSeed(accountID: "account-1"), "seed-2")
        try store.clearSeed()
        XCTAssertNil(store.loadSeed(accountID: "account-1"))
        try store.clearSeed()
    }

    func testRevokedAccessTokenRefreshesOnceAndResumesSync() async {
        api.bootstrapUnauthorizedOnce = true

        let outcome = await transport.runCycle()

        XCTAssertEqual(outcome, .completed(pushed: 0, applied: 0))
        XCTAssertEqual(api.refreshCount, 1)
        XCTAssertEqual(api.bootstrapRequestCount, 2)
        XCTAssertEqual(core.configuredBootstrap?.seed, api.bootstrap.seed)
    }

    func testCompletedCycleReportsSyncingThenSyncedToCore() async {
        let outcome = await transport.runCycle()

        XCTAssertEqual(outcome, .completed(pushed: 0, applied: 0))
        XCTAssertEqual(core.reportedStates, ["syncing", "synced"])
    }

    func testCycleDrainsLargeOfflineOutboxInRelaySizedBatches() async {
        let ids = (0 ..< 70).map { String(format: "550e8400-e29b-41d4-a716-%012d", $0) }
        core.outgoing = ids.map { envelope(id: $0, payload: "AA==", seq: nil) }
        api.pushResponses = [Array(ids.prefix(64)), Array(ids.dropFirst(64))].map { batch in
            .success(RelayPushResponse(acks: batch.enumerated().map { RelayAckV2(deliveryId: $0.element, seq: $0.offset + 1) }))
        }

        let outcome = await transport.runCycle()

        XCTAssertEqual(outcome, .completed(pushed: 70, applied: 0))
        XCTAssertEqual(api.pushRequests.map(\.1.count), [64, 6])
        XCTAssertTrue(core.outgoing.isEmpty)
    }

    func testPerformCycleHydratesLatestSnapshotWhenAvailable() async {
        api.latestSnapshot = DownloadSnapshotResponse(
            hlcTsCeiling: 100,
            createdAt: 1000,
            creatorDeviceId: 1,
            blob: "eyJ2ZXJzaW9uIjoxfQ=="
        )

        let outcome = await transport.runCycle()

        XCTAssertEqual(outcome, .completed(pushed: 0, applied: 0))
        XCTAssertEqual(core.appliedSnapshots.count, 1)
        XCTAssertTrue(core.appliedSnapshots[0].contains("eyJ2ZXJzaW9uIjoxfQ=="))

        // Subsequent cycle must NOT re-hydrate snapshot (one-time initial hydration)
        let secondOutcome = await transport.runCycle()
        XCTAssertEqual(secondOutcome, .completed(pushed: 0, applied: 0))
        XCTAssertEqual(core.appliedSnapshots.count, 1, "snapshot must only be hydrated once")
    }

    func testAuthenticatedActivationConfiguresCoreBeforePersistingSession() async throws {
        authStore.session = nil
        let session = makeSession(accessToken: "fresh-access")

        try await transport.activateAuthenticatedSession(session)

        XCTAssertEqual(authStore.loadSession(), session)
        XCTAssertTrue(core.syncEnabled)
        XCTAssertEqual(api.bootstrapRequestCount, 1)
        XCTAssertEqual(
            Array(core.calls.prefix(3)),
            ["signIn", "configureBootstrap", "setSyncEnabled:true"]
        )
    }

    func testAuthenticatedActivationRollsBackCoreWhenSessionPersistenceFails() async {
        let failingStore = FailingRelayAuthStore()
        let failingTransport = SyncTransport(
            authStore: failingStore,
            apiClient: api,
            core: core,
            seedStore: seedStore
        )

        do {
            try await failingTransport.activateAuthenticatedSession(makeSession(accessToken: "fresh-access"))
            XCTFail("Expected session persistence to fail")
        } catch RelayAuthStoreTestError.persistenceFailed {
        } catch {
            XCTFail("Unexpected error: \(error)")
        }

        XCTAssertNil(failingStore.loadSession())
        XCTAssertEqual(
            core.calls,
            ["signIn", "configureBootstrap", "setSyncEnabled:true", "signOut"]
        )
    }

    func testRepeatedEnabledPreparationDoesNotInvertCoreState() async {
        _ = await transport.runCycle()
        _ = await transport.runCycle()

        XCTAssertTrue(core.syncEnabled)
        XCTAssertEqual(core.calls.filter { $0 == "setSyncEnabled:true" }.count, 2)
    }

    func testV2CycleUsesSharedRoomExactAcksAndPaginatedCursor() async {
        let outgoing = envelope(id: "550e8400-e29b-41d4-a716-446655440000")
        let first = envelope(
            id: "550e8400-e29b-41d4-a716-446655440001",
            payload: "AQ==",
            seq: 1
        )
        let second = envelope(
            id: "550e8400-e29b-41d4-a716-446655440002",
            payload: "Ag==",
            seq: 2
        )
        core.outgoing = [outgoing]
        api.pushResponses = [.success(RelayPushResponse(acks: [RelayAckV2(deliveryId: outgoing.deliveryId, seq: 9)]))]
        api.pullResponses = [
            .success(RelayPullResponse(messages: [first], hasMore: true)),
            .success(RelayPullResponse(messages: [second], hasMore: false)),
        ]

        let outcome = await transport.runCycle()

        XCTAssertEqual(outcome, .completed(pushed: 1, applied: 2))
        XCTAssertEqual(api.pushRequests.map(\.roomID), [core.roomID])
        XCTAssertEqual(api.pullAfterSequences, [0, 1])
        XCTAssertEqual(core.acknowledged, [RelayAckV2(deliveryId: outgoing.deliveryId, seq: 9)])
        XCTAssertEqual(core.applied, [first, second])
        XCTAssertEqual(core.cursor, 2)
    }

    func testUnauthorizedPushRefreshesOnceThenRetries() async {
        let outgoing = envelope(id: "550e8400-e29b-41d4-a716-446655440000")
        core.outgoing = [outgoing]
        api.pushResponses = [
            .failure(RelayAPIError.unauthorized),
            .success(RelayPushResponse(acks: [RelayAckV2(deliveryId: outgoing.deliveryId, seq: 1)])),
        ]

        let outcome = await transport.runCycle()

        XCTAssertEqual(outcome, .completed(pushed: 1, applied: 0))
        XCTAssertEqual(api.refreshCount, 1)
        XCTAssertEqual(api.pushRequests.count, 2)
        XCTAssertEqual(authStore.session?.accessToken, "refreshed")
    }

    func testUnauthorizedPushRetriesTheSameLeaseOnceAfterRefresh() async {
        let first = envelope(id: "550e8400-e29b-41d4-a716-446655440010")
        let second = envelope(id: "550e8400-e29b-41d4-a716-446655440011")
        core.outgoing = [first, second]
        api.pushResponses = [
            .failure(RelayAPIError.unauthorized),
            .success(RelayPushResponse(acks: [
                RelayAckV2(deliveryId: first.deliveryId, seq: 1),
                RelayAckV2(deliveryId: second.deliveryId, seq: 2),
            ])),
        ]

        let outcome = await transport.runCycle()

        XCTAssertEqual(outcome, .completed(pushed: 2, applied: 0))
        XCTAssertEqual(api.refreshCount, 1)
        XCTAssertEqual(core.calls.filter { $0 == "lease" }.count, 1, "a 401 retry must not re-lease")
        XCTAssertEqual(core.requeueCount, 0)
        XCTAssertEqual(api.pushRequests.map { $0.envelopes }, [[first, second], [first, second]])
        XCTAssertEqual(core.acknowledged.map(\.deliveryId), [first.deliveryId, second.deliveryId])
        XCTAssertTrue(core.outgoing.isEmpty)
        XCTAssertTrue(core.leased.isEmpty)
    }

    func testFailedPushRequeuesLeaseAndNextCycleRepushesSameDeliveryIDs() async {
        let first = envelope(id: "550e8400-e29b-41d4-a716-446655440010")
        let second = envelope(id: "550e8400-e29b-41d4-a716-446655440011")
        core.outgoing = [first, second]
        api.pushResponses = [
            .failure(RelayAPIError.http(statusCode: 503, message: "unavailable")),
            .success(RelayPushResponse(acks: [
                RelayAckV2(deliveryId: first.deliveryId, seq: 1),
                RelayAckV2(deliveryId: second.deliveryId, seq: 2),
            ])),
        ]

        let failed = await transport.runCycle()

        guard case .failed = failed else {
            return XCTFail("Expected the 503 push to fail the cycle.")
        }
        XCTAssertEqual(core.requeueCount, 1)
        XCTAssertEqual(core.outgoing, [first, second])
        XCTAssertTrue(core.leased.isEmpty)

        let retried = await transport.runCycle()

        XCTAssertEqual(retried, .completed(pushed: 2, applied: 0))
        XCTAssertEqual(api.pushRequests.count, 2)
        XCTAssertEqual(
            api.pushRequests[1].envelopes.map(\.deliveryId),
            api.pushRequests[0].envelopes.map(\.deliveryId)
        )
        XCTAssertEqual(core.acknowledged.map(\.deliveryId), [first.deliveryId, second.deliveryId])
        XCTAssertTrue(core.outgoing.isEmpty)
        XCTAssertTrue(core.leased.isEmpty)
    }

    func testMismatchedAckFailsWithoutCompletingDurableOutbox() async {
        let outgoing = envelope(id: "550e8400-e29b-41d4-a716-446655440000")
        core.outgoing = [outgoing]
        api.pushResponses = [
            .success(RelayPushResponse(acks: [
                RelayAckV2(
                    deliveryId: "550e8400-e29b-41d4-a716-446655440099",
                    seq: 1
                ),
            ])),
        ]

        let outcome = await transport.runCycle()

        guard case .failed = outcome else {
            return XCTFail("Expected a relay ACK protocol failure.")
        }
        XCTAssertTrue(core.acknowledged.isEmpty)
        XCTAssertEqual(core.outgoing, [outgoing])
        XCTAssertTrue(core.leased.isEmpty)
    }

    func testFailedEnvelopeApplicationLeavesCursorUntouched() async {
        core.applyError = SyncCoreError.operationFailed("apply")
        api.pullResponses = [
            .success(RelayPullResponse(messages: [
                envelope(
                    id: "550e8400-e29b-41d4-a716-446655440001",
                    payload: "AQ==",
                    seq: 4
                ),
            ], hasMore: false)),
        ]

        let outcome = await transport.runCycle()

        guard case .failed = outcome else {
            return XCTFail("Expected envelope application to fail.")
        }
        XCTAssertEqual(core.cursor, 0)
        XCTAssertTrue(core.applied.isEmpty)
    }

    // MARK: - Client-held keys (key_mode client_held / 410 room_retired)

    private static let clientHeldRoomID = "fedcba9876543210fedcba9876543210"

    private func decodeClientHeldBootstrap() throws -> RelaySyncBootstrap {
        // Exact relay shape: GET /sync/bootstrap for a client-held account has no seed.
        try RelayCoding.decoder.decode(
            RelaySyncBootstrap.self,
            from: Data(#"{"key_mode":"client_held","room_id":"\#(Self.clientHeldRoomID)"}"#.utf8)
        )
    }

    func testClientHeldBootstrapDecodesWithoutSeed() throws {
        let bootstrap = try decodeClientHeldBootstrap()
        XCTAssertTrue(bootstrap.isClientHeld)
        XCTAssertNil(bootstrap.seed)
        XCTAssertNil(bootstrap.version)
        XCTAssertEqual(bootstrap.roomId, Self.clientHeldRoomID)

        let explicitNull = try RelayCoding.decoder.decode(
            RelaySyncBootstrap.self,
            from: Data(#"{"key_mode":"client_held","room_id":"abc","seed_b64":null,"seed":null}"#.utf8)
        )
        XCTAssertTrue(explicitNull.isClientHeld)
        XCTAssertNil(explicitNull.seed)
    }

    func testClientHeldBootstrapRequiresRecoveryPhraseWithoutGeneratingOrUploadingSeed() async throws {
        api.bootstrap = try decodeClientHeldBootstrap()

        let outcome = await transport.runCycle()

        XCTAssertEqual(outcome, .failed(message: SyncRecoveryRequirement.clientHeldBootstrap.message))
        XCTAssertEqual(transport.recoveryRequirement, .clientHeldBootstrap)
        XCTAssertEqual(api.putBootstrapCount, 0, "a client-held account must never get a new seed uploaded")
        XCTAssertNil(core.configuredBootstrap)
        XCTAssertNil(seedStore.seed)
        XCTAssertFalse(core.calls.contains("lease"))
        XCTAssertTrue(api.pushRequests.isEmpty)
        XCTAssertTrue(api.pullAfterSequences.isEmpty)
        XCTAssertTrue(SyncRecoveryRequirement.clientHeldBootstrap.message.contains("24-word recovery phrase"))

        // Paused: the next cycle fails fast without touching the relay.
        api.bootstrapRequestCount = 0
        let paused = await transport.runCycle()
        XCTAssertEqual(paused, .failed(message: SyncRecoveryRequirement.clientHeldBootstrap.message))
        XCTAssertEqual(api.bootstrapRequestCount, 0)
    }

    func testInteractiveSignInToClientHeldAccountKeepsSessionAndAsksForPhrase() async throws {
        authStore.session = nil
        api.bootstrap = try decodeClientHeldBootstrap()
        let session = makeSession(accessToken: "fresh-access")

        try await transport.activateAuthenticatedSession(session)

        XCTAssertEqual(authStore.loadSession(), session)
        XCTAssertEqual(transport.recoveryRequirement, .clientHeldBootstrap)
        XCTAssertEqual(api.putBootstrapCount, 0)
        XCTAssertNil(core.configuredBootstrap)
        XCTAssertFalse(core.calls.contains("signOut"))
    }

    func testClientHeldAccountSyncsAfterMatchingRecoveryPhraseJoin() async throws {
        api.bootstrap = try decodeClientHeldBootstrap()
        _ = await transport.runCycle()
        XCTAssertEqual(transport.recoveryRequirement, .clientHeldBootstrap)

        // Core joined with the phrase and now derives the account's room.
        core.roomID = Self.clientHeldRoomID
        transport.recoveryPhraseJoined()
        let outcome = await transport.runCycle()

        XCTAssertEqual(outcome, .completed(pushed: 0, applied: 0))
        XCTAssertNil(transport.recoveryRequirement)
        XCTAssertEqual(api.putBootstrapCount, 0)
        XCTAssertNil(seedStore.seed)
        XCTAssertEqual(api.pullAfterSequences, [0])
    }

    func testWrongRecoveryPhraseShowsMismatchAndDoesNotCreateRoom() async throws {
        api.bootstrap = try decodeClientHeldBootstrap()
        _ = await transport.runCycle()

        // The phrase derived some other room than the account's client-held room.
        core.roomID = "00000000000000000000000000000000"
        transport.recoveryPhraseJoined()
        let outcome = await transport.runCycle()

        XCTAssertEqual(outcome, .failed(message: SyncRecoveryRequirement.recoveryPhraseMismatch.message))
        XCTAssertEqual(transport.recoveryRequirement, .recoveryPhraseMismatch)
        XCTAssertEqual(api.putBootstrapCount, 0)
        XCTAssertTrue(api.pushRequests.isEmpty)
        XCTAssertTrue(api.pullAfterSequences.isEmpty)
    }

    func testRelayMaps410RoomRetiredBodyToRoomRetiredError() {
        let body = Data(#"{"error":"room_retired","code":"room_retired"}"#.utf8)
        XCTAssertTrue(RelayAPIClient.isRoomRetired(statusCode: 410, data: body))
        XCTAssertFalse(RelayAPIClient.isRoomRetired(statusCode: 409, data: body))
        XCTAssertFalse(RelayAPIClient.isRoomRetired(statusCode: 410, data: Data(#"{"code":"gone"}"#.utf8)))
    }

    func testRoomRetiredPushPausesSyncAndAsksForRecoveryPhrase() async {
        let outgoing = envelope(id: "550e8400-e29b-41d4-a716-446655440000")
        core.outgoing = [outgoing]
        try? seedStore.saveSeed("escrow-seed", accountID: makeSession(accessToken: "x").account.id)
        api.pushResponses = [.failure(RelayAPIError.roomRetired)]

        let outcome = await transport.runCycle()

        XCTAssertEqual(outcome, .failed(message: SyncRecoveryRequirement.roomRetired.message))
        XCTAssertEqual(transport.recoveryRequirement, .roomRetired)
        XCTAssertEqual(
            SyncRecoveryRequirement.roomRetired.message,
            "This account now uses your own keys - enter your recovery phrase to continue."
        )
        XCTAssertNil(seedStore.seed, "the retired room's escrow seed must not be reused")
        XCTAssertEqual(core.requeueCount, 1, "the leased batch stays durable for the new room")
        XCTAssertEqual(core.outgoing, [outgoing])
        XCTAssertEqual(api.putBootstrapCount, 0)

        let paused = await transport.runCycle()
        XCTAssertEqual(paused, .failed(message: SyncRecoveryRequirement.roomRetired.message))
        XCTAssertEqual(api.pushRequests.count, 1, "sync stays paused until the phrase is entered")
    }

    func testRoomRetiredPullPausesSyncAndAsksForRecoveryPhrase() async {
        api.pullResponses = [.failure(RelayAPIError.roomRetired)]

        let outcome = await transport.runCycle()

        XCTAssertEqual(outcome, .failed(message: SyncRecoveryRequirement.roomRetired.message))
        XCTAssertEqual(transport.recoveryRequirement, .roomRetired)
        XCTAssertEqual(core.cursor, 0)
        XCTAssertEqual(api.putBootstrapCount, 0)
    }

    func testSignOutClearsRecoveryRequirement() async {
        api.pullResponses = [.failure(RelayAPIError.roomRetired)]
        _ = await transport.runCycle()
        XCTAssertNotNil(transport.recoveryRequirement)

        transport.clearRelaySession()

        XCTAssertNil(transport.recoveryRequirement)
    }

    private func makeSession(
        accessToken: String,
        expiresAt: Date = Date().addingTimeInterval(3_600)
    ) -> RelayAuthSession {
        RelayAuthSession(
            accessToken: accessToken,
            refreshToken: "refresh",
            tokenType: "Bearer",
            expiresAt: expiresAt,
            refreshExpiresAt: Date().addingTimeInterval(86_400),
            account: RelayAccount(email: "sync@example.com", displayName: "Sync"),
            device: RelayDevice(id: "device-1", name: "Test Device", deviceType: "ios"),
            session: RelaySessionInfo(id: "session-1")
        )
    }

    private func envelope(id: String, payload: String = "AA==", seq: Int? = nil) -> SyncEnvelopeV2 {
        SyncEnvelopeV2(
            protocolVersion: 2,
            deliveryId: id,
            payload: payload,
            relaySeq: seq
        )
    }
}
