import XCTest
@testable import Maho

private enum GoogleFlowTestError: Error {
    case failed
}

@MainActor
private final class StubGoogleSignInCoordinator: GoogleSignInCoordinating {
    let result: Result<GoogleIdentityToken, Error>
    private(set) var signInCallCount = 0

    init(result: Result<GoogleIdentityToken, Error>) {
        self.result = result
    }

    func signIn() async throws -> GoogleIdentityToken {
        signInCallCount += 1
        return try result.get()
    }
}

private final class GoogleFlowAuthStore: RelayAuthStoreProtocol {
    var session: RelayAuthSession?
    private(set) var savedSessions: [RelayAuthSession] = []

    func loadSession() -> RelayAuthSession? {
        session
    }

    func saveSession(_ session: RelayAuthSession) throws {
        savedSessions.append(session)
        self.session = session
    }

    func clearSession() throws {
        session = nil
    }
}

private final class GoogleFlowAPIClient: RelayAPIClientProtocol {
    var googleResult: Result<RelayAuthSession, Error>
    private(set) var googleRequests: [(idToken: String, nonce: String)] = []

    init(googleResult: Result<RelayAuthSession, Error>) {
        self.googleResult = googleResult
    }

    func authenticate(mode: RelayAuthMode, email: String, password: String, displayName: String?) async throws -> RelayAuthSession {
        throw GoogleFlowTestError.failed
    }

    func authenticateWithGoogle(idToken: String, nonce: String) async throws -> RelayAuthSession {
        googleRequests.append((idToken, nonce))
        return try googleResult.get()
    }

    func refresh(using refreshToken: String) async throws -> RelayAuthSession {
        throw GoogleFlowTestError.failed
    }

    func logout(session: RelayAuthSession) async throws {}

    func getSyncBootstrap(session: RelayAuthSession) async throws -> RelaySyncBootstrap {
        throw GoogleFlowTestError.failed
    }

    func putSyncBootstrap(
        _ bootstrap: RelaySyncBootstrap,
        session: RelayAuthSession
    ) async throws -> RelaySyncBootstrap {
        throw GoogleFlowTestError.failed
    }

    func getLatestSnapshot(roomID: String, session: RelayAuthSession) async throws -> DownloadSnapshotResponse {
        throw GoogleFlowTestError.failed
    }

    func push(
        roomID: String,
        envelopes: [SyncEnvelopeV2],
        session: RelayAuthSession
    ) async throws -> RelayPushResponse {
        throw GoogleFlowTestError.failed
    }

    func pull(
        roomID: String,
        afterSeq: Int,
        limit: Int,
        session: RelayAuthSession
    ) async throws -> RelayPullResponse {
        throw GoogleFlowTestError.failed
    }
}

@MainActor
final class GoogleRelaySignInFlowTests: XCTestCase {
    func testSuccessExchangesGoogleTokenAndPersistsRelaySession() async throws {
        let relaySession = makeSession(email: "google@example.com")
        let coordinator = StubGoogleSignInCoordinator(
            result: .success(GoogleIdentityToken(idToken: "google-id-token", nonce: "nonce"))
        )
        let apiClient = GoogleFlowAPIClient(googleResult: .success(relaySession))
        let authStore = GoogleFlowAuthStore()
        let flow = GoogleRelaySignInFlow(
            coordinatorProvider: { coordinator },
            apiClient: apiClient,
            authStore: authStore
        )

        let result = try await flow.signIn()

        XCTAssertEqual(result, relaySession)
        XCTAssertEqual(coordinator.signInCallCount, 1)
        XCTAssertEqual(apiClient.googleRequests.count, 1)
        XCTAssertEqual(apiClient.googleRequests.first?.idToken, "google-id-token")
        XCTAssertEqual(apiClient.googleRequests.first?.nonce, "nonce")
        XCTAssertEqual(authStore.savedSessions, [relaySession])
        XCTAssertEqual(authStore.loadSession(), relaySession)
    }

