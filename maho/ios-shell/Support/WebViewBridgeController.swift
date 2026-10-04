import Foundation
import UIKit
import WebKit

struct RelayAuthRPCDependencies {
    let authenticate: (RelayAuthMode, String, String, String?) async throws -> RelayAuthSession
    let googleSignIn: @MainActor () async throws -> RelayAuthSession
    let activateSession: (RelayAuthSession) async throws -> Void
    let startPolling: @MainActor () -> Void

    static let live = RelayAuthRPCDependencies(
        authenticate: { mode, email, password, displayName in
            try await RelayAPIClient().authenticate(
                mode: mode,
                email: email,
                password: password,
                displayName: displayName
            )
        },
        googleSignIn: {
            guard let configuration = GoogleSignInConfiguration.fromBundle() else {
                throw GoogleSignInError.missingClientID
            }
            return try await GoogleRelaySignInFlow(
                coordinatorProvider: { GoogleSignInCoordinator(configuration: configuration) },
                apiClient: RelayAPIClient(),
                authStore: RelayAuthStore.shared,
                persistSession: false
            ).signIn()
        },
        activateSession: { session in
            try await SyncTransport.shared.activateAuthenticatedSession(session)
        },
        startPolling: {
            SyncManager.shared.relayAuthenticationSucceeded()
        }
    )
}

/**
 * WebViewBridgeController
 *
 * WKScriptMessageHandler that bridges the web-ai bundle to MahoBridge.
 *
 * JavaScript sends a JSON-RPC 2.0 request:
 *   window.webkit.messageHandlers.mahoBridge.postMessage(jsonString)
 *
 * The controller dispatches to the appropriate MahoBridge method on a
 * background queue, then resolves the promise in JS by calling:
 *   window.__mahoBridgeResponse(responseJsonString)
 *
 * Navigation signals (back) come through a separate handler:
 *   window.webkit.messageHandlers.mahoBridgeNav.postMessage("back")
 *
 * Usage — attach to a WKWebView before loading the bundle:
 *
 *   let controller = WebViewBridgeController(
 *       bridge: MahoBridge.shared,
 *       onBack: { [weak self] in self?.dismiss(animated: true) },
 *       onOpenSettings: { [weak self] in self?.presentSettings() }
 *   )
 *   let config = WKWebViewConfiguration()
 *   config.userContentController.add(controller, name: "mahoBridge")
 *   config.userContentController.add(controller, name: "mahoBridgeNav")
 *   let webView = WKWebView(frame: .zero, configuration: config)
 *
 * Load the bundle:
 *   let url = Bundle.main.url(forResource: "index", withExtension: "html",
 *                             subdirectory: "web-ai")!
 *   webView.loadFileURL(url, allowingReadAccessTo: url.deletingLastPathComponent())
 *
 * Dev mode: load http://localhost:5173/#byok instead of the bundle URL.
 */
final class WebViewBridgeController: NSObject, WKScriptMessageHandler {

    private static let managedAuthUnavailableEnvelope =
        #"{"version":1,"kind":"credential_error","code":"managed_auth_unavailable"}"#

    private let bridge: MahoBridge
    private let onBack: (() -> Void)?
    private let onOpenSettings: (() -> Void)?
    private let onCompleteOnboarding: (() -> Void)?
    private let agentHandleResolver: (String) -> OpaquePointer?
    private let agentArtifactLister: (OpaquePointer) -> [[String: Any]]
    private let agentArtifactReader: (OpaquePointer, String) -> Data?
    private let artifactShareCoordinator: ArtifactShareCoordinator
    private let chatRPC: ChatSessionRPCDependencies
    private let conversationRPC: ConversationRPCDependencies
    private let browserToolExecutor: BrowserToolExecutor
    private let relayAuthRPC: RelayAuthRPCDependencies
    private let maxBrowserToolResultBytes = 4_000
    let dispatchQueue = DispatchQueue(
        label: "dev.maho.browser.WebViewBridgeController",
        qos: .userInitiated
    )
    private weak var webView: WKWebView?
    private let streamsLock = NSLock()
    private var streams: [String: StreamSession] = [:]
    // U02 owner-scoped lifecycle: handles created through this controller are
    // owned by it; dismantle closes admission and frees them exactly once.
    private let ownershipLock = NSLock()
    private var ownershipClosed = false
    private var ownedAgentHandles: Set<String> = []
    private var ownedChatHandles: Set<String> = []

    init(
        bridge: MahoBridge = .shared,
        onBack: (() -> Void)? = nil,
        onOpenSettings: (() -> Void)? = nil,
        onCompleteOnboarding: (() -> Void)? = nil,
        agentHandleResolver: @escaping (String) -> OpaquePointer? = {
            AgentHandleRegistry.shared.get(id: $0)
        },
        agentArtifactLister: ((OpaquePointer) -> [[String: Any]])? = nil,
        agentArtifactReader: ((OpaquePointer, String) -> Data?)? = nil,
        artifactShareCoordinator: ArtifactShareCoordinator = .shared,
        chatRPC: ChatSessionRPCDependencies? = nil,
        conversationRPC: ConversationRPCDependencies? = nil,
        browserToolExecutor: BrowserToolExecutor? = nil,
        relayAuthRPC: RelayAuthRPCDependencies = .live
    ) {
        self.bridge = bridge
        self.onBack = onBack
        self.onOpenSettings = onOpenSettings
        self.onCompleteOnboarding = onCompleteOnboarding
        self.agentHandleResolver = agentHandleResolver
        self.agentArtifactLister = agentArtifactLister ?? { bridge.agentListArtifacts($0) }
        self.agentArtifactReader = agentArtifactReader ?? { bridge.agentReadArtifact($0, artifactId: $1) }
        self.artifactShareCoordinator = artifactShareCoordinator
        self.chatRPC = chatRPC ?? .live(bridge: bridge)
        self.conversationRPC = conversationRPC ?? .live(bridge: bridge)
        self.browserToolExecutor = browserToolExecutor ?? .live(bridge: bridge)
        self.relayAuthRPC = relayAuthRPC
        super.init()
    }

    deinit {
        shutdownStreaming()
    }

    // MARK: - Streaming API (A6 parity)

    func pushToken(sessionId: String, token: String) {
        streamsLock.lock()
        let session = streams[sessionId] ?? StreamSession(sessionId: sessionId, webView: webView)
        streams[sessionId] = session
        streamsLock.unlock()
        session.pushToken(token)
    }

    func pushThinking(sessionId: String, thinking: String) {
        streamsLock.lock()
        let session = streams[sessionId] ?? StreamSession(sessionId: sessionId, webView: webView)
        streams[sessionId] = session
        streamsLock.unlock()
        session.pushThinking(thinking)
    }

    func pushToolDelta(sessionId: String, deltaJson: String) {
        let js = "window.__mahoStreamToolDelta('\(sessionId)', \(deltaJson))"
        scheduleJsEvaluation(js)
    }

    func pushImageDelta(sessionId: String, deltaJson: String) {
        let js = "window.__mahoStreamImageDelta('\(sessionId)', \(deltaJson))"
        scheduleJsEvaluation(js)
    }

    func pushStreamComplete(sessionId: String, finalMessage: String) {
        streamsLock.lock()
        let session = streams.removeValue(forKey: sessionId)
        streamsLock.unlock()
        session?.complete(finalMessage: finalMessage)
    }

    func pushStreamError(sessionId: String, error: String) {
        streamsLock.lock()
        let session = streams.removeValue(forKey: sessionId)
        streamsLock.unlock()
        session?.error(message: error)
    }

    func shutdownStreaming() {
        streamsLock.lock()
        let activeSessions = Array(streams.values)
        streams.removeAll()
        streamsLock.unlock()
        
        for session in activeSessions {
            session.error(message: "Shutdown")
        }
        flushPendingJsEvaluations()
    }

    // MARK: - JS Batching & Throttling

    private let jsEvaluationLock = NSLock()
    private var pendingJsEvaluations: [String] = []
    private var jsEvaluationScheduled = false

    private func scheduleJsEvaluation(_ js: String, immediate: Bool = false) {
        guard let webView = webView else { return }
        jsEvaluationLock.lock()
        pendingJsEvaluations.append(js)
        if immediate || pendingJsEvaluations.count >= 32 {
            let batch = pendingJsEvaluations
            pendingJsEvaluations.removeAll()
            jsEvaluationScheduled = false
            jsEvaluationLock.unlock()
            DispatchQueue.main.async {
                guard !batch.isEmpty else { return }
                let combined = batch.joined(separator: ";")
                webView.evaluateJavaScript(combined, completionHandler: nil)
            }
        } else {
            if !jsEvaluationScheduled {
                jsEvaluationScheduled = true
                jsEvaluationLock.unlock()
                DispatchQueue.main.async { [weak self] in
                    self?.flushPendingJsEvaluations()
                }
            } else {
                jsEvaluationLock.unlock()
            }
        }
    }

