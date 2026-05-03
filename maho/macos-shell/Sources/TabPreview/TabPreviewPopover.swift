import AppKit

final class TabPreviewViewController: NSViewController {
    private let imageView = NSImageView()
    private let titleLabel = NSTextField(labelWithString: "")
    private let urlLabel = NSTextField(labelWithString: "")

    private(set) var tabId: TabId?

    override func loadView() {
        let container = NSView(frame: NSRect(x: 0, y: 0, width: 300, height: 230))
        container.wantsLayer = true

        imageView.translatesAutoresizingMaskIntoConstraints = false
        imageView.imageScaling = .scaleProportionallyUpOrDown
        imageView.wantsLayer = true
        imageView.layer?.cornerRadius = 4
        imageView.layer?.masksToBounds = true
        imageView.layer?.backgroundColor = NSColor.black.withAlphaComponent(0.05).cgColor
        container.addSubview(imageView)

        titleLabel.translatesAutoresizingMaskIntoConstraints = false
        titleLabel.font = .systemFont(ofSize: 12, weight: .medium)
        titleLabel.textColor = MahoAppColors.primaryText
        titleLabel.lineBreakMode = .byTruncatingTail
        titleLabel.maximumNumberOfLines = 1
        container.addSubview(titleLabel)

        urlLabel.translatesAutoresizingMaskIntoConstraints = false
        urlLabel.font = .systemFont(ofSize: 10, weight: .regular)
        urlLabel.textColor = MahoAppColors.secondaryText.withAlphaComponent(0.7)
        urlLabel.lineBreakMode = .byTruncatingTail
        urlLabel.maximumNumberOfLines = 1
        container.addSubview(urlLabel)

        NSLayoutConstraint.activate([
            imageView.topAnchor.constraint(equalTo: container.topAnchor, constant: 8),
            imageView.leadingAnchor.constraint(equalTo: container.leadingAnchor, constant: 8),
            imageView.trailingAnchor.constraint(equalTo: container.trailingAnchor, constant: -8),
            imageView.heightAnchor.constraint(equalToConstant: 188),

            titleLabel.topAnchor.constraint(equalTo: imageView.bottomAnchor, constant: 6),
            titleLabel.leadingAnchor.constraint(equalTo: container.leadingAnchor, constant: 10),
            titleLabel.trailingAnchor.constraint(equalTo: container.trailingAnchor, constant: -10),

            urlLabel.topAnchor.constraint(equalTo: titleLabel.bottomAnchor, constant: 2),
            urlLabel.leadingAnchor.constraint(equalTo: container.leadingAnchor, constant: 10),
            urlLabel.trailingAnchor.constraint(equalTo: container.trailingAnchor, constant: -10),
            urlLabel.bottomAnchor.constraint(lessThanOrEqualTo: container.bottomAnchor, constant: -8),
        ])

        self.view = container
    }

    func configure(tabId: TabId, title: String, url: String, image: NSImage?) {
        self.tabId = tabId
        titleLabel.stringValue = title.isEmpty ? url : title
        urlLabel.stringValue = url
        imageView.image = image
    }
}

final class TabPreviewPopover {
    private var popover: NSPopover?
    private var hoverTimer: Timer?
    private var currentTabId: TabId?

    func scheduleShow(for tabId: TabId, title: String, url: String, relativeTo rect: NSRect, of view: NSView) {
        cancelPending()
        currentTabId = tabId

        hoverTimer = Timer.scheduledTimer(withTimeInterval: 0.3, repeats: false) { [weak self] _ in
            self?.showPopover(tabId: tabId, title: title, url: url, relativeTo: rect, of: view)
        }
    }

    func dismiss() {
        cancelPending()
        popover?.close()
        popover = nil
        currentTabId = nil
    }

    var isShown: Bool {
        return popover?.isShown ?? false
    }

    private func cancelPending() {
        hoverTimer?.invalidate()
        hoverTimer = nil
    }

    private func showPopover(tabId: TabId, title: String, url: String, relativeTo rect: NSRect, of view: NSView) {
        let image = TabPreviewManager.shared.getCachedPreview(for: tabId)

        let vc = TabPreviewViewController()
        _ = vc.view // force load
        vc.configure(tabId: tabId, title: title, url: url, image: image)

        let pop = NSPopover()
        pop.contentViewController = vc
        pop.contentSize = NSSize(width: 300, height: 230)
        pop.behavior = .semitransient
        pop.animates = true
        pop.show(relativeTo: rect, of: view, preferredEdge: .maxX)

        popover = pop
    }
}
