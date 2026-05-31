import AppKit

final class SuggestionDropdownView: NSView, NSTableViewDataSource, NSTableViewDelegate {

    private static let shadowColor = NSColor.black.withAlphaComponent(0.25)

    private let scrollView = NSScrollView()
    private let tableView = NSTableView()
    private var suggestions: [SuggestionViewModel] = []
    private var selectedIndex: Int = -1

    private static let rowHeight: CGFloat = 32
    private static let maxVisibleRows = 6
    private static let cellId = NSUserInterfaceItemIdentifier("SuggestionDropdownCell")

    private static let allowedKinds: Set<SuggestionType> = [
        .navigation, .history, .bookmark, .tab, .archivedTab, .closedTab,
        .calculator, .unitConversion, .action
    ]

    var onSuggestionSelected: ((SuggestionViewModel) -> Void)?

    var selectedSuggestion: SuggestionViewModel? {
        guard selectedIndex >= 0, selectedIndex < suggestions.count else { return nil }
        return suggestions[selectedIndex]
    }

    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        setupViews()
    }

    required init?(coder: NSCoder) {
        super.init(coder: coder)
        setupViews()
    }

    private func setupViews() {
        wantsLayer = true
        layer?.cornerRadius = 8
        layer?.borderWidth = 1

        let shadow = NSShadow()
        shadow.shadowBlurRadius = 8
        shadow.shadowOffset = NSSize(width: 0, height: -2)
        self.shadow = shadow

        let column = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("main"))
        column.resizingMask = .autoresizingMask
        tableView.addTableColumn(column)
        tableView.headerView = nil
        tableView.rowHeight = Self.rowHeight
        tableView.backgroundColor = .clear
        tableView.intercellSpacing = NSSize(width: 0, height: 0)
        tableView.selectionHighlightStyle = .none
        tableView.dataSource = self
        tableView.delegate = self
        tableView.target = self
        tableView.action = #selector(rowClicked)

        scrollView.translatesAutoresizingMaskIntoConstraints = false
        scrollView.documentView = tableView
        scrollView.hasVerticalScroller = true
        scrollView.hasHorizontalScroller = false
        scrollView.autohidesScrollers = true
        scrollView.drawsBackground = false
        scrollView.borderType = .noBorder
        addSubview(scrollView)

        NSLayoutConstraint.activate([
            scrollView.topAnchor.constraint(equalTo: topAnchor, constant: 4),
            scrollView.leadingAnchor.constraint(equalTo: leadingAnchor),
            scrollView.trailingAnchor.constraint(equalTo: trailingAnchor),
            scrollView.bottomAnchor.constraint(equalTo: bottomAnchor, constant: -4),
        ])

        refreshAppearance()
        isHidden = true
    }

    override func viewDidChangeEffectiveAppearance() {
        super.viewDidChangeEffectiveAppearance()
        refreshAppearance()
    }

    func refreshAppearance() {
        let appearance = window?.effectiveAppearance ?? effectiveAppearance
        appearance.performAsCurrentDrawingAppearance {
            layer?.backgroundColor = MahoAppColors.sidebarBackground.cgColor
            layer?.borderColor = MahoAppColors.cardBorder.cgColor
            shadow?.shadowColor = Self.shadowColor
        }

        for row in 0..<tableView.numberOfRows {
            if let cell = tableView.view(atColumn: 0, row: row, makeIfNecessary: false) as? SuggestionDropdownCellView {
                cell.refreshAppearance()
            }
        }
    }

    // MARK: - Public API

    func updateSuggestions(_ newSuggestions: [SuggestionViewModel]) {
        suggestions = newSuggestions.filter { Self.allowedKinds.contains($0.kind) }
        selectedIndex = suggestions.isEmpty ? -1 : 0
        tableView.reloadData()
    }

    func show(relativeTo view: NSView) {
        guard !suggestions.isEmpty, let superview = view.superview else { return }

        let viewFrame = view.convert(view.bounds, to: superview)
        let visibleRows = min(suggestions.count, Self.maxVisibleRows)
        let dropdownHeight = CGFloat(visibleRows) * Self.rowHeight + 8

        self.frame = NSRect(
            x: viewFrame.origin.x,
            y: viewFrame.origin.y - dropdownHeight - 4,
            width: viewFrame.width,
            height: dropdownHeight
        )

        if self.superview !== superview {
            self.superview?.subviews.forEach { _ in }
            removeFromSuperview()
            superview.addSubview(self, positioned: .above, relativeTo: view)
        }

        tableView.sizeLastColumnToFit()
        isHidden = false
    }

    func dismiss() {
        isHidden = true
        suggestions = []
        selectedIndex = -1
        tableView.reloadData()
    }

    func moveSelectionDown() {
        guard !suggestions.isEmpty else { return }
        selectedIndex = min(selectedIndex + 1, suggestions.count - 1)
        tableView.reloadData()
        tableView.scrollRowToVisible(selectedIndex)
    }

    func moveSelectionUp() {
        guard !suggestions.isEmpty else { return }
        selectedIndex = max(selectedIndex - 1, 0)
        tableView.reloadData()
        tableView.scrollRowToVisible(selectedIndex)
    }

    // MARK: - NSTableViewDataSource

    func numberOfRows(in tableView: NSTableView) -> Int {
        suggestions.count
    }

    // MARK: - NSTableViewDelegate

    func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?, row: Int) -> NSView? {
        guard row < suggestions.count else { return nil }

        let cell: SuggestionDropdownCellView
        if let reused = tableView.makeView(withIdentifier: Self.cellId, owner: nil) as? SuggestionDropdownCellView {
            cell = reused
        } else {
            cell = SuggestionDropdownCellView()
            cell.identifier = Self.cellId
        }

        cell.configure(with: suggestions[row], isSelected: row == selectedIndex)
        return cell
    }

    func tableView(_ tableView: NSTableView, heightOfRow row: Int) -> CGFloat {
        Self.rowHeight
    }

    @objc private func rowClicked() {
        let row = tableView.clickedRow
        guard row >= 0, row < suggestions.count else { return }
        onSuggestionSelected?(suggestions[row])
    }
}

