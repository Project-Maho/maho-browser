import AppKit
import WebKit

// MARK: - WebViewManager

final class WebViewManager: NSObject {
    var webViews: [TabId: WKWebView] { WebViewRegistry.shared.webViews }
    private(set) var activeTabId: TabId?
    private(set) var profileId: String?
    weak var owner: BrowserWindowController?

    private var titleObservations: [TabId: NSKeyValueObservation] = [:]
    private var urlObservations: [TabId: NSKeyValueObservation] = [:]
    private var loadingObservations: [TabId: NSKeyValueObservation] = [:]
    private var securityObservations: [TabId: NSKeyValueObservation] = [:]
    private var estimatedProgressObservations: [TabId: NSKeyValueObservation] = [:]

    override init() {
        super.init()
    }

    private func resolvedActiveSpaceId() -> SpaceId? {
        MahoBridge.shared.getCanonicalActiveSpaceId()
    }

    // MARK: - Profile

    func setProfile(_ profileId: String?) {
        self.profileId = profileId
        WebViewRegistry.shared.setProfile(profileId)
    }

    // MARK: - WebView Lifecycle

    func createWebView(for tabId: TabId, url: String?) -> WKWebView {
        let webView = WebViewRegistry.shared.createWebView(for: tabId, url: url)
        webView.navigationDelegate = self
        webView.uiDelegate = self
        observeWebView(webView, tabId: tabId)
        return webView
    }

    func removeWebView(for tabId: TabId) {
        removeObservations(for: tabId)
        if let webView = WebViewRegistry.shared.webView(for: tabId) {
            webView.navigationDelegate = nil
            webView.uiDelegate = nil
        }
        WebViewRegistry.shared.removeWebView(for: tabId)
        if activeTabId == tabId {
            activeTabId = nil
        }
    }

    func activateWebView(for tabId: TabId, in container: NSView) {
        if let currentId = activeTabId, let currentView = WebViewRegistry.shared.webView(for: currentId) {
            currentView.removeFromSuperview()
        }

        activeTabId = tabId

        guard let ownerController = owner,
              let webView = WebViewRegistry.shared.claimForDisplay(tabId: tabId, by: ownerController) else {
            guard let webView = WebViewRegistry.shared.webView(for: tabId) else { return }
            webView.navigationDelegate = self
            webView.uiDelegate = self
            webView.translatesAutoresizingMaskIntoConstraints = false
            container.addSubview(webView)
            NSLayoutConstraint.activate([
                webView.topAnchor.constraint(equalTo: container.topAnchor),
                webView.bottomAnchor.constraint(equalTo: container.bottomAnchor),
                webView.leadingAnchor.constraint(equalTo: container.leadingAnchor),
                webView.trailingAnchor.constraint(equalTo: container.trailingAnchor),
            ])
            return
        }

        webView.navigationDelegate = self
        webView.uiDelegate = self
        webView.translatesAutoresizingMaskIntoConstraints = false
        container.addSubview(webView)

        NSLayoutConstraint.activate([
            webView.topAnchor.constraint(equalTo: container.topAnchor),
            webView.bottomAnchor.constraint(equalTo: container.bottomAnchor),
            webView.leadingAnchor.constraint(equalTo: container.leadingAnchor),
            webView.trailingAnchor.constraint(equalTo: container.trailingAnchor),
        ])
    }

    func clearActiveWebView() {
        if let currentId = activeTabId, let currentView = WebViewRegistry.shared.webView(for: currentId) {
            currentView.removeFromSuperview()
        }
        activeTabId = nil
    }

    // MARK: - Navigation

    func loadURL(_ url: String, in tabId: TabId) {
        guard let webView = WebViewRegistry.shared.webView(for: tabId),
              let requestUrl = URL(string: url) else { return }
        webView.load(URLRequest(url: requestUrl))
    }

    func goBack(tabId: TabId) {
        WebViewRegistry.shared.webView(for: tabId)?.goBack()
    }

    func goForward(tabId: TabId) {
        WebViewRegistry.shared.webView(for: tabId)?.goForward()
    }

    func reload(tabId: TabId) {
        WebViewRegistry.shared.webView(for: tabId)?.reload()
    }

    func stop(tabId: TabId) {
        WebViewRegistry.shared.webView(for: tabId)?.stopLoading()
    }

    // MARK: - Zoom

    func setZoom(_ zoomLevel: Double, for tabId: TabId) {
        guard let webView = WebViewRegistry.shared.webView(for: tabId) else { return }
        webView.pageZoom = CGFloat(zoomLevel)
    }

    // MARK: - Print

    func printWebView(tabId: TabId, in window: NSWindow) {
        guard let webView = WebViewRegistry.shared.webView(for: tabId) else { return }
        let printInfo = NSPrintInfo.shared
        let printOp = webView.printOperation(with: printInfo)
        printOp.runModal(for: window, delegate: nil, didRun: nil, contextInfo: nil)
    }


