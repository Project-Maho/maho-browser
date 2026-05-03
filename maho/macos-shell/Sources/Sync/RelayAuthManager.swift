import Foundation

struct RelayAuthSession: Codable, Equatable {
    let serverURL: String
    let email: String
    let displayName: String?
    let accessToken: String
    let refreshToken: String
    let userID: String?
    let deviceID: String?
}

struct RelayPulledMessage: Equatable {
    let seq: Int
    let blob: String
}

struct RelayPullEnvelope: Equatable {
    let messages: [RelayPulledMessage]
    let hasMore: Bool
}

enum RelayAuthAction {
    case signUp
    case logIn

    var buttonTitle: String {
        switch self {
        case .signUp:
            return "Sign Up"
        case .logIn:
            return "Log In"
        }
    }

    fileprivate var endpointVariants: [String] {
        switch self {
        case .signUp:
            return ["auth/signup", "signup"]
        case .logIn:
            return ["auth/login", "login"]
        }
    }
}

enum RelayAuthError: LocalizedError {
    case invalidServerURL
    case missingCredentials
    case notAuthenticated
    case invalidResponse
    case serverError(statusCode: Int, message: String)
    case tokenPersistenceFailed

    var errorDescription: String? {
        switch self {
        case .invalidServerURL:
            return "Enter a valid relay server URL."
        case .missingCredentials:
            return "Enter both an email address and password."
        case .notAuthenticated:
            return "Log in to the relay before starting sync."
        case .invalidResponse:
            return "The relay returned an unexpected response."
        case .serverError(let statusCode, let message):
            if message.isEmpty {
                return "Relay request failed with status \(statusCode)."
            }
            return message
        case .tokenPersistenceFailed:
            return "Signed in, but failed to securely persist relay tokens."
        }
    }
}

enum RelayServerLocator {
    static func normalizedBaseURL(from rawValue: String) -> URL? {
        let trimmed = rawValue.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty, var components = URLComponents(string: trimmed) else {
            return nil
        }

        switch components.scheme?.lowercased() {
        case "ws":
            components.scheme = "http"
        case "wss":
            components.scheme = "https"
        case "http", "https":
            break
        default:
            return nil
        }

        guard let host = components.host, !host.isEmpty else {
            return nil
        }

        let normalizedPath = components.path == "/" ? "" : components.path.trimmingCharacters(in: CharacterSet(charactersIn: "/"))
        components.path = normalizedPath.isEmpty ? "" : "/\(normalizedPath)"
        components.query = nil
        components.fragment = nil
        return components.url
    }

    static func endpointCandidates(baseURL: URL, pathVariants: [String]) -> [URL] {
        var urls: [URL] = []
        var seen = Set<String>()

        for variant in pathVariants {
            let trimmed = variant.trimmingCharacters(in: CharacterSet(charactersIn: "/"))
            guard !trimmed.isEmpty else { continue }
            let url = baseURL.appendingPathComponent(trimmed)
            let key = url.absoluteString
            if seen.insert(key).inserted {
                urls.append(url)
            }
        }

        return urls
    }
}

enum RelayPayloadCodec {
    static func decodeJSONObject(from jsonString: String) -> Any? {
        guard let data = jsonString.data(using: .utf8) else { return nil }
        return try? JSONSerialization.jsonObject(with: data)
    }

    static func authSession(
        from data: Data,
        baseURL: URL,
        fallbackEmail: String,
        fallbackDisplayName: String?,
        fallbackRefreshToken: String? = nil
    ) -> RelayAuthSession? {
        guard let root = jsonObject(from: data) else { return nil }
        let payload = nestedDictionary(in: root, keys: ["data", "session"]) ?? root

        guard let accessToken = string(in: payload, keys: ["access_token", "accessToken", "token"]),
              !accessToken.isEmpty else {
            return nil
        }

        let refreshToken = string(in: payload, keys: ["refresh_token", "refreshToken"])
            ?? fallbackRefreshToken
        guard let refreshToken, !refreshToken.isEmpty else { return nil }

        let user = nestedDictionary(in: payload, keys: ["user", "account"])
        let session = nestedDictionary(in: payload, keys: ["session"])
        let device = nestedDictionary(in: payload, keys: ["device"])
        let email = string(in: user, keys: ["email"])
            ?? string(in: payload, keys: ["email"])
            ?? fallbackEmail
        let displayName = string(in: user, keys: ["display_name", "displayName", "name"])
            ?? string(in: payload, keys: ["display_name", "displayName", "name"])
            ?? fallbackDisplayName
        let userID = string(in: user, keys: ["id", "user_id", "userId"])
            ?? string(in: session, keys: ["id", "user_id", "userId"])
            ?? string(in: payload, keys: ["user_id", "userId"])
        let deviceID = string(in: device, keys: ["id", "device_id", "deviceId"])
            ?? string(in: payload, keys: ["device_id", "deviceId"])

        return RelayAuthSession(
            serverURL: baseURL.absoluteString,
            email: email,
            displayName: displayName,
            accessToken: accessToken,
            refreshToken: refreshToken,
            userID: userID,
            deviceID: deviceID
        )
    }

