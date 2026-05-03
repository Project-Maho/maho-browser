import XCTest
@testable import Maho

private final class InMemoryRelayAuthStore: RelayAuthStoreProtocol {
    var session: RelayAuthSession?

    func loadSession() -> RelayAuthSession? {
        session
    }

    func saveSession(_ session: RelayAuthSession) throws {
        self.session = session
    }

    func clearSession() throws {
        session = nil
    }
}

private final class StubRelayAPIClient: RelayAPIClientProtocol {
    var authenticatedSession: RelayAuthSession?
    var refreshedSession: RelayAuthSession?
    var pushedBatches: [[String]] = []
    var pullResponses: [RelayPullResponse] = []
    var pulledSinceSeqs: [Int] = []
    var pulledLimits: [Int] = []
    var logoutCalled = false
    var unauthorizedOnPush = false

    func authenticate(mode: RelayAuthMode, email: String, password: String, displayName: String?) async throws -> RelayAuthSession {
        authenticatedSession ?? makeSession(email: email, displayName: displayName)
    }

    func refresh(using refreshToken: String) async throws -> RelayAuthSession {
        refreshedSession ?? makeSession(email: "refresh@example.com", displayName: "Refreshed")
    }

    func logout(session: RelayAuthSession) async throws {
        logoutCalled = true
    }

    func push(blobs: [String], session: RelayAuthSession) async throws -> RelayPushResponse {
        if unauthorizedOnPush {
            unauthorizedOnPush = false
            throw RelayAPIError.unauthorized
        }

        pushedBatches.append(blobs)
        return RelayPushResponse(seqs: [5])
    }

    func pull(sinceSeq: Int, limit: Int, session: RelayAuthSession) async throws -> RelayPullResponse {
        pulledSinceSeqs.append(sinceSeq)
        pulledLimits.append(limit)

        if pullResponses.isEmpty {
            return RelayPullResponse(messages: [], hasMore: false)
        }

        return pullResponses.removeFirst()
    }

    private func makeSession(email: String, displayName: String?) -> RelayAuthSession {
        RelayAuthSession(
            accessToken: "access-token",
            refreshToken: "refresh-token",
            tokenType: "Bearer",
            expiresAt: Date().addingTimeInterval(3600),
            refreshExpiresAt: Date().addingTimeInterval(86400),
            account: RelayAccount(email: email, displayName: displayName),
            device: RelayDevice(id: "device", name: "Test Device", deviceType: "ios"),
            session: RelaySessionInfo(id: "session")
        )
    }
}

@MainActor
final class SyncTransportTests: XCTestCase {
    private var authStore: InMemoryRelayAuthStore!
    private var apiClient: StubRelayAPIClient!
    private var appliedEntities: [String]!
    private var drainedOutgoingPayload: String?
    private var transport: SyncTransport!

    override func setUp() {
        super.setUp()
        authStore = InMemoryRelayAuthStore()
        apiClient = StubRelayAPIClient()
        appliedEntities = []
        drainedOutgoingPayload = nil
        transport = SyncTransport(
            authStore: authStore,
            apiClient: apiClient,
            bridge: .shared,
            drainOutgoing: { [unowned self] in self.drainedOutgoingPayload },
            applyRemoteEntities: { [unowned self] json in
                self.appliedEntities.append(json)
                return json
            }
        )
        UserDefaults.standard.removeObject(forKey: SyncTransport.seqKey)
    }

    override func tearDown() {
        transport.disconnect()
        UserDefaults.standard.removeObject(forKey: SyncTransport.seqKey)
        super.tearDown()
    }

    func testUpdateLastServerSeqPersistsZeroAndHighestAck() {
        transport.updateLastServerSeq(0)
        transport.updateLastServerSeq(3)
        transport.updateLastServerSeq(2)
        transport.updateLastServerSeq(5)

        XCTAssertEqual(transport.lastServerSeq, 5)
        XCTAssertEqual(UserDefaults.standard.object(forKey: SyncTransport.seqKey) as? Int, 5)
    }

    func testReplaceRelaySessionPersistsSession() throws {
        let session = RelayAuthSession(
            accessToken: "access",
            refreshToken: "refresh",
            tokenType: "Bearer",
            expiresAt: Date().addingTimeInterval(600),
            refreshExpiresAt: Date().addingTimeInterval(3600),
            account: RelayAccount(email: "user@example.com", displayName: "User"),
            device: RelayDevice(id: "device", name: "Phone", deviceType: "ios"),
            session: RelaySessionInfo(id: "session")
        )

        try transport.replaceRelaySession(session)

        XCTAssertEqual(authStore.loadSession(), session)
        XCTAssertEqual(transport.currentRelaySession(), session)
    }

    func testClearRelaySessionRemovesPersistedSession() throws {
        let session = RelayAuthSession(
            accessToken: "access",
            refreshToken: "refresh",
            tokenType: "Bearer",
            expiresAt: Date().addingTimeInterval(600),
            refreshExpiresAt: Date().addingTimeInterval(3600),
            account: RelayAccount(email: "user@example.com", displayName: nil),
            device: nil,
            session: nil
        )
        try authStore.saveSession(session)

        transport.clearRelaySession()

        XCTAssertNil(authStore.loadSession())
    }

    func testTriggerSyncWithoutSessionDoesNotCrash() {
        transport.connect()
        transport.triggerSync()
        transport.disconnect()

        XCTAssertNil(authStore.loadSession())
    }

