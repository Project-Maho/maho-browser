import AppKit
import WebKit

final class IncognitoWindowController: NSWindowController {

    private var webView: WKWebView!

    convenience init() {
        let window = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 900, height: 600),
            styleMask: [.titled, .closable, .miniaturizable, .resizable],
            backing: .buffered,
            defer: false
        )
        window.title = "Incognito"
        window.center()
        window.appearance = NSAppearance(named: .darkAqua)
        window.backgroundColor = NSColor(white: 0.12, alpha: 1.0)
        window.minSize = NSSize(width: 400, height: 300)

        self.init(window: window)

        setupWebView()
    }

    private func setupWebView() {
        let config = WKWebViewConfiguration()
        config.websiteDataStore = .nonPersistent()

        let prefs = WKPreferences()
        prefs.javaScriptCanOpenWindowsAutomatically = true
        config.preferences = prefs

        let webpagePrefs = WKWebpagePreferences()
        webpagePrefs.allowsContentJavaScript = true
        config.defaultWebpagePreferences = webpagePrefs

        webView = WKWebView(frame: .zero, configuration: config)
        webView.translatesAutoresizingMaskIntoConstraints = false
        webView.allowsBackForwardNavigationGestures = true

        if #available(macOS 13.3, *) {
            webView.isInspectable = true
        }

        guard let contentView = window?.contentView else { return }
        contentView.addSubview(webView)
        NSLayoutConstraint.activate([
            webView.topAnchor.constraint(equalTo: contentView.topAnchor),
            webView.bottomAnchor.constraint(equalTo: contentView.bottomAnchor),
            webView.leadingAnchor.constraint(equalTo: contentView.leadingAnchor),
            webView.trailingAnchor.constraint(equalTo: contentView.trailingAnchor),
        ])

        webView.loadHTMLString(
            "<html><body style='background:#1a1a1a;color:#aaa;font-family:system-ui;display:flex;align-items:center;justify-content:center;height:100vh;margin:0'><p>Incognito Mode</p></body></html>",
            baseURL: nil
        )
    }
}
