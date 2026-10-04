import Foundation
#if canImport(UIKit)
import UIKit
#endif

struct RelayAuthResponse: Codable, Equatable {
    let accessToken: String
    let refreshToken: String
    let tokenType: String
    let expiresAt: Date
    let refreshExpiresAt: Date
    let account: RelayAccount
    let device: RelayDevice?
    let session: RelaySessionInfo?

    func asSession() -> RelayAuthSession {
        RelayAuthSession(
            accessToken: accessToken,
            refreshToken: refreshToken,
            tokenType: tokenType,
            expiresAt: expiresAt,
            refreshExpiresAt: refreshExpiresAt,
            account: account,
            device: device,
            session: session
        )
    }
}

struct RelayLoginRequest: Codable, Equatable {
    let email: String
    let password: String
    let deviceName: String?
    let deviceType: String?
}

struct RelaySignupRequest: Codable, Equatable {
    let email: String
    let password: String
    let displayName: String?
    let deviceName: String?
    let deviceType: String?
}

struct RelayGoogleOAuthRequest: Codable, Equatable {
    let idToken: String
    let nonce: String
    let deviceName: String?
    let deviceType: String?
}

struct RelayRefreshRequest: Codable, Equatable {
    let refreshToken: String
}

struct RelayPushRequest: Codable, Equatable {
    let roomId: String
    let messages: [SyncEnvelopeV2]
}

struct RelayPushResponse: Codable, Equatable {
    let acks: [RelayAckV2]
}

struct RelayPullResponse: Codable, Equatable {
    let messages: [SyncEnvelopeV2]
    let hasMore: Bool
}

struct RelaySyncBootstrap: Codable, Equatable {
    let version: Int
    let roomId: String
    let seed: String
    let createdAt: Int?
}

struct RelaySyncBootstrapRequest: Codable, Equatable {
    let version: Int
    let roomId: String
    let seed: String
}

struct DownloadSnapshotResponse: Codable, Equatable {
    let hlcTsCeiling: Int64
    let createdAt: Int64
    let creatorDeviceId: Int32
    let blob: String
}

struct RelayLogoutResponse: Codable, Equatable {
    let ok: Bool
}

enum RelayAuthMode {
    case signup
    case login
}

enum RelayAPIError: LocalizedError {
    case invalidBaseURL(String)
    case invalidHTTPResponse
    case unauthorized
    case server(message: String)
    case http(statusCode: Int, message: String)
    case missingRefreshToken

    var errorDescription: String? {
        switch self {
        case .invalidBaseURL(let value):
            return "Invalid relay server URL: \(value)"
        case .invalidHTTPResponse:
            return "Relay returned an invalid response."
        case .unauthorized:
            return "Your relay session expired. Please sign in again."
        case .server(let message):
            return message
        case .http(let statusCode, let message):
            return "Relay request failed (\(statusCode)): \(message)"
        case .missingRefreshToken:
            return "Missing relay refresh token."
        }
    }
}

protocol RelayAPIClientProtocol: AnyObject {
    func authenticate(mode: RelayAuthMode, email: String, password: String, displayName: String?) async throws -> RelayAuthSession
    func authenticateWithGoogle(idToken: String, nonce: String) async throws -> RelayAuthSession
    func refresh(using refreshToken: String) async throws -> RelayAuthSession
    func logout(session: RelayAuthSession) async throws
    func push(roomID: String, envelopes: [SyncEnvelopeV2], session: RelayAuthSession) async throws -> RelayPushResponse
    func pull(roomID: String, afterSeq: Int, limit: Int, session: RelayAuthSession) async throws -> RelayPullResponse
    func getSyncBootstrap(session: RelayAuthSession) async throws -> RelaySyncBootstrap
    func putSyncBootstrap(_ bootstrap: RelaySyncBootstrap, session: RelayAuthSession) async throws -> RelaySyncBootstrap
    func getLatestSnapshot(roomID: String, session: RelayAuthSession) async throws -> DownloadSnapshotResponse
}

final class RelayAPIClient: RelayAPIClientProtocol {
    static let serverURLDefaultsKey = "maho_sync_server_url"
    static let defaultBaseURLString = "https://relay.mahobrowser.com"

    private enum Endpoint: String {
        case signup = "/auth/signup"
        case login = "/auth/login"
        case googleOAuth = "/auth/oauth/google"
        case refresh = "/auth/refresh"
        case logout = "/auth/logout"
        case push = "/sync/push"
        case pull = "/sync/pull"
        case bootstrap = "/sync/bootstrap"
        case latestSnapshot = "/sync/snapshot/latest"
    }

