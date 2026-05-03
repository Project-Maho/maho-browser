import AppKit
import WebKit

final class AddToEaselService {

    static let shared = AddToEaselService()
    private init() {}

    func addTabToEasel(tabId: TabId) {
        guard let webView = WebViewRegistry.shared.webView(for: tabId) else {
            showError("No Web View", "Could not find the web view for this tab. The tab may have been closed or not yet loaded.")
            return
        }

        captureSnapshot(webView: webView) { [weak self] image in
            guard let self = self, let image = image else {
                self?.showError("Capture Failed", "Could not capture a screenshot of this tab.")
                return
            }
            self.pickEaselAndAppend(image: image)
        }
    }

    // MARK: - Capture

    private func captureSnapshot(webView: WKWebView, completion: @escaping (NSImage?) -> Void) {
        let config = WKSnapshotConfiguration()
        webView.takeSnapshot(with: config) { image, _ in
            DispatchQueue.main.async {
                completion(image)
            }
        }
    }

    // MARK: - Easel Picker

    private func pickEaselAndAppend(image: NSImage) {
        let easels = loadEaselViewModels()

        let alert = NSAlert()
        alert.messageText = "Add to Easel"
        alert.informativeText = "Choose an existing easel or create a new one."
        alert.alertStyle = .informational
        alert.addButton(withTitle: "Add")
        alert.addButton(withTitle: "Cancel")

        let accessoryView = NSView(frame: NSRect(x: 0, y: 0, width: 280, height: 56))

        let popup = NSPopUpButton(frame: NSRect(x: 0, y: 28, width: 280, height: 24), pullsDown: false)
        for easel in easels {
            popup.addItem(withTitle: easel.name)
        }
        popup.addItem(withTitle: "— New Easel —")
        accessoryView.addSubview(popup)

        let nameField = NSTextField(frame: NSRect(x: 0, y: 0, width: 280, height: 24))
        nameField.placeholderString = "New easel name"
        nameField.stringValue = ""
        nameField.isHidden = !easels.isEmpty
        accessoryView.addSubview(nameField)

        alert.accessoryView = accessoryView

        let response = alert.runModal()
        guard response == .alertFirstButtonReturn else { return }

        let selectedIndex = popup.indexOfSelectedItem
        let isNewEasel = selectedIndex == easels.count

        if isNewEasel || easels.isEmpty {
            let name = nameField.stringValue.trimmingCharacters(in: .whitespaces)
            let easelName = name.isEmpty ? "Untitled Easel" : name
            createEaselAndAppend(name: easelName, image: image)
        } else {
            let easelVM = easels[selectedIndex]
            appendToExistingEasel(easelId: easelVM.id, image: image)
        }
    }

    // MARK: - Persist

    private func appendToExistingEasel(easelId: String, image: NSImage) {
        guard let json = MahoBridge.shared.getEasel(easelId: easelId),
              let easel: Easel = MahoBridge.shared.decode(json) else {
            showError("Load Failed", "Could not load the selected easel.")
            return
        }

        var items = easel.canvasItems
        let newItem = makeImageCanvasItem(from: image, existingItems: items)
        items.append(newItem)

        saveItems(items, easelId: easelId)
    }

    private func createEaselAndAppend(name: String, image: NSImage) {
        guard let json = MahoBridge.shared.createEasel(name: name),
              let easel: Easel = MahoBridge.shared.decode(json) else {
            showError("Create Failed", "Could not create a new easel.")
            return
        }

        let newItem = makeImageCanvasItem(from: image, existingItems: [])
        saveItems([newItem], easelId: easel.id)
    }

    private func saveItems(_ items: [CanvasItem], easelId: String) {
        let encoder = JSONEncoder()
        guard let data = try? encoder.encode(items),
              let json = String(data: data, encoding: .utf8) else {
            showError("Encode Error", "Could not encode the easel data.")
            return
        }

        let result = MahoBridge.shared.updateEasel(easelId: easelId, json: json)
        if result {
            let confirmation = NSAlert()
            confirmation.messageText = "Added to Easel"
            confirmation.informativeText = "The screenshot was saved to the easel."
            confirmation.alertStyle = .informational
            confirmation.addButton(withTitle: "OK")
            confirmation.runModal()
        } else {
            showError("Save Failed", "Could not save the screenshot to the easel.")
        }
    }

    // MARK: - Image Conversion

    private func makeImageCanvasItem(from image: NSImage, existingItems: [CanvasItem]) -> CanvasItem {
        let tiffData = image.tiffRepresentation ?? Data()
        let bitmap = NSBitmapImageRep(data: tiffData)
        let pngData = bitmap?.representation(using: .png, properties: [:]) ?? Data()

        let imageData = ImageData(
            data: [UInt8](pngData),
            width: UInt32(image.size.width),
            height: UInt32(image.size.height),
            format: .png
        )

        let offsetY = existingItems.reduce(0.0) { maxY, item in
            let itemBottom: Double
            switch item {
            case .note(_, let r), .image(_, let r), .webEmbed(_, let r), .drawing(_, let r):
                itemBottom = r.origin.y + r.size.height
            }
            return max(maxY, itemBottom)
        }

        let rect = Rect(
            origin: Point(x: 20, y: offsetY + 20),
            size: Size(width: Double(image.size.width), height: Double(image.size.height))
        )

        return .image(data: imageData, rect: rect)
    }

    // MARK: - Helpers

    private func loadEaselViewModels() -> [EaselViewModel] {
        guard let json = MahoBridge.shared.getEaselViewModels(),
              let models: [EaselViewModel] = MahoBridge.shared.decode(json) else {
            return []
        }
        return models
    }

    private func showError(_ title: String, _ message: String) {
        let alert = NSAlert()
        alert.messageText = title
        alert.informativeText = message
        alert.alertStyle = .warning
        alert.addButton(withTitle: "OK")
        alert.runModal()
    }
}
