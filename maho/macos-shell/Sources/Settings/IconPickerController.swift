import AppKit

final class IconPickerController: NSViewController {
    private var iconsGrid: NSCollectionView!
    private let iconNames = ["AppIcon", "AppIcon-Dark", "AppIcon-Minimal", "AppIcon-Neon", "AppIcon-Classic"]
    
    override func loadView() {
        view = NSView(frame: NSRect(x: 0, y: 0, width: 400, height: 300))
        
        let label = NSTextField(labelWithString: "Choose App Icon")
        label.font = .systemFont(ofSize: 16, weight: .semibold)
        label.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(label)
        
        // Create a simple grid of buttons, each with an icon preview
        let stack = NSStackView()
        stack.orientation = .horizontal
        stack.spacing = 16
        stack.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(stack)
        
        for (index, name) in iconNames.enumerated() {
            let button = NSButton(frame: NSRect(x: 0, y: 0, width: 64, height: 64))
            button.bezelStyle = .regularSquare
            button.isBordered = true
            button.title = ""
            // Use app icon or a colored placeholder
            let placeholder = NSImage(size: NSSize(width: 48, height: 48), flipped: false) { rect in
                let colors: [NSColor] = [.systemBlue, .systemPurple, .systemGreen, .systemOrange, .systemRed]
                colors[index % colors.count].setFill()
                NSBezierPath(roundedRect: rect.insetBy(dx: 4, dy: 4), xRadius: 8, yRadius: 8).fill()
                return true
            }
            button.image = placeholder
            button.imagePosition = .imageOnly
            button.tag = index
            button.target = self
            button.action = #selector(iconSelected(_:))
            stack.addArrangedSubview(button)
            _ = name
        }
        
        NSLayoutConstraint.activate([
            label.topAnchor.constraint(equalTo: view.topAnchor, constant: 16),
            label.centerXAnchor.constraint(equalTo: view.centerXAnchor),
            stack.topAnchor.constraint(equalTo: label.bottomAnchor, constant: 20),
            stack.centerXAnchor.constraint(equalTo: view.centerXAnchor),
        ])
    }
    
    @objc private func iconSelected(_ sender: NSButton) {
        let name = iconNames[sender.tag]
        UserDefaults.standard.set(name, forKey: "maho_selected_app_icon")
        NSApp.applicationIconImage = NSImage(named: NSImage.Name(name)) ?? sender.image
        // Note: NSApp.applicationIconImage can be set but won't persist across launches
        // Real icon switching requires alternate icon assets in the bundle
    }
}
