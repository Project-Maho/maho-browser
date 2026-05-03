import AppKit
import WebKit

final class SplitViewController: NSSplitViewController, CoreUpdateObserver {
    private var paneTabIds: [TabId] = []
    private var paneOrientation: Orientation = .horizontal
    private let webViewManager = WebViewManager()

    private let toolbarContainer = NSView()
    private let orientationButton = NSButton()
    private let paneCountLabel = NSTextField(labelWithString: "")
    private let splitContentContainer = NSView()

    override func viewDidLoad() {
        super.viewDidLoad()
        splitView.dividerStyle = .thick
        splitView.arrangesAllSubviews = true
        CoreUpdateDispatcher.shared.register(self)
        setupToolbar()
    }

    deinit {
        CoreUpdateDispatcher.shared.unregister(self)
    }

    private func setupToolbar() {
        toolbarContainer.translatesAutoresizingMaskIntoConstraints = false
        toolbarContainer.wantsLayer = true
        toolbarContainer.layer?.backgroundColor = MahoAppColors.cardBackground.cgColor
        toolbarContainer.layer?.borderWidth = 1
        toolbarContainer.layer?.borderColor = MahoAppColors.cardBorder.cgColor

        orientationButton.title = "Horizontal"
        orientationButton.bezelStyle = .rounded
        orientationButton.target = self
        orientationButton.action = #selector(toggleOrientation(_:))
        orientationButton.translatesAutoresizingMaskIntoConstraints = false

        paneCountLabel.textColor = MahoAppColors.secondaryText
        paneCountLabel.translatesAutoresizingMaskIntoConstraints = false
        updatePaneCountLabel()

        toolbarContainer.addSubview(orientationButton)
        toolbarContainer.addSubview(paneCountLabel)

        NSLayoutConstraint.activate([
            toolbarContainer.heightAnchor.constraint(equalToConstant: 36),
            orientationButton.leadingAnchor.constraint(equalTo: toolbarContainer.leadingAnchor, constant: 8),
            orientationButton.centerYAnchor.constraint(equalTo: toolbarContainer.centerYAnchor),
            paneCountLabel.trailingAnchor.constraint(equalTo: toolbarContainer.trailingAnchor, constant: -8),
            paneCountLabel.centerYAnchor.constraint(equalTo: toolbarContainer.centerYAnchor),
        ])

        let wrapper = view
        wrapper.addSubview(toolbarContainer)

        NSLayoutConstraint.activate([
            toolbarContainer.topAnchor.constraint(equalTo: wrapper.topAnchor),
            toolbarContainer.leadingAnchor.constraint(equalTo: wrapper.leadingAnchor),
            toolbarContainer.trailingAnchor.constraint(equalTo: wrapper.trailingAnchor),
        ])

        splitView.translatesAutoresizingMaskIntoConstraints = false
        NSLayoutConstraint.activate([
            splitView.topAnchor.constraint(equalTo: toolbarContainer.bottomAnchor),
        ])
    }

    private func updatePaneCountLabel() {
        paneCountLabel.stringValue = "\(paneTabIds.count) pane\(paneTabIds.count == 1 ? "" : "s")"
    }

    private func updateOrientationButton() {
        orientationButton.title = paneOrientation == .horizontal ? "Horizontal" : "Vertical"
    }

    @objc private func toggleOrientation(_ sender: NSButton) {
        let newOrientation: Orientation = paneOrientation == .horizontal ? .vertical : .horizontal
        configureSplit(tabIds: paneTabIds, orientation: newOrientation)
    }

    func configureSplit(tabIds: [TabId], orientation: Orientation) {
        self.paneTabIds = tabIds
        self.paneOrientation = orientation

        for item in splitViewItems.reversed() {
            removeSplitViewItem(item)
        }

        splitView.isVertical = orientation == .horizontal

        for (index, tabId) in tabIds.enumerated() {
            let container = NSView()
            container.translatesAutoresizingMaskIntoConstraints = false

            let webView = webViewManager.createWebView(for: tabId, url: "")

            webView.translatesAutoresizingMaskIntoConstraints = false
            container.addSubview(webView)

            NSLayoutConstraint.activate([
                webView.topAnchor.constraint(equalTo: container.topAnchor),
                webView.bottomAnchor.constraint(equalTo: container.bottomAnchor),
                webView.leadingAnchor.constraint(equalTo: container.leadingAnchor),
                webView.trailingAnchor.constraint(equalTo: container.trailingAnchor),
            ])

            let viewController = NSViewController()
            viewController.view = container

            let item = NSSplitViewItem(viewController: viewController)
            item.minimumThickness = 200
            if index == 0 {
                item.holdingPriority = .defaultLow
            }
            addSplitViewItem(item)
        }

        updatePaneCountLabel()
        updateOrientationButton()
    }

    func handleCoreUpdate(_ update: CoreUpdate) {
        switch update {
        case .fullState:
            if let config = MahoBridge.shared.getSplitViewConfig() {
                configureSplit(tabIds: config.panes.map { $0.tabId }, orientation: config.orientation)
            }
        default:
            break
        }
    }
}
