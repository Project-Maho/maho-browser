import AppKit
import EventKit
import WebKit
#if canImport(Sparkle)
import Sparkle
#endif

class AppDelegate: NSObject, NSApplicationDelegate {
    private static let defaultUITestStatePath = "/tmp/maho-uitest-state.json"
    private static let defaultUITestDebugLogPath = "/tmp/maho-uitest-debug.log"
    private static let defaultUITestAIPanelCapturePath = "/tmp/maho-ai-panel-capture/panel.png"
    private static let uiTestStateLock = NSLock()

    static var uiTestStatePath: String {
        uiTestPath(defaultPath: defaultUITestStatePath, environmentVariable: "MAHO_UI_TEST_STATE_PATH")
    }

    static var uiTestDebugLogPath: String {
        uiTestPath(defaultPath: defaultUITestDebugLogPath, environmentVariable: "MAHO_UI_TEST_DEBUG_LOG_PATH")
    }

    static var uiTestAIPanelCapturePath: String {
        uiTestPath(defaultPath: defaultUITestAIPanelCapturePath, environmentVariable: "MAHO_UI_TEST_AI_PANEL_CAPTURE_PATH")
    }

    private static func uiTestPath(defaultPath: String, environmentVariable: String) -> String {
        guard let override = ProcessInfo.processInfo.environment[environmentVariable], !override.isEmpty else {
            return defaultPath
        }
        return override
    }

    private(set) var mainWindowController: BrowserWindowController?
    var mainWindowControllerRef: BrowserWindowController? { WindowManager.shared.focusedWindow ?? mainWindowController }
    private var commandBarController: CommandBarWindowController?
    private var settingsWindowController: SettingsWindowController?
    private var boostEditorController: BoostEditorController?
    private var boostMarketplaceController: BoostMarketplaceController?
    #if canImport(Sparkle)
    private let updaterController = SPUStandardUpdaterController(
        startingUpdater: false,
        updaterDelegate: nil,
        userDriverDelegate: nil
    )
    #endif

    func openLittleArc(url: String, sourceTabId: TabId? = nil) {
        let controller = LittleArcWindowController(url: url, sourceTabId: sourceTabId)
        littleArcWindows.append(controller)
        controller.showWindow(nil)
        controller.window?.makeKeyAndOrderFront(nil)

        // Clean up closed windows
        littleArcWindows.removeAll { $0.window == nil || !$0.window!.isVisible }
    }

    @objc func newLittleArcWindow() {
        openLittleArc(url: "about:blank")
    }
    private var littleArcWindows: [LittleArcWindowController] = []

    private func createStorageDirectory(at path: String, replacingExisting: Bool = false) {
        if replacingExisting {
            try? FileManager.default.removeItem(atPath: path)
        }
        try? FileManager.default.createDirectory(atPath: path, withIntermediateDirectories: true)
    }

    private func quarantineCorruptStorage(at path: String) {
        let fileManager = FileManager.default
        let quarantinePath = path + ".corrupt"
        try? fileManager.removeItem(atPath: quarantinePath)
        if fileManager.fileExists(atPath: path) {
            try? fileManager.moveItem(atPath: path, toPath: quarantinePath)
        }
    }

    private func activateMainWindow() -> BrowserWindowController {
        let wc = WindowManager.shared.createWindow()
        mainWindowController = wc
        wc.showWindow(nil)
        wc.window?.orderFrontRegardless()
        wc.window?.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
        DispatchQueue.main.async { [weak wc] in
            guard let wc else { return }
            if !wc.ensureMainWindowFrameIsValid() {
                wc.window?.orderFrontRegardless()
                wc.window?.makeKeyAndOrderFront(nil)
                NSApp.activate(ignoringOtherApps: true)
            }
        }
        return wc
    }

    private func recoverMainWindow(using storagePath: String) -> BrowserWindowController {
        quarantineCorruptStorage(at: storagePath)
        MahoBridge.shared.shutdown()
        createStorageDirectory(at: storagePath, replacingExisting: true)
        MahoBridge.shared.initialize(storagePath: storagePath)
        _ = MahoBridge.shared.loadState()
        return activateMainWindow()
    }

    func applicationWillFinishLaunching(_ notification: Notification) {
        NSApp.setActivationPolicy(.regular)
        UserDefaults.standard.set(false, forKey: "NSQuitAlwaysKeepsWindows")
        if let bundleId = Bundle.main.bundleIdentifier,
           let libDir = FileManager.default.urls(for: .libraryDirectory, in: .userDomainMask).first {
            let savedState = libDir.appendingPathComponent("Saved Application State/\(bundleId).savedState")
            try? FileManager.default.removeItem(at: savedState)
        }
        if isUITesting {
            try? FileManager.default.removeItem(atPath: Self.uiTestStatePath)
            try? FileManager.default.removeItem(atPath: Self.uiTestDebugLogPath)
        }
    }

