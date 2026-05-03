import AppKit
import WebKit

// MARK: - Inline Edit Action

enum InlineEditAction: String {
    case rewrite = "Rewrite"
    case fixGrammar = "Fix Grammar"
    case shorter = "Shorter"
    case longer = "Longer"
    case toneProfessional = "Professional"
    case toneCasual = "Casual"
    case toneFriendly = "Friendly"
    case toneFormal = "Formal"

    var prompt: String {
        switch self {
        case .rewrite:
            return "Rewrite the following text, improving clarity and flow. Return ONLY the rewritten text.\n\nOriginal: "
        case .fixGrammar:
            return "Fix grammar and spelling in the following text. Return ONLY the corrected text.\n\nOriginal: "
        case .shorter:
            return "Make this text more concise while keeping the meaning. Return ONLY the shortened text.\n\nOriginal: "
        case .longer:
            return "Expand this text with more detail and context. Return ONLY the expanded text.\n\nOriginal: "
        case .toneProfessional:
            return "Rewrite this text in a professional tone. Return ONLY the rewritten text.\n\nOriginal: "
        case .toneCasual:
            return "Rewrite this text in a casual tone. Return ONLY the rewritten text.\n\nOriginal: "
        case .toneFriendly:
            return "Rewrite this text in a friendly tone. Return ONLY the rewritten text.\n\nOriginal: "
        case .toneFormal:
            return "Rewrite this text in a formal tone. Return ONLY the rewritten text.\n\nOriginal: "
        }
    }
}

// MARK: - InlineEditToolbar

final class InlineEditToolbar {
    private var panel: NSPanel?
    private var contentView: NSView?
    private var spinner: NSProgressIndicator?
    private var resultLabel: NSTextField?
    private var acceptButton: NSButton?
    private var rejectButton: NSButton?
    private var buttonStack: NSStackView?
    private var resultStack: NSStackView?

    private weak var webView: WKWebView?
    private var originalText: String = ""
    private var resultText: String = ""
    private var onAction: ((InlineEditAction) -> Void)?

    var isVisible: Bool { panel?.isVisible ?? false }

    func show(relativeTo rect: NSRect, in webView: WKWebView, text: String, action: @escaping (InlineEditAction) -> Void) {
        self.webView = webView
        self.originalText = text
        self.onAction = action

        dismiss()
        buildPanel()

        guard let panel = panel, let window = webView.window else { return }

        let webViewOrigin = webView.convert(NSPoint.zero, to: nil)
        let windowOrigin = window.convertPoint(toScreen: webViewOrigin)
        let screenRect = NSRect(
            x: windowOrigin.x + rect.origin.x,
            y: windowOrigin.y + rect.origin.y + rect.height + 4,
            width: rect.width,
            height: rect.height
        )

        let panelWidth = panel.frame.width
        let panelX = max(screenRect.origin.x, screenRect.origin.x + (screenRect.width - panelWidth) / 2)
        let panelY = screenRect.origin.y

        panel.setFrameOrigin(NSPoint(x: panelX, y: panelY))
        panel.orderFront(nil)
    }

    func dismiss() {
        panel?.orderOut(nil)
        panel = nil
        contentView = nil
    }

    func showLoading() {
        buttonStack?.isHidden = true
        resultStack?.isHidden = false
        resultLabel?.stringValue = "Thinking..."
        spinner?.startAnimation(nil)
        spinner?.isHidden = false
        acceptButton?.isHidden = true
        rejectButton?.isHidden = true
        resizePanel()
    }

    func showResult(_ text: String) {
        resultText = text
        spinner?.stopAnimation(nil)
        spinner?.isHidden = true
        resultLabel?.stringValue = text
        acceptButton?.isHidden = false
        rejectButton?.isHidden = false
        resizePanel()
    }

    func showError(_ message: String) {
        spinner?.stopAnimation(nil)
        spinner?.isHidden = true
        resultLabel?.stringValue = "Error: \(message)"
        acceptButton?.isHidden = true
        rejectButton?.isHidden = false
        rejectButton?.title = "Dismiss"
        resizePanel()
    }