    private func flushPendingJsEvaluations() {
        jsEvaluationLock.lock()
        jsEvaluationScheduled = false
        if pendingJsEvaluations.isEmpty {
            jsEvaluationLock.unlock()
            return
        }
        let batch = pendingJsEvaluations
        pendingJsEvaluations.removeAll()
        jsEvaluationLock.unlock()

        guard let webView = webView else { return }
        let combined = batch.joined(separator: ";")
        webView.evaluateJavaScript(combined, completionHandler: nil)
    }


    /// Attach to a webView after init. Called by the host view controller.
    func attach(to webView: WKWebView) {
        self.webView = webView
#if DEBUG
        if #available(iOS 16.4, *) {
            webView.isInspectable = true
        }
#endif
    }

    // MARK: - WKScriptMessageHandler

    func userContentController(
        _ userContentController: WKUserContentController,
        didReceive message: WKScriptMessage
    ) {
        switch message.name {
        case "mahoBridge":
            guard let body = message.body as? String else { return }
            handleRPC(body)
        case "mahoBridgeNav":
            let signal = message.body as? String ?? ""
            handleNav(signal)
        default:
            break
        }
    }

    // MARK: - RPC dispatch

    private func handleRPC(_ requestJson: String) {
        dispatchQueue.async { [weak self] in
            guard let self else { return }
            let response = self.dispatch(requestJson)
            self.resolveOnJS(response)
        }
    }

    private func handleNav(_ signal: String) {
        if signal == "back" {
            DispatchQueue.main.async { [weak self] in
                self?.onBack?()
            }
        }
    }

    func dispatchRPCForTesting(_ requestJson: String) -> String {
        dispatch(requestJson)
    }

    // MARK: - Owner-scoped teardown (U02)

    /// SwiftUI dismantle entry: closes admission immediately and frees every
    /// agent handle this controller created, exactly once. The work runs on the
    /// controller queue; native frees defer behind active operation leases in
    /// `AgentSessionRegistry`, so this never blocks the caller (Main).
    func dismantle() {
        dispatchQueue.async { [weak self] in
            self?.performOwnerTeardown()
        }
    }

    private func performOwnerTeardown() {
        ownershipLock.lock()
        guard !ownershipClosed else {
            ownershipLock.unlock()
            return
        }
        ownershipClosed = true
        let handles = ownedAgentHandles
        ownedAgentHandles.removeAll()
        ownershipLock.unlock()
        print("PROBE_TEARDOWN handles=\(handles) closed=\(ownershipClosed)")

        for handle in handles {
            if let ptr = AgentHandleRegistry.shared.get(id: handle) {
                // close() marks the entry closing: admission stops, and the
                // native free defers behind any active operation lease.
                bridge.agentFreeSession(ptr)
            }
            AgentHandleRegistry.shared.unregister(id: handle)
        }
        for chatHandle in ownedChatHandles {
            // release(id:) is the canonical chat teardown; it does not block
            // on in-flight chat operations.
            ChatSessionRegistry.shared.release(id: chatHandle)
        }
        ownedChatHandles.removeAll()
        // U02: remove the script message handlers on Main so the page loses
        // its native transport the moment the owner is dismantled.
        DispatchQueue.main.async { [weak self] in
            self?.webView?.configuration.userContentController
                .removeAllScriptMessageHandlers(from: .page)
        }
    }

    private func dispatch(_ requestJson: String) -> String {
        guard
            let data = requestJson.data(using: .utf8),
            let obj = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
            let id = obj["id"],
            let method = obj["method"] as? String
        else {
            return errorResponse(id: NSNull(), message: "Invalid JSON-RPC request")
        }

        let params = obj["params"] ?? []

        // U06i: after a failed startup, dependent RPCs fail explicitly instead
        // of returning null/empty profile results.
        if bridge.startupFailed {
            return errorResponse(id: id, message: "core_initialization_failed", kind: "native_error")
        }

        do {
            let result = try dispatchMethod(method, params: params)
            return successResponse(id: id, result: result)
        } catch let error as BridgeError {
            return errorResponse(id: id, message: error.rpcReason, kind: error.rpcKind)
        } catch {
            return errorResponse(id: id, message: error.localizedDescription)
        }
    }

    // swiftlint:disable:next cyclomatic_complexity function_body_length
    private func dispatchMethod(_ method: String, params: Any) throws -> Any? {
        switch method {

        // ── BYOK ──────────────────────────────────────────────────────────────
        case "byokGetProviders":
            return bridge.byokGetProviders()

        case "byokGetKey":
            let provider = try stringParam(params, at: 0, name: "provider")
            return bridge.byokGetKey(provider: provider)

        case "byokSetKey":
            let provider = try stringParam(params, at: 0, name: "provider")
            let key = try stringParam(params, at: 1, name: "key")
            return bridge.byokSetKey(provider: provider, key: key)

        case "byokDeleteKey":
            let provider = try stringParam(params, at: 0, name: "provider")
            return bridge.byokDeleteKey(provider: provider)

        case "byokValidateKey":
            let provider = try stringParam(params, at: 0, name: "provider")
            let key = try stringParam(params, at: 1, name: "key")
            return bridge.byokValidateKey(provider: provider, key: key)

        // ── AI provider settings (desktop parity) ───────────────────────────────
        case "getAiSettings":
            let defaults = UserDefaults.standard
            return [
                "provider": defaults.string(forKey: AiSettingsKeys.provider) ?? "",
                "baseUrl": defaults.string(forKey: AiSettingsKeys.baseUrl) ?? "",
                "model": defaults.string(forKey: AiSettingsKeys.model) ?? "",
                "hasApiKey": !(BYOKKeychain.get(provider: AiSettingsKeys.customApiKeyAccount) ?? "").isEmpty,
                "hasByokOpenai": !(bridge.byokGetKey(provider: "openai") ?? "").isEmpty,
                "hasByokAnthropic": !(bridge.byokGetKey(provider: "anthropic") ?? "").isEmpty
            ]

        case "setAiProvider":
            UserDefaults.standard.set(try stringParam(params, at: 0, name: "provider"), forKey: AiSettingsKeys.provider)
            return nil

        case "setAiBaseUrl":
            UserDefaults.standard.set(try stringParam(params, at: 0, name: "url"), forKey: AiSettingsKeys.baseUrl)
            return nil

        case "setAiApiKey":
            let key = try stringParam(params, at: 0, name: "key")
            return BYOKKeychain.set(provider: AiSettingsKeys.customApiKeyAccount, key: key)

        case "setAiModel":
            UserDefaults.standard.set(try stringParam(params, at: 0, name: "model"), forKey: AiSettingsKeys.model)
            return nil

        // ── Chat Session ──────────────────────────────────────────────────────
        case "chatSessionStart":
            let config = try resolveChatConfig(chatOptions(params, at: 0))
            guard let ptr = chatRPC.create(config) else {
                throw BridgeError.operationFailed("chatSessionStart returned nil")
            }
            guard let chatHandle = chatRPC.handleForPointer(ptr) else {
                throw BridgeError.operationFailed("chatSessionStart registration failed")
            }
            print("PROBE_CHAT created handle=\(chatHandle)")
            ownershipLock.lock()
            if ownershipClosed {
                ownershipLock.unlock()
                ChatSessionRegistry.shared.release(id: chatHandle)
                throw BridgeError.operationFailed("owner closed during chatSessionStart")
            }
            ownedChatHandles.insert(chatHandle)
            ownershipLock.unlock()
            return chatHandle

        case "chatSessionResume":
            let conversationId = try resumeConversationId(params)
            if chatRPC.hasSession(conversationId) {
                return conversationId
            }
            guard let conversation = chatRPC.conversation(conversationId) else {
                throw BridgeError.operationFailed("Conversation session not found for handle: \(conversationId)")
            }
            var options = chatOptions(params, at: 1)
            if options.model.isEmpty {
                options.model = conversation.model ?? ""
            }
            let config = try resolveChatConfig(options)
            guard !config.endpoint.isEmpty else {
                throw BridgeError.missingParam("endpoint")
            }
            guard let ptr = chatRPC.create(config) else {
                throw BridgeError.operationFailed("chatSessionResume failed to create chat session")
            }

            chatRPC.bind(conversationId, ptr)

            for turn in chatRPC.messages(conversationId) {
                if turn.role == "user" {
                    chatRPC.appendUser(ptr, turn.content)
                } else if turn.role == "assistant" {
                    chatRPC.appendAssistant(ptr, turn.content)
                }
            }
            return conversationId


        case "chatSessionFree":
            let handle = try stringParam(params, at: 0, name: "handle")
            ChatSessionRegistry.shared.release(id: handle)
            return nil

        case "chatSendMessage":
            let handle = try sessionParam(params, at: 0, name: "handle")
            let contentVal = getParam(params, at: 1, name: "content")
            let content = contentVal as? [String: Any] ?? [:]
            let text = content["text"] as? String ?? ""
            if let imageObj = content["image"] as? [String: Any] {
                let mime = imageObj["mime"] as? String ?? ""
                let rawBytes = imageObj["base64"] as? String ?? ""
                let data = Data(base64Encoded: rawBytes) ?? Data()
                if text.isEmpty {
                    return bridge.chatSendImage(handle, mime: mime, data: data)
                } else {
                    return bridge.chatSendTextWithImage(handle, text: text, mime: mime, data: data)
                }
            } else {
                return bridge.chatSendUserTurn(handle, message: text)
            }

        case "chatCancelTurn":
            let handle = try sessionParam(params, at: 0, name: "handle")
            bridge.chatCancel(handle)
            return nil

        case "chatPollEvents":
            let handle = try sessionParam(params, at: 0, name: "handle")
            var events: [[String: Any]] = []
            while let eventJson = bridge.chatPollEvent(handle),
                  let data = eventJson.data(using: .utf8),
                  let obj = try? JSONSerialization.jsonObject(with: data) as? [String: Any] {
                events.append(obj)
            }
            return events

        case "chatRegisterTool":
            let handle = try sessionParam(params, at: 0, name: "handle")
            let toolVal = getParam(params, at: 1, name: "tool")
            let tool = toolVal as? [String: Any] ?? [:]
            let name = tool["name"] as? String ?? ""
            let description = tool["description"] as? String ?? ""
            let parameters = tool["parameters"] as? [String: Any] ?? [:]
            let parametersJson: String
            if let data = try? JSONSerialization.data(withJSONObject: parameters),
               let str = String(data: data, encoding: .utf8) {
                parametersJson = str
            } else {
                parametersJson = "{}"
            }
            return bridge.chatRegisterTool(handle, name: name, description: description, schemaJson: parametersJson)

        case "chatSendToolResult":
            let handle = try sessionParam(params, at: 0, name: "handle")
            let toolCallId = try stringParam(params, at: 1, name: "toolCallId")
            let toolName = stringParamOpt(params, at: 3, name: "toolName") ?? ""
            let trigger = (getParam(params, at: 4, name: "trigger") as? Bool) ?? true
            let resultVal = getParam(params, at: 2, name: "result")
            let result = resultVal as? [String: Any] ?? [:]
            let resultJson: String
            if let data = try? JSONSerialization.data(withJSONObject: result),
               let str = String(data: data, encoding: .utf8) {
                resultJson = str
            } else {
                resultJson = "{}"
            }
            return chatRPC.sendToolResult(handle, toolCallId, toolName, resultJson, trigger)

        case "chatAppendAssistantMessage":
            let handle = try sessionParam(params, at: 0, name: "handle")
            let content = try stringParam(params, at: 1, name: "content")
            let toolCallsJson = stringParamOpt(params, at: 2, name: "toolCallsJson") ?? "[]"
            bridge.chatAppendAssistantMessage(handle, content: content, toolCallsJson: toolCallsJson)
            return nil

        case "chatGetHistory":
            let handle = try stringParam(params, at: 0, name: "handle")
            let turns = bridge.getConversationMessages(sessionId: handle)
            var messages: [[String: Any]] = []
            for turn in turns {
                messages.append([
                    "id": turn.id,
                    "role": turn.role,
                    "content": turn.content
                ])
            }
            return messages

        case "browserToolInvoke":
            let name = try stringParam(params, at: 0, name: "name")
            let args = getParam(params, at: 1, name: "args") as? [String: Any] ?? [:]
            return boundedBrowserToolResult(browserToolExecutor.invoke(name, args))

        // ── Conversations ─────────────────────────────────────────────────────
        case "conversationCreate":
            let metaVal = getParam(params, at: 0, name: "meta")
            let meta = metaVal as? [String: Any] ?? [:]
            let id = meta["id"] as? String ?? UUID().uuidString
            let title = meta["title"] as? String
            let spaceId = meta["spaceId"] as? String
            let model = meta["model"] as? String
            guard bridge.createConversation(id: id, title: title, spaceId: spaceId, model: model) else {
                throw BridgeError.operationFailed("conversationCreate failed")
            }
            return id

        case "conversationList":
            let state = try conversationListState(params)
            let limit = try conversationListLimit(params)
            guard let payload = conversationRPC.listPayload(state, limit),
                  let data = payload.data(using: .utf8),
                  let result = try? JSONSerialization.jsonObject(with: data) as? [[String: Any]] else {
                throw BridgeError.operationFailed("conversationList returned invalid native payload")
            }
            return result

        case "conversationGet":
            let id = try stringParam(params, at: 0, name: "id")
            let sessions = bridge.listConversations(limit: 1000)
            guard let match = sessions.first(where: { $0.id == id }) else { return nil }
            return try jsonObject(match)

        case "conversationDelete":
            let id = try stringParam(params, at: 0, name: "id")
            return bridge.deleteConversation(sessionId: id)

        case "conversationRename":
            let id = try stringParam(params, at: 0, name: "id")
            let title = try stringParam(params, at: 1, name: "title")
            return bridge.renameConversation(sessionId: id, title: title)

        case "conversationArchive":
            return conversationRPC.archive(try nonemptyStringParam(params, at: 0, name: "id"))

        case "conversationUnarchive":
            return conversationRPC.unarchive(try nonemptyStringParam(params, at: 0, name: "id"))

        case "conversationBulk":
            let operation = try conversationBulkOperation(params)
            let ids = try conversationBulkIds(params)
            guard let payload = conversationRPC.bulk(operation, ids),
                  let data = payload.data(using: .utf8),
                  let result = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
                  isConversationBulkResult(result) else {
                throw BridgeError.operationFailed("conversationBulk returned invalid native payload")
            }
            return result

        case "conversationGetAutoArchivePolicy":
            return Int(conversationRPC.getPolicy())

        case "conversationSetAutoArchivePolicy":
            return conversationRPC.setPolicy(try conversationAutoArchiveDays(params))

        case "conversationGetMessages":
            let id = try stringParam(params, at: 0, name: "id")
            let turns = bridge.getConversationMessages(sessionId: id)
            return try jsonArray(turns)

        case "conversationProjectList":
            return try requiredJSONArrayPayload(bridge.listConversationProjectsPayload(), operation: "conversationProjectList")

        case "conversationProjectCreate":
            let name = try nonemptyStringParam(params, at: 0, name: "name")
            return try requiredJSONObjectPayload(bridge.createConversationProjectPayload(name: name), operation: "conversationProjectCreate")

        case "conversationProjectRename":
            return bridge.renameConversationProject(
                id: try nonemptyStringParam(params, at: 0, name: "id"),
                name: try nonemptyStringParam(params, at: 1, name: "name")
            )

        case "conversationProjectDelete":
            return bridge.deleteConversationProject(id: try nonemptyStringParam(params, at: 0, name: "id"))

        case "conversationProjectMove":
            let ids = try conversationProjectMoveIds(params)
            let projectId = try nullableNonemptyStringParam(params, at: 1, name: "projectId")
            return try requiredJSONObjectPayload(
                bridge.moveConversationsToProjectPayload(ids: ids, projectId: projectId),
                operation: "conversationProjectMove"
            )

        case "saveConversationMessage":
            let sessionId = try stringParam(params, at: 0, name: "sessionId")
            let role = try stringParam(params, at: 1, name: "role")
            let content = try stringParam(params, at: 2, name: "content")
            return bridge.saveConversationMessage(
                sessionId: sessionId,
                role: role,
                content: content
            )

        // ── Composer drafts ───────────────────────────────────────────────────
        case "composerDraftGet":
            let scopeJson = try composerDraftScopeJson(params)
            guard let payload = bridge.getComposerDraft(scopeJson: scopeJson),
                  let data = payload.data(using: .utf8),
                  let obj = try? JSONSerialization.jsonObject(with: data) as? [String: Any] else {
                return nil
            }
            return obj

        case "composerDraftSet":
            let scopeJson = try composerDraftScopeJson(params)
            let text = try stringParam(params, at: 1, name: "text")
            return bridge.setComposerDraft(scopeJson: scopeJson, text: text)

        case "composerDraftDelete":
            let scopeJson = try composerDraftScopeJson(params)
            return bridge.deleteComposerDraft(scopeJson: scopeJson)

        // ── Space AI Config ───────────────────────────────────────────────────
        case "getSpaceAIConfig":
            let spaceId = try stringParam(params, at: 0, name: "spaceId")
            guard let config = bridge.getSpaceAIConfig(spaceId: spaceId) else { return nil }
            return try jsonObject(config)

        case "setSpaceAIConfig":
            let spaceId = try stringParam(params, at: 0, name: "spaceId")
            let configVal = getParam(params, at: 1, name: "config")
            let configJson: String
            if let configStr = configVal as? String {
                configJson = configStr
            } else if let configObj = configVal as? [String: Any],
                      let data = try? JSONSerialization.data(withJSONObject: configObj),
                      let str = String(data: data, encoding: .utf8) {
                configJson = str
            } else {
                throw BridgeError.invalidParam("config must be a valid JSON or object")
            }
            guard
                let data = configJson.data(using: .utf8),
                let config = try? JSONDecoder().decode(SpaceAIConfig.self, from: data)
            else {
                throw BridgeError.invalidParam("config is not valid SpaceAIConfig JSON")
            }
            return bridge.setSpaceAIConfig(spaceId: spaceId, config: config)

        // ── Pinch ─────────────────────────────────────────────────────────────
        case "pinchEstimateCost":
            let pageContent = stringParamOpt(params, at: 0, name: "pageContent") ?? ""
            let model = stringParamOpt(params, at: 1, name: "model") ?? "default"
            let inputTokens = pageContent.count / 4
            let outputTokens = min(inputTokens / 4, 1024)
            return [
                "inputTokens": inputTokens,
                "outputTokens": outputTokens,
                "estimatedCostUsd": 0.0,
                "model": model
            ]

        // ── Agent Session ─────────────────────────────────────────────────────
        case "agentCreateSession":
            let sessionId = try stringParam(params, at: 0, name: "sessionId")
            // The agent runtime lease handshake must run off the calling thread:
            // a create issued on Main fails inside the native lease boundary.
            let createGroup = DispatchGroup()
            createGroup.enter()
            var createdPointer: OpaquePointer?
            let bridgeForCreate = bridge
            DispatchQueue.global(qos: .userInitiated).async {
                defer { createGroup.leave() }
                createdPointer = bridgeForCreate.agentCreateSession(sessionId: sessionId)
            }
            createGroup.wait()
            guard let ptr = createdPointer else {
                throw BridgeError.operationFailed("agentCreateSession returned nil")
            }
            // U02: a create that lands on a dismantled controller is freed
            // locally and reports an error instead of registering a usable
            // orphan; a live controller adopts the handle into its owned set.
            ownershipLock.lock()
            if ownershipClosed {
                ownershipLock.unlock()
                bridge.agentFreeSession(ptr)
                throw BridgeError.operationFailed("owner closed during agentCreateSession")
            }
            let handle = AgentHandleRegistry.shared.register(ptr: ptr)
            ownedAgentHandles.insert(handle)
            ownershipLock.unlock()
            print("PROBE_OWN registered handle=\(handle) count=\(ownedAgentHandles.count) closed=\(ownershipClosed)")
            return handle

        case "agentSendMessage":
            let ptr = try agentSessionParam(params, at: 0, name: "handle")
            let message = try stringParam(params, at: 1, name: "message")
            return bridge.agentSendMessage(ptr, message: message)

        case "agentPollEvent":
            let ptr = try agentSessionParam(params, at: 0, name: "handle")
            return bridge.agentPollEvent(ptr) ?? NSNull()

        case "agentCancel":
            let ptr = try agentSessionParam(params, at: 0, name: "handle")
            return bridge.agentCancel(ptr)

        case "agentFreeSession":
            let handle = try stringParam(params, at: 0, name: "handle")
            guard let ptr = AgentHandleRegistry.shared.get(id: handle) else {
                throw BridgeError.operationFailed("Agent handle not found: \(handle)")
            }
            bridge.agentFreeSession(ptr)
            AgentHandleRegistry.shared.unregister(id: handle)
            return nil

        case "agentListTools":
            let ptr = try agentSessionParam(params, at: 0, name: "handle")
            guard
                let toolsJson = bridge.agentListTools(ptr),
                let data = toolsJson.data(using: .utf8),
                let tools = try? JSONSerialization.jsonObject(with: data) as? [Any]
            else {
                return [Any]()
            }
            return tools

        case "agentListArtifacts":
            let ptr = try agentSessionParam(params, at: 0, name: "handle")
            return agentArtifactLister(ptr)

        case "artifactShare":
            let ptr = try agentSessionParam(params, at: 0, name: "handle")
            let artifactId = try stringParam(params, at: 1, name: "artifactId")
            guard let metadata = agentArtifactLister(ptr).first(where: {
                $0["artifactId"] as? String == artifactId
            }) else {
                throw BridgeError.operationFailed("Artifact not found: \(artifactId)")
            }
            guard let displayName = metadata["displayName"] as? String,
                  let data = agentArtifactReader(ptr, artifactId) else {
                throw BridgeError.operationFailed("Artifact could not be read: \(artifactId)")
            }
            try artifactShareCoordinator.share(data: data, displayName: displayName)
            return true

        // ── Native-only actions ───────────────────────────────────────────────
        case "hapticFeedback":
            let style = stringParamOpt(params, at: 0, name: "style") ?? "medium"
            triggerHaptic(style: style)
            return nil

        case "openSettings":
            DispatchQueue.main.async { [weak self] in
                self?.onOpenSettings?()
            }
            return nil

        case "capturePhoto", "captureScreenshot":
            return nil

        // ── Relay auth (gated onboarding) ──────────────────────────────────────
        case "relaySignIn":
            let email = try stringParam(params, at: 0, name: "email")
            let password = try stringParam(params, at: 1, name: "password")
            var apiResult: [String: Any] = [:]
            let semaphore = DispatchSemaphore(value: 0)
            Task {
                do {
                    let session = try await relayAuthRPC.authenticate(.login, email, password, nil)
                    try await relayAuthRPC.activateSession(session)
                    await relayAuthRPC.startPolling()
                    apiResult = ["ok": true]
                } catch {
                    apiResult = ["ok": false, "error": error.localizedDescription]
                }
                semaphore.signal()
            }
            semaphore.wait()
            return apiResult

        case "relaySignUp":
            let email = try stringParam(params, at: 0, name: "email")
            let password = try stringParam(params, at: 1, name: "password")
            let displayName = try stringParam(params, at: 2, name: "displayName")
            var apiResult: [String: Any] = [:]
            let semaphore = DispatchSemaphore(value: 0)
            Task {
                do {
                    let session = try await relayAuthRPC.authenticate(.signup, email, password, displayName)
                    try await relayAuthRPC.activateSession(session)
                    await relayAuthRPC.startPolling()
                    apiResult = ["ok": true]
                } catch {
                    apiResult = ["ok": false, "error": error.localizedDescription]
                }
                semaphore.signal()
            }
            semaphore.wait()
            return apiResult

        case "relaySignInWithGoogle":
            var apiResult: [String: Any] = [:]
            let semaphore = DispatchSemaphore(value: 0)
            Task { @MainActor in
                defer { semaphore.signal() }
                do {
                    let session = try await relayAuthRPC.googleSignIn()
                    try await relayAuthRPC.activateSession(session)
                    relayAuthRPC.startPolling()
                    apiResult = ["ok": true]
                } catch {
                    if GoogleSignInError.isCancellation(error) {
                        apiResult = ["ok": false, "cancelled": true]
                    } else {
                        apiResult = ["ok": false, "error": error.localizedDescription]
                    }
                }
            }
            semaphore.wait()
            return apiResult

        case "relayAccountStatus":
            let session = RelayAuthStore.shared.loadSession()
            let hasValidSession = session != nil && !session!.isRefreshTokenExpired
            return [
                "hasValidSession": hasValidSession,
                "isReauth": false
            ]

        // ── Default browser ───────────────────────────────────────────────────
        case "openDefaultBrowserSettings":
            DispatchQueue.main.async {
                if let url = URL(string: UIApplication.openSettingsURLString) {
                    UIApplication.shared.open(url)
                }
            }
            return nil

        // ── Onboarding completion ──────────────────────────────────────────────
        case "completeOnboarding":
            DispatchQueue.main.async { [weak self] in
                self?.onCompleteOnboarding?()
            }
            return nil

        default:
            throw BridgeError.unknownMethod(method)
        }
    }

    // MARK: - JS response delivery

    private func resolveOnJS(_ responseJson: String) {
        let escaped = responseJson
            .replacingOccurrences(of: "\\", with: "\\\\")
            .replacingOccurrences(of: "'", with: "\\'")
        let js = "window.__mahoBridgeResponse('\(escaped)')"
        scheduleJsEvaluation(js)
    }

    private func triggerHaptic(style: String) {
        DispatchQueue.main.async {
            let feedbackStyle: UIImpactFeedbackGenerator.FeedbackStyle
            switch style {
            case "light":  feedbackStyle = .light
            case "heavy":  feedbackStyle = .heavy
            default:       feedbackStyle = .medium
            }
            UIImpactFeedbackGenerator(style: feedbackStyle).impactOccurred()
        }
    }

    // MARK: - JSON-RPC envelope helpers

    private func successResponse(id: Any, result: Any?) -> String {
        var obj: [String: Any] = ["jsonrpc": "2.0", "id": id]
        obj["result"] = result ?? NSNull()
        guard
            let data = try? JSONSerialization.data(withJSONObject: obj),
            let str = String(data: data, encoding: .utf8)
        else {
            return "{\"jsonrpc\":\"2.0\",\"id\":null,\"result\":null}"
        }
        return str
    }

    private func errorResponse(id: Any, message: String, kind: String = "internal") -> String {
        let obj: [String: Any] = [
            "jsonrpc": "2.0",
            "id": id,
            "error": [
                "kind": kind,
                "reason": message
            ]
        ]
        guard
            let data = try? JSONSerialization.data(withJSONObject: obj),
            let str = String(data: data, encoding: .utf8)
        else {
            return "{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"kind\":\"internal\",\"reason\":\"internal\"}}"
        }
        return str
    }

    // MARK: - Parameter extraction helpers

    private func getParam(_ params: Any, at index: Int, name: String) -> Any? {
        if let array = params as? [Any] {
            return index < array.count ? array[index] : nil
        } else if let dict = params as? [String: Any] {
            return dict[name]
        }
        return nil
    }

    private func stringParam(_ params: Any, at index: Int, name: String) throws -> String {
        guard let value = getParam(params, at: index, name: name) as? String else {
            throw BridgeError.missingParam(name)
        }
        return value
    }

    private func chatOptions(_ params: Any, at index: Int) -> ChatSessionRPCOptions {
        let value: Any?
        if let array = params as? [Any] {
            value = index < array.count ? array[index] : nil
        } else if let dict = params as? [String: Any] {
            value = dict["opts"] ?? dict
        } else {
            value = nil
        }
        return ChatSessionRPCOptions(value as? [String: Any] ?? [:])
    }

    private func resolveChatConfig(_ options: ChatSessionRPCOptions) throws -> ChatSessionRPCConfig {
        if options.credentialProvider == AiProvider.managed.rawValue {
            guard let managed = chatRPC.resolveManagedProvider() else {
                throw BridgeError.operationFailed(Self.managedAuthUnavailableEnvelope)
            }
            return ChatSessionRPCConfig(
                apiKey: managed.apiKey,
                endpoint: managed.endpoint,
                model: managed.model,
                systemInstruction: options.systemInstruction
            )
        }

        var apiKey = options.apiKey
        if apiKey.isEmpty, options.credentialProvider == AiSettingsKeys.customApiKeyAccount {
            apiKey = chatRPC.resolveSecret(AiSettingsKeys.customApiKeyAccount) ?? ""
        }
        return ChatSessionRPCConfig(
            apiKey: apiKey,
            endpoint: options.endpoint,
            model: options.model,
            systemInstruction: options.systemInstruction
        )
    }

    private func resumeConversationId(_ params: Any) throws -> String {
        if let array = params as? [Any], let value = array.first as? String {
            return value
        }
        if let dict = params as? [String: Any],
           let value = (dict["conversationId"] ?? dict["handle"]) as? String {
            return value
        }
        throw BridgeError.missingParam("conversationId")
    }

    private func stringParamOpt(_ params: Any, at index: Int, name: String) -> String? {
        return getParam(params, at: index, name: name) as? String
    }

    private func nonemptyStringParam(_ params: Any, at index: Int, name: String) throws -> String {
        let value = try stringParam(params, at: index, name: name)
        guard !value.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty else {
            throw BridgeError.invalidParam("\(name) must be nonempty")
        }
        return value
    }

    private func conversationListState(_ params: Any) throws -> String {
        guard let raw = getParam(params, at: 0, name: "state") else { return "active" }
        guard let state = raw as? String, ["active", "archived", "all"].contains(state) else {
            throw BridgeError.invalidParam("state must be active, archived, or all")
        }
        return state
    }

    private func conversationListLimit(_ params: Any) throws -> Int {
        guard let raw = getParam(params, at: 1, name: "limit") else { return 100 }
        guard let limit = integralInt(raw), (1...500).contains(limit) else {
            throw BridgeError.invalidParam("limit must be an integer from 1 through 500")
        }
        return limit
    }

    private func conversationBulkOperation(_ params: Any) throws -> String {
        let operation = try stringParam(params, at: 0, name: "op")
        guard ["archive", "unarchive", "delete"].contains(operation) else {
            throw BridgeError.invalidParam("op must be archive, unarchive, or delete")
        }
        return operation
    }

    private func conversationBulkIds(_ params: Any) throws -> [String] {
        let ids = try stringArrayParam(params, at: 1, name: "ids")
        guard (1...500).contains(ids.count) else {
            throw BridgeError.invalidParam("ids must contain from 1 through 500 values")
        }
        guard ids.allSatisfy({ !$0.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty }) else {
            throw BridgeError.invalidParam("ids must contain only nonempty values")
        }
        guard Set(ids).count == ids.count else {
            throw BridgeError.invalidParam("ids must be unique")
        }
        return ids
    }

    private func isConversationBulkResult(_ value: [String: Any]) -> Bool {
        if let error = value["error"] as? String,
           !error.isEmpty,
           let ids = value["ids"] as? [Any],
           ids.allSatisfy({ $0 is String }) {
            return true
        }
        guard let requestedCount = value["requestedCount"], integralInt(requestedCount) != nil else {
            return false
        }
        return ["affectedIds", "unchangedIds", "missingIds"].allSatisfy { key in
            guard let ids = value[key] as? [Any] else { return false }
            return ids.allSatisfy { $0 is String }
        }
    }

    private func conversationProjectMoveIds(_ params: Any) throws -> [String] {
        let ids = try stringArrayParam(params, at: 0, name: "ids")
        guard (1...500).contains(ids.count) else {
            throw BridgeError.invalidParam("ids must contain from 1 through 500 values")
        }
        guard ids.allSatisfy({ !$0.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty }) else {
            throw BridgeError.invalidParam("ids must contain only nonempty values")
        }
        guard Set(ids).count == ids.count else {
            throw BridgeError.invalidParam("ids must be unique")
        }
        return ids
    }

    private func nullableNonemptyStringParam(_ params: Any, at index: Int, name: String) throws -> String? {
        guard let raw = getParam(params, at: index, name: name) else { throw BridgeError.missingParam(name) }
        if raw is NSNull { return nil }
        guard let value = raw as? String, !value.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty else {
            throw BridgeError.invalidParam("\(name) must be null or a nonempty string")
        }
        return value
    }

    private func requiredJSONArrayPayload(_ payload: String?, operation: String) throws -> [Any] {
        guard let payload, let data = payload.data(using: .utf8),
              let result = try? JSONSerialization.jsonObject(with: data) as? [Any] else {
            throw BridgeError.operationFailed("\(operation) returned invalid native payload")
        }
        return result
    }

    private func requiredJSONObjectPayload(_ payload: String?, operation: String) throws -> [String: Any] {
        guard let payload, let data = payload.data(using: .utf8),
              let result = try? JSONSerialization.jsonObject(with: data) as? [String: Any] else {
            throw BridgeError.operationFailed("\(operation) returned invalid native payload")
        }
        return result
    }

    private func conversationAutoArchiveDays(_ params: Any) throws -> Int32 {
        guard let raw = getParam(params, at: 0, name: "afterDays") else {
            throw BridgeError.invalidParam("afterDays is required")
        }
        if raw is NSNull { return -1 }
        guard let days = integralInt(raw), [-1, 3, 7, 30].contains(days) else {
            throw BridgeError.invalidParam("afterDays must be null, -1, 3, 7, or 30")
        }
        return Int32(days)
    }

    private func integralInt(_ value: Any) -> Int? {
        guard let number = value as? NSNumber, CFGetTypeID(number) != CFBooleanGetTypeID() else {
            return nil
        }
        let double = number.doubleValue
        guard double.isFinite, double.rounded(.towardZero) == double,
              double >= Double(Int.min), double <= Double(Int.max) else { return nil }
        return Int(double)
    }

    private func stringArrayParam(_ params: Any, at index: Int, name: String) throws -> [String] {
        guard let values = getParam(params, at: index, name: name) as? [Any],
              values.allSatisfy({ $0 is String }) else {
            throw BridgeError.invalidParam("\(name) must be an array of strings")
        }
        return values.compactMap { $0 as? String }
    }

    /// Normalizes the `scope` RPC parameter into the exact JSON core expects:
    /// `{"kind":"new_task"}` or `{"kind":"conversation","conversationId":"<id>"}`.
    /// Anything else is rejected here so a malformed scope can never be written.
    private func composerDraftScopeJson(_ params: Any) throws -> String {
        let raw = getParam(params, at: 0, name: "scope")
        guard let scope = raw as? [String: Any], let kind = scope["kind"] as? String else {
            throw BridgeError.missingParam("scope")
        }

        let normalized: [String: Any]
        switch kind {
        case "new_task":
            normalized = ["kind": "new_task"]
        case "conversation":
            guard let conversationId = scope["conversationId"] as? String, !conversationId.isEmpty else {
                throw BridgeError.missingParam("scope.conversationId")
            }
            normalized = ["kind": "conversation", "conversationId": conversationId]
        default:
            throw BridgeError.invalidParam("scope.kind must be new_task or conversation")
        }

        guard let data = try? JSONSerialization.data(withJSONObject: normalized),
              let json = String(data: data, encoding: .utf8) else {
            throw BridgeError.encodingFailed
        }
        return json
    }

    private func ptrParam(_ params: Any, at index: Int, name: String) throws -> OpaquePointer {
        guard let raw = getParam(params, at: index, name: name) as? Int, raw != 0 else {
            throw BridgeError.missingParam(name)
        }
        return OpaquePointer(bitPattern: raw)!
    }

    private func sessionParam(_ params: Any, at index: Int, name: String) throws -> OpaquePointer {
        let handle = try stringParam(params, at: index, name: name)
        guard let ptr = chatRPC.pointerForHandle(handle) else {
            throw BridgeError.operationFailed("Session handle not found: \(handle)")
        }
        return ptr
    }

    private func agentSessionParam(_ params: Any, at index: Int, name: String) throws -> OpaquePointer {
        let handle = try stringParam(params, at: index, name: name)
        guard let ptr = agentHandleResolver(handle) else {
            throw BridgeError.operationFailed("Agent handle not found: \(handle)")
        }
        return ptr
    }

    private func dataParam(_ params: Any, at index: Int, name: String) throws -> Data {
        guard let val = getParam(params, at: index, name: name) else {
            throw BridgeError.missingParam(name)
        }
        if let arr = val as? [Any] {
            let bytes = arr.compactMap { $0 as? Int }.map { UInt8(clamping: $0) }
            return Data(bytes)
        } else if let str = val as? String {
            if let d = Data(base64Encoded: str) {
                return d
            }
            return Data(str.utf8)
        } else if let data = val as? Data {
            return data
        }
        throw BridgeError.missingParam(name)
    }

    private func jsonArray<T: Encodable>(_ values: [T]) throws -> [[String: Any]] {
        let data = try JSONEncoder().encode(values)
        guard let arr = try JSONSerialization.jsonObject(with: data) as? [[String: Any]] else {
            throw BridgeError.encodingFailed
        }
        return arr
    }

    private func boundedBrowserToolResult(_ result: [String: Any]) -> [String: Any] {
        guard let data = try? JSONSerialization.data(withJSONObject: result),
              data.count > maxBrowserToolResultBytes,
              let entries = result["result"] as? [Any] else {
            return result
        }

        var bounded = result
        var kept: [Any] = []
        for entry in entries {
            bounded["result"] = kept + [entry]
            bounded["truncated"] = true
            guard let candidateData = try? JSONSerialization.data(withJSONObject: bounded),
                  candidateData.count <= maxBrowserToolResultBytes else {
                break
            }
            kept.append(entry)
        }
        bounded["result"] = kept
        bounded["truncated"] = true
        return bounded
    }

    private func jsonObject<T: Encodable>(_ value: T) throws -> [String: Any] {
        let data = try JSONEncoder().encode(value)
        guard let obj = try JSONSerialization.jsonObject(with: data) as? [String: Any] else {
            throw BridgeError.encodingFailed
        }
        return obj
    }
}

