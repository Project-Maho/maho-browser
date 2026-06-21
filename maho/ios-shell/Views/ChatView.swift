import SwiftUI
import LucideIcons

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

private struct ChatProviderConfig {
    let providerLabel: String
    let apiKey: String
    let endpoint: String
    let model: String
}

struct ChatImageAttachment: Equatable {
    enum Source: Equatable {
        case camera
        case screenshot

        var icon: UIImage {
            switch self {
            case .camera:
                Lucide.camera
            case .screenshot:
                Lucide.scanLine
            }
        }

        var label: String {
            switch self {
            case .camera:
                "Camera"
            case .screenshot:
                "Screenshot"
            }
        }

        var persistenceLabel: String {
            switch self {
            case .camera:
                "[Camera image]"
            case .screenshot:
                "[Page screenshot]"
            }
        }
    }

    let data: Data
    let mimeType: String
    let source: Source
}

struct ChatMessage: Identifiable, Equatable {
    enum Content: Equatable {
        case text(String)
        case image(ChatImageAttachment)
        case mixed(text: String, attachment: ChatImageAttachment)
    }

    enum Role {
        case user
        case assistant
    }

    let id: UUID
    let role: Role
    var content: Content

    init(id: UUID = UUID(), role: Role, text: String) {
        self.id = id
        self.role = role
        self.content = .text(text)
    }

    init(id: UUID = UUID(), role: Role, attachment: ChatImageAttachment, text: String? = nil) {
        self.id = id
        self.role = role

        if let text, !text.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
            self.content = .mixed(text: text, attachment: attachment)
        } else {
            self.content = .image(attachment)
        }
    }

    var isUser: Bool {
        role == .user
    }

    var text: String {
        get {
            switch content {
            case .text(let text):
                return text
            case .image:
                return ""
            case .mixed(let text, _):
                return text
            }
        }

        set {
            switch content {
            case .text:
                content = .text(newValue)
            case .image(let attachment), .mixed(_, let attachment):
                if newValue.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
                    content = .image(attachment)
                } else {
                    content = .mixed(text: newValue, attachment: attachment)
                }
            }
        }
    }

    var attachment: ChatImageAttachment? {
        switch content {
        case .text:
            return nil
        case .image(let attachment):
            return attachment
        case .mixed(_, let attachment):
            return attachment
        }
    }

    init(turn: ConversationTurn) {
        self.init(role: turn.role == "user" ? .user : .assistant, text: turn.content)
    }
}

@MainActor
final class ChatViewModel: ObservableObject {
    @Published var messages: [ChatMessage] = []
    @Published var isLoading = false
    @Published var errorMessage: String?
    @Published var composerText = ""

    private let bridge: MahoBridge
    private let pageUrl: String
    private let pageTitle: String
    private let pageText: String
    private let initialSessionId: String?
    private var sessionPtr: OpaquePointer?
    private var pollTask: Task<Void, Never>?
    private var streamingMessageID: UUID?
    private var conversationSessionId: String?
    private var resumedTurns: [ConversationTurn] = []

    private(set) var providerLabel = "AI"
    private(set) var modelName = ""

    init(
        bridge: MahoBridge = .shared,
        pageUrl: String,
        pageTitle: String,
        pageText: String,
        sessionId: String? = nil
    ) {
        self.bridge = bridge
        self.pageUrl = pageUrl
        self.pageTitle = pageTitle
        self.pageText = pageText
        self.initialSessionId = sessionId
        restoreConversationIfNeeded()
        ensureConversationRecord()
        initializeSession()
    }

    deinit {
        pollTask?.cancel()

        if let sessionPtr {
            bridge.chatCancel(sessionPtr)
            bridge.freeChatSession(sessionPtr)
        }
    }

    var canSend: Bool {
        hasSession && !isLoading && !composerText.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty
    }

    var canStartAttachmentFlow: Bool {
        hasSession && !isLoading
    }

    var hasSession: Bool {
        sessionPtr != nil
    }

    var contextTitle: String {
        let trimmedTitle = pageTitle.trimmingCharacters(in: .whitespacesAndNewlines)
        if !trimmedTitle.isEmpty {
            return trimmedTitle
        }

        if let pageHost {
            return pageHost
        }

        return "Current page"
    }

    var contextSubtitle: String {
        if let pageHost {
            return pageHost
        }

        return hasSession ? "Ask about what you’re reading." : "Add a provider key to begin."
    }

    var pageHost: String? {
        URLComponents(string: pageUrl)?.host
    }

    var sessionLabel: String {
        modelName.isEmpty ? providerLabel : "\(providerLabel) · \(modelName)"
    }

    var composerPlaceholder: String {
        hasSession ? "Ask about this page…" : "Add an API key in Settings to start chatting"
    }

    func sendCurrentMessage() {
        guard let text = currentComposerPrompt() else { return }

        composerText = ""
        send(text)
    }