    // MARK: - Build UI

    private func buildPanel() {
        let p = NSPanel(
            contentRect: NSRect(x: 0, y: 0, width: 420, height: 40),
            styleMask: [.borderless, .nonactivatingPanel],
            backing: .buffered,
            defer: false
        )
        p.level = .floating
        p.isOpaque = false
        p.backgroundColor = .clear
        p.hasShadow = true

        let container = NSView(frame: p.contentView!.bounds)
        container.wantsLayer = true
        container.layer?.backgroundColor = MahoAppColors.cardBackground.cgColor
        container.layer?.cornerRadius = 8

        let shadow = NSShadow()
        shadow.shadowBlurRadius = 4
        shadow.shadowOffset = NSSize(width: 0, height: -2)
        shadow.shadowColor = NSColor.black.withAlphaComponent(0.3)
        container.shadow = shadow

        p.contentView?.addSubview(container)
        container.translatesAutoresizingMaskIntoConstraints = false
        NSLayoutConstraint.activate([
            container.leadingAnchor.constraint(equalTo: p.contentView!.leadingAnchor),
            container.trailingAnchor.constraint(equalTo: p.contentView!.trailingAnchor),
            container.topAnchor.constraint(equalTo: p.contentView!.topAnchor),
            container.bottomAnchor.constraint(equalTo: p.contentView!.bottomAnchor),
        ])

        // Action buttons
        let buttons = buildActionButtons()
        let bStack = NSStackView(views: buttons)
        bStack.orientation = .horizontal
        bStack.spacing = 4
        bStack.edgeInsets = NSEdgeInsets(top: 8, left: 8, bottom: 8, right: 8)
        container.addSubview(bStack)
        bStack.translatesAutoresizingMaskIntoConstraints = false
        NSLayoutConstraint.activate([
            bStack.leadingAnchor.constraint(equalTo: container.leadingAnchor),
            bStack.trailingAnchor.constraint(equalTo: container.trailingAnchor),
            bStack.topAnchor.constraint(equalTo: container.topAnchor),
            bStack.bottomAnchor.constraint(equalTo: container.bottomAnchor),
        ])
        self.buttonStack = bStack

        // Result view (hidden initially)
        let rStack = buildResultView()
        container.addSubview(rStack)
        rStack.translatesAutoresizingMaskIntoConstraints = false
        NSLayoutConstraint.activate([
            rStack.leadingAnchor.constraint(equalTo: container.leadingAnchor, constant: 8),
            rStack.trailingAnchor.constraint(equalTo: container.trailingAnchor, constant: -8),
            rStack.topAnchor.constraint(equalTo: container.topAnchor, constant: 8),
            rStack.bottomAnchor.constraint(equalTo: container.bottomAnchor, constant: -8),
        ])
        rStack.isHidden = true
        self.resultStack = rStack

        self.contentView = container
        self.panel = p
    }

