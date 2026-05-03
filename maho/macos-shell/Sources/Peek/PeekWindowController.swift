import AppKit
import WebKit

final class PeekWindowController: NSWindowController {
    private let webView: WKWebView
    private let urlToPreview: String
    private let sourceTabId: TabId

    init(url: String, sourceTabId: TabId) {
        self.urlToPreview = url
        self.sourceTabId = sourceTabId

        let config = WKWebViewConfiguration()
        let prefs = WKPreferences()
        config.preferences = prefs
        webView = WKWebView(frame: .zero, configuration: config)

        let window = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 500, height: 400),
            styleMask: [.titled, .closable, .resizable, .fullSizeContentView],
            backing: .buffered,
            defer: false
        )
        window.title = "Preview"
        window.titlebarAppearsTransparent = true
        window.appearance = NSAppearance(named: .darkAqua)
        window.backgroundColor = MahoAppColors.windowBackground
        window.isMovableByWindowBackground = true
        window.level = .floating
        window.center()

        super.init(window: window)
        setupUI()
        loadPreview()
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    private func setupUI() {
        guard let window = window else { return }

        let container = NSView()
        container.translatesAutoresizingMaskIntoConstraints = false

        webView.translatesAutoresizingMaskIntoConstraints = false

        let toolbar = NSView()
        toolbar.translatesAutoresizingMaskIntoConstraints = false
        toolbar.wantsLayer = true
        toolbar.layer?.backgroundColor = MahoAppColors.cardBackground.cgColor

        let openButton = NSButton(title: "Open in Tab", target: self, action: #selector(openInTab))
        openButton.translatesAutoresizingMaskIntoConstraints = false
        openButton.bezelStyle = .rounded

        let urlLabel = NSTextField(labelWithString: "")
        urlLabel.translatesAutoresizingMaskIntoConstraints = false
        urlLabel.textColor = MahoAppColors.secondaryText
        urlLabel.font = NSFont.systemFont(ofSize: 11)
        urlLabel.lineBreakMode = .byTruncatingMiddle
        urlLabel.tag = 200

        toolbar.addSubview(urlLabel)
        toolbar.addSubview(openButton)

        container.addSubview(toolbar)
        container.addSubview(webView)

        window.contentView = container

        NSLayoutConstraint.activate([
            toolbar.topAnchor.constraint(equalTo: container.topAnchor),
            toolbar.leadingAnchor.constraint(equalTo: container.leadingAnchor),
            toolbar.trailingAnchor.constraint(equalTo: container.trailingAnchor),
            toolbar.heightAnchor.constraint(equalToConstant: 36),

            urlLabel.leadingAnchor.constraint(equalTo: toolbar.leadingAnchor, constant: 12),
            urlLabel.centerYAnchor.constraint(equalTo: toolbar.centerYAnchor),
            urlLabel.trailingAnchor.constraint(lessThanOrEqualTo: openButton.leadingAnchor, constant: -8),

            openButton.trailingAnchor.constraint(equalTo: toolbar.trailingAnchor, constant: -8),
            openButton.centerYAnchor.constraint(equalTo: toolbar.centerYAnchor),

            webView.topAnchor.constraint(equalTo: toolbar.bottomAnchor),
            webView.leadingAnchor.constraint(equalTo: container.leadingAnchor),
            webView.trailingAnchor.constraint(equalTo: container.trailingAnchor),
            webView.bottomAnchor.constraint(equalTo: container.bottomAnchor),
        ])
    }

    private func loadPreview() {
        if let url = URL(string: urlToPreview) {
            webView.load(URLRequest(url: url))
            if let urlLabel = window?.contentView?.viewWithTag(200) as? NSTextField {
                urlLabel.stringValue = urlToPreview
            }
        }
    }

    @objc private func openInTab() {
        guard let spaceId = MahoBridge.shared.getActiveSpaceId() else { return }
        CoreUpdateDispatcher.shared.sendAndDispatch(.createTab(spaceId: spaceId, url: MahoUrl(urlToPreview), parentId: nil))
        close()
    }
}
