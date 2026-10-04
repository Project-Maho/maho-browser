import Foundation
import Security

// MARK: - FFI declarations

@_silgen_name("maho_agent_create_session_leased")
private func ffiAgentCreateSessionLeased(
    _ core: OpaquePointer?,
    _ sessionId: UnsafePointer<CChar>?,
    _ workspaceRoot: UnsafePointer<CChar>?,
    _ allowInsecureKeyStorage: Bool,
    _ permissionCb: MahoAgentPermissionCallback?,
    _ permissionUserData: UnsafeMutableRawPointer?,
    _ secureStorageCb: MahoAgentSecureStorageCallback?,
    _ secureStorageUserData: UnsafeMutableRawPointer?,
    _ browserToolCb: MahoAgentBrowserToolCallback?,
    _ browserToolUserData: UnsafeMutableRawPointer?,
    _ spaceId: UnsafePointer<CChar>?,
    _ onSessionRelease: @convention(c) (UnsafeMutableRawPointer?) -> Void,
    _ sessionReleaseUserData: UnsafeMutableRawPointer?
) -> OpaquePointer?

@_silgen_name("maho_agent_session_free")
private func ffiAgentSessionFree(_ session: OpaquePointer?)

@_silgen_name("maho_agent_send_message_leased")
private func ffiAgentSendMessageLeased(
    _ session: OpaquePointer?,
    _ message: UnsafePointer<CChar>?,
    _ onToken: (@convention(c) (UnsafeMutableRawPointer?, UnsafePointer<CChar>?) -> Void)?,
    _ onThinking: (@convention(c) (UnsafeMutableRawPointer?, UnsafePointer<CChar>?) -> Void)?,
    _ onToolCall: (@convention(c) (UnsafeMutableRawPointer?, UnsafePointer<CChar>?, UnsafePointer<CChar>?, UnsafePointer<CChar>?) -> Void)?,
    _ onToolResult: (@convention(c) (UnsafeMutableRawPointer?, UnsafePointer<CChar>?, UnsafePointer<CChar>?, UnsafePointer<CChar>?) -> Void)?,
    _ onComplete: (@convention(c) (UnsafeMutableRawPointer?, UnsafePointer<CChar>?, UnsafePointer<CChar>?) -> Void)?,
    _ onError: (@convention(c) (UnsafeMutableRawPointer?, UnsafePointer<CChar>?) -> Void)?,
    _ callbackUserData: UnsafeMutableRawPointer?,
    _ onRelease: @convention(c) (UnsafeMutableRawPointer?) -> Void,
    _ releaseUserData: UnsafeMutableRawPointer?
) -> Bool

@_silgen_name("maho_agent_cancel")
private func ffiAgentCancel(_ session: OpaquePointer?) -> Bool

@_silgen_name("maho_agent_list_tools")
private func ffiAgentListTools(_ session: OpaquePointer?) -> UnsafeMutablePointer<CChar>?

@_silgen_name("maho_agent_set_artifact_root")
private func ffiAgentSetArtifactRoot(
    _ session: OpaquePointer?,
    _ path: UnsafePointer<CChar>?
)

@_silgen_name("maho_agent_set_artifact_created_callback")
private func ffiAgentSetArtifactCreatedCallback(
    _ session: OpaquePointer?,
    _ callback: (@convention(c) (UnsafeMutableRawPointer?, UnsafePointer<CChar>?) -> Void)?,
    _ userData: UnsafeMutableRawPointer?
)

@_silgen_name("maho_agent_list_artifacts")
private func ffiAgentListArtifacts(_ session: OpaquePointer?) -> UnsafeMutablePointer<CChar>?

@_silgen_name("maho_agent_artifact_path")
private func ffiAgentArtifactPath(
    _ session: OpaquePointer?,
    _ artifactId: UnsafePointer<CChar>?
) -> UnsafeMutablePointer<CChar>?

private typealias MahoAgentPermissionCallback = @convention(c) (
    UnsafeMutableRawPointer?,
    UnsafePointer<CChar>?,
    UnsafePointer<CChar>?
) -> Int32

private typealias MahoAgentSecureStorageCallback = @convention(c) (
    UnsafeMutableRawPointer?,
    UnsafePointer<CChar>?
) -> MahoAgentSecureKey

