import Foundation
import os.log

// MahoCore FFI pointer: *mut MahoCore in Rust, opaque to Swift.
// Swift sees this as OpaquePointer since cbindgen emits `typedef struct MahoCore MahoCore;`

final class MahoBridge: @unchecked Sendable {
    static let shared = MahoBridge()

    private let lock = NSLock()
    private var corePtr: OpaquePointer?
    private let log = Logger(subsystem: "dev.maho.browser", category: "Bridge")

    private init() {}

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

    func tick() -> String? {
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
