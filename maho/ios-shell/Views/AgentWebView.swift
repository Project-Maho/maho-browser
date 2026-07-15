import SwiftUI
import WebKit

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

    func makeUIView(context: Context) -> WKWebView {
        let (webView, controller) = WebViewBridgeController.makeWebView(
            onBack: { dismiss() },
            onOpenSettings: onOpenSettings
        )
        context.coordinator.controller = controller
        WebViewBridgeController.loadBundle(
            into: webView,
            screen: "agent",
            params: query.isEmpty ? [:] : ["goal": query]
        )
        return webView
    }

    func updateUIView(_ webView: WKWebView, context: Context) {}
}
