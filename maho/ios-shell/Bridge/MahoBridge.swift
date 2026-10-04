import Foundation
import os.log
import Security

// MARK: - Asset Provisioning Manifest Models

struct AssetManifest: Codable, Equatable, Sendable {
    var schemaVersion: Int
    var bundleVersion: String
    var buildTimestamp: TimeInterval
    var files: [String: AssetFileEntry]

    struct AssetFileEntry: Codable, Equatable, Sendable {
        var size: Int64
        var sha256: String?
        var modifiedTimestamp: TimeInterval?

        init(size: Int64 = 0, sha256: String? = nil, modifiedTimestamp: TimeInterval? = nil) {
            self.size = size
            self.sha256 = sha256
            self.modifiedTimestamp = modifiedTimestamp
        }
    }

    init(
        schemaVersion: Int = 1,
        bundleVersion: String,
        buildTimestamp: TimeInterval = Date().timeIntervalSince1970,
        files: [String: AssetFileEntry] = [:]
    ) {
        self.schemaVersion = schemaVersion
        self.bundleVersion = bundleVersion
        self.buildTimestamp = buildTimestamp
        self.files = files
    }
}

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

    static func delete() -> Bool {
        let query: [CFString: Any] = [
            kSecClass: kSecClassGenericPassword,
            kSecAttrService: service,
            kSecAttrAccount: account
        ]
        let status = SecItemDelete(query as CFDictionary)
        return status == errSecSuccess || status == errSecItemNotFound
    }
}

// MahoCore FFI pointer: *mut MahoCore in Rust, opaque to Swift.
// Swift sees this as OpaquePointer since cbindgen emits `typedef struct MahoCore MahoCore;`

final class MahoBridge: @unchecked Sendable {
    static let shared = MahoBridge()

    let lock = NSLock()
    var corePtr: OpaquePointer?
    // U06i: the hydration loader owns the native core privately until a full
    // initial load succeeds; destroy() during that window only marks a close
    // request and the loader reclaims the private core itself.
    var pendingCorePtr: OpaquePointer?
    private var closeRequested = false
    private(set) var startupFailed = false
    private let log = Logger(subsystem: "dev.maho.browser", category: "Bridge")

    // Internal construction permits isolated native owners in hosted tests.
    init() {}

#if DEBUG
    // Set before dispatch; nil in normal app runs. These never own a native handle.
    var memoryThreadStorageOpenEntered: (() -> Void)?
    var memoryThreadHydrationEntered: ((Bool) -> Void)?
    var memoryThreadAgentCreated: ((OpaquePointer) -> Void)?
    // Read only from the hydration observation while withCore holds lock.
    var memoryThreadCorePublished: Bool { corePtr != nil }
#endif

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

    // MARK: - Asset Provisioning & Differential Sync

    /// Inspects bundle asset manifest vs sandbox destination manifest.
    /// If identical, fast paths and returns `false` (no staging/copying performed).
    /// If changed or missing, performs differential staging in staging directory and atomically swaps into place, returning `true`.
    @discardableResult
    func provisionAssets(
        storagePath: String,
        bundle: Bundle = .main,
        force: Bool = false
    ) -> Bool {
        let fileManager = FileManager.default
        let assetsDir = URL(fileURLWithPath: storagePath).appendingPathComponent("assets", isDirectory: true)
        let manifestURL = assetsDir.appendingPathComponent("asset-manifest.json")

        let bundleManifest = resolveBundleManifest(bundle: bundle)

        // Fast path: Check versioned manifest comparison
        if !force, fileManager.fileExists(atPath: assetsDir.path), fileManager.fileExists(atPath: manifestURL.path) {
            if let storedData = try? Data(contentsOf: manifestURL),
               let storedManifest = try? JSONDecoder().decode(AssetManifest.self, from: storedData) {
                if storedManifest.bundleVersion == bundleManifest.bundleVersion,
                   storedManifest.schemaVersion == bundleManifest.schemaVersion,
                   storedManifest.files == bundleManifest.files {
                    log.debug("Asset manifest identical (v\(storedManifest.bundleVersion, privacy: .public)); skipped asset walk and sync.")
                    return false
                }
            }
        }

        // Staging path: Perform staging in background/temporary directory then atomic swap
        let parentDir = assetsDir.deletingLastPathComponent()
        if !fileManager.fileExists(atPath: parentDir.path) {
            try? fileManager.createDirectory(at: parentDir, withIntermediateDirectories: true)
        }

        let stagingDir = parentDir.appendingPathComponent("assets_staging_\(UUID().uuidString)", isDirectory: true)
        do {
            try fileManager.createDirectory(at: stagingDir, withIntermediateDirectories: true)

            // Stage bundle web-ai assets
            stageBundleAssets(bundle: bundle, to: stagingDir, existingLiveDir: assetsDir)

            // Write updated manifest to staging directory
            let manifestData = try JSONEncoder().encode(bundleManifest)
            try manifestData.write(to: stagingDir.appendingPathComponent("asset-manifest.json"), options: .atomic)

            // Atomic swap: replace live directory with staging directory
            if fileManager.fileExists(atPath: assetsDir.path) {
                let backupDir = parentDir.appendingPathComponent("assets_backup_\(UUID().uuidString)", isDirectory: true)
                try fileManager.moveItem(at: assetsDir, to: backupDir)
                do {
                    try fileManager.moveItem(at: stagingDir, to: assetsDir)
                    try? fileManager.removeItem(at: backupDir)
                } catch {
                    // Rollback if swap failed
                    try? fileManager.moveItem(at: backupDir, to: assetsDir)
                    throw error
                }
            } else {
                try fileManager.moveItem(at: stagingDir, to: assetsDir)
            }

            log.info("Asset provisioning complete (v\(bundleManifest.bundleVersion, privacy: .public)) with atomic swap.")
            return true
        } catch {
            log.error("Asset provisioning failed: \(error.localizedDescription, privacy: .public)")
            try? fileManager.removeItem(at: stagingDir)
            return false
        }
    }

