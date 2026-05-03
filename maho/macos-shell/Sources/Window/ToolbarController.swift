import AppKit

// MARK: - Toolbar Item Identifiers

private extension NSToolbarItem.Identifier {
    static let backButton = NSToolbarItem.Identifier("maho.toolbar.back")
    static let forwardButton = NSToolbarItem.Identifier("maho.toolbar.forward")
    static let reloadStop = NSToolbarItem.Identifier("maho.toolbar.reloadStop")
    static let urlField = NSToolbarItem.Identifier("maho.toolbar.urlField")
    static let extensionsArea = NSToolbarItem.Identifier("maho.toolbar.extensions")
}

private final class AddressBarToolbarButton: NSButton {
    var contextMenuProvider: (() -> NSMenu?)?

    override func menu(for event: NSEvent) -> NSMenu? {
        contextMenuProvider?()
    }
}

// MARK: - ToolbarController

final class ToolbarController: NSObject, NSToolbarDelegate {
    weak var window: NSWindow?

    private var extensionToolbarManager: ExtensionToolbarManager?
    private var unpinObserver: Any?
    private var pinObserver: Any?
    private var activeTabId: TabId?
    private var currentUrl: String = ""
    private var currentTitle: String = ""
    private var canGoBack: Bool = false
    private var canGoForward: Bool = false
    private var isLoading: Bool = false

    private var backButton: NSButton?
    private var forwardButton: NSButton?
    private var reloadStopButton: NSButton?
    private var urlField: AddressBarToolbarButton?

    func makeToolbar() -> NSToolbar {
        let toolbar = NSToolbar(identifier: "MahoMainToolbar")
        toolbar.delegate = self
        toolbar.displayMode = .iconOnly
        toolbar.allowsUserCustomization = false
        toolbar.showsBaselineSeparator = false
        return toolbar
    }

    func updateActiveTab(_ tabId: TabId?) {
        activeTabId = tabId
    }

    func refreshAppearance() {
        let tint = MahoAppColors.titlebarControlTint.withAlphaComponent(0.80)
        backButton?.contentTintColor = tint
        forwardButton?.contentTintColor = tint
        reloadStopButton?.contentTintColor = tint
        urlField?.contentTintColor = MahoAppColors.urlBarText
    }

    func updateNavigationState(
        url: String,
        title: String,
        canGoBack: Bool,
        canGoForward: Bool,
        isLoading: Bool
    ) {
        self.currentUrl = url
        self.currentTitle = title
        self.canGoBack = canGoBack
        self.canGoForward = canGoForward
        self.isLoading = isLoading

        DispatchQueue.main.async { [weak self] in
            self?.refreshControls()
        }
    }

    private func refreshControls() {
        backButton?.isEnabled = canGoBack
        forwardButton?.isEnabled = canGoForward

        if isLoading {
            reloadStopButton?.image = NSImage(systemSymbolName: "xmark", accessibilityDescription: "Stop")
            reloadStopButton?.toolTip = "Stop"
            reloadStopButton?.setAccessibilityLabel("Stop Loading")
        } else {
            reloadStopButton?.image = NSImage(systemSymbolName: "arrow.clockwise", accessibilityDescription: "Reload")
            reloadStopButton?.toolTip = "Reload"
            reloadStopButton?.setAccessibilityLabel("Reload Page")
        }

        if let field = urlField {
            field.title = UrlSubmissionHelper.displayDomain(from: currentUrl)
        }
        refreshAppearance()
    }

    // MARK: - Public

    func focusURLField() {
        openCommandBarForCurrentTab()
    }

    private func openCommandBarForCurrentTab() {
        (NSApp.delegate as? AppDelegate)?.focusURLBar()
    }

    // MARK: - Actions

    @objc private func backAction(_ sender: Any) {
        guard let tabId = activeTabId else { return }
        CoreUpdateDispatcher.shared.sendAndDispatch(.goBack(tabId: tabId))
    }

    @objc private func forwardAction(_ sender: Any) {
        guard let tabId = activeTabId else { return }
        CoreUpdateDispatcher.shared.sendAndDispatch(.goForward(tabId: tabId))
    }

    @objc private func reloadStopAction(_ sender: Any) {
        guard let tabId = activeTabId else { return }
        if isLoading {
            CoreUpdateDispatcher.shared.sendAndDispatch(.stop(tabId: tabId))
        } else {
            CoreUpdateDispatcher.shared.sendAndDispatch(.reload(tabId: tabId))
        }
    }

    // MARK: - NSToolbarDelegate

