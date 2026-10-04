import AuthenticationServices
import CryptoKit
import Foundation

/// Google Sign-In (identity only) for iOS: PKCE authorization in an
/// `ASWebAuthenticationSession`, code exchange against Google, then the relay
/// exchange for a Maho session.
///
/// Scope is `openid email profile` — no Gmail scope. Mailbox access is a
/// separate consent, because a restricted scope forces the whole client through
/// Google app verification and CASA review before anyone can sign in.
enum GoogleSignInError: LocalizedError, Equatable {
    case missingClientID
    case invalidAuthorizationURL
    case stateMismatch
    case denied(String)
    case cancelled
    case missingCode
    case missingIDToken
    case unableToStart
    case randomGenerationFailed(OSStatus)
    case tokenExchangeFailed(Int)

    var errorDescription: String? {
        switch self {
        case .missingClientID:
            return "Google sign-in is not configured."
        case .invalidAuthorizationURL:
            return "Could not build the Google sign-in URL."
        case .stateMismatch:
            return "Google sign-in state mismatch (possible CSRF)."
        case .denied(let reason):
            return "Google sign-in denied: \(reason)"
        case .cancelled:
            return "Google sign-in was cancelled."
        case .missingCode:
            return "Google did not return an authorization code."
        case .missingIDToken:
            return "Google did not return an id_token."
        case .unableToStart:
            return "Could not start Google sign-in."
        case .randomGenerationFailed(let status):
            return "Could not securely generate Google sign-in values (Security error \(status))."
        case .tokenExchangeFailed(let status):
            return "Google token exchange failed (HTTP \(status))."
        }
    }

    static func isCancellation(_ error: Error) -> Bool {
        if (error as? GoogleSignInError) == .cancelled {
            return true
        }
        guard let authenticationError = error as? ASWebAuthenticationSessionError else {
            return false
        }
        return authenticationError.code == .canceledLogin
    }
}

struct GoogleIdentityToken: Equatable {
    let idToken: String
    let nonce: String
}

enum GooglePKCE {
    static func verifier() throws -> String {
        var bytes = [UInt8](repeating: 0, count: 32)
        let status = SecRandomCopyBytes(kSecRandomDefault, bytes.count, &bytes)
        guard status == errSecSuccess else {
            throw GoogleSignInError.randomGenerationFailed(status)
        }
        return base64URL(Data(bytes))
    }

    static func challenge(for verifier: String) -> String {
        let digest = SHA256.hash(data: Data(verifier.utf8))
        return base64URL(Data(digest))
    }

    static func base64URL(_ data: Data) -> String {
        data.base64EncodedString()
            .replacingOccurrences(of: "+", with: "-")
            .replacingOccurrences(of: "/", with: "_")
            .replacingOccurrences(of: "=", with: "")
    }
}

struct GoogleSignInConfiguration: Equatable {
    static let authorizationEndpoint = URL(string: "https://accounts.google.com/o/oauth2/v2/auth")!
    static let tokenEndpoint = URL(string: "https://oauth2.googleapis.com/token")!
    static let scopes = "openid email profile"

    let clientID: String
    /// Reversed-DNS client id, per Google's iOS OAuth requirements.
    let redirectScheme: String

    var redirectURI: String { "\(redirectScheme):/oauth2redirect" }

    static func fromBundle(_ bundle: Bundle = .main) -> GoogleSignInConfiguration? {
        guard let clientID = bundle.object(forInfoDictionaryKey: "MahoGoogleClientID") as? String,
              !clientID.isEmpty,
              let scheme = bundle.object(forInfoDictionaryKey: "MahoGoogleRedirectScheme") as? String,
              !scheme.isEmpty else {
            return nil
        }
        return GoogleSignInConfiguration(clientID: clientID, redirectScheme: scheme)
    }
}

enum GoogleAuthorizationURLBuilder {
    static func build(
        configuration: GoogleSignInConfiguration,
        state: String,
        nonce: String,
        challenge: String
    ) -> URL? {
        var components = URLComponents(
            url: GoogleSignInConfiguration.authorizationEndpoint,
            resolvingAgainstBaseURL: false
        )
        components?.queryItems = [
            URLQueryItem(name: "client_id", value: configuration.clientID),
            URLQueryItem(name: "redirect_uri", value: configuration.redirectURI),
            URLQueryItem(name: "response_type", value: "code"),
            URLQueryItem(name: "scope", value: GoogleSignInConfiguration.scopes),
            URLQueryItem(name: "state", value: state),
            URLQueryItem(name: "nonce", value: nonce),
            URLQueryItem(name: "code_challenge", value: challenge),
            URLQueryItem(name: "code_challenge_method", value: "S256"),
            URLQueryItem(name: "prompt", value: "select_account"),
        ]
        return components?.url
    }

    static func value(_ name: String, in callback: URL) -> String? {
        URLComponents(url: callback, resolvingAgainstBaseURL: false)?
            .queryItems?
            .first { $0.name == name }?
            .value
    }

    static func code(from callback: URL, expectedState: String) throws -> String {
        guard let state = value("state", in: callback), state == expectedState else {
            throw GoogleSignInError.stateMismatch
        }
        if let error = value("error", in: callback) {
            throw GoogleSignInError.denied(error)
        }
        guard let code = value("code", in: callback) else {
            throw GoogleSignInError.missingCode
        }
        return code
    }
}

