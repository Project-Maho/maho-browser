import Foundation

extension Notification.Name {
    static let mahoAIConfigurationDidChange = Notification.Name("mahoAIConfigurationDidChange")
}

// MARK: - Types

struct LLMMessage: Codable, Sendable {
    enum Role: String, Codable, Sendable { case system, user, assistant }
    let role: Role
    let content: String
}

enum LLMProviderType: String, CaseIterable {
    case openai = "OpenAI"
    case anthropic = "Anthropic"
    case localServer = "Local Server"
    case remoteServer = "Remote Server"

    static func persistedType(from rawValue: String) -> LLMProviderType? {
        if let provider = Self(rawValue: rawValue) {
            return provider
        }
        if rawValue == "Custom" {
            return .localServer
        }
        return nil
    }
}

enum AIProviderStorageKey {
    static let provider = "maho.ai.provider"
    static let model = "maho.ai.model"
    static let openAIModel = "maho.ai.openai.model"
    static let anthropicModel = "maho.ai.anthropic.model"
    static let localEndpoint = "maho.ai.localServer.endpoint"
    static let localModel = "maho.ai.localServer.model"
    static let remoteEndpoint = "maho.ai.remoteServer.endpoint"
    static let remoteModel = "maho.ai.remoteServer.model"
    static let legacyCustomEndpoint = "maho.ai.custom.endpoint"
    static let legacyCustomModel = "maho.ai.custom.model"
}

struct OpenAICompatibleServerConfig: Sendable {
    let endpoint: URL
    let modelName: String
    let apiKey: String?
}

enum LLMError: Error, LocalizedError {
    case noProvider
    case noAPIKey
    case invalidResponse
    case httpError(Int, String)
    case streamingError(String)
    case cancelled
    
    var errorDescription: String? {
        switch self {
        case .noProvider: return "No LLM provider configured"
        case .noAPIKey: return "API key not set"
        case .invalidResponse: return "Invalid response from LLM"
        case .httpError(let code, let msg): return "HTTP \(code): \(msg)"
        case .streamingError(let msg): return "Streaming error: \(msg)"
        case .cancelled: return "Request cancelled"
        }
    }
}

// MARK: - Provider Protocol

protocol LLMProvider: Sendable {
    func streamChat(messages: [LLMMessage], model: String) -> AsyncThrowingStream<String, Error>
    func completeChat(messages: [LLMMessage], model: String) async throws -> String
    func streamChatWithTools(messages: [LLMExtendedMessage], model: String, tools: [[String: Any]]) -> AsyncThrowingStream<LLMStreamElement, Error>
}

extension LLMProvider {
    func streamChatWithTools(messages: [LLMExtendedMessage], model: String, tools: [[String: Any]]) -> AsyncThrowingStream<LLMStreamElement, Error> {
        let basicMessages = messages.compactMap { msg -> LLMMessage? in
            guard let content = msg.content, !content.isEmpty else { return nil }
            let role: LLMMessage.Role
            switch msg.role {
            case .system: role = .system
            case .user: role = .user
            case .assistant: role = .assistant
            case .tool: return nil
            }
            return LLMMessage(role: role, content: content)
        }
        let textStream = streamChat(messages: basicMessages, model: model)
        return AsyncThrowingStream { continuation in
            Task {
                do {
                    for try await chunk in textStream {
                        continuation.yield(.textChunk(chunk))
                    }
                    continuation.finish()
                } catch {
                    continuation.finish(throwing: error)
                }
            }
        }
    }
}

// MARK: - LLMService

final class LLMService {
    static let shared = LLMService()

    private var provider: LLMProvider?
    private var providerType: LLMProviderType?
    private init() {}
    
