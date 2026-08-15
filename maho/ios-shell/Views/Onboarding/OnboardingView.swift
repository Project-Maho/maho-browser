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

    final class Coordinator {
        var controller: WebViewBridgeController?
    }

    func makeCoordinator() -> Coordinator {
        Coordinator()
    }

    func makeUIView(context: Context) -> WKWebView {
        let (webView, controller) = WebViewBridgeController.makeWebView(
            onBack: {},
            onOpenSettings: {},
            onCompleteOnboarding: onComplete
        )
        context.coordinator.controller = controller
        
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