@MainActor
protocol GoogleSignInCoordinating: AnyObject {
    func signIn() async throws -> GoogleIdentityToken
}

@MainActor
final class GoogleSignInCoordinator: NSObject, GoogleSignInCoordinating {
    private let configuration: GoogleSignInConfiguration
    private let session: URLSession
    private let randomVerifier: () throws -> String
    private var webSession: ASWebAuthenticationSession?

    init(
        configuration: GoogleSignInConfiguration,
        session: URLSession = .shared,
        randomVerifier: @escaping () throws -> String = GooglePKCE.verifier
    ) {
        self.configuration = configuration
        self.session = session
        self.randomVerifier = randomVerifier
    }

    func signIn() async throws -> GoogleIdentityToken {
        let verifier = try randomVerifier()
        let state = try randomVerifier()
        let nonce = try randomVerifier()

        guard let authURL = GoogleAuthorizationURLBuilder.build(
            configuration: configuration,
            state: state,
            nonce: nonce,
            challenge: GooglePKCE.challenge(for: verifier)
        ) else {
            throw GoogleSignInError.invalidAuthorizationURL
        }

        let callback = try await presentWebSession(authURL: authURL)
        let code = try GoogleAuthorizationURLBuilder.code(from: callback, expectedState: state)
        let idToken = try await exchange(code: code, verifier: verifier)
        return GoogleIdentityToken(idToken: idToken, nonce: nonce)
    }

    private func presentWebSession(authURL: URL) async throws -> URL {
        try await withCheckedThrowingContinuation { continuation in
            let webSession = ASWebAuthenticationSession(
                url: authURL,
                callbackURLScheme: configuration.redirectScheme
            ) { [weak self] callbackURL, error in
                self?.webSession = nil
                if let error {
                    if GoogleSignInError.isCancellation(error) {
                        continuation.resume(throwing: GoogleSignInError.cancelled)
                    } else {
                        continuation.resume(throwing: error)
                    }
                    return
                }
                guard let callbackURL else {
                    continuation.resume(throwing: GoogleSignInError.missingCode)
                    return
                }
                continuation.resume(returning: callbackURL)
            }
            webSession.presentationContextProvider = self
            webSession.prefersEphemeralWebBrowserSession = false
            self.webSession = webSession
            guard webSession.start() else {
                self.webSession = nil
                continuation.resume(throwing: GoogleSignInError.unableToStart)
                return
            }
        }
    }

    private func exchange(code: String, verifier: String) async throws -> String {
        var request = URLRequest(url: GoogleSignInConfiguration.tokenEndpoint)
        request.httpMethod = "POST"
        request.setValue("application/x-www-form-urlencoded", forHTTPHeaderField: "Content-Type")

        var form = URLComponents()
        form.queryItems = [
            URLQueryItem(name: "client_id", value: configuration.clientID),
            URLQueryItem(name: "code", value: code),
            URLQueryItem(name: "code_verifier", value: verifier),
            URLQueryItem(name: "grant_type", value: "authorization_code"),
            URLQueryItem(name: "redirect_uri", value: configuration.redirectURI),
        ]
        request.httpBody = form.query?.data(using: .utf8)

        let (data, response) = try await session.data(for: request)
        let status = (response as? HTTPURLResponse)?.statusCode ?? 0
        guard (200 ..< 300).contains(status) else {
            throw GoogleSignInError.tokenExchangeFailed(status)
        }

        struct TokenResponse: Decodable {
            let idToken: String?

            enum CodingKeys: String, CodingKey {
                case idToken = "id_token"
            }
        }
        let decoded = try JSONDecoder().decode(TokenResponse.self, from: data)
        guard let idToken = decoded.idToken, !idToken.isEmpty else {
            throw GoogleSignInError.missingIDToken
        }
        return idToken
    }
}

@MainActor
struct GoogleRelaySignInFlow {
    private let coordinatorProvider: () throws -> any GoogleSignInCoordinating
    private let apiClient: RelayAPIClientProtocol
    private let authStore: RelayAuthStoreProtocol
    private let persistSession: Bool

    init(
        coordinatorProvider: @escaping () throws -> any GoogleSignInCoordinating,
        apiClient: RelayAPIClientProtocol,
        authStore: RelayAuthStoreProtocol,
        persistSession: Bool = true
    ) {
        self.coordinatorProvider = coordinatorProvider
        self.apiClient = apiClient
        self.authStore = authStore
        self.persistSession = persistSession
    }

    func signIn() async throws -> RelayAuthSession {
        let identity = try await coordinatorProvider().signIn()
        let relaySession = try await apiClient.authenticateWithGoogle(
            idToken: identity.idToken,
            nonce: identity.nonce
        )
        if persistSession {
            try authStore.saveSession(relaySession)
        }
        return relaySession
    }
}

extension GoogleSignInCoordinator: ASWebAuthenticationPresentationContextProviding {
    func presentationAnchor(for session: ASWebAuthenticationSession) -> ASPresentationAnchor {
        #if canImport(UIKit)
        return UIApplication.shared.connectedScenes
            .compactMap { $0 as? UIWindowScene }
            .flatMap { $0.windows }
            .first { $0.isKeyWindow } ?? ASPresentationAnchor()
        #else
        return ASPresentationAnchor()
        #endif
    }
}

#if canImport(UIKit)
import UIKit
#endif