    @discardableResult
    func sendImage(
        _ imageData: Data,
        source: ChatImageAttachment.Source,
        prompt: String? = nil,
        clearComposer: Bool = false
    ) -> Bool {
        guard let sessionPtr else {
            errorMessage = "Chat session is unavailable."
            return false
        }

        guard !isLoading else {
            return false
        }

        let normalizedPrompt = normalizedText(prompt)
        if clearComposer {
            composerText = ""
        }

        errorMessage = nil

        let attachment = ChatImageAttachment(
            data: imageData,
            mimeType: "image/jpeg",
            source: source
        )

        messages.append(ChatMessage(role: .user, attachment: attachment, text: normalizedPrompt))

        let assistantID = UUID()
        messages.append(ChatMessage(id: assistantID, role: .assistant, text: ""))
        streamingMessageID = assistantID
        isLoading = true

        let didSend: Bool
        if let normalizedPrompt {
            didSend = bridge.chatSendTextWithImage(
                sessionPtr,
                text: normalizedPrompt,
                mime: attachment.mimeType,
                data: imageData
            )
        } else {
            didSend = bridge.chatSendImage(sessionPtr, mime: attachment.mimeType, data: imageData)
        }

        guard didSend else {
            isLoading = false
            streamingMessageID = nil
            errorMessage = "Failed to send image."
            messages.removeAll { $0.id == assistantID }
            return false
        }

        persistConversationMessage(
            role: "user",
            content: persistedContent(for: attachment, prompt: normalizedPrompt),
            urlContext: pageURLContext
        )
        return true
    }

    func cancelStreaming() {
        guard let sessionPtr else { return }

        bridge.chatCancel(sessionPtr)
        removeEmptyStreamingPlaceholderIfNeeded()
        isLoading = false
        streamingMessageID = nil
    }

    private func initializeSession() {
        guard let config = preferredProviderConfig() else {
            errorMessage = "Add an OpenAI or Anthropic API key in Settings → Max AI → LLM API Keys."
            return
        }

        providerLabel = config.providerLabel
        modelName = config.model
        sessionPtr = bridge.createChatSession(
            apiKey: config.apiKey,
            endpoint: config.endpoint,
            model: config.model,
            systemInstruction: buildSystemInstruction(priorTranscript: resumedConversationTranscript)
        )

        guard let sessionPtr else {
            errorMessage = "Failed to create chat session."
            return
        }

        startPolling(sessionPtr: sessionPtr)
    }

    private func preferredProviderConfig() -> ChatProviderConfig? {
        let openAIKey = bridge.byokGetKey(provider: "openai")?.trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
        if !openAIKey.isEmpty {
            return ChatProviderConfig(
                providerLabel: "OpenAI",
                apiKey: openAIKey,
                endpoint: "https://api.openai.com/v1",
                model: "gpt-4o"
            )
        }

        let anthropicKey = bridge.byokGetKey(provider: "anthropic")?.trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
        if !anthropicKey.isEmpty {
            return ChatProviderConfig(
                providerLabel: "Anthropic",
                apiKey: anthropicKey,
                endpoint: "https://api.anthropic.com/v1",
                model: "claude-sonnet-4-20250514"
            )
        }

        return nil
    }

    private func buildSystemInstruction() -> String {
        buildSystemInstruction(priorTranscript: nil)
    }

    private func buildSystemInstruction(priorTranscript: String?) -> String {
        let trimmedTitle = pageTitle.trimmingCharacters(in: .whitespacesAndNewlines)
        let trimmedURL = pageUrl.trimmingCharacters(in: .whitespacesAndNewlines)
        let trimmedText = pageText.trimmingCharacters(in: .whitespacesAndNewlines)

        var instruction = "You are a helpful AI assistant inside Maho Browser. Keep answers concise, factual, and grounded in the current page when relevant."

        if !trimmedTitle.isEmpty || !trimmedURL.isEmpty || !trimmedText.isEmpty {
            instruction += "\n\nCurrent page context:"

            if !trimmedTitle.isEmpty {
                instruction += "\nTitle: \(trimmedTitle)"
            }

            if !trimmedURL.isEmpty {
                instruction += "\nURL: \(trimmedURL)"
            }

            if !trimmedText.isEmpty {
                instruction += "\nContent:\n\(trimmedText)"
            }
        }

        if let priorTranscript, !priorTranscript.isEmpty {
            instruction += "\n\nPrevious conversation transcript:\n\(priorTranscript)\n\nContinue this conversation naturally when the earlier context is relevant."
        }

        return instruction
    }

    private func restoreConversationIfNeeded() {
        guard let initialSessionId else {
            return
        }

        conversationSessionId = initialSessionId
        resumedTurns = bridge.getConversationMessages(sessionId: initialSessionId)
        messages = resumedTurns.map(ChatMessage.init(turn:))
    }

    private func ensureConversationRecord() {
        guard conversationSessionId == nil else {
            return
        }

        let freshConversationId = UUID().uuidString
        if bridge.createConversation(id: freshConversationId, title: nil, spaceId: nil, model: nil) {
            conversationSessionId = freshConversationId
        }
    }

