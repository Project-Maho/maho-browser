import AppKit

final class SuggestionCellView: NSTableCellView {
    private enum Metrics {
        static let rowHorizontalInset: CGFloat = 2
        static let rowVerticalInset: CGFloat = 0
        static let contentLeadingInset: CGFloat = 10
        static let contentTrailingInset: CGFloat = 6
        static let selectedContentLeadingInset: CGFloat = 10
        static let iconLaneWidth: CGFloat = 30
        static let selectedIconLaneWidth: CGFloat = 30
        static let iconSize: CGFloat = 21
        static let iconTileSize: CGFloat = 30
        static let iconToTextSpacing: CGFloat = 10
        static let selectedIconToTextSpacing: CGFloat = 10
        static let subtitleSpacing: CGFloat = 6
        static let shortcutSpacing: CGFloat = 10
        static let selectionCornerRadius: CGFloat = 11
        static let iconTileCornerRadius: CGFloat = 9
        static let titleFontSize: CGFloat = 17
        static let metaFontSize: CGFloat = 16
        static let selectedTitleVerticalOffset: CGFloat = -1
        static let selectedIconVerticalOffset: CGFloat = 0
    }

    private enum Colors {
        static let selectedFill = NSColor(red: 0.440, green: 0.409, blue: 0.363, alpha: 1)
        static let selectedBorder = NSColor(red: 0.612, green: 0.566, blue: 0.498, alpha: 1)
    }

    private let selectionBackgroundView = NSView()
    private let iconBackgroundView = NSView()
    private let iconLaneView = NSView()
    private let iconImageView = NSImageView()
    private let titleLabel = NSTextField(labelWithString: "")
    private let subtitleLabel = NSTextField(labelWithString: "")
    private let shortcutLabel = NSTextField(labelWithString: "")
    private let shortcutBackground = NSView()
    private let geekNewsIconLabel = NSTextField(labelWithString: "")
    private var iconLaneLeadingConstraint: NSLayoutConstraint?
    private var iconLaneWidthConstraint: NSLayoutConstraint?
    private var iconBackgroundCenterYConstraint: NSLayoutConstraint?
    private var iconImageCenterYConstraint: NSLayoutConstraint?
    private var geekNewsCenterYConstraint: NSLayoutConstraint?
    private var titleLeadingConstraint: NSLayoutConstraint?
    private var titleCenterYConstraint: NSLayoutConstraint?
    private var titleText = ""
    private var subtitleText = ""
    private var matchRanges: [[Int]]?
    private var hasShortcut = false
    private var iconIsTemplate = false
    private var usesGeekNewsIcon = false
    private var chromeBorderColor = MahoAppColors.commandBarPanelBorder

    var isSelected: Bool = false {
        didSet {
            updateTextLayout()
            updateAppearance()
        }
    }

    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        setupViews()
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    override func prepareForReuse() {
        super.prepareForReuse()
        isSelected = false
        titleText = ""
        subtitleText = ""
        matchRanges = nil
        hasShortcut = false
        iconIsTemplate = false
        titleLabel.stringValue = ""
        subtitleLabel.stringValue = ""
        subtitleLabel.isHidden = true
        iconImageView.image = nil
        iconImageView.contentTintColor = nil
        iconImageView.isHidden = false
        geekNewsIconLabel.isHidden = true
        iconBackgroundView.isHidden = true
        shortcutLabel.isHidden = true
        shortcutBackground.isHidden = true
        usesGeekNewsIcon = false
    }

