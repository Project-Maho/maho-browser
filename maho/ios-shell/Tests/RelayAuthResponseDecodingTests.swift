import XCTest
@testable import Maho

/// Regression coverage for the relay HTTP/keychain JSON boundary.
///
/// The relay server (`maho/relay/src/auth/models.rs`) emits the canonical
/// integer contract: `expires_at`/`refresh_expires_at` as i64 Unix seconds and
/// `account.id`/`device.id`/`session.id` as i32 integers. The FFI-oriented
/// `MahoJSON` coder uses `.iso8601` dates and `String?` ids and therefore
/// throws `typeMismatch` on this payload. These tests pin the dedicated relay
/// coder to the server contract while keeping the downstream `String?` id
/// surface intact.
final class RelayAuthResponseDecodingTests: XCTestCase {

    /// A real, server-shaped auth payload: snake_case keys, integer Unix-second
    /// timestamps, and integer account/device/session ids.
    private let serverShapedAuthJSON = """
    {
        "access_token": "access-abc",
        "refresh_token": "refresh-xyz",
        "token_type": "Bearer",
        "expires_at": 1700000000,
        "refresh_expires_at": 1700086400,
        "account": {
            "id": 42,
            "email": "user@example.com",
            "display_name": "Test User",
            "oauth_provider": "google",
            "oauth_provider_sub": "google-sub-123",
            "created_at": 1699000000,
            "locked_until": null,
            "failed_login_attempts": 0,
            "last_failed_login_at": null
        },
        "device": {
            "id": 7,
            "user_id": 42,
            "name": "Test Device",
            "device_type": "ios",
            "room_id": "room-1",
            "created_at": 1699000000,
            "last_seen": null
        },
        "session": {
            "id": 3,
            "user_id": 42,
            "device_id": 7,
            "expires_at": 1700000000,
            "refresh_expires_at": 1700086400,
            "last_used_at": null,
            "created_at": 1699000000
        }
    }
    """

    func testRelayDecoderDecodesServerIntegerContract() throws {
        let data = try XCTUnwrap(serverShapedAuthJSON.data(using: .utf8))

        let response = try RelayCoding.decoder.decode(RelayAuthResponse.self, from: data)

        XCTAssertEqual(response.accessToken, "access-abc")
        XCTAssertEqual(response.refreshToken, "refresh-xyz")
        XCTAssertEqual(response.tokenType, "Bearer")
        XCTAssertEqual(response.expiresAt, Date(timeIntervalSince1970: 1_700_000_000))
        XCTAssertEqual(response.refreshExpiresAt, Date(timeIntervalSince1970: 1_700_086_400))
        XCTAssertEqual(response.account.id, "42")
        XCTAssertEqual(response.account.email, "user@example.com")
        XCTAssertEqual(response.account.displayName, "Test User")
        XCTAssertEqual(response.account.oauthProvider, "google")
        XCTAssertEqual(response.account.oauthProviderSub, "google-sub-123")
        XCTAssertEqual(response.device?.id, "7")
        XCTAssertEqual(response.device?.name, "Test Device")
        XCTAssertEqual(response.device?.deviceType, "ios")
        XCTAssertEqual(response.session?.id, "3")
    }

    func testRelaySessionExpiryChecksUseDecodedDates() throws {
        let data = try XCTUnwrap(serverShapedAuthJSON.data(using: .utf8))
        let session = try RelayCoding.decoder.decode(RelayAuthResponse.self, from: data).asSession()

        // 1700000000 is far in the past → both tokens read as expired via Date comparison.
        XCTAssertTrue(session.isAccessTokenExpired)
        XCTAssertTrue(session.isRefreshTokenExpired)
    }

    /// Keychain persistence round-trips through the SAME relay coder: the encoder
    /// writes ids back as strings and dates as seconds, and the decoder must
    /// accept exactly what it wrote (RelayID accepts the string form).
    func testRelayCoderKeychainRoundTrip() throws {
        let original = RelayAuthSession(
            accessToken: "access",
            refreshToken: "refresh",
            tokenType: "Bearer",
            expiresAt: Date(timeIntervalSince1970: 1_700_000_000),
            refreshExpiresAt: Date(timeIntervalSince1970: 1_700_086_400),
            account: RelayAccount(
                id: "42",
                email: "user@example.com",
                displayName: "Test User",
                oauthProvider: "google",
                oauthProviderSub: "google-sub-123"
            ),
            device: RelayDevice(id: "7", name: "Test Device", deviceType: "ios"),
            session: RelaySessionInfo(id: "3")
        )

        let encoded = try RelayCoding.encoder.encode(original)
        let decoded = try RelayCoding.decoder.decode(RelayAuthSession.self, from: encoded)

        XCTAssertEqual(decoded, original)
        XCTAssertEqual(decoded.account.id, "42")
        XCTAssertEqual(decoded.device?.id, "7")
        XCTAssertEqual(decoded.session?.id, "3")
        XCTAssertEqual(decoded.account.oauthProvider, "google")
        XCTAssertEqual(decoded.account.oauthProviderSub, "google-sub-123")
    }