    private func buildActionButtons() -> [NSButton] {
        let actions: [(String, Selector)] = [
            ("✨ Rewrite", #selector(rewriteTapped)),
            ("📝 Fix Grammar", #selector(fixGrammarTapped)),
            ("📏 Shorter", #selector(shorterTapped)),
            ("📐 Longer", #selector(longerTapped)),
        ]

        var buttons: [NSButton] = actions.map { title, sel in
            makeButton(title: title, action: sel)
        }

        // Tone button with menu
        let toneButton = makeButton(title: "🎭 Tone", action: nil)
        let menu = NSMenu()
        menu.addItem(withTitle: "Professional", action: #selector(toneProfessionalTapped), keyEquivalent: "")
            .target = self
        menu.addItem(withTitle: "Casual", action: #selector(toneCasualTapped), keyEquivalent: "")
            .target = self
        menu.addItem(withTitle: "Friendly", action: #selector(toneFriendlyTapped), keyEquivalent: "")
            .target = self
        menu.addItem(withTitle: "Formal", action: #selector(toneFormalTapped), keyEquivalent: "")
            .target = self
        toneButton.menu = menu
        toneButton.action = #selector(showToneMenu(_:))
        toneButton.target = self
        buttons.append(toneButton)

        return buttons
    }

    private func makeButton(title: String, action: Selector?) -> NSButton {
        let button = NSButton(title: title, target: self, action: action)
        button.bezelStyle = .inline
        button.isBordered = false
        button.font = NSFont.systemFont(ofSize: 11, weight: .medium)
        button.contentTintColor = MahoAppColors.accentBlueBright
        return button
    }

    private func buildResultView() -> NSStackView {
        let spin = NSProgressIndicator()
        spin.style = .spinning
        spin.controlSize = .small
        spin.isIndeterminate = true
        self.spinner = spin

        let label = NSTextField(wrappingLabelWithString: "")
        label.font = NSFont.systemFont(ofSize: 11)
        label.textColor = MahoAppColors.primaryText
        label.maximumNumberOfLines = 6
        label.preferredMaxLayoutWidth = 350
        self.resultLabel = label

        let accept = NSButton(title: "Accept", target: self, action: #selector(acceptTapped))
        accept.bezelStyle = .inline
        accept.isBordered = false
        accept.font = NSFont.systemFont(ofSize: 11, weight: .semibold)
        accept.contentTintColor = MahoAppColors.accentBlueBright
        self.acceptButton = accept

        let reject = NSButton(title: "Reject", target: self, action: #selector(rejectTapped))
        reject.bezelStyle = .inline
        reject.isBordered = false
        reject.font = NSFont.systemFont(ofSize: 11, weight: .medium)
        reject.contentTintColor = MahoAppColors.secondaryText
        self.rejectButton = reject

        let actionRow = NSStackView(views: [accept, reject])
        actionRow.orientation = .horizontal
        actionRow.spacing = 8

        let stack = NSStackView(views: [spin, label, actionRow])
        stack.orientation = .vertical
        stack.alignment = .leading
        stack.spacing = 6

        return stack
    }

    private func resizePanel() {
        guard let panel = panel, let contentView = contentView else { return }
        contentView.layoutSubtreeIfNeeded()
        let fitting = contentView.fittingSize
        let newWidth = max(fitting.width + 16, 200)
        let newHeight = max(fitting.height + 16, 40)
        var frame = panel.frame
        let oldHeight = frame.height
        frame.size = NSSize(width: newWidth, height: newHeight)
        frame.origin.y -= (newHeight - oldHeight)
        panel.setFrame(frame, display: true, animate: true)
    }

    // MARK: - Actions

    @objc private func rewriteTapped() { onAction?(.rewrite) }
    @objc private func fixGrammarTapped() { onAction?(.fixGrammar) }
    @objc private func shorterTapped() { onAction?(.shorter) }
    @objc private func longerTapped() { onAction?(.longer) }
    @objc private func toneProfessionalTapped() { onAction?(.toneProfessional) }
    @objc private func toneCasualTapped() { onAction?(.toneCasual) }
    @objc private func toneFriendlyTapped() { onAction?(.toneFriendly) }
    @objc private func toneFormalTapped() { onAction?(.toneFormal) }

    @objc private func showToneMenu(_ sender: NSButton) {
        sender.menu?.popUp(positioning: nil, at: NSPoint(x: 0, y: sender.bounds.height), in: sender)
    }

    @objc private func acceptTapped() {
        guard let webView = webView else { dismiss(); return }
        let escaped = resultText
            .replacingOccurrences(of: "\\", with: "\\\\")
            .replacingOccurrences(of: "'", with: "\\'")
            .replacingOccurrences(of: "\n", with: "\\n")
            .replacingOccurrences(of: "\r", with: "\\r")
        webView.evaluateJavaScript("window.__mahoReplaceSelection('\(escaped)')") { _, _ in }
        dismiss()
    }

    @objc private func rejectTapped() {
        dismiss()
    }
}
