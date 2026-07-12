import Foundation
import Combine

@MainActor
class AiSearchViewModel: ObservableObject {
    let query: String
    let isIncognito: Bool
    
    @Published var responseText: String = ""
    @Published var isLoading: Bool = false
    @Published var errorMessage: String? = nil
    @Published var showByokCta: Bool = false
    
    private let bridge = MahoBridge.shared
    private var sessionPtr: OpaquePointer? = nil
    private var pollTask: Task<Void, Never>? = nil
    
    init(query: String, isIncognito: Bool) {
        self.query = query
        self.isIncognito = isIncognito
    }
    
    deinit {
        // Inlined (not cleanup()): nonisolated deinit cannot call @MainActor methods.
        pollTask?.cancel()
        if let sessionPtr {
            bridge.chatCancel(sessionPtr)
            bridge.freeChatSession(sessionPtr)
        }
    }
    
    func startSearch() {
        guard !query.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty else { return }
        
        cleanup()
        
        if isIncognito {
            errorMessage = "AI Search is disabled in Private mode."
            return
        }
        
        guard let config = AiProviderResolver.resolve() else {
            showByokCta = true
            return
        }
        
        isLoading = true
        errorMessage = nil
        responseText = ""
        
        sessionPtr = bridge.createChatSession(
            apiKey: config.apiKey,
            endpoint: config.endpoint,
            model: config.model,
            systemInstruction: AiSearchSystemPrompt.build(query: query)
        )
        
        guard let sessionPtr = sessionPtr else {
            isLoading = false
            errorMessage = "Failed to create chat session."
            return
        }
        
        startPolling(sessionPtr: sessionPtr)
        
        let success = bridge.chatSendUserTurn(sessionPtr, message: query)
        if !success {
            isLoading = false
            errorMessage = "Failed to initiate AI search request."
            cleanup()
        }
    }
    
    func cancelSearch() {
        cleanup()
        isLoading = false
    }
    
    private func cleanup() {
        pollTask?.cancel()
        pollTask = nil
        if let sessionPtr = sessionPtr {
            bridge.chatCancel(sessionPtr)
            bridge.freeChatSession(sessionPtr)
        }
        sessionPtr = nil
    }
    
    private func startPolling(sessionPtr: OpaquePointer) {
        let bridge = bridge

        pollTask?.cancel()
        pollTask = Task(priority: .utility) { [weak self] in
            while !Task.isCancelled {
                if let eventJSON = bridge.chatPollEvent(sessionPtr) {
                    await MainActor.run {
                        self?.consumeEventJSON(eventJSON)
                    }
                } else {
                    try? await Task.sleep(for: .milliseconds(60))
                }
            }
        }
    }
    
    private func consumeEventJSON(_ eventJSON: String) {
        guard let data = eventJSON.data(using: .utf8),
              let event = try? JSONDecoder().decode(ChatEventEnvelope.self, from: data) else {
            return
        }
        
        switch event.type {
        case "token":
            if case .text(let token) = event.data {
                responseText += token
            }
        case "complete":
            isLoading = false
            pollTask?.cancel()
            pollTask = nil
        case "error":
            if case .text(let msg) = event.data {
                errorMessage = msg
            }
            isLoading = false
            cleanup()
        default:
            break
        }
    }
}

private struct ChatEventEnvelope: Decodable {
    let type: String
    let data: ChatEventPayload
}

private enum ChatEventPayload: Decodable {
    case text(String)
    case completion(ChatCompletionData)
    case unknown

    init(from decoder: Decoder) throws {
        let container = try decoder.singleValueContainer()

        if let stringValue = try? container.decode(String.self) {
            self = .text(stringValue)
            return
        }

        if let completionValue = try? container.decode(ChatCompletionData.self) {
            self = .completion(completionValue)
            return
        }

        self = .unknown
    }
}

private struct ChatCompletionData: Decodable {
    let fullText: String
    let toolCallsJson: String

    enum CodingKeys: String, CodingKey {
        case fullText = "full_text"
        case toolCallsJson = "tool_calls_json"
    }
}
