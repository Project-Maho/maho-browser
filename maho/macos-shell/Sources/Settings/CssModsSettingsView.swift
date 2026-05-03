import AppKit
import UniformTypeIdentifiers

final class CssModsSettingsViewController: NSViewController {
    private let tableView = NSTableView()
    private let emptyLabel = NSTextField(labelWithString: "No CSS mods installed")
    private var cssMods: [CssMod] = []
    private var modsSectionView: SettingsManagedTableSectionView?
    private weak var activeEditTextView: NSTextView?

    override func loadView() {
        let descLabel = NSTextField(wrappingLabelWithString: "CSS Mods let you customize the appearance of any website with custom stylesheets.")
        descLabel.font = .systemFont(ofSize: 12)
        descLabel.textColor = MahoAppColors.secondaryText

        let installButton = NSButton(title: "Install from File…", target: self, action: #selector(installFromFileClicked(_:)))
        installButton.bezelStyle = .rounded
        installButton.setAccessibilityIdentifier("settings.cssMods.installFromFile")

        let topRow = NSStackView(views: [descLabel, installButton])
        topRow.orientation = .horizontal
        topRow.alignment = .centerY
        topRow.spacing = 12

        let nameCol = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("Name"))
        nameCol.title = "Name"
        nameCol.width = 200
        nameCol.resizingMask = .autoresizingMask
        tableView.addTableColumn(nameCol)

        let authorCol = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("Author"))
        authorCol.title = "Author"
        authorCol.width = 120
        authorCol.resizingMask = []
        tableView.addTableColumn(authorCol)

        let versionCol = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("Version"))
        versionCol.title = "Version"
        versionCol.width = 80
        versionCol.resizingMask = []
        tableView.addTableColumn(versionCol)

        let enabledCol = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("Enabled"))
        enabledCol.title = "Enabled"
        enabledCol.width = 60
        enabledCol.resizingMask = []
        tableView.addTableColumn(enabledCol)

        let actionsCol = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("Actions"))
        actionsCol.title = ""
        actionsCol.width = 120
        actionsCol.resizingMask = []
        tableView.addTableColumn(actionsCol)

        tableView.columnAutoresizingStyle = .firstColumnOnlyAutoresizingStyle

        tableView.delegate = self
        tableView.dataSource = self
        tableView.setAccessibilityIdentifier("settings.cssMods.table")

        emptyLabel.translatesAutoresizingMaskIntoConstraints = false
        emptyLabel.textColor = MahoAppColors.secondaryText
        emptyLabel.font = .systemFont(ofSize: 13)
        emptyLabel.alignment = .center
        emptyLabel.isHidden = true
        emptyLabel.setAccessibilityIdentifier("settings.cssMods.emptyState")

        let modsSectionView = SettingsManagedTableSectionView(
            tableView: tableView,
            containerStyle: .card(titleView: makeSettingsSectionTitle("Installed CSS Mods")),
            shellStyle: .compact(emptyStateView: emptyLabel)
        )
        self.modsSectionView = modsSectionView

        view = SettingsPaneHostView(sections: [
            .card([topRow]),
            .managedTable(modsSectionView)
        ])

        updateTableHeight()
    }

    private func updateTableHeight() {
        modsSectionView?.updateManagedTableLayout(rowCount: cssMods.count)
    }

    override func viewDidLoad() {
        super.viewDidLoad()
        reloadMods()
    }

    private func reloadMods() {
        cssMods = MahoBridge.shared.getCssMods()
        tableView.reloadData()
        updateTableHeight()
    }

    @objc private func installFromFileClicked(_ sender: NSButton) {
        let panel = NSOpenPanel()
        panel.title = "Install CSS Mod"
        panel.allowedContentTypes = [UTType(filenameExtension: "css")!]
        panel.allowsMultipleSelection = false
        panel.canChooseDirectories = false

        guard panel.runModal() == .OK, let url = panel.url else { return }

        do {
            let css = try String(contentsOf: url, encoding: .utf8)
            let name = url.deletingPathExtension().lastPathComponent
            MahoBridge.shared.installCssMod(name: name, css: css)
            reloadMods()
        } catch {
            let alert = NSAlert()
            alert.messageText = "Failed to read CSS file"
            alert.informativeText = error.localizedDescription
            alert.alertStyle = .warning
            alert.runModal()
        }
    }

    @objc private func toggleModClicked(_ sender: NSButton) {
        let row = sender.tag
        guard row >= 0, row < cssMods.count else { return }
        MahoBridge.shared.toggleCssMod(modId: cssMods[row].id)
        reloadMods()
    }

    @objc private func removeModClicked(_ sender: NSButton) {
        let row = sender.tag
        guard row >= 0, row < cssMods.count else { return }
        MahoBridge.shared.uninstallCssMod(modId: cssMods[row].id)
        reloadMods()
    }

    @objc private func editModClicked(_ sender: NSButton) {
        let row = sender.tag
        guard row >= 0, row < cssMods.count else { return }
        let mod = cssMods[row]

        let sheet = NSPanel(
            contentRect: NSRect(x: 0, y: 0, width: 500, height: 400),
            styleMask: [.titled, .closable, .resizable],
            backing: .buffered,
            defer: false
        )
        sheet.title = "Edit CSS: \(mod.name)"

        let scrollView = NSScrollView(frame: NSRect(x: 0, y: 0, width: 480, height: 320))
        scrollView.hasVerticalScroller = true
        scrollView.hasHorizontalScroller = true
        scrollView.autoresizingMask = [.width, .height]

        let textView = NSTextView(frame: scrollView.contentView.bounds)
        textView.isEditable = true
        textView.isRichText = false
        textView.font = NSFont.monospacedSystemFont(ofSize: 12, weight: .regular)
        textView.string = mod.css
        textView.autoresizingMask = [.width, .height]
        textView.isVerticallyResizable = true
        textView.isHorizontallyResizable = false
        textView.textContainer?.containerSize = NSSize(width: scrollView.contentSize.width, height: CGFloat.greatestFiniteMagnitude)
        textView.textContainer?.widthTracksTextView = true
        scrollView.documentView = textView

        let cancelButton = NSButton(title: "Cancel", target: nil, action: nil)
        cancelButton.bezelStyle = .rounded
        cancelButton.keyEquivalent = "\u{1b}"

        let saveButton = NSButton(title: "Save", target: nil, action: nil)
        saveButton.bezelStyle = .rounded
        saveButton.keyEquivalent = "\r"

        let buttonStack = NSStackView(views: [cancelButton, saveButton])
        buttonStack.orientation = .horizontal
        buttonStack.spacing = 8
        buttonStack.translatesAutoresizingMaskIntoConstraints = false

        let contentView = NSView(frame: NSRect(x: 0, y: 0, width: 500, height: 400))
        scrollView.translatesAutoresizingMaskIntoConstraints = false
        contentView.addSubview(scrollView)
        contentView.addSubview(buttonStack)

        NSLayoutConstraint.activate([
            scrollView.topAnchor.constraint(equalTo: contentView.topAnchor, constant: 12),
            scrollView.leadingAnchor.constraint(equalTo: contentView.leadingAnchor, constant: 12),
            scrollView.trailingAnchor.constraint(equalTo: contentView.trailingAnchor, constant: -12),
            scrollView.bottomAnchor.constraint(equalTo: buttonStack.topAnchor, constant: -12),
            buttonStack.trailingAnchor.constraint(equalTo: contentView.trailingAnchor, constant: -12),
            buttonStack.bottomAnchor.constraint(equalTo: contentView.bottomAnchor, constant: -12),
        ])

        sheet.contentView = contentView

        let modId = mod.id
        cancelButton.target = self
        cancelButton.action = #selector(dismissSheet(_:))
        saveButton.target = self
        saveButton.action = #selector(saveEditedMod(_:))
        saveButton.tag = row
        textView.setAccessibilityIdentifier("settings.cssMods.editTextView")
        self.activeEditTextView = textView

        sheet.contentView?.setAccessibilityIdentifier("editSheet.\(modId)")

        guard let window = view.window else { return }
        window.beginSheet(sheet) { _ in }
    }

    @objc private func dismissSheet(_ sender: NSButton) {
        activeEditTextView = nil
        guard let sheet = sender.window else { return }
        sheet.sheetParent?.endSheet(sheet, returnCode: .cancel)
    }

    @objc private func saveEditedMod(_ sender: NSButton) {
        let row = sender.tag
        guard row >= 0, row < cssMods.count else { return }
        guard let sheet = sender.window,
              let textView = activeEditTextView else { return }
        MahoBridge.shared.updateCssMod(modId: cssMods[row].id, css: textView.string)
        activeEditTextView = nil
        sheet.sheetParent?.endSheet(sheet, returnCode: .OK)
        reloadMods()
    }
}