    func configure(type: LLMProviderType) {
        self.providerType = type
        guard !isUITesting else {
            self.provider = nil
            postConfigurationChangeNotification()
            return
        }
        switch type {
        case .openai:
            guard let apiKey = apiKey(for: type.rawValue) else {
                self.provider = nil
                postConfigurationChangeNotification()
                return
            }
            self.provider = OpenAIProvider(apiKey: apiKey)
        case .anthropic:
            guard let apiKey = apiKey(for: type.rawValue) else {
                self.provider = nil
                postConfigurationChangeNotification()
                return
            }
            self.provider = AnthropicProvider(apiKey: apiKey)
        case .localServer:
            let apiKey = apiKey(for: type.rawValue)
            guard let config = openAICompatibleServerConfig(
                endpointKey: AIProviderStorageKey.localEndpoint,
                modelKey: AIProviderStorageKey.localModel,
                fallbackToLegacyCustom: true,
                requiresAPIKey: false,
                apiKey: apiKey,
                defaultEndpoint: "http://localhost:11434/v1"
            ) else {
                self.provider = nil
                postConfigurationChangeNotification()
                return
            }
            self.provider = OpenAICompatibleProvider(config: config)
        case .remoteServer:
            let apiKey = apiKey(for: type.rawValue)
            guard let config = openAICompatibleServerConfig(
                endpointKey: AIProviderStorageKey.remoteEndpoint,
                modelKey: AIProviderStorageKey.remoteModel,
                fallbackToLegacyCustom: false,
                requiresAPIKey: true,
                apiKey: apiKey,
                defaultEndpoint: "https://api.example.com/v1"
            ) else {
                self.provider = nil
                postConfigurationChangeNotification()
                return
            }
            self.provider = OpenAICompatibleProvider(config: config)
        }
        postConfigurationChangeNotification()
    }

    private func postConfigurationChangeNotification() {
        NotificationCenter.default.post(name: .mahoAIConfigurationDidChange, object: self)
    }

    private func apiKey(for provider: String) -> String? {
        APIKeyStore.shared.get(for: provider)
    }
    
    var isConfigured: Bool { provider != nil || isUITesting }
    var currentProviderType: LLMProviderType? { providerType }
    
    func streamChat(messages: [LLMMessage], model: String? = nil) -> AsyncThrowingStream<String, Error> {
        if isUITesting {
            return uiTestStreamChat(messages: messages)
        }
        guard let provider = provider else {
            return AsyncThrowingStream { $0.finish(throwing: LLMError.noProvider) }
        }
        let resolvedModel = model ?? defaultModel
        return provider.streamChat(messages: messages, model: resolvedModel)
    }
    
    func completeChat(messages: [LLMMessage], model: String? = nil) async throws -> String {
        if isUITesting {
            return uiTestAssistantResponse(for: messages)
        }
        guard let provider = provider else { throw LLMError.noProvider }
        let resolvedModel = model ?? defaultModel
        return try await provider.completeChat(messages: messages, model: resolvedModel)
    }

    func streamChatWithTools(messages: [LLMExtendedMessage], tools: [[String: Any]], model: String? = nil) -> AsyncThrowingStream<LLMStreamElement, Error> {
        if isUITesting {
            let basicMessages = messages.compactMap { message -> LLMMessage? in
                guard let content = message.content, !content.isEmpty else { return nil }
                switch message.role {
                case .system: return LLMMessage(role: .system, content: content)
                case .user: return LLMMessage(role: .user, content: content)
                case .assistant: return LLMMessage(role: .assistant, content: content)
                case .tool: return nil
                }
            }
            return uiTestStreamChatWithTools(messages: basicMessages)
        }
        guard let provider = provider else {
            return AsyncThrowingStream { $0.finish(throwing: LLMError.noProvider) }
        }
        let resolvedModel = model ?? defaultModel
        return provider.streamChatWithTools(messages: messages, model: resolvedModel, tools: tools)
    }

    private var isUITesting: Bool {
        ProcessInfo.processInfo.arguments.contains("--ui-testing")
    }

    private func uiTestAssistantResponse(for messages: [LLMMessage]) -> String {
        let userMessage = messages.last(where: { $0.role == .user })?.content.trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
        let echoedText = userMessage.isEmpty ? "your message" : userMessage
        return "UI test assistant reply to: \(echoedText)"
    }

    private func uiTestStreamChat(messages: [LLMMessage]) -> AsyncThrowingStream<String, Error> {
        let response = uiTestAssistantResponse(for: messages)
        return AsyncThrowingStream { continuation in
            Task {
                let midpoint = response.index(response.startIndex, offsetBy: max(1, response.count / 2))
                let chunks = [String(response[..<midpoint]), String(response[midpoint...])]
                for (index, chunk) in chunks.enumerated() {
                    if Task.isCancelled { break }
                    continuation.yield(chunk)
                    if index == 0 {
                        try? await Task.sleep(nanoseconds: 50_000_000)
                    }
                }
                continuation.finish()
            }
        }
    }