    func toolbar(
        _ toolbar: NSToolbar,
        itemForItemIdentifier itemIdentifier: NSToolbarItem.Identifier,
        willBeInsertedIntoToolbar flag: Bool
    ) -> NSToolbarItem? {
        switch itemIdentifier {
        case .backButton:
            return makeNavigationItem(identifier: .backButton, symbolName: "chevron.left", action: #selector(backAction(_:)), tooltip: "Back")
        case .forwardButton:
            return makeNavigationItem(identifier: .forwardButton, symbolName: "chevron.right", action: #selector(forwardAction(_:)), tooltip: "Forward")
        case .reloadStop:
            return makeNavigationItem(identifier: .reloadStop, symbolName: "arrow.clockwise", action: #selector(reloadStopAction(_:)), tooltip: "Reload")
        case .urlField:
            return makeUrlFieldItem()
        case .extensionsArea:
            let item = NSToolbarItem(itemIdentifier: .extensionsArea)
            let extensionsView = NSView(frame: NSRect(x: 0, y: 0, width: 120, height: 28))
            item.view = extensionsView
            extensionToolbarManager = ExtensionToolbarManager(containerView: extensionsView)
            unpinObserver = NotificationCenter.default.addObserver(
                forName: .unpinExtension, object: nil, queue: .main
            ) { [weak self] notification in
                if let extId = notification.userInfo?["extensionId"] as? String {
                    self?.extensionToolbarManager?.unpinExtension(id: extId)
                }
            }
            pinObserver = NotificationCenter.default.addObserver(
                forName: .pinExtension, object: nil, queue: .main
            ) { [weak self] notification in
                if let extId = notification.userInfo?["extensionId"] as? String {
                    self?.extensionToolbarManager?.pinExtension(id: extId)
                }
            }
            return item
        default:
            return nil
        }
    }

    func toolbarDefaultItemIdentifiers(_ toolbar: NSToolbar) -> [NSToolbarItem.Identifier] {
        return []
    }

    func toolbarAllowedItemIdentifiers(_ toolbar: NSToolbar) -> [NSToolbarItem.Identifier] {
        return []
    }

    // MARK: - Item Factory

    private func makeNavigationItem(
        identifier: NSToolbarItem.Identifier,
        symbolName: String,
        action: Selector,
        tooltip: String
    ) -> NSToolbarItem {
        let item = NSToolbarItem(itemIdentifier: identifier)
        let button = NSButton(frame: NSRect(x: 0, y: 0, width: 28, height: 28))
        button.bezelStyle = .texturedRounded
        button.isBordered = false
        let configuration = NSImage.SymbolConfiguration(pointSize: 10.5, weight: .medium)
        button.image = NSImage(systemSymbolName: symbolName, accessibilityDescription: tooltip)?.withSymbolConfiguration(configuration)
        button.contentTintColor = MahoAppColors.titlebarControlTint.withAlphaComponent(0.80)
        button.target = self
        button.action = action
        button.toolTip = tooltip
        button.imageScaling = .scaleProportionallyDown
        button.alphaValue = 0.90
        item.view = button

        button.setAccessibilityRole(.button)

        switch identifier {
        case .backButton:
            backButton = button
            button.isEnabled = canGoBack
            button.setAccessibilityLabel("Go Back")
        case .forwardButton:
            forwardButton = button
            button.isEnabled = canGoForward
            button.setAccessibilityLabel("Go Forward")
        case .reloadStop:
            reloadStopButton = button
            button.setAccessibilityLabel(isLoading ? "Stop Loading" : "Reload Page")
        default:
            break
        }

        return item
    }

    private func makeUrlFieldItem() -> NSToolbarItem {
        let item = NSToolbarItem(itemIdentifier: .urlField)
        item.minSize = NSSize(width: 200, height: 28)
        item.maxSize = NSSize(width: 10000, height: 28)

        let button = AddressBarToolbarButton(frame: NSRect(x: 0, y: 0, width: 400, height: 28))
        button.title = "Search or enter URL"
        button.bezelStyle = .texturedRounded
        button.font = NSFont.systemFont(ofSize: 13)
        button.alignment = .center
        button.isBordered = true
        button.contentTintColor = MahoAppColors.urlBarText
        button.target = self
        button.action = #selector(urlFieldClicked(_:))
        button.focusRingType = .none
        button.setAccessibilityLabel("Address and Search Bar")
        button.setAccessibilityRole(.button)
        button.contextMenuProvider = { [weak self] in
            guard let self else { return nil }
            return AddressBarContextMenu.menu(for: self.currentUrl, onEditAddress: { [weak self] in
                self?.openCommandBarForCurrentTab()
            })
        }

        urlField = button
        item.view = button
        refreshAppearance()

        return item
    }

    @objc private func urlFieldClicked(_ sender: Any) {
        openCommandBarForCurrentTab()
    }
}
