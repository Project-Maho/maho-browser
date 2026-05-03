import AppKit

final class RecoveryPhraseController: NSViewController {

    private var phraseWords: [String] = []
    private var savedCheckbox = NSButton()
    private var continueButton = NSButton()

    override func loadView() {
        let container = NSView(frame: NSRect(x: 0, y: 0, width: 480, height: 440))
        container.wantsLayer = true

        let stack = NSStackView()
        stack.orientation = .vertical
        stack.alignment = .centerX
        stack.spacing = 16
        stack.translatesAutoresizingMaskIntoConstraints = false
        container.addSubview(stack)
        NSLayoutConstraint.activate([
            stack.topAnchor.constraint(equalTo: container.topAnchor, constant: 24),
            stack.leadingAnchor.constraint(equalTo: container.leadingAnchor, constant: 24),
            stack.trailingAnchor.constraint(equalTo: container.trailingAnchor, constant: -24),
            stack.bottomAnchor.constraint(lessThanOrEqualTo: container.bottomAnchor, constant: -24),
        ])

        let titleLabel = NSTextField(labelWithString: "Recovery Phrase")
        titleLabel.font = .systemFont(ofSize: 18, weight: .bold)
        titleLabel.textColor = .labelColor
        stack.addArrangedSubview(titleLabel)

        let warningLabel = NSTextField(wrappingLabelWithString: "Store this phrase safely. You'll need it to recover your data.")
        warningLabel.font = .systemFont(ofSize: 12)
        warningLabel.textColor = .systemOrange
        warningLabel.alignment = .center
        warningLabel.translatesAutoresizingMaskIntoConstraints = false
        warningLabel.widthAnchor.constraint(lessThanOrEqualToConstant: 400).isActive = true
        stack.addArrangedSubview(warningLabel)

        // Generate phrase
        if let key = MahoBridge.shared.generateSyncKey() {
            phraseWords = key.split(separator: " ").map(String.init)
        }
        if phraseWords.isEmpty {
            phraseWords = ["unable", "to", "generate", "key"]
        }

        // Word grid - 4 columns
        let gridContainer = NSView()
        gridContainer.translatesAutoresizingMaskIntoConstraints = false

        let columns = 4
        let rows = (phraseWords.count + columns - 1) / columns
        var wordViews: [NSView] = []

        for (index, word) in phraseWords.enumerated() {
            let wordView = makeWordView(index: index + 1, word: word)
            wordViews.append(wordView)
        }

        let gridStack = NSStackView()
        gridStack.orientation = .vertical
        gridStack.spacing = 6
        gridStack.alignment = .centerX
        gridStack.translatesAutoresizingMaskIntoConstraints = false

        for rowIndex in 0..<rows {
            let rowStack = NSStackView()
            rowStack.orientation = .horizontal
            rowStack.spacing = 8
            rowStack.distribution = .fillEqually
            for colIndex in 0..<columns {
                let wordIndex = rowIndex * columns + colIndex
                if wordIndex < wordViews.count {
                    rowStack.addArrangedSubview(wordViews[wordIndex])
                } else {
                    let spacer = NSView()
                    spacer.translatesAutoresizingMaskIntoConstraints = false
                    spacer.widthAnchor.constraint(equalToConstant: 100).isActive = true
                    rowStack.addArrangedSubview(spacer)
                }
            }
            gridStack.addArrangedSubview(rowStack)
        }

        stack.addArrangedSubview(gridStack)

        // Copy button
        let copyButton = NSButton(title: "Copy to Clipboard", target: self, action: #selector(copyToClipboard(_:)))
        stack.addArrangedSubview(copyButton)

        // Checkbox
        savedCheckbox = NSButton(checkboxWithTitle: "I've saved my recovery phrase", target: self, action: #selector(checkboxToggled(_:)))
        stack.addArrangedSubview(savedCheckbox)

        // Continue button
        continueButton = NSButton(title: "Continue", target: self, action: #selector(continueClicked(_:)))
        continueButton.bezelStyle = .rounded
        continueButton.keyEquivalent = "\r"
        continueButton.isEnabled = false
        stack.addArrangedSubview(continueButton)

        self.view = container
    }

    private func makeWordView(index: Int, word: String) -> NSView {
        let container = NSView()
        container.wantsLayer = true
        container.layer?.cornerRadius = 6
        container.layer?.borderWidth = 1
        container.layer?.borderColor = NSColor.separatorColor.cgColor
        container.layer?.backgroundColor = NSColor.controlBackgroundColor.withAlphaComponent(0.5).cgColor
        container.translatesAutoresizingMaskIntoConstraints = false
        container.widthAnchor.constraint(equalToConstant: 100).isActive = true
        container.heightAnchor.constraint(equalToConstant: 28).isActive = true

        let label = NSTextField(labelWithString: "\(index). \(word)")
        label.font = .monospacedSystemFont(ofSize: 11, weight: .medium)
        label.textColor = .labelColor
        label.translatesAutoresizingMaskIntoConstraints = false
        container.addSubview(label)
        NSLayoutConstraint.activate([
            label.centerXAnchor.constraint(equalTo: container.centerXAnchor),
            label.centerYAnchor.constraint(equalTo: container.centerYAnchor),
        ])

        return container
    }

    @objc private func copyToClipboard(_ sender: NSButton) {
        let phrase = phraseWords.joined(separator: " ")
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(phrase, forType: .string)
    }

    @objc private func checkboxToggled(_ sender: NSButton) {
        continueButton.isEnabled = sender.state == .on
    }

    @objc private func continueClicked(_ sender: NSButton) {
        dismiss(nil)
    }
}