    private let baseURLProvider: () -> String
    private let session: URLSession
    private let deviceNameProvider: () -> String?
    private let deviceTypeProvider: () -> String?

    init(
        session: URLSession = .shared,
        baseURLProvider: @escaping () -> String = {
            RelayAPIClient.currentBaseURLString()
        },
        deviceNameProvider: @escaping () -> String? = {
            #if canImport(UIKit)
            UIDevice.current.name
            #else
            nil
            #endif
        },
        deviceTypeProvider: @escaping () -> String? = {
            #if canImport(UIKit)
            "ios"
            #else
            nil
            #endif
        }
    ) {
        self.session = session
        self.baseURLProvider = baseURLProvider
        self.deviceNameProvider = deviceNameProvider
        self.deviceTypeProvider = deviceTypeProvider
    }

    func authenticate(mode: RelayAuthMode, email: String, password: String, displayName: String?) async throws -> RelayAuthSession {
        switch mode {
        case .signup:
            let payload = RelaySignupRequest(
                email: email,
                password: password,
                displayName: displayName,
                deviceName: deviceNameProvider(),
                deviceType: deviceTypeProvider()
            )
            let response: RelayAuthResponse = try await sendJSONRequest(
                endpoint: .signup,
                method: "POST",
                body: payload,
                authorizedSession: nil
            )
            return response.asSession()
        case .login:
            let payload = RelayLoginRequest(
                email: email,
                password: password,
                deviceName: deviceNameProvider(),
                deviceType: deviceTypeProvider()
            )
            let response: RelayAuthResponse = try await sendJSONRequest(
                endpoint: .login,
                method: "POST",
                body: payload,
                authorizedSession: nil
            )
            return response.asSession()
        }
    }

    func authenticateWithGoogle(idToken: String, nonce: String) async throws -> RelayAuthSession {
        let payload = RelayGoogleOAuthRequest(
            idToken: idToken,
            nonce: nonce,
            deviceName: deviceNameProvider(),
            deviceType: deviceTypeProvider()
        )
        let response: RelayAuthResponse = try await sendJSONRequest(
            endpoint: .googleOAuth,
            method: "POST",
            body: payload,
            authorizedSession: nil
        )
        return response.asSession()
    }

    func refresh(using refreshToken: String) async throws -> RelayAuthSession {
        guard !refreshToken.isEmpty else {
            throw RelayAPIError.missingRefreshToken
        }

        let response: RelayAuthResponse = try await sendJSONRequest(
            endpoint: .refresh,
            method: "POST",
            body: RelayRefreshRequest(refreshToken: refreshToken),
            authorizedSession: nil
        )
        return response.asSession()
    }

    func logout(session: RelayAuthSession) async throws {
        _ = try await sendJSONRequest(
            endpoint: .logout,
            method: "POST",
            body: Optional<String>.none,
            authorizedSession: session
        ) as RelayLogoutResponse
    }

    func push(roomID: String, envelopes: [SyncEnvelopeV2], session: RelayAuthSession) async throws -> RelayPushResponse {
        try await sendJSONRequest(
            endpoint: .push,
            method: "POST",
            body: RelayPushRequest(roomId: roomID, messages: envelopes),
            authorizedSession: session
        )
    }

    func pull(roomID: String, afterSeq: Int, limit: Int, session: RelayAuthSession) async throws -> RelayPullResponse {
        var components = URLComponents(url: try makeURL(for: .pull), resolvingAgainstBaseURL: false)
        components?.queryItems = [
            URLQueryItem(name: "room_id", value: roomID),
            URLQueryItem(name: "after_seq", value: String(afterSeq)),
            URLQueryItem(name: "limit", value: String(max(1, min(limit, 100))))
        ]

        guard let url = components?.url else {
            throw RelayAPIError.invalidBaseURL(baseURLProvider())
        }

        return try await sendJSONRequest(
            url: url,
            method: "GET",
            body: Optional<String>.none,
            authorizedSession: session
        )
    }

    func getSyncBootstrap(session: RelayAuthSession) async throws -> RelaySyncBootstrap {
        try await sendJSONRequest(
            endpoint: .bootstrap,
            method: "GET",
            body: Optional<String>.none,
            authorizedSession: session
        )
    }

    func putSyncBootstrap(
        _ bootstrap: RelaySyncBootstrap,
        session: RelayAuthSession
    ) async throws -> RelaySyncBootstrap {
        try await sendJSONRequest(
            endpoint: .bootstrap,
            method: "PUT",
            body: RelaySyncBootstrapRequest(
                version: bootstrap.version,
                roomId: bootstrap.roomId,
                seed: bootstrap.seed
            ),
            authorizedSession: session
        )
    }