// MARK: - Conversation RPC dependencies

struct ConversationRPCDependencies {
    let listPayload: (_ state: String, _ limit: Int) -> String?
    let archive: (_ id: String) -> Bool
    let unarchive: (_ id: String) -> Bool
    let bulk: (_ operation: String, _ ids: [String]) -> String?
    let getPolicy: () -> Int32
    let setPolicy: (_ days: Int32) -> Bool

    static func live(bridge: MahoBridge) -> ConversationRPCDependencies {
        ConversationRPCDependencies(
            listPayload: { bridge.listConversationsPayload(state: $0, limit: $1) },
            archive: { bridge.archiveConversation(id: $0) },
            unarchive: { bridge.unarchiveConversation(id: $0) },
            bulk: { bridge.applyConversationBulkOperation(operation: $0, ids: $1) },
            getPolicy: { bridge.getConversationAutoArchivePolicy() },
            setPolicy: { bridge.setConversationAutoArchivePolicy(days: $0) }
        )
    }
}

// MARK: - Chat RPC dependencies

struct ChatSessionRPCOptions {
    var apiKey: String
    var endpoint: String
    var model: String
    var systemInstruction: String
    var credentialProvider: String?

    init(_ value: [String: Any]) {
        apiKey = value["apiKey"] as? String ?? ""
        endpoint = value["endpoint"] as? String ?? ""
        model = value["model"] as? String ?? ""
        systemInstruction = value["systemInstruction"] as? String ?? ""
        credentialProvider = value["credentialProvider"] as? String
    }
}

