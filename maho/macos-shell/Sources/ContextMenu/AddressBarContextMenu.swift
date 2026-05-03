import AppKit

import AppKit

final class AddressBarContextMenu {
    static func menu(for currentUrl: String, onEditAddress: @escaping () -> Void) -> NSMenu {
        let menu = NSMenu()

        menu.addItem(NSMenuItem(title: "Edit Address…", action: onEditAddress))

        let copyItem = NSMenuItem(title: "Copy URL", action: {
            NSPasteboard.general.clearContents()
            NSPasteboard.general.setString(currentUrl, forType: .string)
        })
        copyItem.isEnabled = !currentUrl.isEmpty
        menu.addItem(copyItem)

        return menu
    }
}

private extension NSMenuItem {
    convenience init(title: String, action: @escaping () -> Void) {
        self.init(title: title, action: #selector(MenuActionTarget.performAction), keyEquivalent: "")
        let target = MenuActionTarget(action: action)
        self.target = target
        self.representedObject = target
    }
}

private final class MenuActionTarget: NSObject {
    private let action: () -> Void

    init(action: @escaping () -> Void) {
        self.action = action
    }

    @objc func performAction() {
        action()
    }
}
