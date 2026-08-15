import XCTest
@testable import Maho

private final class InMemoryRelayAuthStore: RelayAuthStoreProtocol {
    var session: RelayAuthSession?

    func loadSession() -> RelayAuthSession? { session }
    func saveSession(_ session: RelayAuthSession) throws { self.session = session }
    func clearSession() throws { session = nil }
}

private final class FakeSyncCore: SyncCoreClient {
    var roomID = "0123456789abcdef0123456789abcdef"
    var outgoing = [SyncEnvelopeV2]()
    var cursor = 0
    var acknowledged = [RelayAckV2]()
    var applied = [SyncEnvelopeV2]()
    var applyError: Error?

    func signIn(
        email: String,
        displayName: String?,
        accessToken: String?,
        userId: String?,
        deviceId: String?
    ) {}

    func signOut() {}

    func syncState() throws -> SyncStateResponse {
        SyncStateResponse(
            kind: .idle,
            lastSuccessAt: nil,
            pendingOutboxCount: outgoing.count,
            lastError: nil
        )
    }

    func syncRoomID() throws -> String { roomID }
    func leaseOutgoingEnvelopes() throws -> [SyncEnvelopeV2] { outgoing }

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
}

private final class StubRelayAPIClient: RelayAPIClientProtocol {
    var refreshSession: RelayAuthSession?
    var pushResponses = [Result<RelayPushResponse, Error>]()
    var pullResponses = [Result<RelayPullResponse, Error>]()
    var pushRequests = [(roomID: String, envelopes: [SyncEnvelopeV2])]()
    var pullAfterSequences = [Int]()
    var refreshCount = 0

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
        throw RelayAPIError.http(statusCode: 404, message: "not found")
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
