import AppKit
import Foundation
import WebKit

final class AIChatView: NSView {
    private var transcriptRows: [ChatTranscriptRowView] {
        stackView.arrangedSubviews.compactMap { $0 as? ChatTranscriptRowView }
    }

    private let scrollView = NSScrollView()
    private let contentView = NSView()
    private let stackView = NSStackView()
    private let emptyStateContainer = NSView()
    private let emptyStateContentStack = NSStackView()
    private let emptyStateStack = NSStackView()
    private let emptyStateTitleLabel = NSTextField(labelWithString: "Ask a question about this page...")
    private let emptyStateDetailLabel = NSTextField(labelWithString: "")
    private var needsScrollToLatestMessage = false
    private var renderedMessageCount = 0

    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        setupUI()
    }

    required init?(coder: NSCoder) { fatalError() }

    override func viewDidChangeEffectiveAppearance() {
        super.viewDidChangeEffectiveAppearance()
        applyTheme()
    }

    func reloadMessages(_ messages: [ChatMessage]) {
        let hasMessages = !messages.isEmpty
        emptyStateContainer.isHidden = hasMessages
        scrollView.isHidden = !hasMessages

        guard hasMessages else {
            resetTranscript()
            return
        }

        if renderedMessageCount == 0 || messages.count < renderedMessageCount || stackView.arrangedSubviews.count != renderedMessageCount {
            rebuildTranscript(with: messages)
        } else if messages.count > renderedMessageCount {
            appendTranscriptRows(with: messages, startingAt: renderedMessageCount)
        } else if let latestMessage = messages.last,
                  let lastRow = stackView.arrangedSubviews.last as? ChatTranscriptRowView {
            lastRow.configure(with: TranscriptMessageSnapshot(message: latestMessage))
        } else {
            rebuildTranscript(with: messages)
        }

        renderedMessageCount = messages.count
        needsScrollToLatestMessage = true
        needsLayout = true
    }

    func updateSuggestions(hasPageContext: Bool) {
        _ = hasPageContext
    }

    private func applyTheme() {
        layer?.backgroundColor = NSColor.clear.cgColor
        contentView.layer?.backgroundColor = NSColor.clear.cgColor
        emptyStateContainer.layer?.backgroundColor = NSColor.clear.cgColor
        emptyStateTitleLabel.textColor = AIDiaPalette.composerPlaceholderText
        emptyStateDetailLabel.textColor = AIDiaPalette.secondaryText
        transcriptRows.forEach { $0.applyTheme() }
    }

    private func resetTranscript() {
        clearTranscriptRows()
        renderedMessageCount = 0
        needsScrollToLatestMessage = false
        needsLayout = true
    }

    private func rebuildTranscript(with messages: [ChatMessage]) {
        clearTranscriptRows()
        for message in messages {
            appendRow(for: TranscriptMessageSnapshot(message: message))
        }
    }

    private func appendTranscriptRows(with messages: [ChatMessage], startingAt index: Int) {
        guard index < messages.count else { return }
        for message in messages[index...] {
            appendRow(for: TranscriptMessageSnapshot(message: message))
        }
    }

    private func appendRow(for snapshot: TranscriptMessageSnapshot) {
        let rowView = ChatTranscriptRowView(frame: .zero)
        rowView.translatesAutoresizingMaskIntoConstraints = false
        rowView.owningChatView = self
        rowView.configure(with: snapshot)
        stackView.addArrangedSubview(rowView)
        rowView.widthAnchor.constraint(equalTo: stackView.widthAnchor).isActive = true
    }

    private func clearTranscriptRows() {
        stackView.arrangedSubviews.forEach { view in
            stackView.removeArrangedSubview(view)
            view.removeFromSuperview()
        }
    }

    private func setupUI() {
        wantsLayer = true
        setAccessibilityIdentifier("ai.panel.chat")
        layer?.backgroundColor = NSColor.clear.cgColor

        stackView.translatesAutoresizingMaskIntoConstraints = false
        stackView.orientation = .vertical
        stackView.alignment = .leading
        stackView.spacing = 22

        contentView.translatesAutoresizingMaskIntoConstraints = false
        contentView.wantsLayer = true
        contentView.layer?.backgroundColor = NSColor.clear.cgColor
        contentView.addSubview(stackView)

        scrollView.translatesAutoresizingMaskIntoConstraints = false
        scrollView.setAccessibilityIdentifier("ai.panel.chat.transcript")
        scrollView.drawsBackground = false
        scrollView.borderType = .noBorder
        scrollView.hasVerticalScroller = true
        scrollView.autohidesScrollers = true
        scrollView.scrollerStyle = .overlay
        scrollView.documentView = contentView
        addSubview(scrollView)

        emptyStateContainer.translatesAutoresizingMaskIntoConstraints = false
        emptyStateContainer.wantsLayer = true
        emptyStateContainer.layer?.backgroundColor = NSColor.clear.cgColor
        addSubview(emptyStateContainer)

        emptyStateContentStack.translatesAutoresizingMaskIntoConstraints = false
        emptyStateContentStack.orientation = .vertical
        emptyStateContentStack.alignment = .leading
        emptyStateContentStack.spacing = 10
        emptyStateContainer.addSubview(emptyStateContentStack)

        emptyStateStack.translatesAutoresizingMaskIntoConstraints = false
        emptyStateStack.setAccessibilityIdentifier("ai.panel.chat.emptyState")
        emptyStateStack.orientation = .vertical
        emptyStateStack.alignment = .leading
        emptyStateStack.spacing = 10
        emptyStateContentStack.addArrangedSubview(emptyStateStack)

        emptyStateTitleLabel.setAccessibilityIdentifier("ai.panel.chat.emptyState.title")
        emptyStateTitleLabel.font = .systemFont(ofSize: 24, weight: .medium)
        emptyStateTitleLabel.maximumNumberOfLines = 0
        emptyStateTitleLabel.lineBreakMode = .byWordWrapping
        emptyStateTitleLabel.isHidden = true
        emptyStateStack.addArrangedSubview(emptyStateTitleLabel)

        emptyStateDetailLabel.setAccessibilityIdentifier("ai.panel.chat.emptyState.detail")
        emptyStateDetailLabel.font = .systemFont(ofSize: 13, weight: .regular)
        emptyStateDetailLabel.maximumNumberOfLines = 0
        emptyStateDetailLabel.lineBreakMode = .byWordWrapping
        emptyStateDetailLabel.isHidden = true
        emptyStateStack.addArrangedSubview(emptyStateDetailLabel)

        NSLayoutConstraint.activate([
            scrollView.topAnchor.constraint(equalTo: topAnchor),
            scrollView.leadingAnchor.constraint(equalTo: leadingAnchor),
            scrollView.trailingAnchor.constraint(equalTo: trailingAnchor),
            scrollView.bottomAnchor.constraint(equalTo: bottomAnchor),

            contentView.widthAnchor.constraint(equalTo: scrollView.contentView.widthAnchor),
            contentView.topAnchor.constraint(equalTo: scrollView.contentView.topAnchor),
            contentView.leadingAnchor.constraint(equalTo: scrollView.contentView.leadingAnchor),
            contentView.trailingAnchor.constraint(equalTo: scrollView.contentView.trailingAnchor),
            contentView.bottomAnchor.constraint(greaterThanOrEqualTo: scrollView.contentView.bottomAnchor),

            stackView.topAnchor.constraint(equalTo: contentView.topAnchor, constant: 6),
            stackView.leadingAnchor.constraint(equalTo: contentView.leadingAnchor),
            stackView.trailingAnchor.constraint(equalTo: contentView.trailingAnchor),
            stackView.bottomAnchor.constraint(equalTo: contentView.bottomAnchor, constant: -12),

            emptyStateContainer.topAnchor.constraint(equalTo: topAnchor),
            emptyStateContainer.leadingAnchor.constraint(equalTo: leadingAnchor),
            emptyStateContainer.trailingAnchor.constraint(equalTo: trailingAnchor),
            emptyStateContainer.bottomAnchor.constraint(equalTo: bottomAnchor),

            emptyStateContentStack.leadingAnchor.constraint(equalTo: emptyStateContainer.leadingAnchor, constant: 2),
            emptyStateContentStack.trailingAnchor.constraint(lessThanOrEqualTo: emptyStateContainer.trailingAnchor),
            emptyStateContentStack.bottomAnchor.constraint(equalTo: emptyStateContainer.bottomAnchor, constant: -18),

            emptyStateStack.trailingAnchor.constraint(lessThanOrEqualTo: emptyStateContentStack.trailingAnchor, constant: -24),
        ])

        applyTheme()
        reloadMessages([])
    }

    override func layout() {
        super.layout()

        guard needsScrollToLatestMessage, !scrollView.isHidden else { return }
        needsScrollToLatestMessage = false
        scrollToLatestMessage()
    }

    private func scrollToLatestMessage() {
        layoutSubtreeIfNeeded()
        let maxY = max(0, contentView.bounds.height - scrollView.contentView.bounds.height)
        scrollView.contentView.scroll(to: NSPoint(x: 0, y: maxY))
        scrollView.reflectScrolledClipView(scrollView.contentView)
    }
}

