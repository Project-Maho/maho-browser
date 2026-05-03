import AppKit

final class WindowManager {
    static let shared = WindowManager()

    private(set) var windows: [BrowserWindowController] = []
    private(set) weak var focusedWindow: BrowserWindowController?

    private init() {}

    func addWindow(_ wc: BrowserWindowController) {
        windows.append(wc)
        if focusedWindow == nil {
            focusedWindow = wc
        }
    }

    func removeWindow(_ wc: BrowserWindowController) {
        windows.removeAll { $0 === wc }
        if focusedWindow === wc {
            focusedWindow = windows.first
        }
    }

    func createWindow() -> BrowserWindowController {
        let wc = BrowserWindowController.create()
        addWindow(wc)
        return wc
    }

    func setFocused(_ wc: BrowserWindowController) {
        focusedWindow = wc
    }

    func windowForTab(_ tabId: TabId) -> BrowserWindowController? {
        return WebViewRegistry.shared.currentDisplayer(of: tabId)
    }
}