    private func uiTestStreamChatWithTools(messages: [LLMMessage]) -> AsyncThrowingStream<LLMStreamElement, Error> {
        let response = uiTestAssistantResponse(for: messages)
        return AsyncThrowingStream { continuation in
            Task {
                let midpoint = response.index(response.startIndex, offsetBy: max(1, response.count / 2))
                let chunks = [String(response[..<midpoint]), String(response[midpoint...])]
                for (index, chunk) in chunks.enumerated() {
                    if Task.isCancelled { break }
                    continuation.yield(.textChunk(chunk))
                    if index == 0 {
                        try? await Task.sleep(nanoseconds: 50_000_000)
                    }
                }
                continuation.finish()
            }
        }
    }
    
    private var defaultModel: String {
        switch providerType {
        case .openai:
            return UserDefaults.standard.string(forKey: AIProviderStorageKey.openAIModel)
                ?? UserDefaults.standard.string(forKey: AIProviderStorageKey.model)
                ?? "gpt-4o-mini"
        case .anthropic:
            return UserDefaults.standard.string(forKey: AIProviderStorageKey.anthropicModel)
                ?? UserDefaults.standard.string(forKey: AIProviderStorageKey.model)
                ?? "claude-sonnet-4-20250514"
        case .localServer:
            return UserDefaults.standard.string(forKey: AIProviderStorageKey.localModel)
                ?? UserDefaults.standard.string(forKey: AIProviderStorageKey.legacyCustomModel)
                ?? "default"
        case .remoteServer:
            return UserDefaults.standard.string(forKey: AIProviderStorageKey.remoteModel) ?? "default"
        case .none: return ""
        }
    }

    private func openAICompatibleServerConfig(
        endpointKey: String,
        modelKey: String,
        fallbackToLegacyCustom: Bool,
        requiresAPIKey: Bool,
        apiKey: String?,
        defaultEndpoint: String
    ) -> OpenAICompatibleServerConfig? {
        let defaults = UserDefaults.standard

        let endpointString = defaults.string(forKey: endpointKey)
            ?? (fallbackToLegacyCustom ? defaults.string(forKey: AIProviderStorageKey.legacyCustomEndpoint) : nil)
            ?? defaultEndpoint
        guard let endpoint = URL(string: endpointString) else { return nil }

        let modelName = defaults.string(forKey: modelKey)
            ?? (fallbackToLegacyCustom ? defaults.string(forKey: AIProviderStorageKey.legacyCustomModel) : nil)
        guard let modelName, !modelName.isEmpty else { return nil }

        if requiresAPIKey, (apiKey == nil || apiKey?.isEmpty == true) {
            return nil
        }

        return OpenAICompatibleServerConfig(endpoint: endpoint, modelName: modelName, apiKey: apiKey)
    }
}

// MARK: - OpenAI Provider

struct OpenAIProvider: LLMProvider, Sendable {
    let apiKey: String
    private let baseURL = "https://api.openai.com/v1/chat/completions"
    
    func streamChat(messages: [LLMMessage], model: String) -> AsyncThrowingStream<String, Error> {
        AsyncThrowingStream { continuation in
            let task = Task {
                do {
                    var request = URLRequest(url: URL(string: baseURL)!)
                    request.httpMethod = "POST"
                    request.setValue("Bearer \(apiKey)", forHTTPHeaderField: "Authorization")
                    request.setValue("application/json", forHTTPHeaderField: "Content-Type")
                    
                    let body: [String: Any] = [
                        "model": model,
                        "messages": messages.map { ["role": $0.role.rawValue, "content": $0.content] },
                        "stream": true
                    ]
                    request.httpBody = try JSONSerialization.data(withJSONObject: body)
                    
                    let (bytes, response) = try await URLSession.shared.bytes(for: request)
                    guard let httpResponse = response as? HTTPURLResponse else {
                        throw LLMError.invalidResponse
                    }
                    guard httpResponse.statusCode == 200 else {
                        var errorBody = ""
                        for try await line in bytes.lines { errorBody += line }
                        throw LLMError.httpError(httpResponse.statusCode, errorBody)
                    }
                    
                    for try await line in bytes.lines {
                        if Task.isCancelled { break }
                        guard line.hasPrefix("data: ") else { continue }
                        let data = String(line.dropFirst(6))
                        if data == "[DONE]" { break }
                        guard let jsonData = data.data(using: .utf8),
                              let json = try? JSONSerialization.jsonObject(with: jsonData) as? [String: Any],
                              let choices = json["choices"] as? [[String: Any]],
                              let delta = choices.first?["delta"] as? [String: Any],
                              let content = delta["content"] as? String else { continue }
                        continuation.yield(content)
                    }
                    continuation.finish()
                } catch {
                    continuation.finish(throwing: error)
                }
            }
            continuation.onTermination = { _ in task.cancel() }
        }
    }
    
