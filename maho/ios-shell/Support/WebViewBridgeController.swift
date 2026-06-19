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
            let id = obj["id"] as? Int,
            let method = obj["method"] as? String
        else {
            return errorResponse(id: -1, message: "Invalid JSON-RPC request")
        }

        let params = obj["params"] as? [Any] ?? []

        do {
            let result = try dispatchMethod(method, params: params)
            return successResponse(id: id, result: result)
        } catch {
            return errorResponse(id: id, message: error.localizedDescription)
        }
    }

    // swiftlint:disable:next cyclomatic_complexity function_body_length
    private func dispatchMethod(_ method: String, params: [Any]) throws -> Any? {
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
        case "chatSessionNew":
            let apiKey = try stringParam(params, at: 0, name: "apiKey")
            let endpoint = try stringParam(params, at: 1, name: "endpoint")
            let model = try stringParam(params, at: 2, name: "model")
            let instruction = try stringParam(params, at: 3, name: "systemInstruction")
            guard let ptr = bridge.createChatSession(
                apiKey: apiKey, endpoint: endpoint,
                model: model, systemInstruction: instruction
            ) else {
                throw BridgeError.operationFailed("chatSessionNew returned nil")
            }
            return Int(bitPattern: UnsafeRawPointer(ptr))

        case "chatSendUserTurn":
            let sessionPtr = try ptrParam(params, at: 0, name: "sessionPtr")
            let message = try stringParam(params, at: 1, name: "message")
            return bridge.chatSendUserTurn(sessionPtr, message: message)

        case "chatSendImage":
            let sessionPtr = try ptrParam(params, at: 0, name: "sessionPtr")
            let mime = try stringParam(params, at: 1, name: "mime")
            let data = try dataParam(params, at: 2, name: "data")
            return bridge.chatSendImage(sessionPtr, mime: mime, data: data)

        case "chatSendTextWithImage":
            let sessionPtr = try ptrParam(params, at: 0, name: "sessionPtr")
            let text = try stringParam(params, at: 1, name: "text")
            let mime = try stringParam(params, at: 2, name: "mime")
            let data = try dataParam(params, at: 3, name: "data")
            return bridge.chatSendTextWithImage(sessionPtr, text: text, mime: mime, data: data)

        case "chatCancel":
            let sessionPtr = try ptrParam(params, at: 0, name: "sessionPtr")
            bridge.chatCancel(sessionPtr)
            return nil

        case "chatSessionFree":
            let sessionPtr = try ptrParam(params, at: 0, name: "sessionPtr")
            bridge.freeChatSession(sessionPtr)
            return nil

        case "chatSessionPollEvent":
            let sessionPtr = try ptrParam(params, at: 0, name: "sessionPtr")
            return bridge.chatPollEvent(sessionPtr)

        // ── Conversations ─────────────────────────────────────────────────────
        case "createConversation":
            let id = try stringParam(params, at: 0, name: "id")
            let title = params.count > 1 ? params[1] as? String : nil
            let spaceId = params.count > 2 ? params[2] as? String : nil
            let model = params.count > 3 ? params[3] as? String : nil
            return bridge.createConversation(id: id, title: title, spaceId: spaceId, model: model)

        case "listConversations":
            let limit = params.first as? Int ?? 100
            let sessions = bridge.listConversations(limit: limit)
            return try jsonArray(sessions)

        case "getConversationMessages":
            let sessionId = try stringParam(params, at: 0, name: "sessionId")
            let turns = bridge.getConversationMessages(sessionId: sessionId)
            return try jsonArray(turns)

        case "saveConversationMessage":
            let sessionId = try stringParam(params, at: 0, name: "sessionId")
            let role = try stringParam(params, at: 1, name: "role")
            let content = try stringParam(params, at: 2, name: "content")
            let urlContext = params.count > 3 ? params[3] as? String : nil
            return bridge.saveConversationMessage(
                sessionId: sessionId, role: role,
                content: content, urlContext: urlContext
            )

        case "deleteConversation":
            let sessionId = try stringParam(params, at: 0, name: "sessionId")
            return bridge.deleteConversation(sessionId: sessionId)

        case "renameConversation":
            let sessionId = try stringParam(params, at: 0, name: "sessionId")
            let title = try stringParam(params, at: 1, name: "title")
            return bridge.renameConversation(sessionId: sessionId, title: title)

        // ── Space AI Config ───────────────────────────────────────────────────
        case "getSpaceAIConfig":
            let spaceId = try stringParam(params, at: 0, name: "spaceId")
            guard let config = bridge.getSpaceAIConfig(spaceId: spaceId) else { return nil }
            return try jsonObject(config)

        case "setSpaceAIConfig":
            let spaceId = try stringParam(params, at: 0, name: "spaceId")
            let configJson = try stringParam(params, at: 1, name: "config")
            guard
                let data = configJson.data(using: .utf8),
                let config = try? JSONDecoder().decode(SpaceAIConfig.self, from: data)
            else {
                throw BridgeError.invalidParam("config is not valid SpaceAIConfig JSON")
            }
            return bridge.setSpaceAIConfig(spaceId: spaceId, config: config)

        // ── Offline Model ─────────────────────────────────────────────────────
        case "offlineModelInfo":
            guard let info = bridge.offlineModelInfo() else { return nil }
            return try jsonObject(info)

        case "offlineModelIsDownloaded":
            return bridge.offlineModelIsDownloaded()

        case "offlineModelDelete":
            return bridge.offlineModelDelete()

        // ── Native-only actions ───────────────────────────────────────────────
        case "hapticFeedback":
            let style = params.first as? String ?? "medium"
            triggerHaptic(style: style)
            return nil

        case "openSettings":
            DispatchQueue.main.async { [weak self] in
                self?.onOpenSettings?()
            }
            return nil

        case "capturePhoto", "captureScreenshot":
            // These require UIImagePickerController / UIScreen — handled by the
            // host view controller. Returning nil here; the host VC pushes the
            // result via evaluateJavaScript when the capture completes.
            return nil

        default:
            throw BridgeError.unknownMethod(method)
        }
    }

    // MARK: - Haptics

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

    // MARK: - JSON-RPC envelope helpers

    private func successResponse(id: Int, result: Any?) -> String {
        var obj: [String: Any] = ["id": id]
        obj["result"] = result ?? NSNull()
        guard
            let data = try? JSONSerialization.data(withJSONObject: obj),
            let str = String(data: data, encoding: .utf8)
        else {
            return "{\"id\":\(id),\"result\":null}"
        }
        return str
    }

    private func errorResponse(id: Int, message: String) -> String {
        let obj: [String: Any] = ["id": id, "error": ["message": message]]
        guard
            let data = try? JSONSerialization.data(withJSONObject: obj),
            let str = String(data: data, encoding: .utf8)
        else {
            return "{\"id\":\(id),\"error\":{\"message\":\"internal\"}}"
        }
        return str
    }

    // MARK: - Parameter extraction helpers

    private func stringParam(_ params: [Any], at index: Int, name: String) throws -> String {
        guard index < params.count, let value = params[index] as? String else {
            throw BridgeError.missingParam(name)
        }
        return value
    }

    private func ptrParam(_ params: [Any], at index: Int, name: String) throws -> OpaquePointer {
        guard index < params.count, let raw = params[index] as? Int, raw != 0 else {
            throw BridgeError.missingParam(name)
        }
        return OpaquePointer(bitPattern: raw)!
    }

    private func dataParam(_ params: [Any], at index: Int, name: String) throws -> Data {
        guard index < params.count, let arr = params[index] as? [Any] else {
            throw BridgeError.missingParam(name)
        }
        let bytes = arr.compactMap { $0 as? Int }.map { UInt8(clamping: $0) }
        return Data(bytes)
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