// MARK: - Cell View

private final class SuggestionDropdownCellView: NSTableCellView {

    private let iconView = NSImageView()
    private var currentSuggestion: SuggestionViewModel?
    private var isCurrentlySelected = false
    private let titleLabel = NSTextField(labelWithString: "")
    private let subtitleLabel = NSTextField(labelWithString: "")
    private let selectionBackground = NSView()

    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        setup()
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    private func setup() {
        wantsLayer = true

        selectionBackground.translatesAutoresizingMaskIntoConstraints = false
        selectionBackground.wantsLayer = true
        selectionBackground.layer?.cornerRadius = 6
        addSubview(selectionBackground)

        iconView.translatesAutoresizingMaskIntoConstraints = false
        iconView.imageScaling = .scaleProportionallyDown
        addSubview(iconView)

        titleLabel.translatesAutoresizingMaskIntoConstraints = false
        titleLabel.font = .systemFont(ofSize: 12, weight: .medium)
        titleLabel.lineBreakMode = .byTruncatingTail
        addSubview(titleLabel)

        subtitleLabel.translatesAutoresizingMaskIntoConstraints = false
        subtitleLabel.font = .systemFont(ofSize: 10, weight: .regular)
        subtitleLabel.lineBreakMode = .byTruncatingTail
        addSubview(subtitleLabel)

        NSLayoutConstraint.activate([
            selectionBackground.topAnchor.constraint(equalTo: topAnchor, constant: 1),
            selectionBackground.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 4),
            selectionBackground.trailingAnchor.constraint(equalTo: trailingAnchor, constant: -4),
            selectionBackground.bottomAnchor.constraint(equalTo: bottomAnchor, constant: -1),

            iconView.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 10),
            iconView.centerYAnchor.constraint(equalTo: centerYAnchor),
            iconView.widthAnchor.constraint(equalToConstant: 16),
            iconView.heightAnchor.constraint(equalToConstant: 16),

            titleLabel.leadingAnchor.constraint(equalTo: iconView.trailingAnchor, constant: 8),
            titleLabel.centerYAnchor.constraint(equalTo: centerYAnchor),

            subtitleLabel.leadingAnchor.constraint(equalTo: titleLabel.trailingAnchor, constant: 6),
            subtitleLabel.centerYAnchor.constraint(equalTo: centerYAnchor),
            subtitleLabel.trailingAnchor.constraint(lessThanOrEqualTo: trailingAnchor, constant: -10),
        ])

        let titleTrailing = titleLabel.trailingAnchor.constraint(lessThanOrEqualTo: trailingAnchor, constant: -10)
        titleTrailing.priority = .defaultLow
        titleTrailing.isActive = true
    }

    override func viewDidChangeEffectiveAppearance() {
        super.viewDidChangeEffectiveAppearance()
        refreshAppearance()
    }

    func refreshAppearance() {
        guard let currentSuggestion else { return }
        configure(with: currentSuggestion, isSelected: isCurrentlySelected)
    }

    func configure(with suggestion: SuggestionViewModel, isSelected: Bool) {
        currentSuggestion = suggestion
        isCurrentlySelected = isSelected
        let titleColor = isSelected ? NSColor.white : MahoAppColors.primaryText
        let secondaryColor = isSelected ? NSColor.white.withAlphaComponent(0.65) : MahoAppColors.tertiaryText

        selectionBackground.layer?.backgroundColor = isSelected
            ? MahoAppColors.commandBarSelectionFill.cgColor
            : NSColor.clear.cgColor

        if let icon = suggestion.icon {
            let rawData = Data(icon.data.map { UInt8($0) })
            if let image = NSImage(data: rawData) {
                image.size = NSSize(width: 16, height: 16)
                iconView.image = image
                iconView.contentTintColor = nil
            } else {
                iconView.image = placeholderIcon(for: suggestion.kind)
                iconView.contentTintColor = isSelected ? .white : MahoAppColors.secondaryText
            }
        } else {
            iconView.image = placeholderIcon(for: suggestion.kind)
            iconView.contentTintColor = isSelected ? .white : MahoAppColors.secondaryText
        }

        let attributed = NSMutableAttributedString(string: suggestion.title)
        attributed.addAttributes([
            .font: NSFont.systemFont(ofSize: 12, weight: .medium),
            .foregroundColor: titleColor,
        ], range: NSRange(location: 0, length: attributed.length))

        if let ranges = suggestion.matchRanges {
            let boldFont = NSFont.systemFont(ofSize: 12, weight: .bold)
            for range in ranges {
                guard range.count == 2 else { continue }
                let start = max(0, min(range[0], suggestion.title.utf16.count))
                let end = max(start, min(range[1], suggestion.title.utf16.count))
                if start < end {
                    attributed.addAttribute(.font, value: boldFont, range: NSRange(location: start, length: end - start))
                }
            }
        }
        titleLabel.attributedStringValue = attributed

        subtitleLabel.stringValue = suggestion.subtitle ?? ""
        subtitleLabel.textColor = secondaryColor
        subtitleLabel.isHidden = (suggestion.subtitle ?? "").isEmpty
    }

    private func placeholderIcon(for kind: SuggestionType) -> NSImage? {
        let name: String
        switch kind {
        case .tab: name = "doc.text"
        case .bookmark: name = "bookmark"
        case .history: name = "clock.arrow.circlepath"
        case .action: name = "bolt"
        case .navigation: name = "safari"
        case .calculator: name = "function"
        case .unitConversion: name = "arrow.left.arrow.right"
        case .archivedTab: name = "archivebox"
        case .closedTab: name = "xmark.square"
        }
        return MahoIcon.imageForSFSymbol(name)
    }
}