    static func pushBlobs(from outgoingJSONString: String) -> [String] {
        let trimmed = outgoingJSONString.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty, trimmed != "[]" else { return [] }

        if let array = decodeJSONObject(from: trimmed) as? [Any], !array.isEmpty {
            let encoded = array.compactMap { jsonString(from: $0) }.map { Data($0.utf8).base64EncodedString() }
            if !encoded.isEmpty {
                return encoded
            }
        }

        return [Data(trimmed.utf8).base64EncodedString()]
    }

    static func pullEnvelope(from data: Data) -> RelayPullEnvelope? {
        guard let root = jsonObject(from: data) else { return nil }
        let rawMessages = (root["messages"] as? [[String: Any]]) ?? []
        let messages = rawMessages.compactMap { messageDictionary -> RelayPulledMessage? in
            guard let seq = int(in: messageDictionary, keys: ["seq"]),
                  let blob = string(in: messageDictionary, keys: ["blob"]) else {
                return nil
            }
            return RelayPulledMessage(seq: seq, blob: blob)
        }
        let hasMore = root["has_more"] as? Bool ?? false
        return RelayPullEnvelope(messages: messages, hasMore: hasMore)
    }

    static func decodedBlobString(from base64Blob: String) -> String? {
        guard let blobData = Data(base64Encoded: base64Blob),
              let decoded = String(data: blobData, encoding: .utf8),
              !decoded.isEmpty else {
            return nil
        }
        return decoded
    }

    static func errorMessage(from data: Data) -> String? {
        guard let root = jsonObject(from: data) else { return nil }
        return string(in: root, keys: ["message", "error", "detail", "msg"])
            ?? string(in: nestedDictionary(in: root, keys: ["error"]), keys: ["message", "detail"])
    }

    private static func jsonObject(from data: Data) -> [String: Any]? {
        (try? JSONSerialization.jsonObject(with: data)) as? [String: Any]
    }

    private static func nestedDictionary(in root: [String: Any]?, keys: [String]) -> [String: Any]? {
        guard let root else { return nil }
        for key in keys {
            if let nested = root[key] as? [String: Any] {
                return nested
            }
        }
        return nil
    }

    private static func string(in root: [String: Any]?, keys: [String]) -> String? {
        guard let root else { return nil }
        for key in keys {
            if let value = root[key] as? String, !value.isEmpty {
                return value
            }
        }
        return nil
    }

    private static func int(in root: [String: Any]?, keys: [String]) -> Int? {
        guard let root else { return nil }
        for key in keys {
            if let value = root[key] as? Int {
                return value
            }
            if let value = root[key] as? NSNumber {
                return value.intValue
            }
        }
        return nil
    }

    private static func jsonString(from object: Any) -> String? {
        guard JSONSerialization.isValidJSONObject(object),
              let data = try? JSONSerialization.data(withJSONObject: object),
              let string = String(data: data, encoding: .utf8) else {
            return nil
        }
        return string
    }
}

final class RelayAuthStore {
    static let shared = RelayAuthStore()

    private let storageKey = "relay.auth.session"
    private let store = SecureLocalStore.shared

    private init() {}

    func loadSession() -> RelayAuthSession? {
        guard let json = store.get(for: storageKey),
              let data = json.data(using: .utf8) else {
            return nil
        }
        return try? JSONDecoder().decode(RelayAuthSession.self, from: data)
    }

