import Foundation

struct ChatMessage {
    enum Role { case user, assistant }
    let role: Role
    var content: String
    let timestamp: Date
}

protocol AIChatHandlerDelegate: AnyObject {
    func didUpdateMessages()
    func didStartStreaming()
    func didFinishStreaming()
    func aiChatHandlerRequiresConfiguration()
}

final class AIChatHandler {
    weak var delegate: AIChatHandlerDelegate?
    var toolRegistry: MCPToolRegistry?
    private(set) var messages: [ChatMessage] = []
    private(set) var currentConversationId: String?
    private var currentTask: Task<Void, Never>?
    private var referencedPageContexts: [PageContext] = []

    private func ensureConversation() {
        if currentConversationId == nil {
            currentConversationId = ConversationStore.shared.createConversation()
        }
    }

    private func persistUserMessage(_ message: ChatMessage) {
        guard let cid = currentConversationId else { return }
        ConversationStore.shared.saveMessage(conversationId: cid, role: message.role, content: message.content, timestamp: message.timestamp)
        if messages.filter({ $0.role == .user }).count == 1 {
            ConversationStore.shared.autoTitle(conversationId: cid, firstMessage: message.content)
        }
    }

    private func persistAssistantMessage(_ message: ChatMessage) {
        guard let cid = currentConversationId else { return }
        ConversationStore.shared.saveMessage(conversationId: cid, role: message.role, content: message.content, timestamp: message.timestamp)
    }

    func loadConversation(_ id: String) {
        cancel()
        currentConversationId = id
        messages = ConversationStore.shared.loadMessages(conversationId: id)
        delegate?.didUpdateMessages()
    }

    func startNewConversation() {
        cancel()
        currentConversationId = nil
        messages.removeAll()
        delegate?.didUpdateMessages()
    }

    func send(_ text: String, responseFormat: String = "Auto") {
        referencedPageContexts = []
        sendInternal(text, responseFormat: responseFormat)
    }

    func sendWithSkill(_ skill: SkillInfo, userInput: String, responseFormat: String = "Auto") {
        referencedPageContexts = []
        ensureConversation()
        let userMessage = ChatMessage(role: .user, content: "\(skill.slashCommand) \(userInput)", timestamp: Date())
        messages.append(userMessage)
        persistUserMessage(userMessage)
        delegate?.didUpdateMessages()

        currentTask?.cancel()

        guard LLMService.shared.isConfigured else {
            delegate?.aiChatHandlerRequiresConfiguration()
            return
        }

        delegate?.didStartStreaming()
        let systemPrompt = buildSkillPrompt(skill, userInput: userInput, responseFormat: responseFormat)

        let assistantMessage = ChatMessage(role: .assistant, content: "", timestamp: Date())
        messages.append(assistantMessage)
        let assistantIndex = messages.count - 1
        delegate?.didUpdateMessages()

        let llmMessages = TokenBudget.truncateMessages(
            [
                LLMMessage(role: .system, content: systemPrompt),
                LLMMessage(role: .user, content: userInput.isEmpty ? "Please proceed." : userInput),
            ],
            maxTokens: TokenBudget.availableContextTokens
        )

        streamResponse(llmMessages: llmMessages, assistantIndex: assistantIndex)
    }

    func sendWithReferencedPages(_ text: String, referencedPages: [PageContext], responseFormat: String = "Auto") {
        referencedPageContexts = referencedPages
        sendInternal(text, responseFormat: responseFormat)
    }

