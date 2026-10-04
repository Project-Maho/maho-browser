import Foundation

/// Thread-safe registry that maps UUID string handles to native (opaque) session pointers.
///
/// Lock discipline — the only invariant that matters for safety:
/// `lock` is NEVER held when `nativeFree` is called.
///
/// Rust's drop logic for a chat session may invoke callbacks or attempt to
/// acquire resources that themselves need to re-enter the registry.  Calling
/// `nativeFree` while the lock is held would deadlock in that scenario.  The
/// implementation removes the pointer from the map (under lock), releases the
/// lock, and only then calls `nativeFree`.
///
/// Constructor-injected `nativeFree` keeps the class testable without loading
/// the native library.  At the production wiring site, pass
/// `maho_core_chat_session_free`.
final class ChatSessionRegistry {
    private var sessions: [String: OpaquePointer] = [:]
    private let lock = NSLock()
    private let nativeFree: (OpaquePointer) -> Void

    init(nativeFree: @escaping (OpaquePointer) -> Void) {
        self.nativeFree = nativeFree
    }

    // MARK: - Public API

    /// Stores `ptr` in the registry and returns the newly allocated UUID handle.
    func register(ptr: OpaquePointer) -> String {
        let id = UUID().uuidString
        lock.lock()
        sessions[id] = ptr
        lock.unlock()
        return id
    }

    /// Stores `ptr` with the specified handle.
    func register(id: String, ptr: OpaquePointer) {
        lock.lock()
        sessions[id] = ptr
        lock.unlock()
    }


    /// Returns the native pointer for `id`, or `nil` if `id` is not registered.
    func get(id: String) -> OpaquePointer? {
        lock.lock()
        defer { lock.unlock() }
        return sessions[id]
    }

    /// Removes the entry for `id` and frees the underlying native session.
    ///
    /// Idempotent: a call with an unknown or already-released `id` is a no-op.
    ///
    /// - Important: The lock is released **before** `nativeFree` is called to
    ///   prevent a deadlock if Rust's drop logic re-enters the registry.
    func release(id: String) {
        lock.lock()
        let ptr = sessions.removeValue(forKey: id)
        lock.unlock()
        // Lock is free; safe to cross the FFI boundary now.
        if let ptr {
            nativeFree(ptr)
        }
    }

    /// Releases all registered sessions.
    ///
    /// All pointers are snapshot-under-lock and the map is cleared, then every
    /// pointer is freed outside the lock.
    func releaseAll() {
        lock.lock()
        let snapshot = Array(sessions.values)
        sessions.removeAll()
        lock.unlock()
        // Lock is free; safe to cross the FFI boundary for each pointer.
        for ptr in snapshot {
            nativeFree(ptr)
        }
    }

    /// Removes the entry whose stored pointer equals `ptr` and frees it.
    ///
    /// Use this when only the raw pointer is available (e.g. normal session
    /// teardown).  The pointer is removed from the map atomically so a
    /// concurrent `releaseAll()` cannot double-free the same session.
    ///
    /// Idempotent: if `ptr` is not in the registry (already freed or never
    /// registered) this is a no-op.
    func releaseByPointer(_ ptr: OpaquePointer) {
        lock.lock()
        let id = sessions.first(where: { $0.value == ptr })?.key
        if let id { sessions.removeValue(forKey: id) }
        lock.unlock()
        if id != nil {
            nativeFree(ptr)
        }
    }

    /// Removes the entry whose stored pointer equals `ptr` without freeing the native session.
    func unregisterWithoutFree(_ ptr: OpaquePointer) {
        lock.lock()
        let id = sessions.first(where: { $0.value == ptr })?.key
        if let id { sessions.removeValue(forKey: id) }
        lock.unlock()
    }


    /// Looks up the UUID string handle registered for `ptr`.
    func findHandle(for ptr: OpaquePointer) -> String? {
        lock.lock()
        defer { lock.unlock() }
        return sessions.first(where: { $0.value == ptr })?.key
    }

    static let shared = ChatSessionRegistry { ptr in
        maho_core_chat_session_free(ptr)
    }
}
