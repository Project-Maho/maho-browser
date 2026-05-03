import Foundation
import UIKit
import WebKit
import os.log

@MainActor
final class WebViewCoordinator: NSObject, WKNavigationDelegate, WKUIDelegate, UIGestureRecognizerDelegate {
    weak var webView: WKWebView?
    var onPinchSummarize: (() -> Void)?
    private let viewModel: BrowserViewModel
    private let bridge = MahoBridge.shared
    private let log = Logger(subsystem: "dev.maho.browser", category: "WebView")

    private var titleObservation: NSKeyValueObservation?
    private var urlObservation: NSKeyValueObservation?
    private var progressObservation: NSKeyValueObservation?
    private var canGoBackObservation: NSKeyValueObservation?
    private var canGoForwardObservation: NSKeyValueObservation?
    private var pinchSummarizeTriggered = false

    init(viewModel: BrowserViewModel) {
        self.viewModel = viewModel
        super.init()
    }

    // MARK: - Pinch to Summarize

    @objc func handlePinchToSummarize(_ recognizer: UIPinchGestureRecognizer) {
        guard let scrollView = webView?.scrollView else { return }

        let atMinZoom = abs(scrollView.zoomScale - scrollView.minimumZoomScale) < 0.01

        switch recognizer.state {
        case .began:
            pinchSummarizeTriggered = false
        case .changed:
            if atMinZoom && recognizer.scale < 0.7 && !pinchSummarizeTriggered {
                pinchSummarizeTriggered = true
                onPinchSummarize?()
            }
        case .ended, .cancelled:
            pinchSummarizeTriggered = false
        default:
            break
        }
    }

    nonisolated func gestureRecognizer(
        _ gestureRecognizer: UIGestureRecognizer,
        shouldRecognizeSimultaneouslyWith otherGestureRecognizer: UIGestureRecognizer
    ) -> Bool {
        true
    }

    func startObserving() {
        guard let webView else { return }

        titleObservation = webView.observe(\.title, options: [.new]) { [weak self] _, change in
            Task { @MainActor in
                guard let self, let title = change.newValue ?? nil else { return }
                self.viewModel.currentTitle = title
                guard !self.viewModel.tabId.isEmpty else { return }
                self.bridge.sendEvent(.tabTitleUpdated(tabId: self.viewModel.tabId, title: title))
            }
        }

        urlObservation = webView.observe(\.url, options: [.new]) { [weak self] _, change in
            Task { @MainActor in
                guard let self, let url = change.newValue ?? nil else { return }
                let urlString = url.absoluteString
                self.viewModel.currentUrl = urlString
                self.viewModel.isSecure = urlString.hasPrefix("https://")
                guard !self.viewModel.tabId.isEmpty else { return }
                self.bridge.sendEvent(.tabUrlUpdated(tabId: self.viewModel.tabId, url: Url(urlString)))
            }
        }

        progressObservation = webView.observe(\.estimatedProgress, options: [.new]) { [weak self] _, change in
            Task { @MainActor in
                guard let self, let progress = change.newValue else { return }
                self.viewModel.loadingProgress = progress
            }
        }

        canGoBackObservation = webView.observe(\.canGoBack, options: [.new]) { [weak self] _, change in
            Task { @MainActor in
                guard let self, let canGoBack = change.newValue else { return }
                self.viewModel.canGoBack = canGoBack
                self.sendNavigationState()
            }
        }

        canGoForwardObservation = webView.observe(\.canGoForward, options: [.new]) { [weak self] _, change in
            Task { @MainActor in
                guard let self, let canGoForward = change.newValue else { return }
                self.viewModel.canGoForward = canGoForward
                self.sendNavigationState()
            }
        }
    }

    func stopObserving() {
        titleObservation?.invalidate()
        urlObservation?.invalidate()
        progressObservation?.invalidate()
        canGoBackObservation?.invalidate()
        canGoForwardObservation?.invalidate()

        titleObservation = nil
        urlObservation = nil
        progressObservation = nil
        canGoBackObservation = nil
        canGoForwardObservation = nil
    }

    private func sendNavigationState() {
        guard !viewModel.tabId.isEmpty else { return }
        bridge.sendEvent(.tabNavigationStateChanged(
            tabId: viewModel.tabId,
            canGoBack: viewModel.canGoBack,
            canGoForward: viewModel.canGoForward
        ))
    }

    @objc func handleRefresh(_ refreshControl: UIRefreshControl) {
        viewModel.reload()
        refreshControl.endRefreshing()
    }

    func loadURL(_ urlString: String) {
        guard let url = Foundation.URL(string: urlString) else {
            log.warning("Invalid URL: \(urlString, privacy: .public)")
            return
        }
        webView?.load(URLRequest(url: url))
    }

