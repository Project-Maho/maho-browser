import Foundation
import UIKit
import WebKit
import os.log

@MainActor
final class WebViewCoordinator: NSObject, WKNavigationDelegate, WKUIDelegate, UIGestureRecognizerDelegate {
    weak var webView: WKWebView?
    var onPinchSummarize: (() -> Void)?
    var onNewTabRequested: ((URL) -> Void)?
    private let viewModel: BrowserViewModel
    private let bridge = MahoBridge.shared
    private let log = Logger(subsystem: "dev.maho.browser", category: "WebView")

    private var titleObservation: NSKeyValueObservation?
    private var urlObservation: NSKeyValueObservation?
    private var progressObservation: NSKeyValueObservation?
    private var canGoBackObservation: NSKeyValueObservation?
    private var canGoForwardObservation: NSKeyValueObservation?
    private var pinchSummarizeTriggered = false
    private var activeDownloads: [ObjectIdentifier: String] = [:]
    private var downloadObservations: [ObjectIdentifier: NSKeyValueObservation] = [:]
    private var originMediaPermissions: [String: [WKMediaCaptureType: WKPermissionDecision]] = [:]
    private var crashTimestamps: [Date] = []

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

        for (_, observation) in downloadObservations {
            observation.invalidate()
        }
        downloadObservations.removeAll()
        for (_, downloadId) in activeDownloads {
            bridge.cancelDownload(id: downloadId)
        }
        activeDownloads.removeAll()

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
            viewModel.lastErrorMessage = nil
            viewModel.failedUrl = nil
            guard !viewModel.tabId.isEmpty else { return }
            bridge.sendEvent(.tabLoadingChanged(tabId: viewModel.tabId, isLoading: true))
            bridge.refreshContentRules(webView: webView)
        }
    }

    nonisolated func webView(_ webView: WKWebView, didFinish navigation: WKNavigation!) {
        Task { @MainActor in
            crashTimestamps.removeAll()
            viewModel.isLoading = false
            viewModel.loadingProgress = 1.0
            viewModel.lastErrorMessage = nil
            viewModel.failedUrl = nil
            guard !viewModel.tabId.isEmpty else { return }
            bridge.sendEvent(.tabLoadingChanged(tabId: viewModel.tabId, isLoading: false))
            let tabId = viewModel.tabId
            Task { @MainActor in
                if let snapshot = await captureWebViewScreenshot(webView) {
                    bridge.updateTabPreview(tabId: tabId, imageData: snapshot)
                }
            }
            captureFavicon(webView: webView, tabId: tabId)
        }
    }

    nonisolated func webView(_ webView: WKWebView, didFail navigation: WKNavigation!, withError error: Error) {
        Task { @MainActor in
            viewModel.isLoading = false
            let nsError = error as NSError
            if (nsError.domain == "WebKitErrorDomain" && nsError.code == 102) || (nsError.domain == NSURLErrorDomain && nsError.code == NSURLErrorCancelled) {
                return
            }
            viewModel.lastErrorMessage = error.localizedDescription
            viewModel.failedUrl = webView.url?.absoluteString ?? viewModel.currentUrl
            log.error("Navigation failed: \(error.localizedDescription, privacy: .public)")
            guard !viewModel.tabId.isEmpty else { return }
            bridge.sendEvent(.tabLoadingChanged(tabId: viewModel.tabId, isLoading: false))
        }
    }

    nonisolated func webView(_ webView: WKWebView, didFailProvisionalNavigation navigation: WKNavigation!, withError error: Error) {
        Task { @MainActor in
            viewModel.isLoading = false
            let nsError = error as NSError
            if (nsError.domain == "WebKitErrorDomain" && nsError.code == 102) || (nsError.domain == NSURLErrorDomain && nsError.code == NSURLErrorCancelled) {
                return
            }
            viewModel.lastErrorMessage = error.localizedDescription
            viewModel.failedUrl = webView.url?.absoluteString ?? viewModel.currentUrl
            log.error("Provisional navigation failed: \(error.localizedDescription, privacy: .public)")
            guard !viewModel.tabId.isEmpty else { return }
            bridge.sendEvent(.tabLoadingChanged(tabId: viewModel.tabId, isLoading: false))
        }
    }

    nonisolated func webViewWebContentProcessDidTerminate(_ webView: WKWebView) {
        Task { @MainActor in
            log.warning("WebContent process terminated (OOM or crash). Recovering...")
            viewModel.isLoading = false

            let now = Date()
            crashTimestamps = crashTimestamps.filter { now.timeIntervalSince($0) < 15 }
            crashTimestamps.append(now)

            if crashTimestamps.count >= 3 {
                log.error("WebContent process repeatedly terminated (>= 3 times in 15s). Displaying error overlay.")
                viewModel.lastErrorMessage = "A problem repeatedly occurred with this webpage."
                viewModel.failedUrl = viewModel.currentUrl
            } else if !viewModel.currentUrl.isEmpty && viewModel.currentUrl != "about:blank" {
                webView.reload()
            }
        }
    }

    nonisolated func webView(
        _ webView: WKWebView,
        didReceive challenge: URLAuthenticationChallenge,
        completionHandler: @escaping (URLSession.AuthChallengeDisposition, URLCredential?) -> Void
    ) {
        completionHandler(.performDefaultHandling, nil)
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
                if UIApplication.shared.canOpenURL(url) {
                    await UIApplication.shared.open(url)
                } else {
                    self.log.warning("Cannot open external scheme URL: \(url.absoluteString, privacy: .public)")
                }
            }
            decisionHandler(.cancel)
        }
    }

    nonisolated func webView(
        _ webView: WKWebView,
        decidePolicyFor navigationResponse: WKNavigationResponse,
        decisionHandler: @escaping (WKNavigationResponsePolicy) -> Void
    ) {
        if !navigationResponse.canShowMIMEType {
            if #available(iOS 14.5, *) {
                decisionHandler(.download)
                return
            }
        }
        decisionHandler(.allow)
    }

    @available(iOS 14.5, *)
    nonisolated func webView(_ webView: WKWebView, navigationAction: WKNavigationAction, didBecome download: WKDownload) {
        download.delegate = self
    }

    @available(iOS 14.5, *)
    nonisolated func webView(_ webView: WKWebView, navigationResponse: WKNavigationResponse, didBecome download: WKDownload) {
        download.delegate = self
    }

    nonisolated func webView(
        _ webView: WKWebView,
        createWebViewWith configuration: WKWebViewConfiguration,
        for navigationAction: WKNavigationAction,
        windowFeatures: WKWindowFeatures
    ) -> WKWebView? {
        if navigationAction.targetFrame == nil || !(navigationAction.targetFrame?.isMainFrame ?? false) {
            if let targetUrl = navigationAction.request.url, !targetUrl.absoluteString.isEmpty, targetUrl.absoluteString != "about:blank" {
                Task { @MainActor in
                    if let onNewTabRequested = self.onNewTabRequested {
                        onNewTabRequested(targetUrl)
                    } else {
                        webView.load(navigationAction.request)
                    }
                }
            }
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
            presentAlert(alert, onFallback: { completionHandler() })
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
            presentAlert(alert, onFallback: { completionHandler(false) })
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
            presentAlert(alert, onFallback: { completionHandler(nil) })
        }
    }

    private func presentAlert(_ alert: UIAlertController, retryCount: Int = 0, onFallback: (() -> Void)? = nil) {
        guard let scene = UIApplication.shared.connectedScenes.first(where: { $0.activationState == .foregroundActive }) as? UIWindowScene ?? UIApplication.shared.connectedScenes.first as? UIWindowScene,
              let rootVC = scene.windows.first(where: { $0.isKeyWindow })?.rootViewController ?? scene.windows.first?.rootViewController else {
            onFallback?()
            return
        }
        var presenter = rootVC
        while let presented = presenter.presentedViewController, !presented.isBeingDismissed {
            presenter = presented
        }
        if presenter.isBeingPresented {
            if retryCount < 5 {
                DispatchQueue.main.asyncAfter(deadline: .now() + 0.3) { [weak self] in
                    self?.presentAlert(alert, retryCount: retryCount + 1, onFallback: onFallback)
                }
            } else {
                onFallback?()
            }
            return
        }
        presenter.present(alert, animated: true)
    }

    nonisolated func webViewDidClose(_ webView: WKWebView) {
        Task { @MainActor in
            guard !viewModel.tabId.isEmpty else { return }
            bridge.closeTab(id: viewModel.tabId)
        }
    }

    @available(iOS 15.0, *)
    nonisolated func webView(
        _ webView: WKWebView,
        requestMediaCapturePermissionFor origin: WKSecurityOrigin,
        initiatedByFrame frame: WKFrameInfo,
        type: WKMediaCaptureType,
        decisionHandler: @escaping (WKPermissionDecision) -> Void
    ) {
        Task { @MainActor in
            let typeName: String
            switch type {
            case .camera:
                typeName = "camera"
            case .microphone:
                typeName = "microphone"
            case .cameraAndMicrophone:
                typeName = "camera and microphone"
            @unknown default:
                typeName = "media devices"
            }

            let originHost = origin.host.isEmpty ? "This page" : origin.host

            if let cachedDecision = self.originMediaPermissions[originHost]?[type] {
                decisionHandler(cachedDecision)
                return
            }

            let alert = UIAlertController(
                title: "\(originHost) wants to use your \(typeName)",
                message: "Do you want to allow this website to access your \(typeName)?",
                preferredStyle: .alert
            )
            alert.addAction(UIAlertAction(title: "Don't Allow", style: .cancel) { [weak self] _ in
                self?.recordMediaPermission(host: originHost, type: type, decision: .deny)
                decisionHandler(.deny)
            })
            alert.addAction(UIAlertAction(title: "Allow", style: .default) { [weak self] _ in
                self?.recordMediaPermission(host: originHost, type: type, decision: .grant)
                decisionHandler(.grant)
            })
            presentAlert(alert, onFallback: { [weak self] in
                self?.recordMediaPermission(host: originHost, type: type, decision: .deny)
                decisionHandler(.deny)
            })
        }
    }

    private func recordMediaPermission(host: String, type: WKMediaCaptureType, decision: WKPermissionDecision) {
        if originMediaPermissions[host] == nil {
            originMediaPermissions[host] = [:]
        }
        originMediaPermissions[host]?[type] = decision
    }

    private func captureFavicon(webView: WKWebView, tabId: TabId) {
        guard let pageURL = webView.url,
              let scheme = pageURL.scheme,
              scheme == "http" || scheme == "https" else { return }
        let js = "(function(){var l=document.querySelector('link[rel~=\"icon\"],link[rel=\"shortcut icon\"],link[rel=\"apple-touch-icon\"]');return l&&l.href?l.href:(location.origin+'/favicon.ico');})()"
        webView.evaluateJavaScript(js) { [weak self] result, _ in
            guard let self,
                  let href = result as? String,
                  let iconURL = Foundation.URL(string: href) else { return }
            Task {
                guard let (data, _) = try? await URLSession.shared.data(from: iconURL),
                      let image = UIImage(data: data),
                      let pngData = image.pngData() else { return }
                let imageData = ImageData(
                    data: [UInt8](pngData),
                    width: UInt32(max(image.size.width * image.scale, 1)),
                    height: UInt32(max(image.size.height * image.scale, 1)),
                    format: .png
                )
                await MainActor.run {
                    guard !tabId.isEmpty else { return }
                    self.bridge.sendEvent(.tabFaviconUpdated(tabId: tabId, favicon: imageData))
                }
            }
        }
    }
}

