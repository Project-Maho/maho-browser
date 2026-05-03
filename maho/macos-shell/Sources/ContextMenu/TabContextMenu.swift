import AppKit

final class TabContextMenu {
    static func menu(for tab: TabViewModel, spaceId: SpaceId, spaces: [SpaceViewModel]) -> NSMenu {
        let menu = NSMenu()

        menu.addItem(NSMenuItem(title: "Close Tab", action: {
            CoreUpdateDispatcher.shared.sendAndDispatch(.closeTab(tabId: tab.id))
        }))

        menu.addItem(NSMenuItem(title: "Close Other Tabs", action: {
            CoreUpdateDispatcher.shared.sendAndDispatch(.closeOtherTabs(spaceId: spaceId, tabId: tab.id))
        }))

        menu.addItem(NSMenuItem(title: "Close Tabs to Right", action: {
            CoreUpdateDispatcher.shared.sendAndDispatch(.closeTabsToRight(spaceId: spaceId, tabId: tab.id))
        }))

        menu.addItem(NSMenuItem(title: "Close Tabs to Left", action: {
            CoreUpdateDispatcher.shared.sendAndDispatch(.closeTabsToLeft(spaceId: spaceId, tabId: tab.id))
        }))

        menu.addItem(.separator())

        menu.addItem(NSMenuItem(title: "Duplicate", action: {
            CoreUpdateDispatcher.shared.sendAndDispatch(.duplicateTab(tabId: tab.id))
        }))

        menu.addItem(NSMenuItem(title: tab.isPinned ? "Unpin" : "Pin", action: {
            if tab.isPinned {
                CoreUpdateDispatcher.shared.sendAndDispatch(.unpinTab(tabId: tab.id))
            } else {
                CoreUpdateDispatcher.shared.sendAndDispatch(.pinTab(tabId: tab.id))
            }
        }))

        menu.addItem(NSMenuItem(title: tab.isMuted ? "Unmute" : "Mute", action: {
            if tab.isMuted {
                CoreUpdateDispatcher.shared.sendAndDispatch(.unmuteTab(tabId: tab.id))
            } else {
                CoreUpdateDispatcher.shared.sendAndDispatch(.muteTab(tabId: tab.id))
            }
        }))

        let moveMenu = NSMenu(title: "Move to Space")
        for space in spaces where space.id != spaceId {
            let item = NSMenuItem(title: space.name, action: {
                CoreUpdateDispatcher.shared.sendAndDispatch(.moveTab(tabId: tab.id, targetSpace: space.id, position: -1))
            })
            moveMenu.addItem(item)
        }
        let moveItem = NSMenuItem(title: "Move to Space", action: nil, keyEquivalent: "")
        moveItem.submenu = moveMenu
        menu.addItem(moveItem)

        menu.addItem(NSMenuItem(title: tab.isFavorite ? "Remove from Favorites" : "Add to Favorites", action: {
            if tab.isFavorite {
                CoreUpdateDispatcher.shared.sendAndDispatch(.unfavoriteTab(tabId: tab.id))
            } else {
                CoreUpdateDispatcher.shared.sendAndDispatch(.favoriteTab(tabId: tab.id))
            }
            DispatchQueue.main.async {
                NotificationCenter.default.post(name: .mahoSidebarRefreshRequested, object: nil)
            }
        }))

        menu.addItem(.separator())

        menu.addItem(NSMenuItem(title: "Copy URL", action: {
            NSPasteboard.general.clearContents()
            NSPasteboard.general.setString(tab.url, forType: .string)
        }))

        menu.addItem(NSMenuItem(title: "Reload", action: {
            CoreUpdateDispatcher.shared.sendAndDispatch(.reload(tabId: tab.id))
        }))

        menu.addItem(NSMenuItem(title: tab.lifecycleState == "frozen" ? "Unfreeze" : "Freeze", action: {
            if tab.lifecycleState == "frozen" {
                CoreUpdateDispatcher.shared.sendAndDispatch(.unfreezeTab(tabId: tab.id))
            } else {
                CoreUpdateDispatcher.shared.sendAndDispatch(.freezeTab(tabId: tab.id))
            }
        }))

        menu.addItem(.separator())

        menu.addItem(NSMenuItem(title: "Add to Easel", action: {
            AddToEaselService.shared.addTabToEasel(tabId: tab.id)
        }))

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
