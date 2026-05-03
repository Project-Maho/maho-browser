import AppKit
import WebKit

final class ReaderModeController: NSViewController {
    enum ReaderBackground: Int {
        case light = 0
        case sepia = 1
        case dark = 2

        var backgroundColor: NSColor {
            switch self {
            case .light: return .white
            case .sepia: return NSColor(red: 0.98, green: 0.95, blue: 0.89, alpha: 1.0)
            case .dark: return NSColor(red: 0.15, green: 0.15, blue: 0.15, alpha: 1.0)
            }
        }

        var textColor: NSColor {
            switch self {
            case .light, .sepia: return NSColor(red: 0.13, green: 0.13, blue: 0.13, alpha: 1.0)
            case .dark: return NSColor(red: 0.88, green: 0.88, blue: 0.88, alpha: 1.0)
            }
        }
    }

    private let scrollView = NSScrollView()
    private let textView = NSTextView()
    private let readerToolbar = NSView()
    private let closeButton = NSButton(title: "✕", target: nil, action: nil)
    private let fontSizeUpButton = NSButton(title: "A+", target: nil, action: nil)
    private let fontSizeDownButton = NSButton(title: "A−", target: nil, action: nil)
    private let bgToggleButton = NSButton(title: "◐", target: nil, action: nil)

    private var fontSize: CGFloat = 18.0
    private var currentBackground: ReaderBackground = .light

    var onDismiss: (() -> Void)?

    override func loadView() {
        let container = NSView(frame: NSRect(x: 0, y: 0, width: 700, height: 500))
        container.wantsLayer = true
        container.layer?.backgroundColor = currentBackground.backgroundColor.cgColor
        self.view = container
    }

    override func viewDidLoad() {
        super.viewDidLoad()
        setupToolbar()
        setupTextView()
    }

    private func setupToolbar() {
        readerToolbar.translatesAutoresizingMaskIntoConstraints = false
        readerToolbar.wantsLayer = true
        readerToolbar.layer?.backgroundColor = NSColor(white: 0.95, alpha: 1).cgColor
        view.addSubview(readerToolbar)

        for button in [closeButton, fontSizeDownButton, fontSizeUpButton, bgToggleButton] {
            button.translatesAutoresizingMaskIntoConstraints = false
            button.bezelStyle = .rounded
            button.setButtonType(.momentaryPushIn)
            button.target = self
            readerToolbar.addSubview(button)
        }

        closeButton.action = #selector(closeReader)
        fontSizeUpButton.action = #selector(increaseFontSize)
        fontSizeDownButton.action = #selector(decreaseFontSize)
        bgToggleButton.action = #selector(toggleBackground)

        NSLayoutConstraint.activate([
            readerToolbar.topAnchor.constraint(equalTo: view.topAnchor),
            readerToolbar.leadingAnchor.constraint(equalTo: view.leadingAnchor),
            readerToolbar.trailingAnchor.constraint(equalTo: view.trailingAnchor),
            readerToolbar.heightAnchor.constraint(equalToConstant: 40),

            closeButton.leadingAnchor.constraint(equalTo: readerToolbar.leadingAnchor, constant: 12),
            closeButton.centerYAnchor.constraint(equalTo: readerToolbar.centerYAnchor),

            bgToggleButton.trailingAnchor.constraint(equalTo: readerToolbar.trailingAnchor, constant: -12),
            bgToggleButton.centerYAnchor.constraint(equalTo: readerToolbar.centerYAnchor),

            fontSizeUpButton.trailingAnchor.constraint(equalTo: bgToggleButton.leadingAnchor, constant: -8),
            fontSizeUpButton.centerYAnchor.constraint(equalTo: readerToolbar.centerYAnchor),

            fontSizeDownButton.trailingAnchor.constraint(equalTo: fontSizeUpButton.leadingAnchor, constant: -4),
            fontSizeDownButton.centerYAnchor.constraint(equalTo: readerToolbar.centerYAnchor),
        ])
    }

    private func setupTextView() {
        scrollView.translatesAutoresizingMaskIntoConstraints = false
        scrollView.hasVerticalScroller = true
        scrollView.hasHorizontalScroller = false
        scrollView.drawsBackground = false
        view.addSubview(scrollView)

        textView.isEditable = false
        textView.isSelectable = true
        textView.textContainerInset = NSSize(width: 60, height: 20)
        textView.drawsBackground = false
        textView.isVerticallyResizable = true
        textView.isHorizontallyResizable = false
        textView.autoresizingMask = [.width]
        textView.textContainer?.widthTracksTextView = true

        scrollView.documentView = textView

        NSLayoutConstraint.activate([
            scrollView.topAnchor.constraint(equalTo: readerToolbar.bottomAnchor),
            scrollView.leadingAnchor.constraint(equalTo: view.leadingAnchor),
            scrollView.trailingAnchor.constraint(equalTo: view.trailingAnchor),
            scrollView.bottomAnchor.constraint(equalTo: view.bottomAnchor),
        ])
    }

    func setContent(title: String, body: String) {
        let attributed = NSMutableAttributedString()

        let titleFont = NSFont.systemFont(ofSize: fontSize + 8, weight: .bold)
        let titleAttrs: [NSAttributedString.Key: Any] = [
            .font: titleFont,
            .foregroundColor: currentBackground.textColor,
        ]
        attributed.append(NSAttributedString(string: title + "\n\n", attributes: titleAttrs))

        let bodyFont = NSFont.systemFont(ofSize: fontSize, weight: .regular)
        let paragraphStyle = NSMutableParagraphStyle()
        paragraphStyle.lineSpacing = 8
        let bodyAttrs: [NSAttributedString.Key: Any] = [
            .font: bodyFont,
            .foregroundColor: currentBackground.textColor,
            .paragraphStyle: paragraphStyle,
        ]
        attributed.append(NSAttributedString(string: body, attributes: bodyAttrs))

        textView.textStorage?.setAttributedString(attributed)
    }

    private func refreshColors() {
        view.layer?.backgroundColor = currentBackground.backgroundColor.cgColor
        if let storage = textView.textStorage {
            let range = NSRange(location: 0, length: storage.length)
            storage.addAttribute(.foregroundColor, value: currentBackground.textColor, range: range)
        }
    }

    @objc private func closeReader() {
        onDismiss?()
    }

    @objc private func increaseFontSize() {
        fontSize = min(fontSize + 2, 32)
        refreshFont()
    }

    @objc private func decreaseFontSize() {
        fontSize = max(fontSize - 2, 12)
        refreshFont()
    }

    @objc private func toggleBackground() {
        let next = (currentBackground.rawValue + 1) % 3
        currentBackground = ReaderBackground(rawValue: next) ?? .light
        refreshColors()
    }

    private func refreshFont() {
        guard let storage = textView.textStorage else { return }
        let range = NSRange(location: 0, length: storage.length)
        storage.enumerateAttribute(.font, in: range) { value, attrRange, _ in
            guard let font = value as? NSFont else { return }
            let isBold = font.fontDescriptor.symbolicTraits.contains(.bold)
            let newFont: NSFont
            if isBold {
                newFont = NSFont.systemFont(ofSize: fontSize + 8, weight: .bold)
            } else {
                newFont = NSFont.systemFont(ofSize: fontSize, weight: .regular)
            }
            storage.addAttribute(.font, value: newFont, range: attrRange)
        }
    }
}