private struct TranscriptMessageSnapshot {
    let isUser: Bool
    let content: String
    let timestamp: Date

    init(message: ChatMessage) {
        self.isUser = message.role == .user
        self.content = message.content
        self.timestamp = message.timestamp
    }
}

private enum TranscriptSegment {
    case paragraph(String)
    case code(String)
}

private final class ChatTranscriptRowView: NSView, WKNavigationDelegate {
    private enum LayoutMetrics {
        static let bubbleSideInset: CGFloat = 18
        static let userAlignmentInset: CGFloat = 38
        static let bubbleMinimumWidth: CGFloat = 96
        static let messageVerticalInset: CGFloat = 14
        static let messageHorizontalInset: CGFloat = 16
        static let assistantBubbleCornerRadius: CGFloat = 22
    }

    private var currentSnapshot: TranscriptMessageSnapshot?

    private let headerStack = NSStackView()
    private let roleLabel = NSTextField(labelWithString: "")
    private let timestampLabel = NSTextField(labelWithString: "")
    private let bubbleView = NSView()
    private let messageStack = NSStackView()

    private var webView: WKWebView?
    private var webViewHeightConstraint: NSLayoutConstraint?
    private var thinkingLabel: NSTextField?
    private var currentIsUser = false
    weak var owningChatView: AIChatView?

