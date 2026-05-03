import AppKit

final class DownloadToolbarButton: NSButton, CoreUpdateObserver {
    private var activeCount: Int = 0
    private var popover: NSPopover?
    private let badgeLabel = NSTextField(labelWithString: "")

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
        image = NSImage(systemSymbolName: "arrow.down.circle", accessibilityDescription: "Downloads")?
            .withSymbolConfiguration(config)
        contentTintColor = MahoAppColors.titlebarControlTint.withAlphaComponent(0.80)
        imageScaling = .scaleProportionallyDown
        toolTip = "Downloads"
        alphaValue = 0.90
        target = self
        action = #selector(togglePopover(_:))

        badgeLabel.font = NSFont.systemFont(ofSize: 8, weight: .bold)
        badgeLabel.textColor = .white
        badgeLabel.backgroundColor = MahoAppColors.accentBlue
        badgeLabel.drawsBackground = true
        badgeLabel.alignment = .center
        badgeLabel.isBezeled = false
        badgeLabel.isEditable = false
        badgeLabel.wantsLayer = true
        badgeLabel.layer?.cornerRadius = 6
        badgeLabel.layer?.masksToBounds = true
        badgeLabel.translatesAutoresizingMaskIntoConstraints = false
        badgeLabel.isHidden = true
        addSubview(badgeLabel)

        NSLayoutConstraint.activate([
            badgeLabel.widthAnchor.constraint(greaterThanOrEqualToConstant: 12),
            badgeLabel.heightAnchor.constraint(equalToConstant: 12),
            badgeLabel.trailingAnchor.constraint(equalTo: trailingAnchor, constant: 2),
            badgeLabel.topAnchor.constraint(equalTo: topAnchor, constant: -2),
        ])

        CoreUpdateDispatcher.shared.register(self)
        refreshBadge()
    }

    deinit {
        CoreUpdateDispatcher.shared.unregister(self)
    }

    private func refreshBadge() {
        let downloads = MahoBridge.shared.getDownloadViewModels()
        let count = downloads.filter { $0.state == .downloading || $0.state == .paused }.count
        DispatchQueue.main.async { [weak self] in
            guard let self = self else { return }
            self.activeCount = count
            if count > 0 {
                self.badgeLabel.stringValue = "\(count)"
                self.badgeLabel.isHidden = false
            } else {
                self.badgeLabel.isHidden = true
            }
        }
    }

    @objc private func togglePopover(_ sender: Any) {
        if let existing = popover, existing.isShown {
            existing.performClose(sender)
            popover = nil
            return
        }
        let vc = DownloadPopoverView()
        let pop = NSPopover()
        pop.contentViewController = vc
        pop.behavior = .transient
        pop.contentSize = NSSize(width: 340, height: 400)
        pop.show(relativeTo: bounds, of: self, preferredEdge: .minY)
        popover = pop
    }

    // MARK: - CoreUpdateObserver

    func handleCoreUpdate(_ update: CoreUpdate) {
        switch update {
        case .downloadStarted, .downloadProgress, .downloadCompleted:
            refreshBadge()
        default:
            break
        }
    }
}