    @discardableResult
    func save(session: RelayAuthSession) -> Bool {
        guard let data = try? JSONEncoder().encode(session),
              let json = String(data: data, encoding: .utf8) else {
            return false
        }
        return store.save(value: json, for: storageKey)
    }

    @discardableResult
    func clearSession() -> Bool {
        store.delete(for: storageKey)
    }
}

final class RelayAuthManager {
    static let shared = RelayAuthManager()
    static let authStateChangedNotification = Notification.Name("relayAuthStateChanged")

    private let urlSession: URLSession
    private let store: RelayAuthStore
    private(set) var currentSession: RelayAuthSession?

    init(urlSession: URLSession = .shared, store: RelayAuthStore = .shared) {
        self.urlSession = urlSession
        self.store = store
    }

    func start() {
        currentSession = store.loadSession()
        syncCoreAuthState()
        postAuthStateChanged()
    }

    func hasSession(for serverURL: String) -> Bool {
        session(for: serverURL) != nil
    }

    func session(for serverURL: String) -> RelayAuthSession? {
        guard let session = currentSession,
              let normalized = RelayServerLocator.normalizedBaseURL(from: serverURL)?.absoluteString,
              session.serverURL == normalized else {
            return nil
        }
        return session
    }

    func authenticate(action: RelayAuthAction, serverURL: String, email: String, password: String) async throws -> RelayAuthSession {
        let trimmedEmail = email.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmedEmail.isEmpty, !password.isEmpty else {
            throw RelayAuthError.missingCredentials
        }
        guard let baseURL = RelayServerLocator.normalizedBaseURL(from: serverURL) else {
            throw RelayAuthError.invalidServerURL
        }

        let payload: [String: Any] = [
            "email": trimmedEmail,
            "password": password,
        ]
        let body = try JSONSerialization.data(withJSONObject: payload)
        let (data, _) = try await performJSONRequest(
            candidates: RelayServerLocator.endpointCandidates(baseURL: baseURL, pathVariants: action.endpointVariants),
            method: "POST",
            body: body,
            bearerToken: nil,
            allowRefresh: false,
            baseURL: baseURL
        )

        guard let session = RelayPayloadCodec.authSession(
            from: data,
            baseURL: baseURL,
            fallbackEmail: trimmedEmail,
            fallbackDisplayName: nil
        ) else {
            throw RelayAuthError.invalidResponse
        }

        guard store.save(session: session) else {
            throw RelayAuthError.tokenPersistenceFailed
        }

        currentSession = session
        syncCoreAuthState()
        postAuthStateChanged()
        return session
    }

    func signOut(serverURL: String?) async {
        if let activeSession = currentSession,
           let baseURL = serverURL.flatMap(RelayServerLocator.normalizedBaseURL(from:)) ?? URL(string: activeSession.serverURL) {
            let payload = try? JSONSerialization.data(withJSONObject: ["refresh_token": activeSession.refreshToken])
            _ = try? await performJSONRequest(
                candidates: RelayServerLocator.endpointCandidates(baseURL: baseURL, pathVariants: ["auth/logout", "logout"]),
                method: "POST",
                body: payload,
                bearerToken: activeSession.accessToken,
                allowRefresh: false,
                baseURL: baseURL
            )
        }

        currentSession = nil
        _ = store.clearSession()
        syncCoreAuthState()
        postAuthStateChanged()
    }

    func performAuthorizedJSONRequest(
        serverURL: String,
        endpointVariants: [String],
        jsonBody: [String: Any]
    ) async throws -> (Data, HTTPURLResponse) {
        guard let session = session(for: serverURL) else {
            throw RelayAuthError.notAuthenticated
        }
        guard let baseURL = URL(string: session.serverURL) else {
            throw RelayAuthError.invalidServerURL
        }

        let body = try JSONSerialization.data(withJSONObject: jsonBody)
        return try await performJSONRequest(
            candidates: RelayServerLocator.endpointCandidates(baseURL: baseURL, pathVariants: endpointVariants),
            method: "POST",
            body: body,
            bearerToken: session.accessToken,
            allowRefresh: true,
            baseURL: baseURL
        )
    }

