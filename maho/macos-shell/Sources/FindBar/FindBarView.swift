import AppKit

final class FindBarView: NSView, CoreUpdateObserver {
    private let searchField = NSSearchField()
    private let matchCountLabel = NSTextField(labelWithString: "")
    private let prevButton = NSButton(title: "◀", target: nil, action: nil)
    private let nextButton = NSButton(title: "▶", target: nil, action: nil)
    private let closeButton = NSButton(title: "×", target: nil, action: nil)

    private var currentTabId: TabId?

    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        setupUI()
        CoreUpdateDispatcher.shared.register(self)
        updateVisibilityFromBridge()
    }

    required init?(coder: NSCoder) {
        super.init(coder: coder)
        setupUI()
        CoreUpdateDispatcher.shared.register(self)
        updateVisibilityFromBridge()
    }

    deinit {
        CoreUpdateDispatcher.shared.unregister(self)
    }

    private func setupUI() {
        wantsLayer = true
        layer?.backgroundColor = MahoAppColors.toolbarBackground.cgColor

        searchField.translatesAutoresizingMaskIntoConstraints = false
        searchField.placeholderString = "Find in page"
        searchField.target = self
        searchField.action = #selector(searchFieldChanged(_:))
        searchField.setAccessibilityLabel("Find in Page")
        addSubview(searchField)

        matchCountLabel.translatesAutoresizingMaskIntoConstraints = false
        matchCountLabel.setAccessibilityLabel("Match Count")
        matchCountLabel.setAccessibilityRole(.staticText)
        matchCountLabel.font = NSFont.systemFont(ofSize: 12)
        matchCountLabel.textColor = MahoAppColors.sidebarSecondaryText
        addSubview(matchCountLabel)

        prevButton.translatesAutoresizingMaskIntoConstraints = false
        prevButton.bezelStyle = .rounded
        prevButton.setButtonType(.momentaryPushIn)
        prevButton.target = self
        prevButton.action = #selector(prevClicked(_:))
        prevButton.setAccessibilityLabel("Find Previous")
        addSubview(prevButton)

        nextButton.translatesAutoresizingMaskIntoConstraints = false
        nextButton.bezelStyle = .rounded
        nextButton.setButtonType(.momentaryPushIn)
        nextButton.target = self
        nextButton.action = #selector(nextClicked(_:))
        nextButton.setAccessibilityLabel("Find Next")
        addSubview(nextButton)

        closeButton.translatesAutoresizingMaskIntoConstraints = false
        closeButton.bezelStyle = .rounded
        closeButton.setButtonType(.momentaryPushIn)
        closeButton.font = NSFont.systemFont(ofSize: 14, weight: .semibold)
        closeButton.target = self
        closeButton.action = #selector(closeClicked(_:))
        closeButton.setAccessibilityLabel("Close Find Bar")
        addSubview(closeButton)

        NSLayoutConstraint.activate([
            searchField.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 12),
            searchField.centerYAnchor.constraint(equalTo: centerYAnchor),
            searchField.widthAnchor.constraint(equalToConstant: 200),

            matchCountLabel.leadingAnchor.constraint(equalTo: searchField.trailingAnchor, constant: 8),
            matchCountLabel.centerYAnchor.constraint(equalTo: centerYAnchor),

            prevButton.leadingAnchor.constraint(equalTo: matchCountLabel.trailingAnchor, constant: 8),
            prevButton.centerYAnchor.constraint(equalTo: centerYAnchor),

            nextButton.leadingAnchor.constraint(equalTo: prevButton.trailingAnchor, constant: 4),
            nextButton.centerYAnchor.constraint(equalTo: centerYAnchor),

            closeButton.leadingAnchor.constraint(equalTo: nextButton.trailingAnchor, constant: 8),
            closeButton.centerYAnchor.constraint(equalTo: centerYAnchor),
            closeButton.trailingAnchor.constraint(lessThanOrEqualTo: trailingAnchor, constant: -12),

            heightAnchor.constraint(equalToConstant: 40),
        ])
    }

    private func updateVisibilityFromBridge() {
        if let state = MahoBridge.shared.getFindBarState() {
            isHidden = !state.isVisible
            searchField.stringValue = state.query
            matchCountLabel.stringValue = state.matchCount > 0 ? "\(state.activeIndex + 1)/\(state.matchCount)" : "0/0"
        } else {
            isHidden = true
        }
    }

    @objc private func searchFieldChanged(_ sender: NSSearchField) {
        guard let tabId = currentTabId else { return }
        _ = MahoBridge.shared.startFind(tabId: tabId, query: sender.stringValue, caseSensitive: false, wholeWord: false)
        if let state = MahoBridge.shared.getFindBarState() {
            matchCountLabel.stringValue = state.matchCount > 0 ? "\(state.activeIndex + 1)/\(state.matchCount)" : "0/0"
        }
    }

    @objc private func prevClicked(_ sender: NSButton) {
        _ = MahoBridge.shared.findPrevious()
        if let state = MahoBridge.shared.getFindBarState() {
            matchCountLabel.stringValue = state.matchCount > 0 ? "\(state.activeIndex + 1)/\(state.matchCount)" : "0/0"
        }
    }

    @objc private func nextClicked(_ sender: NSButton) {
        _ = MahoBridge.shared.findNext()
        if let state = MahoBridge.shared.getFindBarState() {
            matchCountLabel.stringValue = state.matchCount > 0 ? "\(state.activeIndex + 1)/\(state.matchCount)" : "0/0"
        }
    }

    @objc private func closeClicked(_ sender: NSButton) {
        MahoBridge.shared.dismissFind()
        isHidden = true
    }

    override func performKeyEquivalent(with event: NSEvent) -> Bool {
        guard !isHidden else { return super.performKeyEquivalent(with: event) }

        let key = event.charactersIgnoringModifiers ?? ""

        // Escape: close find bar
        if event.keyCode == 53 {
            closeClicked(closeButton)
            return true
        }

        // Enter: find next
        if event.keyCode == 36 && !event.modifierFlags.contains(.shift) {
            nextClicked(nextButton)
            return true
        }

        // Shift+Enter: find previous
        if event.keyCode == 36 && event.modifierFlags.contains(.shift) {
            prevClicked(prevButton)
            return true
        }

        // ⌘G: find next
        if event.modifierFlags.contains(.command) && !event.modifierFlags.contains(.shift) && key == "g" {
            nextClicked(nextButton)
            return true
        }

        // ⇧⌘G: find previous
        if event.modifierFlags.contains(.command) && event.modifierFlags.contains(.shift) && key == "g" {
            prevClicked(prevButton)
            return true
        }

        return super.performKeyEquivalent(with: event)
    }

    func handleCoreUpdate(_ update: CoreUpdate) {
        switch update {
        case .showFindBar(let tabId):
            currentTabId = tabId
            isHidden = false
            updateVisibilityFromBridge()
        default:
            break
        }
    }
}
