import AppKit

final class SplitToolbarButton: NSButton {

    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        commonInit()
    }

    required init?(coder: NSCoder) {
        super.init(coder: coder)
        commonInit()
    }

    private func commonInit() {
        bezelStyle = .texturedRounded
        isBordered = false
        let config = NSImage.SymbolConfiguration(pointSize: 10.5, weight: .medium)
        image = NSImage(systemSymbolName: "rectangle.split.2x1", accessibilityDescription: "Split View")?
            .withSymbolConfiguration(config)
        contentTintColor = MahoAppColors.titlebarControlTint.withAlphaComponent(0.80)
        imageScaling = .scaleProportionallyDown
        toolTip = "Toggle Split View"
        alphaValue = 0.90
        target = self
        action = #selector(toggleSplit(_:))
    }

    @objc private func toggleSplit(_ sender: Any) {
        let currentConfig = MahoBridge.shared.getSplitViewConfig()
        if let config = currentConfig, config.panes.count >= 2 {
            // Already split — remove the last pane
            let lastPaneId = PaneId(config.panes.last?.tabId ?? "")
            CoreUpdateDispatcher.shared.sendAndDispatch(.removeSplit(paneId: lastPaneId))
        } else {
            // Not split — create a split with the active tab duplicated
            if let appDelegate = NSApp.delegate as? AppDelegate,
               let mainWC = appDelegate.mainWindowControllerRef,
               let tabId = mainWC.activeTabId {
                CoreUpdateDispatcher.shared.sendAndDispatch(
                    .createSplit(tabIds: [tabId, tabId], orientation: .horizontal)
                )
            }
        }
    }
}