    private func sendInternal(_ text: String, responseFormat: String) {
        ensureConversation()
        let userMessage = ChatMessage(role: .user, content: text, timestamp: Date())
        messages.append(userMessage)
        persistUserMessage(userMessage)
        delegate?.didUpdateMessages()

        currentTask?.cancel()

        guard LLMService.shared.isConfigured else {
            delegate?.aiChatHandlerRequiresConfiguration()
            return
        }

        delegate?.didStartStreaming()

        let assistantMessage = ChatMessage(role: .assistant, content: "", timestamp: Date())
        messages.append(assistantMessage)
        let assistantIndex = messages.count - 1
        delegate?.didUpdateMessages()

        let systemPrompt = buildSystemPrompt(maxTokens: TokenBudget.availableContextTokens, responseFormat: responseFormat)
        let fullMessages = buildLLMMessages(systemPrompt: systemPrompt)
        let llmMessages = TokenBudget.truncateMessages(fullMessages, maxTokens: TokenBudget.availableContextTokens)

        if AIFeatureFlags.toolCallingEnabled, let registry = toolRegistry, let providerType = LLMService.shared.currentProviderType {
            let tools = registry.toolDefinitionsForLLM(providerType: providerType)
            if !tools.isEmpty {
                let extendedMessages = llmMessages.map { LLMExtendedMessage(from: $0) }
                streamResponseWithTools(extendedMessages: extendedMessages, tools: tools, registry: registry, assistantIndex: assistantIndex)
                return
            }
        }

        streamResponse(llmMessages: llmMessages, assistantIndex: assistantIndex)
    }

    private func responseFormatDirective(for responseFormat: String) -> String? {
        switch responseFormat.lowercased() {
        case "concise":
            return "Keep the response brief, direct, and to the point."
        case "detailed":
            return "Provide a thorough, detailed response with helpful context."
        case "step-by-step":
            return "Respond as a clear numbered step-by-step guide."
        default:
            return nil
        }
    }

    private func buildSystemPrompt(maxTokens: Int = TokenBudget.availableContextTokens, responseFormat: String = "Auto") -> String {
        let hasReferences = !referencedPageContexts.isEmpty
        var basePrompt = hasReferences
            ? "You are a helpful assistant. The user is viewing multiple web pages."
            : "You are a helpful assistant."

        if let responseFormatInstruction = responseFormatDirective(for: responseFormat) {
            basePrompt += "\n\nResponse format: \(responseFormatInstruction)"
        }

        guard hasReferences else {
            return basePrompt
        }

        let baseTokens = TokenBudget.estimateTokens(basePrompt)
        let pageHeaders = referencedPageContexts.enumerated().map { index, ref in
            "\n\nReferenced Page \(index + 1):\nTitle: \(ref.title)\nURL: \(ref.url)\nContent:\n"
        }
        let headerTokens = pageHeaders.reduce(0) { $0 + TokenBudget.estimateTokens($1) }
        let availableContentTokens = max(maxTokens - baseTokens - headerTokens, 0)
        let perPageContentTokens = referencedPageContexts.isEmpty ? 0 : availableContentTokens / referencedPageContexts.count

        var prompt = basePrompt
        for (index, ref) in referencedPageContexts.enumerated() {
            let header = pageHeaders[index]
            var pageContent = ""

            if !ref.selectedText.isEmpty {
                pageContent += "User-selected text: \(ref.selectedText)\n\n"
            }
            if !ref.metaDescription.isEmpty {
                pageContent += "Meta description: \(ref.metaDescription)\n\n"
            }
            if !ref.headings.isEmpty {
                pageContent += "Page structure:\n"
                for h in ref.headings {
                    pageContent += "\(String(repeating: "#", count: h.level)) \(h.text)\n"
                }
                pageContent += "\n"
            }

            let truncatedText = TokenBudget.truncateContext(ref.text, maxTokens: perPageContentTokens)
            pageContent += truncatedText
            prompt += header + pageContent
        }

        return prompt
    }

    private func buildSkillPrompt(_ skill: SkillInfo, userInput: String, responseFormat: String = "Auto") -> String {
        var prompt = "You are a helpful assistant.\n\n"
        if let responseFormatInstruction = responseFormatDirective(for: responseFormat) {
            prompt += "Response format: \(responseFormatInstruction)\n\n"
        }
        prompt += skill.description + "\n\n"
        if !userInput.isEmpty {
            prompt += "User request: \(userInput)"
        }
        return prompt
    }

    func cancel() {
        currentTask?.cancel()
        currentTask = nil
    }

    private func buildLLMMessages(systemPrompt: String) -> [LLMMessage] {
        var llmMessages: [LLMMessage] = [LLMMessage(role: .system, content: systemPrompt)]
        llmMessages.append(contentsOf: messages.compactMap { message in
            switch message.role {
            case .user:
                return LLMMessage(role: .user, content: message.content)
            case .assistant:
                return message.content.isEmpty ? nil : LLMMessage(role: .assistant, content: message.content)
            }
        })
        return llmMessages
    }