    /// Asynchronously provisions assets in background queue if needed, executing fast-path check first.
    func provisionAssetsInBackground(
        storagePath: String,
        bundle: Bundle = .main,
        force: Bool = false,
        completion: (@Sendable (Result<Bool, Error>) -> Void)? = nil
    ) {
        let fileManager = FileManager.default
        let assetsDir = URL(fileURLWithPath: storagePath).appendingPathComponent("assets", isDirectory: true)
        let manifestURL = assetsDir.appendingPathComponent("asset-manifest.json")
        let bundleManifest = resolveBundleManifest(bundle: bundle)

        // Immediate fast path check
        if !force, fileManager.fileExists(atPath: assetsDir.path), fileManager.fileExists(atPath: manifestURL.path) {
            if let storedData = try? Data(contentsOf: manifestURL),
               let storedManifest = try? JSONDecoder().decode(AssetManifest.self, from: storedData),
               storedManifest.bundleVersion == bundleManifest.bundleVersion,
               storedManifest.schemaVersion == bundleManifest.schemaVersion,
               storedManifest.files == bundleManifest.files {
                completion?(.success(false))
                return
            }
        }

        DispatchQueue.global(qos: .userInitiated).async { [weak self] in
            guard let self else { return }
            let updated = self.provisionAssets(storagePath: storagePath, bundle: bundle, force: force)
            completion?(.success(updated))
        }
    }

    /// Retrieves URL for a provisioned sandbox asset if present.
    func getProvisionedAssetURL(storagePath: String, relativePath: String) -> URL? {
        let fileManager = FileManager.default
        let url = URL(fileURLWithPath: storagePath)
            .appendingPathComponent("assets", isDirectory: true)
            .appendingPathComponent(relativePath)
        return fileManager.fileExists(atPath: url.path) ? url : nil
    }

    private func resolveBundleManifest(bundle: Bundle) -> AssetManifest {
        // 1. Check for explicit asset-manifest.json in bundle
        if let manifestURL = bundle.url(forResource: "asset-manifest", withExtension: "json", subdirectory: "web-ai") ??
                              bundle.url(forResource: "asset-manifest", withExtension: "json"),
           let data = try? Data(contentsOf: manifestURL),
           let manifest = try? JSONDecoder().decode(AssetManifest.self, from: data) {
            return manifest
        }

        // 2. Derive manifest from bundle version & resource metadata
        let version = (bundle.infoDictionary?["CFBundleVersion"] as? String) ??
                      (bundle.infoDictionary?["CFBundleShortVersionString"] as? String) ??
                      "1.0.0"

        var files: [String: AssetManifest.AssetFileEntry] = [:]
        let fileManager = FileManager.default

        if let webAiPath = bundle.resourceURL?.appendingPathComponent("web-ai").path,
           fileManager.fileExists(atPath: webAiPath),
           let enumerator = fileManager.enumerator(atPath: webAiPath) {
            for case let relativePath as String in enumerator {
                let fullPath = (webAiPath as NSString).appendingPathComponent(relativePath)
                var isDir: ObjCBool = false
                if fileManager.fileExists(atPath: fullPath, isDirectory: &isDir), !isDir.boolValue {
                    let attrs = (try? fileManager.attributesOfItem(atPath: fullPath)) ?? [:]
                    let size = (attrs[.size] as? NSNumber)?.int64Value ?? 0
                    let modDate = (attrs[.modificationDate] as? Date)?.timeIntervalSince1970
                    files["web-ai/\(relativePath)"] = AssetManifest.AssetFileEntry(
                        size: size,
                        modifiedTimestamp: modDate
                    )
                }
            }
        }

        return AssetManifest(
            schemaVersion: 1,
            bundleVersion: version,
            files: files
        )
    }