    private var pendingContent: String?
    private var pendingUpdateWorkItem: DispatchWorkItem?

    private var bubbleLeadingConstraint: NSLayoutConstraint?
    private var bubbleTrailingConstraint: NSLayoutConstraint?
    private var bubbleLeadingLimitConstraint: NSLayoutConstraint?
    private var bubbleTrailingLimitConstraint: NSLayoutConstraint?
    private var bubbleMaximumWidthConstraint: NSLayoutConstraint?
    private var bubbleMinimumWidthConstraint: NSLayoutConstraint?
    private var headerLeadingConstraint: NSLayoutConstraint?
    private var headerTrailingConstraint: NSLayoutConstraint?

    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        setupUI()
    }

    required init?(coder: NSCoder) { fatalError() }

    override func viewDidChangeEffectiveAppearance() {
        super.viewDidChangeEffectiveAppearance()
        applyTheme()
    }

    override func removeFromSuperview() {
        pendingUpdateWorkItem?.cancel()
        pendingUpdateWorkItem = nil
        webView?.stopLoading()
        webView?.navigationDelegate = nil
        super.removeFromSuperview()
    }

    func configure(with message: TranscriptMessageSnapshot) {
        currentSnapshot = message
        let isUser = message.isUser
        currentIsUser = isUser
        let trimmedContent = message.content.trimmingCharacters(in: .whitespacesAndNewlines)

        roleLabel.stringValue = isUser ? "YOU" : "MAHO AI"
        roleLabel.textColor = isUser ? AIDiaPalette.secondaryText : AIDiaPalette.mutedText
        timestampLabel.stringValue = Self.timestampFormatter.string(from: message.timestamp)
        timestampLabel.textColor = AIDiaPalette.mutedText

        if isUser {
            webView?.isHidden = true
            thinkingLabel?.isHidden = true
            messageStack.isHidden = false

            messageStack.arrangedSubviews.forEach { view in
                messageStack.removeArrangedSubview(view)
                view.removeFromSuperview()
            }

            let displayContent = trimmedContent.isEmpty ? "…" : message.content
            for segment in Self.makeSegments(from: displayContent, isUser: true) {
                switch segment {
                case .paragraph(let text):
                    let label = NSTextField(wrappingLabelWithString: text)
                    label.translatesAutoresizingMaskIntoConstraints = false
                    label.font = .systemFont(ofSize: 13, weight: .regular)
                    label.textColor = AIDiaPalette.bodyText
                    label.lineBreakMode = .byWordWrapping
                    label.maximumNumberOfLines = 0
                    messageStack.addArrangedSubview(label)
                case .code(let code):
                    let codeView = CodeBlockView(code: code)
                    codeView.translatesAutoresizingMaskIntoConstraints = false
                    messageStack.addArrangedSubview(codeView)
                    codeView.widthAnchor.constraint(equalTo: messageStack.widthAnchor).isActive = true
                }
            }
        } else {
            messageStack.isHidden = true

            if trimmedContent.isEmpty {
                webView?.isHidden = true
                if thinkingLabel == nil {
                    let label = NSTextField(labelWithString: "Thinking…")
                    label.translatesAutoresizingMaskIntoConstraints = false
                    label.font = .systemFont(ofSize: 13, weight: .regular)
                    label.textColor = AIDiaPalette.secondaryText
                    bubbleView.addSubview(label)
                    NSLayoutConstraint.activate([
                        label.topAnchor.constraint(equalTo: bubbleView.topAnchor, constant: LayoutMetrics.messageVerticalInset),
                        label.leadingAnchor.constraint(equalTo: bubbleView.leadingAnchor, constant: LayoutMetrics.messageHorizontalInset),
                        label.trailingAnchor.constraint(lessThanOrEqualTo: bubbleView.trailingAnchor, constant: -LayoutMetrics.messageHorizontalInset),
                        label.bottomAnchor.constraint(equalTo: bubbleView.bottomAnchor, constant: -LayoutMetrics.messageVerticalInset),
                    ])
                    thinkingLabel = label
                }
                thinkingLabel?.isHidden = false
            } else {
                thinkingLabel?.isHidden = true
                let wv = getOrCreateWebView()
                wv.isHidden = false
                let html = Self.markdownToHTML(message.content, appearance: effectiveAppearance)
                pendingContent = html
                pendingUpdateWorkItem?.cancel()
                let workItem = DispatchWorkItem { [weak self] in
                    guard let self, let content = self.pendingContent else { return }
                    self.pendingContent = nil
                    self.webView?.loadHTMLString(content, baseURL: nil)
                }
                pendingUpdateWorkItem = workItem
                DispatchQueue.main.asyncAfter(deadline: .now() + 0.25, execute: workItem)
            }
        }

        bubbleView.layer?.backgroundColor = isUser
            ? AIDiaPalette.userBubbleBackground.cgColor
            : AIDiaPalette.assistantBubbleBackground.cgColor
        bubbleView.layer?.borderColor = isUser
            ? AIDiaPalette.userBubbleBorder.cgColor
            : AIDiaPalette.assistantBubbleBorder.cgColor
        bubbleView.layer?.borderWidth = 1
        bubbleView.layer?.cornerRadius = isUser ? 20 : LayoutMetrics.assistantBubbleCornerRadius
        bubbleView.layer?.shadowColor = AIDiaPalette.shadow.cgColor
        bubbleView.layer?.shadowOpacity = isUser ? 0.08 : 0.12
        bubbleView.layer?.shadowRadius = 12
        bubbleView.layer?.shadowOffset = CGSize(width: 0, height: -2)

        bubbleLeadingConstraint?.isActive = !isUser
        bubbleTrailingLimitConstraint?.isActive = !isUser
        bubbleLeadingLimitConstraint?.isActive = isUser
        bubbleTrailingConstraint?.isActive = isUser
        headerLeadingConstraint?.isActive = !isUser
        headerTrailingConstraint?.isActive = isUser
    }

    func applyTheme() {
        guard let snapshot = currentSnapshot else { return }

        roleLabel.textColor = snapshot.isUser ? AIDiaPalette.secondaryText : AIDiaPalette.mutedText
        timestampLabel.textColor = AIDiaPalette.mutedText

        bubbleView.layer?.backgroundColor = snapshot.isUser
            ? AIDiaPalette.userBubbleBackground.cgColor
            : AIDiaPalette.assistantBubbleBackground.cgColor
        bubbleView.layer?.borderColor = snapshot.isUser
            ? AIDiaPalette.userBubbleBorder.cgColor
            : AIDiaPalette.assistantBubbleBorder.cgColor

        for case let label as NSTextField in messageStack.arrangedSubviews {
            label.textColor = AIDiaPalette.bodyText
        }
        thinkingLabel?.textColor = AIDiaPalette.secondaryText
        webView?.loadHTMLString(Self.markdownToHTML(snapshot.content, appearance: effectiveAppearance), baseURL: nil)
    }

    private func getOrCreateWebView() -> WKWebView {
        if let existing = webView { return existing }

        let config = WKWebViewConfiguration()
        config.suppressesIncrementalRendering = true
        config.defaultWebpagePreferences.allowsContentJavaScript = false
        let wv = WKWebView(frame: .zero, configuration: config)
        wv.translatesAutoresizingMaskIntoConstraints = false
        wv.navigationDelegate = self
        wv.underPageBackgroundColor = .clear

        bubbleView.addSubview(wv)
        let heightConstraint = wv.heightAnchor.constraint(equalToConstant: 20)
        heightConstraint.priority = .defaultHigh
        NSLayoutConstraint.activate([
            wv.topAnchor.constraint(equalTo: bubbleView.topAnchor),
            wv.leadingAnchor.constraint(equalTo: bubbleView.leadingAnchor),
            wv.trailingAnchor.constraint(equalTo: bubbleView.trailingAnchor),
            wv.bottomAnchor.constraint(equalTo: bubbleView.bottomAnchor),
            heightConstraint,
        ])
        webView = wv
        webViewHeightConstraint = heightConstraint
        return wv
    }

    func webView(_ webView: WKWebView, didFinish navigation: WKNavigation!) {
        webView.evaluateJavaScript("document.body.scrollHeight") { [weak self] result, _ in
            guard let height = result as? CGFloat, height > 0 else { return }
            self?.webViewHeightConstraint?.constant = height
            self?.invalidateIntrinsicContentSize()
            self?.owningChatView?.needsLayout = true
        }
    }

    func webView(_ webView: WKWebView,
                 decidePolicyFor navigationAction: WKNavigationAction,
                 decisionHandler: @escaping (WKNavigationActionPolicy) -> Void) {
        if navigationAction.navigationType == .linkActivated, let url = navigationAction.request.url {
            NSWorkspace.shared.open(url)
            decisionHandler(.cancel)
        } else {
            decisionHandler(.allow)
        }
    }

    private func setupUI() {
        translatesAutoresizingMaskIntoConstraints = false

        headerStack.translatesAutoresizingMaskIntoConstraints = false
        headerStack.orientation = .horizontal
        headerStack.alignment = .centerY
        headerStack.spacing = 6
        addSubview(headerStack)

        roleLabel.font = .systemFont(ofSize: 10, weight: .semibold)
        headerStack.addArrangedSubview(roleLabel)

        timestampLabel.font = .monospacedDigitSystemFont(ofSize: 10, weight: .regular)
        headerStack.addArrangedSubview(timestampLabel)

        bubbleView.translatesAutoresizingMaskIntoConstraints = false
        bubbleView.wantsLayer = true
        addSubview(bubbleView)

        messageStack.translatesAutoresizingMaskIntoConstraints = false
        messageStack.orientation = .vertical
        messageStack.alignment = .leading
        messageStack.spacing = 10
        bubbleView.addSubview(messageStack)

        bubbleLeadingConstraint = bubbleView.leadingAnchor.constraint(equalTo: leadingAnchor)
        bubbleTrailingConstraint = bubbleView.trailingAnchor.constraint(equalTo: trailingAnchor)
        bubbleLeadingLimitConstraint = bubbleView.leadingAnchor.constraint(greaterThanOrEqualTo: leadingAnchor, constant: LayoutMetrics.userAlignmentInset)
        bubbleTrailingLimitConstraint = bubbleView.trailingAnchor.constraint(lessThanOrEqualTo: trailingAnchor, constant: -LayoutMetrics.userAlignmentInset)
        let initialAvailableWidth = max(1, bounds.width > 0 ? bounds.width - (LayoutMetrics.bubbleSideInset * 2) : 304)
        bubbleMaximumWidthConstraint = bubbleView.widthAnchor.constraint(lessThanOrEqualToConstant: initialAvailableWidth)
        bubbleMinimumWidthConstraint = bubbleView.widthAnchor.constraint(greaterThanOrEqualToConstant: LayoutMetrics.bubbleMinimumWidth)
        headerLeadingConstraint = headerStack.leadingAnchor.constraint(equalTo: bubbleView.leadingAnchor)
        headerTrailingConstraint = headerStack.trailingAnchor.constraint(equalTo: bubbleView.trailingAnchor)

        bubbleLeadingConstraint?.isActive = false
        bubbleTrailingConstraint?.isActive = false
        bubbleLeadingLimitConstraint?.isActive = false
        bubbleTrailingLimitConstraint?.isActive = false
        headerLeadingConstraint?.isActive = false
        headerTrailingConstraint?.isActive = false

        NSLayoutConstraint.activate([
            headerStack.topAnchor.constraint(equalTo: topAnchor),
            headerLeadingConstraint!,
            headerStack.trailingAnchor.constraint(lessThanOrEqualTo: trailingAnchor),

            bubbleView.topAnchor.constraint(equalTo: headerStack.bottomAnchor, constant: 8),
            bubbleView.bottomAnchor.constraint(equalTo: bottomAnchor),
            bubbleMaximumWidthConstraint!,
            bubbleMinimumWidthConstraint!,

            messageStack.topAnchor.constraint(equalTo: bubbleView.topAnchor, constant: LayoutMetrics.messageVerticalInset),
            messageStack.leadingAnchor.constraint(equalTo: bubbleView.leadingAnchor, constant: LayoutMetrics.messageHorizontalInset),
            messageStack.trailingAnchor.constraint(equalTo: bubbleView.trailingAnchor, constant: -LayoutMetrics.messageHorizontalInset),
            messageStack.bottomAnchor.constraint(equalTo: bubbleView.bottomAnchor, constant: -LayoutMetrics.messageVerticalInset),
        ])
    }

    override func layout() {
        super.layout()

        let availableWidth = max(1, bounds.width - (LayoutMetrics.bubbleSideInset * 2))
        bubbleMaximumWidthConstraint?.constant = availableWidth
        bubbleMinimumWidthConstraint?.constant = min(LayoutMetrics.bubbleMinimumWidth, availableWidth)
    }

    private static func markdownToHTML(_ markdown: String, appearance: NSAppearance) -> String {
        var lines = markdown.components(separatedBy: "\n")
        var htmlParts: [String] = []
        var i = 0

        while i < lines.count {
            let line = lines[i]

            if line.hasPrefix("```") {
                var codeLines: [String] = []
                i += 1
                while i < lines.count && !lines[i].hasPrefix("```") {
                    codeLines.append(escapeHTML(lines[i]))
                    i += 1
                }
                i += 1
                htmlParts.append("<pre><code>\(codeLines.joined(separator: "\n"))</code></pre>")
                continue
            }

            if line.hasPrefix("### ") {
                htmlParts.append("<h3>\(inlineMarkdown(escapeHTML(String(line.dropFirst(4)))))</h3>")
                i += 1; continue
            }
            if line.hasPrefix("## ") {
                htmlParts.append("<h2>\(inlineMarkdown(escapeHTML(String(line.dropFirst(3)))))</h2>")
                i += 1; continue
            }
            if line.hasPrefix("# ") {
                htmlParts.append("<h1>\(inlineMarkdown(escapeHTML(String(line.dropFirst(2)))))</h1>")
                i += 1; continue
            }

            if line.hasPrefix("- ") || line.hasPrefix("* ") {
                var items: [String] = []
                while i < lines.count && (lines[i].hasPrefix("- ") || lines[i].hasPrefix("* ")) {
                    items.append("<li>\(inlineMarkdown(escapeHTML(String(lines[i].dropFirst(2)))))</li>")
                    i += 1
                }
                htmlParts.append("<ul>\(items.joined())</ul>")
                continue
            }

            if let _ = line.range(of: #"^\d+\. "#, options: .regularExpression) {
                var items: [String] = []
                while i < lines.count, let r = lines[i].range(of: #"^\d+\. "#, options: .regularExpression) {
                    items.append("<li>\(inlineMarkdown(escapeHTML(String(lines[i][r.upperBound...]))))</li>")
                    i += 1
                }
                htmlParts.append("<ol>\(items.joined())</ol>")
                continue
            }

            if line.trimmingCharacters(in: .whitespaces).isEmpty {
                i += 1; continue
            }

            htmlParts.append("<p>\(inlineMarkdown(escapeHTML(line)))</p>")
            i += 1
        }

        let bodyColor = cssRGBAString(for: AIDiaPalette.bodyText, appearance: appearance)
        let headingColor = cssRGBAString(for: AIDiaPalette.titleText, appearance: appearance)
        let codeBackgroundColor = cssRGBAString(for: AIDiaPalette.codeBackground, appearance: appearance)
        let codeBorderColor = cssRGBAString(for: AIDiaPalette.codeBorder, appearance: appearance)
        let linkColor = cssRGBAString(for: AIDiaPalette.bodyText, appearance: appearance)

        return """
        <!DOCTYPE html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">
        <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body { font: 13px/1.55 -apple-system, BlinkMacSystemFont, sans-serif; color: \(bodyColor); background: transparent; -webkit-text-size-adjust: none; }
        h1, h2, h3 { color: \(headingColor); font-weight: 600; margin: 12px 0 4px 0; }
        h1 { font-size: 18px; } h2 { font-size: 16px; } h3 { font-size: 14px; }
        h1:first-child, h2:first-child, h3:first-child { margin-top: 0; }
        p { margin: 7px 0; }
        p:first-child { margin-top: 0; }
        p:last-child { margin-bottom: 0; }
        code { background: \(codeBackgroundColor); border: 1px solid \(codeBorderColor); border-radius: 5px; padding: 1px 4px; font-size: 12px; font-family: ui-monospace, Menlo, monospace; }
        pre { background: \(codeBackgroundColor); border: 1px solid \(codeBorderColor); border-radius: 12px; padding: 12px; overflow-x: auto; margin: 10px 0; }
        pre code { background: none; border: none; padding: 0; }
        a { color: \(linkColor); text-decoration: underline; text-underline-offset: 2px; }
        ul, ol { padding-left: 20px; margin: 6px 0; }
        li { margin: 2px 0; }
        strong { font-weight: 600; }
        </style></head><body>\(htmlParts.joined())</body></html>
        """
    }

    private static func cssRGBAString(for color: NSColor, appearance: NSAppearance) -> String {
        var css = "rgba(0, 0, 0, 1.000)"
        appearance.performAsCurrentDrawingAppearance {
            let resolved = color.usingColorSpace(.deviceRGB) ?? color
            let red = Int(round(resolved.redComponent * 255))
            let green = Int(round(resolved.greenComponent * 255))
            let blue = Int(round(resolved.blueComponent * 255))
            let alpha = String(format: "%.3f", resolved.alphaComponent)
            css = "rgba(\(red), \(green), \(blue), \(alpha))"
        }
        return css
    }

    private static func escapeHTML(_ text: String) -> String {
        text.replacingOccurrences(of: "&", with: "&amp;")
            .replacingOccurrences(of: "<", with: "&lt;")
            .replacingOccurrences(of: ">", with: "&gt;")
            .replacingOccurrences(of: "\"", with: "&quot;")
    }

    private static func inlineMarkdown(_ text: String) -> String {
        var result = text
        while let range = result.range(of: #"\*\*(.+?)\*\*"#, options: .regularExpression) {
            let match = String(result[range])
            let inner = String(match.dropFirst(2).dropLast(2))
            result.replaceSubrange(range, with: "<strong>\(inner)</strong>")
        }
        while let range = result.range(of: #"(?<!\*)\*(?!\*)(.+?)(?<!\*)\*(?!\*)"#, options: .regularExpression) {
            let match = String(result[range])
            let inner = String(match.dropFirst(1).dropLast(1))
            result.replaceSubrange(range, with: "<em>\(inner)</em>")
        }
        while let range = result.range(of: #"`([^`]+)`"#, options: .regularExpression) {
            let match = String(result[range])
            let inner = String(match.dropFirst(1).dropLast(1))
            result.replaceSubrange(range, with: "<code>\(inner)</code>")
        }
        while let range = result.range(of: #"\[([^\]]+)\]\(([^)]+)\)"#, options: .regularExpression) {
            let match = String(result[range])
            if let textRange = match.range(of: #"(?<=\[)[^\]]+(?=\])"#, options: .regularExpression),
               let urlRange = match.range(of: #"(?<=\()[^)]+(?=\))"#, options: .regularExpression) {
                let linkText = String(match[textRange])
                let linkUrl = String(match[urlRange])
                result.replaceSubrange(range, with: "<a href=\"\(linkUrl)\">\(linkText)</a>")
            } else {
                break
            }
        }
        return result
    }

    private static func makeSegments(from content: String, isUser: Bool) -> [TranscriptSegment] {
        let normalized = content.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !normalized.isEmpty else { return [.paragraph(isUser ? "…" : "Thinking…")] }

        let parts = normalized.components(separatedBy: "```")
        var segments: [TranscriptSegment] = []

        for (index, part) in parts.enumerated() {
            if index.isMultiple(of: 2) {
                let paragraphs = part
                    .components(separatedBy: "\n\n")
                    .map { $0.trimmingCharacters(in: .whitespacesAndNewlines) }
                    .filter { !$0.isEmpty }

                for paragraph in paragraphs {
                    segments.append(.paragraph(paragraph))
                }
            } else {
                var code = part.trimmingCharacters(in: .whitespacesAndNewlines)
                if let newlineIndex = code.firstIndex(of: "\n") {
                    let firstLine = code[..<newlineIndex]
                    if !firstLine.contains(" ") {
                        code = String(code[code.index(after: newlineIndex)...])
                    }
                }
                if !code.isEmpty {
                    segments.append(.code(code))
                }
            }
        }

        return segments.isEmpty ? [.paragraph(normalized)] : segments
    }

    private static let timestampFormatter: DateFormatter = {
        let formatter = DateFormatter()
        formatter.dateStyle = .none
        formatter.timeStyle = .short
        return formatter
    }()
}

