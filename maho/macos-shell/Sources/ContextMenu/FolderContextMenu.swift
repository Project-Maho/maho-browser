import AppKit

import AppKit

final class FolderContextMenu {
    static func menu(for folder: FolderViewModel, spaceId: SpaceId) -> NSMenu {
        let menu = NSMenu()

        menu.addItem(NSMenuItem(title: "Rename", action: {
            guard let newName = promptForFolderRename(currentName: folder.name) else { return }
            CoreUpdateDispatcher.shared.sendAndDispatch(.renameFolder(spaceId: spaceId, folderId: folder.id, name: newName))
        }))

        menu.addItem(NSMenuItem(title: "Delete", action: {
            CoreUpdateDispatcher.shared.sendAndDispatch(.deleteFolder(spaceId: spaceId, folderId: folder.id))
        }))

        menu.addItem(NSMenuItem(title: folder.isPinned ? "Unpin Folder" : "Pin Folder", action: {
            if folder.isPinned {
                CoreUpdateDispatcher.shared.sendAndDispatch(.unpinFolder(spaceId: spaceId, folderId: folder.id))
            } else {
                CoreUpdateDispatcher.shared.sendAndDispatch(.pinFolder(spaceId: spaceId, folderId: folder.id))
            }
        }))

        menu.addItem(NSMenuItem(title: "New Tab in Folder", action: {
            (NSApp.delegate as? AppDelegate)?.showCommandBarForNewTab(spaceId: spaceId, folderId: folder.id)
        }))

        return menu
    }

    private static func promptForFolderRename(currentName: String) -> String? {
        let alert = NSAlert()
        alert.messageText = "Rename Folder"
        alert.informativeText = "Enter a new name for \"\(currentName)\"."
        alert.addButton(withTitle: "Rename")
        alert.addButton(withTitle: "Cancel")

        let textField = NSTextField(frame: NSRect(x: 0, y: 0, width: 240, height: 24))
        textField.stringValue = currentName
        alert.accessoryView = textField
        alert.window.initialFirstResponder = textField

        guard alert.runModal() == .alertFirstButtonReturn else { return nil }
        let trimmed = textField.stringValue.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty, trimmed != currentName else { return nil }
        return trimmed
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
