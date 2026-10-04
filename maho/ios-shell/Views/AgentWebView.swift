import SwiftUI
import WebKit

extension BrowserToolExecutor {
    static func agenticLive(bridge: MahoBridge) -> BrowserToolExecutor {
        let base = BrowserToolExecutor.live(bridge: bridge)
        return BrowserToolExecutor { name, args in
            if AgenticBrowsingDOM.toolNames.contains(name) {
                return AgenticBrowsingDOM.invoke(name: name, args: args)
            }
            return base.invoke(name, args)
        }
    }
}

extension WebViewBridgeController {
    /// Builds the shared web-ai WebView with DOM agent tools routed to the
    /// separate, user-visible browsing WKWebView.
    static func makeAgenticWebView(
        bridge: MahoBridge = .shared,
        onBack: (() -> Void)? = nil,
        onOpenSettings: (() -> Void)? = nil,
        onCompleteOnboarding: (() -> Void)? = nil
    ) -> (webView: WKWebView, controller: WebViewBridgeController) {
        let controller = WebViewBridgeController(
            bridge: bridge,
            onBack: onBack,
            onOpenSettings: onOpenSettings,
            onCompleteOnboarding: onCompleteOnboarding,
            browserToolExecutor: .agenticLive(bridge: bridge)
        )
        let userContentController = WKUserContentController()
        userContentController.add(controller, name: "mahoBridge")
        userContentController.add(controller, name: "mahoBridgeNav")

        let configuration = WKWebViewConfiguration()
        configuration.userContentController = userContentController
        configuration.preferences.javaScriptEnabled = true

        let webView = WKWebView(frame: .zero, configuration: configuration)
        controller.attach(to: webView)
        return (webView, controller)
    }
}

/// SwiftUI host that presents the shared web-ai `#agent` screen (Preact bundle)
/// inside a `WKWebView`, wired to `MahoBridge` through `WebViewBridgeController`.
///
/// The bridge controller is retained by the coordinator for the lifetime of the
/// representable so the JSON-RPC handler is not deallocated while the web view
/// is on screen.
struct AgentWebView: UIViewRepresentable {
    let query: String
    var onOpenSettings: (() -> Void)?

    @Environment(\.dismiss) private var dismiss

    final class Coordinator {
        var controller: WebViewBridgeController?
    }

    func makeCoordinator() -> Coordinator {
        Coordinator()
    }

    static func dismantleUIView(_ uiView: WKWebView, coordinator: Coordinator) {
        coordinator.controller?.dismantle()
    }

    func makeUIView(context: Context) -> WKWebView {
        let (webView, controller) = WebViewBridgeController.makeAgenticWebView(
            onBack: { dismiss() },
            onOpenSettings: onOpenSettings
        )
        context.coordinator.controller = controller
        AppThemeStore.installWebColorSchemeBootstrap(on: webView.configuration.userContentController)
        AppThemeStore.apply(AppThemeStore.persistedTheme(), to: webView)
        WebViewBridgeController.loadBundle(
            into: webView,
            screen: "agent",
            params: query.isEmpty ? [:] : ["goal": query]
        )
        return webView
    }

    func updateUIView(_ webView: WKWebView, context: Context) {
        AppThemeStore.apply(AppThemeStore.persistedTheme(), to: webView)
    }
}
