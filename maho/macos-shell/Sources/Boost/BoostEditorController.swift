import AppKit

final class BoostEditorController: NSWindowController, CoreUpdateObserver {
    private let listTableView = NSTableView()
    private let listScrollView = NSScrollView()
    private let urlPatternField = NSTextField()
    private let cssTextView = NSTextView()
    private let jsTextView = NSTextView()
    private let enableCheckbox = NSButton(checkboxWithTitle: "Enabled", target: nil, action: nil)
    private let createButton = NSButton(title: "Create", target: nil, action: nil)
    private let updateButton = NSButton(title: "Update", target: nil, action: nil)
    private let deleteButton = NSButton(title: "Delete", target: nil, action: nil)
    private let exportButton = NSButton(title: "Export", target: nil, action: nil)
    private let importButton = NSButton(title: "Import", target: nil, action: nil)

    private var boosts: [BoostViewModel] = []
    private var selectedBoostId: BoostId?

    init() {
        let window = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 600, height: 500),
            styleMask: [.titled, .closable, .miniaturizable, .resizable],
            backing: .buffered,
            defer: false
        )
        window.title = "Boost Editor"
        window.center()
        window.minSize = NSSize(width: 500, height: 400)
        super.init(window: window)
        setupUI()
        CoreUpdateDispatcher.shared.register(self)
        reloadBoosts()
    }

    required init?(coder: NSCoder) {
        super.init(coder: coder)
        setupUI()
        CoreUpdateDispatcher.shared.register(self)
        reloadBoosts()
    }

    deinit {
        CoreUpdateDispatcher.shared.unregister(self)
    }

    private func setupUI() {
        guard let contentView = window?.contentView else { return }

        let splitView = NSSplitView()
        splitView.translatesAutoresizingMaskIntoConstraints = false
        splitView.dividerStyle = .thin
        splitView.isVertical = true
        contentView.addSubview(splitView)

        let leftView = NSView()
        leftView.translatesAutoresizingMaskIntoConstraints = false

        listTableView.delegate = self
        listTableView.dataSource = self
        listTableView.headerView = nil
        let column = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("BoostColumn"))
        column.title = "Boosts"
        listTableView.addTableColumn(column)

        listScrollView.translatesAutoresizingMaskIntoConstraints = false
        listScrollView.documentView = listTableView
        listScrollView.hasVerticalScroller = true
        leftView.addSubview(listScrollView)

        NSLayoutConstraint.activate([
            listScrollView.topAnchor.constraint(equalTo: leftView.topAnchor),
            listScrollView.leadingAnchor.constraint(equalTo: leftView.leadingAnchor),
            listScrollView.trailingAnchor.constraint(equalTo: leftView.trailingAnchor),
            listScrollView.bottomAnchor.constraint(equalTo: leftView.bottomAnchor),
        ])

        let rightView = NSView()
        rightView.translatesAutoresizingMaskIntoConstraints = false

        let urlLabel = NSTextField(labelWithString: "URL Pattern")
        urlLabel.translatesAutoresizingMaskIntoConstraints = false
        rightView.addSubview(urlLabel)

        urlPatternField.translatesAutoresizingMaskIntoConstraints = false
        urlPatternField.placeholderString = "*://example.com/*"
        rightView.addSubview(urlPatternField)

        enableCheckbox.translatesAutoresizingMaskIntoConstraints = false
        rightView.addSubview(enableCheckbox)

        let cssLabel = NSTextField(labelWithString: "Custom CSS")
        cssLabel.translatesAutoresizingMaskIntoConstraints = false
        rightView.addSubview(cssLabel)

        let cssScroll = NSScrollView()
        cssScroll.translatesAutoresizingMaskIntoConstraints = false
        cssScroll.hasVerticalScroller = true
        cssScroll.documentView = cssTextView
        cssTextView.isRichText = false
        cssTextView.font = NSFont.userFixedPitchFont(ofSize: 12)
        rightView.addSubview(cssScroll)

        let jsLabel = NSTextField(labelWithString: "Custom JS")
        jsLabel.translatesAutoresizingMaskIntoConstraints = false
        rightView.addSubview(jsLabel)

        let jsScroll = NSScrollView()
        jsScroll.translatesAutoresizingMaskIntoConstraints = false
        jsScroll.hasVerticalScroller = true
        jsScroll.documentView = jsTextView
        jsTextView.isRichText = false
        jsTextView.font = NSFont.userFixedPitchFont(ofSize: 12)
        rightView.addSubview(jsScroll)

        createButton.translatesAutoresizingMaskIntoConstraints = false
        createButton.target = self
        createButton.action = #selector(createBoostClicked(_:))
        rightView.addSubview(createButton)

        updateButton.translatesAutoresizingMaskIntoConstraints = false
        updateButton.target = self
        updateButton.action = #selector(updateBoostClicked(_:))
        rightView.addSubview(updateButton)

        deleteButton.translatesAutoresizingMaskIntoConstraints = false
        deleteButton.target = self
        deleteButton.action = #selector(deleteBoostClicked(_:))
        rightView.addSubview(deleteButton)

        exportButton.translatesAutoresizingMaskIntoConstraints = false
        exportButton.target = self
        exportButton.action = #selector(exportBoostClicked(_:))
        rightView.addSubview(exportButton)

        importButton.translatesAutoresizingMaskIntoConstraints = false
        importButton.target = self
        importButton.action = #selector(importBoostClicked(_:))
        rightView.addSubview(importButton)

        NSLayoutConstraint.activate([
            urlLabel.topAnchor.constraint(equalTo: rightView.topAnchor, constant: 12),
            urlLabel.leadingAnchor.constraint(equalTo: rightView.leadingAnchor, constant: 12),

            urlPatternField.topAnchor.constraint(equalTo: urlLabel.bottomAnchor, constant: 4),
            urlPatternField.leadingAnchor.constraint(equalTo: rightView.leadingAnchor, constant: 12),
            urlPatternField.trailingAnchor.constraint(equalTo: rightView.trailingAnchor, constant: -12),

            enableCheckbox.topAnchor.constraint(equalTo: urlPatternField.bottomAnchor, constant: 8),
            enableCheckbox.leadingAnchor.constraint(equalTo: rightView.leadingAnchor, constant: 12),

            cssLabel.topAnchor.constraint(equalTo: enableCheckbox.bottomAnchor, constant: 12),
            cssLabel.leadingAnchor.constraint(equalTo: rightView.leadingAnchor, constant: 12),

            cssScroll.topAnchor.constraint(equalTo: cssLabel.bottomAnchor, constant: 4),
            cssScroll.leadingAnchor.constraint(equalTo: rightView.leadingAnchor, constant: 12),
            cssScroll.trailingAnchor.constraint(equalTo: rightView.trailingAnchor, constant: -12),
            cssScroll.heightAnchor.constraint(equalToConstant: 100),

            jsLabel.topAnchor.constraint(equalTo: cssScroll.bottomAnchor, constant: 12),
            jsLabel.leadingAnchor.constraint(equalTo: rightView.leadingAnchor, constant: 12),

            jsScroll.topAnchor.constraint(equalTo: jsLabel.bottomAnchor, constant: 4),
            jsScroll.leadingAnchor.constraint(equalTo: rightView.leadingAnchor, constant: 12),
            jsScroll.trailingAnchor.constraint(equalTo: rightView.trailingAnchor, constant: -12),
            jsScroll.heightAnchor.constraint(equalToConstant: 100),

            createButton.topAnchor.constraint(equalTo: jsScroll.bottomAnchor, constant: 12),
            createButton.leadingAnchor.constraint(equalTo: rightView.leadingAnchor, constant: 12),

            updateButton.topAnchor.constraint(equalTo: jsScroll.bottomAnchor, constant: 12),
            updateButton.leadingAnchor.constraint(equalTo: createButton.trailingAnchor, constant: 8),

            deleteButton.topAnchor.constraint(equalTo: jsScroll.bottomAnchor, constant: 12),
            deleteButton.leadingAnchor.constraint(equalTo: updateButton.trailingAnchor, constant: 8),

            exportButton.topAnchor.constraint(equalTo: jsScroll.bottomAnchor, constant: 12),
            exportButton.leadingAnchor.constraint(equalTo: deleteButton.trailingAnchor, constant: 16),

            importButton.topAnchor.constraint(equalTo: jsScroll.bottomAnchor, constant: 12),
            importButton.leadingAnchor.constraint(equalTo: exportButton.trailingAnchor, constant: 8),
        ])

        splitView.addArrangedSubview(leftView)
        splitView.addArrangedSubview(rightView)

        NSLayoutConstraint.activate([
            splitView.topAnchor.constraint(equalTo: contentView.topAnchor),
            splitView.leadingAnchor.constraint(equalTo: contentView.leadingAnchor),
            splitView.trailingAnchor.constraint(equalTo: contentView.trailingAnchor),
            splitView.bottomAnchor.constraint(equalTo: contentView.bottomAnchor),
        ])
    }

    private func reloadBoosts() {
        reloadBoosts(showAlertOnFailure: true)
    }

    @discardableResult
    private func reloadBoosts(showAlertOnFailure: Bool) -> Bool {
        switch MahoBridge.shared.getBoostViewModelsResult() {
        case .success(let loadedBoosts):
            boosts = loadedBoosts
            listTableView.reloadData()
            return true
        case .failure(let error):
            if showAlertOnFailure {
                showBoostAlert(error.localizedDescription)
            }
            return false
        }
    }

    private func updateForm(boost: BoostViewModel?) {
        selectedBoostId = boost?.id
        urlPatternField.stringValue = boost?.urlPattern ?? ""
        cssTextView.string = boost?.customCss ?? ""
        jsTextView.string = boost?.customJs ?? ""
        enableCheckbox.state = (boost?.enabled ?? false) ? .on : .off
    }

    @objc private func createBoostClicked(_ sender: NSButton) {
        let pattern = UrlPattern(pattern: urlPatternField.stringValue, patternType: .glob)
        _ = sendBoostEventAndShowError(.createBoost(urlPattern: pattern))
        clearForm()
        reloadBoosts(showAlertOnFailure: true)
    }

    @objc private func updateBoostClicked(_ sender: NSButton) {
        guard let boostId = selectedBoostId else { return }
        let changes = BoostUpdate(
            urlPattern: UrlPattern(pattern: urlPatternField.stringValue, patternType: .glob),
            customCss: cssTextView.string.isEmpty ? nil : cssTextView.string,
            customJs: jsTextView.string.isEmpty ? nil : jsTextView.string,
            colorTheme: nil,
            fontOverride: nil,
            enabled: enableCheckbox.state == .on,
            spaceScope: nil
        )
        _ = sendBoostEventAndShowError(.updateBoost(boostId: boostId, changes: changes))
        reloadBoosts(showAlertOnFailure: true)
    }

    @objc private func deleteBoostClicked(_ sender: NSButton) {
        guard let boostId = selectedBoostId else { return }
        _ = sendBoostEventAndShowError(.deleteBoost(boostId: boostId))
        clearForm()
        reloadBoosts(showAlertOnFailure: true)
    }

    private func clearForm() {
        selectedBoostId = nil
        urlPatternField.stringValue = ""
        cssTextView.string = ""
        jsTextView.string = ""
        enableCheckbox.state = .off
    }

    @objc private func exportBoostClicked(_ sender: NSButton) {
        guard let boostId = selectedBoostId,
              let boost = boosts.first(where: { $0.id == boostId }) else { return }
        let exportDict: [String: String] = [
            "urlPattern": boost.urlPattern,
            "customCss": boost.customCss ?? "",
            "customJs": boost.customJs ?? "",
        ]
        guard let jsonData = try? JSONSerialization.data(withJSONObject: exportDict, options: [.prettyPrinted, .sortedKeys]),
              let jsonString = String(data: jsonData, encoding: .utf8) else { return }
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(jsonString, forType: .string)
    }

    @objc private func importBoostClicked(_ sender: NSButton) {
        guard let clipString = NSPasteboard.general.string(forType: .string),
              let data = clipString.data(using: .utf8),
              let dict = try? JSONSerialization.jsonObject(with: data) as? [String: String],
              let urlPatternStr = dict["urlPattern"], !urlPatternStr.isEmpty else { return }

        let pattern = UrlPattern(pattern: urlPatternStr, patternType: .glob)
        _ = sendBoostEventAndShowError(.createBoost(urlPattern: pattern))
        guard reloadBoosts(showAlertOnFailure: true) else { return }

        if let created = boosts.last {
            let css = dict["customCss"]
            let js = dict["customJs"]
            let changes = BoostUpdate(
                urlPattern: pattern,
                customCss: (css?.isEmpty ?? true) ? nil : css,
                customJs: (js?.isEmpty ?? true) ? nil : js,
                colorTheme: nil,
                fontOverride: nil,
                enabled: true,
                spaceScope: nil
            )
            _ = sendBoostEventAndShowError(.updateBoost(boostId: created.id, changes: changes))
            reloadBoosts(showAlertOnFailure: true)
        }
    }

    @discardableResult
    private func sendBoostEventAndShowError(_ event: ShellEvent) -> Bool {
        let updates = MahoBridge.shared.sendEvent(event)
        CoreUpdateDispatcher.shared.dispatch(updates)

        if let errorUpdate = updates.first(where: {
            if case .error = $0 { return true }
            return false
        }), case .error(let context, let error) = errorUpdate {
            showBoostAlert("\(context): \(error.message) (\(error.code))")
            return false
        }

        return true
    }

    private func showBoostAlert(_ message: String) {
        let alert = NSAlert()
        alert.messageText = "Boost Editor"
        alert.informativeText = message
        alert.alertStyle = .warning
        alert.addButton(withTitle: "OK")
        alert.runModal()
    }

    func handleCoreUpdate(_ update: CoreUpdate) {
        switch update {
        case .fullState:
            reloadBoosts()
        default:
            break
        }
    }
}

