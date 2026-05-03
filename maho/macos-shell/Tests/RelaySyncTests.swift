import Foundation

func testRelayServerLocatorNormalizesWebSocketURLs() {
    let secure = RelayServerLocator.normalizedBaseURL(from: "wss://sync.maho.app")
    assertEqual(secure?.absoluteString, "https://sync.maho.app")

    let insecure = RelayServerLocator.normalizedBaseURL(from: "ws://localhost:8787/api/")
    assertEqual(insecure?.absoluteString, "http://localhost:8787/api")
}

func testRelayServerLocatorRejectsUnsupportedSchemes() {
    assertTrue(RelayServerLocator.normalizedBaseURL(from: "ftp://sync.maho.app") == nil)
    assertTrue(RelayServerLocator.normalizedBaseURL(from: "sync.maho.app") == nil)
}

func testRelayEndpointCandidatesDeduplicatePaths() {
    let baseURL = RelayServerLocator.normalizedBaseURL(from: "https://sync.maho.app")!
    let candidates = RelayServerLocator.endpointCandidates(baseURL: baseURL, pathVariants: ["/auth/login", "auth/login", "login"])
    assertEqual(candidates.map(\.absoluteString), [
        "https://sync.maho.app/auth/login",
        "https://sync.maho.app/login",
    ])
}

func testRelayAuthSessionDecodesNestedPayload() throws {
    let payload: [String: Any] = [
        "data": [
            "access_token": "access-123",
            "refresh_token": "refresh-456",
            "user": [
                "email": "person@example.com",
                "display_name": "Person"
            ]
        ]
    ]
    let data = try JSONSerialization.data(withJSONObject: payload)
    let session = RelayPayloadCodec.authSession(
        from: data,
        baseURL: URL(string: "https://sync.maho.app")!,
        fallbackEmail: "fallback@example.com",
        fallbackDisplayName: nil
    )

    assertEqual(session?.accessToken, "access-123")
    assertEqual(session?.refreshToken, "refresh-456")
    assertEqual(session?.email, "person@example.com")
    assertEqual(session?.displayName, "Person")
}

func testRelayAuthSessionUsesFallbackRefreshToken() throws {
    let payload: [String: Any] = [
        "session": [
            "access_token": "new-access",
            "user": ["email": "saved@example.com"]
        ]
    ]
    let data = try JSONSerialization.data(withJSONObject: payload)
    let session = RelayPayloadCodec.authSession(
        from: data,
        baseURL: URL(string: "https://sync.maho.app")!,
        fallbackEmail: "saved@example.com",
        fallbackDisplayName: "Saved User",
        fallbackRefreshToken: "existing-refresh"
    )

    assertEqual(session?.refreshToken, "existing-refresh")
    assertEqual(session?.displayName, "Saved User")
}

func testRelayPayloadCodecEncodesSinglePushBlob() {
    let blobs = RelayPayloadCodec.pushBlobs(from: "[{\"id\":\"tab-1\"}]")
    assertEqual(blobs.count, 1)
    assertEqual(RelayPayloadCodec.decodedBlobString(from: blobs[0]), "{\"id\":\"tab-1\"}")
}

func testRelayPayloadCodecEncodesMultiplePushBlobsFromArrayItems() {
    let outgoing = "[{\"id\":\"tab-1\"},{\"id\":\"tab-2\"}]"
    let blobs = RelayPayloadCodec.pushBlobs(from: outgoing)
    assertEqual(blobs.count, 2)
    assertEqual(RelayPayloadCodec.decodedBlobString(from: blobs[0]), "{\"id\":\"tab-1\"}")
    assertEqual(RelayPayloadCodec.decodedBlobString(from: blobs[1]), "{\"id\":\"tab-2\"}")
}

func testRelayPayloadCodecDecodesPullEnvelopeWithSeqZero() throws {
    let payload: [String: Any] = [
        "messages": [
            ["seq": 0, "blob": Data("[{\"id\":\"first\"}]".utf8).base64EncodedString()],
            ["seq": 1, "blob": Data("[{\"id\":\"second\"}]".utf8).base64EncodedString()],
        ],
        "has_more": true,
    ]
    let data = try JSONSerialization.data(withJSONObject: payload)
    let envelope = RelayPayloadCodec.pullEnvelope(from: data)

    assertEqual(envelope?.messages.count, 2)
    assertEqual(envelope?.messages[0].seq, 0)
    assertEqual(RelayPayloadCodec.decodedBlobString(from: envelope?.messages[0].blob ?? ""), "[{\"id\":\"first\"}]")
    assertEqual(envelope?.messages[1].seq, 1)
    assertTrue(envelope?.hasMore == true)
}

func testRelayPayloadCodecDecodesEmptyPullEnvelope() throws {
    let payload: [String: Any] = [
        "messages": [],
        "has_more": false,
    ]
    let data = try JSONSerialization.data(withJSONObject: payload)
    let envelope = RelayPayloadCodec.pullEnvelope(from: data)

    assertEqual(envelope?.messages.count, 0)
    assertFalse(envelope?.hasMore ?? true)
}

func testRelayPayloadCodecRejectsInvalidBlob() {
    assertTrue(RelayPayloadCodec.decodedBlobString(from: "not-base64") == nil)
}

func testRelayPayloadCodecFindsErrorMessage() throws {
    let payload: [String: Any] = ["error": ["message": "Bad credentials"]]
    let data = try JSONSerialization.data(withJSONObject: payload)
    assertEqual(RelayPayloadCodec.errorMessage(from: data), "Bad credentials")
}

func runRelaySyncTests() {
    testRelayServerLocatorNormalizesWebSocketURLs()
    testRelayServerLocatorRejectsUnsupportedSchemes()
    testRelayEndpointCandidatesDeduplicatePaths()
    try! testRelayAuthSessionDecodesNestedPayload()
    try! testRelayAuthSessionUsesFallbackRefreshToken()
    testRelayPayloadCodecEncodesSinglePushBlob()
    testRelayPayloadCodecEncodesMultiplePushBlobsFromArrayItems()
    try! testRelayPayloadCodecDecodesPullEnvelopeWithSeqZero()
    try! testRelayPayloadCodecDecodesEmptyPullEnvelope()
    testRelayPayloadCodecRejectsInvalidBlob()
    try! testRelayPayloadCodecFindsErrorMessage()
    print("ALL TESTS PASSED")
}