    private var resumedConversationTranscript: String {
        resumedTurns
            .suffix(12)
            .map { turn in
                let speaker = turn.role == "user" ? "User" : "Assistant"
                let cleanedContent = turn.content.trimmingCharacters(in: .whitespacesAndNewlines)
                let clippedContent = cleanedContent.count > 1200
                    ? String(cleanedContent.prefix(1197)) + "..."
                    : cleanedContent
                return "\(speaker): \(clippedContent)"
            }
            .joined(separator: "\n\n")
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
        handleEventJSON(eventJSON)
    }

    private func send(_ text: String) {
        guard let sessionPtr else {
            errorMessage = "Chat session is unavailable."
            return
        }

        errorMessage = nil
        messages.append(ChatMessage(role: .user, text: text))

        let assistantID = UUID()
        messages.append(ChatMessage(id: assistantID, role: .assistant, text: ""))
        streamingMessageID = assistantID
        isLoading = true

        guard bridge.chatSendUserTurn(sessionPtr, message: text) else {
            isLoading = false
            streamingMessageID = nil
            errorMessage = "Failed to send message."
            messages.removeAll { $0.id == assistantID }
            return
        }

        persistConversationMessage(role: "user", content: text, urlContext: pageURLContext)
    }

    private func handleEventJSON(_ eventJSON: String) {
        guard let data = eventJSON.data(using: .utf8),
              let event = try? JSONDecoder().decode(ChatEventEnvelope.self, from: data) else {
            return
        }

        switch event.type {
        case "token":
            if case .text(let token) = event.data {
                appendToken(token)
            }
        case "complete":
            if case .completion(let completion) = event.data {
                completeStreaming(with: completion)
            }
        case "error":
            if case .text(let message) = event.data {
                failStreaming(message)
            }
        default:
            break
        }
    }

    private func appendToken(_ token: String) {
        guard let streamingMessageID,
              let index = messages.firstIndex(where: { $0.id == streamingMessageID }) else {
            return
        }

        var message = messages[index]
        message.text += token
        messages[index] = message
    }

    private func completeStreaming(with completion: ChatCompletionData) {
        var assistantText = completion.fullText

        if let streamingMessageID,
           let index = messages.firstIndex(where: { $0.id == streamingMessageID }) {
            let bufferedText = messages[index].text

            if !completion.fullText.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
                var message = messages[index]
                message.text = completion.fullText
                messages[index] = message
                assistantText = completion.fullText
            } else {
                assistantText = bufferedText
            }
        }

        if let sessionPtr,
           !assistantText.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty || !completion.toolCallsJson.isEmpty {
            bridge.chatAppendAssistantMessage(sessionPtr, content: assistantText, toolCallsJson: completion.toolCallsJson)
        }

        persistConversationMessage(role: "assistant", content: assistantText)

        isLoading = false
        streamingMessageID = nil
    }

    private func failStreaming(_ message: String) {
        errorMessage = message
        removeEmptyStreamingPlaceholderIfNeeded()
        isLoading = false
        streamingMessageID = nil
    }

    private func removeEmptyStreamingPlaceholderIfNeeded() {
        guard let streamingMessageID,
              let index = messages.firstIndex(where: { $0.id == streamingMessageID }) else {
            return
        }

        if messages[index].text.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
            messages.remove(at: index)
        }
    }

    private var pageURLContext: String? {
        let trimmedURL = pageUrl.trimmingCharacters(in: .whitespacesAndNewlines)
        return trimmedURL.isEmpty ? nil : trimmedURL
    }

    private func persistConversationMessage(role: String, content: String, urlContext: String? = nil) {
        let trimmedContent = content.trimmingCharacters(in: .whitespacesAndNewlines)
        guard let conversationSessionId, !trimmedContent.isEmpty else {
            return
        }

        _ = bridge.saveConversationMessage(
            sessionId: conversationSessionId,
            role: role,
            content: trimmedContent,
            urlContext: urlContext
        )
    }

    func currentComposerPrompt() -> String? {
        normalizedText(composerText)
    }

    private func normalizedText(_ text: String?) -> String? {
        guard let text else {
            return nil
        }

        let trimmed = text.trimmingCharacters(in: .whitespacesAndNewlines)
        return trimmed.isEmpty ? nil : trimmed
    }

    private func persistedContent(for attachment: ChatImageAttachment, prompt: String?) -> String {
        guard let prompt else {
            return attachment.source.persistenceLabel
        }

        return "\(attachment.source.persistenceLabel)\n\(prompt)"
    }
}

private struct ChatAttachmentPreview: Identifiable {
    let id = UUID()
    let attachment: ChatImageAttachment
}

@MainActor
struct ChatView: View {
    @Environment(\.dismiss) private var dismiss
    @StateObject private var viewModel: ChatViewModel
    @State private var voiceManager = VoiceSearchManager()
    @State private var isShowingVisualSearch = false
    @State private var isCapturingScreenshot = false
    @State private var selectedAttachmentPreview: ChatAttachmentPreview?
    @FocusState private var isComposerFocused: Bool

    private let bottomAnchor = "chat-bottom-anchor"
    private let onClose: (() -> Void)?
    private let captureCurrentPageScreenshot: (@MainActor () async -> Data?)?

