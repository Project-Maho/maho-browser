import Foundation
import CryptoKit
import IOKit

/// Encrypted local file store that replaces macOS Keychain for secret storage.
/// Uses AES-256-GCM with a device-bound key derived via HKDF-SHA256.
final class SecureLocalStore {
    static let shared = SecureLocalStore()

    private let storeURL: URL
    private let encryptionKey: SymmetricKey
    private let queue = DispatchQueue(label: "com.maho.secure-store", qos: .userInitiated)

    private init() {
        let appSupport = FileManager.default.urls(
            for: .applicationSupportDirectory,
            in: .userDomainMask
        ).first!
        let mahoDir = appSupport.appendingPathComponent("Maho", isDirectory: true)
        try? FileManager.default.createDirectory(at: mahoDir, withIntermediateDirectories: true)
        storeURL = mahoDir.appendingPathComponent("secure_store.enc")
        encryptionKey = SecureLocalStore.deriveKey()
    }

    // MARK: - Public API

    @discardableResult
    func save(value: String, for key: String) -> Bool {
        queue.sync {
            var store = loadStore()
            store[key] = value
            return persistStore(store)
        }
    }

    func get(for key: String) -> String? {
        queue.sync {
            let store = loadStore()
            return store[key]
        }
    }

    @discardableResult
    func delete(for key: String) -> Bool {
        queue.sync {
            var store = loadStore()
            guard store.removeValue(forKey: key) != nil else { return true }
            return persistStore(store)
        }
    }

    func has(key: String) -> Bool {
        queue.sync {
            let store = loadStore()
            return store[key] != nil
        }
    }

    // MARK: - Encryption

    private static func deriveKey() -> SymmetricKey {
        let uuid = hardwareUUID() ?? "maho-fallback-\(NSUserName())"
        let inputKey = SymmetricKey(data: Data(uuid.utf8))
        return HKDF<SHA256>.deriveKey(
            inputKeyMaterial: inputKey,
            salt: Data("com.maho.secure-store.salt".utf8),
            info: Data("com.maho.secure-store.encryption".utf8),
            outputByteCount: 32
        )
    }

    private static func hardwareUUID() -> String? {
        let service = IOServiceGetMatchingService(
            kIOMainPortDefault,
            IOServiceMatching("IOPlatformExpertDevice")
        )
        guard service != IO_OBJECT_NULL else { return nil }
        defer { IOObjectRelease(service) }
        guard let property = IORegistryEntryCreateCFProperty(
            service,
            "IOPlatformUUID" as CFString,
            kCFAllocatorDefault,
            0
        ) else { return nil }
        return property.takeRetainedValue() as? String
    }

    // MARK: - Store I/O

    private func loadStore() -> [String: String] {
        guard FileManager.default.fileExists(atPath: storeURL.path) else { return [:] }
        guard let fileData = try? Data(contentsOf: storeURL),
              fileData.count > 0 else { return [:] }
        do {
            let box = try AES.GCM.SealedBox(combined: fileData)
            let decrypted = try AES.GCM.open(box, using: encryptionKey)
            let store = try JSONDecoder().decode([String: String].self, from: decrypted)
            return store
        } catch {
            return [:]
        }
    }

    private func persistStore(_ store: [String: String]) -> Bool {
        do {
            let json = try JSONEncoder().encode(store)
            let sealed = try AES.GCM.seal(json, using: encryptionKey)
            guard let combined = sealed.combined else { return false }
            try combined.write(to: storeURL, options: [.atomic, .completeFileProtection])
            return true
        } catch {
            return false
        }
    }
}