private typealias MahoAgentBrowserToolCallback = @convention(c) (
    UnsafeMutableRawPointer?,
    UnsafePointer<CChar>?,
    UnsafePointer<CChar>?
) -> MahoAgentToolResult

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

struct AgentArtifactRecord: Decodable {
    let artifactId: String
    let sessionId: String
    let displayName: String
    let mimeType: String
    let sizeBytes: UInt64
    let createdAtMs: Int64

    enum CodingKeys: String, CodingKey {
        case artifactId = "artifact_id"
        case sessionId = "session_id"
        case displayName = "display_name"
        case mimeType = "mime_type"
        case sizeBytes = "size_bytes"
        case createdAtMs = "created_at_ms"
    }

    var bridgeValue: [String: Any] {
        [
            "artifactId": artifactId,
            "sessionId": sessionId,
            "displayName": displayName,
            "mimeType": mimeType,
            "sizeBytes": sizeBytes,
            "createdAt": Double(createdAtMs) / 1_000.0
        ]
    }

    var eventValue: [String: Any] {
        [
            "artifact_id": artifactId,
            "session_id": sessionId,
            "display_name": displayName,
            "mime_type": mimeType,
            "size_bytes": sizeBytes,
            "created_at": Double(createdAtMs) / 1_000.0
        ]
    }
}

final class AgentSessionContext {
    let artifactRoot: URL

    private let lock = NSLock()
    private struct QueuedRecord {
        let type: String
        let seq: UInt64
        var jsonString: String
        var byteCount: Int
        var isDelta: Bool
        var textByteCount: Int
    }

    private var events: [QueuedRecord?] = []
    private var readIndex: Int = 0
    private var totalLiveBytes: Int = 0
    private var deliveredArtifactIds: Set<String> = []
    private var seq: UInt64 = 0
    private var closing = false
    private var overflowed = false
    private var turnInFlight = false
    private var terminalEnqueued = false

    private static let maxTotalRecords = 2048
    private static let maxTotalBytes = 4 * 1024 * 1024 // 4 MiB
    private static let controlRecordAllowance = 32
    private static let controlByteAllowance = 64 * 1024 // 64 KiB
    private static let maxChunkBytes = 64 * 1024 // 64 KiB
    private static let reservedOverflowRecords = 1
    private static let reservedOverflowBytes = 1024

    var cancelNativeTurn: (() -> Void)?

#if DEBUG
    // Observation only: installed before a fixture transfers native ownership.
    var memoryThreadDidRelease: (() -> Void)?
#endif

    init(artifactRoot: URL) {
        self.artifactRoot = artifactRoot
    }

    func beginTurn() -> Bool {
        lock.lock()
        defer { lock.unlock() }
        guard !closing, !overflowed, !turnInFlight else { return false }
        turnInFlight = true
        terminalEnqueued = false
        return true
    }

    func endTurn() {
        lock.lock()
        turnInFlight = false
        lock.unlock()
    }

    func beginClosing() {
        lock.lock()
        closing = true
        events.removeAll(keepingCapacity: false)
        readIndex = 0
        totalLiveBytes = 0
        deliveredArtifactIds.removeAll(keepingCapacity: false)
        lock.unlock()
    }

    func enqueue(type: String, data: Any) {
        lock.lock()
        defer { lock.unlock() }
        guard !closing, !overflowed, !terminalEnqueued else { return }
        if (type == "token" || type == "thinking"), let text = data as? String,
           text.utf8.count > Self.maxChunkBytes {
            guard text.utf8.count <= Self.maxTotalBytes else {
                triggerOverflowLocked()
                return
            }
            var chunk = ""
            var chunkBytes = 0
            for scalar in text.unicodeScalars {
                let scalarBytes = String(scalar).utf8.count
                if chunkBytes + scalarBytes > Self.maxChunkBytes {
                    guard enqueueLocked(type: type, data: chunk) else { return }
                    chunk = ""
                    chunkBytes = 0
                }
                chunk.unicodeScalars.append(scalar)
                chunkBytes += scalarBytes
            }
            _ = enqueueLocked(type: type, data: chunk)
            return
        }
        _ = enqueueLocked(type: type, data: data)
    }

