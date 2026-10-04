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

private enum RelayAuthStoreTestError: Error {
    case persistenceFailed
}

private final class FailingRelayAuthStore: RelayAuthStoreProtocol {
    func loadSession() -> RelayAuthSession? { nil }

    func saveSession(_ session: RelayAuthSession) throws {
        throw RelayAuthStoreTestError.persistenceFailed
    }

    func clearSession() throws {}
}

private final class FakeSyncCore: SyncCoreClient {
    var roomID = "0123456789abcdef0123456789abcdef"
    var outgoing = [SyncEnvelopeV2]()
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
        return outgoing
    }

    func acknowledge(_ ack: RelayAckV2) throws {
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
        return bootstrap
    }

    func putSyncBootstrap(
        _ bootstrap: RelaySyncBootstrap,
        session: RelayAuthSession
    ) async throws -> RelaySyncBootstrap {
        bootstrap
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
    private var transport: SyncTransport!

    override func setUp() {
        super.setUp()
        authStore = InMemoryRelayAuthStore()
        api = StubRelayAPIClient()
        core = FakeSyncCore()
        authStore.session = makeSession(accessToken: "access")
        transport = SyncTransport(authStore: authStore, apiClient: api, core: core)
        transport.connect()
    }

    override func tearDown() {
        transport.disconnect()
        super.tearDown()
    }

    func testSharedV2FixtureUsesIOSRelaySchema() throws {
        #if targetEnvironment(simulator) || os(macOS)
        let fixture = URL(fileURLWithPath: #filePath)
            .deletingLastPathComponent()
            .deletingLastPathComponent()
            .deletingLastPathComponent()
            .appendingPathComponent("tests/fixtures/sync-v2/protocol.json")
        let data = try Data(contentsOf: fixture)
        let root = try JSONSerialization.jsonObject(with: data) as! [String: Any]
        let pushData = try JSONSerialization.data(withJSONObject: root["push"]!)
        let ackData = try JSONSerialization.data(withJSONObject: root["ack"]!)
        let push = try RelayCoding.decoder.decode(RelayPushRequest.self, from: pushData)
        let ack = try RelayCoding.decoder.decode(RelayAckV2.self, from: ackData)

        XCTAssertEqual(push.roomId, "0123456789abcdef0123456789abcdef")
        XCTAssertEqual(push.messages.count, 1)
        XCTAssertEqual(push.messages.first?.deliveryId, ack.deliveryId)
        #endif
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
        let failingTransport = SyncTransport(authStore: failingStore, apiClient: api, core: core)

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

    private func envelope(id: String, payload: String = "AA==", seq: Int? = nil) -> SyncEnvelopeV2 {
        SyncEnvelopeV2(
            protocolVersion: 2,
            deliveryId: id,
            payload: payload,
            relaySeq: seq
        )
    }
}
