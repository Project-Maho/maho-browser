import AppKit

protocol SkillPickerDelegate: AnyObject {
    func skillPickerDidSelect(_ skill: SkillInfo)
    func skillPickerDidDismiss()
}

final class SkillPickerView: NSView {
    weak var delegate: SkillPickerDelegate?

    private let scrollView = NSScrollView()
    private let stackView = NSStackView()
    private var items: [SkillInfo] = []
    private var selectedIndex: Int = 0
    private static let maxResults = 6
    private static let rowHeight: CGFloat = 72
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
        stackView.arrangedSubviews.compactMap { $0 as? SkillRowView }.forEach { $0.applyTheme() }
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
        stackView.spacing = SkillPickerView.rowSpacing
        stackView.translatesAutoresizingMaskIntoConstraints = false

        scrollView.documentView = stackView
        scrollView.hasVerticalScroller = false
        scrollView.drawsBackground = false
        scrollView.borderType = .noBorder
        scrollView.translatesAutoresizingMaskIntoConstraints = false
        addSubview(scrollView)

        NSLayoutConstraint.activate([
            scrollView.topAnchor.constraint(equalTo: topAnchor, constant: SkillPickerView.contentInset),
            scrollView.leadingAnchor.constraint(equalTo: leadingAnchor, constant: SkillPickerView.contentInset),
            scrollView.trailingAnchor.constraint(equalTo: trailingAnchor, constant: -SkillPickerView.contentInset),
            scrollView.bottomAnchor.constraint(equalTo: bottomAnchor, constant: -SkillPickerView.contentInset),
            stackView.widthAnchor.constraint(equalTo: scrollView.widthAnchor),
        ])

