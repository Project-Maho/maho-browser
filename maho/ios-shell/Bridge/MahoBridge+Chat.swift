import Foundation

@_silgen_name("maho_chat_send_image")
private func maho_chat_send_image(
    _ ptr: OpaquePointer?,
    _ mime: UnsafePointer<CChar>?,
    _ data: UnsafePointer<UInt8>?,
    _ dataLen: UInt
) -> Bool

@_silgen_name("maho_chat_send_text_with_image")
private func maho_chat_send_text_with_image(
    _ ptr: OpaquePointer?,
    _ text: UnsafePointer<CChar>?,
    _ mime: UnsafePointer<CChar>?,
    _ data: UnsafePointer<UInt8>?,
    _ dataLen: UInt
) -> Bool

private final class ChatSessionEventBuffer {
    private let lock = NSLock()
    private var events: [String] = []

    func enqueue(_ event: String) {
        lock.lock()
        events.append(event)
        lock.unlock()
    }

    func dequeue() -> String? {
        lock.lock()
        defer { lock.unlock() }

        guard !events.isEmpty else {
            return nil
        }

        return events.removeFirst()
    }
}

private final class ChatSessionEventContext {
    let buffer: ChatSessionEventBuffer

    init(buffer: ChatSessionEventBuffer) {
        self.buffer = buffer
    }
}

private final class ChatSessionEventRegistry {
    static let shared = ChatSessionEventRegistry()

    private struct State {
        let buffer: ChatSessionEventBuffer
        let contextPointer: UnsafeMutableRawPointer
    }

    private let lock = NSLock()
    private var states: [UnsafeMutableRawPointer: State] = [:]

    private init() {}

    func register(_ sessionPtr: OpaquePointer) -> UnsafeMutableRawPointer {
        let key = UnsafeMutableRawPointer(sessionPtr)
        let buffer = ChatSessionEventBuffer()
        let context = ChatSessionEventContext(buffer: buffer)
        let contextPointer = Unmanaged.passRetained(context).toOpaque()

        lock.lock()
        states[key] = State(buffer: buffer, contextPointer: contextPointer)
        lock.unlock()

        return contextPointer
    }

    func buffer(for sessionPtr: OpaquePointer) -> ChatSessionEventBuffer? {
        let key = UnsafeMutableRawPointer(sessionPtr)

        lock.lock()
        defer { lock.unlock() }
        return states[key]?.buffer
    }

    func unregister(_ sessionPtr: OpaquePointer) {
        let key = UnsafeMutableRawPointer(sessionPtr)

        lock.lock()
        let removed = states.removeValue(forKey: key)
        lock.unlock()

        guard let removed else {
            return
        }

        Unmanaged<ChatSessionEventContext>.fromOpaque(removed.contextPointer).release()
    }
}

private func makeChatEventJSON(type: String, data: Any) -> String? {
    guard JSONSerialization.isValidJSONObject(["type": type, "data": data]) else {
        return nil
    }

    guard let jsonData = try? JSONSerialization.data(withJSONObject: ["type": type, "data": data]),
          let jsonString = String(data: jsonData, encoding: .utf8) else {
        return nil
    }

    return jsonString
}

private func enqueueChatToken(_ userData: UnsafeMutableRawPointer?, _ token: UnsafePointer<CChar>?) {
    guard let userData,
          let token,
          let event = makeChatEventJSON(type: "token", data: String(cString: token)) else {
        return
    }

    let context = Unmanaged<ChatSessionEventContext>.fromOpaque(userData).takeUnretainedValue()
    context.buffer.enqueue(event)
}

private func enqueueChatThinking(_ userData: UnsafeMutableRawPointer?, _ thinking: UnsafePointer<CChar>?) {
    guard let userData,
          let thinking,
          let event = makeChatEventJSON(type: "thinking", data: String(cString: thinking)) else {
        return
    }

    let context = Unmanaged<ChatSessionEventContext>.fromOpaque(userData).takeUnretainedValue()
    context.buffer.enqueue(event)
}

private func enqueueChatCompletion(
    _ userData: UnsafeMutableRawPointer?,
    _ fullText: UnsafePointer<CChar>?,
    _ toolCallsJSON: UnsafePointer<CChar>?
) {
    guard let userData else {
        return
    }

    let completionPayload: [String: String] = [
        "full_text": fullText.map { String(cString: $0) } ?? "",
        "tool_calls_json": toolCallsJSON.map { String(cString: $0) } ?? ""
    ]

    guard let event = makeChatEventJSON(type: "complete", data: completionPayload) else {
        return
    }

    let context = Unmanaged<ChatSessionEventContext>.fromOpaque(userData).takeUnretainedValue()
    context.buffer.enqueue(event)
}

