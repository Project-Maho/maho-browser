import Foundation
import UIKit
import WebKit

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

    private let bridge: MahoBridge
    private let onBack: (() -> Void)?
    private let onOpenSettings: (() -> Void)?
    private let dispatchQueue = DispatchQueue(
        label: "dev.maho.browser.WebViewBridgeController",
        qos: .userInitiated
    )
    private weak var webView: WKWebView?
    private let streamsLock = NSLock()
    private var streams: [String: StreamSession] = [:]

    init(
        bridge: MahoBridge = .shared,
        onBack: (() -> Void)? = nil,
        onOpenSettings: (() -> Void)? = nil
    ) {
        self.bridge = bridge
        self.onBack = onBack
        self.onOpenSettings = onOpenSettings
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

    func pushToolDelta(sessionId: String, deltaJson: String) {
        guard let webView = webView else { return }
        let js = "window.__mahoStreamToolDelta('\(sessionId)', \(deltaJson))"
        DispatchQueue.main.async {
            webView.evaluateJavaScript(js, completionHandler: nil)
        }
    }

    func pushImageDelta(sessionId: String, deltaJson: String) {
        guard let webView = webView else { return }
        let js = "window.__mahoStreamImageDelta('\(sessionId)', \(deltaJson))"
        DispatchQueue.main.async {
            webView.evaluateJavaScript(js, completionHandler: nil)
        }
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
    }


    /// Attach to a webView after init. Called by the host view controller.
    func attach(to webView: WKWebView) {
        self.webView = webView
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

        do {
            let result = try dispatchMethod(method, params: params)
            return successResponse(id: id, result: result)
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

        // ── Chat Session ──────────────────────────────────────────────────────
        case "chatSessionStart":
            let optsVal = getParam(params, at: 0, name: "opts")
            let opts = optsVal as? [String: Any] ?? [:]
            let apiKey = opts["apiKey"] as? String ?? ""
            let endpoint = opts["endpoint"] as? String ?? ""
            let model = opts["model"] as? String ?? ""
            let systemInstruction = opts["systemInstruction"] as? String ?? ""
            guard let ptr = bridge.createChatSession(
                apiKey: apiKey, endpoint: endpoint,
                model: model, systemInstruction: systemInstruction
            ) else {
                throw BridgeError.operationFailed("chatSessionStart returned nil")
            }
            guard let handle = ChatSessionRegistry.shared.findHandle(for: ptr) else {
                throw BridgeError.operationFailed("chatSessionStart registration failed")
            }
            return handle

        case "chatSessionResume":
            // Resuming is a no-op on iOS native since the session pointer is managed in memory
            return nil

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
            let resultVal = getParam(params, at: 2, name: "result")
            let result = resultVal as? [String: Any] ?? [:]
            let resultJson: String
            if let data = try? JSONSerialization.data(withJSONObject: result),
               let str = String(data: data, encoding: .utf8) {
                resultJson = str
            } else {
                resultJson = "{}"
            }
            return bridge.chatSendToolResult(handle, toolCallId: toolCallId, toolName: "", result: resultJson, trigger: true)

        case "chatAppendAssistantMessage":
            let handle = try sessionParam(params, at: 0, name: "handle")
            let content = try stringParam(params, at: 1, name: "content")
            bridge.chatAppendAssistantMessage(handle, content: content, toolCallsJson: "[]")
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

        // ── Conversations ─────────────────────────────────────────────────────
        case "conversationCreate":
            let metaVal = getParam(params, at: 0, name: "meta")
            let meta = metaVal as? [String: Any] ?? [:]
            let id = meta["id"] as? String ?? UUID().uuidString
            let title = meta["title"] as? String
            let spaceId = meta["spaceId"] as? String
            let model = meta["model"] as? String
            return bridge.createConversation(id: id, title: title, spaceId: spaceId, model: model)

        case "conversationList":
            let sessions = bridge.listConversations(limit: 100)
            return try jsonArray(sessions)

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

        case "conversationGetMessages":
            let id = try stringParam(params, at: 0, name: "id")
            let turns = bridge.getConversationMessages(sessionId: id)
            return try jsonArray(turns)

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

        default:
            throw BridgeError.unknownMethod(method)
        }
    }

    // MARK: - JS response delivery

    private func resolveOnJS(_ responseJson: String) {
        guard let webView else { return }
        let escaped = responseJson
            .replacingOccurrences(of: "\\", with: "\\\\")
            .replacingOccurrences(of: "'", with: "\\'")
        let js = "window.__mahoBridgeResponse('\(escaped)')"
        DispatchQueue.main.async {
            webView.evaluateJavaScript(js, completionHandler: nil)
        }
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

    private func errorResponse(id: Any, message: String) -> String {
        let obj: [String: Any] = [
            "jsonrpc": "2.0",
            "id": id,
            "error": [
                "kind": "internal",
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

    private func stringParamOpt(_ params: Any, at index: Int, name: String) -> String? {
        return getParam(params, at: index, name: name) as? String
    }

    private func ptrParam(_ params: Any, at index: Int, name: String) throws -> OpaquePointer {
        guard let raw = getParam(params, at: index, name: name) as? Int, raw != 0 else {
            throw BridgeError.missingParam(name)
        }
        return OpaquePointer(bitPattern: raw)!
    }

    private func sessionParam(_ params: Any, at index: Int, name: String) throws -> OpaquePointer {
        let handle = try stringParam(params, at: index, name: name)
        guard let ptr = ChatSessionRegistry.shared.get(id: handle) else {
            throw BridgeError.operationFailed("Session handle not found: \(handle)")
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

    private func jsonObject<T: Encodable>(_ value: T) throws -> [String: Any] {
        let data = try JSONEncoder().encode(value)
        guard let obj = try JSONSerialization.jsonObject(with: data) as? [String: Any] else {
            throw BridgeError.encodingFailed
        }
        return obj
    }
}

// MARK: - Errors

private enum BridgeError: LocalizedError {
    case missingParam(String)
    case invalidParam(String)
    case unknownMethod(String)
    case operationFailed(String)
    case encodingFailed

    var errorDescription: String? {
        switch self {
        case .missingParam(let n):    return "Missing or invalid parameter: \(n)"
        case .invalidParam(let msg):  return "Invalid parameter: \(msg)"
        case .unknownMethod(let m):   return "Unknown bridge method: \(m)"
        case .operationFailed(let m): return "Operation failed: \(m)"
        case .encodingFailed:         return "Failed to encode response to JSON"
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
        onOpenSettings: (() -> Void)? = nil
    ) -> (webView: WKWebView, controller: WebViewBridgeController) {
        let controller = WebViewBridgeController(
            bridge: bridge,
            onBack: onBack,
            onOpenSettings: onOpenSettings
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

        guard let bundleURL = Bundle.main.url(
            forResource: "index",
            withExtension: "html",
            subdirectory: "web-ai"
        ) else {
            assertionFailure("[WebViewBridgeController] web-ai/index.html not found in bundle")
            return
        }

        var hash = "#\(screen)"
        if !params.isEmpty {
            let query = params
                .map { "\($0.key)=\($0.value.addingPercentEncoding(withAllowedCharacters: .urlQueryAllowed) ?? $0.value)" }
                .joined(separator: "&")
            hash += "?\(query)"
        }

        // WKWebView requires a URL with the fragment; we reload with JS after the base loads.
        let accessURL = bundleURL.deletingLastPathComponent()
        webView.loadFileURL(bundleURL, allowingReadAccessTo: accessURL)

        if !hash.isEmpty, hash != "#" {
            // After load, push the hash so app.tsx picks up the correct screen.
            let escapedHash = hash
                .replacingOccurrences(of: "\\", with: "\\\\")
                .replacingOccurrences(of: "'", with: "\\'")
            let js = "window.location.hash = '\(escapedHash)'"
            webView.evaluateJavaScript(js, completionHandler: nil)
        }
    }
}

// MARK: - StreamSession helper class

private final class StreamSession {
    private let sessionId: String
    private weak var webView: WKWebView?
    private var buffer: [String] = []
    private var droppedCount = 0
    private var flushScheduled = false
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
        let pendingSnapshot = buffer.isEmpty ? nil : buffer
        let drops = droppedCount
        buffer.removeAll()
        droppedCount = 0
        lock.unlock()

        if let pending = pendingSnapshot {
            dispatchBatch(tokens: pending, drops: drops)
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
        let pendingSnapshot = buffer.isEmpty ? nil : buffer
        let drops = droppedCount
        buffer.removeAll()
        droppedCount = 0
        lock.unlock()

        if let pending = pendingSnapshot {
            dispatchBatch(tokens: pending, drops: drops)
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