    private func streamResponse(llmMessages: [LLMMessage], assistantIndex: Int) {
        currentTask = Task { [weak self] in
            guard let self else { return }
            do {
                let stream = LLMService.shared.streamChat(messages: llmMessages)
                var assistantContent = ""

                for try await chunk in stream {
                    if Task.isCancelled { return }
                    assistantContent += chunk
                    await MainActor.run {
                        guard assistantIndex < self.messages.count else { return }
                        self.messages[assistantIndex].content = assistantContent
                        self.delegate?.didUpdateMessages()
                    }
                }

                guard !Task.isCancelled else { return }
                await MainActor.run {
                    if assistantIndex < self.messages.count {
                        self.persistAssistantMessage(self.messages[assistantIndex])
                    }
                    self.delegate?.didFinishStreaming()
                }
            } catch {
                guard !Task.isCancelled else { return }
                await MainActor.run {
                    if assistantIndex < self.messages.count {
                        self.messages[assistantIndex].content = error.localizedDescription
                        self.delegate?.didUpdateMessages()
                        self.persistAssistantMessage(self.messages[assistantIndex])
                    }
                    self.delegate?.didFinishStreaming()
                }
            }
        }
    }

    private func streamResponseWithTools(extendedMessages: [LLMExtendedMessage], tools: [[String: Any]], registry: MCPToolRegistry, assistantIndex: Int) {
        currentTask = Task { [weak self] in
            guard let self else { return }
            var conversation = extendedMessages
            var assistantContent = ""

            do {
                while !Task.isCancelled {
                    let stream = LLMService.shared.streamChatWithTools(messages: conversation, tools: tools)
                    var pendingToolCalls: [LLMToolCall] = []

                    for try await element in stream {
                        if Task.isCancelled { return }
                        switch element {
                        case .textChunk(let chunk):
                            assistantContent += chunk
                            await MainActor.run {
                                guard assistantIndex < self.messages.count else { return }
                                self.messages[assistantIndex].content = assistantContent
                                self.delegate?.didUpdateMessages()
                            }
                        case .toolCall(let toolCall):
                            pendingToolCalls.append(toolCall)
                        case .toolResult:
                            break
                        }
                    }

                    guard !Task.isCancelled else { return }

                    if pendingToolCalls.isEmpty {
                        await MainActor.run {
                            if assistantIndex < self.messages.count {
                                self.persistAssistantMessage(self.messages[assistantIndex])
                            }
                            self.delegate?.didFinishStreaming()
                        }
                        return
                    }

                    let toolResults = try await executeToolCalls(pendingToolCalls, registry: registry)
                    let assistantMessage = LLMExtendedMessage(
                        role: .assistant,
                        content: assistantContent.isEmpty ? nil : assistantContent,
                        toolCalls: pendingToolCalls
                    )
                    conversation.append(assistantMessage)
                    conversation.append(contentsOf: toolResults.map { LLMExtendedMessage(role: .tool, content: $0.content, toolCallId: $0.toolCallId) })
                }
            } catch {
                guard !Task.isCancelled else { return }
                await MainActor.run {
                    if assistantIndex < self.messages.count {
                        self.messages[assistantIndex].content = error.localizedDescription
                        self.delegate?.didUpdateMessages()
                        self.persistAssistantMessage(self.messages[assistantIndex])
                    }
                    self.delegate?.didFinishStreaming()
                }
            }
        }
    }

    private func executeToolCalls(_ toolCalls: [LLMToolCall], registry: MCPToolRegistry) async throws -> [LLMToolResult] {
        var results: [LLMToolResult] = []
        for toolCall in toolCalls {
            let value = try await registry.callToolAsync(name: toolCall.name, arguments: toolCall.arguments.mapValues { $0.value })
            let data = try JSONSerialization.data(withJSONObject: value)
            let content = String(data: data, encoding: .utf8) ?? "{}"
            results.append(LLMToolResult(toolCallId: toolCall.id, content: content))
        }
        return results
    }
}