    private func setupViews() {
        identifier = NSUserInterfaceItemIdentifier("SuggestionCellView")
        wantsLayer = true
        setAccessibilityElement(true)
        setAccessibilityRole(.cell)

        selectionBackgroundView.translatesAutoresizingMaskIntoConstraints = false
        selectionBackgroundView.wantsLayer = true
        selectionBackgroundView.layer?.cornerRadius = Metrics.selectionCornerRadius
        selectionBackgroundView.layer?.borderWidth = 0
        selectionBackgroundView.layer?.shadowColor = NSColor.black.withAlphaComponent(0.14).cgColor
        addSubview(selectionBackgroundView)

        iconLaneView.translatesAutoresizingMaskIntoConstraints = false
        iconLaneView.setAccessibilityElement(false)
        addSubview(iconLaneView)

        iconBackgroundView.translatesAutoresizingMaskIntoConstraints = false
        iconBackgroundView.wantsLayer = true
        iconBackgroundView.layer?.cornerRadius = Metrics.iconTileCornerRadius
        iconBackgroundView.isHidden = true
        addSubview(iconBackgroundView)

        iconImageView.translatesAutoresizingMaskIntoConstraints = false
        iconImageView.imageScaling = .scaleProportionallyDown
        addSubview(iconImageView)

        geekNewsIconLabel.translatesAutoresizingMaskIntoConstraints = false
        geekNewsIconLabel.alignment = .left
        geekNewsIconLabel.lineBreakMode = .byClipping
        geekNewsIconLabel.maximumNumberOfLines = 2
        geekNewsIconLabel.isHidden = true
        addSubview(geekNewsIconLabel)

        titleLabel.translatesAutoresizingMaskIntoConstraints = false
        titleLabel.font = NSFont.systemFont(ofSize: Metrics.titleFontSize, weight: .semibold)
        titleLabel.textColor = MahoAppColors.primaryText
        titleLabel.lineBreakMode = .byTruncatingTail
        titleLabel.maximumNumberOfLines = 1
        titleLabel.setContentCompressionResistancePriority(.defaultHigh, for: .horizontal)
        addSubview(titleLabel)

        subtitleLabel.translatesAutoresizingMaskIntoConstraints = false
        subtitleLabel.font = NSFont.systemFont(ofSize: Metrics.metaFontSize, weight: .medium)
        subtitleLabel.textColor = MahoAppColors.tertiaryText
        subtitleLabel.lineBreakMode = .byClipping
        subtitleLabel.maximumNumberOfLines = 1
        subtitleLabel.setContentCompressionResistancePriority(.defaultLow - 1, for: .horizontal)
        subtitleLabel.isHidden = true
        addSubview(subtitleLabel)

        shortcutBackground.translatesAutoresizingMaskIntoConstraints = false
        shortcutBackground.wantsLayer = true
        shortcutBackground.layer?.cornerRadius = 9
        shortcutBackground.layer?.borderWidth = 1
        shortcutBackground.layer?.backgroundColor = MahoAppColors.commandBarShortcutBadge.cgColor
        shortcutBackground.isHidden = true
        addSubview(shortcutBackground)

        shortcutLabel.translatesAutoresizingMaskIntoConstraints = false
        shortcutLabel.font = NSFont.systemFont(ofSize: 12, weight: .semibold)
        shortcutLabel.textColor = MahoAppColors.commandBarShortcutText
        shortcutLabel.lineBreakMode = .byClipping
        shortcutLabel.alignment = .center
        shortcutLabel.setContentCompressionResistancePriority(.required, for: .horizontal)
        shortcutLabel.isHidden = true
        addSubview(shortcutLabel)

        NSLayoutConstraint.activate([
            selectionBackgroundView.topAnchor.constraint(equalTo: topAnchor, constant: Metrics.rowVerticalInset),
            selectionBackgroundView.leadingAnchor.constraint(equalTo: leadingAnchor, constant: Metrics.rowHorizontalInset),
            selectionBackgroundView.trailingAnchor.constraint(equalTo: trailingAnchor, constant: -Metrics.rowHorizontalInset),
            selectionBackgroundView.bottomAnchor.constraint(equalTo: bottomAnchor, constant: -Metrics.rowVerticalInset),

            iconLaneView.centerYAnchor.constraint(equalTo: centerYAnchor),
            iconLaneView.heightAnchor.constraint(equalToConstant: Metrics.iconTileSize),

            iconBackgroundView.centerXAnchor.constraint(equalTo: iconLaneView.centerXAnchor),
            iconBackgroundView.widthAnchor.constraint(equalToConstant: Metrics.iconTileSize),
            iconBackgroundView.heightAnchor.constraint(equalToConstant: Metrics.iconTileSize),

            iconImageView.centerXAnchor.constraint(equalTo: iconLaneView.centerXAnchor),
            iconImageView.widthAnchor.constraint(equalToConstant: Metrics.iconSize),
            iconImageView.heightAnchor.constraint(equalToConstant: Metrics.iconSize),

            geekNewsIconLabel.centerXAnchor.constraint(equalTo: iconLaneView.centerXAnchor),
            geekNewsIconLabel.widthAnchor.constraint(equalToConstant: Metrics.iconTileSize),

            subtitleLabel.leadingAnchor.constraint(equalTo: titleLabel.trailingAnchor, constant: Metrics.subtitleSpacing),
            subtitleLabel.firstBaselineAnchor.constraint(equalTo: titleLabel.firstBaselineAnchor),
            subtitleLabel.trailingAnchor.constraint(lessThanOrEqualTo: shortcutBackground.leadingAnchor, constant: -Metrics.shortcutSpacing),

            shortcutBackground.trailingAnchor.constraint(equalTo: trailingAnchor, constant: -Metrics.contentTrailingInset),
            shortcutBackground.centerYAnchor.constraint(equalTo: centerYAnchor),

            shortcutLabel.leadingAnchor.constraint(equalTo: shortcutBackground.leadingAnchor, constant: 7),
            shortcutLabel.trailingAnchor.constraint(equalTo: shortcutBackground.trailingAnchor, constant: -7),
            shortcutLabel.topAnchor.constraint(equalTo: shortcutBackground.topAnchor, constant: 4),
            shortcutLabel.bottomAnchor.constraint(equalTo: shortcutBackground.bottomAnchor, constant: -4),
        ])

        iconLaneLeadingConstraint = iconLaneView.leadingAnchor.constraint(equalTo: leadingAnchor, constant: Metrics.contentLeadingInset)
        iconLaneLeadingConstraint?.isActive = true
        iconLaneWidthConstraint = iconLaneView.widthAnchor.constraint(equalToConstant: Metrics.iconLaneWidth)
        iconLaneWidthConstraint?.isActive = true
        iconBackgroundCenterYConstraint = iconBackgroundView.centerYAnchor.constraint(equalTo: centerYAnchor)
        iconBackgroundCenterYConstraint?.isActive = true
        iconImageCenterYConstraint = iconImageView.centerYAnchor.constraint(equalTo: centerYAnchor)
        iconImageCenterYConstraint?.isActive = true
        geekNewsCenterYConstraint = geekNewsIconLabel.centerYAnchor.constraint(equalTo: centerYAnchor)
        geekNewsCenterYConstraint?.isActive = true
        titleLeadingConstraint = titleLabel.leadingAnchor.constraint(equalTo: iconLaneView.trailingAnchor, constant: Metrics.iconToTextSpacing)
        titleLeadingConstraint?.isActive = true
        titleCenterYConstraint = titleLabel.centerYAnchor.constraint(equalTo: centerYAnchor)
        titleCenterYConstraint?.isActive = true

        let titleTrailing = titleLabel.trailingAnchor.constraint(lessThanOrEqualTo: trailingAnchor, constant: -Metrics.contentTrailingInset)
        titleTrailing.priority = .defaultHigh
        titleTrailing.isActive = true

        updateTextLayout()
        updateAppearance()
    }