        applyTheme()
    }

    func update(skills: [SkillInfo]) {
        items = Array(skills.prefix(SkillPickerView.maxResults))
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
        delegate?.skillPickerDidSelect(items[selectedIndex])
    }

    var preferredHeight: CGFloat {
        let count = min(items.count, SkillPickerView.maxResults)
        let spacing = CGFloat(max(0, count - 1)) * SkillPickerView.rowSpacing
        return CGFloat(count) * SkillPickerView.rowHeight + spacing + (SkillPickerView.contentInset * 2)
    }

    override var intrinsicContentSize: NSSize {
        NSSize(width: NSView.noIntrinsicMetric, height: preferredHeight)
    }

    private func rebuildRows() {
        stackView.arrangedSubviews.forEach { $0.removeFromSuperview() }
        for skill in items {
            let row = SkillRowView(skill: skill)
            row.translatesAutoresizingMaskIntoConstraints = false
            row.addGestureRecognizer(NSClickGestureRecognizer(target: self, action: #selector(rowClicked(_:))))
            stackView.addArrangedSubview(row)
            row.widthAnchor.constraint(equalTo: stackView.widthAnchor).isActive = true
            row.heightAnchor.constraint(equalToConstant: SkillPickerView.rowHeight).isActive = true
        }
        updateHighlight()
        invalidateIntrinsicContentSize()
    }

    @objc private func rowClicked(_ gesture: NSClickGestureRecognizer) {
        guard let row = gesture.view,
              let index = stackView.arrangedSubviews.firstIndex(of: row),
              index < items.count else { return }
        selectedIndex = index
        delegate?.skillPickerDidSelect(items[index])
    }

    private func updateHighlight() {
        for (index, view) in stackView.arrangedSubviews.enumerated() {
            (view as? SkillRowView)?.setHighlighted(index == selectedIndex)
        }
    }
}

private final class SkillRowView: NSView {
    private var isHighlightedRow = false

    private let iconBadgeView = NSView()
    private let iconView = NSImageView()
    private let nameLabel = NSTextField(labelWithString: "")
    private let slashLabel = NSTextField(labelWithString: "")
    private let descLabel = NSTextField(labelWithString: "")
    private let actionHintLabel = NSTextField(labelWithString: "RUN")

    init(skill: SkillInfo) {
        super.init(frame: .zero)
        setupUI(skill: skill)
    }

    required init?(coder: NSCoder) { fatalError() }

    override func viewDidChangeEffectiveAppearance() {
        super.viewDidChangeEffectiveAppearance()
        applyTheme()
    }

    func applyTheme() {
        nameLabel.textColor = AIDiaPalette.titleText
        iconView.contentTintColor = AIDiaPalette.bodyText
        descLabel.textColor = AIDiaPalette.secondaryText
        actionHintLabel.textColor = AIDiaPalette.mutedText
        slashLabel.textColor = AIDiaPalette.secondaryText
        setHighlighted(isHighlightedRow)
    }

    private func setupUI(skill: SkillInfo) {
        wantsLayer = true
        layer?.cornerRadius = 18
        layer?.borderWidth = 1
        layer?.backgroundColor = AIDiaPalette.panelInnerBackground.cgColor
        layer?.borderColor = AIDiaPalette.softBorder.cgColor

        iconBadgeView.translatesAutoresizingMaskIntoConstraints = false
        iconBadgeView.wantsLayer = true
        iconBadgeView.layer?.cornerRadius = 16
        addSubview(iconBadgeView)

        iconView.translatesAutoresizingMaskIntoConstraints = false
        iconView.image = NSImage(systemSymbolName: skill.icon, accessibilityDescription: skill.name)
        iconView.contentTintColor = AIDiaPalette.bodyText
        iconView.imageScaling = .scaleProportionallyDown
        iconBadgeView.addSubview(iconView)

        nameLabel.translatesAutoresizingMaskIntoConstraints = false
        nameLabel.font = .systemFont(ofSize: 12, weight: .semibold)
        nameLabel.textColor = AIDiaPalette.titleText
        nameLabel.lineBreakMode = .byTruncatingTail
        nameLabel.stringValue = skill.name
        nameLabel.setContentCompressionResistancePriority(NSLayoutConstraint.Priority.defaultLow, for: .horizontal)
        addSubview(nameLabel)

        slashLabel.translatesAutoresizingMaskIntoConstraints = false
        slashLabel.font = .monospacedSystemFont(ofSize: 10, weight: .semibold)
        slashLabel.textColor = AIDiaPalette.secondaryText
        slashLabel.alignment = .center
        slashLabel.wantsLayer = true
        slashLabel.layer?.cornerRadius = 11
        slashLabel.layer?.borderWidth = 1
        slashLabel.layer?.backgroundColor = AIDiaPalette.controlBackground.cgColor
        slashLabel.layer?.borderColor = AIDiaPalette.softBorder.cgColor
        slashLabel.stringValue = skill.slashCommand
        slashLabel.setContentHuggingPriority(NSLayoutConstraint.Priority.required, for: .horizontal)
        slashLabel.setContentCompressionResistancePriority(NSLayoutConstraint.Priority.required, for: .horizontal)
        addSubview(slashLabel)

        descLabel.translatesAutoresizingMaskIntoConstraints = false
        descLabel.font = .systemFont(ofSize: 10, weight: .medium)
        descLabel.textColor = AIDiaPalette.secondaryText
        descLabel.lineBreakMode = .byWordWrapping
        descLabel.maximumNumberOfLines = 2
        descLabel.stringValue = skill.description
        addSubview(descLabel)

        actionHintLabel.translatesAutoresizingMaskIntoConstraints = false
        actionHintLabel.font = .systemFont(ofSize: 9, weight: .bold)
        actionHintLabel.textColor = AIDiaPalette.mutedText
        actionHintLabel.alignment = .center
        actionHintLabel.wantsLayer = true
        actionHintLabel.layer?.cornerRadius = 11
        actionHintLabel.layer?.borderWidth = 1
        actionHintLabel.layer?.backgroundColor = AIDiaPalette.controlBackground.cgColor
        actionHintLabel.layer?.borderColor = AIDiaPalette.softBorder.cgColor
        actionHintLabel.setContentHuggingPriority(NSLayoutConstraint.Priority.required, for: .horizontal)
        actionHintLabel.setContentCompressionResistancePriority(NSLayoutConstraint.Priority.required, for: .horizontal)
        addSubview(actionHintLabel)

        NSLayoutConstraint.activate([
            iconBadgeView.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 12),
            iconBadgeView.centerYAnchor.constraint(equalTo: centerYAnchor),
            iconBadgeView.widthAnchor.constraint(equalToConstant: 32),
            iconBadgeView.heightAnchor.constraint(equalToConstant: 32),

            iconView.centerXAnchor.constraint(equalTo: iconBadgeView.centerXAnchor),
            iconView.centerYAnchor.constraint(equalTo: iconBadgeView.centerYAnchor),
            iconView.widthAnchor.constraint(equalToConstant: 15),
            iconView.heightAnchor.constraint(equalToConstant: 15),

            nameLabel.leadingAnchor.constraint(equalTo: iconBadgeView.trailingAnchor, constant: 12),
            nameLabel.topAnchor.constraint(equalTo: topAnchor, constant: 10),

            slashLabel.leadingAnchor.constraint(equalTo: nameLabel.trailingAnchor, constant: 8),
            slashLabel.centerYAnchor.constraint(equalTo: nameLabel.centerYAnchor),
            slashLabel.heightAnchor.constraint(equalToConstant: 22),
            slashLabel.widthAnchor.constraint(greaterThanOrEqualToConstant: 54),

            actionHintLabel.leadingAnchor.constraint(equalTo: slashLabel.trailingAnchor, constant: 8),
            actionHintLabel.centerYAnchor.constraint(equalTo: slashLabel.centerYAnchor),
            actionHintLabel.trailingAnchor.constraint(lessThanOrEqualTo: trailingAnchor, constant: -12),
            actionHintLabel.widthAnchor.constraint(equalToConstant: 42),
            actionHintLabel.heightAnchor.constraint(equalToConstant: 22),

            descLabel.leadingAnchor.constraint(equalTo: iconBadgeView.trailingAnchor, constant: 12),
            descLabel.topAnchor.constraint(equalTo: nameLabel.bottomAnchor, constant: 4),
            descLabel.trailingAnchor.constraint(equalTo: trailingAnchor, constant: -12),
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
            layer?.shadowRadius = 14
            layer?.shadowOffset = CGSize(width: 0, height: -5)
            iconBadgeView.layer?.backgroundColor = AIDiaPalette.accentWash.cgColor
            iconBadgeView.layer?.borderColor = AIDiaPalette.accentBorder.cgColor
            iconBadgeView.layer?.borderWidth = 1
            slashLabel.layer?.backgroundColor = AIDiaPalette.documentBackground.cgColor
            slashLabel.layer?.borderColor = AIDiaPalette.softBorder.cgColor
            actionHintLabel.layer?.backgroundColor = AIDiaPalette.accentWash.cgColor
            actionHintLabel.layer?.borderColor = AIDiaPalette.accentBorder.cgColor
        } else {
            layer?.backgroundColor = AIDiaPalette.documentBackground.withAlphaComponent(0.56).cgColor
            layer?.borderColor = AIDiaPalette.softBorder.withAlphaComponent(0.22).cgColor
            layer?.shadowOpacity = 0
            iconBadgeView.layer?.backgroundColor = AIDiaPalette.panelInnerBackground.cgColor
            iconBadgeView.layer?.borderColor = AIDiaPalette.softBorder.withAlphaComponent(0.6).cgColor
            iconBadgeView.layer?.borderWidth = 1
            slashLabel.layer?.backgroundColor = AIDiaPalette.controlBackground.cgColor
            slashLabel.layer?.borderColor = AIDiaPalette.softBorder.withAlphaComponent(0.6).cgColor
            actionHintLabel.layer?.backgroundColor = AIDiaPalette.controlBackground.cgColor
            actionHintLabel.layer?.borderColor = AIDiaPalette.softBorder.withAlphaComponent(0.6).cgColor
        }
    }
}
