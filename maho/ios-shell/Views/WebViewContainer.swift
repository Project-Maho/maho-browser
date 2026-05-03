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
        coordinator.stopObserving()
        coordinator.webView = nil
    }
}