    init(
        pageUrl: String,
        pageTitle: String,
        pageText: String,
        sessionId: String? = nil,
        bridge: MahoBridge = .shared,
        captureCurrentPageScreenshot: (@MainActor () async -> Data?)? = nil,
        onClose: (() -> Void)? = nil
    ) {
        self.onClose = onClose
        self.captureCurrentPageScreenshot = captureCurrentPageScreenshot
        _viewModel = StateObject(
            wrappedValue: ChatViewModel(
                bridge: bridge,
                pageUrl: pageUrl,
                pageTitle: pageTitle,
                pageText: pageText,
                sessionId: sessionId
            )
        )
    }

    var body: some View {
        ZStack {
            backgroundLayer

            VStack(spacing: 0) {
                header
                conversation
                composer
            }
        }
        .onAppear {
            if viewModel.hasSession {
                isComposerFocused = true
            }
        }
        .accessibilityIdentifier("aiScreenChat")
        .fullScreenCover(isPresented: $isShowingVisualSearch) {
            VisualSearchView { imageData, prompt in
                sendCapturedImage(imageData, prompt: prompt, source: .camera)
            }
        }
        .fullScreenCover(item: $selectedAttachmentPreview) { preview in
            ChatAttachmentLightbox(attachment: preview.attachment)
        }
        .navigationBarBackButtonHidden(true)
        .toolbar(.hidden, for: .navigationBar)
    }

    private var backgroundLayer: some View {
        LinearGradient(
            colors: [
                ShellTheme.Palette.tabGridBackdropTop,
                ShellTheme.Palette.homeBackground,
                ShellTheme.Palette.browsingBackground
            ],
            startPoint: .topLeading,
            endPoint: .bottomTrailing
        )
        .ignoresSafeArea()
    }

    private var header: some View {
        VStack(alignment: .leading, spacing: ShellTheme.Spacing.mediumLarge) {
            HStack(spacing: ShellTheme.Spacing.small) {
                Button {
                    dismiss()
                } label: {
                    Label {
                        Text("Chats")
                    } icon: {
                        Image(uiImage: Lucide.messageCircle.withRenderingMode(.alwaysTemplate))
                    }
                    .font(.caption.weight(.semibold))
                    .foregroundStyle(.secondary)
                    .padding(.horizontal, ShellTheme.Spacing.medium)
                    .padding(.vertical, ShellTheme.Spacing.xSmall)
                    .background(
                        Capsule(style: .continuous)
                            .fill(ShellTheme.Palette.summaryMutedFill)
                    )
                }
                .buttonStyle(.plain)

                Spacer(minLength: 0)

                Button {
                    closeChat()
                } label: {
                    Image(uiImage: Lucide.circleX.withRenderingMode(.alwaysTemplate))
                        .font(.title3)
                        .foregroundStyle(.secondary)
                }
                .buttonStyle(.plain)
            }

            HStack(alignment: .top, spacing: ShellTheme.Spacing.medium) {
                ZStack {
                    RoundedRectangle(cornerRadius: ShellTheme.Radius.card, style: .continuous)
                        .fill(ShellTheme.Palette.summaryHeroFill)
                        .frame(width: ShellTheme.Size.summaryStateIcon + ShellTheme.Spacing.xSmall, height: ShellTheme.Size.summaryStateIcon + ShellTheme.Spacing.xSmall)

                    Image(uiImage: Lucide.bot.withRenderingMode(.alwaysTemplate))
                        .font(.headline.weight(.semibold))
                        .foregroundStyle(ShellTheme.Palette.summaryAccent)
                }

                VStack(alignment: .leading, spacing: ShellTheme.Spacing.xSmall) {
                    Text("AI Chat")
                        .font(.caption.weight(.semibold))
                        .foregroundStyle(.secondary)
                        .textCase(.uppercase)

                    Text(viewModel.contextTitle)
                        .font(.title3.weight(.semibold))
                        .foregroundStyle(.primary)
                        .fixedSize(horizontal: false, vertical: true)

                    Text(viewModel.contextSubtitle)
                        .font(.footnote)
                        .foregroundStyle(.secondary)
                        .fixedSize(horizontal: false, vertical: true)
                }

                Spacer(minLength: 0)
            }

            HStack(spacing: ShellTheme.Spacing.small) {
                capsuleLabel(text: viewModel.sessionLabel, icon: Lucide.sparkles)

                if let pageHost = viewModel.pageHost {
                    capsuleLabel(text: pageHost, icon: Lucide.globe)
                }

                Spacer(minLength: 0)

                if viewModel.isLoading {
                    ProgressView()
                        .controlSize(.small)
                }
            }
        }
        .padding(ShellTheme.Spacing.page)
        .background(
            RoundedRectangle(cornerRadius: ShellTheme.Radius.overlay, style: .continuous)
                .fill(ShellTheme.Palette.tabGridFloatingSurface)
                .background(
                    RoundedRectangle(cornerRadius: ShellTheme.Radius.overlay, style: .continuous)
                        .fill(ShellTheme.Materials.summaryOverlay)
                )
        )
        .overlay(
            RoundedRectangle(cornerRadius: ShellTheme.Radius.overlay, style: .continuous)
                .strokeBorder(ShellTheme.Palette.summarySurfaceBorder, lineWidth: ShellTheme.Stroke.hairline)
        )
        .shadow(
            color: ShellTheme.Elevation.summaryOverlay.color,
            radius: ShellTheme.Elevation.summaryOverlay.radius,
            x: ShellTheme.Elevation.summaryOverlay.x,
            y: ShellTheme.Elevation.summaryOverlay.y
        )
        .padding(.horizontal, ShellTheme.Spacing.large)
        .padding(.top, ShellTheme.Spacing.small)
    }