extension BoostEditorController: NSTableViewDelegate, NSTableViewDataSource {
    func numberOfRows(in tableView: NSTableView) -> Int {
        return boosts.count
    }

    func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?, row: Int) -> NSView? {
        let cellIdentifier = NSUserInterfaceItemIdentifier("BoostCell")
        var cell = tableView.makeView(withIdentifier: cellIdentifier, owner: self) as? NSTableCellView
        if cell == nil {
            cell = NSTableCellView()
            cell?.identifier = cellIdentifier
            let textField = NSTextField(labelWithString: "")
            textField.translatesAutoresizingMaskIntoConstraints = false
            cell?.textField = textField
            cell?.addSubview(textField)
            NSLayoutConstraint.activate([
                textField.leadingAnchor.constraint(equalTo: cell!.leadingAnchor, constant: 4),
                textField.trailingAnchor.constraint(equalTo: cell!.trailingAnchor, constant: -4),
                textField.centerYAnchor.constraint(equalTo: cell!.centerYAnchor),
            ])
        }
        if row < boosts.count {
            let boost = boosts[row]
            cell?.textField?.stringValue = "\(boost.urlPattern) \(boost.enabled ? "✓" : "✗")"
        }
        return cell
    }

    func tableViewSelectionDidChange(_ notification: Notification) {
        let row = listTableView.selectedRow
        if row >= 0, row < boosts.count {
            updateForm(boost: boosts[row])
        } else {
            updateForm(boost: nil)
        }
    }
}