    func applicationDidFinishLaunching(_ notification: Notification) {
        UserDefaults.standard.set("didFinishLaunching: entered", forKey: "uitest_launch_1")
        UserDefaults.standard.synchronize()
        if isUITesting {
            UserDefaults.standard.set("didFinishLaunching started", forKey: "uitest_debug_1")
        }
        let appSupport = FileManager.default.urls(
            for: .applicationSupportDirectory,
            in: .userDomainMask
        ).first
        let storageDirectoryName = isUITesting ? "Maho-UI-Testing" : "Maho"
        let storagePath = appSupport?.appendingPathComponent(storageDirectoryName).path
        if let path = storagePath {
            createStorageDirectory(at: path, replacingExisting: isUITesting)
            // Sidebar alignment is now managed by Maho-specific prefs
            // (sidebarWidth / sidebarCollapsed in AppearanceSettings)
        }

        var wc: BrowserWindowController
        if isUITesting {
            if let path = storagePath {
                MahoBridge.shared.initialize(storagePath: path)
            } else {
                MahoBridge.shared.initialize()
            }
            _ = MahoBridge.shared.loadState()
            seedDeterministicTestState()
            wc = activateMainWindow()
            UserDefaults.standard.set("createWindow done, window=\(wc.window != nil)", forKey: "uitest_debug_2")
            UserDefaults.standard.set("showWindow done, visible=\(wc.window?.isVisible ?? false)", forKey: "uitest_debug_3")
            debugLog("Window shown, isVisible=\(wc.window?.isVisible ?? false)")
        } else if let path = storagePath {
            MahoBridge.shared.initialize(storagePath: path)
            let loadedState = MahoBridge.shared.loadState()
            if loadedState {
                wc = activateMainWindow()
                if wc.window?.isVisible != true {
                    wc = recoverMainWindow(using: path)
                }
            } else {
                wc = recoverMainWindow(using: path)
            }
        } else {
            MahoBridge.shared.initialize()
            _ = MahoBridge.shared.loadState()
            wc = activateMainWindow()
        }

        if let activeSpaceId = MahoBridge.shared.getActiveSpaceId() {
            mainWindowController?.sidebarController.activeSpaceId = activeSpaceId
            mainWindowController?.sidebarController.reloadSidebar()
        }

        CoreUpdateDispatcher.shared.sendAndDispatch(.appLaunched)

        CrashRecoveryManager.shared.startPeriodicSave()

        if isUITesting {
            DispatchQueue.main.async { [weak self, weak wc] in
                guard let self, let wc else { return }
                self.applyUITestDefaultSearchEngineIfRequested()
                wc.window?.orderFrontRegardless()
                wc.window?.makeKeyAndOrderFront(nil)
                NSApp.activate(ignoringOtherApps: true)
                self.debugLog("Seeding done, isKey=\(wc.window?.isKeyWindow ?? false), isVisible=\(wc.window?.isVisible ?? false), windowCount=\(NSApp.windows.count)")
                for (i, w) in NSApp.windows.enumerated() {
                    self.debugLog("  window[\(i)]: title=\(w.title), visible=\(w.isVisible)")
                }
                if ProcessInfo.processInfo.arguments.contains("--ui-testing-ai-preview-seed") {
                    let tabViewModels = MahoBridge.shared.getTabViewModels()
                    if let previewTab = tabViewModels.first(where: { $0.url.contains("diabrowser.com") }) {
                        wc.activateTabLocally(previewTab.id)
                    }
                }
                if ProcessInfo.processInfo.arguments.contains("--ui-testing-open-ai-panel") || self.shouldCaptureAIPanelForUITest {
                    wc.showAIPanel()
                }
                if ProcessInfo.processInfo.arguments.contains("--ui-testing-open-archive-panel") {
                    wc.sidebarController.toggleArchivePanel()
                }
                self.openUITestingSettingsIfRequested()
                self.scheduleUITestAIPanelCaptureIfRequested(for: wc)
            }
        }

        commandBarController = CommandBarWindowController()
        setupMainMenu()
        ShortcutDispatcher.shared.start()

        LLMBridgeHandler.shared.start()
        LinkPreviewHandler.shared.start()

        AutofillHandler.shared.start()
        _ = AutofillPopoverController.shared
        CredentialProviderManager.shared.start()
        if let providerName = UserDefaults.standard.string(forKey: AIProviderStorageKey.provider),
           let providerType = LLMProviderType.persistedType(from: providerName) {
            LLMService.shared.configure(type: providerType)
        }

        if UserDefaults.standard.bool(forKey: "maho.mcp.enabled") {
            MCPServer.shared.toolRegistry.delegate = mainWindowController
            MCPServer.shared.start()
        }

        RelayAuthManager.shared.start()

        if isUITesting,
           ProcessInfo.processInfo.environment["MAHO_OPEN_NEW_TAB_COMMAND_BAR_ON_LAUNCH"] == "1" {
            DispatchQueue.main.async { [weak self] in
                guard let self,
                      let spaceId = self.mainWindowController?.sidebarController.activeSpaceId else { return }
                self.showCommandBarForNewTab(spaceId: spaceId)
            }
        }
    }

    func applicationWillTerminate(_ notification: Notification) {
        MCPServer.shared.stop()
        CrashRecoveryManager.shared.stopAndSave()
        ShortcutDispatcher.shared.stop()
        CoreUpdateDispatcher.shared.sendAndDispatch(.appWillTerminate)
        MahoBridge.shared.shutdown()
    }

    func application(_ application: NSApplication, continue userActivity: NSUserActivity, restorationHandler: @escaping ([NSUserActivityRestoring]) -> Void) -> Bool {
        if userActivity.activityType == NSUserActivityTypeBrowsingWeb || userActivity.activityType == "dev.maho.browsing",
           let url = userActivity.webpageURL {
            CoreUpdateDispatcher.shared.sendAndDispatch(.navigateTo(tabId: mainWindowControllerRef?.activeTabId ?? "", url: MahoUrl(url.absoluteString)))
            return true
        }
        return false
    }