    func enqueueArtifact(_ artifact: AgentArtifactRecord) {
        lock.lock()
        defer { lock.unlock() }
        guard !closing,
              !overflowed,
              !artifact.artifactId.isEmpty,
              !deliveredArtifactIds.contains(artifact.artifactId) else {
            return
        }
        if enqueueLocked(type: "artifact_created", data: artifact.eventValue) {
            deliveredArtifactIds.insert(artifact.artifactId)
        }
    }

    @discardableResult
    private func enqueueLocked(type: String, data: Any) -> Bool {
        guard !closing, !overflowed, !terminalEnqueued else { return false }

        let isDelta = (type == "token" || type == "thinking") && (data is String)
        if isDelta, let text = data as? String {
            guard let encodedData = try? JSONSerialization.data(
                withJSONObject: text, options: .fragmentsAllowed),
                  let encodedText = String(data: encodedData, encoding: .utf8) else {
                return false
            }
            let textBytes = text.utf8.count
            if events.count > readIndex {
                let lastIndex = events.count - 1
                if var lastRecord = events[lastIndex],
                   lastRecord.isDelta && lastRecord.type == type,
                   lastRecord.textByteCount + textBytes <= Self.maxChunkBytes {
                    let deltaBytes = encodedData.count - 2
                    if totalLiveBytes + deltaBytes <= Self.maxTotalBytes - Self.controlByteAllowance {
                        // Delta JSON has a fixed wrapper ending in `"}`.
                        // Append escaped text without re-encoding the growing chunk.
                        lastRecord.jsonString.removeLast(2)
                        lastRecord.jsonString.append(contentsOf: encodedText.dropFirst())
                        lastRecord.jsonString.append("}")
                        lastRecord.byteCount += deltaBytes
                        lastRecord.textByteCount += textBytes
                        totalLiveBytes += deltaBytes
                        events[lastIndex] = lastRecord
                        return true
                    }
                }
            }

            // Delta as a new record: must respect control allowance
            let maxDeltaRecords = Self.maxTotalRecords - Self.controlRecordAllowance
            let maxDeltaBytes = Self.maxTotalBytes - Self.controlByteAllowance
            let liveCount = events.count - readIndex

            let currentSeq = seq
            let jsonString = "{\"type\":\"\(type)\",\"seq\":\(currentSeq),\"data\":\(encodedText)}"
            let newBytes = jsonString.utf8.count

            if liveCount + 1 <= maxDeltaRecords && totalLiveBytes + newBytes <= maxDeltaBytes {
                seq += 1
                totalLiveBytes += newBytes
                events.append(QueuedRecord(
                    type: type,
                    seq: currentSeq,
                    jsonString: jsonString,
                    byteCount: newBytes,
                    isDelta: true,
                    textByteCount: textBytes
                ))
                return true
            } else {
                triggerOverflowLocked()
                return false
            }
        }

        // Non-delta event (control or terminal)
        let currentSeq = seq
        let payload: [String: Any] = ["type": type, "seq": currentSeq, "data": data]
        guard let jsonData = try? JSONSerialization.data(withJSONObject: payload),
              let jsonString = String(data: jsonData, encoding: .utf8) else {
            return false
        }
        let eventBytes = jsonData.count

        // Never discard an accepted record to make an incoming terminal look
        // successful. Keep the overflow reserve even across queued turns.
        if (events.count - readIndex + 1 + Self.reservedOverflowRecords > Self.maxTotalRecords) ||
           (totalLiveBytes + eventBytes + Self.reservedOverflowBytes > Self.maxTotalBytes) {
            triggerOverflowLocked()
            return false
        }

        seq += 1
        totalLiveBytes += eventBytes
        events.append(QueuedRecord(
            type: type,
            seq: currentSeq,
            jsonString: jsonString,
            byteCount: eventBytes,
            isDelta: false,
            textByteCount: 0
        ))
        if type == "complete" || type == "error" {
            terminalEnqueued = true
        }
        return true
    }

    private func triggerOverflowLocked() {
        guard !overflowed else { return }
        overflowed = true
        terminalEnqueued = true

        let currentSeq = seq
        seq += 1
        let payload: [String: Any] = ["type": "error", "seq": currentSeq, "data": "event_queue_overflow"]
        if let jsonData = try? JSONSerialization.data(withJSONObject: payload),
           let jsonString = String(data: jsonData, encoding: .utf8) {
            let byteCount = jsonData.count
            totalLiveBytes += byteCount
            events.append(QueuedRecord(
                type: "error",
                seq: currentSeq,
                jsonString: jsonString,
                byteCount: byteCount,
                isDelta: false,
                textByteCount: 0
            ))
        }

        // The installed callback acquires ownership before scheduling native
        // cancellation; no unleased pointer crosses the asynchronous boundary.
        cancelNativeTurn?()
    }