private final class CodeBlockView: NSView {
    private let code: String

    private let containerView = NSView()
    private let textView = NSTextView()
    private let scrollView = NSScrollView()

    init(code: String) {
        self.code = code
        super.init(frame: .zero)
        setupUI(code: code)
    }

    required init?(coder: NSCoder) { fatalError() }

    override func viewDidChangeEffectiveAppearance() {
        super.viewDidChangeEffectiveAppearance()
        applyTheme()
    }

    private func applyTheme() {
        containerView.layer?.backgroundColor = AIDiaPalette.codeBackground.cgColor
        containerView.layer?.borderColor = AIDiaPalette.codeBorder.cgColor
        textView.textColor = AIDiaPalette.bodyText
        textView.string = code
    }

    private func setupUI(code: String) {
        translatesAutoresizingMaskIntoConstraints = false

        containerView.translatesAutoresizingMaskIntoConstraints = false
        containerView.wantsLayer = true
        containerView.layer?.backgroundColor = AIDiaPalette.codeBackground.cgColor
        containerView.layer?.borderColor = AIDiaPalette.codeBorder.cgColor
        containerView.layer?.borderWidth = 1
        containerView.layer?.cornerRadius = 12
        addSubview(containerView)

        scrollView.translatesAutoresizingMaskIntoConstraints = false
        scrollView.drawsBackground = false
        scrollView.borderType = .noBorder
        scrollView.hasVerticalScroller = false
        scrollView.hasHorizontalScroller = false
        containerView.addSubview(scrollView)

        textView.isEditable = false
        textView.isSelectable = true
        textView.drawsBackground = false
        textView.font = .monospacedSystemFont(ofSize: 12, weight: .regular)
        textView.textColor = AIDiaPalette.bodyText
        textView.textContainerInset = NSSize(width: 2, height: 6)
        textView.string = code
        textView.isVerticallyResizable = true
        textView.isHorizontallyResizable = false
        textView.textContainer?.widthTracksTextView = true
        scrollView.documentView = textView

        NSLayoutConstraint.activate([
            containerView.topAnchor.constraint(equalTo: topAnchor),
            containerView.leadingAnchor.constraint(equalTo: leadingAnchor),
            containerView.trailingAnchor.constraint(equalTo: trailingAnchor),
            containerView.bottomAnchor.constraint(equalTo: bottomAnchor),

            scrollView.topAnchor.constraint(equalTo: containerView.topAnchor, constant: 6),
            scrollView.leadingAnchor.constraint(equalTo: containerView.leadingAnchor, constant: 8),
            scrollView.trailingAnchor.constraint(equalTo: containerView.trailingAnchor, constant: -8),
            scrollView.bottomAnchor.constraint(equalTo: containerView.bottomAnchor, constant: -6),
            scrollView.heightAnchor.constraint(greaterThanOrEqualToConstant: 36),
        ])

        applyTheme()
    }
}
