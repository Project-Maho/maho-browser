import AppKit

final class FolderPreviewPopoverController: NSViewController {
    private let folderId: FolderId
    private let spaceId: SpaceId
    private let gridView = NSStackView()
    private var onTabSelected: ((TabId) -> Void)?

    private static let maxPreviewTabs = 8

    init(folderId: FolderId, spaceId: SpaceId, onTabSelected: ((TabId) -> Void)?) {
        self.folderId = folderId
        self.spaceId = spaceId
        self.onTabSelected = onTabSelected
        super.init(nibName: nil, bundle: nil)
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    override func loadView() {
        let container = NSView(frame: NSRect(x: 0, y: 0, width: 240, height: 10))
        container.translatesAutoresizingMaskIntoConstraints = false

        gridView.translatesAutoresizingMaskIntoConstraints = false
        gridView.orientation = .vertical
        gridView.spacing = 2
        gridView.edgeInsets = NSEdgeInsets(top: 8, left: 8, bottom: 8, right: 8)
        container.addSubview(gridView)

        NSLayoutConstraint.activate([
            gridView.topAnchor.constraint(equalTo: container.topAnchor),
            gridView.leadingAnchor.constraint(equalTo: container.leadingAnchor),
            gridView.trailingAnchor.constraint(equalTo: container.trailingAnchor),
            gridView.bottomAnchor.constraint(equalTo: container.bottomAnchor),
            container.widthAnchor.constraint(equalToConstant: 240),
        ])

        self.view = container
    }

    override func viewDidLoad() {
        super.viewDidLoad()
        loadFolderTabs()
    }

    private func loadFolderTabs() {
        let folders = MahoBridge.shared.getFolderViewModels(spaceId: spaceId)
        guard let folder = folders.first(where: { $0.id == folderId }) else { return }

        let allTabs = MahoBridge.shared.getTabViewModels()
        let folderTabs = folder.tabIds.compactMap { tabId in
            allTabs.first(where: { $0.id == tabId })
        }

        let displayTabs = Array(folderTabs.prefix(Self.maxPreviewTabs))
        let overflowCount = max(0, folderTabs.count - Self.maxPreviewTabs)

        for tab in displayTabs {
            let row = makeTabRow(tab)
            gridView.addArrangedSubview(row)
        }

        if overflowCount > 0 {
            let overflowLabel = NSTextField(labelWithString: "+\(overflowCount) more")
            overflowLabel.font = .systemFont(ofSize: 11, weight: .regular)
            overflowLabel.textColor = MahoAppColors.tertiaryText
            overflowLabel.alignment = .center
            overflowLabel.translatesAutoresizingMaskIntoConstraints = false
            gridView.addArrangedSubview(overflowLabel)
        }

        if displayTabs.isEmpty {
            let emptyLabel = NSTextField(labelWithString: "Empty folder")
            emptyLabel.font = .systemFont(ofSize: 11, weight: .regular)
            emptyLabel.textColor = MahoAppColors.tertiaryText
            emptyLabel.alignment = .center
            emptyLabel.translatesAutoresizingMaskIntoConstraints = false
            gridView.addArrangedSubview(emptyLabel)
        }
    }

    private func makeTabRow(_ tab: TabViewModel) -> NSView {
        let row = FolderPreviewTabRow(tabId: tab.id)
        row.translatesAutoresizingMaskIntoConstraints = false
        row.onClick = { [weak self] tabId in
            self?.onTabSelected?(tabId)
        }

        let iconView = NSImageView()
        iconView.translatesAutoresizingMaskIntoConstraints = false
        iconView.imageScaling = .scaleProportionallyUpOrDown

        if let faviconData = tab.favicon, let image = NSImage(data: Data(faviconData.data)) {
            iconView.image = image
        } else {
            let config = NSImage.SymbolConfiguration(pointSize: 10, weight: .medium)
            iconView.image = NSImage(systemSymbolName: "globe", accessibilityDescription: nil)?.withSymbolConfiguration(config)
            iconView.contentTintColor = MahoAppColors.secondaryText
        }

        let titleLabel = NSTextField(labelWithString: tab.title.isEmpty ? tab.url : tab.title)
        titleLabel.translatesAutoresizingMaskIntoConstraints = false
        titleLabel.font = .systemFont(ofSize: 11, weight: .regular)
        titleLabel.textColor = MahoAppColors.primaryText
        titleLabel.lineBreakMode = .byTruncatingTail
        titleLabel.maximumNumberOfLines = 1

        row.addSubview(iconView)
        row.addSubview(titleLabel)

        NSLayoutConstraint.activate([
            row.heightAnchor.constraint(equalToConstant: 24),
            iconView.leadingAnchor.constraint(equalTo: row.leadingAnchor, constant: 4),
            iconView.centerYAnchor.constraint(equalTo: row.centerYAnchor),
            iconView.widthAnchor.constraint(equalToConstant: 14),
            iconView.heightAnchor.constraint(equalToConstant: 14),
            titleLabel.leadingAnchor.constraint(equalTo: iconView.trailingAnchor, constant: 6),
            titleLabel.centerYAnchor.constraint(equalTo: row.centerYAnchor),
            titleLabel.trailingAnchor.constraint(lessThanOrEqualTo: row.trailingAnchor, constant: -4),
        ])

        return row
    }
}

private final class FolderPreviewTabRow: NSView {
    let tabId: TabId
    var onClick: ((TabId) -> Void)?
    private var trackingArea: NSTrackingArea?

    init(tabId: TabId) {
        self.tabId = tabId
        super.init(frame: .zero)
        wantsLayer = true
        layer?.cornerRadius = 4
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    override func updateTrackingAreas() {
        super.updateTrackingAreas()
        if let existing = trackingArea {
            removeTrackingArea(existing)
        }
        trackingArea = NSTrackingArea(
            rect: bounds,
            options: [.mouseEnteredAndExited, .activeInActiveApp],
            owner: self,
            userInfo: nil
        )
        addTrackingArea(trackingArea!)
    }

    override func mouseEntered(with event: NSEvent) {
        layer?.backgroundColor = MahoAppColors.sidebarHover.cgColor
    }

    override func mouseExited(with event: NSEvent) {
        layer?.backgroundColor = NSColor.clear.cgColor
    }

    override func mouseDown(with event: NSEvent) {
        onClick?(tabId)
    }
}
