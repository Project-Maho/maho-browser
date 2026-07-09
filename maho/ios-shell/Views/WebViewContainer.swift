import SwiftUI
import UIKit
import WebKit

struct WebViewContainer: UIViewRepresentable {
    @ObservedObject var viewModel: BrowserViewModel
    var onPinchSummarize: (() -> Void)?

    func makeCoordinator() -> WebViewCoordinator {
        WebViewCoordinator(viewModel: viewModel)
    }

    func makeUIView(context: Context) -> WKWebView {
        let configuration = WKWebViewConfiguration()
        let preferences = WKWebpagePreferences()
        preferences.allowsContentJavaScript = true
        configuration.defaultWebpagePreferences = preferences
        configuration.allowsInlineMediaPlayback = true
        configuration.allowsPictureInPictureMediaPlayback = true
        configuration.mediaTypesRequiringUserActionForPlayback = [.all]

        configuration.userContentController.add(context.coordinator, name: "mahoReader")
        Self.installReadabilityUserScript(on: configuration.userContentController)
        let webView = WKWebView(frame: .zero, configuration: configuration)
        webView.allowsBackForwardNavigationGestures = true
        webView.navigationDelegate = context.coordinator
        webView.uiDelegate = context.coordinator

        let refreshControl = UIRefreshControl()
        refreshControl.addTarget(
            context.coordinator,
            action: #selector(WebViewCoordinator.handleRefresh(_:)),
            for: .valueChanged
        )
        webView.scrollView.refreshControl = refreshControl

        let pinchRecognizer = UIPinchGestureRecognizer(
            target: context.coordinator,
            action: #selector(WebViewCoordinator.handlePinchToSummarize(_:))
        )
        pinchRecognizer.delegate = context.coordinator
        webView.scrollView.addGestureRecognizer(pinchRecognizer)

        context.coordinator.webView = webView
        context.coordinator.onPinchSummarize = onPinchSummarize
        context.coordinator.startObserving()

        return webView
    }

    func updateUIView(_ webView: WKWebView, context: Context) {
        context.coordinator.onPinchSummarize = onPinchSummarize
    }

    static func dismantleUIView(_ webView: WKWebView, coordinator: WebViewCoordinator) {
        webView.configuration.userContentController.removeScriptMessageHandler(forName: "mahoReader")
        coordinator.stopObserving()
        coordinator.webView = nil
    }

    /// Pre-registers Readability.js at `.atDocumentEnd` so the extractor is
    /// available on every page load without re-injecting the 2812-line script.
    /// The reader-mode toggle then only evaluates a small `runMahoReadability()`
    /// call, which reduces per-toggle payload from ~350KB to ~200 bytes and
    /// avoids parse-time reflow of loaded pages.
    private static func installReadabilityUserScript(on controller: WKUserContentController) {
        guard let url = Bundle.main.url(forResource: "Readability", withExtension: "js"),
              let source = try? String(contentsOf: url, encoding: .utf8) else {
            return
        }
        let wrapped = """
        \(source)
        (function() {
            window.__mahoRunReadability = function() {
                try {
                    var docClone = document.cloneNode(true);
                    var article = new Readability(docClone).parse();
                    if (article) {
                        window.webkit.messageHandlers.mahoReader.postMessage({
                            title: article.title || "",
                            content: article.content || "",
                            textContent: article.textContent || "",
                            byline: article.byline || "",
                            dir: article.dir || "",
                            excerpt: article.excerpt || null,
                            siteName: article.siteName || null,
                            length: article.length || null,
                            publishedTime: article.publishedTime || null
                        });
                    } else {
                        window.webkit.messageHandlers.mahoReader.postMessage({ error: "Failed to parse page content." });
                    }
                } catch (e) {
                    window.webkit.messageHandlers.mahoReader.postMessage({ error: e.toString() });
                }
            };
        })();
        """
        let script = WKUserScript(
            source: wrapped,
            injectionTime: .atDocumentEnd,
            forMainFrameOnly: true
        )
        controller.addUserScript(script)
    }
}