    func configure(with suggestion: SuggestionViewModel, shortcutText: String? = nil) {
        titleText = suggestion.title
        subtitleText = suggestion.subtitle ?? ""
        matchRanges = suggestion.matchRanges
        titleLabel.stringValue = suggestion.title
        usesGeekNewsIcon = false
        geekNewsIconLabel.isHidden = true
        iconImageView.isHidden = false
        updateTextLayout()

        if applyCustomMockupIconIfNeeded(for: suggestion) {
            iconIsTemplate = false
        } else if let icon = suggestion.icon {
            let rawData = Data(icon.data.map { UInt8($0) })
            if let image = NSImage(data: rawData) {
                image.size = NSSize(width: Metrics.iconSize, height: Metrics.iconSize)
                iconImageView.image = image
                iconIsTemplate = false
                iconImageView.contentTintColor = nil
            } else {
                let placeholder = placeholderIcon(for: suggestion.kind)
                iconImageView.image = placeholder
                iconIsTemplate = placeholder?.isTemplate ?? false
            }
        } else {
            let placeholder = placeholderIcon(for: suggestion.kind)
            iconImageView.image = placeholder
            iconIsTemplate = placeholder?.isTemplate ?? false
        }

        if let sc = shortcutText {
            hasShortcut = true
            shortcutLabel.stringValue = sc
            shortcutLabel.isHidden = false
            shortcutBackground.isHidden = false
        } else {
            hasShortcut = false
            shortcutLabel.isHidden = true
            shortcutBackground.isHidden = true
        }

        setAccessibilityIdentifier("commandBar.cell.\(suggestion.key)")
        setAccessibilityLabel("\(suggestion.title), \(suggestion.kind.rawValue)")
        selectionBackgroundView.setAccessibilityElement(false)
        iconLaneView.setAccessibilityElement(false)
        iconBackgroundView.setAccessibilityElement(false)
        iconImageView.setAccessibilityElement(false)
        titleLabel.setAccessibilityElement(false)
        subtitleLabel.setAccessibilityElement(false)
        shortcutBackground.setAccessibilityElement(false)
        shortcutLabel.setAccessibilityElement(false)
        updateAppearance()
    }