    nonisolated func webView(_ webView: WKWebView, didStartProvisionalNavigation navigation: WKNavigation!) {
        Task { @MainActor in
            viewModel.isLoading = true
            guard !viewModel.tabId.isEmpty else { return }
            bridge.sendEvent(.tabLoadingChanged(tabId: viewModel.tabId, isLoading: true))
        }
    }

    nonisolated func webView(_ webView: WKWebView, didFinish navigation: WKNavigation!) {
        Task { @MainActor in
            viewModel.isLoading = false
            viewModel.loadingProgress = 1.0
            guard !viewModel.tabId.isEmpty else { return }
            bridge.sendEvent(.tabLoadingChanged(tabId: viewModel.tabId, isLoading: false))
        }
    }

    nonisolated func webView(_ webView: WKWebView, didFail navigation: WKNavigation!, withError error: Error) {
        Task { @MainActor in
            viewModel.isLoading = false
            log.error("Navigation failed: \(error.localizedDescription, privacy: .public)")
            guard !viewModel.tabId.isEmpty else { return }
            bridge.sendEvent(.tabLoadingChanged(tabId: viewModel.tabId, isLoading: false))
        }
    }

    nonisolated func webView(_ webView: WKWebView, didFailProvisionalNavigation navigation: WKNavigation!, withError error: Error) {
        Task { @MainActor in
            viewModel.isLoading = false
            log.error("Provisional navigation failed: \(error.localizedDescription, privacy: .public)")
            guard !viewModel.tabId.isEmpty else { return }
            bridge.sendEvent(.tabLoadingChanged(tabId: viewModel.tabId, isLoading: false))
        }
    }

    nonisolated func webView(
        _ webView: WKWebView,
        decidePolicyFor navigationAction: WKNavigationAction,
        decisionHandler: @escaping (WKNavigationActionPolicy) -> Void
    ) {
        guard let url = navigationAction.request.url else {
            decisionHandler(.cancel)
            return
        }

        let scheme = url.scheme?.lowercased() ?? ""
        if scheme == "http" || scheme == "https" || scheme == "about" || scheme == "data" || scheme == "blob" {
            decisionHandler(.allow)
        } else {
            Task { @MainActor in
                await UIApplication.shared.open(url)
            }
            decisionHandler(.cancel)
        }
    }

    nonisolated func webView(
        _ webView: WKWebView,
        createWebViewWith configuration: WKWebViewConfiguration,
        for navigationAction: WKNavigationAction,
        windowFeatures: WKWindowFeatures
    ) -> WKWebView? {
        if navigationAction.targetFrame == nil || !(navigationAction.targetFrame?.isMainFrame ?? false) {
            webView.load(navigationAction.request)
        }
        return nil
    }

    nonisolated func webView(
        _ webView: WKWebView,
        runJavaScriptAlertPanelWithMessage message: String,
        initiatedByFrame frame: WKFrameInfo,
        completionHandler: @escaping () -> Void
    ) {
        Task { @MainActor in
            let alert = UIAlertController(title: nil, message: message, preferredStyle: .alert)
            alert.addAction(UIAlertAction(title: "OK", style: .default) { _ in
                completionHandler()
            })
            presentAlert(alert)
        }
    }

    nonisolated func webView(
        _ webView: WKWebView,
        runJavaScriptConfirmPanelWithMessage message: String,
        initiatedByFrame frame: WKFrameInfo,
        completionHandler: @escaping (Bool) -> Void
    ) {
        Task { @MainActor in
            let alert = UIAlertController(title: nil, message: message, preferredStyle: .alert)
            alert.addAction(UIAlertAction(title: "Cancel", style: .cancel) { _ in
                completionHandler(false)
            })
            alert.addAction(UIAlertAction(title: "OK", style: .default) { _ in
                completionHandler(true)
            })
            presentAlert(alert)
        }
    }

    nonisolated func webView(
        _ webView: WKWebView,
        runJavaScriptTextInputPanelWithPrompt prompt: String,
        defaultText: String?,
        initiatedByFrame frame: WKFrameInfo,
        completionHandler: @escaping (String?) -> Void
    ) {
        Task { @MainActor in
            let alert = UIAlertController(title: nil, message: prompt, preferredStyle: .alert)
            alert.addTextField { textField in
                textField.text = defaultText
            }
            alert.addAction(UIAlertAction(title: "Cancel", style: .cancel) { _ in
                completionHandler(nil)
            })
            alert.addAction(UIAlertAction(title: "OK", style: .default) { _ in
                completionHandler(alert.textFields?.first?.text)
            })
            presentAlert(alert)
        }
    }

    private func presentAlert(_ alert: UIAlertController) {
        guard let scene = UIApplication.shared.connectedScenes.first as? UIWindowScene,
              let rootVC = scene.windows.first?.rootViewController else {
            return
        }
        var presenter = rootVC
        while let presented = presenter.presentedViewController {
            presenter = presented
        }
        presenter.present(alert, animated: true)
    }
}