    func completeChat(messages: [LLMMessage], model: String) async throws -> String {
        var request = URLRequest(url: URL(string: baseURL)!)
        request.httpMethod = "POST"
        request.setValue("Bearer \(apiKey)", forHTTPHeaderField: "Authorization")
        request.setValue("application/json", forHTTPHeaderField: "Content-Type")
        
        let body: [String: Any] = [
            "model": model,
            "messages": messages.map { ["role": $0.role.rawValue, "content": $0.content] },
            "stream": false
        ]
        request.httpBody = try JSONSerialization.data(withJSONObject: body)
        
        let (data, response) = try await URLSession.shared.data(for: request)
        guard let httpResponse = response as? HTTPURLResponse else { throw LLMError.invalidResponse }
        guard httpResponse.statusCode == 200 else {
            let errorBody = String(data: data, encoding: .utf8) ?? "Unknown error"
            throw LLMError.httpError(httpResponse.statusCode, errorBody)
        }
        
        guard let json = try JSONSerialization.jsonObject(with: data) as? [String: Any],
              let choices = json["choices"] as? [[String: Any]],
              let message = choices.first?["message"] as? [String: Any],
              let content = message["content"] as? String else {
            throw LLMError.invalidResponse
        }
        return content.trimmingCharacters(in: .whitespacesAndNewlines)
    }

    func streamChatWithTools(messages: [LLMExtendedMessage], model: String, tools: [[String: Any]]) -> AsyncThrowingStream<LLMStreamElement, Error> {
        AsyncThrowingStream { continuation in
            let task = Task {
                do {
                    var request = URLRequest(url: URL(string: baseURL)!)
                    request.httpMethod = "POST"
                    request.setValue("Bearer \(apiKey)", forHTTPHeaderField: "Authorization")
                    request.setValue("application/json", forHTTPHeaderField: "Content-Type")

                    let body: [String: Any] = [
                        "model": model,
                        "messages": Self.openAIMessages(from: messages),
                        "tools": tools,
                        "tool_choice": "auto",
                        "stream": true
                    ]
                    request.httpBody = try JSONSerialization.data(withJSONObject: body)

                    let (bytes, response) = try await URLSession.shared.bytes(for: request)
                    guard let httpResponse = response as? HTTPURLResponse else {
                        throw LLMError.invalidResponse
                    }
                    guard httpResponse.statusCode == 200 else {
                        var errorBody = ""
                        for try await line in bytes.lines { errorBody += line }
                        throw LLMError.httpError(httpResponse.statusCode, errorBody)
                    }

                    try await Self.parseOpenAIToolStream(bytes: bytes, continuation: continuation)
                    continuation.finish()
                } catch {
                    continuation.finish(throwing: error)
                }
            }
            continuation.onTermination = { _ in task.cancel() }
        }
    }