    func performAuthorizedGETRequest(
        serverURL: String,
        endpointVariants: [String],
        queryItems: [URLQueryItem]
    ) async throws -> (Data, HTTPURLResponse) {
        guard let session = session(for: serverURL) else {
            throw RelayAuthError.notAuthenticated
        }
        guard let baseURL = URL(string: session.serverURL) else {
            throw RelayAuthError.invalidServerURL
        }

        let candidates = RelayServerLocator.endpointCandidates(baseURL: baseURL, pathVariants: endpointVariants).compactMap { candidate -> URL? in
            guard var components = URLComponents(url: candidate, resolvingAgainstBaseURL: false) else {
                return nil
            }
            components.queryItems = queryItems
            return components.url
        }

        return try await performJSONRequest(
            candidates: candidates,
            method: "GET",
            body: nil,
            bearerToken: session.accessToken,
            allowRefresh: true,
            baseURL: baseURL
        )
    }

    private func performJSONRequest(
        candidates: [URL],
        method: String,
        body: Data?,
        bearerToken: String?,
        allowRefresh: Bool,
        baseURL: URL
    ) async throws -> (Data, HTTPURLResponse) {
        var lastError: Error = RelayAuthError.invalidResponse

        for candidate in candidates {
            var request = URLRequest(url: candidate)
            request.httpMethod = method
            request.httpBody = body
            request.setValue("application/json", forHTTPHeaderField: "Accept")
            if body != nil {
                request.setValue("application/json", forHTTPHeaderField: "Content-Type")
            }
            if let bearerToken {
                request.setValue("Bearer \(bearerToken)", forHTTPHeaderField: "Authorization")
            }

            do {
                let (data, response) = try await urlSession.data(for: request)
                guard let httpResponse = response as? HTTPURLResponse else {
                    throw RelayAuthError.invalidResponse
                }

                if httpResponse.statusCode == 401 && allowRefresh {
                    let refreshed = try await refreshSession(baseURL: baseURL)
                    return try await performJSONRequest(
                        candidates: [candidate],
                        method: method,
                        body: body,
                        bearerToken: refreshed.accessToken,
                        allowRefresh: false,
                        baseURL: baseURL
                    )
                }

                if (200 ..< 300).contains(httpResponse.statusCode) {
                    return (data, httpResponse)
                }

                let message = RelayPayloadCodec.errorMessage(from: data) ?? HTTPURLResponse.localizedString(forStatusCode: httpResponse.statusCode)
                let error = RelayAuthError.serverError(statusCode: httpResponse.statusCode, message: message)
                if httpResponse.statusCode == 404 {
                    lastError = error
                    continue
                }
                throw error
            } catch {
                lastError = error
            }
        }

        throw lastError
    }

    private func refreshSession(baseURL: URL) async throws -> RelayAuthSession {
        guard let existing = currentSession, existing.serverURL == baseURL.absoluteString else {
            throw RelayAuthError.notAuthenticated
        }

        let body = try JSONSerialization.data(withJSONObject: ["refresh_token": existing.refreshToken])
        let (data, _) = try await performJSONRequest(
            candidates: RelayServerLocator.endpointCandidates(baseURL: baseURL, pathVariants: ["auth/refresh", "refresh"]),
            method: "POST",
            body: body,
            bearerToken: nil,
            allowRefresh: false,
            baseURL: baseURL
        )

        guard let session = RelayPayloadCodec.authSession(
            from: data,
            baseURL: baseURL,
            fallbackEmail: existing.email,
            fallbackDisplayName: existing.displayName,
            fallbackRefreshToken: existing.refreshToken
        ) else {
            throw RelayAuthError.invalidResponse
        }

        guard store.save(session: session) else {
            throw RelayAuthError.tokenPersistenceFailed
        }

        currentSession = session
        syncCoreAuthState()
        postAuthStateChanged()
        return session
    }

    private func syncCoreAuthState() {
        if let session = currentSession {
            CoreUpdateDispatcher.shared.sendAndDispatch(
                .signIn(
                    email: session.email,
                    displayName: session.displayName,
                    password: nil,
                    accessToken: session.accessToken,
                    userId: session.userID,
                    deviceId: session.deviceID
                )
            )
        } else {
            CoreUpdateDispatcher.shared.sendAndDispatch(.signOut)
        }
    }

    private func postAuthStateChanged() {
        NotificationCenter.default.post(
            name: Self.authStateChangedNotification,
            object: nil,
            userInfo: ["session": currentSession as Any]
        )
    }
}