    func testSuccessCanDeferRelaySessionPersistence() async throws {
        let relaySession = makeSession(email: "google@example.com")
        let coordinator = StubGoogleSignInCoordinator(
            result: .success(GoogleIdentityToken(idToken: "google-id-token", nonce: "nonce"))
        )
        let apiClient = GoogleFlowAPIClient(googleResult: .success(relaySession))
        let authStore = GoogleFlowAuthStore()
        let flow = GoogleRelaySignInFlow(
            coordinatorProvider: { coordinator },
            apiClient: apiClient,
            authStore: authStore,
            persistSession: false
        )

        let result = try await flow.signIn()

        XCTAssertEqual(result, relaySession)
        XCTAssertEqual(apiClient.googleRequests.count, 1)
        XCTAssertTrue(authStore.savedSessions.isEmpty)
        XCTAssertNil(authStore.loadSession())
    }

    func testCancellationPreservesExistingSessionAndDoesNotCallRelay() async {
        let existingSession = makeSession(email: "existing@example.com")
        let coordinator = StubGoogleSignInCoordinator(result: .failure(GoogleSignInError.cancelled))
        let apiClient = GoogleFlowAPIClient(googleResult: .failure(GoogleFlowTestError.failed))
        let authStore = GoogleFlowAuthStore()
        authStore.session = existingSession
        let flow = GoogleRelaySignInFlow(
            coordinatorProvider: { coordinator },
            apiClient: apiClient,
            authStore: authStore
        )

        do {
            _ = try await flow.signIn()
            XCTFail("Expected cancellation")
        } catch {
            XCTAssertTrue(GoogleSignInError.isCancellation(error))
        }

        XCTAssertTrue(apiClient.googleRequests.isEmpty)
        XCTAssertTrue(authStore.savedSessions.isEmpty)
        XCTAssertEqual(authStore.loadSession(), existingSession)
    }

    func testCoordinatorErrorPreservesExistingSession() async {
        let existingSession = makeSession(email: "existing@example.com")
        let coordinator = StubGoogleSignInCoordinator(result: .failure(GoogleFlowTestError.failed))
        let apiClient = GoogleFlowAPIClient(googleResult: .failure(GoogleFlowTestError.failed))
        let authStore = GoogleFlowAuthStore()
        authStore.session = existingSession
        let flow = GoogleRelaySignInFlow(
            coordinatorProvider: { coordinator },
            apiClient: apiClient,
            authStore: authStore
        )

        do {
            _ = try await flow.signIn()
            XCTFail("Expected coordinator failure")
        } catch GoogleFlowTestError.failed {
        } catch {
            XCTFail("Unexpected error: \(error)")
        }

        XCTAssertTrue(apiClient.googleRequests.isEmpty)
        XCTAssertTrue(authStore.savedSessions.isEmpty)
        XCTAssertEqual(authStore.loadSession(), existingSession)
    }

    func testRelayErrorPreservesExistingSession() async {
        let existingSession = makeSession(email: "existing@example.com")
        let coordinator = StubGoogleSignInCoordinator(
            result: .success(GoogleIdentityToken(idToken: "google-id-token", nonce: "nonce"))
        )
        let apiClient = GoogleFlowAPIClient(googleResult: .failure(GoogleFlowTestError.failed))
        let authStore = GoogleFlowAuthStore()
        authStore.session = existingSession
        let flow = GoogleRelaySignInFlow(
            coordinatorProvider: { coordinator },
            apiClient: apiClient,
            authStore: authStore
        )

        do {
            _ = try await flow.signIn()
            XCTFail("Expected relay failure")
        } catch GoogleFlowTestError.failed {
        } catch {
            XCTFail("Unexpected error: \(error)")
        }

        XCTAssertEqual(apiClient.googleRequests.count, 1)
        XCTAssertTrue(authStore.savedSessions.isEmpty)
        XCTAssertEqual(authStore.loadSession(), existingSession)
    }

    private func makeSession(email: String) -> RelayAuthSession {
        RelayAuthSession(
            accessToken: "access-token",
            refreshToken: "refresh-token",
            tokenType: "Bearer",
            expiresAt: Date(timeIntervalSince1970: 2_000_000_000),
            refreshExpiresAt: Date(timeIntervalSince1970: 2_100_000_000),
            account: RelayAccount(id: "42", email: email, displayName: "User"),
            device: RelayDevice(id: "7", name: "Test iPhone", deviceType: "ios"),
            session: RelaySessionInfo(id: "3")
        )
    }
}