    // MARK: - KVO Observations

    private func observeWebView(_ webView: WKWebView, tabId: TabId) {
        titleObservations[tabId] = webView.observe(\.title, options: [.new]) { [weak self] wv, _ in
            guard let self = self, let tabId = self.tabId(for: wv) else { return }
            let title = wv.title ?? ""
            DispatchQueue.main.async {
                CoreUpdateDispatcher.shared.sendAndDispatch(
                    .tabTitleUpdated(tabId: tabId, title: title)
                )
            }
        }

        urlObservations[tabId] = webView.observe(\.url, options: [.new]) { [weak self] wv, _ in
            guard let self = self, let tabId = self.tabId(for: wv) else { return }
            let urlString = wv.url?.absoluteString ?? ""
            DispatchQueue.main.async {
                CoreUpdateDispatcher.shared.sendAndDispatch(
                    .tabUrlUpdated(tabId: tabId, url: MahoUrl(urlString))
                )
            }
        }

        loadingObservations[tabId] = webView.observe(\.isLoading, options: [.new]) { [weak self] wv, _ in
            guard let self = self, let tabId = self.tabId(for: wv) else { return }
            let loading = wv.isLoading
            DispatchQueue.main.async {
                CoreUpdateDispatcher.shared.sendAndDispatch(
                    .tabLoadingChanged(tabId: tabId, isLoading: loading)
                )
            }
        }

        securityObservations[tabId] = webView.observe(\.hasOnlySecureContent, options: [.new]) { [weak self] wv, _ in
            guard let self = self, let tabId = self.tabId(for: wv) else { return }
            let isSecure = wv.hasOnlySecureContent
            DispatchQueue.main.async {
                CoreUpdateDispatcher.shared.sendAndDispatch(
                    .tabSecurityChanged(tabId: tabId, isSecure: isSecure)
                )
            }
        }

        estimatedProgressObservations[tabId] = webView.observe(\.estimatedProgress, options: [.new]) { wv, _ in
            _ = wv.estimatedProgress
        }
    }

    private func removeObservations(for tabId: TabId) {
        titleObservations.removeValue(forKey: tabId)?.invalidate()
        urlObservations.removeValue(forKey: tabId)?.invalidate()
        loadingObservations.removeValue(forKey: tabId)?.invalidate()
        securityObservations.removeValue(forKey: tabId)?.invalidate()
        estimatedProgressObservations.removeValue(forKey: tabId)?.invalidate()
    }

    // MARK: - Helpers

    private func tabId(for webView: WKWebView) -> TabId? {
        return WebViewRegistry.shared.tabId(for: webView)
    }
}

// MARK: - WKNavigationDelegate

extension WebViewManager: WKNavigationDelegate {

    func webView(_ webView: WKWebView, didStartProvisionalNavigation navigation: WKNavigation!) {
        guard let tabId = tabId(for: webView) else { return }
        CoreUpdateDispatcher.shared.sendAndDispatch(
            .tabLoadingChanged(tabId: tabId, isLoading: true)
        )
    }

    func webView(_ webView: WKWebView, didCommit navigation: WKNavigation!) {
        guard let tabId = tabId(for: webView) else { return }
        let canGoBack = webView.canGoBack
        let canGoForward = webView.canGoForward
        CoreUpdateDispatcher.shared.sendAndDispatch(
            .tabNavigationStateChanged(tabId: tabId, canGoBack: canGoBack, canGoForward: canGoForward)
        )
    }

    func webView(_ webView: WKWebView, didFinish navigation: WKNavigation!) {
        guard let tabId = tabId(for: webView) else { return }
        let title = webView.title ?? ""
        let urlString = webView.url?.absoluteString ?? ""

        CoreUpdateDispatcher.shared.sendAndDispatch(
            .tabLoadingChanged(tabId: tabId, isLoading: false)
        )
        CoreUpdateDispatcher.shared.sendAndDispatch(
            .tabTitleUpdated(tabId: tabId, title: title)
        )
        CoreUpdateDispatcher.shared.sendAndDispatch(
            .tabUrlUpdated(tabId: tabId, url: MahoUrl(urlString))
        )
        if activeTabId == tabId {
            AppDelegate.writeUITestState([
                "currentLoadedURL": urlString
            ])
        }

        updateFavicon(for: webView, tabId: tabId, pageURL: urlString)

        if !urlString.isEmpty, urlString != "about:blank" {
            MahoBridge.shared.callCoreVoid { core in
                urlString.withCString { urlPtr in
                    title.withCString { titlePtr in
                        maho_core_add_history_entry(core, urlPtr, titlePtr)
                    }
                }
            }
        }

        injectCssModsIfNeeded(into: webView)
    }

