import AppKit

final class TidyTabsLoadingViewController: NSViewController {
    override func loadView() {
        let container = NSView(frame: NSRect(x: 0, y: 0, width: 300, height: 80))

        let spinner = NSProgressIndicator()
        spinner.translatesAutoresizingMaskIntoConstraints = false
        spinner.style = .spinning
        spinner.controlSize = .small
        spinner.startAnimation(nil)
        container.addSubview(spinner)

        let label = NSTextField(labelWithString: "AI is analyzing your tabs...")
        label.translatesAutoresizingMaskIntoConstraints = false
        label.font = .systemFont(ofSize: 12, weight: .regular)
        label.textColor = .secondaryLabelColor
        container.addSubview(label)

        NSLayoutConstraint.activate([
            spinner.centerXAnchor.constraint(equalTo: container.centerXAnchor),
            spinner.topAnchor.constraint(equalTo: container.topAnchor, constant: 20),
            label.centerXAnchor.constraint(equalTo: container.centerXAnchor),
            label.topAnchor.constraint(equalTo: spinner.bottomAnchor, constant: 8),
        ])

        self.view = container
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    init() {
        super.init(nibName: nil, bundle: nil)
    }
}

final class TidyTabsConfirmViewController: NSViewController {
    private let spaceId: SpaceId
    private let folders: [TidyTabFolder]
    private let allTabs: [TabViewModel]
    private let onCancel: () -> Void
    private let onApply: (SpaceId) -> Void

    init(spaceId: SpaceId, folders: [TidyTabFolder], allTabs: [TabViewModel],
         onCancel: @escaping () -> Void, onApply: @escaping (SpaceId) -> Void) {
        self.spaceId = spaceId
        self.folders = folders
        self.allTabs = allTabs
        self.onCancel = onCancel
        self.onApply = onApply
        super.init(nibName: nil, bundle: nil)
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    override func loadView() {
        let container = NSView(frame: NSRect(x: 0, y: 0, width: 300, height: 10))
        container.translatesAutoresizingMaskIntoConstraints = false

        let stack = NSStackView()
        stack.translatesAutoresizingMaskIntoConstraints = false
        stack.orientation = .vertical
        stack.spacing = 4
        stack.alignment = .leading
        stack.edgeInsets = NSEdgeInsets(top: 12, left: 16, bottom: 12, right: 16)
        container.addSubview(stack)

        let headerRow = NSStackView()
        headerRow.orientation = .horizontal
        headerRow.spacing = 8

        let titleLabel = NSTextField(labelWithString: "\u{1F9F9} Tidy Tabs")
        titleLabel.font = .systemFont(ofSize: 14, weight: .semibold)
        headerRow.addArrangedSubview(titleLabel)

        let spacer = NSView()
        spacer.translatesAutoresizingMaskIntoConstraints = false
        spacer.setContentHuggingPriority(.defaultLow, for: .horizontal)
        headerRow.addArrangedSubview(spacer)

        let cancelButton = NSButton(title: "Cancel", target: self, action: #selector(cancelTapped))
        cancelButton.bezelStyle = .rounded
        cancelButton.controlSize = .small
        headerRow.addArrangedSubview(cancelButton)

        let applyButton = NSButton(title: "Apply", target: self, action: #selector(applyTapped))
        applyButton.bezelStyle = .rounded
        applyButton.controlSize = .small
        applyButton.contentTintColor = .white
        applyButton.wantsLayer = true
        applyButton.layer?.backgroundColor = NSColor(red: 0.2, green: 0.4, blue: 0.9, alpha: 1.0).cgColor
        applyButton.layer?.cornerRadius = 5
        headerRow.addArrangedSubview(applyButton)

        headerRow.translatesAutoresizingMaskIntoConstraints = false
        stack.addArrangedSubview(headerRow)
        NSLayoutConstraint.activate([
            headerRow.widthAnchor.constraint(equalTo: stack.widthAnchor, constant: -32),
        ])

        let separator = NSBox()
        separator.boxType = .separator
        separator.translatesAutoresizingMaskIntoConstraints = false
        stack.addArrangedSubview(separator)
        NSLayoutConstraint.activate([
            separator.widthAnchor.constraint(equalTo: stack.widthAnchor, constant: -32),
        ])

        let folderIconColor = NSColor(red: 96.0/255.0, green: 165.0/255.0, blue: 250.0/255.0, alpha: 1.0)

        for folder in folders {
            let folderRow = NSStackView()
            folderRow.orientation = .horizontal
            folderRow.spacing = 6
            folderRow.alignment = .firstBaseline

            let folderIcon = NSImageView()
            folderIcon.translatesAutoresizingMaskIntoConstraints = false
            folderIcon.image = MahoIcon.imageForSFSymbol("folder.fill")
            folderIcon.contentTintColor = folderIconColor
            NSLayoutConstraint.activate([
                folderIcon.widthAnchor.constraint(equalToConstant: 14),
                folderIcon.heightAnchor.constraint(equalToConstant: 14),
            ])
            folderRow.addArrangedSubview(folderIcon)

            let folderLabel = NSTextField(labelWithString: folder.name)
            folderLabel.font = .systemFont(ofSize: 12, weight: .medium)
            folderRow.addArrangedSubview(folderLabel)

            stack.addArrangedSubview(folderRow)

            for tabId in folder.tabIds {
                if let tab = allTabs.first(where: { $0.id == tabId }) {
                    let tabLabel = NSTextField(labelWithString: tab.title)
                    tabLabel.font = .systemFont(ofSize: 11, weight: .regular)
                    tabLabel.textColor = .secondaryLabelColor
                    tabLabel.lineBreakMode = .byTruncatingTail
                    tabLabel.translatesAutoresizingMaskIntoConstraints = false

                    let tabRow = NSStackView()
                    tabRow.orientation = .horizontal
                    tabRow.spacing = 0

                    let indent = NSView()
                    indent.translatesAutoresizingMaskIntoConstraints = false
                    NSLayoutConstraint.activate([
                        indent.widthAnchor.constraint(equalToConstant: 20),
                    ])
                    tabRow.addArrangedSubview(indent)
                    tabRow.addArrangedSubview(tabLabel)

                    stack.addArrangedSubview(tabRow)
                }
            }
        }

        NSLayoutConstraint.activate([
            stack.topAnchor.constraint(equalTo: container.topAnchor),
            stack.leadingAnchor.constraint(equalTo: container.leadingAnchor),
            stack.trailingAnchor.constraint(equalTo: container.trailingAnchor),
            stack.bottomAnchor.constraint(equalTo: container.bottomAnchor),
            container.widthAnchor.constraint(equalToConstant: 300),
        ])

        self.view = container
    }

    @objc private func cancelTapped() {
        onCancel()
    }

    @objc private func applyTapped() {
        onApply(spaceId)
    }
}