extension WebViewCoordinator: WKScriptMessageHandler {
    func userContentController(_ userContentController: WKUserContentController, didReceive message: WKScriptMessage) {
        guard message.name == "mahoReader" else { return }
        guard let dict = message.body as? [String: Any] else { return }
        
        if let error = dict["error"] as? String {
            log.error("Reader Mode extraction failed: \(error)")
            return
        }
        
        let title = dict["title"] as? String ?? ""
        let content = dict["content"] as? String ?? ""
        let textContent = dict["textContent"] as? String ?? ""
        let byline = dict["byline"] as? String ?? ""
        let dir = dict["dir"] as? String
        
        let excerpt = dict["excerpt"] as? String
        let siteName = dict["siteName"] as? String
        let length = dict["length"] as? Int
        let publishedTime = dict["publishedTime"] as? String
        
        let article = ExtractedArticle(
            title: title,
            content: content,
            textContent: textContent,
            byline: byline,
            textDirection: TextDirection(readabilityDir: dir),
            excerpt: excerpt,
            siteName: siteName,
            length: length,
            publishedTime: publishedTime
        )
        viewModel.extractedArticle = article
    }
}

extension WebViewCoordinator: WKDownloadDelegate {
    @available(iOS 14.5, *)
    nonisolated func download(
        _ download: WKDownload,
        decideDestinationUsing response: URLResponse,
        suggestedFilename: String,
        completionHandler: @escaping (URL?) -> Void
    ) {
        let docs = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first ?? FileManager.default.temporaryDirectory
        let dest = docs.appendingPathComponent(suggestedFilename)
        try? FileManager.default.removeItem(at: dest)

        let urlStr = response.url?.absoluteString ?? ""
        let totalBytes = response.expectedContentLength > 0 ? response.expectedContentLength : 0
        let mimeType = response.mimeType

        Task { @MainActor in
            if let downloadId = self.bridge.startDownload(
                filename: suggestedFilename,
                url: urlStr,
                totalBytes: totalBytes,
                filePath: dest.path,
                mimeType: mimeType
            ) {
                let key = ObjectIdentifier(download)
                self.activeDownloads[key] = downloadId

                let observation = download.progress.observe(\.completedUnitCount) { [weak self] prog, _ in
                    guard let self = self else { return }
                    let bytes = UInt64(max(0, prog.completedUnitCount))
                    Task { @MainActor in
                        self.bridge.updateDownloadProgress(id: downloadId, receivedBytes: bytes)
                    }
                }
                self.downloadObservations[key] = observation
            }
        }

        completionHandler(dest)
    }

    @available(iOS 14.5, *)
    nonisolated func downloadDidFinish(_ download: WKDownload) {
        Task { @MainActor in
            self.log.info("Download completed successfully")
            let key = ObjectIdentifier(download)
            self.downloadObservations.removeValue(forKey: key)?.invalidate()
            if let downloadId = self.activeDownloads.removeValue(forKey: key) {
                self.bridge.completeDownload(id: downloadId)
            }
        }
    }

    @available(iOS 14.5, *)
    nonisolated func download(_ download: WKDownload, didFailWithError error: Error, resumeData: Data?) {
        Task { @MainActor in
            self.log.error("Download failed: \(error.localizedDescription, privacy: .public)")
            let key = ObjectIdentifier(download)
            self.downloadObservations.removeValue(forKey: key)?.invalidate()
            if let downloadId = self.activeDownloads.removeValue(forKey: key) {
                self.bridge.cancelDownload(id: downloadId)
            }
        }
    }
}
