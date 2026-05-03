import AppKit

final class CommandBarWindowController: NSWindowController, CoreUpdateObserver {
    private let commandBarVC = CommandBarViewController()
    private var requestedMode: CommandBarMode = .search
    private var localClickMonitor: Any?
    private var appDeactivationObserver: NSObjectProtocol?
    private var isHiding = false

    init() {
        let isUITesting = CommandLine.arguments.contains("--ui-testing")
        let initialSize = NSSize(width: 720, height: 224)

        let win: NSWindow
        if isUITesting {
            win = NSWindow(
                contentRect: NSRect(origin: .zero, size: initialSize),
                styleMask: [.titled, .fullSizeContentView],
                backing: .buffered,
                defer: false
            )
        } else {
            let panel = NSPanel(
                contentRect: NSRect(origin: .zero, size: initialSize),
                styleMask: [.titled, .fullSizeContentView, .nonactivatingPanel],
                backing: .buffered,
                defer: false
            )
            panel.isFloatingPanel = true
            panel.level = .floating
            win = panel
        }

        win.title = "Command Bar"
        win.setAccessibilityIdentifier("commandBarWindow")
        win.titlebarAppearsTransparent = true
        win.titleVisibility = .hidden
        win.isOpaque = false
        win.hasShadow = true
        win.standardWindowButton(.closeButton)?.isHidden = true
        win.standardWindowButton(.miniaturizeButton)?.isHidden = true
        win.standardWindowButton(.zoomButton)?.isHidden = true
        win.backgroundColor = .clear

        win.isReleasedWhenClosed = false

        super.init(window: win)
        contentViewController = commandBarVC
        commandBarVC.contentDelegate = self

        CoreUpdateDispatcher.shared.register(self)
        NotificationCenter.default.addObserver(
            self,
            selector: #selector(activeSpaceThemeDidChange),
            name: .mahoActiveSpaceThemeChanged,
            object: nil
        )
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    deinit {
        removeVisibilityMonitors()
        NotificationCenter.default.removeObserver(self, name: .mahoActiveSpaceThemeChanged, object: nil)
        CoreUpdateDispatcher.shared.unregister(self)
    }

    func showCommandBar(mode: CommandBarMode = .search) {
        AppDelegate.appendUITestDebugLog("CommandBarWindow.showCommandBar requested mode=\(mode)")
        requestedMode = mode
        commandBarVC.prepareForPresentation(mode: requestedMode)
        guard let win = window else { return }
        positionWindow(win)
        let isUITesting = CommandLine.arguments.contains("--ui-testing")
        if isUITesting {
            let parentWindow = NSApp.mainWindow ?? NSApp.windows.first(where: { $0.identifier?.rawValue == "mainWindow" })
            win.alphaValue = 1
            win.level = .floating
            win.collectionBehavior = [.moveToActiveSpace, .fullScreenAuxiliary]
            win.setAccessibilityIdentifier("commandBarWindow")
            if let parentWindow, parentWindow !== win {
                parentWindow.addChildWindow(win, ordered: .above)
            }
            win.makeKeyAndOrderFront(nil)
            NSApp.activate(ignoringOtherApps: true)
        } else {
            let finalFrame = win.frame
            let slideOffset: CGFloat = 10

            win.alphaValue = 0
            win.setFrame(
                NSRect(x: finalFrame.origin.x,
                       y: finalFrame.origin.y + slideOffset,
                       width: finalFrame.width,
                       height: finalFrame.height),
                display: false
            )

            win.orderFront(nil)
            win.makeKey()

            NSAnimationContext.runAnimationGroup { ctx in
                ctx.duration = 0.35
                ctx.timingFunction = CAMediaTimingFunction(controlPoints: 0.175, 0.885, 0.32, 1.1)
                ctx.allowsImplicitAnimation = true
                win.animator().setFrame(finalFrame, display: true)
            }

            NSAnimationContext.runAnimationGroup { ctx in
                ctx.duration = 0.12
                ctx.timingFunction = CAMediaTimingFunction(name: .easeOut)
                win.animator().alphaValue = 1
            }
        }
        installVisibilityMonitors()
        commandBarVC.open(mode: requestedMode)
        AppDelegate.appendUITestDebugLog("CommandBarWindow.showCommandBar opened mode=\(requestedMode)")
    }

    func hideCommandBar() {
        guard let win = window, win.isVisible, !isHiding else { return }
        isHiding = true
        removeVisibilityMonitors()

        let currentFrame = win.frame
        let slideOffset: CGFloat = 6
        let targetFrame = NSRect(
            x: currentFrame.origin.x,
            y: currentFrame.origin.y + slideOffset,
            width: currentFrame.width,
            height: currentFrame.height
        )

        NSAnimationContext.runAnimationGroup({ ctx in
            ctx.duration = 0.18
            ctx.timingFunction = CAMediaTimingFunction(name: .easeIn)
            ctx.allowsImplicitAnimation = true
            win.animator().alphaValue = 0
            win.animator().setFrame(targetFrame, display: true)
        }, completionHandler: { [weak self] in
            guard let self else { return }
            win.orderOut(nil)
            self.commandBarVC.closeCommandBar()
            self.isHiding = false
        })
    }

    func handleCoreUpdate(_ update: CoreUpdate) {
        switch update {
        case .commandBarResults(let suggestions):
            commandBarVC.updateSuggestions(suggestions)
        case .recentSearchesUpdated(let searches):
            commandBarVC.updateRecentSearches(searches)
        case .searchEnginesUpdated(let engines):
            commandBarVC.updateSearchEngines(engines)
        case .activeSpaceChanged(_, _):
            if window?.isVisible == true {
                commandBarVC.refreshForSpaceChange()
            }
            commandBarVC.updateCommandBarChromeTint()
        default:
            break
        }
    }

    @objc private func activeSpaceThemeDidChange() {
        commandBarVC.updateCommandBarChromeTint()
    }

    private func positionWindow(_ win: NSWindow) {
        let reference = NSApp.keyWindow ?? NSApp.mainWindow
        let screenFrame = reference?.screen?.visibleFrame ?? NSScreen.main?.visibleFrame ?? .zero
        let windowWidth: CGFloat = 720
        let windowHeight: CGFloat = 224
        let x: CGFloat
        let targetY: CGFloat
        if let refFrame = reference?.frame {
            x = refFrame.midX - windowWidth / 2
            targetY = refFrame.midY - windowHeight / 2
        } else {
            x = screenFrame.midX - windowWidth / 2
            targetY = screenFrame.midY - windowHeight / 2
        }
        win.setContentSize(NSSize(width: windowWidth, height: windowHeight))
        win.setFrameOrigin(NSPoint(x: x, y: clampedVerticalOrigin(targetY, windowHeight: windowHeight, screenFrame: screenFrame)))
    }

    private func clampedVerticalOrigin(_ targetY: CGFloat, windowHeight: CGFloat, screenFrame: NSRect) -> CGFloat {
        let minY = screenFrame.minY + 40
        let maxY = screenFrame.maxY - 62 - windowHeight
        if maxY >= minY {
            return min(max(targetY, minY), maxY)
        }
        return min(max(targetY, screenFrame.minY), screenFrame.maxY - windowHeight)
    }

    private func installVisibilityMonitors() {
        removeVisibilityMonitors()

        localClickMonitor = NSEvent.addLocalMonitorForEvents(matching: [.leftMouseDown, .rightMouseDown, .otherMouseDown]) { [weak self] event in
            guard let self else { return event }
            guard let panel = self.window, panel.isVisible else { return event }
            let targetWindow = event.window
            if targetWindow === panel {
                return event
            }
            self.hideCommandBar()
            return event
        }

        appDeactivationObserver = NotificationCenter.default.addObserver(
            forName: NSApplication.didResignActiveNotification,
            object: NSApp,
            queue: .main
        ) { [weak self] _ in
            guard let self, self.window?.isVisible == true else { return }
            self.hideCommandBar()
        }
    }

    private func removeVisibilityMonitors() {
        if let localClickMonitor {
            NSEvent.removeMonitor(localClickMonitor)
            self.localClickMonitor = nil
        }
        if let appDeactivationObserver {
            NotificationCenter.default.removeObserver(appDeactivationObserver)
            self.appDeactivationObserver = nil
        }
    }

    func showCommandBarForCurrentTab(tabId: String) {
        showCommandBar(mode: .currentTab(tabId: tabId))
    }


}

extension CommandBarWindowController: CommandBarContentDelegate {
    func commandBarContentDidUpdate(preferredHeight: CGFloat) {
        guard let panel = window else { return }
        let currentFrame = panel.frame
        let newHeight = preferredHeight
        let screenFrame = panel.screen?.visibleFrame ?? NSScreen.main?.visibleFrame ?? .zero
        let newOrigin = NSPoint(
            x: currentFrame.origin.x,
            y: clampedVerticalOrigin(currentFrame.midY - newHeight / 2, windowHeight: newHeight, screenFrame: screenFrame)
        )
        let newFrame = NSRect(origin: newOrigin, size: NSSize(width: currentFrame.width, height: newHeight))
        NSAnimationContext.runAnimationGroup { ctx in
            ctx.duration = 0.2
            ctx.timingFunction = CAMediaTimingFunction(controlPoints: 0.25, 0.1, 0.25, 1.0)
            ctx.allowsImplicitAnimation = true
            panel.animator().setFrame(newFrame, display: true)
        }
    }
}