    static func parseOpenAIToolStream(bytes: URLSession.AsyncBytes, continuation: AsyncThrowingStream<LLMStreamElement, Error>.Continuation) async throws {
        var toolCallAccumulator: [Int: (id: String, name: String, arguments: String)] = [:]

        for try await line in bytes.lines {
            if Task.isCancelled { break }
            guard line.hasPrefix("data: ") else { continue }
            let data = String(line.dropFirst(6))
            if data == "[DONE]" { break }
            guard let jsonData = data.data(using: .utf8),
                  let json = try? JSONSerialization.jsonObject(with: jsonData) as? [String: Any],
                  let choices = json["choices"] as? [[String: Any]],
                  let choice = choices.first else { continue }

            let finishReason = choice["finish_reason"] as? String

            if let delta = choice["delta"] as? [String: Any] {
                if let content = delta["content"] as? String {
                    continuation.yield(.textChunk(content))
                }
                if let toolCalls = delta["tool_calls"] as? [[String: Any]] {
                    for tc in toolCalls {
                        let index = tc["index"] as? Int ?? 0
                        if let id = tc["id"] as? String {
                            let funcInfo = tc["function"] as? [String: Any]
                            toolCallAccumulator[index] = (id: id, name: funcInfo?["name"] as? String ?? "", arguments: funcInfo?["arguments"] as? String ?? "")
                        } else if let funcInfo = tc["function"] as? [String: Any] {
                            if var existing = toolCallAccumulator[index] {
                                if let name = funcInfo["name"] as? String, !name.isEmpty {
                                    existing.name = name
                                }
                                if let args = funcInfo["arguments"] as? String {
                                    existing.arguments += args
                                }
                                toolCallAccumulator[index] = existing
                            }
                        }
                    }
                }
            }

            if finishReason == "tool_calls" || finishReason == "stop" {
                for index in toolCallAccumulator.keys.sorted() {
                    let tc = toolCallAccumulator[index]!
                    let args = parseToolArguments(tc.arguments)
                    continuation.yield(.toolCall(LLMToolCall(id: tc.id, name: tc.name, arguments: args)))
                }
                toolCallAccumulator.removeAll()
            }
        }
    }

    static func openAIMessages(from messages: [LLMExtendedMessage]) -> [[String: Any]] {
        messages.compactMap { msg -> [String: Any]? in
            switch msg.role {
            case .system:
                return ["role": "system", "content": msg.content ?? ""]
            case .user:
                return ["role": "user", "content": msg.content ?? ""]
            case .assistant:
                var dict: [String: Any] = ["role": "assistant"]
                if let toolCalls = msg.toolCalls, !toolCalls.isEmpty {
                    if let content = msg.content, !content.isEmpty {
                        dict["content"] = content
                    } else {
                        dict["content"] = NSNull()
                    }
                    dict["tool_calls"] = toolCalls.map { tc -> [String: Any] in
                        let argsJSON: String
                        if let data = try? JSONSerialization.data(withJSONObject: tc.arguments.mapValues { $0.value }),
                           let str = String(data: data, encoding: .utf8) {
                            argsJSON = str
                        } else {
                            argsJSON = "{}"
                        }
                        return [
                            "id": tc.id,
                            "type": "function",
                            "function": ["name": tc.name, "arguments": argsJSON]
                        ]
                    }
                } else {
                    dict["content"] = msg.content ?? ""
                }
                return dict
            case .tool:
                return ["role": "tool", "tool_call_id": msg.toolCallId ?? "", "content": msg.content ?? ""]
            }
        }
    }

    static func parseToolArguments(_ jsonString: String) -> [String: AnyCodable] {
        guard let data = jsonString.data(using: .utf8),
              let dict = try? JSONSerialization.jsonObject(with: data) as? [String: Any] else {
            return [:]
        }
        return dict.mapValues { AnyCodable($0) }
    }
}

// MARK: - Anthropic Provider

struct AnthropicProvider: LLMProvider, Sendable {
    let apiKey: String
    private let baseURL = "https://api.anthropic.com/v1/messages"
    private let anthropicVersion = "2023-06-01"
    
    func streamChat(messages: [LLMMessage], model: String) -> AsyncThrowingStream<String, Error> {
        let systemMessage = messages.first(where: { $0.role == .system })?.content
        let chatMessages = messages.filter { $0.role != .system }
        
        return AsyncThrowingStream { continuation in
            let task = Task {
                do {
                    var request = URLRequest(url: URL(string: baseURL)!)
                    request.httpMethod = "POST"
                    request.setValue(apiKey, forHTTPHeaderField: "x-api-key")
                    request.setValue(anthropicVersion, forHTTPHeaderField: "anthropic-version")
                    request.setValue("application/json", forHTTPHeaderField: "Content-Type")
                    
                    var body: [String: Any] = [
                        "model": model,
                        "messages": chatMessages.map { ["role": $0.role.rawValue, "content": $0.content] },
                        "max_tokens": 4096,
                        "stream": true
                    ]
                    if let system = systemMessage { body["system"] = system }
                    request.httpBody = try JSONSerialization.data(withJSONObject: body)
                    
                    let (bytes, response) = try await URLSession.shared.bytes(for: request)
                    guard let httpResponse = response as? HTTPURLResponse else {
                        throw LLMError.invalidResponse
                    }
                    guard httpResponse.statusCode == 200 else {
                        var errorBody = ""
                        for try await line in bytes.lines { errorBody += line }
                        throw LLMError.httpError(httpResponse.statusCode, errorBody)
                    }
                    
                    for try await line in bytes.lines {
                        if Task.isCancelled { break }
                        guard line.hasPrefix("data: ") else { continue }
                        let data = String(line.dropFirst(6))
                        guard let jsonData = data.data(using: .utf8),
                              let json = try? JSONSerialization.jsonObject(with: jsonData) as? [String: Any],
                              let type = json["type"] as? String else { continue }
                        
                        if type == "content_block_delta",
                           let delta = json["delta"] as? [String: Any],
                           let text = delta["text"] as? String {
                            continuation.yield(text)
                        } else if type == "message_stop" {
                            break
                        }
                    }
                    continuation.finish()
                } catch {
                    continuation.finish(throwing: error)
                }
            }
            continuation.onTermination = { _ in task.cancel() }
        }
    }
    
