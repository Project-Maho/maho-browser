import AppKit
import WebKit

final class LittleArcWindowController: NSWindowController {
    private let webView: WKWebView
    private let urlToLoad: String
    private let sourceTabId: TabId?

    init(url: String, sourceTabId: TabId? = nil) {
        self.urlToLoad = url
        self.sourceTabId = sourceTabId

        let config = WKWebViewConfiguration()
        let prefs = WKPreferences()
        config.preferences = prefs
        webView = WKWebView(frame: .zero, configuration: config)

        let window = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 400, height: 500),
            styleMask: [.titled, .closable, .resizable, .fullSizeContentView],
            backing: .buffered,
            defer: false
        )
        window.title = "Little Arc"
        window.titlebarAppearsTransparent = true
        window.appearance = NSAppearance(named: .darkAqua)
        window.backgroundColor = MahoAppColors.windowBackground
        window.isMovableByWindowBackground = true
        window.level = .floating
        window.center()

        super.init(window: window)
        webView.navigationDelegate = self
        setupUI()
        loadURL()
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

        let backButton = NSButton(title: "◀", target: self, action: #selector(goBack))
        backButton.translatesAutoresizingMaskIntoConstraints = false
        backButton.bezelStyle = .rounded
        backButton.isBordered = false
        backButton.font = NSFont.systemFont(ofSize: 12)

        let forwardButton = NSButton(title: "▶", target: self, action: #selector(goForward))
        forwardButton.translatesAutoresizingMaskIntoConstraints = false
        forwardButton.bezelStyle = .rounded
        forwardButton.isBordered = false
        forwardButton.font = NSFont.systemFont(ofSize: 12)

        let promoteButton = NSButton(title: "Open in Tab", target: self, action: #selector(promoteToTab))
        promoteButton.translatesAutoresizingMaskIntoConstraints = false
        promoteButton.bezelStyle = .rounded

        let closeButton = NSButton(title: "Close", target: self, action: #selector(closeWindow))
        closeButton.translatesAutoresizingMaskIntoConstraints = false
        closeButton.bezelStyle = .rounded

        let urlLabel = NSTextField(labelWithString: "")
        urlLabel.translatesAutoresizingMaskIntoConstraints = false
        urlLabel.textColor = MahoAppColors.secondaryText
        urlLabel.font = NSFont.systemFont(ofSize: 11)
        urlLabel.lineBreakMode = .byTruncatingMiddle
        urlLabel.tag = 100

        toolbar.addSubview(backButton)
        toolbar.addSubview(forwardButton)
        toolbar.addSubview(urlLabel)
        toolbar.addSubview(promoteButton)
        toolbar.addSubview(closeButton)

        container.addSubview(toolbar)
        container.addSubview(webView)

        window.contentView = container

        NSLayoutConstraint.activate([
            toolbar.topAnchor.constraint(equalTo: container.topAnchor),
            toolbar.leadingAnchor.constraint(equalTo: container.leadingAnchor),
            toolbar.trailingAnchor.constraint(equalTo: container.trailingAnchor),
            toolbar.heightAnchor.constraint(equalToConstant: 36),

            backButton.leadingAnchor.constraint(equalTo: toolbar.leadingAnchor, constant: 8),
            backButton.centerYAnchor.constraint(equalTo: toolbar.centerYAnchor),

            forwardButton.leadingAnchor.constraint(equalTo: backButton.trailingAnchor, constant: 2),
            forwardButton.centerYAnchor.constraint(equalTo: toolbar.centerYAnchor),

            urlLabel.leadingAnchor.constraint(equalTo: forwardButton.trailingAnchor, constant: 8),
            urlLabel.centerYAnchor.constraint(equalTo: toolbar.centerYAnchor),
            urlLabel.trailingAnchor.constraint(lessThanOrEqualTo: closeButton.leadingAnchor, constant: -8),

            closeButton.trailingAnchor.constraint(equalTo: toolbar.trailingAnchor, constant: -8),
            closeButton.centerYAnchor.constraint(equalTo: toolbar.centerYAnchor),

            promoteButton.trailingAnchor.constraint(equalTo: closeButton.leadingAnchor, constant: -8),
            promoteButton.centerYAnchor.constraint(equalTo: toolbar.centerYAnchor),

            webView.topAnchor.constraint(equalTo: toolbar.bottomAnchor),
            webView.leadingAnchor.constraint(equalTo: container.leadingAnchor),
            webView.trailingAnchor.constraint(equalTo: container.trailingAnchor),
            webView.bottomAnchor.constraint(equalTo: container.bottomAnchor),
        ])
    }

    private func loadURL() {
        if let url = URL(string: urlToLoad) {
            webView.load(URLRequest(url: url))
            if let urlLabel = window?.contentView?.viewWithTag(100) as? NSTextField {
                urlLabel.stringValue = urlToLoad
            }
        }
    }

    @objc private func promoteToTab() {
        guard let spaceId = MahoBridge.shared.getCanonicalActiveSpaceId() else { return }
        CoreUpdateDispatcher.shared.sendAndDispatch(.createTab(spaceId: spaceId, url: MahoUrl(urlToLoad), parentId: nil))
        close()
    }

    @objc private func closeWindow() {
        close()
    }

    @objc private func goBack() {
        webView.goBack()
    }

    @objc private func goForward() {
        webView.goForward()
    }

    override func performKeyEquivalent(with event: NSEvent) -> Bool {
        let flags = event.modifierFlags.intersection(.deviceIndependentFlagsMask)
        guard let chars = event.charactersIgnoringModifiers else {
            return super.performKeyEquivalent(with: event)
        }
        if chars == "o" && flags == .command {
            promoteToTab()
            return true
        }
        if chars == "o" && flags == [.command, .option] {
            promoteToSpaceChooser()
            return true
        }
        if chars == "w" && flags == .command {
            closeWindow()
            return true
        }
        return super.performKeyEquivalent(with: event)
    }

    private func promoteToSpaceChooser() {
        let spaces = MahoBridge.shared.getCanonicalSpaceViewModels()
        let activeSpaceId = MahoBridge.shared.getCanonicalActiveSpaceId()
        let menu = NSMenu()
        for space in spaces where space.id != activeSpaceId {
            let item = NSMenuItem(title: space.name, action: #selector(spaceChooserItemSelected(_:)), keyEquivalent: "")
            item.target = self
            item.representedObject = space.id
            menu.addItem(item)
        }
        guard !menu.items.isEmpty, let window = window else { return }
        let point = NSPoint(x: window.frame.midX, y: window.frame.midY)
        menu.popUp(positioning: nil, at: window.convertPoint(fromScreen: point), in: window.contentView)
    }

    @objc private func spaceChooserItemSelected(_ sender: NSMenuItem) {
        guard let spaceId = sender.representedObject as? SpaceId else { return }
        CoreUpdateDispatcher.shared.sendAndDispatch(.createTab(spaceId: spaceId, url: MahoUrl(urlToLoad), parentId: nil))
        close()
    }
}

extension LittleArcWindowController: WKNavigationDelegate {
    func webView(_ webView: WKWebView, didCommit navigation: WKNavigation!) {
        if let urlLabel = window?.contentView?.viewWithTag(100) as? NSTextField {
            urlLabel.stringValue = webView.url?.absoluteString ?? urlToLoad
        }
    }

    func webView(_ webView: WKWebView, didFinish navigation: WKNavigation!) {
        if let urlLabel = window?.contentView?.viewWithTag(100) as? NSTextField {
            urlLabel.stringValue = webView.url?.absoluteString ?? urlToLoad
        }
        window?.title = webView.title ?? "Little Arc"
    }
}