    func getLatestSnapshot(roomID: String, session: RelayAuthSession) async throws -> DownloadSnapshotResponse {
        var components = URLComponents(url: try makeURL(for: .latestSnapshot), resolvingAgainstBaseURL: false)
        components?.queryItems = [
            URLQueryItem(name: "room_id", value: roomID)
        ]

        guard let url = components?.url else {
            throw RelayAPIError.invalidBaseURL(baseURLProvider())
        }

        return try await sendJSONRequest(
            url: url,
            method: "GET",
            body: Optional<String>.none,
            authorizedSession: session
        )
    }

    private func sendJSONRequest<Body: Encodable, Response: Decodable>(
        endpoint: Endpoint,
        method: String,
        body: Body?,
        authorizedSession: RelayAuthSession?
    ) async throws -> Response {
        try await sendJSONRequest(
            url: try makeURL(for: endpoint),
            method: method,
            body: body,
            authorizedSession: authorizedSession
        )
    }

    private func sendJSONRequest<Body: Encodable, Response: Decodable>(
        url: URL,
        method: String,
        body: Body?,
        authorizedSession: RelayAuthSession?
    ) async throws -> Response {
        guard let scheme = url.scheme?.lowercased(), scheme == "https" else {
            throw RelayAPIError.invalidBaseURL(url.absoluteString)
        }

        var request = URLRequest(url: url)
        request.httpMethod = method
        request.setValue("application/json", forHTTPHeaderField: "Accept")

        if body != nil {
            request.setValue("application/json", forHTTPHeaderField: "Content-Type")
        }

        if let authorizedSession {
            request.setValue(authorizedSession.authorizationHeaderValue, forHTTPHeaderField: "Authorization")
        }

        if let body {
            request.httpBody = try RelayCoding.encoder.encode(body)
        }

        let (data, response) = try await session.data(for: request)
        guard let httpResponse = response as? HTTPURLResponse else {
            throw RelayAPIError.invalidHTTPResponse
        }

        switch httpResponse.statusCode {
        case 200 ..< 300:
            return try RelayCoding.decoder.decode(Response.self, from: data)
        case 401:
            throw RelayAPIError.unauthorized
        default:
            if let message = parseServerMessage(from: data) {
                throw RelayAPIError.http(statusCode: httpResponse.statusCode, message: message)
            }
            throw RelayAPIError.http(
                statusCode: httpResponse.statusCode,
                message: HTTPURLResponse.localizedString(forStatusCode: httpResponse.statusCode)
            )
        }
    }

    private func makeURL(for endpoint: Endpoint) throws -> URL {
        try normalizedBaseURL().appendingPathComponent(String(endpoint.rawValue.dropFirst()))
    }

    private func normalizedBaseURL() throws -> URL {
        let rawBaseURL = baseURLProvider().trimmingCharacters(in: .whitespacesAndNewlines)
        let normalizedBaseURL = Self.normalizedBaseURLString(from: rawBaseURL)

        guard let url = URL(string: normalizedBaseURL),
              let scheme = url.scheme?.lowercased(),
              scheme == "https" else {
            throw RelayAPIError.invalidBaseURL(rawBaseURL)
        }

        return url
    }

    static func currentBaseURLString() -> String {
        if let env = ProcessInfo.processInfo.environment["MAHO_RELAY_URL"]?
            .trimmingCharacters(in: .whitespacesAndNewlines), !env.isEmpty {
            return normalizedBaseURLString(from: env)
        }
        return defaultBaseURLString
    }

    static func normalizedBaseURLString(from rawValue: String) -> String {
        let trimmed = rawValue.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else { return defaultBaseURLString }

        if var components = URLComponents(string: trimmed) {
            switch components.scheme?.lowercased() {
            case "ws":
                components.scheme = "http"
            case "wss":
                components.scheme = "https"
            default:
                break
            }

            if let normalized = components.url?.absoluteString {
                return normalized
            }
        }

        if trimmed.lowercased().hasPrefix("ws://") {
            return "http://" + trimmed.dropFirst("ws://".count)
        }

        if trimmed.lowercased().hasPrefix("wss://") {
            return "https://" + trimmed.dropFirst("wss://".count)
        }

        return trimmed
    }

    private func parseServerMessage(from data: Data) -> String? {
        guard let json = try? JSONSerialization.jsonObject(with: data) as? [String: Any] else {
            return String(data: data, encoding: .utf8)?.trimmingCharacters(in: .whitespacesAndNewlines)
        }

        if let message = json["message"] as? String, !message.isEmpty {
            return message
        }

        if let error = json["error"] as? String, !error.isEmpty {
            return error
        }

        if let details = json["details"] as? String, !details.isEmpty {
            return details
        }

        return nil
    }
}