    func completeChat(messages: [LLMMessage], model: String) async throws -> String {
        let systemMessage = messages.first(where: { $0.role == .system })?.content
        let chatMessages = messages.filter { $0.role != .system }
        
        var request = URLRequest(url: URL(string: baseURL)!)
        request.httpMethod = "POST"
        request.setValue(apiKey, forHTTPHeaderField: "x-api-key")
        request.setValue(anthropicVersion, forHTTPHeaderField: "anthropic-version")
        request.setValue("application/json", forHTTPHeaderField: "Content-Type")
        
        var body: [String: Any] = [
            "model": model,
            "messages": chatMessages.map { ["role": $0.role.rawValue, "content": $0.content] },
            "max_tokens": 4096
        ]
        if let system = systemMessage { body["system"] = system }
        request.httpBody = try JSONSerialization.data(withJSONObject: body)
        
        let (data, response) = try await URLSession.shared.data(for: request)
        guard let httpResponse = response as? HTTPURLResponse else { throw LLMError.invalidResponse }
        guard httpResponse.statusCode == 200 else {
            let errorBody = String(data: data, encoding: .utf8) ?? "Unknown error"
            throw LLMError.httpError(httpResponse.statusCode, errorBody)
        }
        
        guard let json = try JSONSerialization.jsonObject(with: data) as? [String: Any],
              let content = json["content"] as? [[String: Any]],
              let text = content.first?["text"] as? String else {
            throw LLMError.invalidResponse
        }
        return text.trimmingCharacters(in: .whitespacesAndNewlines)
    }

    func streamChatWithTools(messages: [LLMExtendedMessage], model: String, tools: [[String: Any]]) -> AsyncThrowingStream<LLMStreamElement, Error> {
        let systemMessage = messages.first(where: { $0.role == .system })?.content
        let chatMessages = messages.filter { $0.role != .system }

        return AsyncThrowingStream { continuation in
            let task = Task {
                do {
                    var request = URLRequest(url: URL(string: baseURL)!)
                    request.httpMethod = "POST"
                    request.setValue(apiKey, forHTTPHeaderField: "x-api-key")
                    request.setValue(anthropicVersion, forHTTPHeaderField: "anthropic-version")
                    request.setValue("application/json", forHTTPHeaderField: "Content-Type")

                    var body: [String: Any] = [
                        "model": model,
                        "messages": Self.anthropicMessages(from: chatMessages),
                        "tools": tools,
                        "max_tokens": 4096,
                        "stream": true
                    ]
                    if let system = systemMessage { body["system"] = system }
                    request.httpBody = try JSONSerialization.data(withJSONObject: body)

                    let (bytes, response) = try await URLSession.shared.bytes(for: request)
                    guard let httpResponse = response as? HTTPURLResponse else {
                        throw LLMError.invalidResponse
                    }
                    guard httpResponse.statusCode == 200 else {
                        var errorBody = ""
                        for try await line in bytes.lines { errorBody += line }
                        throw LLMError.httpError(httpResponse.statusCode, errorBody)
                    }

                    var currentToolId = ""
                    var currentToolName = ""
                    var currentToolInput = ""
                    var inToolUse = false

                    for try await line in bytes.lines {
                        if Task.isCancelled { break }
                        guard line.hasPrefix("data: ") else { continue }
                        let data = String(line.dropFirst(6))
                        guard let jsonData = data.data(using: .utf8),
                              let json = try? JSONSerialization.jsonObject(with: jsonData) as? [String: Any],
                              let type = json["type"] as? String else { continue }

                        switch type {
                        case "content_block_start":
                            if let contentBlock = json["content_block"] as? [String: Any],
                               contentBlock["type"] as? String == "tool_use" {
                                currentToolId = contentBlock["id"] as? String ?? ""
                                currentToolName = contentBlock["name"] as? String ?? ""
                                currentToolInput = ""
                                inToolUse = true
                            }
                        case "content_block_delta":
                            if let delta = json["delta"] as? [String: Any] {
                                let deltaType = delta["type"] as? String ?? ""
                                if deltaType == "text_delta", let text = delta["text"] as? String {
                                    continuation.yield(.textChunk(text))
                                } else if deltaType == "input_json_delta", let partial = delta["partial_json"] as? String {
                                    currentToolInput += partial
                                }
                            }
                        case "content_block_stop":
                            if inToolUse {
                                let args = OpenAIProvider.parseToolArguments(currentToolInput)
                                continuation.yield(.toolCall(LLMToolCall(id: currentToolId, name: currentToolName, arguments: args)))
                                inToolUse = false
                            }
                        case "message_stop":
                            break
                        default:
                            break
                        }
                    }
                    continuation.finish()
                } catch {
                    continuation.finish(throwing: error)
                }
            }
            continuation.onTermination = { _ in task.cancel() }
        }
    }

