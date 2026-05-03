import AppKit

final class ShortcutDispatcher {
    static let shared = ShortcutDispatcher()

    private var eventMonitor: Any?
    private var menuItemRegistry: [String: NSMenuItem] = [:]

    // MARK: - Lifecycle

    func start() {
        syncMenuShortcuts()
        installEventMonitor()
    }

    func stop() {
        if let monitor = eventMonitor {
            NSEvent.removeMonitor(monitor)
            eventMonitor = nil
        }
    }

    // MARK: - Menu Registration

    func registerMenuItem(_ item: NSMenuItem, forAction action: String) {
        menuItemRegistry[action] = item
    }

    // MARK: - Menu Sync

    func syncMenuShortcuts() {
        guard let json = MahoBridge.shared.getShortcuts(),
              let bindings: [ShortcutBinding] = MahoBridge.shared.decode(json) else { return }

        for binding in bindings where binding.enabled {
            guard let menuItem = menuItemRegistry[binding.action] else { continue }
            menuItem.keyEquivalent = binding.keyCombo.key
            menuItem.keyEquivalentModifierMask = Self.modifierFlags(from: binding.keyCombo.modifiers)
        }
    }

    // MARK: - Event Monitor

    private func installEventMonitor() {
        eventMonitor = NSEvent.addLocalMonitorForEvents(matching: .keyDown) { [weak self] event in
            if let self = self, self.handleKeyEvent(event) {
                return nil
            }
            return event
        }
    }

    private func handleKeyEvent(_ event: NSEvent) -> Bool {
        let key = event.charactersIgnoringModifiers?.lowercased() ?? ""
        if let responder = NSApp.keyWindow?.firstResponder,
           responder is NSTextView {
            guard event.modifierFlags.contains(.command) else { return false }
        }

        guard !key.isEmpty else { return false }

        var modifiers: [KeyModifier] = []
        if event.modifierFlags.contains(.control) { modifiers.append(.ctrl) }
        if event.modifierFlags.contains(.shift) { modifiers.append(.shift) }
        if event.modifierFlags.contains(.option) { modifiers.append(.alt) }
        if event.modifierFlags.contains(.command) { modifiers.append(.meta) }

        let combo = KeyCombo(key: key, modifiers: modifiers)
        if let comboJson = MahoBridge.shared.encode(combo),
           let actionStr = MahoBridge.shared.resolveShortcut(keyComboJson: comboJson) {
            if menuItemRegistry[actionStr] != nil { return false }
            return dispatchAction(actionStr)
        }

        return handleBuiltInShortcut(event, key: key, modifiers: modifiers)
    }

    // MARK: - Action Dispatch

    private func dispatchAction(_ action: String) -> Bool {
        switch action {
        case "activate_tab_1": return activateTabByIndex(0)
        case "activate_tab_2": return activateTabByIndex(1)
        case "activate_tab_3": return activateTabByIndex(2)
        case "activate_tab_4": return activateTabByIndex(3)
        case "activate_tab_5": return activateTabByIndex(4)
        case "activate_tab_6": return activateTabByIndex(5)
        case "activate_tab_7": return activateTabByIndex(6)
        case "activate_tab_8": return activateTabByIndex(7)
        case "activate_tab_9": return activateTabByIndex(8)

        case "focus_space_1": return activateSpaceByIndex(0)
        case "focus_space_2": return activateSpaceByIndex(1)
        case "focus_space_3": return activateSpaceByIndex(2)
        case "focus_space_4": return activateSpaceByIndex(3)
        case "focus_space_5": return activateSpaceByIndex(4)
        case "focus_space_6": return activateSpaceByIndex(5)
        case "focus_space_7": return activateSpaceByIndex(6)
        case "focus_space_8": return activateSpaceByIndex(7)
        case "focus_space_9": return activateSpaceByIndex(8)

        case "next_tab": return switchTab(direction: .next)
        case "prev_tab": return switchTab(direction: .previous)

        case "new_window":
            guard let appDelegate = NSApp.delegate as? AppDelegate else { return false }
            appDelegate.newBrowserWindow()
            return true

        case "little_arc":
            guard let appDelegate = NSApp.delegate as? AppDelegate else { return false }
            appDelegate.newLittleArcWindow()
            return true

        case "focus_url_bar": return focusURLBar()
        case "toggle_archive_panel": return toggleArchivePanel()

        case "add_split_view": return dispatchQuickAction(.custom(actionId: "add_split_view"))
        case "remove_split_view": return dispatchQuickAction(.custom(actionId: "remove_split_view"))

        default:
            return dispatchQuickActionByString(action)
        }
    }