private func enqueueChatError(_ userData: UnsafeMutableRawPointer?, _ error: UnsafePointer<CChar>?) {
    guard let userData,
          let error,
          let event = makeChatEventJSON(type: "error", data: String(cString: error)) else {
        return
    }

    let context = Unmanaged<ChatSessionEventContext>.fromOpaque(userData).takeUnretainedValue()
    context.buffer.enqueue(event)
}

extension MahoBridge {
    func createChatSession(apiKey: String, endpoint: String, model: String, systemInstruction: String) -> OpaquePointer? {
        return FFIString.withCString(apiKey) { apiKeyPointer -> OpaquePointer? in
            return FFIString.withCString(endpoint) { endpointPointer -> OpaquePointer? in
                return FFIString.withCString(model) { modelPointer -> OpaquePointer? in
                    return FFIString.withCString(systemInstruction) { instructionPointer -> OpaquePointer? in
                        var config = MahoChatConfig(
                            api_key: apiKeyPointer,
                            endpoint: endpointPointer,
                            model: modelPointer,
                            system_instruction: instructionPointer
                        )

                        guard let sessionPointer = maho_core_chat_session_new(&config) else {
                            return nil
                        }

                        ChatSessionRegistry.shared.register(ptr: sessionPointer)

                        let contextPointer = ChatSessionEventRegistry.shared.register(sessionPointer)

                        maho_core_chat_set_event_sink(
                            sessionPointer,
                            enqueueChatToken,
                            enqueueChatThinking,
                            enqueueChatCompletion,
                            enqueueChatError,
                            contextPointer
                        )

                        return sessionPointer
                    }
                }
            }
        }
    }

    func freeChatSession(_ ptr: OpaquePointer) {
        ChatSessionRegistry.shared.releaseByPointer(ptr)
        ChatSessionEventRegistry.shared.unregister(ptr)
    }

    @discardableResult
    func chatSendUserTurn(_ ptr: OpaquePointer, message: String) -> Bool {
        return FFIString.withCString(message) { messagePointer in
            maho_core_chat_send_user_turn(ptr, messagePointer)
        }
    }

    @discardableResult
    func chatSendImage(_ session: OpaquePointer, mime: String, data: Data) -> Bool {
        return FFIString.withCString(mime) { mimePointer in
            data.withUnsafeBytes { rawBuffer in
                guard let baseAddress = rawBuffer.baseAddress else { return false }
                return maho_chat_send_image(
                    session,
                    mimePointer,
                    baseAddress.assumingMemoryBound(to: UInt8.self),
                    UInt(data.count)
                )
            }
        }
    }

    @discardableResult
    func chatSendTextWithImage(_ session: OpaquePointer, text: String, mime: String, data: Data) -> Bool {
        return FFIString.withCString(text) { textPointer in
            FFIString.withCString(mime) { mimePointer in
                data.withUnsafeBytes { rawBuffer in
                    guard let baseAddress = rawBuffer.baseAddress else { return false }
                    return maho_chat_send_text_with_image(
                        session,
                        textPointer,
                        mimePointer,
                        baseAddress.assumingMemoryBound(to: UInt8.self),
                        UInt(data.count)
                    )
                }
            }
        }
    }

    func chatPollEvent(_ ptr: OpaquePointer) -> String? {
        ChatSessionEventRegistry.shared.buffer(for: ptr)?.dequeue()
    }

    func chatCancel(_ ptr: OpaquePointer) {
        maho_core_chat_cancel(ptr)
    }

    func chatAppendUserMessage(_ ptr: OpaquePointer, content: String) {
        FFIString.withCString(content) { contentPointer in
            maho_core_chat_append_user_message(ptr, contentPointer)
        }
    }

    func chatAppendAssistantMessage(_ ptr: OpaquePointer, content: String, toolCallsJson: String) {
        FFIString.withCString(content) { contentPointer in
            FFIString.withCString(toolCallsJson) { toolCallsPointer in
                maho_core_chat_append_assistant_message(ptr, contentPointer, toolCallsPointer)
            }
        }
    }


    @discardableResult
    func chatRegisterTool(_ ptr: OpaquePointer, name: String, description: String, schemaJson: String) -> Bool {
        FFIString.withCString(name) { namePointer in
            FFIString.withCString(description) { descPointer in
                FFIString.withCString(schemaJson) { schemaPointer in
                    maho_core_chat_register_tool(ptr, namePointer, descPointer, schemaPointer)
                }
            }
        }
    }

    @discardableResult
    func chatSendToolResult(_ ptr: OpaquePointer, toolCallId: String, toolName: String, result: String, trigger: Bool) -> Bool {
        FFIString.withCString(toolCallId) { idPointer in
            FFIString.withCString(toolName) { namePointer in
                FFIString.withCString(result) { resultPointer in
                    maho_core_chat_send_tool_result(ptr, idPointer, namePointer, resultPointer, trigger)
                }
            }
        }
    }
}
