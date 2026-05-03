import AppKit

final class ExtensionToolbarManager: CoreUpdateObserver {
    private weak var containerView: NSView?
    private var extensionButtons: [String: NSButton] = [:]
    private var pinnedExtensionIds: [String] = []

    init(containerView: NSView) {
        self.containerView = containerView
        loadPinnedExtensions()
        CoreUpdateDispatcher.shared.register(self)
        rebuildButtons()
    }

    deinit { CoreUpdateDispatcher.shared.unregister(self) }

    func handleCoreUpdate(_ update: CoreUpdate) {
        DispatchQueue.main.async { [weak self] in
            switch update {
            case .extensionToggled, .extensionRemoved, .fullState:
                self?.rebuildButtons()
            default:
                break
            }
        }
    }

    func pinExtension(id: String) {
        guard !pinnedExtensionIds.contains(id) else { return }
        pinnedExtensionIds.append(id)
        savePinnedExtensions()
        rebuildButtons()
    }

    func unpinExtension(id: String) {
        pinnedExtensionIds.removeAll { $0 == id }
        savePinnedExtensions()
        rebuildButtons()
    }

    func isPinned(id: String) -> Bool {
        return pinnedExtensionIds.contains(id)
    }

    private func rebuildButtons() {
        guard let container = containerView else { return }
        container.subviews.forEach { $0.removeFromSuperview() }
        extensionButtons.removeAll()

        let stack = NSStackView()
        stack.orientation = .horizontal
        stack.spacing = 4
        stack.translatesAutoresizingMaskIntoConstraints = false
        container.addSubview(stack)
        NSLayoutConstraint.activate([
            stack.leadingAnchor.constraint(equalTo: container.leadingAnchor),
            stack.trailingAnchor.constraint(lessThanOrEqualTo: container.trailingAnchor),
            stack.centerYAnchor.constraint(equalTo: container.centerYAnchor),
        ])

        for extId in pinnedExtensionIds {
            let button = NSButton(frame: NSRect(x: 0, y: 0, width: 24, height: 24))
            button.bezelStyle = .recessed
            button.isBordered = false
            button.image = NSImage(systemSymbolName: "puzzlepiece.extension", accessibilityDescription: extId)
            button.imagePosition = .imageOnly
            button.toolTip = extId
            button.target = self
            button.action = #selector(extensionButtonClicked(_:))
            button.setAccessibilityLabel("Extension: \(extId)")
            extensionButtons[extId] = button
            stack.addArrangedSubview(button)
        }
    }

    @objc private func extensionButtonClicked(_ sender: NSButton) {
        guard let extId = sender.toolTip else { return }
        let popover = ExtensionPopoverController(extensionId: extId)
        popover.show(relativeTo: sender.bounds, of: sender, preferredEdge: .minY)
    }

    private func loadPinnedExtensions() {
        pinnedExtensionIds = UserDefaults.standard.stringArray(forKey: "maho_pinned_extensions") ?? []
    }

    private func savePinnedExtensions() {
        UserDefaults.standard.set(pinnedExtensionIds, forKey: "maho_pinned_extensions")
    }
}