    private func handleBuiltInShortcut(_ event: NSEvent, key: String, modifiers: [KeyModifier]) -> Bool {
        let hasCmd = modifiers.contains(.meta)
        let hasCtrl = modifiers.contains(.ctrl)
        let hasShift = modifiers.contains(.shift)
        let hasOpt = modifiers.contains(.alt)

        // ⌘1-9: Tab index switching
        if hasCmd && !hasCtrl && !hasOpt && !hasShift,
           let digit = Int(key), digit >= 1 && digit <= 9 {
            return activateTabByIndex(digit - 1)
        }

        // Ctrl+1-9: Space index switching
        if hasCtrl && !hasCmd && !hasOpt && !hasShift,
           let digit = Int(key), digit >= 1 && digit <= 9 {
            return activateSpaceByIndex(digit - 1)
        }

        // ⌘⌥←: Previous space
        if hasCmd && hasOpt && !hasCtrl && !hasShift && event.keyCode == 123 {
            CoreUpdateDispatcher.shared.sendAndDispatch(.commandBarAction(action: .prevSpace))
            return true
        }

        // ⌘⌥→: Next space
        if hasCmd && hasOpt && !hasCtrl && !hasShift && event.keyCode == 124 {
            CoreUpdateDispatcher.shared.sendAndDispatch(.commandBarAction(action: .nextSpace))
            return true
        }

        // ⌘⌥↑: Previous tab
        if hasCmd && hasOpt && !hasCtrl && !hasShift && event.keyCode == 126 {
            return switchTab(direction: .previous)
        }

        // ⌘⌥↓: Next tab
        if hasCmd && hasOpt && !hasCtrl && !hasShift && event.keyCode == 125 {
            return switchTab(direction: .next)
        }

        // Ctrl+Tab: Next tab
        if hasCtrl && !hasCmd && !hasOpt && !hasShift && event.keyCode == 48 {
            return switchTab(direction: .next)
        }

        // Ctrl+Shift+Tab: Previous tab
        if hasCtrl && !hasCmd && !hasOpt && hasShift && event.keyCode == 48 {
            return switchTab(direction: .previous)
        }

        // Ctrl+Shift++ (=): Add split view
        if hasCtrl && hasShift && !hasCmd && !hasOpt && (key == "=" || key == "+") {
            return dispatchQuickAction(.custom(actionId: "add_split_view"))
        }

        // Ctrl+Shift+-: Remove split view
        if hasCtrl && hasShift && !hasCmd && !hasOpt && key == "-" {
            return dispatchQuickAction(.custom(actionId: "remove_split_view"))
        }

        // Ctrl+Shift+1-9: Split view focus
        if hasCtrl && hasShift && !hasCmd && !hasOpt,
           let digit = Int(key), digit >= 1 && digit <= 9 {
            return dispatchQuickAction(.custom(actionId: "focus_split_\(digit)"))
        }

        // ⌘E: Toggle AI Panel
        if hasCmd && !hasCtrl && !hasOpt && !hasShift && key == "e" {
            (NSApp.delegate as? AppDelegate)?.toggleAIPanel()
            return true
        }

        // Escape: Stop loading
        if event.keyCode == 53 && !hasCmd && !hasCtrl && !hasOpt && !hasShift {
            guard let appDelegate = NSApp.delegate as? AppDelegate,
                  let mainWC = appDelegate.mainWindowControllerRef,
                  let tabId = mainWC.activeTabId else { return false }
            if let responder = NSApp.keyWindow?.firstResponder, responder is NSTextView {
                return false
            }
            CoreUpdateDispatcher.shared.sendAndDispatch(.stop(tabId: tabId))
            return true
        }

        // F12: Toggle Dev Tools
        if event.keyCode == 122 {
            CoreUpdateDispatcher.shared.sendAndDispatch(.commandBarAction(action: .toggleDevTools))
            return true
        }

        // ⌘←: Go Back
        if hasCmd && !hasCtrl && !hasOpt && !hasShift && event.keyCode == 123 {
            return goBack()
        }

        // ⌘→: Go Forward
        if hasCmd && !hasCtrl && !hasOpt && !hasShift && event.keyCode == 124 {
            return goForward()
        }

        return false
    }

    // MARK: - Tab Operations

    private enum TabDirection { case next, previous }

    private func visibleTabs(for spaceId: String) -> [TabViewModel] {
        MahoBridge.shared.getSpaceTabs(spaceId: spaceId).filter { !$0.isPinned && !$0.isFavorite }
    }

    private func activateTabByIndex(_ index: Int) -> Bool {
        guard let appDelegate = NSApp.delegate as? AppDelegate,
              let mainWC = appDelegate.mainWindowControllerRef else { return false }
        let spaceId = mainWC.sidebarController.activeSpaceId ?? MahoBridge.shared.getCanonicalActiveSpaceId() ?? ""
        guard !spaceId.isEmpty else { return false }

        let tabs = visibleTabs(for: spaceId)
        guard index < tabs.count else { return false }

        let tabId = tabs[index].id
        mainWC.sidebarController.activateTab(tabId)
        return true
    }