    private var conversation: some View {
        ScrollViewReader { proxy in
            ScrollView(showsIndicators: false) {
                LazyVStack(alignment: .leading, spacing: ShellTheme.Spacing.large) {
                    if viewModel.messages.isEmpty {
                        emptyState
                    } else {
                        ForEach(viewModel.messages) { message in
                            ChatBubble(message: message) { attachment in
                                selectedAttachmentPreview = ChatAttachmentPreview(attachment: attachment)
                            }
                        }
                    }

                    if let errorMessage = viewModel.errorMessage {
                        errorBanner(errorMessage)
                    }

                    if viewModel.isLoading {
                        loadingRow
                    }

                    Color.clear
                        .frame(height: ShellTheme.Stroke.hairline)
                        .id(bottomAnchor)
                }
                .padding(.horizontal, ShellTheme.Spacing.large)
                .padding(.top, ShellTheme.Spacing.large)
                .padding(.bottom, ShellTheme.Spacing.large)
                .frame(maxWidth: ShellTheme.Size.summaryDetentMaxWidth, alignment: .leading)
                .frame(maxWidth: .infinity, alignment: .center)
            }
            .onReceive(viewModel.$messages) { _ in
                scrollToBottom(using: proxy)
            }
            .onChange(of: viewModel.errorMessage) { _, _ in
                scrollToBottom(using: proxy)
            }
            .onChange(of: viewModel.isLoading) { _, _ in
                scrollToBottom(using: proxy)
            }
        }
    }

    private var emptyState: some View {
        VStack(spacing: ShellTheme.Spacing.mediumLarge) {
            Image(uiImage: viewModel.hasSession ? Lucide.sparkles : Lucide.keyRound.withRenderingMode(.alwaysTemplate))
                .font(.system(size: ShellTheme.Size.summaryStateIcon, weight: .semibold))
                .foregroundStyle(ShellTheme.Palette.summaryAccent)

            Text(viewModel.hasSession ? "Ask a question about the current page" : "Add an AI provider key to begin")
                .font(.subheadline.weight(.semibold))
                .foregroundStyle(.primary)

            Text(
                viewModel.hasSession
                    ? "Try “Summarize this page”, “What are the main points?”, or ask for clarification on what you’re reading."
                    : "Open Settings → Max AI → LLM API Keys, then add an OpenAI or Anthropic key."
            )
            .font(.footnote)
            .foregroundStyle(.secondary)
            .multilineTextAlignment(.center)
            .fixedSize(horizontal: false, vertical: true)
        }
        .frame(maxWidth: .infinity)
        .padding(ShellTheme.Spacing.page)
        .background(
            RoundedRectangle(cornerRadius: ShellTheme.Radius.card, style: .continuous)
                .fill(ShellTheme.Palette.summaryAccentFill)
        )
        .overlay(
            RoundedRectangle(cornerRadius: ShellTheme.Radius.card, style: .continuous)
                .strokeBorder(ShellTheme.Palette.overlayBorder, lineWidth: ShellTheme.Stroke.hairline)
        )
    }

    private var loadingRow: some View {
        HStack(spacing: ShellTheme.Spacing.medium) {
            ProgressView()
                .controlSize(.small)

            Text("Waiting for response…")
                .font(.footnote)
                .foregroundStyle(.secondary)
        }
        .padding(ShellTheme.Spacing.medium)
        .background(
            RoundedRectangle(cornerRadius: ShellTheme.Radius.card, style: .continuous)
                .fill(ShellTheme.Palette.summaryMutedFill)
        )
    }

