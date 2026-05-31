import AppKit

final class ToolbarCustomizationController: NSViewController {

    private var availableTableView = NSTableView()
    private var currentTableView = NSTableView()
    private var allItems: [ToolbarItem] = []
    private var currentItems: [ToolbarItem] = []
    private var availableItems: [ToolbarItem] = []

    override func loadView() {
        let container = NSView(frame: NSRect(x: 0, y: 0, width: 640, height: 440))
        container.wantsLayer = true

        let stack = NSStackView()
        stack.orientation = .vertical
        stack.alignment = .centerX
        stack.spacing = 12
        stack.translatesAutoresizingMaskIntoConstraints = false
        container.addSubview(stack)
        NSLayoutConstraint.activate([
            stack.topAnchor.constraint(equalTo: container.topAnchor, constant: 16),
            stack.leadingAnchor.constraint(equalTo: container.leadingAnchor, constant: 16),
            stack.trailingAnchor.constraint(equalTo: container.trailingAnchor, constant: -16),
            stack.bottomAnchor.constraint(lessThanOrEqualTo: container.bottomAnchor, constant: -16),
        ])

        let titleLabel = NSTextField(labelWithString: "Customize Toolbar")
        titleLabel.font = .systemFont(ofSize: 16, weight: .bold)
        titleLabel.textColor = .labelColor
        stack.addArrangedSubview(titleLabel)

        let descLabel = NSTextField(wrappingLabelWithString: "Drag items between panels to add or remove toolbar items. Reorder items in the current panel.")
        descLabel.font = .systemFont(ofSize: 11)
        descLabel.textColor = .secondaryLabelColor
        descLabel.alignment = .center
        descLabel.translatesAutoresizingMaskIntoConstraints = false
        descLabel.widthAnchor.constraint(lessThanOrEqualToConstant: 500).isActive = true
        stack.addArrangedSubview(descLabel)

        // Panels
        let panelsRow = NSStackView()
        panelsRow.orientation = .horizontal
        panelsRow.spacing = 16
        panelsRow.alignment = .top

        // Available items panel
        let availableColumn = NSStackView()
        availableColumn.orientation = .vertical
        availableColumn.alignment = .leading
        availableColumn.spacing = 4

        let availableLabel = NSTextField(labelWithString: "Available Items")
        availableLabel.font = .systemFont(ofSize: 12, weight: .semibold)
        availableLabel.textColor = .labelColor
        availableColumn.addArrangedSubview(availableLabel)

        setupTable(availableTableView, tag: 0)
        let availableScroll = NSScrollView()
        availableScroll.translatesAutoresizingMaskIntoConstraints = false
        availableScroll.documentView = availableTableView
        availableScroll.hasVerticalScroller = true
        availableScroll.heightAnchor.constraint(equalToConstant: 280).isActive = true
        availableScroll.widthAnchor.constraint(equalToConstant: 240).isActive = true
        availableScroll.drawsBackground = false
        availableColumn.addArrangedSubview(availableScroll)

        panelsRow.addArrangedSubview(availableColumn)

        // Arrows
        let arrowColumn = NSStackView()
        arrowColumn.orientation = .vertical
        arrowColumn.alignment = .centerX
        arrowColumn.spacing = 8

        let addButton = NSButton(title: ">>", target: self, action: #selector(addItemClicked(_:)))
        addButton.toolTip = "Add to toolbar"
        arrowColumn.addArrangedSubview(addButton)

        let removeButton = NSButton(title: "<<", target: self, action: #selector(removeItemClicked(_:)))
        removeButton.toolTip = "Remove from toolbar"
        arrowColumn.addArrangedSubview(removeButton)

        let moveUpButton = NSButton(title: "Up", target: self, action: #selector(moveUpClicked(_:)))
        arrowColumn.addArrangedSubview(moveUpButton)

        let moveDownButton = NSButton(title: "Down", target: self, action: #selector(moveDownClicked(_:)))
        arrowColumn.addArrangedSubview(moveDownButton)

        panelsRow.addArrangedSubview(arrowColumn)

        // Current items panel
        let currentColumn = NSStackView()
        currentColumn.orientation = .vertical
        currentColumn.alignment = .leading
        currentColumn.spacing = 4

        let currentLabel = NSTextField(labelWithString: "Current Toolbar")
        currentLabel.font = .systemFont(ofSize: 12, weight: .semibold)
        currentLabel.textColor = .labelColor
        currentColumn.addArrangedSubview(currentLabel)

        setupTable(currentTableView, tag: 1)
        let currentScroll = NSScrollView()
        currentScroll.translatesAutoresizingMaskIntoConstraints = false
        currentScroll.documentView = currentTableView
        currentScroll.hasVerticalScroller = true
        currentScroll.heightAnchor.constraint(equalToConstant: 280).isActive = true
        currentScroll.widthAnchor.constraint(equalToConstant: 240).isActive = true
        currentScroll.drawsBackground = false
        currentColumn.addArrangedSubview(currentScroll)

        panelsRow.addArrangedSubview(currentColumn)

        stack.addArrangedSubview(panelsRow)

        // Bottom buttons
        let buttonRow = NSStackView()
        buttonRow.orientation = .horizontal
        buttonRow.spacing = 8

        let restoreButton = NSButton(title: "Restore Defaults", target: self, action: #selector(restoreDefaultsClicked(_:)))
        buttonRow.addArrangedSubview(restoreButton)

        let spacer = NSView()
        spacer.translatesAutoresizingMaskIntoConstraints = false
        spacer.widthAnchor.constraint(greaterThanOrEqualToConstant: 100).isActive = true
        buttonRow.addArrangedSubview(spacer)

        let doneButton = NSButton(title: "Done", target: self, action: #selector(doneClicked(_:)))
        doneButton.bezelStyle = .rounded
        doneButton.keyEquivalent = "\r"
        buttonRow.addArrangedSubview(doneButton)

        stack.addArrangedSubview(buttonRow)

        self.view = container
        loadItems()
    }

    private func setupTable(_ tableView: NSTableView, tag: Int) {
        tableView.tag = tag
        tableView.delegate = self
        tableView.dataSource = self
        let iconCol = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("Icon"))
        iconCol.title = ""
        iconCol.width = 28
        tableView.addTableColumn(iconCol)
        let labelCol = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("Label"))
        labelCol.title = "Item"
        labelCol.width = 180
        tableView.addTableColumn(labelCol)
        tableView.headerView = nil
        tableView.rowHeight = 28
        tableView.backgroundColor = .clear
        tableView.gridColor = .clear
        tableView.intercellSpacing = NSSize(width: 0, height: 2)
        tableView.usesAlternatingRowBackgroundColors = false
        tableView.selectionHighlightStyle = .regular
    }

    private func loadItems() {
        if let json = MahoBridge.shared.getToolbarItems(),
           let decoded: [ToolbarItem] = MahoBridge.shared.decode(json) {
            allItems = decoded
            currentItems = decoded.filter { $0.visible }
            availableItems = decoded.filter { !$0.visible }
        } else {
            allItems = []
            currentItems = []
            availableItems = []
        }
        availableTableView.reloadData()
        currentTableView.reloadData()
    }

    private func iconName(for kind: ToolbarItemKind) -> String {
        switch kind {
        case .backForward: return "chevron.left.chevron.right"
        case .reload: return "arrow.clockwise"
        case .addressBar: return "globe"
        case .share: return "square.and.arrow.up"
        case .downloads: return "arrow.down.circle"
        case .extensions: return "puzzlepiece.extension"
        case .splitView: return "rectangle.split.2x1"
        case .spacer: return "minus"
        case .flexibleSpacer: return "arrow.left.and.right"
        case .custom: return "star"
        }
    }

    // MARK: - Actions

    @objc private func addItemClicked(_ sender: NSButton) {
        let row = availableTableView.selectedRow
        guard row >= 0, row < availableItems.count else { return }
        let item = availableItems.remove(at: row)
        currentItems.append(item)
        availableTableView.reloadData()
        currentTableView.reloadData()
    }

    @objc private func removeItemClicked(_ sender: NSButton) {
        let row = currentTableView.selectedRow
        guard row >= 0, row < currentItems.count else { return }
        let item = currentItems.remove(at: row)
        availableItems.append(item)
        availableTableView.reloadData()
        currentTableView.reloadData()
    }

    @objc private func moveUpClicked(_ sender: NSButton) {
        let row = currentTableView.selectedRow
        guard row > 0, row < currentItems.count else { return }
        currentItems.swapAt(row, row - 1)
        currentTableView.reloadData()
        currentTableView.selectRowIndexes(IndexSet(integer: row - 1), byExtendingSelection: false)
    }

    @objc private func moveDownClicked(_ sender: NSButton) {
        let row = currentTableView.selectedRow
        guard row >= 0, row < currentItems.count - 1 else { return }
        currentItems.swapAt(row, row + 1)
        currentTableView.reloadData()
        currentTableView.selectRowIndexes(IndexSet(integer: row + 1), byExtendingSelection: false)
    }

    @objc private func restoreDefaultsClicked(_ sender: NSButton) {
        if let json = MahoBridge.shared.getDefaultToolbarItems(),
           let decoded: [ToolbarItem] = MahoBridge.shared.decode(json) {
            let defaultIds = Set(decoded.map { $0.id })
            currentItems = decoded
            availableItems = allItems.filter { !defaultIds.contains($0.id) }
            availableTableView.reloadData()
            currentTableView.reloadData()
        }
    }

    @objc private func doneClicked(_ sender: NSButton) {
        // Build final list: current items as visible, available as not visible
        var finalItems: [ToolbarItemUpdate] = []
        for item in currentItems {
            finalItems.append(ToolbarItemUpdate(id: item.id, visible: true))
        }
        for item in availableItems {
            finalItems.append(ToolbarItemUpdate(id: item.id, visible: false))
        }
        if let data = try? JSONEncoder().encode(finalItems),
           let jsonStr = String(data: data, encoding: .utf8) {
            _ = MahoBridge.shared.setToolbarItems(itemsJson: jsonStr)
        }
        dismiss(nil)
    }
}

private struct ToolbarItemUpdate: Codable {
    let id: String
    let visible: Bool
}

extension ToolbarCustomizationController: NSTableViewDelegate, NSTableViewDataSource {
    func numberOfRows(in tableView: NSTableView) -> Int {
        if tableView.tag == 0 {
            return availableItems.count
        }
        return currentItems.count
    }

    func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?, row: Int) -> NSView? {
        let items = tableView.tag == 0 ? availableItems : currentItems
        guard row < items.count else { return nil }
        let item = items[row]
        let colId = tableColumn?.identifier.rawValue ?? ""

        switch colId {
        case "Icon":
            let imageView = NSImageView()
            imageView.image = MahoIcon.imageForSFSymbol(iconName(for: item.kind), accessibilityDescription: item.label)
            imageView.contentTintColor = .secondaryLabelColor
            let cell = NSTableCellView()
            cell.imageView = imageView
            cell.addSubview(imageView)
            imageView.translatesAutoresizingMaskIntoConstraints = false
            NSLayoutConstraint.activate([
                imageView.centerXAnchor.constraint(equalTo: cell.centerXAnchor),
                imageView.centerYAnchor.constraint(equalTo: cell.centerYAnchor),
                imageView.widthAnchor.constraint(equalToConstant: 16),
                imageView.heightAnchor.constraint(equalToConstant: 16),
            ])
            return cell
        case "Label":
            let label = NSTextField(labelWithString: item.label)
            label.font = .systemFont(ofSize: 12)
            label.textColor = .labelColor
            let cell = NSTableCellView()
            cell.textField = label
            cell.addSubview(label)
            label.translatesAutoresizingMaskIntoConstraints = false
            NSLayoutConstraint.activate([
                label.leadingAnchor.constraint(equalTo: cell.leadingAnchor, constant: 4),
                label.centerYAnchor.constraint(equalTo: cell.centerYAnchor),
            ])
            return cell
        default:
            return nil
        }
    }
}