    func dequeue() -> String? {
        lock.lock()
        defer { lock.unlock() }
        guard !closing, readIndex < events.count,
              let record = events[readIndex] else { return nil }
        events[readIndex] = nil
        readIndex += 1
        totalLiveBytes -= record.byteCount

        // Amortized compaction
        if readIndex >= 64 && readIndex * 2 >= events.count {
            events.removeFirst(readIndex)
            readIndex = 0
        } else if readIndex == events.count {
            events.removeAll(keepingCapacity: false)
            readIndex = 0
            totalLiveBytes = 0
        }
        return record.jsonString
    }
}

final class AgentSessionRegistry {
    static let shared = AgentSessionRegistry()

    struct SessionState {
        let context: AgentSessionContext
        let contextPointer: UnsafeMutableRawPointer
    }

    final class OperationLease {
        let ptr: OpaquePointer
        let context: AgentSessionContext

        private let registry: AgentSessionRegistry
        private let entry: SessionEntry
        private var released = false

        fileprivate init(
            ptr: OpaquePointer,
            context: AgentSessionContext,
            registry: AgentSessionRegistry,
            entry: SessionEntry
        ) {
            self.ptr = ptr
            self.context = context
            self.registry = registry
            self.entry = entry
        }

        deinit {
            release()
        }

        func release() {
            guard !released else { return }
            released = true
            registry.releaseOperation(entry)
        }
    }

    fileprivate final class SessionEntry {
        let ptr: OpaquePointer
        let state: SessionState
        var activeOperations = 0
        var closing = false
        var nativeFree: ((OpaquePointer) -> Void)?

        init(ptr: OpaquePointer, state: SessionState) {
            self.ptr = ptr
            self.state = state
        }
    }

    private let lock = NSLock()
    private let teardownQueue = DispatchQueue(
        label: "dev.maho.agent-session-teardown",
        qos: .utility,
        attributes: .concurrent
    )
    private var sessions: [UnsafeMutableRawPointer: SessionEntry] = [:]

    private init() {}

    func register(ptr: OpaquePointer, state: SessionState) {
        let key = UnsafeMutableRawPointer(ptr)
        lock.lock()
        sessions[key] = SessionEntry(ptr: ptr, state: state)
        lock.unlock()
    }

    func acquireOperation(for ptr: OpaquePointer) -> OperationLease? {
        let key = UnsafeMutableRawPointer(ptr)
        lock.lock()
        guard let entry = sessions[key], !entry.closing else {
            lock.unlock()
            return nil
        }
        entry.activeOperations += 1
        let lease = OperationLease(
            ptr: ptr,
            context: entry.state.context,
            registry: self,
            entry: entry
        )
        lock.unlock()
        return lease
    }

    func context(for ptr: OpaquePointer) -> AgentSessionContext? {
        let key = UnsafeMutableRawPointer(ptr)
        lock.lock()
        defer { lock.unlock() }
        guard let entry = sessions[key], !entry.closing else { return nil }
        return entry.state.context
    }

    func close(ptr: OpaquePointer, nativeFree: @escaping (OpaquePointer) -> Void) {
        let key = UnsafeMutableRawPointer(ptr)
        lock.lock()
        guard let entry = sessions.removeValue(forKey: key), !entry.closing else {
            lock.unlock()
            return
        }
        entry.closing = true
        entry.nativeFree = nativeFree
        let shouldFree = entry.activeOperations == 0
        let free = shouldFree ? entry.nativeFree : nil
        if shouldFree {
            entry.nativeFree = nil
        }
        lock.unlock()

        entry.state.context.beginClosing()
        if let free {
            scheduleNativeFree(entry.ptr, free: free)
        }
    }

    private func releaseOperation(_ entry: SessionEntry) {
        lock.lock()
        precondition(entry.activeOperations > 0)
        entry.activeOperations -= 1
        let shouldFree = entry.closing && entry.activeOperations == 0
        let free = shouldFree ? entry.nativeFree : nil
        if shouldFree {
            entry.nativeFree = nil
        }
        lock.unlock()

        if let free {
            scheduleNativeFree(entry.ptr, free: free)
        }
    }

