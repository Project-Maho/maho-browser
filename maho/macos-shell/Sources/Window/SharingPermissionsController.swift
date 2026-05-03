import AppKit

final class SharingPermissionsController: NSViewController, NSTableViewDataSource, NSTableViewDelegate {
    private let collectionId: String
    private var members: [(name: String, email: String, permission: String)] = []
    private let tableView = NSTableView()

    init(collectionId: String) {
        self.collectionId = collectionId
        super.init(nibName: nil, bundle: nil)
    }

    required init?(coder: NSCoder) { fatalError() }

    override func loadView() {
        let container = NSView(frame: NSRect(x: 0, y: 0, width: 500, height: 300))
        self.view = container

        let titleLabel = NSTextField(labelWithString: "Collection Members")
        titleLabel.font = .boldSystemFont(ofSize: 14)
        titleLabel.translatesAutoresizingMaskIntoConstraints = false
        container.addSubview(titleLabel)

        let nameCol = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("name"))
        nameCol.title = "Name"
        nameCol.width = 150
        tableView.addTableColumn(nameCol)

        let emailCol = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("email"))
        emailCol.title = "Email"
        emailCol.width = 180
        tableView.addTableColumn(emailCol)

        let permCol = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("permission"))
        permCol.title = "Permission"
        permCol.width = 100
        tableView.addTableColumn(permCol)

        tableView.dataSource = self
        tableView.delegate = self
        tableView.headerView = NSTableHeaderView()

        let scrollView = NSScrollView()
        scrollView.documentView = tableView
        scrollView.hasVerticalScroller = true
        scrollView.translatesAutoresizingMaskIntoConstraints = false
        container.addSubview(scrollView)

        let doneButton = NSButton(title: "Done", target: self, action: #selector(dismissSheet))
        doneButton.bezelStyle = .rounded
        doneButton.keyEquivalent = "\u{1b}"
        doneButton.translatesAutoresizingMaskIntoConstraints = false
        container.addSubview(doneButton)

        NSLayoutConstraint.activate([
            titleLabel.topAnchor.constraint(equalTo: container.topAnchor, constant: 16),
            titleLabel.leadingAnchor.constraint(equalTo: container.leadingAnchor, constant: 16),

            scrollView.topAnchor.constraint(equalTo: titleLabel.bottomAnchor, constant: 12),
            scrollView.leadingAnchor.constraint(equalTo: container.leadingAnchor, constant: 16),
            scrollView.trailingAnchor.constraint(equalTo: container.trailingAnchor, constant: -16),
            scrollView.bottomAnchor.constraint(equalTo: doneButton.topAnchor, constant: -12),

            doneButton.trailingAnchor.constraint(equalTo: container.trailingAnchor, constant: -16),
            doneButton.bottomAnchor.constraint(equalTo: container.bottomAnchor, constant: -16),
        ])
    }

    func numberOfRows(in tableView: NSTableView) -> Int {
        return members.count
    }

    func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?, row: Int) -> NSView? {
        guard row < members.count else { return nil }
        let member = members[row]
        let cell = NSTextField(labelWithString: "")
        cell.lineBreakMode = .byTruncatingTail

        switch tableColumn?.identifier.rawValue {
        case "name":
            cell.stringValue = member.name
        case "email":
            cell.stringValue = member.email
        case "permission":
            cell.stringValue = member.permission
        default:
            break
        }
        return cell
    }

    @objc private func dismissSheet() {
        dismiss(nil)
    }
}