    func testOldPersistedSessionWithoutOAuthMetadataStillDecodes() throws {
        let oldSessionJSON = """
        {
          "access_token":"access",
          "refresh_token":"refresh",
          "token_type":"Bearer",
          "expires_at":1700000000,
          "refresh_expires_at":1700086400,
          "account":{"id":"42","email":"user@example.com","display_name":"User"}
        }
        """

        let decoded = try RelayCoding.decoder.decode(
            RelayAuthSession.self,
            from: try XCTUnwrap(oldSessionJSON.data(using: .utf8))
        )

        XCTAssertNil(decoded.account.oauthProvider)
        XCTAssertNil(decoded.account.oauthProviderSub)
    }

    func testMalformedWhitespaceOAuthMetadataIsDiscardedAsPair() throws {
        let malformed = serverShapedAuthJSON
            .replacingOccurrences(of: "\"google\"", with: "\" google \"")
            .replacingOccurrences(of: "\"google-sub-123\"", with: "\"   \"")

        let response = try RelayCoding.decoder.decode(
            RelayAuthResponse.self,
            from: try XCTUnwrap(malformed.data(using: .utf8))
        )

        XCTAssertNil(response.account.oauthProvider)
        XCTAssertNil(response.account.oauthProviderSub)
    }

    func testRelayDecoderDecodesAccountSyncBootstrap() throws {
        let bootstrap = try RelayCoding.decoder.decode(
            RelaySyncBootstrap.self,
            from: Data(
                """
                {
                  "version": 1,
                  "room_id": "0123456789abcdef0123456789abcdef",
                  "seed": "WlpaWlpaWlpaWlpaWlpaWlpaWlpaWlpaWlpaWlpaWlo=",
                  "created_at": 42
                }
                """.utf8
            )
        )

        XCTAssertEqual(bootstrap.version, 1)
        XCTAssertEqual(bootstrap.roomId, "0123456789abcdef0123456789abcdef")
        XCTAssertEqual(bootstrap.createdAt, 42)
    }

    func testRelayBootstrapRequestUsesServerSnakeCaseKeys() throws {
        let request = RelaySyncBootstrapRequest(
            version: 1,
            roomId: "0123456789abcdef0123456789abcdef",
            seed: "Wlpa"
        )

        let payload = try JSONSerialization.jsonObject(
            with: RelayCoding.encoder.encode(request)
        ) as? [String: Any]

        XCTAssertEqual(payload?["room_id"] as? String, request.roomId)
        XCTAssertEqual(payload?["seed"] as? String, request.seed)
        XCTAssertNil(payload?["created_at"])
    }

    // MARK: - Cleartext Rejection Tests

