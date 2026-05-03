import AppKit

let app = NSApplication.shared

if ProcessInfo.processInfo.arguments.contains("--ui-testing") {
    UserDefaults.standard.set(true, forKey: "ApplePersistenceIgnoreState")
}

let delegate = AppDelegate()
app.delegate = delegate
app.run()
