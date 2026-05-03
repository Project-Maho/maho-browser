import AppKit

final class NotesWindowController: NSWindowController, CoreUpdateObserver {
    private let listTableView = NSTableView()
    private let listScrollView = NSScrollView()
    private let editorTextView = NSTextView()
    private let editorScrollView = NSScrollView()
    private let newNoteButton = NSButton(title: "New Note", target: nil, action: nil)
    private let deleteButton = NSButton(title: "Delete", target: nil, action: nil)
    private let linkTabButton = NSButton(title: "Link to Tab", target: nil, action: nil)

    private var notes: [NoteViewModel] = []
    private var selectedNoteIndex: Int = -1
    private var debounceTimer: Timer?

    init() {
        let window = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 600, height: 400),
            styleMask: [.titled, .closable, .miniaturizable, .resizable],
            backing: .buffered,
            defer: false
        )
        window.title = "Notes"
        window.center()
        window.minSize = NSSize(width: 400, height: 300)
        window.appearance = NSAppearance(named: .darkAqua)
        window.backgroundColor = MahoAppColors.windowBackground
        super.init(window: window)
        setupUI()
        CoreUpdateDispatcher.shared.register(self)
        reloadNotes()
    }

    required init?(coder: NSCoder) {
        super.init(coder: coder)
        setupUI()
        CoreUpdateDispatcher.shared.register(self)
        reloadNotes()
    }

    deinit {
        debounceTimer?.invalidate()
        CoreUpdateDispatcher.shared.unregister(self)
    }

    private func setupUI() {
        guard let contentView = window?.contentView else { return }
        contentView.wantsLayer = true
        contentView.layer?.backgroundColor = MahoAppColors.windowBackground.cgColor

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
        listTableView.backgroundColor = .clear
        let column = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("NoteColumn"))
        column.title = "Notes"
        listTableView.addTableColumn(column)

        listScrollView.translatesAutoresizingMaskIntoConstraints = false
        listScrollView.documentView = listTableView
        listScrollView.hasVerticalScroller = true
        listScrollView.drawsBackground = false
        leftView.addSubview(listScrollView)

        let buttonStack = NSStackView(views: [newNoteButton, deleteButton])
        buttonStack.translatesAutoresizingMaskIntoConstraints = false
        buttonStack.orientation = .horizontal
        buttonStack.spacing = 4
        leftView.addSubview(buttonStack)

        newNoteButton.target = self
        newNoteButton.action = #selector(newNoteClicked(_:))
        newNoteButton.bezelStyle = .rounded
        newNoteButton.font = .systemFont(ofSize: 11)

        deleteButton.target = self
        deleteButton.action = #selector(deleteNoteClicked(_:))
        deleteButton.bezelStyle = .rounded
        deleteButton.font = .systemFont(ofSize: 11)

        NSLayoutConstraint.activate([
            listScrollView.topAnchor.constraint(equalTo: leftView.topAnchor),
            listScrollView.leadingAnchor.constraint(equalTo: leftView.leadingAnchor),
            listScrollView.trailingAnchor.constraint(equalTo: leftView.trailingAnchor),
            listScrollView.bottomAnchor.constraint(equalTo: buttonStack.topAnchor, constant: -4),

            buttonStack.leadingAnchor.constraint(equalTo: leftView.leadingAnchor, constant: 8),
            buttonStack.bottomAnchor.constraint(equalTo: leftView.bottomAnchor, constant: -8),
        ])

        let rightView = NSView()
        rightView.translatesAutoresizingMaskIntoConstraints = false

        editorScrollView.translatesAutoresizingMaskIntoConstraints = false
        editorScrollView.hasVerticalScroller = true
        editorScrollView.drawsBackground = false
        editorScrollView.documentView = editorTextView
        editorTextView.isRichText = false
        editorTextView.font = .systemFont(ofSize: 13)
        editorTextView.textColor = MahoAppColors.primaryText
        editorTextView.backgroundColor = MahoAppColors.contentBackground
        editorTextView.delegate = self
        editorTextView.isEditable = true
        editorTextView.isSelectable = true
        editorTextView.textContainerInset = NSSize(width: 8, height: 8)
        rightView.addSubview(editorScrollView)

        linkTabButton.translatesAutoresizingMaskIntoConstraints = false
        linkTabButton.target = self
        linkTabButton.action = #selector(linkTabClicked(_:))
        linkTabButton.bezelStyle = .rounded
        linkTabButton.font = .systemFont(ofSize: 11)
        rightView.addSubview(linkTabButton)

        NSLayoutConstraint.activate([
            editorScrollView.topAnchor.constraint(equalTo: rightView.topAnchor),
            editorScrollView.leadingAnchor.constraint(equalTo: rightView.leadingAnchor),
            editorScrollView.trailingAnchor.constraint(equalTo: rightView.trailingAnchor),
            editorScrollView.bottomAnchor.constraint(equalTo: linkTabButton.topAnchor, constant: -4),

            linkTabButton.trailingAnchor.constraint(equalTo: rightView.trailingAnchor, constant: -8),
            linkTabButton.bottomAnchor.constraint(equalTo: rightView.bottomAnchor, constant: -8),
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

    private func reloadNotes() {
        notes = MahoBridge.shared.getNoteViewModels()
        listTableView.reloadData()

        if selectedNoteIndex >= 0, selectedNoteIndex < notes.count {
            editorTextView.string = notes[selectedNoteIndex].content
        } else {
            editorTextView.string = ""
            selectedNoteIndex = -1
        }
    }

    @objc private func newNoteClicked(_ sender: NSButton) {
        let notesApp = UserDefaults.standard.string(forKey: "maho_default_notes_app") ?? "maho"
        switch notesApp {
        case "apple_notes":
            NSWorkspace.shared.open(URL(string: "notes://")!)
            return
        case "notion":
            NSWorkspace.shared.open(URL(string: "https://notion.new")!)
            return
        case "google_docs":
            NSWorkspace.shared.open(URL(string: "https://docs.new")!)
            return
        default:
            break // Continue with Maho's built-in notes
        }
        CoreUpdateDispatcher.shared.sendAndDispatch(.createNote(linkedTab: nil, content: ""))
        reloadNotes()
    }

    @objc private func deleteNoteClicked(_ sender: NSButton) {
        guard selectedNoteIndex >= 0, selectedNoteIndex < notes.count else { return }
        let noteId = notes[selectedNoteIndex].id
        CoreUpdateDispatcher.shared.sendAndDispatch(.deleteNote(noteId: noteId))
        selectedNoteIndex = -1
        reloadNotes()
    }

    @objc private func linkTabClicked(_ sender: NSButton) {
        guard selectedNoteIndex >= 0, selectedNoteIndex < notes.count else { return }
        let noteId = notes[selectedNoteIndex].id
        CoreUpdateDispatcher.shared.sendAndDispatch(.updateNote(noteId: noteId, content: editorTextView.string))
    }

    private func scheduleDebounce() {
        debounceTimer?.invalidate()
        debounceTimer = Timer.scheduledTimer(withTimeInterval: 0.5, repeats: false) { [weak self] _ in
            self?.saveCurrentNote()
        }
    }

    private func saveCurrentNote() {
        guard selectedNoteIndex >= 0, selectedNoteIndex < notes.count else { return }
        let noteId = notes[selectedNoteIndex].id
        let content = editorTextView.string
        CoreUpdateDispatcher.shared.sendAndDispatch(.updateNote(noteId: noteId, content: content))
    }

    func handleCoreUpdate(_ update: CoreUpdate) {
        switch update {
        case .fullState:
            DispatchQueue.main.async { [weak self] in self?.reloadNotes() }
        default:
            break
        }
    }
}

extension NotesWindowController: NSTableViewDelegate, NSTableViewDataSource {
    func numberOfRows(in tableView: NSTableView) -> Int {
        return notes.count
    }

    func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?, row: Int) -> NSView? {
        guard row < notes.count else { return nil }
        let note = notes[row]
        let firstLine = note.content.components(separatedBy: .newlines).first ?? ""
        let title = firstLine.isEmpty ? "Untitled Note" : (firstLine.count > 40 ? String(firstLine.prefix(40)) + "..." : firstLine)

        let cellIdentifier = NSUserInterfaceItemIdentifier("NoteCell")
        var cell = tableView.makeView(withIdentifier: cellIdentifier, owner: self) as? NSTableCellView
        if cell == nil {
            cell = NSTableCellView()
            cell?.identifier = cellIdentifier
            let textField = NSTextField(labelWithString: "")
            textField.translatesAutoresizingMaskIntoConstraints = false
            textField.textColor = MahoAppColors.primaryText
            textField.font = .systemFont(ofSize: 12)
            cell?.textField = textField
            cell?.addSubview(textField)
            NSLayoutConstraint.activate([
                textField.leadingAnchor.constraint(equalTo: cell!.leadingAnchor, constant: 4),
                textField.trailingAnchor.constraint(equalTo: cell!.trailingAnchor, constant: -4),
                textField.centerYAnchor.constraint(equalTo: cell!.centerYAnchor),
            ])
        }
        cell?.textField?.stringValue = title
        return cell
    }

    func tableViewSelectionDidChange(_ notification: Notification) {
        let row = listTableView.selectedRow
        if row >= 0, row < notes.count {
            selectedNoteIndex = row
            editorTextView.string = notes[row].content
        } else {
            selectedNoteIndex = -1
            editorTextView.string = ""
        }
    }
}

extension NotesWindowController: NSTextViewDelegate {
    func textDidChange(_ notification: Notification) {
        scheduleDebounce()
    }
}