    private func updateFavicon(for webView: WKWebView, tabId: TabId, pageURL: String) {
        guard !pageURL.isEmpty, pageURL != "about:blank" else {
            CoreUpdateDispatcher.shared.sendAndDispatch(.tabFaviconUpdated(tabId: tabId, favicon: nil))
            return
        }

        let expectedPageURL = pageURL
        let faviconProbe = """
        (() => {
            const selectors = [
                'link[rel~="icon"]',
                'link[rel="shortcut icon"]',
                'link[rel*="icon"]',
                'link[rel="apple-touch-icon"]'
            ];
            for (const selector of selectors) {
                const link = document.querySelector(selector);
                if (link && link.href) { return link.href; }
            }
            return null;
        })()
        """

        webView.evaluateJavaScript(faviconProbe) { [weak webView] result, error in
            guard error == nil else { return }
            guard let webView = webView else { return }
            guard webView.url?.absoluteString == expectedPageURL else { return }

            guard let href = result as? String,
                  let faviconURL = URL(string: href) else {
                CoreUpdateDispatcher.shared.sendAndDispatch(.tabFaviconUpdated(tabId: tabId, favicon: nil))
                return
            }

            URLSession.shared.dataTask(with: faviconURL) { data, _, _ in
                let favicon = data.flatMap { Self.imageData(from: $0) }
                DispatchQueue.main.async {
                    guard webView.url?.absoluteString == expectedPageURL else { return }
                    CoreUpdateDispatcher.shared.sendAndDispatch(.tabFaviconUpdated(tabId: tabId, favicon: favicon))
                }
            }.resume()
        }
    }

    private func injectCssModsIfNeeded(into webView: WKWebView) {
        guard let css = MahoBridge.shared.getCombinedModCss(), !css.isEmpty else { return }

        // Escape CSS for JavaScript string embedding
        let escapedCss = css
            .replacingOccurrences(of: "\\", with: "\\\\")
            .replacingOccurrences(of: "'", with: "\\'")
            .replacingOccurrences(of: "\n", with: "\\n")
            .replacingOccurrences(of: "\r", with: "")

        let script = """
        (function() {
            var existingStyle = document.getElementById('maho-css-mods');
            if (existingStyle) existingStyle.remove();
            var style = document.createElement('style');
            style.id = 'maho-css-mods';
            style.textContent = '\(escapedCss)';
            (document.head || document.documentElement).appendChild(style);
        })();
        """

        webView.evaluateJavaScript(script, completionHandler: nil)
    }

    private static func imageData(from data: Data) -> ImageData? {
        guard let image = NSImage(data: data),
              let tiffData = image.tiffRepresentation,
              let bitmap = NSBitmapImageRep(data: tiffData),
              let pngData = bitmap.representation(using: .png, properties: [:]) else {
            return nil
        }

        return ImageData(
            data: [UInt8](pngData),
            width: UInt32(max(1, Int(image.size.width.rounded()))),
            height: UInt32(max(1, Int(image.size.height.rounded()))),
            format: .png
        )
    }

    func webView(_ webView: WKWebView, didFailProvisionalNavigation navigation: WKNavigation!, withError error: Error) {
        guard let tabId = tabId(for: webView) else { return }
        CoreUpdateDispatcher.shared.sendAndDispatch(
            .tabLoadingChanged(tabId: tabId, isLoading: false)
        )
    }

    func webView(_ webView: WKWebView, didFail navigation: WKNavigation!, withError error: Error) {
        guard let tabId = tabId(for: webView) else { return }
        CoreUpdateDispatcher.shared.sendAndDispatch(
            .tabLoadingChanged(tabId: tabId, isLoading: false)
        )
    }

    func webView(
        _ webView: WKWebView,
        didReceive challenge: URLAuthenticationChallenge,
        completionHandler: @escaping (URLSession.AuthChallengeDisposition, URLCredential?) -> Void
    ) {
        guard challenge.protectionSpace.authenticationMethod == NSURLAuthenticationMethodHTTPBasic ||
              challenge.protectionSpace.authenticationMethod == NSURLAuthenticationMethodHTTPDigest else {
            completionHandler(.performDefaultHandling, nil)
            return
        }

        DispatchQueue.main.async {
            let alert = NSAlert()
            alert.messageText = "Authentication Required"
            alert.informativeText = "\(challenge.protectionSpace.host) requires a username and password."
            if challenge.previousFailureCount > 0 {
                alert.informativeText += "\nPrevious login attempt failed."
            }
            alert.addButton(withTitle: "Log In")
            alert.addButton(withTitle: "Cancel")

            let inputStack = NSStackView()
            inputStack.orientation = .vertical
            inputStack.spacing = 8

            let usernameField = NSTextField(frame: NSRect(x: 0, y: 0, width: 300, height: 24))
            usernameField.placeholderString = "Username"

            let passwordField = NSSecureTextField(frame: NSRect(x: 0, y: 0, width: 300, height: 24))
            passwordField.placeholderString = "Password"

            inputStack.addArrangedSubview(usernameField)
            inputStack.addArrangedSubview(passwordField)
            inputStack.frame = NSRect(x: 0, y: 0, width: 300, height: 56)
            alert.accessoryView = inputStack

            guard let window = webView.window else {
                completionHandler(.cancelAuthenticationChallenge, nil)
                return
            }

            alert.beginSheetModal(for: window) { response in
                if response == .alertFirstButtonReturn {
                    let username = usernameField.stringValue
                    let password = passwordField.stringValue
                    let credential = URLCredential(
                        user: username,
                        password: password,
                        persistence: .forSession
                    )
                    completionHandler(.useCredential, credential)
                } else {
                    completionHandler(.cancelAuthenticationChallenge, nil)
                }
            }
        }
    }

