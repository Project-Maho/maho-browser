import AppKit

// MARK: - Library Category

enum LibraryCategory: String, CaseIterable {
    case media = "Media"
    case downloads = "Downloads"
    case easels = "Easels"
    case spaces = "Spaces"
    case boosts = "Boosts"
    case archivedTabs = "Archived Tabs"

    var iconName: String {
        switch self {
        case .media: return "photo.on.rectangle"
        case .downloads: return "arrow.down.circle"
        case .easels: return "paintbrush.pointed"
        case .spaces: return "square.grid.2x2"
        case .boosts: return "bolt"
        case .archivedTabs: return "archivebox"
        }
    }
}

// MARK: - Rail Item View

final class LibraryRailItemView: NSView {
    private let backgroundView = NSView()
    private let iconView = NSImageView()
    private let titleLabel = NSTextField(labelWithString: "")
    private var trackingArea: NSTrackingArea?

    let category: LibraryCategory
    var isSelected: Bool = false { didSet { refreshAppearance() } }
    private var isHovered: Bool = false { didSet { refreshAppearance() } }
    var onSelect: ((LibraryCategory) -> Void)?

    init(category: LibraryCategory) {
        self.category = category
        super.init(frame: .zero)
        setupViews()
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError() }

    private func setupViews() {
        wantsLayer = true

        backgroundView.translatesAutoresizingMaskIntoConstraints = false
        backgroundView.wantsLayer = true
        backgroundView.layer?.cornerRadius = 7
        addSubview(backgroundView)

        iconView.translatesAutoresizingMaskIntoConstraints = false
        iconView.imageScaling = .scaleProportionallyDown
        iconView.symbolConfiguration = NSImage.SymbolConfiguration(pointSize: 16, weight: .medium)
        if let img = NSImage(systemSymbolName: category.iconName, accessibilityDescription: category.rawValue) {
            iconView.image = img
        }
        addSubview(iconView)

        titleLabel.translatesAutoresizingMaskIntoConstraints = false
        titleLabel.stringValue = category.rawValue
        titleLabel.font = .systemFont(ofSize: 9, weight: .medium)
        titleLabel.alignment = .center
        titleLabel.lineBreakMode = .byTruncatingTail
        titleLabel.maximumNumberOfLines = 1
        titleLabel.isBezeled = false
        titleLabel.drawsBackground = false
        titleLabel.isEditable = false
        titleLabel.isSelectable = false
        addSubview(titleLabel)

        NSLayoutConstraint.activate([
            backgroundView.centerXAnchor.constraint(equalTo: centerXAnchor),
            backgroundView.topAnchor.constraint(equalTo: topAnchor, constant: 2),
            backgroundView.widthAnchor.constraint(equalToConstant: 48),
            backgroundView.heightAnchor.constraint(equalToConstant: 30),

            iconView.centerXAnchor.constraint(equalTo: centerXAnchor),
            iconView.centerYAnchor.constraint(equalTo: backgroundView.centerYAnchor),
            iconView.widthAnchor.constraint(equalToConstant: 18),
            iconView.heightAnchor.constraint(equalToConstant: 18),

            titleLabel.topAnchor.constraint(equalTo: backgroundView.bottomAnchor, constant: 1),
            titleLabel.centerXAnchor.constraint(equalTo: centerXAnchor),
            titleLabel.leadingAnchor.constraint(greaterThanOrEqualTo: leadingAnchor, constant: 2),
            titleLabel.trailingAnchor.constraint(lessThanOrEqualTo: trailingAnchor, constant: -2),
            titleLabel.bottomAnchor.constraint(equalTo: bottomAnchor, constant: -2),
        ])

        refreshAppearance()
    }

    func refreshAppearance() {
        if isSelected {
            backgroundView.layer?.backgroundColor = MahoAppColors.libraryRailItemSelected.cgColor
        } else if isHovered {
            backgroundView.layer?.backgroundColor = MahoAppColors.libraryRailItemHover.cgColor
        } else {
            backgroundView.layer?.backgroundColor = NSColor.clear.cgColor
        }

        let textColor = isSelected
            ? MahoAppColors.libraryRailItemSelectedText
            : MahoAppColors.libraryRailItemText
        titleLabel.textColor = textColor
        iconView.contentTintColor = textColor
    }