    func configureForRecentSearch(_ search: String) {
        titleText = search
        subtitleText = ""
        matchRanges = nil
        hasShortcut = false
        titleLabel.stringValue = search
        let clockIcon = MahoIcon.imageForSFSymbol("clock")
        iconImageView.image = clockIcon
        iconIsTemplate = clockIcon?.isTemplate ?? false
        iconImageView.isHidden = false
        geekNewsIconLabel.isHidden = true
        shortcutLabel.isHidden = true
        shortcutBackground.isHidden = true
        setAccessibilityIdentifier("commandBar.cell.recent.\(search)")
        setAccessibilityLabel(search)
        selectionBackgroundView.setAccessibilityElement(false)
        iconLaneView.setAccessibilityElement(false)
        iconBackgroundView.setAccessibilityElement(false)
        iconImageView.setAccessibilityElement(false)
        titleLabel.setAccessibilityElement(false)
        subtitleLabel.setAccessibilityElement(false)
        shortcutBackground.setAccessibilityElement(false)
        shortcutLabel.setAccessibilityElement(false)
        updateTextLayout()
        updateAppearance()
    }

    private func placeholderIcon(for kind: SuggestionType) -> NSImage? {
        if kind == .tab {
            return CommandBarSparkleArt.image(size: NSSize(width: Metrics.iconSize, height: Metrics.iconSize))
        }

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
        return MahoIcon.imageForSFSymbol(name, pointSize: Metrics.iconSize)
    }

    private func applyCustomMockupIconIfNeeded(for suggestion: SuggestionViewModel) -> Bool {
        switch suggestion.key {
        case "search:https://accounts.google.com/signin/v2/identifier?continue=https%3A%2F%2Fgemini.google.com%2Foauth-callback":
            iconImageView.image = googleGImage(size: NSSize(width: Metrics.iconSize, height: Metrics.iconSize))
            iconImageView.isHidden = false
            geekNewsIconLabel.isHidden = true
            return true
        case "nav:http://localhost:3456":
            iconImageView.image = globeImage(size: NSSize(width: Metrics.iconSize, height: Metrics.iconSize))
            iconImageView.isHidden = false
            geekNewsIconLabel.isHidden = true
            return true
        case "history:https://news.hada.io/topic?id=5047&utm_campaign=5047":
            usesGeekNewsIcon = true
            iconImageView.image = nil
            iconImageView.isHidden = true
            geekNewsIconLabel.attributedStringValue = geekNewsIconText()
            geekNewsIconLabel.isHidden = false
            return true
        default:
            return false
        }
    }

    private func googleGImage(size: NSSize) -> NSImage? {
        let image = NSImage(size: size)
        image.lockFocus()

        let rect = NSRect(origin: .zero, size: size)
        let ringRect = rect.insetBy(dx: size.width * 0.06, dy: size.height * 0.06)
        NSGradient(colors: [
            NSColor(red: 0.26, green: 0.52, blue: 0.96, alpha: 1),
            NSColor(red: 0.20, green: 0.66, blue: 0.33, alpha: 1),
            NSColor(red: 0.98, green: 0.74, blue: 0.05, alpha: 1),
            NSColor(red: 0.92, green: 0.26, blue: 0.21, alpha: 1)
        ])?.draw(in: NSBezierPath(ovalIn: ringRect), angle: -45)

        NSColor(calibratedWhite: 0.06, alpha: 1).setFill()
        NSBezierPath(ovalIn: ringRect.insetBy(dx: size.width * 0.18, dy: size.height * 0.18)).fill()

        let letterRect = NSRect(x: rect.minX, y: rect.minY + size.height * 0.02, width: rect.width, height: rect.height)
        let paragraph = NSMutableParagraphStyle()
        paragraph.alignment = .center
        let attributes: [NSAttributedString.Key: Any] = [
            .font: NSFont.systemFont(ofSize: size.height * 0.58, weight: .bold),
            .foregroundColor: NSColor(red: 0.23, green: 0.50, blue: 0.98, alpha: 1),
            .paragraphStyle: paragraph,
            .kern: -0.6
        ]
        NSAttributedString(string: "G", attributes: attributes).draw(in: letterRect)

        image.unlockFocus()
        return image
    }