    private func makeDummySession() -> RelayAuthSession {
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

    private func makeDummyBootstrap() -> RelaySyncBootstrap {
        RelaySyncBootstrap(
            version: 1,
            roomId: "0123456789abcdef0123456789abcdef",
            seed: "WlpaWlpaWlpaWlpaWlpaWlpaWlpaWlpaWlpaWlpaWlo=",
            createdAt: 42
        )
    }

    func testRelayClientRejectsCleartextHttpAndWsBaseURLsBeforeRequestConstruction() async {
        let cleartextURLs = [
            "http://relay.mahobrowser.com",
            "http://localhost:8080",
            "ws://relay.mahobrowser.com",
            "ws://127.0.0.1:8080",
            "HTTP://relay.mahobrowser.com",
            "WS://relay.mahobrowser.com",
            "   http://relay.mahobrowser.com   ",
            "   ws://relay.mahobrowser.com   ",
        ]

        let session = makeDummySession()
        let bootstrap = makeDummyBootstrap()

        for baseURL in cleartextURLs {
            let client = RelayAPIClient(baseURLProvider: { baseURL })

            // 1. authenticate (login)
            do {
                _ = try await client.authenticate(mode: .login, email: "user@example.com", password: "password", displayName: nil)
                XCTFail("Expected cleartext rejection for login with base URL: \(baseURL)")
            } catch RelayAPIError.invalidBaseURL {
                // Expected
            } catch {
                XCTFail("Unexpected error for login with \(baseURL): \(error)")
            }

            // 2. authenticate (signup)
            do {
                _ = try await client.authenticate(mode: .signup, email: "user@example.com", password: "password", displayName: "User")
                XCTFail("Expected cleartext rejection for signup with base URL: \(baseURL)")
            } catch RelayAPIError.invalidBaseURL {
                // Expected
            } catch {
                XCTFail("Unexpected error for signup with \(baseURL): \(error)")
            }

            // 3. authenticateWithGoogle
            do {
                _ = try await client.authenticateWithGoogle(idToken: "google-id-token", nonce: "nonce-value")
                XCTFail("Expected cleartext rejection for google auth with base URL: \(baseURL)")
            } catch RelayAPIError.invalidBaseURL {
                // Expected
            } catch {
                XCTFail("Unexpected error for google auth with \(baseURL): \(error)")
            }

            // 4. refresh
            do {
                _ = try await client.refresh(using: "refresh-token")
                XCTFail("Expected cleartext rejection for refresh with base URL: \(baseURL)")
            } catch RelayAPIError.invalidBaseURL {
                // Expected
            } catch {
                XCTFail("Unexpected error for refresh with \(baseURL): \(error)")
            }

            // 5. logout
            do {
                try await client.logout(session: session)
                XCTFail("Expected cleartext rejection for logout with base URL: \(baseURL)")
            } catch RelayAPIError.invalidBaseURL {
                // Expected
            } catch {
                XCTFail("Unexpected error for logout with \(baseURL): \(error)")
            }

            // 6. getSyncBootstrap
            do {
                _ = try await client.getSyncBootstrap(session: session)
                XCTFail("Expected cleartext rejection for getSyncBootstrap with base URL: \(baseURL)")
            } catch RelayAPIError.invalidBaseURL {
                // Expected
            } catch {
                XCTFail("Unexpected error for getSyncBootstrap with \(baseURL): \(error)")
            }

            // 7. putSyncBootstrap
            do {
                _ = try await client.putSyncBootstrap(bootstrap, session: session)
                XCTFail("Expected cleartext rejection for putSyncBootstrap with base URL: \(baseURL)")
            } catch RelayAPIError.invalidBaseURL {
                // Expected
            } catch {
                XCTFail("Unexpected error for putSyncBootstrap with \(baseURL): \(error)")
            }

            // 8. push
            do {
                _ = try await client.push(roomID: "0123456789abcdef0123456789abcdef", envelopes: [], session: session)
                XCTFail("Expected cleartext rejection for push with base URL: \(baseURL)")
            } catch RelayAPIError.invalidBaseURL {
                // Expected
            } catch {
                XCTFail("Unexpected error for push with \(baseURL): \(error)")
            }

            // 9. pull
            do {
                _ = try await client.pull(roomID: "0123456789abcdef0123456789abcdef", afterSeq: 0, limit: 100, session: session)
                XCTFail("Expected cleartext rejection for pull with base URL: \(baseURL)")
            } catch RelayAPIError.invalidBaseURL {
                // Expected
            } catch {
                XCTFail("Unexpected error for pull with \(baseURL): \(error)")
            }
        }
    }

    func testRelayClientNormalizesWssAndHttpsSafely() {
        XCTAssertEqual(
            RelayAPIClient.normalizedBaseURLString(from: "wss://relay.mahobrowser.com"),
            "https://relay.mahobrowser.com"
        )
        XCTAssertEqual(
            RelayAPIClient.normalizedBaseURLString(from: "WSS://relay.mahobrowser.com/sync/"),
            "https://relay.mahobrowser.com/sync/"
        )
        XCTAssertEqual(
            RelayAPIClient.normalizedBaseURLString(from: "https://relay.mahobrowser.com"),
            "https://relay.mahobrowser.com"
        )
        XCTAssertEqual(
            RelayAPIClient.normalizedBaseURLString(from: "   https://relay.mahobrowser.com   "),
            "https://relay.mahobrowser.com"
        )
    }
}
