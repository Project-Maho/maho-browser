import Foundation

// MARK: - Safe C String Helper

func withFFIString<T>(_ string: String, _ body: (UnsafePointer<CChar>) -> T) -> T {
    return string.withCString { body($0) }
}

// MARK: - MahoBridge Core Lifecycle

final class MahoBridge {
    static let shared = MahoBridge()

    private var core: OpaquePointer?
    private let lock = NSLock()

    private init() {}

    // MARK: - Lifecycle

    func initialize(storagePath: String? = nil) {
        lock.lock()
        defer { lock.unlock() }
        if let path = storagePath {
            core = path.withCString { maho_core_new_with_storage($0) }
        } else {
            core = maho_core_new()
        }
    }

    func shutdown() {
        lock.lock()
        defer { lock.unlock() }
        if let c = core {
            maho_core_free(c)
            core = nil
        }
    }

    // MARK: - Core Event Handling

    func sendEvent(_ event: ShellEvent) -> [CoreUpdate] {
        guard let json = encode(event) else { return [] }
        return withFFIString(json) { cStr -> [CoreUpdate] in
            guard let resultPtr = callCore({ maho_core_handle_event($0, cStr) }) else { return [] }
            defer { maho_string_free(resultPtr) }
            let resultStr = String(cString: resultPtr)
            return decode(resultStr) ?? []
        }
    }

    func tick() -> [CoreUpdate] {
        guard let resultPtr = callCore({ maho_core_tick($0) }) else { return [] }
        defer { maho_string_free(resultPtr) }
        return decode(String(cString: resultPtr)) ?? []
    }

    // MARK: - State Persistence

    func saveState() -> Bool {
        guard let result = callCore({ maho_core_save_state($0) }) else { return false }
        return result == 1
    }

    func loadState() -> Bool {
        guard let result = callCore({ maho_core_load_state($0) }) else { return false }
        return result == 1
    }

    // MARK: - URL Scheme Handling

    func handleUrlScheme(_ url: String) -> [CoreUpdate] {
        return withFFIString(url) { cStr -> [CoreUpdate] in
            guard let resultPtr = callCore({ maho_core_handle_url_scheme($0, cStr) }) else { return [] }
            defer { maho_string_free(resultPtr) }
            return decode(String(cString: resultPtr)) ?? []
        }
    }

    // MARK: - Memory Pressure

    func handleMemoryPressure(level: String) -> [CoreUpdate] {
        return withFFIString(level) { cStr -> [CoreUpdate] in
            guard let resultPtr = callCore({ maho_core_handle_memory_pressure($0, cStr) }) else { return [] }
            defer { maho_string_free(resultPtr) }
            return decode(String(cString: resultPtr)) ?? []
        }
    }

    // MARK: - Internal Helpers

    func callCore<T>(_ fn: (OpaquePointer) -> T?) -> T? {
        lock.lock()
        defer { lock.unlock() }
        guard let c = core else { return nil }
        return fn(c)
    }

    func callCoreNonOptional<T>(_ fn: (OpaquePointer) -> T) -> T? {
        lock.lock()
        defer { lock.unlock() }
        guard let c = core else { return nil }
        return fn(c)
    }

    func callCoreVoid(_ fn: (OpaquePointer) -> Void) {
        lock.lock()
        defer { lock.unlock() }
        guard let c = core else { return }
        fn(c)
    }

    func callCoreString(_ fn: (OpaquePointer) -> UnsafeMutablePointer<CChar>?) -> String? {
        guard let ptr = callCore(fn) else { return nil }
        defer { maho_string_free(ptr) }
        return String(cString: ptr)
    }

    enum BoostLoadError: LocalizedError {
        case coreUnavailable
        case decodeFailed

        var errorDescription: String? {
            switch self {
            case .coreUnavailable:
                return "Could not load boosts from the core."
            case .decodeFailed:
                return "Could not decode boost data."
            }
        }
    }

    func getBoostViewModelsResult() -> Result<[BoostViewModel], BoostLoadError> {
        guard let json = callCoreString({ maho_core_get_boost_view_models($0) }) else {
            return .failure(.coreUnavailable)
        }
        guard let boosts: [BoostViewModel] = decode(json) else {
            return .failure(.decodeFailed)
        }
        return .success(boosts)
    }

    func encode<T: Encodable>(_ value: T) -> String? {
        guard let data = try? JSONEncoder().encode(value) else { return nil }
        return String(data: data, encoding: .utf8)
    }

    func decode<T: Decodable>(_ json: String) -> T? {
        guard let data = json.data(using: .utf8) else { return nil }
        return try? JSONDecoder().decode(T.self, from: data)
    }
}
