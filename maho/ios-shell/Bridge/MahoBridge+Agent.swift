import Foundation
import Security

// MARK: - FFI declarations

@_silgen_name("maho_agent_create_session")
private func ffiAgentCreateSession(
    _ core: OpaquePointer?,
    _ sessionId: UnsafePointer<CChar>?,
    _ workspaceRoot: UnsafePointer<CChar>?,
    _ allowInsecureKeyStorage: Bool,
    _ permissionCb: MahoAgentPermissionCallback?,
    _ permissionUserData: UnsafeMutableRawPointer?,
    _ secureStorageCb: MahoAgentSecureStorageCallback?,
    _ secureStorageUserData: UnsafeMutableRawPointer?
) -> OpaquePointer?

@_silgen_name("maho_agent_session_free")
private func ffiAgentSessionFree(_ session: OpaquePointer?)

@_silgen_name("maho_agent_send_message")
private func ffiAgentSendMessage(
    _ session: OpaquePointer?,
    _ message: UnsafePointer<CChar>?,
    _ onToken: (@convention(c) (UnsafeMutableRawPointer?, UnsafePointer<CChar>?) -> Void)?,
    _ onComplete: (@convention(c) (UnsafeMutableRawPointer?, UnsafePointer<CChar>?, UnsafePointer<CChar>?) -> Void)?,
    _ onError: (@convention(c) (UnsafeMutableRawPointer?, UnsafePointer<CChar>?) -> Void)?,
    _ userData: UnsafeMutableRawPointer?
) -> Bool

@_silgen_name("maho_agent_cancel")
private func ffiAgentCancel(_ session: OpaquePointer?) -> Bool

@_silgen_name("maho_agent_list_tools")
private func ffiAgentListTools(_ session: OpaquePointer?) -> UnsafeMutablePointer<CChar>?

private typealias MahoAgentPermissionCallback = @convention(c) (
    UnsafeMutableRawPointer?,
    UnsafePointer<CChar>?,
    UnsafePointer<CChar>?
) -> Int32


private typealias MahoAgentSecureStorageCallback = @convention(c) (
    UnsafeMutableRawPointer?,
    UnsafePointer<CChar>?
) -> MahoAgentSecureKey

// MARK: - BYOK OS Keychain Helper

struct BYOKKeychain {
    static let service = "dev.maho.browser.byok"

    static func get(provider: String) -> String? {
        guard let data = getData(provider: provider) else { return nil }
        return String(data: data, encoding: .utf8)
    }

    // Returns raw keychain Data, bypassing the Swift String intermediate. The
    // agent secure_storage callback uses this to copy plaintext into a libc
    // buffer that gets memset_s'd on free, avoiding String/ARC plaintext
    // residue (R7 A4 finding W6).
    static func getData(provider: String) -> Data? {
        let query: [CFString: Any] = [
            kSecClass: kSecClassGenericPassword,
            kSecAttrService: service,
            kSecAttrAccount: provider,
            kSecReturnData: true,
            kSecMatchLimit: kSecMatchLimitOne
        ]
        var result: CFTypeRef?
        let status = SecItemCopyMatching(query as CFDictionary, &result)
        guard status == errSecSuccess, let data = result as? Data else { return nil }
        return data
    }