    static func anthropicMessages(from messages: [LLMExtendedMessage]) -> [[String: Any]] {
        var result: [[String: Any]] = []
        var pendingToolResults: [[String: Any]] = []

        for msg in messages {
            switch msg.role {
            case .system:
                continue
            case .user:
                if !pendingToolResults.isEmpty {
                    result.append(["role": "user", "content": pendingToolResults])
                    pendingToolResults = []
                }
                result.append(["role": "user", "content": msg.content ?? ""])
            case .assistant:
                if !pendingToolResults.isEmpty {
                    result.append(["role": "user", "content": pendingToolResults])
                    pendingToolResults = []
                }
                if let toolCalls = msg.toolCalls, !toolCalls.isEmpty {
                    var contentParts: [[String: Any]] = []
                    if let text = msg.content, !text.isEmpty {
                        contentParts.append(["type": "text", "text": text])
                    }
                    for tc in toolCalls {
                        contentParts.append([
                            "type": "tool_use",
                            "id": tc.id,
                            "name": tc.name,
                            "input": tc.arguments.mapValues { $0.value }
                        ])
                    }
                    result.append(["role": "assistant", "content": contentParts])
                } else {
                    result.append(["role": "assistant", "content": msg.content ?? ""])
                }
            case .tool:
                pendingToolResults.append([
                    "type": "tool_result",
                    "tool_use_id": msg.toolCallId ?? "",
                    "content": msg.content ?? ""
                ])
            }
        }

        if !pendingToolResults.isEmpty {
            result.append(["role": "user", "content": pendingToolResults])
        }

        return result
    }
}

// MARK: - Custom Provider (OpenAI-compatible)

struct OpenAICompatibleProvider: LLMProvider, Sendable {
    let config: OpenAICompatibleServerConfig

