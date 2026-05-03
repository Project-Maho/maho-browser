import AppKit

protocol TabMentionAutocompleteDelegate: AnyObject {
    func tabMentionDidSelect(title: String)
    func tabMentionDidDismiss()
}

final class TabMentionAutocompleteView: NSView {
    weak var delegate: TabMentionAutocompleteDelegate?


    private let scrollView = NSScrollView()
    private let stackView = NSStackView()
    private var items: [(title: String, url: String)] = []
    private var selectedIndex: Int = 0
    private static let maxResults = 5
    private static let rowHeight: CGFloat = 60
    private static let contentInset: CGFloat = 8
    private static let rowSpacing: CGFloat = 6

    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        setupUI()
    }

    required init?(coder: NSCoder) { fatalError() }

    override func viewDidChangeEffectiveAppearance() {
        super.viewDidChangeEffectiveAppearance()
        applyTheme()
    }

    private func applyTheme() {
        layer?.backgroundColor = AIDiaPalette.panelBackground.withAlphaComponent(0.92).cgColor
        layer?.borderColor = AIDiaPalette.softBorder.withAlphaComponent(0.9).cgColor
        layer?.shadowColor = AIDiaPalette.shadow.cgColor
        stackView.arrangedSubviews.compactMap { $0 as? MentionRowView }.forEach { $0.applyTheme() }
    }

    private func setupUI() {
        wantsLayer = true
        layer?.backgroundColor = AIDiaPalette.panelBackground.withAlphaComponent(0.92).cgColor
        layer?.cornerRadius = 20
        layer?.borderWidth = 1
        layer?.borderColor = AIDiaPalette.softBorder.withAlphaComponent(0.9).cgColor
        layer?.shadowColor = AIDiaPalette.shadow.cgColor
        layer?.shadowOpacity = 1
        layer?.shadowRadius = 18
        layer?.shadowOffset = CGSize(width: 0, height: -8)

        stackView.orientation = .vertical
        stackView.spacing = TabMentionAutocompleteView.rowSpacing
        stackView.translatesAutoresizingMaskIntoConstraints = false

        scrollView.documentView = stackView
        scrollView.hasVerticalScroller = false
        scrollView.drawsBackground = false
        scrollView.borderType = .noBorder
        scrollView.translatesAutoresizingMaskIntoConstraints = false
        addSubview(scrollView)

        NSLayoutConstraint.activate([
            scrollView.topAnchor.constraint(equalTo: topAnchor, constant: TabMentionAutocompleteView.contentInset),
            scrollView.leadingAnchor.constraint(equalTo: leadingAnchor, constant: TabMentionAutocompleteView.contentInset),
            scrollView.trailingAnchor.constraint(equalTo: trailingAnchor, constant: -TabMentionAutocompleteView.contentInset),
            scrollView.bottomAnchor.constraint(equalTo: bottomAnchor, constant: -TabMentionAutocompleteView.contentInset),
            stackView.widthAnchor.constraint(equalTo: scrollView.widthAnchor),
        ])

        applyTheme()
    }

    func update(tabs: [(title: String, url: String)]) {
        items = Array(tabs.prefix(TabMentionAutocompleteView.maxResults))
        selectedIndex = 0
        rebuildRows()
    }

    func moveSelectionUp() {
        guard !items.isEmpty else { return }
        selectedIndex = (selectedIndex - 1 + items.count) % items.count
        updateHighlight()
    }

    func moveSelectionDown() {
        guard !items.isEmpty else { return }
        selectedIndex = (selectedIndex + 1) % items.count
        updateHighlight()
    }

    func confirmSelection() {
        guard selectedIndex < items.count else { return }
        delegate?.tabMentionDidSelect(title: items[selectedIndex].title)
    }

    var preferredHeight: CGFloat {
        let count = min(items.count, TabMentionAutocompleteView.maxResults)
        let spacing = CGFloat(max(0, count - 1)) * TabMentionAutocompleteView.rowSpacing
        return CGFloat(count) * TabMentionAutocompleteView.rowHeight + spacing + (TabMentionAutocompleteView.contentInset * 2)
    }

    override var intrinsicContentSize: NSSize {
        NSSize(width: NSView.noIntrinsicMetric, height: preferredHeight)
    }

    private func rebuildRows() {
        stackView.arrangedSubviews.forEach { $0.removeFromSuperview() }
        for item in items {
            let row = MentionRowView(title: item.title, url: item.url)
            row.translatesAutoresizingMaskIntoConstraints = false
            row.addGestureRecognizer(NSClickGestureRecognizer(target: self, action: #selector(rowClicked(_:))))
            stackView.addArrangedSubview(row)
            row.widthAnchor.constraint(equalTo: stackView.widthAnchor).isActive = true
            row.heightAnchor.constraint(equalToConstant: TabMentionAutocompleteView.rowHeight).isActive = true
        }
        updateHighlight()
        invalidateIntrinsicContentSize()
    }

    @objc private func rowClicked(_ gesture: NSClickGestureRecognizer) {
        guard let row = gesture.view,
              let index = stackView.arrangedSubviews.firstIndex(of: row),
              index < items.count else { return }
        selectedIndex = index
        delegate?.tabMentionDidSelect(title: items[index].title)
    }

    private func updateHighlight() {
        for (index, view) in stackView.arrangedSubviews.enumerated() {
            (view as? MentionRowView)?.setHighlighted(index == selectedIndex)
        }
    }
}

private final class MentionRowView: NSView {
    private var isHighlightedRow = false
    private let iconBadgeView = NSView()
    private let iconView = NSImageView()
    private let titleLabel = NSTextField(labelWithString: "")
    private let urlLabel = NSTextField(labelWithString: "")
    private let tagLabel = NSTextField(labelWithString: "TAB")

    init(title: String, url: String) {
        super.init(frame: .zero)
        setupUI()
        titleLabel.stringValue = title
        urlLabel.stringValue = url
    }

    required init?(coder: NSCoder) { fatalError() }

    override func viewDidChangeEffectiveAppearance() {
        super.viewDidChangeEffectiveAppearance()
        applyTheme()
    }

    func applyTheme() {
        iconView.contentTintColor = AIDiaPalette.bodyText
        titleLabel.textColor = AIDiaPalette.titleText
        urlLabel.textColor = AIDiaPalette.secondaryText
        tagLabel.textColor = AIDiaPalette.mutedText
        setHighlighted(isHighlightedRow)
    }

    private func setupUI() {
        wantsLayer = true
        layer?.cornerRadius = 16
        layer?.borderWidth = 1
        layer?.backgroundColor = AIDiaPalette.panelInnerBackground.cgColor
        layer?.borderColor = AIDiaPalette.softBorder.cgColor

        iconBadgeView.translatesAutoresizingMaskIntoConstraints = false
        iconBadgeView.wantsLayer = true
        iconBadgeView.layer?.cornerRadius = 14
        addSubview(iconBadgeView)

        iconView.translatesAutoresizingMaskIntoConstraints = false
        iconView.image = NSImage(systemSymbolName: "globe", accessibilityDescription: "Tab")
        iconView.contentTintColor = AIDiaPalette.bodyText
        iconView.imageScaling = .scaleProportionallyDown
        iconBadgeView.addSubview(iconView)

        titleLabel.translatesAutoresizingMaskIntoConstraints = false
        titleLabel.font = .systemFont(ofSize: 12, weight: .semibold)
        titleLabel.textColor = AIDiaPalette.titleText
        titleLabel.lineBreakMode = .byTruncatingTail
        titleLabel.setContentCompressionResistancePriority(NSLayoutConstraint.Priority.defaultLow, for: .horizontal)
        addSubview(titleLabel)

        urlLabel.translatesAutoresizingMaskIntoConstraints = false
        urlLabel.font = .systemFont(ofSize: 10, weight: .medium)
        urlLabel.textColor = AIDiaPalette.secondaryText
        urlLabel.lineBreakMode = .byTruncatingTail
        addSubview(urlLabel)

        tagLabel.translatesAutoresizingMaskIntoConstraints = false
        tagLabel.font = .systemFont(ofSize: 9, weight: .bold)
        tagLabel.textColor = AIDiaPalette.mutedText
        tagLabel.alignment = .center
        tagLabel.wantsLayer = true
        tagLabel.layer?.cornerRadius = 10
        tagLabel.layer?.borderWidth = 1
        tagLabel.layer?.backgroundColor = AIDiaPalette.controlBackground.cgColor
        tagLabel.layer?.borderColor = AIDiaPalette.softBorder.cgColor
        tagLabel.setContentHuggingPriority(NSLayoutConstraint.Priority.required, for: .horizontal)
        tagLabel.setContentCompressionResistancePriority(NSLayoutConstraint.Priority.required, for: .horizontal)
        addSubview(tagLabel)

        NSLayoutConstraint.activate([
            iconBadgeView.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 12),
            iconBadgeView.centerYAnchor.constraint(equalTo: centerYAnchor),
            iconBadgeView.widthAnchor.constraint(equalToConstant: 28),
            iconBadgeView.heightAnchor.constraint(equalToConstant: 28),

            iconView.centerXAnchor.constraint(equalTo: iconBadgeView.centerXAnchor),
            iconView.centerYAnchor.constraint(equalTo: iconBadgeView.centerYAnchor),
            iconView.widthAnchor.constraint(equalToConstant: 13),
            iconView.heightAnchor.constraint(equalToConstant: 13),

            titleLabel.leadingAnchor.constraint(equalTo: iconBadgeView.trailingAnchor, constant: 12),
            titleLabel.topAnchor.constraint(equalTo: topAnchor, constant: 10),

            tagLabel.leadingAnchor.constraint(equalTo: titleLabel.trailingAnchor, constant: 8),
            tagLabel.centerYAnchor.constraint(equalTo: titleLabel.centerYAnchor),
            tagLabel.trailingAnchor.constraint(lessThanOrEqualTo: trailingAnchor, constant: -12),
            tagLabel.widthAnchor.constraint(equalToConstant: 34),
            tagLabel.heightAnchor.constraint(equalToConstant: 20),

            urlLabel.leadingAnchor.constraint(equalTo: iconBadgeView.trailingAnchor, constant: 12),
            urlLabel.topAnchor.constraint(equalTo: titleLabel.bottomAnchor, constant: 4),
            urlLabel.trailingAnchor.constraint(equalTo: trailingAnchor, constant: -12),
        ])

        applyTheme()
        setHighlighted(false)
    }

    func setHighlighted(_ highlighted: Bool) {
        isHighlightedRow = highlighted
        if highlighted {
            layer?.backgroundColor = AIDiaPalette.controlActiveBackground.cgColor
            layer?.borderColor = AIDiaPalette.accentBorder.cgColor
            layer?.shadowColor = AIDiaPalette.shadow.cgColor
            layer?.shadowOpacity = 1
            layer?.shadowRadius = 12
            layer?.shadowOffset = CGSize(width: 0, height: -4)
            iconBadgeView.layer?.backgroundColor = AIDiaPalette.accentWash.cgColor
            iconBadgeView.layer?.borderColor = AIDiaPalette.accentBorder.cgColor
            iconBadgeView.layer?.borderWidth = 1
            tagLabel.layer?.backgroundColor = AIDiaPalette.documentBackground.cgColor
            tagLabel.layer?.borderColor = AIDiaPalette.softBorder.cgColor
        } else {
            layer?.backgroundColor = AIDiaPalette.documentBackground.withAlphaComponent(0.56).cgColor
            layer?.borderColor = AIDiaPalette.softBorder.withAlphaComponent(0.22).cgColor
            layer?.shadowOpacity = 0
            iconBadgeView.layer?.backgroundColor = AIDiaPalette.panelInnerBackground.cgColor
            iconBadgeView.layer?.borderColor = AIDiaPalette.softBorder.withAlphaComponent(0.6).cgColor
            iconBadgeView.layer?.borderWidth = 1
            tagLabel.layer?.backgroundColor = AIDiaPalette.controlBackground.cgColor
            tagLabel.layer?.borderColor = AIDiaPalette.softBorder.withAlphaComponent(0.6).cgColor
        }
    }
}