struct ChatSessionRPCConfig: Equatable {
    let apiKey: String
    let endpoint: String
    let model: String
    let systemInstruction: String
}

struct ChatSessionRPCConversation {
    let model: String?
}

struct ChatSessionRPCMessage {
    let role: String
    let content: String
}

struct ChatSessionRPCDependencies {
    let create: (ChatSessionRPCConfig) -> OpaquePointer?
    let handleForPointer: (OpaquePointer) -> String?
    let pointerForHandle: (String) -> OpaquePointer?
    let hasSession: (String) -> Bool
    let conversation: (String) -> ChatSessionRPCConversation?
    let bind: (String, OpaquePointer) -> Void
    let messages: (String) -> [ChatSessionRPCMessage]
    let appendUser: (OpaquePointer, String) -> Void
    let appendAssistant: (OpaquePointer, String) -> Void
    let sendToolResult: (OpaquePointer, String, String, String, Bool) -> Bool
    let resolveSecret: (String) -> String?
    let resolveManagedProvider: () -> AiProviderConfig?

    static func live(bridge: MahoBridge) -> ChatSessionRPCDependencies {
        ChatSessionRPCDependencies(
            create: { config in
                bridge.createChatSession(
                    apiKey: config.apiKey,
                    endpoint: config.endpoint,
                    model: config.model,
                    systemInstruction: config.systemInstruction
                )
            },
            handleForPointer: { ChatSessionRegistry.shared.findHandle(for: $0) },
            pointerForHandle: { ChatSessionRegistry.shared.get(id: $0) },
            hasSession: { ChatSessionRegistry.shared.get(id: $0) != nil },
            conversation: { conversationId in
                bridge.listConversations().first(where: { $0.id == conversationId }).map {
                    ChatSessionRPCConversation(model: $0.model)
                }
            },
            bind: { conversationId, ptr in
                ChatSessionRegistry.shared.unregisterWithoutFree(ptr)
                ChatSessionRegistry.shared.register(id: conversationId, ptr: ptr)
            },
            messages: { conversationId in
                bridge.getConversationMessages(sessionId: conversationId).map {
                    ChatSessionRPCMessage(role: $0.role, content: $0.content)
                }
            },
            appendUser: { bridge.chatAppendUserMessage($0, content: $1) },
            appendAssistant: { bridge.chatAppendAssistantMessage($0, content: $1, toolCallsJson: "[]") },
            sendToolResult: { bridge.chatSendToolResult($0, toolCallId: $1, toolName: $2, result: $3, trigger: $4) },
            resolveSecret: { BYOKKeychain.get(provider: $0) },
            resolveManagedProvider: { AiProviderResolver.resolveManaged(bridge: bridge) }
        )
    }
}

