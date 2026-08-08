import Foundation
import os.log
import Security

// MARK: - SQLCipher Keychain Helper

struct SqlCipherKeychain {
    static let service = "dev.maho.browser.storage"
    static let account = "sqlcipher-key"

    static func getData() -> Data? {
        let query: [CFString: Any] = [
            kSecClass: kSecClassGenericPassword,
            kSecAttrService: service,
            kSecAttrAccount: account,
            kSecReturnData: true,
            kSecMatchLimit: kSecMatchLimitOne
        ]
        var result: CFTypeRef?
        let status = SecItemCopyMatching(query as CFDictionary, &result)
        guard status == errSecSuccess, let data = result as? Data else { return nil }
        return data
    }

    static func set(key: String) -> Bool {
        guard let data = key.data(using: .utf8) else { return false }
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
            return createStatus == errSecSuccess
        }
        return updateStatus == errSecSuccess
    }
}

// MahoCore FFI pointer: *mut MahoCore in Rust, opaque to Swift.
// Swift sees this as OpaquePointer since cbindgen emits `typedef struct MahoCore MahoCore;`

final class MahoBridge: @unchecked Sendable {
    static let shared = MahoBridge()

    private let lock = NSLock()
    private var corePtr: OpaquePointer?
    private let log = Logger(subsystem: "dev.maho.browser", category: "Bridge")

    private init() {}

    func provisionSqlcipherKey(storagePath: String) {
        let fileManager = FileManager.default
        if !fileManager.fileExists(atPath: storagePath) {
            try? fileManager.createDirectory(atPath: storagePath, withIntermediateDirectories: true)
        }

        var url = URL(fileURLWithPath: storagePath)
        do {
            var resourceValues = URLResourceValues()
            resourceValues.isExcludedFromBackup = true
            try url.setResourceValues(resourceValues)
        } catch {
            log.error("Failed to exclude storage directory from backup: \(error)")
        }

        let dbPath = URL(fileURLWithPath: storagePath).appendingPathComponent("maho.db").path
        var dbExists = fileManager.fileExists(atPath: dbPath)

        var keyHex: String? = nil
        if let keyData = SqlCipherKeychain.getData() {
            keyHex = String(data: keyData, encoding: .utf8)
        }

        let isTesting = NSClassFromString("XCTestCase") != nil || ProcessInfo.processInfo.environment["XCTestConfigurationFilePath"] != nil

        if dbExists {
            if (keyHex == nil || keyHex!.isEmpty) && isTesting {
                let dbUrl = URL(fileURLWithPath: dbPath)
                let parentDir = dbUrl.deletingLastPathComponent()
                if let enumerator = fileManager.enumerator(at: parentDir, includingPropertiesForKeys: nil) {
                    for case let fileUrl as URL in enumerator {
                        if fileUrl.lastPathComponent.hasPrefix("maho.db") {
                            try? fileManager.removeItem(at: fileUrl)
                        }
                    }
                }
                dbExists = false
            }
        }

        if dbExists {
            guard let key = keyHex, !key.isEmpty else {
                fatalError("Critical: maho.db exists but SQLCipher key is missing or failed to read from Keychain. Aborting to prevent data corruption.")
            }
        } else {
            if keyHex == nil || keyHex!.isEmpty {
                var bytes = [UInt8](repeating: 0, count: 32)
                let status = SecRandomCopyBytes(kSecRandomDefault, bytes.count, &bytes)
                guard status == errSecSuccess else {
                    fatalError("Failed to generate random bytes for SQLCipher key")
                }
                let newKeyHex = bytes.map { String(format: "%02x", $0) }.joined()
                guard SqlCipherKeychain.set(key: newKeyHex) else {
                    fatalError("Failed to store generated SQLCipher key in Keychain")
                }
                keyHex = newKeyHex
            }
        }

        guard let key = keyHex else {
            fatalError("SQLCipher key is unavailable")
        }

        let success = key.withCString { cStr in
            maho_storage_set_sqlcipher_key(cStr)
        }
        if !success {
            fatalError("Failed to set SQLCipher key in storage driver")
        }
    }

    var isInitialized: Bool {
        lock.lock()
        defer { lock.unlock() }
        return corePtr != nil
    }

    @discardableResult
    func initialize(storagePath: String) -> Bool {
        lock.lock()
        defer { lock.unlock() }

        guard corePtr == nil else {
            log.warning("MahoBridge.initialize called but core already exists")
            return false
        }

        let ptr = storagePath.withCString { path in
            maho_core_new_with_storage(path)
        }

        guard let ptr else {
            log.error("maho_core_new_with_storage returned NULL")
            return false
        }

        corePtr = ptr
        log.info("MahoCore initialized at \(storagePath, privacy: .public)")
        return true
    }

    @discardableResult
    func initialize() -> Bool {
        lock.lock()
        defer { lock.unlock() }

        guard corePtr == nil else {
            log.warning("MahoBridge.initialize called but core already exists")
            return false
        }

        let ptr = maho_core_new()
        guard let ptr else {
            log.error("maho_core_new returned NULL")
            return false
        }

        corePtr = ptr
        log.info("MahoCore initialized (in-memory)")
        return true
    }

    func destroy() {
        lock.lock()
        defer { lock.unlock() }

        guard let ptr = corePtr else { return }
        maho_core_free(ptr)
        corePtr = nil
        log.info("MahoCore destroyed")
    }

    func withCore<R>(_ body: (OpaquePointer) -> R) -> R? {
        lock.lock()
        defer { lock.unlock() }
        guard let ptr = corePtr else {
            log.warning("withCore called but core is not initialized")
            return nil
        }
        return body(ptr)
    }

    func getStateJson() -> String? {
        withCore { ptr in
            FFIString.consume(maho_core_get_tab_view_models(ptr))
        } ?? nil
    }

    func tickJson() -> String? {
        withCore { ptr in
            FFIString.consume(maho_core_tick(ptr))
        } ?? nil
    }

    func sendEvent(name: String, payload: [String: Any] = [:]) {
        var dict = payload
        dict["type"] = name
        guard let jsonData = try? JSONSerialization.data(withJSONObject: dict),
              let jsonString = String(data: jsonData, encoding: .utf8) else { return }
        _ = withCore { ptr in
            jsonString.withCString { cStr in
                FFIString.consume(maho_core_handle_event(ptr, cStr))
            }
        }
    }

    deinit {
        if let ptr = corePtr {
            maho_core_free(ptr)
        }
    }
}