    static func set(provider: String, key: String) -> Bool {
        guard let data = key.data(using: .utf8) else { return false }
        let query: [CFString: Any] = [
            kSecClass: kSecClassGenericPassword,
            kSecAttrService: service,
            kSecAttrAccount: provider
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

    static func delete(provider: String) -> Bool {
        let query: [CFString: Any] = [
            kSecClass: kSecClassGenericPassword,
            kSecAttrService: service,
            kSecAttrAccount: provider
        ]
        let status = SecItemDelete(query as CFDictionary)
        return status == errSecSuccess || status == errSecItemNotFound
    }
}

// MARK: - Agent Session Context & Registry

private final class AgentSessionContext {
    private let lock = NSLock()
    private var events: [String] = []
    private static let maxEventQueueDepth = 2048

    func enqueue(type: String, data: Any) {
        lock.lock()
        defer { lock.unlock() }
        if events.count >= Self.maxEventQueueDepth { return }
        let payload: [String: Any] = ["type": type, "data": data]
        if let jsonData = try? JSONSerialization.data(withJSONObject: payload),
           let jsonString = String(data: jsonData, encoding: .utf8) {
            events.append(jsonString)
        }
    }
    
    func dequeue() -> String? {
        lock.lock()
        defer { lock.unlock() }
        guard !events.isEmpty else { return nil }
        return events.removeFirst()
    }
}

private final class AgentSessionRegistry {
    static let shared = AgentSessionRegistry()
    
    struct SessionState {
        let context: AgentSessionContext
        let contextPointer: UnsafeMutableRawPointer
    }
    
    private let lock = NSLock()
    private var sessions: [UnsafeMutableRawPointer: SessionState] = [:]
    
    private init() {}
    
    func register(ptr: OpaquePointer, state: SessionState) {
        let key = UnsafeMutableRawPointer(ptr)
        lock.lock()
        sessions[key] = state
        lock.unlock()
    }
    
    func contextPointer(for ptr: OpaquePointer) -> UnsafeMutableRawPointer? {
        let key = UnsafeMutableRawPointer(ptr)
        lock.lock()
        defer { lock.unlock() }
        return sessions[key]?.contextPointer
    }
    
    func context(for ptr: OpaquePointer) -> AgentSessionContext? {
        let key = UnsafeMutableRawPointer(ptr)
        lock.lock()
        defer { lock.unlock() }
        return sessions[key]?.context
    }
    
    func unregister(ptr: OpaquePointer) {
        let key = UnsafeMutableRawPointer(ptr)
        lock.lock()
        let removed = sessions.removeValue(forKey: key)
        lock.unlock()
        if let removed = removed {
            Unmanaged<AgentSessionContext>.fromOpaque(removed.contextPointer).release()
        }
    }
}

// MARK: - Global Callback Functions

private func agentPermissionCallback(
    _ userData: UnsafeMutableRawPointer?,
    _ toolName: UnsafePointer<CChar>?,
    _ arguments: UnsafePointer<CChar>?
) -> Int32 {
    guard let toolName = toolName else { return 1 }
    let tool = String(cString: toolName)
    if tool == "fs_read" || tool == "web_search" {
        return 0
    }
    return 1
}

private let freeSecureKey: @convention(c) (UnsafeMutablePointer<CChar>?, UInt) -> Void = { ptr, len in
    guard let ptr = ptr else { return }
    if len > 0 {
        _ = Darwin.memset_s(ptr, Int(len), 0, Int(len))
    }
    free(ptr)
}

private func agentSecureStorageCallback(
    _ userData: UnsafeMutableRawPointer?,
    _ provider: UnsafePointer<CChar>?
) -> MahoAgentSecureKey {
    guard let provider else {
        return MahoAgentSecureKey(ptr: nil, len: 0, free_fn: nil)
    }
    let providerStr = String(cString: provider)
    guard let data = BYOKKeychain.getData(provider: providerStr), !data.isEmpty else {
        return MahoAgentSecureKey(ptr: nil, len: 0, free_fn: nil)
    }
    let len = data.count
    guard let allocated = malloc(len)?.assumingMemoryBound(to: CChar.self) else {
        return MahoAgentSecureKey(ptr: nil, len: 0, free_fn: nil)
    }
    data.withUnsafeBytes { (raw: UnsafeRawBufferPointer) in
        if let base = raw.baseAddress {
            memcpy(allocated, base, len)
        }
    }
    return MahoAgentSecureKey(
        ptr: allocated,
        len: UInt(len),
        free_fn: freeSecureKey
    )
}

private func onAgentToken(
    _ userData: UnsafeMutableRawPointer?,
    _ token: UnsafePointer<CChar>?
) {
    guard let userData, let token else { return }
    let context = Unmanaged<AgentSessionContext>.fromOpaque(userData).takeUnretainedValue()
    context.enqueue(type: "token", data: String(cString: token))
}

private func onAgentComplete(
    _ userData: UnsafeMutableRawPointer?,
    _ fullText: UnsafePointer<CChar>?,
    _ toolCallsJSON: UnsafePointer<CChar>?
) {
    guard let userData else { return }
    let context = Unmanaged<AgentSessionContext>.fromOpaque(userData).takeUnretainedValue()
    let payload: [String: String] = [
        "full_text": fullText.map { String(cString: $0) } ?? "",
        "tool_calls_json": toolCallsJSON.map { String(cString: $0) } ?? ""
    ]
    context.enqueue(type: "complete", data: payload)
}

private func onAgentError(
    _ userData: UnsafeMutableRawPointer?,
    _ error: UnsafePointer<CChar>?
) {
    guard let userData, let error else { return }
    let context = Unmanaged<AgentSessionContext>.fromOpaque(userData).takeUnretainedValue()
    context.enqueue(type: "error", data: String(cString: error))
}

// MARK: - MahoBridge extension

extension MahoBridge {
    func agentCreateSession(sessionId: String) -> OpaquePointer? {
        return withCore { corePtr -> OpaquePointer? in
            FFIString.withCString(sessionId) { sessionIdPointer -> OpaquePointer? in
                let fileManager = FileManager.default
                let workspacePath = fileManager.urls(for: .documentDirectory, in: .userDomainMask).first?
                    .appendingPathComponent("maho-agent-workspace").path ?? ""
                if !fileManager.fileExists(atPath: workspacePath) {
                    try? fileManager.createDirectory(atPath: workspacePath, withIntermediateDirectories: true, attributes: nil)
                }
                
                return FFIString.withCString(workspacePath) { workspacePathPointer -> OpaquePointer? in
                    let context = AgentSessionContext()
                    let contextPointer = Unmanaged.passRetained(context).toOpaque()
                    
                    guard let sessionPtr = ffiAgentCreateSession(
                        corePtr,
                        sessionIdPointer,
                        workspacePathPointer,
                        false, // allowInsecureKeyStorage = false
                        agentPermissionCallback,
                        contextPointer,
                        agentSecureStorageCallback,
                        contextPointer
                    ) else {
                        Unmanaged<AgentSessionContext>.fromOpaque(contextPointer).release()
                        return nil
                    }
                    
                    AgentSessionRegistry.shared.register(
                        ptr: sessionPtr,
                        state: AgentSessionRegistry.SessionState(context: context, contextPointer: contextPointer)
                    )
                    return sessionPtr
                }
            }
        } ?? nil
    }
    
    func agentFreeSession(_ ptr: OpaquePointer) {
        ffiAgentSessionFree(ptr)
        AgentSessionRegistry.shared.unregister(ptr: ptr)
    }
    
    @discardableResult
    func agentSendMessage(_ ptr: OpaquePointer, message: String) -> Bool {
        guard let contextState = AgentSessionRegistry.shared.contextPointer(for: ptr) else {
            return false
        }
        return FFIString.withCString(message) { messagePointer in
            ffiAgentSendMessage(
                ptr,
                messagePointer,
                onAgentToken,
                onAgentComplete,
                onAgentError,
                contextState
            )
        }
    }
    
    @discardableResult
    func agentCancel(_ ptr: OpaquePointer) -> Bool {
        return ffiAgentCancel(ptr)
    }
    
    func agentListTools(_ ptr: OpaquePointer) -> String? {
        return FFIString.consume(ffiAgentListTools(ptr))
    }
    
    func agentPollEvent(_ ptr: OpaquePointer) -> String? {
        return AgentSessionRegistry.shared.context(for: ptr)?.dequeue()
    }
}
