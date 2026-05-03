import AppKit

final class MiniPlayerWindowController: NSWindowController {
    private let miniPlayerView = MiniPlayerView(frame: NSRect(x: 0, y: 0, width: 300, height: 80))

    convenience init() {
        let panel = NSPanel(
            contentRect: NSRect(x: 0, y: 0, width: 300, height: 80),
            styleMask: [.titled, .closable, .nonactivatingPanel, .utilityWindow, .hudWindow],
            backing: .buffered,
            defer: false
        )
        panel.isFloatingPanel = true
        panel.level = .floating
        panel.title = "Mini Player"
        panel.isMovableByWindowBackground = true
        panel.collectionBehavior = [.canJoinAllSpaces, .fullScreenAuxiliary]
        self.init(window: panel)
        panel.contentView = miniPlayerView
    }

    func configure(tabId: TabId, title: String, isPlaying: Bool) {
        miniPlayerView.configure(tabId: tabId, title: title, isPlaying: isPlaying)
        showWindow(nil)
        if let screen = NSScreen.main {
            let screenFrame = screen.visibleFrame
            window?.setFrameOrigin(NSPoint(
                x: screenFrame.maxX - 320,
                y: screenFrame.minY + 20
            ))
        }
    }

    func dismiss() {
        close()
    }
}
