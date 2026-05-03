import AppKit

final class SyncConflictController: NSViewController {
    private let conflictJson: String

    struct ConflictItem {
        let entityType: String
        let localValue: String
        let remoteValue: String
    }

    private var conflicts: [ConflictItem] = []
    private var tableView: NSTableView!

    init(conflictJson: String) {
        self.conflictJson = conflictJson
        super.init(nibName: nil, bundle: nil)
        parseConflicts()
    }

    required init?(coder: NSCoder) { fatalError() }

    private func parseConflicts() {
        guard let data = conflictJson.data(using: .utf8),
              let array = try? JSONSerialization.jsonObject(with: data) as? [[String: String]] else { return }
        conflicts = array.compactMap { dict in
            guard let type = dict["type"], let local = dict["local"], let remote = dict["remote"] else { return nil }
            return ConflictItem(entityType: type, localValue: local, remoteValue: remote)
        }
    }

    override func loadView() {
        view = NSView(frame: NSRect(x: 0, y: 0, width: 500, height: 350))

        let titleLabel = NSTextField(labelWithString: "Sync Conflicts")
        titleLabel.font = .systemFont(ofSize: 16, weight: .semibold)
        titleLabel.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(titleLabel)

        let descLabel = NSTextField(wrappingLabelWithString: "Some items were changed on multiple devices. Choose which version to keep.")
        descLabel.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(descLabel)

        let scrollView = NSScrollView(frame: .zero)
        scrollView.translatesAutoresizingMaskIntoConstraints = false
        scrollView.hasVerticalScroller = true

        tableView = NSTableView()
        tableView.style = .inset
        tableView.usesAlternatingRowBackgroundColors = true

        let typeCol = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("type"))
        typeCol.title = "Type"
        typeCol.width = 80
        tableView.addTableColumn(typeCol)

        let localCol = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("local"))
        localCol.title = "Local"
        localCol.width = 160
        tableView.addTableColumn(localCol)

        let remoteCol = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("remote"))
        remoteCol.title = "Remote"
        remoteCol.width = 160
        tableView.addTableColumn(remoteCol)

        tableView.delegate = self
        tableView.dataSource = self
        scrollView.documentView = tableView
        view.addSubview(scrollView)

        let keepLocalBtn = NSButton(title: "Keep Local", target: self, action: #selector(keepLocal))
        let keepRemoteBtn = NSButton(title: "Keep Remote", target: self, action: #selector(keepRemote))
        keepLocalBtn.translatesAutoresizingMaskIntoConstraints = false
        keepRemoteBtn.translatesAutoresizingMaskIntoConstraints = false
        keepRemoteBtn.keyEquivalent = "\r"
        view.addSubview(keepLocalBtn)
        view.addSubview(keepRemoteBtn)

        NSLayoutConstraint.activate([
            titleLabel.topAnchor.constraint(equalTo: view.topAnchor, constant: 16),
            titleLabel.leadingAnchor.constraint(equalTo: view.leadingAnchor, constant: 16),
            descLabel.topAnchor.constraint(equalTo: titleLabel.bottomAnchor, constant: 8),
            descLabel.leadingAnchor.constraint(equalTo: view.leadingAnchor, constant: 16),
            descLabel.trailingAnchor.constraint(equalTo: view.trailingAnchor, constant: -16),
            scrollView.topAnchor.constraint(equalTo: descLabel.bottomAnchor, constant: 12),
            scrollView.leadingAnchor.constraint(equalTo: view.leadingAnchor, constant: 16),
            scrollView.trailingAnchor.constraint(equalTo: view.trailingAnchor, constant: -16),
            scrollView.bottomAnchor.constraint(equalTo: keepLocalBtn.topAnchor, constant: -12),
            keepRemoteBtn.trailingAnchor.constraint(equalTo: view.trailingAnchor, constant: -16),
            keepRemoteBtn.bottomAnchor.constraint(equalTo: view.bottomAnchor, constant: -16),
            keepLocalBtn.trailingAnchor.constraint(equalTo: keepRemoteBtn.leadingAnchor, constant: -8),
            keepLocalBtn.bottomAnchor.constraint(equalTo: view.bottomAnchor, constant: -16),
        ])
    }

    @objc private func keepLocal() {
        resolveConflicts(keep: "local")
    }

    @objc private func keepRemote() {
        resolveConflicts(keep: "remote")
    }

    private func resolveConflicts(keep: String) {
        let resolution = conflicts.map { ["type": $0.entityType, "keep": keep] }
        if let data = try? JSONSerialization.data(withJSONObject: resolution),
           let json = String(data: data, encoding: .utf8) {
            _ = MahoBridge.shared.applyRemoteEntities(entitiesJson: json)
        }
        dismiss(nil)
    }
}

extension SyncConflictController: NSTableViewDataSource, NSTableViewDelegate {
    func numberOfRows(in tableView: NSTableView) -> Int { conflicts.count }

    func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?, row: Int) -> NSView? {
        let conflict = conflicts[row]
        let cell = NSTextField(labelWithString: "")
        switch tableColumn?.identifier.rawValue {
        case "type": cell.stringValue = conflict.entityType
        case "local": cell.stringValue = conflict.localValue
        case "remote": cell.stringValue = conflict.remoteValue
        default: break
        }
        return cell
    }
}
