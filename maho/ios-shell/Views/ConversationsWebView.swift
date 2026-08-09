import SwiftUI
import WebKit

/// SwiftUI host for the shared web-ai `#conversations` screen.
struct ConversationsWebView: UIViewRepresentable {
    @Environment(\.dismiss) private var dismiss

    final class Coordinator {
        var controller: WebViewBridgeController?
    }

    func makeCoordinator() -> Coordinator {
        Coordinator()
    }

    func makeUIView(context: Context) -> WKWebView {
        let (webView, controller) = WebViewBridgeController.makeWebView(
            onBack: { dismiss() }
        )
        context.coordinator.controller = controller
        WebViewBridgeController.loadBundle(into: webView, screen: "conversations")
        return webView
    }

    func updateUIView(_ webView: WKWebView, context: Context) {}
}