    private var composer: some View {
        VStack(spacing: 0) {
            Divider()

            if captureCurrentPageScreenshot != nil {
                HStack {
                    Button {
                        captureScreenshotForChat()
                    } label: {
                        Label {
                            Text(isCapturingScreenshot ? "Capturing screenshot…" : "Screenshot current page")
                        } icon: {
                            if isCapturingScreenshot {
                                ProgressView()
                                    .controlSize(.small)
                            } else {
                                Image(uiImage: Lucide.scanLine.withRenderingMode(.alwaysTemplate))
                            }
                        }
                        .font(.subheadline.weight(.semibold))
                        .foregroundStyle(viewModel.canStartAttachmentFlow ? .primary : .secondary)
                        .padding(.horizontal, ShellTheme.Spacing.medium)
                        .padding(.vertical, ShellTheme.Spacing.small)
                        .background(
                            Capsule(style: .continuous)
                                .fill(ShellTheme.Palette.summaryMutedFill)
                        )
                    }
                    .buttonStyle(.plain)
                    .disabled(!viewModel.canStartAttachmentFlow || isCapturingScreenshot)

                    Spacer(minLength: 0)
                }
                .padding(.horizontal, ShellTheme.Spacing.page)
                .padding(.top, ShellTheme.Spacing.small)
            }

            HStack(alignment: .bottom, spacing: ShellTheme.Spacing.medium) {
                TextField(viewModel.composerPlaceholder, text: $viewModel.composerText, axis: .vertical)
                    .textFieldStyle(.plain)
                    .lineLimit(1...5)
                    .padding(.horizontal, ShellTheme.Spacing.large)
                    .padding(.vertical, ShellTheme.Spacing.medium)
                    .background(
                        RoundedRectangle(cornerRadius: ShellTheme.Radius.searchField, style: .continuous)
                            .fill(ShellTheme.Palette.searchFieldFill)
                    )
                    .overlay(
                        RoundedRectangle(cornerRadius: ShellTheme.Radius.searchField, style: .continuous)
                            .strokeBorder(ShellTheme.Palette.overlayBorder, lineWidth: ShellTheme.Stroke.hairline)
                    )
                    .focused($isComposerFocused)
                    .submitLabel(.send)
                    .onSubmit {
                        if viewModel.canSend {
                            viewModel.sendCurrentMessage()
                        }
                    }

                VoiceSearchButton(manager: voiceManager) { transcribedText in
                    viewModel.composerText = transcribedText
                    isComposerFocused = true
                }

                // TODO: Re-enable after UserContent enum (C9) lands — see hyperplan T11
                Button {
                    isComposerFocused = false
                    isShowingVisualSearch = true
                } label: {
                    Image(uiImage: Lucide.camera.withRenderingMode(.alwaysTemplate))
                        .font(.title3)
                        .foregroundStyle(viewModel.canStartAttachmentFlow ? Color.secondary : Color.secondary.opacity(0.45))
                        .frame(width: 32, height: 32)
                }
                .buttonStyle(.plain)
                .disabled(true)
                .opacity(0)
                .accessibilityLabel("Open camera")

                Button {
                    if viewModel.isLoading {
                        viewModel.cancelStreaming()
                    } else {
                        viewModel.sendCurrentMessage()
                    }
                } label: {
                    Image(uiImage: viewModel.isLoading ? Lucide.circleStop : Lucide.send.withRenderingMode(.alwaysTemplate))
                        .font(.body.weight(.semibold))
                        .foregroundStyle(.white)
                        .frame(width: ShellTheme.Size.touchTarget, height: ShellTheme.Size.touchTarget)
                        .background(
                            Circle()
                                .fill((viewModel.canSend || viewModel.isLoading) ? ShellTheme.Palette.summaryAccent : Color.secondary.opacity(0.35))
                        )
                }
                .buttonStyle(.plain)
                .disabled(!viewModel.canSend && !viewModel.isLoading)
            }
            .padding(.horizontal, ShellTheme.Spacing.page)
            .padding(.top, ShellTheme.Spacing.small)
            .padding(.bottom, ShellTheme.Spacing.page)
        }
        .background(.ultraThinMaterial)
    }

    private func errorBanner(_ message: String) -> some View {
        HStack(alignment: .top, spacing: ShellTheme.Spacing.medium) {
            Image(uiImage: Lucide.triangleAlert.withRenderingMode(.alwaysTemplate))
                .foregroundStyle(.orange)

            Text(message)
                .font(.footnote)
                .foregroundStyle(.primary)
                .fixedSize(horizontal: false, vertical: true)
        }
        .padding(ShellTheme.Spacing.medium)
        .background(
            RoundedRectangle(cornerRadius: ShellTheme.Radius.card, style: .continuous)
                .fill(ShellTheme.Palette.summaryMutedFill)
        )
        .overlay(
            RoundedRectangle(cornerRadius: ShellTheme.Radius.card, style: .continuous)
                .strokeBorder(ShellTheme.Palette.overlayBorder, lineWidth: ShellTheme.Stroke.hairline)
        )
    }

    private func capsuleLabel(text: String, icon: UIImage) -> some View {
        Label {
            Text(text)
                .lineLimit(1)
        } icon: {
            Image(uiImage: icon.withRenderingMode(.alwaysTemplate))
        }
        .font(.caption.weight(.semibold))
        .foregroundStyle(.secondary)
        .padding(.horizontal, ShellTheme.Spacing.medium)
        .padding(.vertical, ShellTheme.Spacing.xSmall)
        .background(
            Capsule()
                .fill(ShellTheme.Palette.summaryMutedFill)
        )
    }

    private func scrollToBottom(using proxy: ScrollViewProxy) {
        withAnimation(.easeOut(duration: 0.18)) {
            proxy.scrollTo(bottomAnchor, anchor: .bottom)
        }
    }

    private func closeChat() {
        if let onClose {
            onClose()
        } else {
            dismiss()
        }
    }