struct BrowserToolExecutor {
    let invoke: (_ name: String, _ args: [String: Any]) -> [String: Any]

    static func live(bridge: MahoBridge) -> BrowserToolExecutor {
        BrowserToolExecutor { name, args in
            switch name {
            case "list_tabs":
                return ["ok": true, "result": bridge.getTabViewModels().filter { !$0.isPrivate }.map { tab in
                    ["id": tab.id, "spaceId": tab.spaceId, "url": tab.url, "title": tab.title]
                }]
            case "get_page_info":
                guard
                    let activeTabId = bridge.getActiveTabId(),
                    let tab = bridge.getTabViewModels().first(where: { $0.id == activeTabId })
                else {
                    return ["ok": false, "error": "no_active_tab"]
                }
                return ["ok": true, "result": ["id": tab.id, "url": tab.url, "title": tab.title]]
            case "search_bookmarks":
                let query = args["query"] as? String ?? ""
                return ["ok": true, "result": bridge.searchBookmarks(query: query).map { bookmark in
                    ["id": bookmark.id, "url": bookmark.url, "title": bookmark.title]
                }]
            case "search_history":
                let query = args["query"] as? String ?? ""
                let limit = args["limit"] as? Int ?? 20
                return ["ok": true, "result": bridge.searchHistory(query: query, limit: limit).map { entry in
                    ["id": entry.id, "url": entry.url, "title": entry.title]
                }]
            case "open_tab":
                guard let url = args["url"] as? String, let spaceId = bridge.getActiveSpaceId() else {
                    return ["ok": false, "error": "missing_url"]
                }
                bridge.createTab(url: Url(url), inSpace: spaceId)
                return ["ok": true, "result": ["url": url]]
            case "navigate":
                guard let tabId = bridge.getActiveTabId(), let url = args["url"] as? String else {
                    return ["ok": false, "error": "missing_tab_id_or_url"]
                }
                bridge.navigate(tabId: tabId, url: Url(url))
                return ["ok": true, "result": ["tab_id": tabId, "url": url]]
            case "close_tab":
                guard let tabId = args["tab_id"] as? String else {
                    return ["ok": false, "error": "missing_tab_id"]
                }
                bridge.closeTab(id: tabId)
                return ["ok": true, "result": ["tab_id": tabId]]
            case "create_bookmark":
                guard let url = args["url"] as? String else {
                    return ["ok": false, "error": "missing_url"]
                }
                bridge.addBookmark(url: url, title: args["title"] as? String ?? url, folderId: args["folder_id"] as? String)
                return ["ok": true, "result": ["url": url]]
            default:
                return ["ok": false, "error": "unknown_tool"]
            }
        }
    }
}