    func testConnectPushesOutgoingPayloadAsSingleBase64Blob() async throws {
        let session = makeSession(email: "user@example.com")
        try authStore.saveSession(session)
        drainedOutgoingPayload = "[{\"kind\":\"tab\"}]"

        transport.connect()
        defer { transport.disconnect() }

        try await waitFor { [weak self] in
            guard let self else { return false }
            return self.apiClient.pushedBatches.count == 1
        }

        XCTAssertEqual(apiClient.pushedBatches.count, 1)
        let firstBatch = try XCTUnwrap(apiClient.pushedBatches.first)
        XCTAssertEqual(firstBatch.count, 1)
        let firstBlob = try XCTUnwrap(firstBatch.first)
        let decoded = try XCTUnwrap(Data(base64Encoded: firstBlob))
        XCTAssertEqual(String(decoding: decoded, as: UTF8.self), drainedOutgoingPayload)
        XCTAssertEqual(transport.lastServerSeq, 5)
        XCTAssertEqual(UserDefaults.standard.object(forKey: SyncTransport.seqKey) as? Int, 5)
    }

    func testConnectPullsAllPagesAndAppliesDecodedEntities() async throws {
        let session = makeSession(email: "user@example.com")
        try authStore.saveSession(session)
        transport.updateLastServerSeq(0)
        apiClient.pullResponses = [
            RelayPullResponse(
                messages: [RelayPulledMessage(seq: 1, blob: Data("[{\"kind\":\"remote-1\"}]".utf8).base64EncodedString())],
                hasMore: true
            ),
            RelayPullResponse(
                messages: [RelayPulledMessage(seq: 2, blob: Data("[{\"kind\":\"remote-2\"}]".utf8).base64EncodedString())],
                hasMore: false
            )
        ]

        transport.connect()
        defer { transport.disconnect() }

        try await waitFor { [weak self] in
            guard let self else { return false }
            return self.appliedEntities.count == 2
        }

        XCTAssertEqual(appliedEntities, ["[{\"kind\":\"remote-1\"}]", "[{\"kind\":\"remote-2\"}]"])
        XCTAssertEqual(apiClient.pulledSinceSeqs.prefix(2), [0, 1])
        XCTAssertEqual(apiClient.pulledLimits.prefix(2), [100, 100])
        XCTAssertEqual(transport.lastServerSeq, 2)
        XCTAssertEqual(UserDefaults.standard.object(forKey: SyncTransport.seqKey) as? Int, 2)
    }

    func testUnauthorizedPushRefreshesSessionAndRetriesPollingCycle() async throws {
        let expiredAccessSession = RelayAuthSession(
            accessToken: "expired-access",
            refreshToken: "refresh-token",
            tokenType: "Bearer",
            expiresAt: Date().addingTimeInterval(3600),
            refreshExpiresAt: Date().addingTimeInterval(86400),
            account: RelayAccount(email: "user@example.com", displayName: nil),
            device: nil,
            session: nil
        )
        let refreshedSession = RelayAuthSession(
            accessToken: "fresh-access",
            refreshToken: "fresh-refresh",
            tokenType: "Bearer",
            expiresAt: Date().addingTimeInterval(3600),
            refreshExpiresAt: Date().addingTimeInterval(86400),
            account: RelayAccount(email: "user@example.com", displayName: "Fresh"),
            device: nil,
            session: nil
        )

        try authStore.saveSession(expiredAccessSession)
        apiClient.refreshedSession = refreshedSession
        apiClient.unauthorizedOnPush = true
        drainedOutgoingPayload = "[{\"kind\":\"outgoing\"}]"
        apiClient.pullResponses = [
            RelayPullResponse(
                messages: [RelayPulledMessage(seq: 7, blob: Data("[{\"kind\":\"remote\"}]".utf8).base64EncodedString())],
                hasMore: false
            )
        ]

        transport.connect()
        defer { transport.disconnect() }

        try await waitFor { [weak self] in
            guard let self else { return false }
            return self.apiClient.pushedBatches.count == 1 && self.appliedEntities.count == 1
        }

        XCTAssertEqual(authStore.loadSession(), refreshedSession)
        XCTAssertEqual(apiClient.pushedBatches.count, 1)
        XCTAssertEqual(appliedEntities, ["[{\"kind\":\"remote\"}]"])
        XCTAssertEqual(transport.lastServerSeq, 7)
    }

    private func makeSession(email: String) -> RelayAuthSession {
        RelayAuthSession(
            accessToken: "access",
            refreshToken: "refresh",
            tokenType: "Bearer",
            expiresAt: Date().addingTimeInterval(600),
            refreshExpiresAt: Date().addingTimeInterval(3600),
            account: RelayAccount(email: email, displayName: nil),
            device: RelayDevice(id: "device", name: "Phone", deviceType: "ios"),
            session: RelaySessionInfo(id: "session")
        )
    }

    private func waitFor(
        timeout: TimeInterval = 1.5,
        pollIntervalNanoseconds: UInt64 = 20_000_000,
        condition: @escaping () -> Bool
    ) async throws {
        let deadline = Date().addingTimeInterval(timeout)
        while Date() < deadline {
            if condition() {
                return
            }
            try await Task.sleep(nanoseconds: pollIntervalNanoseconds)
        }
        XCTFail("Condition was not met before timeout")
    }
}
