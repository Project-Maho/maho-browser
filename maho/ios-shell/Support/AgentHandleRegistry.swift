import Foundation

/// Thread-safe registry that maps string wire handles to native agent session
/// pointers (`OpaquePointer`).
///
/// The web-ai bundle speaks in opaque **string** handles: `agentCreateSession`
/// returns a string, and every subsequent agent RPC (`agentSendMessage`,
/// `agentPollEvent`, `agentCancel`, `agentFreeSession`, `agentListTools`) passes
/// that string back. The native FFI, however, works with `OpaquePointer`
/// session values. This registry is the translation layer between the two.
///
/// It intentionally does **not** own the native lifetime: freeing the underlying
/// session is `MahoBridge.agentFreeSession(_:)`'s responsibility (which also
/// tears down the internal pointer-keyed `AgentSessionRegistry` inside
/// `MahoBridge+Agent.swift`). This registry only tracks the handle→pointer
/// mapping so the dispatcher can resolve a wire handle back to a pointer, and
/// forgets the mapping when the session is freed.
///
/// Mirrors the locking discipline of `ChatSessionRegistry`: all map mutations
/// happen under `lock`, and no FFI call is made while the lock is held.
final class AgentHandleRegistry {
    private var handles: [String: OpaquePointer] = [:]
    private let lock = NSLock()

    /// Registers `ptr` under a freshly minted UUID handle and returns the handle.
    func register(ptr: OpaquePointer) -> String {
        let id = UUID().uuidString
        lock.lock()
        handles[id] = ptr
        lock.unlock()
        return id
    }

    /// Returns the native pointer for `id`, or `nil` if `id` is unknown.
    func get(id: String) -> OpaquePointer? {
        lock.lock()
        defer { lock.unlock() }
        return handles[id]
    }

    /// Forgets the mapping for `id`. Idempotent; does not free the native session.
    func unregister(id: String) {
        lock.lock()
        handles.removeValue(forKey: id)
        lock.unlock()
    }

    /// Shared instance used by `WebViewBridgeController`.
    static let shared = AgentHandleRegistry()
}