    func application(_ application: NSApplication, open urls: [URL]) {
        for url in urls {
            if url.scheme == "maho", url.host == "shared",
               let collectionId = url.pathComponents.dropFirst().first {
                let joined = MahoBridge.shared.joinSharedCollection(shareLink: url.absoluteString)
                if joined {
                    let alert = NSAlert()
                    alert.messageText = "Joined Collection"
                    alert.informativeText = "You joined shared collection \(collectionId)."
                    alert.alertStyle = .informational
                    alert.addButton(withTitle: "OK")
                    alert.runModal()
                }
            } else {
                openLittleArc(url: url.absoluteString)
            }
        }
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool {
        return false
    }

    func applicationShouldHandleReopen(_ sender: NSApplication, hasVisibleWindows flag: Bool) -> Bool {
        let controller = WindowManager.shared.focusedWindow ?? mainWindowController ?? WindowManager.shared.createWindow()
        mainWindowController = controller
        controller.showWindow(nil)
        controller.window?.orderFrontRegardless()
        controller.window?.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
        return true
    }

    @objc func toggleCommandBar() {
        guard let controller = commandBarController else { return }
        if controller.window?.isVisible == true {
            controller.hideCommandBar()
        } else {
            controller.showCommandBar()
        }
    }

    @objc func showCommandBar(_ sender: Any?) {
        commandBarController?.showCommandBar()
    }

    func showCommandBarForNewTab(spaceId: SpaceId, folderId: FolderId? = nil) {
        Self.appendUITestDebugLog("AppDelegate.showCommandBarForNewTab spaceId=\(spaceId) folderId=\(String(describing: folderId))")
        Self.writeUITestState([
            "commandBarMode": String(describing: CommandBarMode.newTabDraft(spaceId: spaceId, folderId: folderId))
        ])
        commandBarController?.showCommandBar(mode: .newTabDraft(spaceId: spaceId, folderId: folderId))
    }

    private func setupMainMenu() {
        let mainMenu = NSMenu()

        // MARK: - App Menu
        let appMenuItem = NSMenuItem()
        appMenuItem.submenu = NSMenu(title: "Maho")
        appMenuItem.submenu?.addItem(
            withTitle: "About Maho",
            action: #selector(NSApplication.orderFrontStandardAboutPanel(_:)),
            keyEquivalent: ""
        )
        appMenuItem.submenu?.addItem(NSMenuItem.separator())
        #if canImport(Sparkle)
        let checkForUpdatesItem = NSMenuItem(
            title: "Check for Updates…",
            action: #selector(checkForUpdates(_:)),
            keyEquivalent: ""
        )
        appMenuItem.submenu?.addItem(checkForUpdatesItem)
        appMenuItem.submenu?.addItem(NSMenuItem.separator())
        #endif
        let preferencesItem = NSMenuItem(
            title: "Preferences…",
            action: #selector(openSettings),
            keyEquivalent: ","
        )
        preferencesItem.keyEquivalentModifierMask = .command
        appMenuItem.submenu?.addItem(preferencesItem)
        appMenuItem.submenu?.addItem(NSMenuItem.separator())
        let quitItem = NSMenuItem(
            title: "Quit Maho",
            action: #selector(NSApplication.terminate(_:)),
            keyEquivalent: "q"
        )
        quitItem.keyEquivalentModifierMask = .command
        appMenuItem.submenu?.addItem(quitItem)
        mainMenu.addItem(appMenuItem)

        // MARK: - File Menu
        let fileMenuItem = NSMenuItem()
        fileMenuItem.submenu = NSMenu(title: "File")
        let newWindowMenuItem = NSMenuItem(
            title: "New Window",
            action: #selector(newBrowserWindow),
            keyEquivalent: "n"
        )
        newWindowMenuItem.keyEquivalentModifierMask = .command
        fileMenuItem.submenu?.addItem(newWindowMenuItem)
        let newWindowItem = NSMenuItem(
            title: "New Window",
            action: nil,
            keyEquivalent: "N"
        )
        newWindowItem.keyEquivalentModifierMask = [.command, .shift]
        newWindowItem.title = "New Incognito Window"
        newWindowItem.action = #selector(newIncognitoWindow)
        fileMenuItem.submenu?.addItem(newWindowItem)
        fileMenuItem.submenu?.addItem(NSMenuItem.separator())
        let closeTabItem = NSMenuItem(
            title: "Close Tab",
            action: #selector(closeTab),
            keyEquivalent: "w"
        )
        closeTabItem.keyEquivalentModifierMask = .command
        fileMenuItem.submenu?.addItem(closeTabItem)
        let closeWindowItem = NSMenuItem(
            title: "Close Window",
            action: #selector(NSWindow.performClose(_:)),
            keyEquivalent: "W"
        )
        closeWindowItem.keyEquivalentModifierMask = [.command, .shift]
        fileMenuItem.submenu?.addItem(closeWindowItem)
        fileMenuItem.submenu?.addItem(NSMenuItem.separator())
        let printItem = NSMenuItem(
            title: "Print…",
            action: #selector(printPage),
            keyEquivalent: "p"
        )
        printItem.keyEquivalentModifierMask = .command
        fileMenuItem.submenu?.addItem(printItem)
        fileMenuItem.submenu?.addItem(NSMenuItem.separator())
        let restoreLastClosedItem = NSMenuItem(
            title: "Restore Last Closed Tab",
            action: #selector(restoreLastClosed),
            keyEquivalent: "T"
        )
        restoreLastClosedItem.keyEquivalentModifierMask = [.command, .shift]
        fileMenuItem.submenu?.addItem(restoreLastClosedItem)
        mainMenu.addItem(fileMenuItem)

        // MARK: - Edit Menu
        let editMenuItem = NSMenuItem()
        editMenuItem.submenu = NSMenu(title: "Edit")
        let undoItem = NSMenuItem(
            title: "Undo",
            action: #selector(UndoManager.undo),
            keyEquivalent: "z"
        )
        undoItem.keyEquivalentModifierMask = .command
        editMenuItem.submenu?.addItem(undoItem)
        let redoItem = NSMenuItem(
            title: "Redo",
            action: #selector(UndoManager.redo),
            keyEquivalent: "Z"
        )
        redoItem.keyEquivalentModifierMask = [.command, .shift]
        editMenuItem.submenu?.addItem(redoItem)
        editMenuItem.submenu?.addItem(NSMenuItem.separator())
        editMenuItem.submenu?.addItem(
            withTitle: "Cut",
            action: #selector(NSText.cut(_:)),
            keyEquivalent: "x"
        )
        editMenuItem.submenu?.addItem(
            withTitle: "Copy",
            action: #selector(NSText.copy(_:)),
            keyEquivalent: "c"
        )
        editMenuItem.submenu?.addItem(
            withTitle: "Paste",
            action: #selector(NSText.paste(_:)),
            keyEquivalent: "v"
        )
        editMenuItem.submenu?.addItem(
            withTitle: "Select All",
            action: #selector(NSText.selectAll(_:)),
            keyEquivalent: "a"
        )
        editMenuItem.submenu?.addItem(NSMenuItem.separator())
        let findItem = NSMenuItem(
            title: "Find…",
            action: #selector(findInPage),
            keyEquivalent: "f"
        )
        findItem.keyEquivalentModifierMask = .command
        editMenuItem.submenu?.addItem(findItem)
        let findNextItem = NSMenuItem(
            title: "Find Next",
            action: #selector(findNext),
            keyEquivalent: "g"
        )
        findNextItem.keyEquivalentModifierMask = .command
        editMenuItem.submenu?.addItem(findNextItem)
        let findPrevItem = NSMenuItem(
            title: "Find Previous",
            action: #selector(findPrevious),
            keyEquivalent: "G"
        )
        findPrevItem.keyEquivalentModifierMask = [.command, .shift]
        editMenuItem.submenu?.addItem(findPrevItem)
        let commandBarItem = NSMenuItem(
            title: "New Tab",
            action: #selector(newTab),
            keyEquivalent: "t"
        )
        commandBarItem.keyEquivalentModifierMask = .command
        editMenuItem.submenu?.addItem(commandBarItem)
        let commandPaletteItem = NSMenuItem(
            title: "Command Palette",
            action: #selector(toggleCommandBar),
            keyEquivalent: ""
        )
        editMenuItem.submenu?.addItem(commandPaletteItem)
        mainMenu.addItem(editMenuItem)

        // MARK: - Tab Menu
        let tabMenuItem = NSMenuItem()
        tabMenuItem.submenu = NSMenu(title: "Tab")
        let pinTabItem = NSMenuItem(
            title: "Pin Tab",
            action: #selector(pinTab),
            keyEquivalent: "d"
        )
        pinTabItem.keyEquivalentModifierMask = .command
        tabMenuItem.submenu?.addItem(pinTabItem)
        let duplicateTabItem = NSMenuItem(
            title: "Duplicate Tab",
            action: #selector(duplicateTab),
            keyEquivalent: "D"
        )
        duplicateTabItem.keyEquivalentModifierMask = [.command, .shift]
        tabMenuItem.submenu?.addItem(duplicateTabItem)
        tabMenuItem.submenu?.addItem(NSMenuItem.separator())
        let copyURLItem = NSMenuItem(
            title: "Copy URL",
            action: #selector(copyURL),
            keyEquivalent: "C"
        )
        copyURLItem.keyEquivalentModifierMask = [.command, .shift]
        tabMenuItem.submenu?.addItem(copyURLItem)
        let copyURLMarkdownItem = NSMenuItem(
            title: "Copy URL as Markdown",
            action: #selector(copyURLAsMarkdown),
            keyEquivalent: "C"
        )
        copyURLMarkdownItem.keyEquivalentModifierMask = [.command, .shift, .option]
        tabMenuItem.submenu?.addItem(copyURLMarkdownItem)
        tabMenuItem.submenu?.addItem(NSMenuItem.separator())
        let clearUnpinnedTabsItem = NSMenuItem(
            title: "Clear Unpinned Tabs",
            action: #selector(clearUnpinnedTabs),
            keyEquivalent: "K"
        )
        clearUnpinnedTabsItem.keyEquivalentModifierMask = [.command, .shift]
        tabMenuItem.submenu?.addItem(clearUnpinnedTabsItem)
        mainMenu.addItem(tabMenuItem)

        // MARK: - View Menu
        let viewMenuItem = NSMenuItem()
        viewMenuItem.submenu = NSMenu(title: "View")
        let reloadItem = NSMenuItem(
            title: "Reload Page",
            action: #selector(reloadPage),
            keyEquivalent: "r"
        )
        reloadItem.keyEquivalentModifierMask = .command
        viewMenuItem.submenu?.addItem(reloadItem)
        let hardReloadItem = NSMenuItem(
            title: "Hard Reload",
            action: #selector(hardReload),
            keyEquivalent: "R"
        )
        hardReloadItem.keyEquivalentModifierMask = [.command, .shift]
        viewMenuItem.submenu?.addItem(hardReloadItem)
        let stopItem = NSMenuItem(
            title: "Stop",
            action: #selector(stopLoading),
            keyEquivalent: "."
        )
        stopItem.keyEquivalentModifierMask = .command
        viewMenuItem.submenu?.addItem(stopItem)
        viewMenuItem.submenu?.addItem(NSMenuItem.separator())
        let goBackItem = NSMenuItem(
            title: "Go Back",
            action: #selector(goBack),
            keyEquivalent: "["
        )
        goBackItem.keyEquivalentModifierMask = .command
        viewMenuItem.submenu?.addItem(goBackItem)
        let goForwardItem = NSMenuItem(
            title: "Go Forward",
            action: #selector(goForward),
            keyEquivalent: "]"
        )
        goForwardItem.keyEquivalentModifierMask = .command
        viewMenuItem.submenu?.addItem(goForwardItem)
        viewMenuItem.submenu?.addItem(NSMenuItem.separator())
        let zoomInItem = NSMenuItem(
            title: "Zoom In",
            action: #selector(zoomIn),
            keyEquivalent: "+"
        )
        zoomInItem.keyEquivalentModifierMask = .command
        viewMenuItem.submenu?.addItem(zoomInItem)
        let zoomOutItem = NSMenuItem(
            title: "Zoom Out",
            action: #selector(zoomOut),
            keyEquivalent: "-"
        )
        zoomOutItem.keyEquivalentModifierMask = .command
        viewMenuItem.submenu?.addItem(zoomOutItem)
        let resetZoomItem = NSMenuItem(
            title: "Actual Size",
            action: #selector(resetZoom),
            keyEquivalent: "0"
        )
        resetZoomItem.keyEquivalentModifierMask = .command
        viewMenuItem.submenu?.addItem(resetZoomItem)
        viewMenuItem.submenu?.addItem(NSMenuItem.separator())
        let toggleSidebarItem = NSMenuItem(
            title: "Toggle Sidebar",
            action: #selector(toggleSidebar),
            keyEquivalent: "s"
        )
        toggleSidebarItem.keyEquivalentModifierMask = .command
        viewMenuItem.submenu?.addItem(toggleSidebarItem)
        let toggleAIPanelItem = NSMenuItem(
            title: "Toggle AI Panel",
            action: #selector(toggleAIPanel),
            keyEquivalent: "e"
        )
        toggleAIPanelItem.keyEquivalentModifierMask = .command
        viewMenuItem.submenu?.addItem(toggleAIPanelItem)
        let toggleArchivePanelItem = NSMenuItem(
            title: "Toggle Archive Panel",
            action: #selector(toggleArchivePanel),
            keyEquivalent: "a"
        )
        toggleArchivePanelItem.keyEquivalentModifierMask = [.command, .shift]
        viewMenuItem.submenu?.addItem(toggleArchivePanelItem)
        let toggleFullScreenItem = NSMenuItem(
            title: "Toggle Full Screen",
            action: #selector(toggleFullScreen),
            keyEquivalent: "f"
        )
        toggleFullScreenItem.keyEquivalentModifierMask = [.control, .command]
        viewMenuItem.submenu?.addItem(toggleFullScreenItem)
        let showToolbarItem = NSMenuItem(
            title: "Show Toolbar",
            action: #selector(toggleToolbarVisible),
            keyEquivalent: ""
        )
        viewMenuItem.submenu?.addItem(showToolbarItem)
        viewMenuItem.submenu?.addItem(NSMenuItem.separator())
        let viewSourceItem = NSMenuItem(
            title: "View Source",
            action: #selector(viewSource),
            keyEquivalent: "u"
        )
        viewSourceItem.keyEquivalentModifierMask = [.command, .option]
        viewMenuItem.submenu?.addItem(viewSourceItem)
        let focusURLBarItem = NSMenuItem(
            title: "Focus URL Bar",
            action: #selector(focusURLBar),
            keyEquivalent: "l"
        )
        focusURLBarItem.keyEquivalentModifierMask = .command
        focusURLBarItem.target = self
        viewMenuItem.submenu?.addItem(focusURLBarItem)
        let devToolsItem = NSMenuItem(
            title: "Toggle Developer Tools",
            action: #selector(toggleDevTools),
            keyEquivalent: "i"
        )
        devToolsItem.keyEquivalentModifierMask = [.command, .option]
        viewMenuItem.submenu?.addItem(devToolsItem)
        viewMenuItem.submenu?.addItem(NSMenuItem.separator())
        let extensionsItem = NSMenuItem(
            title: "Extensions",
            action: #selector(openExtensions),
            keyEquivalent: ""
        )
        viewMenuItem.submenu?.addItem(extensionsItem)

        let joinMeetingItem = NSMenuItem(
            title: "Join Meeting",
            action: #selector(joinMeeting),
            keyEquivalent: "j"
        )
        joinMeetingItem.keyEquivalentModifierMask = [.command, .control]
        viewMenuItem.submenu?.addItem(joinMeetingItem)
        let organizeTabsItem = NSMenuItem(
            title: "Organize Tabs",
            action: #selector(organizeTabs),
            keyEquivalent: "O"
        )
        organizeTabsItem.keyEquivalentModifierMask = [.command, .shift]
        viewMenuItem.submenu?.addItem(organizeTabsItem)
        let boostMarketplaceItem = NSMenuItem(
            title: "Boost Marketplace...",
            action: #selector(openBoostMarketplace),
            keyEquivalent: ""
        )
        viewMenuItem.submenu?.addItem(boostMarketplaceItem)
        mainMenu.addItem(viewMenuItem)

        // MARK: - Window Menu
        let windowMenuItem = NSMenuItem()
        windowMenuItem.submenu = NSMenu(title: "Window")
        let minimizeItem = NSMenuItem(
            title: "Minimize",
            action: #selector(NSWindow.miniaturize(_:)),
            keyEquivalent: "m"
        )
        minimizeItem.keyEquivalentModifierMask = .command
        windowMenuItem.submenu?.addItem(minimizeItem)
        let zoomWindowItem = NSMenuItem(
            title: "Zoom",
            action: #selector(NSWindow.zoom(_:)),
            keyEquivalent: ""
        )
        windowMenuItem.submenu?.addItem(zoomWindowItem)
        windowMenuItem.submenu?.addItem(NSMenuItem.separator())
        windowMenuItem.submenu?.addItem(
            withTitle: "Bring All to Front",
            action: #selector(NSApplication.arrangeInFront(_:)),
            keyEquivalent: ""
        )
        windowMenuItem.submenu?.addItem(NSMenuItem.separator())
        let historyItem = NSMenuItem(
            title: "History",
            action: #selector(openHistory),
            keyEquivalent: "y"
        )
        historyItem.keyEquivalentModifierMask = .command
        windowMenuItem.submenu?.addItem(historyItem)
        let downloadsItem = NSMenuItem(
            title: "Downloads",
            action: #selector(openDownloads),
            keyEquivalent: "j"
        )
        downloadsItem.keyEquivalentModifierMask = .command
        windowMenuItem.submenu?.addItem(downloadsItem)
        let bookmarksItem = NSMenuItem(
            title: "Bookmarks",
            action: #selector(openBookmarks),
            keyEquivalent: "b"
        )
        bookmarksItem.keyEquivalentModifierMask = [.command, .shift, .option]
        windowMenuItem.submenu?.addItem(bookmarksItem)
        mainMenu.addItem(windowMenuItem)

        // MARK: - Help Menu
        let helpMenuItem = NSMenuItem()
        helpMenuItem.submenu = NSMenu(title: "Help")
        helpMenuItem.submenu?.addItem(
            withTitle: "Maho Help",
            action: #selector(openHelp),
            keyEquivalent: ""
        )
        mainMenu.addItem(helpMenuItem)

        NSApplication.shared.mainMenu = mainMenu

        // MARK: - Register with ShortcutDispatcher
        ShortcutDispatcher.shared.registerMenuItem(preferencesItem, forAction: "open_settings")
        ShortcutDispatcher.shared.registerMenuItem(newWindowMenuItem, forAction: "new_window")
        ShortcutDispatcher.shared.registerMenuItem(closeTabItem, forAction: "close_tab")
        ShortcutDispatcher.shared.registerMenuItem(findItem, forAction: "find_in_page")
        ShortcutDispatcher.shared.registerMenuItem(reloadItem, forAction: "reload_tab")
        ShortcutDispatcher.shared.registerMenuItem(toggleSidebarItem, forAction: "toggle_sidebar")
        ShortcutDispatcher.shared.registerMenuItem(zoomInItem, forAction: "zoom_in")
        ShortcutDispatcher.shared.registerMenuItem(zoomOutItem, forAction: "zoom_out")
        ShortcutDispatcher.shared.registerMenuItem(resetZoomItem, forAction: "reset_zoom")
        ShortcutDispatcher.shared.registerMenuItem(toggleFullScreenItem, forAction: "toggle_full_screen")
        ShortcutDispatcher.shared.registerMenuItem(devToolsItem, forAction: "toggle_dev_tools")
        ShortcutDispatcher.shared.registerMenuItem(restoreLastClosedItem, forAction: "restore_last_closed")
        ShortcutDispatcher.shared.registerMenuItem(hardReloadItem, forAction: "hard_reload")
        ShortcutDispatcher.shared.registerMenuItem(goBackItem, forAction: "go_back")
        ShortcutDispatcher.shared.registerMenuItem(goForwardItem, forAction: "go_forward")
        ShortcutDispatcher.shared.registerMenuItem(pinTabItem, forAction: "pin_tab")
        ShortcutDispatcher.shared.registerMenuItem(duplicateTabItem, forAction: "duplicate_tab")
        ShortcutDispatcher.shared.registerMenuItem(toggleArchivePanelItem, forAction: "toggle_archive_panel")
        ShortcutDispatcher.shared.registerMenuItem(copyURLItem, forAction: "copy_url")
        ShortcutDispatcher.shared.registerMenuItem(copyURLMarkdownItem, forAction: "copy_url_markdown")
        ShortcutDispatcher.shared.registerMenuItem(printItem, forAction: "print_page")
        ShortcutDispatcher.shared.registerMenuItem(viewSourceItem, forAction: "view_source")
        ShortcutDispatcher.shared.registerMenuItem(focusURLBarItem, forAction: "focus_url_bar")
        ShortcutDispatcher.shared.registerMenuItem(historyItem, forAction: "open_history")
        ShortcutDispatcher.shared.registerMenuItem(findNextItem, forAction: "find_next")
        ShortcutDispatcher.shared.registerMenuItem(findPrevItem, forAction: "find_prev")
        ShortcutDispatcher.shared.registerMenuItem(commandBarItem, forAction: "command_bar")
    }

    @objc func openSettings() {
        presentSettingsWindow()
    }

    #if canImport(Sparkle)
    @objc func checkForUpdates(_ sender: Any?) {
        updaterController.checkForUpdates(sender)
    }
    #endif

    @objc func openAISettings() {
        presentSettingsWindow(paneIdentifier: SettingsWindowController.mahoAIPaneIdentifier)
    }

    private func presentSettingsWindow(paneIdentifier: String? = nil) {
        settingsWindowController = settingsWindowController ?? SettingsWindowController()
        settingsWindowController?.showWindow(nil)
        settingsWindowController?.window?.makeKeyAndOrderFront(nil)

        guard let paneIdentifier else { return }
        let selectedPaneIdentifier = settingsWindowController?.selectPane(identifier: paneIdentifier)
        Self.writeUITestState([
            "settingsPaneRequested": paneIdentifier,
            "settingsPaneSelected": selectedPaneIdentifier ?? "",
            "settingsPaneVisibleLabel": settingsWindowController?.selectedPaneAccessibilityLabel() ?? ""
        ])
        if isUITesting {
            DispatchQueue.main.async { [weak self] in
                guard let self else { return }
                self.writeSettingsLayoutState(
                    requestedPaneIdentifier: paneIdentifier,
                    selectedPaneIdentifier: selectedPaneIdentifier ?? paneIdentifier
                )
            }
        }
        if isUITesting {
            debugLog("Settings opened, requested=\(paneIdentifier), selected=\(selectedPaneIdentifier ?? String())")
        }
    }

    private func writeSettingsLayoutState(requestedPaneIdentifier: String, selectedPaneIdentifier: String) {
        guard let settingsWindowController else { return }
        settingsWindowController.window?.layoutIfNeeded()
        settingsWindowController.window?.contentView?.layoutSubtreeIfNeeded()

        let diagnostics = settingsWindowController.activePaneLayoutDiagnostics()
        Self.writeUITestStateObject([
            "settingsLayoutDiagnostics": [
                "requestedPaneIdentifier": requestedPaneIdentifier,
                "selectedPaneIdentifier": selectedPaneIdentifier,
                "dump": diagnostics
            ]
        ])
    }

    private var uiTestingSettingsPaneIdentifier: String? {
        if let requestedPaneIdentifier = ProcessInfo.processInfo.environment["MAHO_UI_TEST_SETTINGS_PANE"],
           !requestedPaneIdentifier.isEmpty {
            return requestedPaneIdentifier
        }

        let arguments = ProcessInfo.processInfo.arguments
        if let argumentIndex = arguments.firstIndex(of: "--ui-testing-settings-pane"),
           arguments.index(after: argumentIndex) < arguments.endIndex {
            let requestedPaneIdentifier = arguments[arguments.index(after: argumentIndex)]
            return requestedPaneIdentifier.isEmpty ? nil : requestedPaneIdentifier
        }

        if let argument = arguments.first(where: { $0.hasPrefix("--ui-testing-settings-pane=") }) {
            let requestedPaneIdentifier = String(argument.dropFirst("--ui-testing-settings-pane=".count))
            return requestedPaneIdentifier.isEmpty ? nil : requestedPaneIdentifier
        }

        return nil
    }

    private func openUITestingSettingsIfRequested() {
        guard isUITesting, let paneIdentifier = uiTestingSettingsPaneIdentifier else { return }
        presentSettingsWindow(paneIdentifier: paneIdentifier)
    }

    private var shouldCaptureAIPanelForUITest: Bool {
        ProcessInfo.processInfo.arguments.contains("--ui-testing-capture-ai-panel")
    }

    private func scheduleUITestAIPanelCaptureIfRequested(for windowController: BrowserWindowController) {
        guard isUITesting, shouldCaptureAIPanelForUITest else { return }

        let capturePath = Self.uiTestAIPanelCapturePath
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.75) { [weak self, weak windowController] in
            guard let self, let windowController else { return }
            let success = windowController.captureVisibleAIPanelPNG(to: capturePath)
            Self.writeUITestState([
                "aiPanelCapturePath": capturePath,
                "aiPanelCaptureSuccess": success ? "1" : "0"
            ])
            self.debugLog("AI panel capture \(success ? "succeeded" : "failed") path=\(capturePath)")
        }
    }