    private func globeImage(size: NSSize) -> NSImage? {
        let image = NSImage(size: size)
        image.lockFocus()

        let rect = NSRect(origin: .zero, size: size).insetBy(dx: size.width * 0.14, dy: size.height * 0.14)
        let stroke = NSBezierPath()
        stroke.lineWidth = 1.3
        NSColor(calibratedWhite: 0.45, alpha: 1).setStroke()
        stroke.appendOval(in: rect)
        stroke.stroke()

        let horizontal = NSBezierPath()
        horizontal.lineWidth = 1.1
        horizontal.move(to: CGPoint(x: rect.minX, y: rect.midY))
        horizontal.line(to: CGPoint(x: rect.maxX, y: rect.midY))
        horizontal.stroke()

        let verticalArcRight = NSBezierPath()
        verticalArcRight.lineWidth = 1.1
        verticalArcRight.move(to: CGPoint(x: rect.midX, y: rect.minY))
        verticalArcRight.curve(to: CGPoint(x: rect.midX, y: rect.maxY), controlPoint1: CGPoint(x: rect.maxX, y: rect.minY + rect.height * 0.22), controlPoint2: CGPoint(x: rect.maxX, y: rect.maxY - rect.height * 0.22))
        verticalArcRight.stroke()

        let verticalArcLeft = NSBezierPath()
        verticalArcLeft.lineWidth = 1.1
        verticalArcLeft.move(to: CGPoint(x: rect.midX, y: rect.minY))
        verticalArcLeft.curve(to: CGPoint(x: rect.midX, y: rect.maxY), controlPoint1: CGPoint(x: rect.minX, y: rect.minY + rect.height * 0.22), controlPoint2: CGPoint(x: rect.minX, y: rect.maxY - rect.height * 0.22))
        verticalArcLeft.stroke()

        image.unlockFocus()
        return image
    }

    private func geekNewsIconText() -> NSAttributedString {
        let paragraph = NSMutableParagraphStyle()
        paragraph.alignment = .left
        paragraph.lineSpacing = -1.5
        return NSAttributedString(
            string: "Geek\nNews",
            attributes: [
                .font: NSFont.systemFont(ofSize: 8.5, weight: .bold),
                .foregroundColor: NSColor(calibratedWhite: 0.42, alpha: 1),
                .paragraphStyle: paragraph,
                .kern: -0.45
            ]
        )
    }

    private func formattedSubtitleText() -> String {
        guard !subtitleText.isEmpty else { return "" }
        return "— \(subtitleText)"
    }

