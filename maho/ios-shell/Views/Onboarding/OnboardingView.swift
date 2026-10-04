import SwiftUI
import WebKit

struct OnboardingView: View {
    let onComplete: () -> Void

    var body: some View {
        GeometryReader { geometry in
            OnboardingWebView(
                safeAreaInsets: geometry.safeAreaInsets,
                onComplete: onComplete
            )
            .ignoresSafeArea()
        }
    }
}

struct OnboardingWebView: UIViewRepresentable {
    let safeAreaInsets: EdgeInsets
    let onComplete: () -> Void

    final class Coordinator: NSObject, WKNavigationDelegate {
        var controller: WebViewBridgeController?

        func webView(
            _ webView: WKWebView,
            decidePolicyFor navigationAction: WKNavigationAction,
            decisionHandler: @escaping (WKNavigationActionPolicy) -> Void
        ) {
            guard let url = navigationAction.request.url,
                  url.scheme == "maho-theme" else {
                decisionHandler(.allow)
                return
            }

            let theme: Theme?
            switch url.host?.lowercased() {
            case "system": theme = .system
            case "dark": theme = .dark
            case "light": theme = .light
            default: theme = nil
            }

            if let theme {
                MahoBridge.shared.setTheme(theme)
                AppThemeStore.apply(theme, to: webView)
            }
            decisionHandler(.cancel)
        }
    }

    func makeCoordinator() -> Coordinator {
        Coordinator()
    }

    static func dismantleUIView(_ uiView: WKWebView, coordinator: Coordinator) {
        coordinator.controller?.dismantle()
    }

    func makeUIView(context: Context) -> WKWebView {
        let (webView, controller) = WebViewBridgeController.makeAgenticWebView(
            onBack: {},
            onOpenSettings: {},
            onCompleteOnboarding: onComplete
        )
        context.coordinator.controller = controller
        webView.navigationDelegate = context.coordinator
        AppThemeStore.installWebColorSchemeBootstrap(on: webView.configuration.userContentController)
        AppThemeStore.apply(AppThemeStore.persistedTheme(), to: webView)

        let params = [
            "safeAreaTop": "\(safeAreaInsets.top)",
            "safeAreaBottom": "\(safeAreaInsets.bottom)",
            "safeAreaLeft": "\(safeAreaInsets.leading)",
            "safeAreaRight": "\(safeAreaInsets.trailing)"
        ]

        WebViewBridgeController.loadBundle(
            into: webView,
            screen: "onboarding",
            params: params
        )
        return webView
    }

    func updateUIView(_ webView: WKWebView, context: Context) {
        let js = """
        document.documentElement.style.setProperty('--safe-area-top', '\(safeAreaInsets.top)px');
        document.documentElement.style.setProperty('--safe-area-bottom', '\(safeAreaInsets.bottom)px');
        document.documentElement.style.setProperty('--safe-area-left', '\(safeAreaInsets.leading)px');
        document.documentElement.style.setProperty('--safe-area-right', '\(safeAreaInsets.trailing)px');
        """
        webView.evaluateJavaScript(js, completionHandler: nil)
    }
}
