import AppKit

final class EaselEditorController: NSWindowController {
    var easelId: EaselId?
    var easelName: String = "Untitled Easel"

    private let canvasView = EaselCanvasView(frame: .zero)

    init(easelId: EaselId? = nil, name: String = "Untitled Easel") {
        self.easelId = easelId
        self.easelName = name

        let window = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 800, height: 600),
            styleMask: [.titled, .closable, .miniaturizable, .resizable],
            backing: .buffered,
            defer: false
        )
        window.title = "Easel \u{2014} \(name)"
        window.center()
        super.init(window: window)
        setupUI()

        if let eid = easelId {
            loadEasel(id: eid)
        }
    }

    required init?(coder: NSCoder) {
        super.init(coder: coder)
    }

    private func setupUI() {
        guard let contentView = window?.contentView else { return }

        let addNoteBtn = NSButton(title: "Add Note", target: self, action: #selector(addNote))
        let addImageBtn = NSButton(title: "Add Image", target: self, action: #selector(addImage))
        let captureBtn = NSButton(title: "Capture", target: self, action: #selector(capturePlaceholder))
        let deleteBtn = NSButton(title: "Delete", target: self, action: #selector(deleteSelected))
        let saveBtn = NSButton(title: "Save", target: self, action: #selector(saveEasel))

        let toolbar = NSStackView(views: [addNoteBtn, addImageBtn, captureBtn, deleteBtn, saveBtn])
        toolbar.orientation = .horizontal
        toolbar.spacing = 8
        toolbar.translatesAutoresizingMaskIntoConstraints = false

        canvasView.translatesAutoresizingMaskIntoConstraints = false
        canvasView.wantsLayer = true
        canvasView.layer?.backgroundColor = NSColor.textBackgroundColor.cgColor

        contentView.addSubview(toolbar)
        contentView.addSubview(canvasView)

        NSLayoutConstraint.activate([
            toolbar.topAnchor.constraint(equalTo: contentView.topAnchor, constant: 8),
            toolbar.leadingAnchor.constraint(equalTo: contentView.leadingAnchor, constant: 8),

            canvasView.topAnchor.constraint(equalTo: toolbar.bottomAnchor, constant: 8),
            canvasView.leadingAnchor.constraint(equalTo: contentView.leadingAnchor),
            canvasView.trailingAnchor.constraint(equalTo: contentView.trailingAnchor),
            canvasView.bottomAnchor.constraint(equalTo: contentView.bottomAnchor),
        ])
    }

    func loadEasel(id: EaselId) {
        easelId = id
        guard let json = MahoBridge.shared.getEasel(easelId: id),
              let easel: Easel = MahoBridge.shared.decode(json) else {
            let alert = NSAlert()
            alert.messageText = "Failed to Load Easel"
            alert.informativeText = "Could not load the requested easel. It may have been deleted or the data is corrupt."
            alert.alertStyle = .warning
            alert.addButton(withTitle: "OK")
            alert.runModal()
            return
        }
        easelName = easel.name
        window?.title = "Easel \u{2014} \(easel.name)"
        canvasView.items = easel.canvasItems
        canvasView.scale = CGFloat(easel.viewport.zoom)
        canvasView.offset = NSPoint(x: easel.viewport.offset.x, y: easel.viewport.offset.y)
    }

    func createNewEasel() {
        guard let json = MahoBridge.shared.createEasel(name: easelName),
              let easel: Easel = MahoBridge.shared.decode(json) else {
            let alert = NSAlert()
            alert.messageText = "Failed to Create Easel"
            alert.informativeText = "Could not create a new easel. Please try again."
            alert.alertStyle = .warning
            alert.addButton(withTitle: "OK")
            alert.runModal()
            return
        }
        easelId = easel.id
        window?.title = "Easel \u{2014} \(easel.name)"
    }

    @objc private func addNote() {
        let center = NSPoint(
            x: canvasView.bounds.midX / canvasView.scale - canvasView.offset.x / canvasView.scale,
            y: canvasView.bounds.midY / canvasView.scale - canvasView.offset.y / canvasView.scale
        )
        canvasView.addNoteItem(at: center)
    }

    @objc private func addImage() {
        let panel = NSOpenPanel()
        panel.allowedContentTypes = [.image]
        panel.allowsMultipleSelection = false
        guard panel.runModal() == .OK, let url = panel.url,
              let image = NSImage(contentsOf: url) else { return }
        let center = NSPoint(
            x: canvasView.bounds.midX / canvasView.scale - canvasView.offset.x / canvasView.scale,
            y: canvasView.bounds.midY / canvasView.scale - canvasView.offset.y / canvasView.scale
        )
        canvasView.addImageItem(image: image, at: center)
    }

    @objc private func capturePlaceholder() {
        let alert = NSAlert()
        alert.messageText = "Capture Not Available Here"
        alert.informativeText = "Web page capture is not supported from the editor toolbar. To add web content, right-click on a page in the browser and select \"Add to Easel\"."
        alert.alertStyle = .informational
        alert.addButton(withTitle: "OK")
        alert.runModal()
    }

    @objc private func deleteSelected() {
        canvasView.removeSelectedItem()
    }

    @objc private func saveEasel() {
        guard let eid = easelId else {
            let alert = NSAlert()
            alert.messageText = "Cannot Save"
            alert.informativeText = "This easel has not been created yet. Add content and try again."
            alert.alertStyle = .warning
            alert.addButton(withTitle: "OK")
            alert.runModal()
            return
        }
        let encoder = JSONEncoder()
        guard let data = try? encoder.encode(canvasView.items),
              let json = String(data: data, encoding: .utf8) else {
            let alert = NSAlert()
            alert.messageText = "Save Error"
            alert.informativeText = "Could not encode easel data for saving."
            alert.alertStyle = .warning
            alert.addButton(withTitle: "OK")
            alert.runModal()
            return
        }
        let result = MahoBridge.shared.updateEasel(easelId: eid, json: json)
        if !result {
            let alert = NSAlert()
            alert.messageText = "Save Failed"
            alert.informativeText = "Could not save the easel. Please try again."
            alert.alertStyle = .warning
            alert.addButton(withTitle: "OK")
            alert.runModal()
        }
    }
}
