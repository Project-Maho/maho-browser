import Foundation

private enum FocusedTestFailure: Error, CustomStringConvertible {
    case assertion(String)

    var description: String {
        switch self {
        case .assertion(let message): return message
        }
    }
}

private enum StubError: Error {
    case remoteLogoutFailed
}

private func expect(_ condition: @autoclosure () -> Bool, _ message: String) throws {
    guard condition() else { throw FocusedTestFailure.assertion(message) }
}

private final class FailingClearAuthStore: RelayAuthStoreProtocol {
    let storedSession: RelayAuthSession
    private(set) var clearCallCount = 0

    init(session: RelayAuthSession) {
        storedSession = session
    }

    func loadSession() -> RelayAuthSession? { storedSession }
    func saveSession(_ session: RelayAuthSession) throws {}

    func clearSession() throws {
        clearCallCount += 1
        throw RelayAuthStoreError.unexpectedKeychainStatus(-50)
    }
}

private final class FocusedAPIClient: RelayAPIClientProtocol {
    var logoutError: Error?
    private(set) var logoutCallCount = 0

    func authenticate(mode: RelayAuthMode, email: String, password: String, displayName: String?) async throws -> RelayAuthSession {
        throw StubError.remoteLogoutFailed
    }

    func authenticateWithGoogle(idToken: String, nonce: String) async throws -> RelayAuthSession {
        throw StubError.remoteLogoutFailed
    }

    func refresh(using refreshToken: String) async throws -> RelayAuthSession {
        throw StubError.remoteLogoutFailed
    }

    func logout(session: RelayAuthSession) async throws {
        logoutCallCount += 1
        if let logoutError { throw logoutError }
    }

    func push(blobs: [String], session: RelayAuthSession) async throws -> RelayPushResponse {
        throw StubError.remoteLogoutFailed
    }

    func pull(sinceSeq: Int, limit: Int, session: RelayAuthSession) async throws -> RelayPullResponse {
        throw StubError.remoteLogoutFailed
    }

    func getLatestSnapshot(roomID: String, session: RelayAuthSession) async throws -> DownloadSnapshotResponse {
        throw StubError.remoteLogoutFailed
    }
}

@main
private struct IOSAuthSecurityFocusedTests {
    static func main() async throws {
        try await testRNGFailureAbortsGoogleSignInBeforePresentation()
        try await testKeychainDeletionFailureDoesNotCommitLogoutState()
        print("PASS: iOS auth security focused tests")
    }

    @MainActor
    private static func testRNGFailureAbortsGoogleSignInBeforePresentation() async throws {
        for failingCall in 1 ... 3 {
            var generationCallCount = 0
            let coordinator = GoogleSignInCoordinator(
                configuration: GoogleSignInConfiguration(
                    clientID: "client-id",
                    redirectScheme: "test.redirect"
                ),
                randomVerifier: {
                    generationCallCount += 1
                    if generationCallCount == failingCall {
                        throw GoogleSignInError.randomGenerationFailed(-1)
                    }
                    return "secure-value-\(generationCallCount)"
                }
            )

            do {
                _ = try await coordinator.signIn()
                throw FocusedTestFailure.assertion("Google sign-in unexpectedly continued after RNG failure on call \(failingCall)")
            } catch GoogleSignInError.randomGenerationFailed(let status) {
                try expect(status == -1, "Google sign-in changed the RNG failure status")
            }

            try expect(
                generationCallCount == failingCall,
                "Google sign-in continued generating values after RNG failure on call \(failingCall)"
            )
        }
    }

    @MainActor
    private static func testKeychainDeletionFailureDoesNotCommitLogoutState() async throws {
        let session = makeSession()
        let authStore = FailingClearAuthStore(session: session)
        let apiClient = FocusedAPIClient()
        apiClient.logoutError = StubError.remoteLogoutFailed
        let flow = RelaySignOutFlow(apiClient: apiClient, authStore: authStore)
        var committedLogout = false

        do {
            try await flow.signOut(session: session) {
                committedLogout = true
            }
            throw FocusedTestFailure.assertion("Logout unexpectedly succeeded after Keychain deletion failure")
        } catch RelayAuthStoreError.unexpectedKeychainStatus(let status) {
            try expect(status == -50, "Settings-facing flow changed the Keychain failure status")
        }

        try expect(apiClient.logoutCallCount == 1, "Remote logout was not attempted")
        try expect(authStore.clearCallCount == 1, "Durable relay credentials were not deleted")
        try expect(authStore.loadSession() == session, "Failed deletion cleared the durable session")
        try expect(!committedLogout, "Settings committed logged-out in-memory state after deletion failed")
    }

    private static func makeSession() -> RelayAuthSession {
        RelayAuthSession(
            accessToken: "access-token",
            refreshToken: "refresh-token",
            tokenType: "Bearer",
            expiresAt: Date(timeIntervalSince1970: 2_000_000_000),
            refreshExpiresAt: Date(timeIntervalSince1970: 2_100_000_000),
            account: RelayAccount(id: "42", email: "user@example.com", displayName: "User"),
            device: RelayDevice(id: "7", name: "Test iPhone", deviceType: "ios"),
            session: RelaySessionInfo(id: "3")
        )
    }
}