    private func applyUITestDefaultSearchEngineIfRequested() {
        guard isUITesting,
              let requestedEngineName = ProcessInfo.processInfo.environment["MAHO_UI_TEST_DEFAULT_SEARCH_ENGINE"],
              !requestedEngineName.isEmpty,
              let json = MahoBridge.shared.getSearchEngines(),
              let engines: [SearchEngineViewModel] = MahoBridge.shared.decode(json),
              let engine = engines.first(where: { $0.name == requestedEngineName }) else { return }
        CoreUpdateDispatcher.shared.sendAndDispatch(.setDefaultSearchEngine(id: engine.id))
        Self.writeUITestState([
            "defaultSearchEngineId": engine.id,
            "defaultSearchEngineName": engine.name
        ])
    }

    @objc func openBoostEditor() {
        boostEditorController = boostEditorController ?? BoostEditorController()
        boostEditorController?.showWindow(nil)
        boostEditorController?.window?.makeKeyAndOrderFront(nil)
    }

    @objc func openBoostMarketplace() {
        if boostMarketplaceController == nil {
            boostMarketplaceController = BoostMarketplaceController()
        }
        boostMarketplaceController?.showWindow(nil)
        boostMarketplaceController?.window?.makeKeyAndOrderFront(nil)
    }

    @objc func findInPage() {
        CoreUpdateDispatcher.shared.sendAndDispatch(.commandBarAction(action: .findInPage))
    }