    private func captureScreenshotForChat() {
        guard let captureCurrentPageScreenshot, viewModel.canStartAttachmentFlow else {
            return
        }

        let composerPrompt = viewModel.currentComposerPrompt()
        viewModel.errorMessage = nil
        isComposerFocused = false
        isCapturingScreenshot = true

        Task { @MainActor in
            let imageData = await captureCurrentPageScreenshot()
            isCapturingScreenshot = false

            guard let imageData else {
                viewModel.errorMessage = "Couldn’t capture the current page."
                return
            }

            _ = viewModel.sendImage(
                imageData,
                source: .screenshot,
                prompt: composerPrompt,
                clearComposer: true
            )
        }
    }

    private func sendCapturedImage(
        _ imageData: Data,
        prompt visualSearchPrompt: String?,
        source: ChatImageAttachment.Source
    ) {
        let prompt = mergedPrompt(
            viewModel.currentComposerPrompt(),
            visualSearchPrompt
        )

        _ = viewModel.sendImage(
            imageData,
            source: source,
            prompt: prompt,
            clearComposer: true
        )
    }

    private func mergedPrompt(_ prompts: String?...) -> String? {
        let merged = prompts
            .compactMap { $0?.trimmingCharacters(in: .whitespacesAndNewlines) }
            .filter { !$0.isEmpty }

        return merged.isEmpty ? nil : merged.joined(separator: "\n")
    }
}

private struct ChatBubble: View {
    let message: ChatMessage
    let onTapAttachment: (ChatImageAttachment) -> Void

    var body: some View {
        HStack(alignment: .bottom, spacing: ShellTheme.Spacing.medium) {
            if message.isUser {
                Spacer(minLength: ShellTheme.Size.summaryStateIcon + ShellTheme.Spacing.large)
            } else {
                avatar(icon: Lucide.bot, fill: ShellTheme.Palette.summaryHeroFill, foreground: ShellTheme.Palette.summaryAccent)
            }

            bubbleBody
                .background(
                    RoundedRectangle(cornerRadius: ShellTheme.Radius.card, style: .continuous)
                        .fill(message.isUser ? ShellTheme.Palette.summaryAccent : ShellTheme.Palette.tabGridFloatingSurface)
                )
                .overlay(
                    RoundedRectangle(cornerRadius: ShellTheme.Radius.card, style: .continuous)
                        .strokeBorder(message.isUser ? Color.clear : ShellTheme.Palette.overlayBorder, lineWidth: ShellTheme.Stroke.hairline)
                )
                .fixedSize(horizontal: false, vertical: true)

            if message.isUser {
                avatar(icon: Lucide.user, fill: ShellTheme.Palette.summaryAccentFill, foreground: ShellTheme.Palette.summaryAccent)
            } else {
                Spacer(minLength: ShellTheme.Size.summaryStateIcon + ShellTheme.Spacing.large)
            }
        }
        .frame(maxWidth: .infinity, alignment: message.isUser ? .trailing : .leading)
    }

    private var bubbleBody: some View {
        VStack(alignment: .leading, spacing: ShellTheme.Spacing.small) {
            if let attachment = message.attachment {
                ChatAttachmentThumbnail(attachment: attachment) {
                    onTapAttachment(attachment)
                }
            }

            if let displayText {
                Text(displayText)
                    .font(.subheadline)
                    .foregroundStyle(message.isUser ? Color.white : Color.primary)
                    .padding(.horizontal, message.attachment == nil ? 0 : ShellTheme.Spacing.small)
                    .padding(.top, message.attachment == nil ? 0 : ShellTheme.Spacing.xxSmall)
                    .padding(.bottom, message.attachment == nil ? 0 : ShellTheme.Spacing.xSmall)
            }
        }
        .padding(message.attachment == nil ? ShellTheme.Spacing.large : ShellTheme.Spacing.small)
        .fixedSize(horizontal: false, vertical: true)
    }

    private var displayText: String? {
        let trimmed = message.text.trimmingCharacters(in: .whitespacesAndNewlines)
        return trimmed.isEmpty ? nil : message.text
    }

    private func avatar(icon: UIImage, fill: Color, foreground: Color) -> some View {
        ZStack {
            Circle()
                .fill(fill)
                .frame(width: ShellTheme.Size.summaryStateIcon, height: ShellTheme.Size.summaryStateIcon)

            Image(uiImage: icon.withRenderingMode(.alwaysTemplate))
                .font(.caption.weight(.semibold))
                .foregroundStyle(foreground)
        }
    }
}

private struct ChatAttachmentThumbnail: View {
    let attachment: ChatImageAttachment
    let onTap: () -> Void

    var body: some View {
        Button(action: onTap) {
            ZStack(alignment: .bottomLeading) {
                thumbnailImage

                Label {
                    Text(attachment.source.label)
                } icon: {
                    Image(uiImage: attachment.source.icon.withRenderingMode(.alwaysTemplate))
                }
                .font(.caption2.weight(.semibold))
                .foregroundStyle(.white)
                .padding(.horizontal, ShellTheme.Spacing.medium)
                .padding(.vertical, ShellTheme.Spacing.xSmall)
                .background(
                    Capsule(style: .continuous)
                        .fill(ShellTheme.Palette.chatAttachmentBadgeFill)
                )
                .padding(ShellTheme.Spacing.small)
            }
        }
        .buttonStyle(.plain)
        .accessibilityLabel("Open \(attachment.source.label.lowercased()) image")
    }