// MARK: - Native artifact sharing

final class ArtifactShareCoordinator {
    typealias Presenter = (URL, @escaping () -> Void) -> Bool

    static let shared = ArtifactShareCoordinator()

    private let fileManager: FileManager
    private let tempRoot: URL
    private let presenter: Presenter

    init(
        fileManager: FileManager = .default,
        tempRoot: URL? = nil,
        presenter: Presenter? = nil
    ) {
        self.fileManager = fileManager
        self.tempRoot = tempRoot ?? fileManager.temporaryDirectory
            .appendingPathComponent("MahoArtifactShares", isDirectory: true)
        self.presenter = presenter ?? { fileURL, completion in
            NativeArtifactSharePresenter.present(fileURL: fileURL, completion: completion)
        }
        cleanupStaleExports()
    }

    func share(data: Data, displayName: String) throws {
        let exportDirectory = tempRoot.appendingPathComponent(UUID().uuidString, isDirectory: true)
        let exportURL = exportDirectory.appendingPathComponent(Self.sanitizedDisplayName(displayName))
        do {
            try fileManager.createDirectory(at: exportDirectory, withIntermediateDirectories: true)
            try data.write(to: exportURL, options: [.atomic, .completeFileProtection])
        } catch {
            try? fileManager.removeItem(at: exportDirectory)
            throw BridgeError.operationFailed("Artifact export failed")
        }

        DispatchQueue.main.async { [fileManager, presenter] in
            let cleanup: () -> Void = {
                try? fileManager.removeItem(at: exportDirectory)
            }
            if !presenter(exportURL, cleanup) {
                cleanup()
            }
        }
    }