    private func scheduleNativeFree(
        _ ptr: OpaquePointer,
        free: @escaping (OpaquePointer) -> Void
    ) {
        teardownQueue.async {
            free(ptr)
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
    if tool == "fs_read" || tool == "web_search" || tool == "click_element" || tool == "fill_input" {
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

private let freeCString: @convention(c) (UnsafeMutablePointer<CChar>?) -> Void = { ptr in
    if let ptr = ptr { free(ptr) }
}

private func makeAgentBrowserToolResult(
    json: String? = nil,
    error: String? = nil,
    code: String? = nil,
    retryable: Bool = false
) -> MahoAgentToolResult {
    MahoAgentToolResult(
        json_ptr: json.map { strdup($0) },
        error_ptr: error.map { strdup($0) },
        free_fn: freeCString,
        error_code_ptr: code.map { strdup($0) },
        error_retryable: retryable
    )
}

private func agentBrowserToolCallback(
    _ userData: UnsafeMutableRawPointer?,
    _ toolName: UnsafePointer<CChar>?,
    _ arguments: UnsafePointer<CChar>?
) -> MahoAgentToolResult {
    guard let toolName else {
        return makeAgentBrowserToolResult(error: "Missing browser tool name", code: "invalid_request")
    }
    let name = String(cString: toolName)
    if name == "tools/list" {
        let payload: [String: Any] = ["tools": AgenticBrowsingDOM.toolDescriptors]
        guard JSONSerialization.isValidJSONObject(payload),
              let data = try? JSONSerialization.data(withJSONObject: payload),
              let json = String(data: data, encoding: .utf8) else {
            return makeAgentBrowserToolResult(error: "Failed to encode browser tool descriptors", code: "malformed_discovery")
        }
        return makeAgentBrowserToolResult(json: json)
    }

    guard AgenticBrowsingDOM.toolNames.contains(name) else {
        return makeAgentBrowserToolResult(error: "Unknown browser tool", code: "unknown_tool")
    }

    let args: [String: Any]
    if let arguments {
        let raw = String(cString: arguments)
        guard let data = raw.data(using: .utf8),
              let object = try? JSONSerialization.jsonObject(with: data) as? [String: Any] else {
            return makeAgentBrowserToolResult(error: "Browser tool arguments are not valid JSON", code: "invalid_arguments")
        }
        args = object
    } else {
        args = [:]
    }

    let result = AgenticBrowsingDOM.invoke(name: name, args: args)
    if result["ok"] as? Bool == false {
        let code = result["error"] as? String ?? "browser_tool_failed"
        let message = result["message"] as? String ?? code
        let retryable = ["page_script_timeout", "page_script_failed", "no_active_page"].contains(code)
        return makeAgentBrowserToolResult(error: message, code: code, retryable: retryable)
    }

    guard let data = try? JSONSerialization.data(withJSONObject: result),
          let json = String(data: data, encoding: .utf8) else {
        return makeAgentBrowserToolResult(error: "Failed to encode browser tool result", code: "malformed_execution")
    }
    return makeAgentBrowserToolResult(json: json)
}

enum AiSettingsKeys {
    static let provider = AiProviderResolver.providerDefaultsKey
    static let baseUrl = AiProviderResolver.baseURLDefaultsKey
    static let model = AiProviderResolver.modelDefaultsKey
    static let customApiKeyAccount = AiProvider.openaiCompatible.rawValue
}

private func agentSecureStorageCallback(
    _ userData: UnsafeMutableRawPointer?,
    _ provider: UnsafePointer<CChar>?
) -> MahoAgentSecureKey {
    guard let provider else {
        return MahoAgentSecureKey(ptr: nil, len: 0, free_fn: nil, base_url: nil, model: nil, cstring_free_fn: nil)
    }
    let providerStr = String(cString: provider)
    let defaults = UserDefaults.standard
    let storedProvider = defaults.string(forKey: AiSettingsKeys.provider) ?? ""

    // Custom-provider path (mirrors desktop OnAgentSecureStorage): a custom
    // api_key + base_url + model configured in AI Settings, matched by family
    // ("openai" is served when stored provider is "openai" or "openai-compatible").
    let familyMatches = storedProvider == providerStr || storedProvider == providerStr + "-compatible"
    if familyMatches,
       let customKey = BYOKKeychain.get(provider: AiSettingsKeys.customApiKeyAccount),
       !customKey.isEmpty {
        let klen = customKey.utf8.count
        if let kbuf = malloc(klen)?.assumingMemoryBound(to: CChar.self) {
            _ = customKey.withCString { memcpy(kbuf, $0, klen) }
            let baseUrl = defaults.string(forKey: AiSettingsKeys.baseUrl) ?? ""
            let model = defaults.string(forKey: AiSettingsKeys.model) ?? ""
            return MahoAgentSecureKey(
                ptr: kbuf,
                len: UInt(klen),
                free_fn: freeSecureKey,
                base_url: baseUrl.isEmpty ? nil : strdup(baseUrl),
                model: model.isEmpty ? nil : strdup(model),
                cstring_free_fn: freeCString
            )
        }
    }

    guard let data = BYOKKeychain.getData(provider: providerStr), !data.isEmpty else {
        return MahoAgentSecureKey(ptr: nil, len: 0, free_fn: nil, base_url: nil, model: nil, cstring_free_fn: nil)
    }
    let len = data.count
    guard let allocated = malloc(len)?.assumingMemoryBound(to: CChar.self) else {
        return MahoAgentSecureKey(ptr: nil, len: 0, free_fn: nil, base_url: nil, model: nil, cstring_free_fn: nil)
    }
    data.withUnsafeBytes { (raw: UnsafeRawBufferPointer) in
        if let base = raw.baseAddress {
            memcpy(allocated, base, len)
        }
    }
    return MahoAgentSecureKey(
        ptr: allocated,
        len: UInt(len),
        free_fn: freeSecureKey,
        base_url: nil,
        model: nil,
        cstring_free_fn: nil
    )
}

private let releaseAgentSessionContext: @convention(c) (UnsafeMutableRawPointer?) -> Void = { pointer in
    guard let pointer else { return }
#if DEBUG
    Unmanaged<AgentSessionContext>.fromOpaque(pointer).takeUnretainedValue().memoryThreadDidRelease?()
#endif
    Unmanaged<AgentSessionContext>.fromOpaque(pointer).release()
}

private let releaseAgentTurnContext: @convention(c) (UnsafeMutableRawPointer?) -> Void = { pointer in
    guard let pointer else { return }
    let context = Unmanaged<AgentSessionContext>.fromOpaque(pointer).takeRetainedValue()
    context.endTurn()
}

private func onAgentToken(
    _ userData: UnsafeMutableRawPointer?,
    _ token: UnsafePointer<CChar>?
) {
    guard let userData, let token else { return }
    let context = Unmanaged<AgentSessionContext>.fromOpaque(userData).takeUnretainedValue()
    context.enqueue(type: "token", data: String(cString: token))
}

private func onAgentThinking(
    _ userData: UnsafeMutableRawPointer?,
    _ thinking: UnsafePointer<CChar>?
) {
    guard let userData, let thinking else { return }
    let context = Unmanaged<AgentSessionContext>.fromOpaque(userData).takeUnretainedValue()
    context.enqueue(type: "thinking", data: String(cString: thinking))
}

private func onAgentToolCall(
    _ userData: UnsafeMutableRawPointer?,
    _ id: UnsafePointer<CChar>?,
    _ name: UnsafePointer<CChar>?,
    _ args: UnsafePointer<CChar>?
) {
    guard let userData, let id, let name else { return }
    let context = Unmanaged<AgentSessionContext>.fromOpaque(userData).takeUnretainedValue()
    let payload: [String: String] = [
        "id": String(cString: id),
        "name": String(cString: name),
        "args": args.map { String(cString: $0) } ?? ""
    ]
    context.enqueue(type: "tool_call", data: payload)
}

private func onAgentToolResult(
    _ userData: UnsafeMutableRawPointer?,
    _ id: UnsafePointer<CChar>?,
    _ name: UnsafePointer<CChar>?,
    _ result: UnsafePointer<CChar>?
) {
    guard let userData, let id, let name else { return }
    let context = Unmanaged<AgentSessionContext>.fromOpaque(userData).takeUnretainedValue()
    let payload: [String: String] = [
        "id": String(cString: id),
        "name": String(cString: name),
        "result": result.map { String(cString: $0) } ?? ""
    ]
    context.enqueue(type: "tool_result", data: payload)
}

private func onAgentArtifactCreated(
    _ userData: UnsafeMutableRawPointer?,
    _ artifactJSON: UnsafePointer<CChar>?
) {
    guard let userData, let artifactJSON,
          let data = String(cString: artifactJSON).data(using: .utf8),
          let artifact = try? JSONDecoder().decode(AgentArtifactRecord.self, from: data) else {
        return
    }
    let context = Unmanaged<AgentSessionContext>.fromOpaque(userData).takeUnretainedValue()
    context.enqueueArtifact(artifact)
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
                guard let applicationSupport = fileManager.urls(
                    for: .applicationSupportDirectory,
                    in: .userDomainMask
                ).first else {
                    return nil
                }
                let workspaceURL = fileManager.urls(for: .documentDirectory, in: .userDomainMask).first?
                    .appendingPathComponent("maho-agent-workspace", isDirectory: true)
                    ?? applicationSupport.appendingPathComponent("AgentWorkspace", isDirectory: true)
                let artifactRoot = applicationSupport.appendingPathComponent("Artifacts", isDirectory: true)
                do {
                    try fileManager.createDirectory(at: workspaceURL, withIntermediateDirectories: true)
                    try fileManager.createDirectory(at: artifactRoot, withIntermediateDirectories: true)
                    var resourceValues = URLResourceValues()
                    resourceValues.isExcludedFromBackup = true
                    var mutableArtifactRoot = artifactRoot
                    try mutableArtifactRoot.setResourceValues(resourceValues)
                } catch {
                    return nil
                }

                return FFIString.withCString(workspaceURL.path) { workspacePathPointer -> OpaquePointer? in
                    let context = AgentSessionContext(artifactRoot: artifactRoot)
                    let contextPointer = Unmanaged.passRetained(context).toOpaque()

                    guard let sessionPtr = ffiAgentCreateSessionLeased(
                        corePtr,
                        sessionIdPointer,
                        workspacePathPointer,
                        false, // allowInsecureKeyStorage = false
                        agentPermissionCallback,
                        contextPointer,
                        agentSecureStorageCallback,
                        contextPointer,
                        agentBrowserToolCallback,
                        contextPointer,
                        nil,  // space_id: mobile agent has no space context
                        releaseAgentSessionContext,
                        contextPointer
                    ) else {
                        Unmanaged<AgentSessionContext>.fromOpaque(contextPointer).release()
                        return nil
                    }

                    FFIString.withCString(artifactRoot.path) { artifactRootPointer in
                        ffiAgentSetArtifactRoot(sessionPtr, artifactRootPointer)
                    }
                    ffiAgentSetArtifactCreatedCallback(
                        sessionPtr,
                        onAgentArtifactCreated,
                        contextPointer
                    )
                    context.cancelNativeTurn = { [weak context] in
                        guard let operation = AgentSessionRegistry.shared.acquireOperation(for: sessionPtr),
                              operation.context === context else { return }
                        DispatchQueue.global(qos: .utility).async {
                            _ = ffiAgentCancel(operation.ptr)
                            operation.release()
                        }
                    }
                    AgentSessionRegistry.shared.register(
                        ptr: sessionPtr,
                        state: AgentSessionRegistry.SessionState(context: context, contextPointer: contextPointer)
                    )
#if DEBUG
                    memoryThreadAgentCreated?(sessionPtr)
#endif
                    return sessionPtr
                }
            }
        } ?? nil
    }

    func agentFreeSession(_ ptr: OpaquePointer) {
        AgentSessionRegistry.shared.close(ptr: ptr, nativeFree: ffiAgentSessionFree)
    }

    @discardableResult
    func agentSendMessage(_ ptr: OpaquePointer, message: String) -> Bool {
        guard let operation = AgentSessionRegistry.shared.acquireOperation(for: ptr),
              operation.context.beginTurn() else {
            return false
        }
        let turnContextPointer = Unmanaged.passRetained(operation.context).toOpaque()
        let accepted = FFIString.withCString(message) { messagePointer in
            ffiAgentSendMessageLeased(
                operation.ptr,
                messagePointer,
                onAgentToken,
                onAgentThinking,
                onAgentToolCall,
                onAgentToolResult,
                onAgentComplete,
                onAgentError,
                turnContextPointer,
                releaseAgentTurnContext,
                turnContextPointer
            )
        }
        if !accepted {
            operation.context.endTurn()
            Unmanaged<AgentSessionContext>.fromOpaque(turnContextPointer).release()
        }
        return accepted
    }

    @discardableResult
    func agentCancel(_ ptr: OpaquePointer) -> Bool {
        guard let operation = AgentSessionRegistry.shared.acquireOperation(for: ptr) else {
            return false
        }
        return ffiAgentCancel(operation.ptr)
    }

    func agentListTools(_ ptr: OpaquePointer) -> String? {
        guard let operation = AgentSessionRegistry.shared.acquireOperation(for: ptr) else {
            return nil
        }
        return FFIString.consume(ffiAgentListTools(operation.ptr))
    }

    func agentListArtifacts(_ ptr: OpaquePointer) -> [[String: Any]] {
        guard let operation = AgentSessionRegistry.shared.acquireOperation(for: ptr),
              let json = FFIString.consume(ffiAgentListArtifacts(operation.ptr)),
              let data = json.data(using: .utf8),
              let artifacts = try? JSONDecoder().decode([AgentArtifactRecord].self, from: data) else {
            return []
        }
        return artifacts.map(\.bridgeValue)
    }

    func agentReadArtifact(_ ptr: OpaquePointer, artifactId: String) -> Data? {
        guard !artifactId.isEmpty,
              let operation = AgentSessionRegistry.shared.acquireOperation(for: ptr),
              let relativePath = FFIString.withCString(artifactId, { artifactIdPointer in
                  FFIString.consume(ffiAgentArtifactPath(operation.ptr, artifactIdPointer))
              }) else {
            return nil
        }
        return securelyReadArtifact(root: operation.context.artifactRoot, relativePath: relativePath)
    }

    func agentPollEvent(_ ptr: OpaquePointer) -> String? {
        return AgentSessionRegistry.shared.context(for: ptr)?.dequeue()
    }
}

private func securelyReadArtifact(root: URL, relativePath: String) -> Data? {
    let components = relativePath.split(separator: "/", omittingEmptySubsequences: false)
    guard !relativePath.isEmpty,
          !relativePath.hasPrefix("/"),
          !components.isEmpty,
          components.allSatisfy({ !$0.isEmpty && $0 != "." && $0 != ".." }) else {
        return nil
    }

    let rootDescriptor = open(root.path, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW)
    guard rootDescriptor >= 0 else { return nil }
    defer { close(rootDescriptor) }

    var directoryDescriptor = rootDescriptor
    var ownedDirectoryDescriptor: Int32?
    defer {
        if let ownedDirectoryDescriptor {
            close(ownedDirectoryDescriptor)
        }
    }

    for component in components.dropLast() {
        let nextDescriptor = component.withCString {
            openat(directoryDescriptor, $0, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW)
        }
        guard nextDescriptor >= 0 else { return nil }
        if let ownedDirectoryDescriptor {
            close(ownedDirectoryDescriptor)
        }
        ownedDirectoryDescriptor = nextDescriptor
        directoryDescriptor = nextDescriptor
    }

    guard let fileName = components.last else { return nil }
    let fileDescriptor = fileName.withCString {
        openat(directoryDescriptor, $0, O_RDONLY | O_CLOEXEC | O_NOFOLLOW)
    }
    guard fileDescriptor >= 0 else { return nil }
    defer { close(fileDescriptor) }

    var status = stat()
    guard fstat(fileDescriptor, &status) == 0,
          (status.st_mode & S_IFMT) == S_IFREG,
          status.st_size >= 0,
          status.st_size <= 50 * 1_024 * 1_024 else {
        return nil
    }

    var data = Data()
    data.reserveCapacity(Int(status.st_size))
    var buffer = [UInt8](repeating: 0, count: 64 * 1_024)
    while true {
        let count = buffer.withUnsafeMutableBytes { bytes in
            read(fileDescriptor, bytes.baseAddress, bytes.count)
        }
        if count == 0 { return data }
        if count < 0 {
            if errno == EINTR { continue }
            return nil
        }
        data.append(buffer, count: count)
        if data.count > 50 * 1_024 * 1_024 { return nil }
    }
}