    @ViewBuilder
    private var thumbnailImage: some View {
#if canImport(UIKit)
        if let uiImage = UIImage(data: attachment.data) {
            Image(uiImage: uiImage)
                .resizable()
                .aspectRatio(contentMode: .fit)
                .frame(
                    maxWidth: ShellTheme.Size.chatAttachmentThumbnail,
                    maxHeight: ShellTheme.Size.chatAttachmentThumbnail
                )
                .clipShape(RoundedRectangle(cornerRadius: ShellTheme.Radius.card, style: .continuous))
                .overlay(
                    RoundedRectangle(cornerRadius: ShellTheme.Radius.card, style: .continuous)
                        .strokeBorder(ShellTheme.Palette.chatAttachmentStroke, lineWidth: ShellTheme.Stroke.hairline)
                )
        } else {
            placeholder
        }
#else
        placeholder
#endif
    }

    private var placeholder: some View {
        RoundedRectangle(cornerRadius: ShellTheme.Radius.card, style: .continuous)
            .fill(ShellTheme.Palette.chatAttachmentPlaceholder)
            .frame(
                maxWidth: ShellTheme.Size.chatAttachmentThumbnail,
                maxHeight: ShellTheme.Size.chatAttachmentThumbnail
            )
            .aspectRatio(1, contentMode: .fit)
            .overlay {
                VStack(spacing: ShellTheme.Spacing.small) {
                    Image(uiImage: Lucide.image.withRenderingMode(.alwaysTemplate))
                        .font(.title3)
                        .foregroundStyle(.secondary)

                    Text("Image unavailable")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
            }
    }
}

private struct ChatAttachmentLightbox: View {
    let attachment: ChatImageAttachment

    @Environment(\.dismiss) private var dismiss

    var body: some View {
        ZStack {
            ShellTheme.Palette.visualSearchBackground
                .ignoresSafeArea()

            VStack(spacing: ShellTheme.Spacing.large) {
                HStack(spacing: ShellTheme.Spacing.medium) {
                    Label {
                        Text(attachment.source.label)
                    } icon: {
                        Image(uiImage: attachment.source.icon.withRenderingMode(.alwaysTemplate))
                    }
                    .font(.subheadline.weight(.semibold))
                    .foregroundStyle(ShellTheme.Palette.visualSearchMutedForeground)

                    Spacer(minLength: 0)

                    Button {
                        dismiss()
                    } label: {
                        Image(uiImage: Lucide.circleX.withRenderingMode(.alwaysTemplate))
                            .font(.title3)
                            .foregroundStyle(ShellTheme.Palette.visualSearchForeground)
                            .frame(width: ShellTheme.Size.touchTarget, height: ShellTheme.Size.touchTarget)
                            .background(
                                Circle()
                                    .fill(ShellTheme.Palette.visualSearchSurface)
                            )
                    }
                    .buttonStyle(.plain)
                    .accessibilityLabel("Close image preview")
                }

                Spacer(minLength: 0)

                lightboxImage

                Spacer(minLength: 0)
            }
            .padding(.horizontal, ShellTheme.Spacing.page)
            .padding(.top, ShellTheme.Spacing.large)
            .padding(.bottom, ShellTheme.Spacing.page)
        }
    }

    @ViewBuilder
    private var lightboxImage: some View {
#if canImport(UIKit)
        if let uiImage = UIImage(data: attachment.data) {
            Image(uiImage: uiImage)
                .resizable()
                .aspectRatio(contentMode: .fit)
                .frame(maxWidth: ShellTheme.Size.visualSearchPreviewMaxWidth)
                .clipShape(RoundedRectangle(cornerRadius: ShellTheme.Radius.overlay, style: .continuous))
                .overlay(
                    RoundedRectangle(cornerRadius: ShellTheme.Radius.overlay, style: .continuous)
                        .strokeBorder(ShellTheme.Palette.visualSearchBorder, lineWidth: ShellTheme.Stroke.hairline)
                )
        } else {
            RoundedRectangle(cornerRadius: ShellTheme.Radius.overlay, style: .continuous)
                .fill(ShellTheme.Palette.visualSearchSurface)
                .frame(maxWidth: .infinity)
                .aspectRatio(4.0 / 3.0, contentMode: .fit)
                .overlay {
                    VStack(spacing: ShellTheme.Spacing.small) {
                        Image(uiImage: Lucide.image.withRenderingMode(.alwaysTemplate))
                            .font(.title2)
                            .foregroundStyle(ShellTheme.Palette.visualSearchMutedForeground)

                        Text("Preview unavailable")
                            .font(.footnote.weight(.semibold))
                            .foregroundStyle(ShellTheme.Palette.visualSearchMutedForeground)
                    }
                }
        }
#else
        RoundedRectangle(cornerRadius: ShellTheme.Radius.overlay, style: .continuous)
            .fill(ShellTheme.Palette.visualSearchSurface)
            .frame(maxWidth: .infinity)
            .aspectRatio(4.0 / 3.0, contentMode: .fit)
#endif
    }
}
