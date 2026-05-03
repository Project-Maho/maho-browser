import Foundation

// MARK: - Feature Flag

enum AIFeatureFlags {
    /// Controls whether LLM tool calling is enabled for the AI panel.
    static let toolCallingEnabled = true
}

// MARK: - Tool Calling Types

/// Simple type-erased Codable wrapper for JSON values.
struct AnyCodable: Codable {
    let value: Any
    
    init(_ value: Any) {
        self.value = value
    }
    
    init(from decoder: Decoder) throws {
        let container = try decoder.singleValueContainer()
        if let str = try? container.decode(String.self) {
            value = str
        } else if let bool = try? container.decode(Bool.self) {
            // Bool must be decoded before Int/Double because Foundation's JSONDecoder
            // bridges NSNumber for both, so JSON `true` would decode as Int(1).
            value = bool
        } else if let int = try? container.decode(Int.self) {
            value = int
        } else if let double = try? container.decode(Double.self) {
            value = double
        } else if let dict = try? container.decode([String: AnyCodable].self) {
            value = dict.mapValues { $0.value }
        } else if let array = try? container.decode([AnyCodable].self) {
            value = array.map { $0.value }
        } else {
            value = NSNull()
        }
    }
    
    func encode(to encoder: Encoder) throws {
        var container = encoder.singleValueContainer()
        switch value {
        case let str as String:
            try container.encode(str)
        case let bool as Bool:
            try container.encode(bool)
        case let int as Int:
            try container.encode(int)
        case let double as Double:
            try container.encode(double)
        case let dict as [String: Any]:
            try container.encode(dict.mapValues { AnyCodable($0) })
        case let array as [Any]:
            try container.encode(array.map { AnyCodable($0) })
        default:
            try container.encodeNil()
        }
    }
}

/// A tool call request from the LLM.
struct LLMToolCall: Codable {
    let id: String
    let name: String
    let arguments: [String: AnyCodable]
}

/// Result of executing a tool call.
struct LLMToolResult: Codable {
    let toolCallId: String
    let content: String
    let isError: Bool
    
    init(toolCallId: String, content: String, isError: Bool = false) {
        self.toolCallId = toolCallId
        self.content = content
        self.isError = isError
    }
}

/// A stream element that can be either a text chunk or a tool call.
enum LLMStreamElement {
    case textChunk(String)
    case toolCall(LLMToolCall)
    case toolResult(LLMToolResult)
}

/// Extended message role that includes tool-related roles.
enum LLMExtendedRole: String, Codable {
    case system
    case user
    case assistant
    case tool
}

/// Extended message that supports tool calls and tool results in multi-turn conversations.
struct LLMExtendedMessage: Codable {
    let role: LLMExtendedRole
    let content: String?
    let toolCalls: [LLMToolCall]?
    let toolCallId: String?
    
    init(role: LLMExtendedRole, content: String?, toolCalls: [LLMToolCall]? = nil, toolCallId: String? = nil) {
        self.role = role
        self.content = content
        self.toolCalls = toolCalls
        self.toolCallId = toolCallId
    }
    
    /// Convert from basic LLMMessage.
    init(from message: LLMMessage) {
        switch message.role {
        case .system:
            self.role = .system
        case .user:
            self.role = .user
        case .assistant:
            self.role = .assistant
        }
        self.content = message.content
        self.toolCalls = nil
        self.toolCallId = nil
    }
}