    static func sanitizedDisplayName(_ displayName: String) -> String {
        let pathParts = displayName
            .split(whereSeparator: { $0 == "/" || $0 == "\\" })
            .map(String.init)
            .filter { !$0.isEmpty && $0 != "." && $0 != ".." }
        let joined = pathParts.joined(separator: " ")
        let invalid = CharacterSet(charactersIn: "/\\:\0?%*|\"<>").union(.controlCharacters)
        let sanitizedScalars = joined.unicodeScalars.map { invalid.contains($0) ? "_" : String($0) }
        let collapsed = sanitizedScalars.joined()
            .split(whereSeparator: { $0.isWhitespace })
            .joined(separator: " ")
            .trimmingCharacters(in: CharacterSet(charactersIn: ". "))
        return collapsed.isEmpty ? "Maho Artifact" : String(collapsed.prefix(180))
    }

    private func cleanupStaleExports() {
        try? fileManager.removeItem(at: tempRoot)
        try? fileManager.createDirectory(at: tempRoot, withIntermediateDirectories: true)
    }
}

enum NativeArtifactSharePresenter {
    static func present(
        fileURL: URL,
        rootViewController: UIViewController? = nil,
        activityFactory: ([Any]) -> UIActivityViewController = {
            UIActivityViewController(activityItems: $0, applicationActivities: nil)
        },
        completion: @escaping () -> Void
    ) -> Bool {
        precondition(Thread.isMainThread)
        let root = rootViewController ?? UIApplication.shared.connectedScenes
            .compactMap { $0 as? UIWindowScene }
            .first(where: { $0.activationState == .foregroundActive })?
            .windows.first(where: \.isKeyWindow)?.rootViewController
            ?? UIApplication.shared.connectedScenes
                .compactMap { $0 as? UIWindowScene }
                .first?.windows.first?.rootViewController
        guard let root else { return false }

        let presenter = topPresenter(from: root)

        let activity = activityFactory([fileURL])
        activity.completionWithItemsHandler = { _, _, _, _ in completion() }
        if let popover = activity.popoverPresentationController {
            popover.sourceView = presenter.view
            popover.sourceRect = CGRect(
                x: presenter.view.bounds.midX,
                y: presenter.view.bounds.maxY - ShellTheme.Size.bottomBarMinHeight,
                width: 1,
                height: 1
            )
            popover.permittedArrowDirections = []
        }
        presenter.present(activity, animated: true)
        return true
    }

    static func topPresenter(from root: UIViewController) -> UIViewController {
        var presenter = root
        while let presented = presenter.presentedViewController {
            presenter = presented
        }
        return presenter
    }
}

// MARK: - Errors

private enum BridgeError: LocalizedError {
    case missingParam(String)
    case invalidParam(String)
    case unknownMethod(String)
    case operationFailed(String)
    case encodingFailed

    var rpcKind: String {
        switch self {
        case .missingParam, .invalidParam: return "params_invalid"
        case .operationFailed: return "operation_failed"
        case .unknownMethod: return "method_not_found"
        case .encodingFailed: return "internal"
        }
    }

    var errorDescription: String? {
        switch self {
        case .missingParam(let n):    return "Missing or invalid parameter: \(n)"
        case .invalidParam(let msg):  return "Invalid parameter: \(msg)"
        case .unknownMethod(let m):   return "Unknown bridge method: \(m)"
        case .operationFailed(let m): return "Operation failed: \(m)"
        case .encodingFailed:         return "Failed to encode response to JSON"
        }
    }

    var rpcReason: String {
        switch self {
        case .operationFailed(let reason): return reason
        default: return localizedDescription
        }
    }
}

// MARK: - Convenience factory for the AI WebView

extension WebViewBridgeController {

    /// Build a WKWebView pre-wired with this bridge controller.
    /// The host view controller retains the controller and calls attach(to:).
    static func makeWebView(
        bridge: MahoBridge = .shared,
        onBack: (() -> Void)? = nil,
        onOpenSettings: (() -> Void)? = nil,
        onCompleteOnboarding: (() -> Void)? = nil
    ) -> (webView: WKWebView, controller: WebViewBridgeController) {
        let controller = WebViewBridgeController(
            bridge: bridge,
            onBack: onBack,
            onOpenSettings: onOpenSettings,
            onCompleteOnboarding: onCompleteOnboarding
        )
        let ucc = WKUserContentController()
        ucc.add(controller, name: "mahoBridge")
        ucc.add(controller, name: "mahoBridgeNav")

        let config = WKWebViewConfiguration()
        config.userContentController = ucc
        // Allow file:// access for the bundled assets
        config.preferences.javaScriptEnabled = true

        let wv = WKWebView(frame: .zero, configuration: config)
        controller.attach(to: wv)
        return (wv, controller)
    }

