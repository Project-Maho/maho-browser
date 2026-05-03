import AppKit

protocol SuggestionChipBarDelegate: AnyObject {
    func suggestionChipTapped(_ prompt: String)
}

final class SuggestionChipBar: NSView {
    private var chipButtons: [NSButton] = []

    weak var delegate: SuggestionChipBarDelegate?

    private let outerStack = NSStackView()
    private var currentSuggestions: [String] = []

    private static let defaultSuggestions = [
        "Analyze",
        "Explain",
        "Summarize",
    ]

    private static let pageSuggestions = [
        "Analyze",
        "Explain",
        "Summarize",
    ]

    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        setupUI()
        updateSuggestions(hasPageContext: false)
    }

    required init?(coder: NSCoder) { fatalError() }

    override func viewDidChangeEffectiveAppearance() {
        super.viewDidChangeEffectiveAppearance()
        applyTheme()
    }

    func updateSuggestions(hasPageContext: Bool) {
        let suggestions = hasPageContext ? Self.pageSuggestions : Self.defaultSuggestions
        guard suggestions != currentSuggestions else { return }
        currentSuggestions = suggestions
        rebuildChips()
    }

    private func applyTheme() {
        chipButtons.forEach {
            $0.contentTintColor = AIDiaPalette.chipText
            $0.font = .systemFont(ofSize: 13, weight: NSFont.Weight(rawValue: 0.12))
            $0.attributedTitle = NSAttributedString(
                string: $0.title,
                attributes: [
                    .font: $0.font as Any,
                    .foregroundColor: AIDiaPalette.chipText,
                    .kern: -0.21,
                ]
            )
            $0.styleChromeButton(
                background: AIDiaPalette.chipBackground,
                border: AIDiaPalette.chipBorder,
                radius: 17,
                borderWidth: 0
            )
            $0.aiEnsureGradientLayer(
                named: "ai.chip.surface",
                colors: [AIDiaPalette.chipSheen, AIDiaPalette.chipBackground],
                startPoint: CGPoint(x: 0.5, y: 1),
                endPoint: CGPoint(x: 0.5, y: 0),
                locations: [0, 1],
                opacity: 1,
                cornerRadius: 17
            )
            $0.aiEnsureGradientLayer(
                named: "ai.chip.inset",
                colors: [AIDiaPalette.chipInsetHighlight, NSColor.clear],
                startPoint: CGPoint(x: 0.5, y: 1),
                endPoint: CGPoint(x: 0.5, y: 0.35),
                locations: [0, 1],
                opacity: 1,
                cornerRadius: 17
            )
        }
    }

    private func setupUI() {
        translatesAutoresizingMaskIntoConstraints = false

        outerStack.translatesAutoresizingMaskIntoConstraints = false
        outerStack.orientation = .vertical
        outerStack.alignment = .leading
        outerStack.spacing = 10
        addSubview(outerStack)

        NSLayoutConstraint.activate([
            outerStack.topAnchor.constraint(equalTo: topAnchor),
            outerStack.leadingAnchor.constraint(equalTo: leadingAnchor),
            outerStack.trailingAnchor.constraint(equalTo: trailingAnchor),
            outerStack.bottomAnchor.constraint(equalTo: bottomAnchor),
        ])

        applyTheme()
    }

    private func rebuildChips() {
        outerStack.arrangedSubviews.forEach { view in
            outerStack.removeArrangedSubview(view)
            view.removeFromSuperview()
        }
        chipButtons.removeAll()

        let rowStack = makeRowStack()
        outerStack.addArrangedSubview(rowStack)

        for suggestion in currentSuggestions {
            let chip = makeChipButton(title: suggestion)
            chipButtons.append(chip)
            rowStack.addArrangedSubview(chip)
        }
    }

    private func makeRowStack() -> NSStackView {
        let stack = NSStackView()
        stack.translatesAutoresizingMaskIntoConstraints = false
        stack.orientation = .horizontal
        stack.alignment = .centerY
        stack.spacing = 10
        stack.distribution = .fill
        stack.setContentHuggingPriority(.required, for: .horizontal)
        stack.setContentCompressionResistancePriority(.required, for: .horizontal)
        return stack
    }

    private func makeChipButton(title: String) -> NSButton {
        let button = NSButton(title: title, target: self, action: #selector(chipTapped(_:)))
        button.translatesAutoresizingMaskIntoConstraints = false
        let font = NSFont.systemFont(ofSize: 13, weight: NSFont.Weight(rawValue: 0.12))
        button.font = font
        button.contentTintColor = AIDiaPalette.chipText
        button.isBordered = false
        button.setButtonType(.momentaryPushIn)
        button.styleChromeButton(
            background: AIDiaPalette.chipBackground,
            border: AIDiaPalette.chipBorder,
            radius: 17,
            borderWidth: 0
        )
        button.setContentHuggingPriority(.required, for: .horizontal)
        button.setContentCompressionResistancePriority(.required, for: .horizontal)
        button.lineBreakMode = .byClipping
        let titleWidth = (title as NSString).size(withAttributes: [.font: font]).width
        button.widthAnchor.constraint(equalToConstant: ceil(titleWidth) + 22).isActive = true
        button.heightAnchor.constraint(equalToConstant: 34).isActive = true
        applyTheme()
        return button
    }

    @objc private func chipTapped(_ sender: NSButton) {
        delegate?.suggestionChipTapped(sender.title)
    }
}