    private func stageBundleAssets(bundle: Bundle, to stagingDir: URL, existingLiveDir: URL) {
        let fileManager = FileManager.default

        // Copy web-ai resources
        if let webAiURL = bundle.url(forResource: "index", withExtension: "html", subdirectory: "web-ai")?
            .deletingLastPathComponent() {
            let stagingWebAi = stagingDir.appendingPathComponent("web-ai", isDirectory: true)
            try? fileManager.createDirectory(at: stagingWebAi, withIntermediateDirectories: true)

            if let items = try? fileManager.contentsOfDirectory(at: webAiURL, includingPropertiesForKeys: [.fileSizeKey, .contentModificationDateKey]) {
                for item in items {
                    let destItem = stagingWebAi.appendingPathComponent(item.lastPathComponent)
                    let liveItem = existingLiveDir.appendingPathComponent("web-ai").appendingPathComponent(item.lastPathComponent)

                    // Differential check: if file in live directory exists and matches size/date, copy from live, else copy from bundle
                    if fileManager.fileExists(atPath: liveItem.path),
                       let liveAttrs = try? fileManager.attributesOfItem(atPath: liveItem.path),
                       let bundleAttrs = try? fileManager.attributesOfItem(atPath: item.path),
                       (liveAttrs[.size] as? NSNumber) == (bundleAttrs[.size] as? NSNumber) {
                        try? fileManager.copyItem(at: liveItem, to: destItem)
                    } else {
                        try? fileManager.copyItem(at: item, to: destItem)
                    }
                }
            }
        }
    }

    // U06i: readiness reads must never block on the initialization lock while
    // a native storage open is in flight.
    var isInitialized: Bool { corePtr != nil }

    @discardableResult
    func initialize(storagePath: String) -> Bool {
        // U06i: mirror the app launch sequence — the SQLCipher key must be
        // ready before the native storage open, so the hydration entry is
        // self-contained when driven off Main.
        provisionSqlcipherKey(storagePath: storagePath)

        lock.lock()
        defer { lock.unlock() }

        guard corePtr == nil, pendingCorePtr == nil else {
            print("PROBE_INIT rejected: corePtr=\(corePtr as Any) pending=\(pendingCorePtr as Any)")
            log.warning("MahoBridge.initialize called but core already exists")
            return false
        }

        let ptr = storagePath.withCString { path in
#if DEBUG
            memoryThreadStorageOpenEntered?()
#endif
            return maho_core_new_with_storage(path)
        }

        guard let ptr else {
            print("PROBE_INIT native NULL path=\(storagePath) closeRequested=\(closeRequested)")
            log.error("maho_core_new_with_storage returned NULL")
            startupFailed = true
            return false
        }

        if closeRequested {
            print("PROBE_INIT close-requested abort path=\(storagePath)")
            // The owner closed while the native open was in flight: reclaim the
            // private core here instead of publishing it.
            maho_core_free(ptr)
            closeRequested = false
            startupFailed = true
            log.info("MahoCore hydration aborted by close before publication")
            return false
        }

        // Keep the core private until hydration publishes it (loadState).
        pendingCorePtr = ptr
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
        // A hydration worker owns the lock while its native open is in flight;
        // defer the reclaim to that worker instead of blocking the caller.
        guard lock.`try`() else {
            closeRequested = true
            return
        }
        defer { lock.unlock() }
        if let pending = pendingCorePtr {
            maho_core_free(pending)
            pendingCorePtr = nil
        }
        guard let ptr = corePtr else { return }
        maho_core_free(ptr)
        corePtr = nil
        log.info("MahoCore destroyed")
    }

    func withCore<R>(_ body: (OpaquePointer) -> R) -> R? {
        lock.lock()
        defer { lock.unlock() }
        // U06i: a deferred (privately-owned) core is usable by its owner
        // before publication.
        guard let ptr = corePtr ?? pendingCorePtr else {
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