extension CssModsSettingsViewController: NSTableViewDelegate, NSTableViewDataSource {
    func numberOfRows(in tableView: NSTableView) -> Int {
        return cssMods.count
    }

    func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?, row: Int) -> NSView? {
        guard row < cssMods.count else { return nil }
        let mod = cssMods[row]

        switch tableColumn?.identifier.rawValue {
        case "Name":
            return makeSettingsTableCell(text: mod.name)
        case "Author":
            return makeSettingsTableCell(text: mod.author ?? "—", secondary: true)
        case "Version":
            return makeSettingsTableCell(text: mod.version ?? "—", secondary: true)
        case "Enabled":
            let toggle = NSButton(checkboxWithTitle: "", target: self, action: #selector(toggleModClicked(_:)))
            toggle.state = mod.enabled ? .on : .off
            toggle.tag = row
            toggle.setAccessibilityIdentifier("settings.cssMods.toggle.\(row)")
            return makeSettingsCenteredTableActionCell(toggle)
        case "Actions":
            let editBtn = NSButton(title: "Edit", target: self, action: #selector(editModClicked(_:)))
            editBtn.bezelStyle = .inline
            editBtn.font = .systemFont(ofSize: 10)
            editBtn.tag = row
            editBtn.setAccessibilityIdentifier("settings.cssMods.edit.\(row)")

            let removeBtn = NSButton(title: "Remove", target: self, action: #selector(removeModClicked(_:)))
            removeBtn.bezelStyle = .inline
            removeBtn.font = .systemFont(ofSize: 10)
            removeBtn.tag = row
            removeBtn.contentTintColor = MahoAppColors.dangerRed
            removeBtn.setAccessibilityIdentifier("settings.cssMods.remove.\(row)")

            let stack = NSStackView(views: [editBtn, removeBtn])
            stack.orientation = .horizontal
            stack.spacing = 4
            return makeSettingsCenteredTableActionCell(stack)
        default:
            return nil
        }
    }
}
