import AppKit

final class ShortcutsSettingsViewController: NSViewController, NSTableViewDataSource, NSTableViewDelegate {

    private var tableView = NSTableView()
    private var resetAllButton = NSButton()

    private var shortcuts: [ShortcutBinding] = []
    private var capturingRow: Int? = nil
    private var eventMonitor: Any? = nil

    override func loadView() {
        let labelColumn = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("label"))
        labelColumn.title = "Action"
        labelColumn.width = 200
        labelColumn.resizingMask = .autoresizingMask
        tableView.addTableColumn(labelColumn)

        let categoryColumn = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("category"))
        categoryColumn.title = "Category"
        categoryColumn.width = 120
        categoryColumn.resizingMask = []
        tableView.addTableColumn(categoryColumn)

        let keyComboColumn = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("keyCombo"))
        keyComboColumn.title = "Key Combo"
        keyComboColumn.width = 180
        keyComboColumn.resizingMask = []
        tableView.addTableColumn(keyComboColumn)

        tableView.columnAutoresizingStyle = .firstColumnOnlyAutoresizingStyle

        tableView.headerView = NSTableHeaderView()
        styleSettingsTable(tableView)
        tableView.delegate = self
        tableView.dataSource = self
        tableView.doubleAction = #selector(tableDoubleClicked(_:))
        tableView.target = self

        resetAllButton.title = "Reset All"
        resetAllButton.target = self
        resetAllButton.action = #selector(resetAllShortcuts(_:))

        let shortcutsSectionView = SettingsManagedTableSectionView(
            tableView: tableView,
            containerStyle: .plain(footerView: makeSettingsActionRow([resetAllButton])),
            shellStyle: .fixedHeight(280)
        )

        view = SettingsPaneHostView(sections: [
            .managedTable(shortcutsSectionView)
        ])
    }

    override func viewDidLoad() {
        super.viewDidLoad()
        loadShortcuts()
    }

    private func loadShortcuts() {
        if let json = MahoBridge.shared.getShortcuts(),
           let bindings: [ShortcutBinding] = MahoBridge.shared.decode(json) {
            shortcuts = bindings
            tableView.reloadData()
        }
    }

    // MARK: - NSTableViewDataSource

    func numberOfRows(in tableView: NSTableView) -> Int {
        return shortcuts.count
    }

    // MARK: - NSTableViewDelegate

    func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?, row: Int) -> NSView? {
        guard row < shortcuts.count else { return nil }
        let shortcut = shortcuts[row]

        if tableColumn?.identifier.rawValue == "label" {
            return makeSettingsTableCell(text: shortcut.label)
        } else if tableColumn?.identifier.rawValue == "category" {
            return makeSettingsTableCell(text: shortcut.category.rawValue, secondary: true)
        } else if tableColumn?.identifier.rawValue == "keyCombo" {
            let displayText: String
            if let capturing = capturingRow, capturing == row {
                displayText = "Press keys..."
            } else {
                displayText = formatKeyCombo(shortcut.keyCombo)
            }
            return makeSettingsTableCell(text: displayText)
        }
        return nil
    }

    func tableView(_ tableView: NSTableView, rowActionsForRow row: Int, edge: NSTableView.RowActionEdge) -> [NSTableViewRowAction] {
        guard edge == .trailing else { return [] }
        let resetAction = NSTableViewRowAction(style: .regular, title: "Reset") { [weak self] _, _ in
            self?.resetShortcut(at: row)
        }
        return [resetAction]
    }

    @objc private func tableDoubleClicked(_ sender: NSTableView) {
        let row = sender.clickedRow
        guard row >= 0, row < shortcuts.count else { return }
        capturingRow = row
        tableView.reloadData()
        eventMonitor = NSEvent.addLocalMonitorForEvents(matching: .keyDown) { [weak self] event in
            guard let self = self, let captureRow = self.capturingRow else { return event }
            self.captureKey(event, forRow: captureRow)
            return nil
        }
    }

    private func captureKey(_ event: NSEvent, forRow row: Int) {
        if let monitor = eventMonitor {
            NSEvent.removeMonitor(monitor)
            eventMonitor = nil
        }
        capturingRow = nil
        let key = event.charactersIgnoringModifiers ?? ""
        var modifiers: [KeyModifier] = []
        if event.modifierFlags.contains(.control) { modifiers.append(.ctrl) }
        if event.modifierFlags.contains(.shift) { modifiers.append(.shift) }
        if event.modifierFlags.contains(.option) { modifiers.append(.alt) }
        if event.modifierFlags.contains(.command) { modifiers.append(.meta) }
        let combo = KeyCombo(key: key, modifiers: modifiers)
        guard let comboJson = MahoBridge.shared.encode(combo) else { return }
        let action = shortcuts[row].action
        _ = MahoBridge.shared.setShortcut(action: action, keyComboJson: comboJson)
        loadShortcuts()
    }

    @objc private func resetAllShortcuts(_ sender: NSButton) {
        MahoBridge.shared.resetAllShortcuts()
        loadShortcuts()
    }

    private func resetShortcut(at row: Int) {
        guard row >= 0, row < shortcuts.count else { return }
        MahoBridge.shared.resetShortcut(action: shortcuts[row].action)
        loadShortcuts()
    }

    private func formatKeyCombo(_ combo: KeyCombo) -> String {
        var parts: [String] = []
        if combo.modifiers.contains(.ctrl) { parts.append("Ctrl") }
        if combo.modifiers.contains(.shift) { parts.append("Shift") }
        if combo.modifiers.contains(.alt) { parts.append("Alt") }
        if combo.modifiers.contains(.meta) { parts.append("Cmd") }
        parts.append(combo.key.uppercased())
        return parts.joined(separator: "+")
    }

    deinit {
        if let monitor = eventMonitor {
            NSEvent.removeMonitor(monitor)
        }
    }
}