    override func updateTrackingAreas() {
        super.updateTrackingAreas()
        if let trackingArea { removeTrackingArea(trackingArea) }
        let area = NSTrackingArea(
            rect: bounds,
            options: [.mouseEnteredAndExited, .activeInActiveApp],
            owner: self
        )
        addTrackingArea(area)
        trackingArea = area
    }

    override func mouseEntered(with event: NSEvent) { isHovered = true }
    override func mouseExited(with event: NSEvent) { isHovered = false }

    override func mouseDown(with event: NSEvent) {
        onSelect?(category)
    }

    override func viewDidChangeEffectiveAppearance() {
        super.viewDidChangeEffectiveAppearance()
        refreshAppearance()
    }
}

// MARK: - Library Rail View

final class LibraryRailView: NSView {
    private let stackView = NSStackView()
    private let backButton = NSButton()
    private var itemViews: [LibraryRailItemView] = []

    var selectedCategory: LibraryCategory = .archivedTabs {
        didSet { updateSelection() }
    }
    var onCategorySelected: ((LibraryCategory) -> Void)?
    var onBackPressed: (() -> Void)?

    override init(frame: NSRect) {
        super.init(frame: frame)
        setupViews()
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError() }

    private func setupViews() {
        wantsLayer = true
        layer?.backgroundColor = MahoAppColors.libraryRailBackground.cgColor

        stackView.translatesAutoresizingMaskIntoConstraints = false
        stackView.orientation = .vertical
        stackView.alignment = .centerX
        stackView.spacing = 2
        stackView.distribution = .fill
        addSubview(stackView)

        for category in LibraryCategory.allCases {
            let itemView = LibraryRailItemView(category: category)
            itemView.translatesAutoresizingMaskIntoConstraints = false
            itemView.onSelect = { [weak self] cat in
                self?.selectedCategory = cat
                self?.onCategorySelected?(cat)
            }
            stackView.addArrangedSubview(itemView)
            itemViews.append(itemView)

            NSLayoutConstraint.activate([
                itemView.widthAnchor.constraint(equalTo: stackView.widthAnchor),
                itemView.heightAnchor.constraint(equalToConstant: 50),
            ])
        }

        backButton.translatesAutoresizingMaskIntoConstraints = false
        backButton.bezelStyle = .accessoryBarAction
        backButton.isBordered = false
        backButton.image = NSImage(
            systemSymbolName: "chevron.left",
            accessibilityDescription: "Back"
        )
        backButton.symbolConfiguration = NSImage.SymbolConfiguration(pointSize: 13, weight: .medium)
        backButton.contentTintColor = MahoAppColors.libraryRailItemText
        backButton.target = self
        backButton.action = #selector(handleBack)
        addSubview(backButton)

        NSLayoutConstraint.activate([
            stackView.topAnchor.constraint(equalTo: topAnchor, constant: 12),
            stackView.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 3),
            stackView.trailingAnchor.constraint(equalTo: trailingAnchor, constant: -3),

            backButton.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 8),
            backButton.bottomAnchor.constraint(equalTo: bottomAnchor, constant: -10),
            backButton.widthAnchor.constraint(equalToConstant: 24),
            backButton.heightAnchor.constraint(equalToConstant: 24),
        ])

        updateSelection()
    }

    private func updateSelection() {
        for item in itemViews {
            item.isSelected = (item.category == selectedCategory)
        }
    }

    @objc private func handleBack() {
        onBackPressed?()
    }

    func refreshAppearance() {
        layer?.backgroundColor = MahoAppColors.libraryRailBackground.cgColor
        backButton.contentTintColor = MahoAppColors.libraryRailItemText
        for item in itemViews {
            item.refreshAppearance()
        }
    }

    override func viewDidChangeEffectiveAppearance() {
        super.viewDidChangeEffectiveAppearance()
        refreshAppearance()
    }
}
