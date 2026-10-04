import Foundation
import Security

enum RelayIdentityMetadataState: Equatable {
    case absent
    case validGoogle
    case invalid
}

struct RelayAccount: Codable, Equatable {
    let id: String?
    let email: String
    let displayName: String?
    let oauthProvider: String?
    let oauthProviderSub: String?
    let identityMetadataState: RelayIdentityMetadataState

    init(
        id: String? = nil,
        email: String,
        displayName: String?,
        oauthProvider: String? = nil,
        oauthProviderSub: String? = nil
    ) {
        self.id = id
        self.email = email
        self.displayName = displayName
        let provider = oauthProvider?.trimmingCharacters(in: .whitespacesAndNewlines)
        let providerSub = oauthProviderSub?.trimmingCharacters(in: .whitespacesAndNewlines)
        if oauthProvider == nil && oauthProviderSub == nil {
            self.oauthProvider = nil
            self.oauthProviderSub = nil
            identityMetadataState = .absent
        } else if provider == "google", let providerSub, !providerSub.isEmpty {
            self.oauthProvider = provider
            self.oauthProviderSub = providerSub
            identityMetadataState = .validGoogle
        } else {
            self.oauthProvider = nil
            self.oauthProviderSub = nil
            identityMetadataState = .invalid
        }
    }

    private enum CodingKeys: String, CodingKey {
        case id
        case email
        case displayName
        case oauthProvider
        case oauthProviderSub
    }

    init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        id = try container.decodeIfPresent(RelayID.self, forKey: .id)?.value
        email = try container.decode(String.self, forKey: .email)
        displayName = try container.decodeIfPresent(String.self, forKey: .displayName)
        let providerPresent = container.contains(.oauthProvider)
        let providerSubPresent = container.contains(.oauthProviderSub)
        let provider = try container.decodeIfPresent(String.self, forKey: .oauthProvider)
        let providerSub = try container.decodeIfPresent(String.self, forKey: .oauthProviderSub)
        let normalizedProvider = provider?.trimmingCharacters(in: .whitespacesAndNewlines)
        let normalizedProviderSub = providerSub?.trimmingCharacters(in: .whitespacesAndNewlines)
        if !providerPresent && !providerSubPresent {
            oauthProvider = nil
            oauthProviderSub = nil
            identityMetadataState = .absent
        } else if normalizedProvider == "google",
                  let normalizedProviderSub, !normalizedProviderSub.isEmpty {
            oauthProvider = normalizedProvider
            oauthProviderSub = normalizedProviderSub
            identityMetadataState = .validGoogle
        } else {
            oauthProvider = nil
            oauthProviderSub = nil
            identityMetadataState = .invalid
        }
    }

    func encode(to encoder: Encoder) throws {
        var container = encoder.container(keyedBy: CodingKeys.self)
        try container.encodeIfPresent(id.map(RelayID.init), forKey: .id)
        try container.encode(email, forKey: .email)
        try container.encodeIfPresent(displayName, forKey: .displayName)
        try container.encodeIfPresent(oauthProvider, forKey: .oauthProvider)
        try container.encodeIfPresent(oauthProviderSub, forKey: .oauthProviderSub)
    }
}

struct RelayDevice: Codable, Equatable {
    let id: String?
    let name: String?
    let deviceType: String?

    init(id: String? = nil, name: String? = nil, deviceType: String? = nil) {
        self.id = id
        self.name = name
        self.deviceType = deviceType
    }

    private enum CodingKeys: String, CodingKey {
        case id
        case name
        case deviceType
    }

    init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        id = try container.decodeIfPresent(RelayID.self, forKey: .id)?.value
        name = try container.decodeIfPresent(String.self, forKey: .name)
        deviceType = try container.decodeIfPresent(String.self, forKey: .deviceType)
    }

    func encode(to encoder: Encoder) throws {
        var container = encoder.container(keyedBy: CodingKeys.self)
        try container.encodeIfPresent(id.map(RelayID.init), forKey: .id)
        try container.encodeIfPresent(name, forKey: .name)
        try container.encodeIfPresent(deviceType, forKey: .deviceType)
    }
}

struct RelaySessionInfo: Codable, Equatable {
    let id: String?

    init(id: String? = nil) {
        self.id = id
    }

    private enum CodingKeys: String, CodingKey {
        case id
    }

    init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        id = try container.decodeIfPresent(RelayID.self, forKey: .id)?.value
    }

    func encode(to encoder: Encoder) throws {
        var container = encoder.container(keyedBy: CodingKeys.self)
        try container.encodeIfPresent(id.map(RelayID.init), forKey: .id)
    }
}


enum RelayRefreshIdentityError: Error, Equatable {
    case invalidMetadata
}

struct RelayAuthSession: Codable, Equatable {
    let accessToken: String
    let refreshToken: String
    let tokenType: String
    let expiresAt: Date
    let refreshExpiresAt: Date
    let account: RelayAccount
    let device: RelayDevice?
    let session: RelaySessionInfo?