    @objc func newTab() {
        guard let spaceId = mainWindowControllerRef?.sidebarController.activeSpaceId else { return }
        showCommandBarForNewTab(spaceId: spaceId)
    }

    @objc func newBrowserWindow() {
        let wc = WindowManager.shared.createWindow()
        wc.showWindow(nil)
        wc.window?.makeKeyAndOrderFront(nil)

        if let activeSpaceId = MahoBridge.shared.getActiveSpaceId() {
            wc.sidebarController.activeSpaceId = activeSpaceId
            wc.sidebarController.reloadSidebar()
        }
    }

    @objc func closeTab() {
        CoreUpdateDispatcher.shared.sendAndDispatch(.commandBarAction(action: .closeTab))
    }

    @objc func reloadPage() {
        CoreUpdateDispatcher.shared.sendAndDispatch(.commandBarAction(action: .reloadTab))
    }

    @objc func goBack() {
        guard let tabId = mainWindowControllerRef?.activeTabId else { return }
        CoreUpdateDispatcher.shared.sendAndDispatch(.goBack(tabId: tabId))
    }

    @objc func goForward() {
        guard let tabId = mainWindowControllerRef?.activeTabId else { return }
        CoreUpdateDispatcher.shared.sendAndDispatch(.goForward(tabId: tabId))
    }