    func streamChat(messages: [LLMMessage], model: String) -> AsyncThrowingStream<String, Error> {
        let resolvedModel = model.isEmpty ? config.modelName : model
        let completionsURL = config.endpoint.appendingPathComponent("chat/completions")
        return AsyncThrowingStream { continuation in
            let task = Task {
                do {
                    var request = URLRequest(url: completionsURL)
                    request.httpMethod = "POST"
                    if let apiKey = config.apiKey, !apiKey.isEmpty {
                        request.setValue("Bearer \(apiKey)", forHTTPHeaderField: "Authorization")
                    }
                    request.setValue("application/json", forHTTPHeaderField: "Content-Type")

                    let body: [String: Any] = [
                        "model": resolvedModel,
                        "messages": messages.map { ["role": $0.role.rawValue, "content": $0.content] },
                        "stream": true
                    ]
                    request.httpBody = try JSONSerialization.data(withJSONObject: body)

                    let (bytes, response) = try await URLSession.shared.bytes(for: request)
                    guard let httpResponse = response as? HTTPURLResponse else {
                        throw LLMError.invalidResponse
                    }
                    guard httpResponse.statusCode == 200 else {
                        var errorBody = ""
                        for try await line in bytes.lines { errorBody += line }
                        throw LLMError.httpError(httpResponse.statusCode, errorBody)
                    }

                    for try await line in bytes.lines {
                        if Task.isCancelled { break }
                        guard line.hasPrefix("data: ") else { continue }
                        let data = String(line.dropFirst(6))
                        if data == "[DONE]" { break }
                        guard let jsonData = data.data(using: .utf8),
                              let json = try? JSONSerialization.jsonObject(with: jsonData) as? [String: Any],
                              let choices = json["choices"] as? [[String: Any]],
                              let delta = choices.first?["delta"] as? [String: Any],
                              let content = delta["content"] as? String else { continue }
                        continuation.yield(content)
                    }
                    continuation.finish()
                } catch {
                    continuation.finish(throwing: error)
                }
            }
            continuation.onTermination = { _ in task.cancel() }
        }
    }

    func completeChat(messages: [LLMMessage], model: String) async throws -> String {
        let resolvedModel = model.isEmpty ? config.modelName : model
        let completionsURL = config.endpoint.appendingPathComponent("chat/completions")
        var request = URLRequest(url: completionsURL)
        request.httpMethod = "POST"
        if let apiKey = config.apiKey, !apiKey.isEmpty {
            request.setValue("Bearer \(apiKey)", forHTTPHeaderField: "Authorization")
        }
        request.setValue("application/json", forHTTPHeaderField: "Content-Type")

        let body: [String: Any] = [
            "model": resolvedModel,
            "messages": messages.map { ["role": $0.role.rawValue, "content": $0.content] },
            "stream": false
        ]
        request.httpBody = try JSONSerialization.data(withJSONObject: body)

        let (data, response) = try await URLSession.shared.data(for: request)
        guard let httpResponse = response as? HTTPURLResponse else { throw LLMError.invalidResponse }
        guard httpResponse.statusCode == 200 else {
            let errorBody = String(data: data, encoding: .utf8) ?? "Unknown error"
            throw LLMError.httpError(httpResponse.statusCode, errorBody)
        }

        guard let json = try JSONSerialization.jsonObject(with: data) as? [String: Any],
              let choices = json["choices"] as? [[String: Any]],
              let message = choices.first?["message"] as? [String: Any],
              let content = message["content"] as? String else {
            throw LLMError.invalidResponse
        }
        return content.trimmingCharacters(in: .whitespacesAndNewlines)
    }

    func streamChatWithTools(messages: [LLMExtendedMessage], model: String, tools: [[String: Any]]) -> AsyncThrowingStream<LLMStreamElement, Error> {
        let resolvedModel = model.isEmpty ? config.modelName : model
        let completionsURL = config.endpoint.appendingPathComponent("chat/completions")
        return AsyncThrowingStream { continuation in
            let task = Task {
                do {
                    var request = URLRequest(url: completionsURL)
                    request.httpMethod = "POST"
                    if let apiKey = config.apiKey, !apiKey.isEmpty {
                        request.setValue("Bearer \(apiKey)", forHTTPHeaderField: "Authorization")
                    }
                    request.setValue("application/json", forHTTPHeaderField: "Content-Type")

                    let body: [String: Any] = [
                        "model": resolvedModel,
                        "messages": OpenAIProvider.openAIMessages(from: messages),
                        "tools": tools,
                        "tool_choice": "auto",
                        "stream": true
                    ]
                    request.httpBody = try JSONSerialization.data(withJSONObject: body)

                    let (bytes, response) = try await URLSession.shared.bytes(for: request)
                    guard let httpResponse = response as? HTTPURLResponse else {
                        throw LLMError.invalidResponse
                    }
                    guard httpResponse.statusCode == 200 else {
                        var errorBody = ""
                        for try await line in bytes.lines { errorBody += line }
                        throw LLMError.httpError(httpResponse.statusCode, errorBody)
                    }

                    try await OpenAIProvider.parseOpenAIToolStream(bytes: bytes, continuation: continuation)
                    continuation.finish()
                } catch {
                    continuation.finish(throwing: error)
                }
            }
            continuation.onTermination = { _ in task.cancel() }
        }
    }
}
