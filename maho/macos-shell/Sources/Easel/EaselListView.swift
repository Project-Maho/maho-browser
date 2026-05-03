import AppKit

final class EaselListView: NSView, NSTableViewDelegate, NSTableViewDataSource {
    var easels: [EaselViewModel] = []
    let tableView = NSTableView()
    private let scrollView = NSScrollView()
    private var editorController: EaselEditorController?

    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        setupUI()
        reloadEasels()
    }

    required init?(coder: NSCoder) {
        super.init(coder: coder)
        setupUI()
        reloadEasels()
    }

    private func setupUI() {
        let newBtn = NSButton(title: "New Easel", target: self, action: #selector(createEasel))
        newBtn.translatesAutoresizingMaskIntoConstraints = false
        let deleteBtn = NSButton(title: "Delete", target: self, action: #selector(deleteEasel))
        deleteBtn.translatesAutoresizingMaskIntoConstraints = false

        let buttonBar = NSStackView(views: [newBtn, deleteBtn])
        buttonBar.translatesAutoresizingMaskIntoConstraints = false
        buttonBar.orientation = .horizontal
        buttonBar.spacing = 8

        let nameCol = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("EaselName"))
        nameCol.title = "Name"
        nameCol.width = 300
        tableView.addTableColumn(nameCol)

        let countCol = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("EaselCount"))
        countCol.title = "Items"
        countCol.width = 80
        tableView.addTableColumn(countCol)

        tableView.delegate = self
        tableView.dataSource = self
        tableView.doubleAction = #selector(openEasel)
        tableView.target = self

        scrollView.translatesAutoresizingMaskIntoConstraints = false
        scrollView.documentView = tableView
        scrollView.hasVerticalScroller = true

        addSubview(buttonBar)
        addSubview(scrollView)

        NSLayoutConstraint.activate([
            buttonBar.topAnchor.constraint(equalTo: topAnchor, constant: 8),
            buttonBar.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 8),

            scrollView.topAnchor.constraint(equalTo: buttonBar.bottomAnchor, constant: 8),
            scrollView.leadingAnchor.constraint(equalTo: leadingAnchor),
            scrollView.trailingAnchor.constraint(equalTo: trailingAnchor),
            scrollView.bottomAnchor.constraint(equalTo: bottomAnchor),
        ])
    }

    func reloadEasels() {
        if let json = MahoBridge.shared.getEaselViewModels(),
           let decoded: [EaselViewModel] = MahoBridge.shared.decode(json) {
            easels = decoded
        } else {
            easels = []
        }
        tableView.reloadData()
    }

    // MARK: - NSTableViewDataSource

    func numberOfRows(in tableView: NSTableView) -> Int {
        return easels.count
    }

    func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?, row: Int) -> NSView? {
        guard row < easels.count else { return nil }
        let easel = easels[row]
        let cellId = NSUserInterfaceItemIdentifier("EaselCell")
        var cell = tableView.makeView(withIdentifier: cellId, owner: self) as? NSTableCellView
        if cell == nil {
            cell = NSTableCellView()
            cell?.identifier = cellId
            let tf = NSTextField(labelWithString: "")
            tf.translatesAutoresizingMaskIntoConstraints = false
            cell?.textField = tf
            cell?.addSubview(tf)
            NSLayoutConstraint.activate([
                tf.leadingAnchor.constraint(equalTo: cell!.leadingAnchor, constant: 4),
                tf.trailingAnchor.constraint(equalTo: cell!.trailingAnchor, constant: -4),
                tf.centerYAnchor.constraint(equalTo: cell!.centerYAnchor),
            ])
        }

        let colId = tableColumn?.identifier.rawValue ?? ""
        switch colId {
        case "EaselName":
            cell?.textField?.stringValue = easel.name
        case "EaselCount":
            cell?.textField?.stringValue = "\(easel.itemCount)"
        default:
            cell?.textField?.stringValue = easel.name
        }
        return cell
    }

    // MARK: - Actions

    @objc private func createEasel() {
        let editor = EaselEditorController(name: "Untitled Easel")
        editorController = editor
        editor.createNewEasel()
        editor.showWindow(nil)
    }

    @objc private func deleteEasel() {
        let row = tableView.selectedRow
        guard row >= 0, row < easels.count else { return }
        _ = MahoBridge.shared.deleteEasel(easelId: easels[row].id)
        reloadEasels()
    }

    @objc private func openEasel() {
        let row = tableView.clickedRow
        guard row >= 0, row < easels.count else { return }
        let easel = easels[row]
        let editor = EaselEditorController(easelId: easel.id, name: easel.name)
        editorController = editor
        editor.showWindow(nil)
    }
}