    @objc func printPage() {
        CoreUpdateDispatcher.shared.sendAndDispatch(.commandBarAction(action: .printPage))
    }

    @objc func viewSource() {
        CoreUpdateDispatcher.shared.sendAndDispatch(.commandBarAction(action: .viewSource))
    }

    @objc func openExtensions() {
        settingsWindowController = settingsWindowController ?? SettingsWindowController()
        settingsWindowController?.showWindow(nil)
        settingsWindowController?.window?.makeKeyAndOrderFront(nil)
        settingsWindowController?.selectPane(identifier: "extensions")
    }



    @objc func joinMeeting() {
        let store = EKEventStore()

        let requestAccess: (@escaping (Bool, Error?) -> Void) -> Void = { completion in
            if #available(macOS 14.0, *) {
                store.requestFullAccessToEvents { granted, error in
                    completion(granted, error)
                }
            } else {
                store.requestAccess(to: .event) { granted, error in
                    completion(granted, error)
                }
            }
        }

        requestAccess { granted, error in
            DispatchQueue.main.async {
                guard granted else {
                    let alert = NSAlert()
                    alert.messageText = "Calendar Access Required"
                    alert.informativeText = "Maho needs calendar access to find your next meeting. Please grant access in System Settings > Privacy & Security > Calendars."
                    alert.alertStyle = .warning
                    alert.addButton(withTitle: "Open System Settings")
                    alert.addButton(withTitle: "Cancel")
                    if alert.runModal() == .alertFirstButtonReturn {
                        if let url = URL(string: "x-apple.systempreferences:com.apple.preference.security?Privacy_Calendars") {
                            NSWorkspace.shared.open(url)
                        }
                    }
                    return
                }

                let now = Date()
                let lookAhead = now.addingTimeInterval(15 * 60) // next 15 minutes
                let predicate = store.predicateForEvents(withStart: now.addingTimeInterval(-5 * 60), end: lookAhead, calendars: nil)
                let events = store.events(matching: predicate)
                    .sorted { ($0.startDate ?? now) < ($1.startDate ?? now) }

                let meetingPatterns = [
                    "https?://[a-zA-Z0-9._-]*zoom\\.us/[^\\s\"<>]+",
                    "https?://meet\\.google\\.com/[^\\s\"<>]+",
                    "https?://teams\\.microsoft\\.com/[^\\s\"<>]+",
                    "https?://[a-zA-Z0-9._-]*webex\\.com/[^\\s\"<>]+"
                ]
                let combinedPattern = meetingPatterns.joined(separator: "|")

                func extractMeetingURL(from text: String?) -> URL? {
                    guard let text = text, !text.isEmpty else { return nil }
                    guard let regex = try? NSRegularExpression(pattern: combinedPattern, options: .caseInsensitive) else { return nil }
                    let range = NSRange(text.startIndex..., in: text)
                    if let match = regex.firstMatch(in: text, range: range),
                       let matchRange = Range(match.range, in: text) {
                        return URL(string: String(text[matchRange]))
                    }
                    return nil
                }

                for event in events {
                    // Check structured URL first, then location, notes, title
                    if let urlObj = event.url, self.isMeetingURL(urlObj) {
                        NSWorkspace.shared.open(urlObj)
                        return
                    }
                    for field in [event.location, event.notes, event.title] {
                        if let url = extractMeetingURL(from: field) {
                            NSWorkspace.shared.open(url)
                            return
                        }
                    }
                }

                let alert = NSAlert()
                if events.isEmpty {
                    alert.messageText = "No Upcoming Meetings"
                    alert.informativeText = "No calendar events found in the next 15 minutes."
                } else {
                    alert.messageText = "No Meeting Link Found"
                    alert.informativeText = "Found \(events.count) upcoming event\(events.count == 1 ? "" : "s"), but none contain a recognized meeting link (Zoom, Google Meet, Teams, or Webex)."
                }
                alert.alertStyle = .informational
                alert.addButton(withTitle: "OK")
                alert.runModal()
            }
        }
    }

    private func isMeetingURL(_ url: URL) -> Bool {
        let host = url.host?.lowercased() ?? ""
        return host.contains("zoom.us") || host.contains("meet.google.com") ||
               host.contains("teams.microsoft.com") || host.contains("webex.com")
    }

    @objc func openHelp() {
        if let url = URL(string: "https://maho.dev/help") {
            NSWorkspace.shared.open(url)
        }
    }

    @objc func organizeTabs() {
        guard let spaceId = mainWindowControllerRef?.sidebarController.activeSpaceId else { return }
        CoreUpdateDispatcher.shared.sendAndDispatch(.requestTidyTabs(spaceId: spaceId))
    }

    @objc func toggleDevTools() {
        CoreUpdateDispatcher.shared.sendAndDispatch(.commandBarAction(action: .toggleDevTools))
    }

    @objc func toggleSidebar() {
        mainWindowControllerRef?.toggleSidebar()
    }

    @objc func toggleAIPanel() {
        mainWindowControllerRef?.toggleAIPanel()
    }

    @objc func toggleArchivePanel() {
        mainWindowControllerRef?.sidebarController.toggleArchivePanel()
    }

    func showAIPanel() {
        mainWindowControllerRef?.showAIPanel()
    }

    func showAIPanelWithQuery(_ query: String) {
        mainWindowControllerRef?.showAIPanelWithQuery(query)
    }

    @objc func toggleFullScreen() {
        CoreUpdateDispatcher.shared.sendAndDispatch(.commandBarAction(action: .toggleFullScreen))
    }

    @objc func zoomIn() {
        CoreUpdateDispatcher.shared.sendAndDispatch(.commandBarAction(action: .zoomIn))
    }

    @objc func zoomOut() {
        CoreUpdateDispatcher.shared.sendAndDispatch(.commandBarAction(action: .zoomOut))
    }

    @objc func resetZoom() {
        CoreUpdateDispatcher.shared.sendAndDispatch(.commandBarAction(action: .resetZoom))
    }

    @objc func hardReload() {
        CoreUpdateDispatcher.shared.sendAndDispatch(.commandBarAction(action: .hardReload))
    }

    @objc func restoreLastClosed() {
        CoreUpdateDispatcher.shared.sendAndDispatch(.commandBarAction(action: .restoreLastClosed))
    }

    @objc func stopLoading() {
        guard let tabId = mainWindowControllerRef?.activeTabId else { return }
        CoreUpdateDispatcher.shared.sendAndDispatch(.stop(tabId: tabId))
    }

    @objc func focusURLBar() {
        if let tabId = mainWindowControllerRef?.activeTabId {
            if let currentURL = mainWindowControllerRef?.currentURL?.absoluteString {
                var state: [String: String] = [
                    "currentTabId": tabId,
                    "currentTabURL": currentURL,
                ]

                if currentURL != "about:blank" {
                    state["commandBarPrefilledURL"] = currentURL
                    state["commandBarSelectionRange"] = "0:\(currentURL.count)"
                }

                Self.writeUITestState(state)
            }
            mainWindowControllerRef?.sidebarController.focusAddressBar()
        } else {
            commandBarController?.showCommandBar()
        }
    }

    @objc func openHistory() {
        CoreUpdateDispatcher.shared.sendAndDispatch(.commandBarAction(action: .openHistory))
    }

    @objc func openDownloads() {
        CoreUpdateDispatcher.shared.sendAndDispatch(.commandBarAction(action: .openDownloads))
    }

    @objc func openBookmarks() {
        CoreUpdateDispatcher.shared.sendAndDispatch(.commandBarAction(action: .openBookmarks))
    }

    @objc func pinTab() {
        CoreUpdateDispatcher.shared.sendAndDispatch(.commandBarAction(action: .pinTab))
    }

    @objc func unpinTab() {
        CoreUpdateDispatcher.shared.sendAndDispatch(.commandBarAction(action: .unpinTab))
    }

    @objc func duplicateTab() {
        guard let tabId = mainWindowControllerRef?.activeTabId else { return }
        CoreUpdateDispatcher.shared.sendAndDispatch(.duplicateTab(tabId: tabId))
    }

    @objc func copyURL() {
        CoreUpdateDispatcher.shared.sendAndDispatch(.commandBarAction(action: .copyUrl))
    }

    @objc func sharePage() {
        guard let url = mainWindowControllerRef?.currentURL else { return }
        let picker = NSSharingServicePicker(items: [url])
        if let view = mainWindowControllerRef?.window?.contentView {
            picker.show(relativeTo: .zero, of: view, preferredEdge: .minY)
        }
    }

    @objc func scrollToTop() {
        mainWindowControllerRef?.evaluateJavaScriptInActiveTab(
            "window.scrollTo({top: 0, behavior: 'smooth'})"
        )
    }

    @objc func scrollToBottom() {
        mainWindowControllerRef?.evaluateJavaScriptInActiveTab(
            "window.scrollTo({top: document.body.scrollHeight, behavior: 'smooth'})"
        )
    }

    @objc func clearBrowsingData() {
        let dataStore = WKWebsiteDataStore.default()
        let dataTypes = WKWebsiteDataStore.allWebsiteDataTypes()
        dataStore.fetchDataRecords(ofTypes: dataTypes) { records in
            dataStore.removeData(ofTypes: dataTypes, for: records) { }
        }
        CoreUpdateDispatcher.shared.sendAndDispatch(.commandBarAction(action: .clearHistory))
    }

    @objc func copyURLAsMarkdown() {
        guard let mainWC = mainWindowControllerRef,
              let tabId = mainWC.activeTabId else { return }
        let tabs = MahoBridge.shared.getTabViewModels()
        guard let tab = tabs.first(where: { $0.id == tabId }) else { return }
        let markdown = "[\(tab.title)](\(tab.url))"
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(markdown, forType: .string)
    }

    @objc func toggleToolbarVisible() {
        mainWindowControllerRef?.toggleToolbarVisible()
    }

    @objc func newIncognitoWindow() {
        let incognitoWC = IncognitoWindowController()
        incognitoWC.showWindow(nil)
        incognitoWC.window?.makeKeyAndOrderFront(nil)
    }

    @objc func clearUnpinnedTabs() {
        CoreUpdateDispatcher.shared.sendAndDispatch(.commandBarAction(action: .closeAllTabs))
    }

    @objc func findNext() {
        _ = MahoBridge.shared.findNext()
    }

    @objc func findPrevious() {
        _ = MahoBridge.shared.findPrevious()
    }

    private var isUITesting: Bool {
        ProcessInfo.processInfo.arguments.contains("--ui-testing")
    }

    private func debugLog(_ message: String) {
        let path = Self.uiTestDebugLogPath
        let line = "\(Date()): \(message)\n"
        if FileManager.default.fileExists(atPath: path) {
            if let handle = FileHandle(forWritingAtPath: path) {
                handle.seekToEndOfFile()
                handle.write(line.data(using: .utf8)!)
                handle.synchronizeFile()
                handle.closeFile()
            }
        } else {
            try? line.write(toFile: path, atomically: false, encoding: .utf8)
        }
        fputs(line, stderr)
    }

    static func appendDebugLog(_ message: String) {
        appendUITestDebugLog(message)
    }

    static func appendUITestDebugLog(_ message: String) {
        guard ProcessInfo.processInfo.arguments.contains("--ui-testing") else { return }
        let line = "\(Date()): \(message)\n"
        let path = Self.uiTestDebugLogPath
        if FileManager.default.fileExists(atPath: path) {
            if let handle = FileHandle(forWritingAtPath: path) {
                handle.seekToEndOfFile()
                handle.write(line.data(using: .utf8)!)
                handle.synchronizeFile()
                handle.closeFile()
            }
        } else {
            try? line.write(toFile: path, atomically: false, encoding: .utf8)
        }
    }

    // Removed: normalizeSidePanelAlignmentIfNeeded — Maho sidebar no longer
    // depends on Chromium side_panel alignment prefs.

    static func writeUITestState(_ state: [String: String]) {
        writeUITestStateObject(state)
    }

    static func writeUITestStateObject(_ state: [String: Any]) {
        guard ProcessInfo.processInfo.arguments.contains("--ui-testing") else { return }
        uiTestStateLock.lock()
        defer { uiTestStateLock.unlock() }

        var mergedState: [String: Any] = [:]
        if let existingData = FileManager.default.contents(atPath: uiTestStatePath),
           let existingObject = try? JSONSerialization.jsonObject(with: existingData),
           let existingState = existingObject as? [String: Any] {
            mergedState = existingState
        }

        for (key, value) in state {
            mergedState[key] = value
        }

        guard JSONSerialization.isValidJSONObject(mergedState) else { return }
        guard let data = try? JSONSerialization.data(withJSONObject: mergedState, options: [.sortedKeys]) else { return }
        try? data.write(to: URL(fileURLWithPath: uiTestStatePath), options: .atomic)
        appendUITestDebugLog("writeUITestState incoming=\(state) merged=\(mergedState)")
    }

    private func seedDeterministicTestState() {
        func sendAndDispatch(_ event: ShellEvent) {
            let updates = MahoBridge.shared.sendEvent(event)
            CoreUpdateDispatcher.shared.dispatch(updates)
        }

        let color = SpaceColor(hue: 0.65, saturation: 0.8, brightness: 0.9)
        let profileId = MahoBridge.shared.getActiveProfileId() ?? ""

        sendAndDispatch(.createSpace(name: "Test Space", color: color, profileId: profileId))

        let spaces = MahoBridge.shared.getSpaceViewModels()
        guard let space = spaces.first(where: { $0.name == "Test Space" }) else { return }
        let spaceId = space.id

        let useAIPanelPreviewSeed = ProcessInfo.processInfo.arguments.contains("--ui-testing-ai-preview-seed")

        // Favorited tabs (excluded from regular section by core)
        let favoriteUrls = useAIPanelPreviewSeed ? [
            "https://diabrowser.com",
            "https://news.ycombinator.com",
            "https://stackoverflow.com",
            "https://en.wikipedia.org",
            "https://developer.apple.com",
            "https://www.rust-lang.org",
        ] : [
            "https://github.com",
            "https://news.ycombinator.com",
            "https://stackoverflow.com",
            "https://en.wikipedia.org",
            "https://developer.apple.com",
            "https://www.rust-lang.org",
        ]
        // Pinned tabs
        let pinnedUrls = [
            "https://docs.swift.org",
            "https://reddit.com",
            "https://arxiv.org",
        ]
        // Regular root tabs (not favorited, not pinned, not in folders)
        let regularUrls = [
            "https://www.typescriptlang.org",
            "https://nodejs.org",
            "https://go.dev",
        ]
        // Tabs to assign to folders
        let folderTabUrls = [
            "https://crates.io",
            "https://pypi.org",
        ]

        let allUrls = favoriteUrls + pinnedUrls + regularUrls + folderTabUrls
        for url in allUrls {
            sendAndDispatch(.createTab(spaceId: spaceId, url: MahoUrl(url), parentId: nil))
        }

        let allTabs = MahoBridge.shared.getTabViewModels().filter { $0.spaceId == spaceId }
        guard allTabs.count >= allUrls.count else { return }
        let tabs = Array(allTabs.prefix(allUrls.count))

        for i in 0..<favoriteUrls.count where tabs.count > i {
            sendAndDispatch(.favoriteTab(tabId: tabs[i].id))
        }

        // Set distinct titles for favorited tabs so UI tests can verify reorder by label
        let favoriteLabels = useAIPanelPreviewSeed
            ? ["Dia Browser | Lates…", "Hacker News", "Stack Overflow", "Wikipedia", "Apple Dev", "Rust Lang"]
            : ["GitHub", "Hacker News", "Stack Overflow", "Wikipedia", "Apple Dev", "Rust Lang"]
        for i in 0..<favoriteUrls.count where tabs.count > i {
            sendAndDispatch(.tabTitleUpdated(tabId: tabs[i].id, title: favoriteLabels[i]))
        }

        let previewTabId = useAIPanelPreviewSeed ? tabs.first?.id : nil

        let pinnedStart = favoriteUrls.count
        for i in pinnedStart..<(pinnedStart + pinnedUrls.count) where tabs.count > i {
            sendAndDispatch(.pinTab(tabId: tabs[i].id))
        }

        sendAndDispatch(.createFolder(spaceId: spaceId, name: "Work", isPinned: false))
        sendAndDispatch(.createFolder(spaceId: spaceId, name: "Personal", isPinned: false))

        let folders = MahoBridge.shared.getFolderViewModels(spaceId: spaceId)
        let workTabIndex = favoriteUrls.count + pinnedUrls.count + regularUrls.count
        if let workFolder = folders.first(where: { $0.name == "Work" }), tabs.count > workTabIndex {
            sendAndDispatch(.moveTabToFolder(spaceId: spaceId, folderId: workFolder.id, tabId: tabs[workTabIndex].id))
        }
        let personalTabIndex = workTabIndex + 1
        if let personalFolder = folders.first(where: { $0.name == "Personal" }), tabs.count > personalTabIndex {
            sendAndDispatch(.moveTabToFolder(spaceId: spaceId, folderId: personalFolder.id, tabId: tabs[personalTabIndex].id))
        }

        sendAndDispatch(.activateSpace(spaceId: spaceId))

        let archivedStart = favoriteUrls.count + pinnedUrls.count
        let archivedLabels = ["TypeScript Docs", "Node.js", "Go"]
        for (offset, label) in archivedLabels.enumerated() {
            let tabIndex = archivedStart + offset
            guard tabs.count > tabIndex else { continue }
            let tabId = tabs[tabIndex].id
            sendAndDispatch(.tabTitleUpdated(tabId: tabId, title: label))
            sendAndDispatch(.activateTab(tabId: tabId))
            sendAndDispatch(.commandBarAction(action: .archiveTab))
        }

        if let previewTabId {
            sendAndDispatch(.activateTab(tabId: previewTabId))
        }
    }
}