    var authorizationHeaderValue: String {
        let normalizedType = tokenType.trimmingCharacters(in: .whitespacesAndNewlines)
        if normalizedType.isEmpty {
            return "Bearer \(accessToken)"
        }
        return "\(normalizedType) \(accessToken)"
    }

    var isAccessTokenExpired: Bool {
        expiresAt <= Date()
    }

    var isRefreshTokenExpired: Bool {
        refreshExpiresAt <= Date()
    }

    func mergingRefreshResponse(_ refreshed: RelayAuthSession) throws -> RelayAuthSession {
        switch refreshed.account.identityMetadataState {
        case .validGoogle:
            return refreshed
        case .invalid:
            throw RelayRefreshIdentityError.invalidMetadata
        case .absent:
            guard account.identityMetadataState == .validGoogle else {
                return refreshed
            }
            let mergedAccount = RelayAccount(
                id: refreshed.account.id,
                email: refreshed.account.email,
                displayName: refreshed.account.displayName,
                oauthProvider: account.oauthProvider,
                oauthProviderSub: account.oauthProviderSub
            )
            return RelayAuthSession(
                accessToken: refreshed.accessToken,
                refreshToken: refreshed.refreshToken,
                tokenType: refreshed.tokenType,
                expiresAt: refreshed.expiresAt,
                refreshExpiresAt: refreshed.refreshExpiresAt,
                account: mergedAccount,
                device: refreshed.device,
                session: refreshed.session
            )
        }
    }
}

protocol RelayAuthStoreProtocol: AnyObject {
    func loadSession() -> RelayAuthSession?
    func saveSession(_ session: RelayAuthSession) throws
    func clearSession() throws
}

@MainActor
struct RelaySignOutFlow {
    private let apiClient: RelayAPIClientProtocol
    private let authStore: RelayAuthStoreProtocol

    init(apiClient: RelayAPIClientProtocol, authStore: RelayAuthStoreProtocol) {
        self.apiClient = apiClient
        self.authStore = authStore
    }

    func signOut(
        session: RelayAuthSession,
        commitLogout: () -> Void
    ) async throws {
        do {
            try await apiClient.logout(session: session)
        } catch {
            // Remote invalidation is best effort; local durable credentials must still be removed.
        }
        try authStore.clearSession()
        commitLogout()
    }
}

enum RelayAuthStoreError: LocalizedError {
    case unexpectedKeychainStatus(OSStatus)
    case encodingFailed

    var errorDescription: String? {
        switch self {
        case .unexpectedKeychainStatus(let status):
            return "Keychain error (\(status))."
        case .encodingFailed:
            return "Failed to encode relay auth session."
        }
    }
}

final class RelayAuthStore: RelayAuthStoreProtocol {
    static let shared = RelayAuthStore()

    private let service = "dev.maho.browser.relay-auth"
    private let account = "relay-session"

    private init() {}

    func loadSession() -> RelayAuthSession? {
        let query: [CFString: Any] = [
            kSecClass: kSecClassGenericPassword,
            kSecAttrService: service,
            kSecAttrAccount: account,
            kSecReturnData: true,
            kSecMatchLimit: kSecMatchLimitOne
        ]

        var result: CFTypeRef?
        let status = SecItemCopyMatching(query as CFDictionary, &result)
        guard status != errSecItemNotFound else { return nil }
        guard status == errSecSuccess,
              let data = result as? Data,
              let session = try? RelayCoding.decoder.decode(RelayAuthSession.self, from: data) else {
            return nil
        }
        return session
    }

    func saveSession(_ session: RelayAuthSession) throws {
        guard let data = try? RelayCoding.encoder.encode(session) else {
            throw RelayAuthStoreError.encodingFailed
        }

        let query: [CFString: Any] = [
            kSecClass: kSecClassGenericPassword,
            kSecAttrService: service,
            kSecAttrAccount: account
        ]
        let attributes: [CFString: Any] = [
            kSecValueData: data,
            kSecAttrAccessible: kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly
        ]

        let updateStatus = SecItemUpdate(query as CFDictionary, attributes as CFDictionary)
        if updateStatus == errSecItemNotFound {
            var createQuery = query
            createQuery[kSecValueData] = data
            createQuery[kSecAttrAccessible] = kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly
            let createStatus = SecItemAdd(createQuery as CFDictionary, nil)
            guard createStatus == errSecSuccess else {
                throw RelayAuthStoreError.unexpectedKeychainStatus(createStatus)
            }
            return
        }

        guard updateStatus == errSecSuccess else {
            throw RelayAuthStoreError.unexpectedKeychainStatus(updateStatus)
        }
    }

    func clearSession() throws {
        let query: [CFString: Any] = [
            kSecClass: kSecClassGenericPassword,
            kSecAttrService: service,
            kSecAttrAccount: account
        ]
        let status = SecItemDelete(query as CFDictionary)
        guard status == errSecSuccess || status == errSecItemNotFound else {
            throw RelayAuthStoreError.unexpectedKeychainStatus(status)
        }
    }
}
