import AppKit
import WebKit

// MARK: - WebViewRegistry

final class WebViewRegistry {
    static let shared = WebViewRegistry()

    private(set) var webViews: [TabId: WKWebView] = [:]
    private var webViewToTabId: [ObjectIdentifier: TabId] = [:]

    private let processPool = WKProcessPool()
    private(set) var sharedConfiguration: WKWebViewConfiguration

    // Phase 2: Track which window displays each tab's WebView
    private(set) var displayedIn: [TabId: WeakRef<BrowserWindowController>] = [:]

    struct WeakRef<T: AnyObject> {
        weak var value: T?
        init(_ value: T) { self.value = value }
    }

    private init() {
        let config = WKWebViewConfiguration()
        config.processPool = processPool

        let prefs = WKPreferences()
        prefs.javaScriptCanOpenWindowsAutomatically = true
        config.preferences = prefs

        let webpagePrefs = WKWebpagePreferences()
        webpagePrefs.allowsContentJavaScript = true
        config.defaultWebpagePreferences = webpagePrefs

        config.userContentController = WKUserContentController()
        config.allowsAirPlayForMediaPlayback = true
        config.mediaTypesRequiringUserActionForPlayback = .all

        let linkHoverScript = LinkPreviewHandler.injectionScript
        let userScript = WKUserScript(source: linkHoverScript, injectionTime: .atDocumentEnd, forMainFrameOnly: true)
        config.userContentController.addUserScript(userScript)
        config.userContentController.add(LinkPreviewHandler.shared, name: "linkHover")

        let mediaScript = WKUserScript(source: MediaSessionBridge.injectionScript, injectionTime: .atDocumentEnd, forMainFrameOnly: true)
        config.userContentController.addUserScript(mediaScript)
        config.userContentController.add(MediaSessionBridge.shared, name: "mediaSession")

        let inlineEditScript = WKUserScript(source: InlineEditHandler.injectionScript, injectionTime: .atDocumentEnd, forMainFrameOnly: false)
        config.userContentController.addUserScript(inlineEditScript)
        config.userContentController.add(InlineEditHandler.shared, name: "inlineEdit")

        let autofillScript = WKUserScript(source: AutofillHandler.injectionScript, injectionTime: .atDocumentEnd, forMainFrameOnly: true)
        config.userContentController.addUserScript(autofillScript)
        config.userContentController.add(AutofillHandler.shared, name: "autofill")

        sharedConfiguration = config
    }

    // MARK: - Profile

    func setProfile(_ profileId: String?) {
        let dataStore = ProfileDataStoreManager.shared.dataStore(for: profileId)

        let config = WKWebViewConfiguration()
        config.processPool = processPool
        config.websiteDataStore = dataStore

        let prefs = WKPreferences()
        prefs.javaScriptCanOpenWindowsAutomatically = true
        config.preferences = prefs

        let webpagePrefs = WKWebpagePreferences()
        webpagePrefs.allowsContentJavaScript = true
        config.defaultWebpagePreferences = webpagePrefs

        config.userContentController = WKUserContentController()
        config.allowsAirPlayForMediaPlayback = true
        config.mediaTypesRequiringUserActionForPlayback = .all

        let linkHoverScript = LinkPreviewHandler.injectionScript
        let userScript = WKUserScript(source: linkHoverScript, injectionTime: .atDocumentEnd, forMainFrameOnly: true)
        config.userContentController.addUserScript(userScript)
        config.userContentController.add(LinkPreviewHandler.shared, name: "linkHover")

        let mediaScript = WKUserScript(source: MediaSessionBridge.injectionScript, injectionTime: .atDocumentEnd, forMainFrameOnly: true)
        config.userContentController.addUserScript(mediaScript)
        config.userContentController.add(MediaSessionBridge.shared, name: "mediaSession")

        let inlineEditScript = WKUserScript(source: InlineEditHandler.injectionScript, injectionTime: .atDocumentEnd, forMainFrameOnly: false)
        config.userContentController.addUserScript(inlineEditScript)
        config.userContentController.add(InlineEditHandler.shared, name: "inlineEdit")

        let autofillScript2 = WKUserScript(source: AutofillHandler.injectionScript, injectionTime: .atDocumentEnd, forMainFrameOnly: true)
        config.userContentController.addUserScript(autofillScript2)
        config.userContentController.add(AutofillHandler.shared, name: "autofill")

        sharedConfiguration = config
    }

    // MARK: - WebView Lifecycle

    /// Creates a WebView but does NOT set delegates or KVO -- caller does that
    func createWebView(for tabId: TabId, url: String?) -> WKWebView {
        if let existing = webViews[tabId] {
            return existing
        }

        let webView = WKWebView(frame: .zero, configuration: sharedConfiguration)
        webView.allowsBackForwardNavigationGestures = true
        webView.allowsMagnification = true
        webView.translatesAutoresizingMaskIntoConstraints = false

        if #available(macOS 13.3, *) {
            webView.isInspectable = true
        }

        webViews[tabId] = webView
        webViewToTabId[ObjectIdentifier(webView)] = tabId


        if let urlString = url, let requestUrl = URL(string: urlString) {
            webView.load(URLRequest(url: requestUrl))
        }

        return webView
    }

    func removeWebView(for tabId: TabId) {
        if let webView = webViews[tabId] {
            webView.stopLoading()
            webView.removeFromSuperview()
            webViewToTabId.removeValue(forKey: ObjectIdentifier(webView))
        }
        webViews.removeValue(forKey: tabId)
        displayedIn.removeValue(forKey: tabId)
    }

    func webView(for tabId: TabId) -> WKWebView? {
        return webViews[tabId]
    }

    func tabId(for webView: WKWebView) -> TabId? {
        return webViewToTabId[ObjectIdentifier(webView)]
    }


    // MARK: - Phase 2: Display Tracking

    func claimForDisplay(tabId: TabId, by controller: BrowserWindowController) -> WKWebView? {
        guard let webView = webViews[tabId] else { return nil }

        // If another window is displaying this tab, notify it
        if let current = displayedIn[tabId]?.value, current !== controller {
            current.webViewWasReclaimed(tabId: tabId)
        }

        webView.removeFromSuperview()
        displayedIn[tabId] = WeakRef(controller)
        return webView
    }

    func relinquish(tabId: TabId) {
        if let webView = webViews[tabId] {
            webView.removeFromSuperview()
        }
        displayedIn.removeValue(forKey: tabId)
    }

    func currentDisplayer(of tabId: TabId) -> BrowserWindowController? {
        return displayedIn[tabId]?.value
    }
}