    /// Load the bundled web-ai assets.
    /// Pass `screen` as the hash name, e.g. "byok", "chat".
    static func loadBundle(
        into webView: WKWebView,
        screen: String,
        params: [String: String] = [:]
    ) {
        // Dev mode: load from local Vite server
        let isDev = UserDefaults.standard.bool(forKey: "MahoWebAIDev")
        if isDev {
            var hash = "#\(screen)"
            if !params.isEmpty {
                let query = params
                    .map { "\($0.key)=\($0.value.addingPercentEncoding(withAllowedCharacters: .urlQueryAllowed) ?? $0.value)" }
                    .joined(separator: "&")
                hash += "?\(query)"
            }
            if let url = URL(string: "http://localhost:5173/\(hash)") {
                webView.load(URLRequest(url: url))
            }
            return
        }

        let storagePath = FileManager.default
            .urls(for: .applicationSupportDirectory, in: .userDomainMask)
            .first?
            .appendingPathComponent("dev.maho.browser", isDirectory: true)
            .path
        let assetURL: URL? = {
            if let storagePath, let provisioned = MahoBridge.shared.getProvisionedAssetURL(storagePath: storagePath, relativePath: "web-ai/index.html") {
                return provisioned
            }
            return Bundle.main.url(
                forResource: "index",
                withExtension: "html",
                subdirectory: "web-ai"
            )
        }()

        guard let bundleURL = assetURL else {
            assertionFailure("[WebViewBridgeController] web-ai/index.html not found in bundle or provisioned storage")
            return
        }

        var hash = "#\(screen)"
        if !params.isEmpty {
            let query = params
                .map { "\($0.key)=\($0.value.addingPercentEncoding(withAllowedCharacters: .urlQueryAllowed) ?? $0.value)" }
                .joined(separator: "&")
            hash += "?\(query)"
        }

        // WKWebView requires a file URL; embed the hash as a URL fragment so
        // app.tsx routes to the correct screen at boot (a post-load JS hash set
        // races the document and can leave the router on the default screen).
        let accessURL = bundleURL.deletingLastPathComponent()
        let targetURL: URL
        if hash.isEmpty || hash == "#" {
            targetURL = bundleURL
        } else {
            targetURL = URL(string: bundleURL.absoluteString + hash) ?? bundleURL
        }
        webView.loadFileURL(targetURL, allowingReadAccessTo: accessURL)
    }
}

// MARK: - StreamSession helper class

private final class StreamSession {
    private let sessionId: String
    private weak var webView: WKWebView?
    private var buffer: [String] = []
    private var droppedCount = 0
    private var flushScheduled = false
    private var thinkingBuffer: [String] = []
    private var thinkingFlushScheduled = false
    private var thinkingWorkItem: DispatchWorkItem?
    private let lock = NSLock()
    private var flushWorkItem: DispatchWorkItem?

    init(sessionId: String, webView: WKWebView?) {
        self.sessionId = sessionId
        self.webView = webView
    }

    func pushToken(_ token: String) {
        lock.lock()
        buffer.append(token)
        if buffer.count > 256 {
            buffer.removeFirst()
            droppedCount += 1
        }
        
        if buffer.count >= 32 {
            let snapshot = buffer
            let drops = droppedCount
            buffer.removeAll()
            droppedCount = 0
            flushScheduled = false
            flushWorkItem?.cancel()
            flushWorkItem = nil
            lock.unlock()
            dispatchBatch(tokens: snapshot, drops: drops)
        } else {
            if !flushScheduled {
                flushScheduled = true
                let workItem = DispatchWorkItem { [weak self] in
                    self?.tryFlush()
                }
                flushWorkItem = workItem
                DispatchQueue.main.asyncAfter(deadline: .now() + .milliseconds(60), execute: workItem)
            }
            lock.unlock()
        }
    }

    private func tryFlush() {
        lock.lock()
        guard flushScheduled else {
            lock.unlock()
            return
        }
        flushScheduled = false
        if buffer.isEmpty {
            lock.unlock()
            return
        }
        let snapshot = buffer
        let drops = droppedCount
        buffer.removeAll()
        droppedCount = 0
        lock.unlock()
        dispatchBatch(tokens: snapshot, drops: drops)
    }

    func pushThinking(_ thinking: String) {
        lock.lock()
        thinkingBuffer.append(thinking)
        if thinkingBuffer.count > 256 {
            thinkingBuffer.removeFirst()
        }

        if thinkingBuffer.count >= 32 {
            let snapshot = thinkingBuffer
            thinkingBuffer.removeAll()
            thinkingFlushScheduled = false
            thinkingWorkItem?.cancel()
            thinkingWorkItem = nil
            lock.unlock()
            dispatchThinkingBatch(chunks: snapshot)
        } else {
            if !thinkingFlushScheduled {
                thinkingFlushScheduled = true
                let workItem = DispatchWorkItem { [weak self] in
                    self?.tryFlushThinking()
                }
                thinkingWorkItem = workItem
                DispatchQueue.main.asyncAfter(deadline: .now() + .milliseconds(60), execute: workItem)
            }
            lock.unlock()
        }
    }

    private func tryFlushThinking() {
        lock.lock()
        guard thinkingFlushScheduled else {
            lock.unlock()
            return
        }
        thinkingFlushScheduled = false
        if thinkingBuffer.isEmpty {
            lock.unlock()
            return
        }
        let snapshot = thinkingBuffer
        thinkingBuffer.removeAll()
        lock.unlock()
        dispatchThinkingBatch(chunks: snapshot)
    }

    private func dispatchThinkingBatch(chunks: [String]) {
        guard let webView = webView else { return }
        let combined = chunks.joined()
        let escaped = combined
            .replacingOccurrences(of: "\\", with: "\\\\")
            .replacingOccurrences(of: "'", with: "\\'")
        let js = "window.__mahoStreamThinking('\(sessionId)', '\(escaped)')"
        DispatchQueue.main.async {
            webView.evaluateJavaScript(js, completionHandler: nil)
        }
    }

    private func dispatchBatch(tokens: [String], drops: Int) {
        guard let webView = webView else { return }
        guard let tokensData = try? JSONSerialization.data(withJSONObject: tokens),
              let tokensJson = String(data: tokensData, encoding: .utf8) else {
            return
        }
        
        let js: String
        if drops > 0 {
            js = "window.__mahoStreamBatch('\(sessionId)', \(tokensJson), \(drops))"
        } else {
            js = "window.__mahoStreamBatch('\(sessionId)', \(tokensJson))"
        }
        
        DispatchQueue.main.async {
            webView.evaluateJavaScript(js, completionHandler: nil)
        }
    }

    func complete(finalMessage: String) {
        lock.lock()
        flushScheduled = false
        flushWorkItem?.cancel()
        flushWorkItem = nil
        thinkingFlushScheduled = false
        thinkingWorkItem?.cancel()
        thinkingWorkItem = nil
        let pendingSnapshot = buffer.isEmpty ? nil : buffer
        let drops = droppedCount
        buffer.removeAll()
        droppedCount = 0
        let pendingThinking = thinkingBuffer.isEmpty ? nil : thinkingBuffer
        thinkingBuffer.removeAll()
        lock.unlock()

        if let pending = pendingSnapshot {
            dispatchBatch(tokens: pending, drops: drops)
        }
        if let pendingT = pendingThinking {
            dispatchThinkingBatch(chunks: pendingT)
        }

        guard let webView = webView else { return }
        let escapedMsg = finalMessage
            .replacingOccurrences(of: "\\", with: "\\\\")
            .replacingOccurrences(of: "'", with: "\\'")
        let js = "window.__mahoStreamComplete('\(sessionId)', '\(escapedMsg)')"
        DispatchQueue.main.async {
            webView.evaluateJavaScript(js, completionHandler: nil)
        }
    }

    func error(message: String) {
        lock.lock()
        flushScheduled = false
        flushWorkItem?.cancel()
        flushWorkItem = nil
        thinkingFlushScheduled = false
        thinkingWorkItem?.cancel()
        thinkingWorkItem = nil
        let pendingSnapshot = buffer.isEmpty ? nil : buffer
        let drops = droppedCount
        buffer.removeAll()
        droppedCount = 0
        let pendingThinking = thinkingBuffer.isEmpty ? nil : thinkingBuffer
        thinkingBuffer.removeAll()
        lock.unlock()

        if let pending = pendingSnapshot {
            dispatchBatch(tokens: pending, drops: drops)
        }
        if let pendingT = pendingThinking {
            dispatchThinkingBatch(chunks: pendingT)
        }

        guard let webView = webView else { return }
        let escapedErr = message
            .replacingOccurrences(of: "\\", with: "\\\\")
            .replacingOccurrences(of: "'", with: "\\'")
        let js = "window.__mahoStreamError('\(sessionId)', '\(escapedErr)')"
        DispatchQueue.main.async {
            webView.evaluateJavaScript(js, completionHandler: nil)
        }
    }
}
