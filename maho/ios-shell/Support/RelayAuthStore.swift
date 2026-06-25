import Foundation
import Security

struct RelayAccount: Codable, Equatable {
    let id: String?
    let email: String
    let displayName: String?

    init(id: String? = nil, email: String, displayName: String?) {
        self.id = id
        self.email = email
        self.displayName = displayName
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
}

struct RelaySessionInfo: Codable, Equatable {
    let id: String?

    init(id: String? = nil) {
        self.id = id
    }
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
}

protocol RelayAuthStoreProtocol: AnyObject {
    func loadSession() -> RelayAuthSession?
    func saveSession(_ session: RelayAuthSession) throws
    func clearSession() throws
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
              let session = try? MahoJSON.decoder.decode(RelayAuthSession.self, from: data) else {
            return nil
        }
        return session
    }

    func saveSession(_ session: RelayAuthSession) throws {
        guard let data = try? MahoJSON.encoder.encode(session) else {
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