    private func updateAppearance() {
        iconLaneLeadingConstraint?.constant = isSelected ? Metrics.selectedContentLeadingInset : Metrics.contentLeadingInset
        iconLaneWidthConstraint?.constant = isSelected ? Metrics.selectedIconLaneWidth : Metrics.iconLaneWidth
        iconBackgroundCenterYConstraint?.constant = isSelected ? Metrics.selectedIconVerticalOffset : 0
        iconImageCenterYConstraint?.constant = isSelected ? Metrics.selectedIconVerticalOffset : 0
        geekNewsCenterYConstraint?.constant = isSelected ? Metrics.selectedIconVerticalOffset : 0
        titleLeadingConstraint?.constant = isSelected ? Metrics.selectedIconToTextSpacing : Metrics.iconToTextSpacing
        titleCenterYConstraint?.constant = isSelected ? Metrics.selectedTitleVerticalOffset : 0

        let titleColor = isSelected ? NSColor(calibratedWhite: 0.97, alpha: 1) : MahoAppColors.primaryText
        let secondaryColor = isSelected ? NSColor.white.withAlphaComponent(0.72) : MahoAppColors.tertiaryText
        let selectionBorderColor = Colors.selectedBorder.blended(withFraction: 0.1, of: chromeBorderColor) ?? Colors.selectedBorder

        let selectionFill = isSelected ? Colors.selectedFill : NSColor.clear
        selectionBackgroundView.layer?.backgroundColor = selectionFill.cgColor
        selectionBackgroundView.layer?.cornerRadius = isSelected ? Metrics.selectionCornerRadius : Metrics.selectionCornerRadius
        selectionBackgroundView.layer?.borderColor = isSelected ? selectionBorderColor.cgColor : NSColor.clear.cgColor
        selectionBackgroundView.layer?.borderWidth = isSelected ? 0.75 : 0
        selectionBackgroundView.layer?.shadowOpacity = isSelected ? 0.11 : 0
        selectionBackgroundView.layer?.shadowRadius = isSelected ? 5 : 0
        selectionBackgroundView.layer?.shadowOffset = CGSize(width: 0, height: -2)

        iconBackgroundView.isHidden = !isSelected
        iconBackgroundView.layer?.cornerRadius = isSelected ? 8 : Metrics.iconTileCornerRadius
        iconBackgroundView.layer?.backgroundColor = isSelected ? NSColor.white.withAlphaComponent(0.98).cgColor : NSColor.clear.cgColor
        iconBackgroundView.layer?.borderColor = isSelected ? NSColor.white.withAlphaComponent(0.28).cgColor : NSColor.clear.cgColor
        iconBackgroundView.layer?.borderWidth = isSelected ? 0.75 : 0
        iconBackgroundView.layer?.shadowColor = NSColor.black.withAlphaComponent(0.20).cgColor
        iconBackgroundView.layer?.shadowOpacity = isSelected ? 0.12 : 0
        iconBackgroundView.layer?.shadowRadius = isSelected ? 5 : 0
        iconBackgroundView.layer?.shadowOffset = CGSize(width: 0, height: -1)

        if iconIsTemplate {
            iconImageView.contentTintColor = isSelected
                ? MahoAppColors.commandBarBackground.withAlphaComponent(0.96)
                : MahoAppColors.primaryText.withAlphaComponent(0.96)
        } else {
            iconImageView.contentTintColor = nil
        }

        geekNewsIconLabel.alphaValue = usesGeekNewsIcon && isSelected ? 0.96 : 1

        subtitleLabel.textColor = secondaryColor
        subtitleLabel.isHidden = subtitleText.isEmpty || isSelected

        if hasShortcut {
            shortcutLabel.textColor = isSelected ? NSColor.white : MahoAppColors.commandBarShortcutText
            shortcutBackground.layer?.borderColor = isSelected
                ? NSColor.white.withAlphaComponent(0.14).cgColor
                : MahoAppColors.commandBarKeycapBorder.cgColor
            shortcutBackground.layer?.backgroundColor = isSelected
                ? NSColor.white.withAlphaComponent(0.18).cgColor
                : MahoAppColors.commandBarShortcutBadge.cgColor
        }

        let attributed = NSMutableAttributedString(string: titleText)
        attributed.addAttribute(
            .font,
            value: NSFont.systemFont(ofSize: Metrics.titleFontSize, weight: isSelected ? .semibold : .medium),
            range: NSRange(location: 0, length: attributed.length)
        )
        attributed.addAttribute(
            .foregroundColor,
            value: titleColor,
            range: NSRange(location: 0, length: attributed.length)
        )
        attributed.addAttribute(
            .kern,
            value: -0.82,
            range: NSRange(location: 0, length: attributed.length)
        )

        if let ranges = matchRanges, !ranges.isEmpty {
            let boldFont = NSFont.systemFont(ofSize: Metrics.titleFontSize, weight: .semibold)
            for range in ranges {
                guard range.count == 2 else { continue }
                let start = max(0, min(range[0], titleText.utf16.count))
                let end = max(start, min(range[1], titleText.utf16.count))
                if start < end {
                    attributed.addAttribute(.font, value: boldFont, range: NSRange(location: start, length: end - start))
                }
            }
        }

        titleLabel.attributedStringValue = attributed
    }

    func updateSelectionChromeTint(active: Bool, chromeBorderColor: NSColor) {
        self.chromeBorderColor = chromeBorderColor
        if active {
            updateAppearance()
        }
    }

    private func updateTextLayout() {
        let subtitle = formattedSubtitleText()
        subtitleLabel.attributedStringValue = NSAttributedString(
            string: subtitle,
            attributes: [
                .font: NSFont.systemFont(ofSize: Metrics.metaFontSize, weight: .medium),
                .foregroundColor: subtitleLabel.textColor ?? MahoAppColors.tertiaryText,
                .kern: -0.72
            ]
        )
        subtitleLabel.isHidden = subtitleText.isEmpty || isSelected
    }
}