    private func switchTab(direction: TabDirection) -> Bool {
        guard let appDelegate = NSApp.delegate as? AppDelegate,
              let mainWC = appDelegate.mainWindowControllerRef else { return false }
        let spaceId = mainWC.sidebarController.activeSpaceId ?? MahoBridge.shared.getCanonicalActiveSpaceId() ?? ""
        guard !spaceId.isEmpty else { return false }

        let tabs = visibleTabs(for: spaceId)
        guard !tabs.isEmpty else { return false }

        let activeTabId = mainWC.activeTabId
        let currentIndex = tabs.firstIndex(where: { $0.id == activeTabId }) ?? 0

        let newIndex: Int
        switch direction {
        case .next:
            newIndex = (currentIndex + 1) % tabs.count
        case .previous:
            newIndex = (currentIndex - 1 + tabs.count) % tabs.count
        }

        mainWC.sidebarController.activateTab(tabs[newIndex].id)
        return true
    }

    // MARK: - Space Operations

    private func activateSpaceByIndex(_ index: Int) -> Bool {
        let spaces = MahoBridge.shared.getCanonicalSpaceViewModels()
        guard index < spaces.count else { return false }
        CoreUpdateDispatcher.shared.sendAndDispatch(.activateSpace(spaceId: spaces[index].id))
        return true
    }

    // MARK: - URL Bar

    private func focusURLBar() -> Bool {
        guard let appDelegate = NSApp.delegate as? AppDelegate else { return false }
        appDelegate.focusURLBar()
        return true
    }

    private func toggleArchivePanel() -> Bool {
        guard let appDelegate = NSApp.delegate as? AppDelegate else { return false }
        appDelegate.toggleArchivePanel()
        return true
    }

    // MARK: - QuickAction Dispatch

    private func dispatchQuickAction(_ action: QuickAction) -> Bool {
        CoreUpdateDispatcher.shared.sendAndDispatch(.commandBarAction(action: action))
        return true
    }

    private func dispatchQuickActionByString(_ actionStr: String) -> Bool {
        let mapping: [String: QuickAction] = [
            "new_tab": .newTab,
            "new_space": .newSpace,
            "close_tab": .closeTab,
            "close_other_tabs": .closeOtherTabs,
            "close_all_tabs": .closeAllTabs,
            "duplicate_tab": .duplicateTab,
            "pin_tab": .pinTab,
            "unpin_tab": .unpinTab,
            "mute_tab": .muteTab,
            "unmute_tab": .unmuteTab,
            "mute_all_tabs": .muteAllTabs,
            "freeze_tab": .freezeTab,
            "unfreeze_tab": .unfreezeTab,
            "reload_tab": .reloadTab,
            "hard_reload": .hardReload,
            "copy_url": .copyUrl,
            "copy_url_markdown": .copyUrlMarkdown,
            "clear_history": .clearHistory,
            "clear_cookies": .clearCookies,
            "open_settings": .openSettings,
            "open_downloads": .openDownloads,
            "open_bookmarks": .openBookmarks,
            "open_history": .openHistory,
            "toggle_boost": .toggleBoost,
            "toggle_sidebar": .toggleSidebar,
            "toggle_content_blocker": .toggleContentBlocker,
            "new_folder": .newFolder,
            "archive_tab": .archiveTab,
            "restore_last_closed": .restoreLastClosed,
            "toggle_full_screen": .toggleFullScreen,
            "zoom_in": .zoomIn,
            "zoom_out": .zoomOut,
            "reset_zoom": .resetZoom,
            "find_in_page": .findInPage,
            "print_page": .printPage,
            "view_source": .viewSource,
            "toggle_dev_tools": .toggleDevTools,
            "next_space": .nextSpace,
            "prev_space": .prevSpace,
        ]

        if let action = mapping[actionStr] {
            return dispatchQuickAction(action)
        }
        return false
    }

    // MARK: - Modifier Conversion

    private func goBack() -> Bool {
        guard let appDelegate = NSApp.delegate as? AppDelegate,
              let tabId = appDelegate.mainWindowControllerRef?.activeTabId else { return false }
        CoreUpdateDispatcher.shared.sendAndDispatch(.goBack(tabId: tabId))
        return true
    }

    private func goForward() -> Bool {
        guard let appDelegate = NSApp.delegate as? AppDelegate,
              let tabId = appDelegate.mainWindowControllerRef?.activeTabId else { return false }
        CoreUpdateDispatcher.shared.sendAndDispatch(.goForward(tabId: tabId))
        return true
    }

    static func modifierFlags(from modifiers: [KeyModifier]) -> NSEvent.ModifierFlags {
        var flags: NSEvent.ModifierFlags = []
        for mod in modifiers {
            switch mod {
            case .ctrl: flags.insert(.control)
            case .shift: flags.insert(.shift)
            case .alt: flags.insert(.option)
            case .meta: flags.insert(.command)
            }
        }
        return flags
    }
}