    func webView(
        _ webView: WKWebView,
        decidePolicyFor navigationAction: WKNavigationAction,
        decisionHandler: @escaping (WKNavigationActionPolicy) -> Void
    ) {
        if let url = navigationAction.request.url,
           url.scheme == "macappstore" || url.absoluteString.contains("safari-extension") {
            NSWorkspace.shared.open(url)
            decisionHandler(.cancel)
            return
        }

        if navigationAction.targetFrame == nil,
           let url = navigationAction.request.url {
            guard let sourceTabId = tabId(for: webView) else {
                decisionHandler(.cancel)
                return
            }
            let activeSpaceId = resolvedActiveSpaceId() ?? ""
            CoreUpdateDispatcher.shared.sendAndDispatch(
                .createTab(spaceId: activeSpaceId, url: MahoUrl(url.absoluteString), parentId: sourceTabId)
            )
            decisionHandler(.cancel)
            return
        }

        if let url = navigationAction.request.url,
           let rule = ATCRuleEngine.shared.matchingRule(for: url) {
            NotificationCenter.default.post(
                name: Notification.Name("ATCLinkRuleTriggered"),
                object: nil,
                userInfo: ["url": url, "rule": rule]
            )
            if rule.openInNewTab {
                decisionHandler(.cancel)
                return
            }
        }

        decisionHandler(.allow)
    }
}

// MARK: - WKUIDelegate

extension WebViewManager: WKUIDelegate {

    func webView(
        _ webView: WKWebView,
        createWebViewWith configuration: WKWebViewConfiguration,
        for navigationAction: WKNavigationAction,
        windowFeatures: WKWindowFeatures
    ) -> WKWebView? {
        guard let url = navigationAction.request.url,
              let sourceTabId = tabId(for: webView) else { return nil }

        let activeSpaceId = resolvedActiveSpaceId() ?? ""
        CoreUpdateDispatcher.shared.sendAndDispatch(
            .createTab(spaceId: activeSpaceId, url: MahoUrl(url.absoluteString), parentId: sourceTabId)
        )
        return nil
    }

    func webView(
        _ webView: WKWebView,
        runJavaScriptAlertPanelWithMessage message: String,
        initiatedByFrame frame: WKFrameInfo,
        completionHandler: @escaping () -> Void
    ) {
        let alert = NSAlert()
        alert.messageText = frame.request.url?.host ?? "Page"
        alert.informativeText = message
        alert.addButton(withTitle: "OK")
        alert.runModal()
        completionHandler()
    }

    func webView(
        _ webView: WKWebView,
        runJavaScriptConfirmPanelWithMessage message: String,
        initiatedByFrame frame: WKFrameInfo,
        completionHandler: @escaping (Bool) -> Void
    ) {
        let alert = NSAlert()
        alert.messageText = frame.request.url?.host ?? "Page"
        alert.informativeText = message
        alert.addButton(withTitle: "OK")
        alert.addButton(withTitle: "Cancel")
        let response = alert.runModal()
        completionHandler(response == .alertFirstButtonReturn)
    }

    func webView(
        _ webView: WKWebView,
        runJavaScriptTextInputPanelWithPrompt prompt: String,
        defaultText: String?,
        initiatedByFrame frame: WKFrameInfo,
        completionHandler: @escaping (String?) -> Void
    ) {
        let alert = NSAlert()
        alert.messageText = frame.request.url?.host ?? "Page"
        alert.informativeText = prompt
        alert.addButton(withTitle: "OK")
        alert.addButton(withTitle: "Cancel")

        let textField = NSTextField(frame: NSRect(x: 0, y: 0, width: 260, height: 24))
        textField.stringValue = defaultText ?? ""
        alert.accessoryView = textField

        let response = alert.runModal()
        if response == .alertFirstButtonReturn {
            completionHandler(textField.stringValue)
        } else {
            completionHandler(nil)
        }
    }
}
