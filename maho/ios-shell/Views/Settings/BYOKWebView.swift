import SwiftUI
import WebKit

/// SwiftUI host for the shared web-ai `#byok` settings screen.
struct BYOKWebView: UIViewRepresentable {
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
            onBack: { dismiss() }
        )
        context.coordinator.controller = controller
        AppThemeStore.installWebColorSchemeBootstrap(on: webView.configuration.userContentController)
        AppThemeStore.apply(AppThemeStore.persistedTheme(), to: webView)
        WebViewBridgeController.loadBundle(into: webView, screen: "byok")
        return webView
    }

    func updateUIView(_ webView: WKWebView, context: Context) {
        AppThemeStore.apply(AppThemeStore.persistedTheme(), to: webView)
    }
}
