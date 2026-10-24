import Foundation
import AppIntents

@available(iOS 16.0, *)
struct OpenInMahoIntent: AppIntent {
    static var title: LocalizedStringResource = "Open in Maho"
    static var description = IntentDescription("Open a URL in Maho browser.")
    static var openAppWhenRun: Bool = true

    @Parameter(title: "URL")
    var urlString: String

    @MainActor
    func perform() async throws -> some IntentResult {
        let trimmed = urlString.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else { return .result() }
        // Route through the UI so the tab is created and shown in front; on a
        // cold launch the browser view consumes the queued URL once it appears.
        DeepLinkHandler.shared.openURL(trimmed)
        return .result()
    }
}

@available(iOS 16.0, *)
struct NewTabIntent: AppIntent {
    static var title: LocalizedStringResource = "New Tab"
    static var description = IntentDescription("Open a new tab in Maho.")
    static var openAppWhenRun: Bool = true

    @MainActor
    func perform() async throws -> some IntentResult {
        DeepLinkHandler.shared.openNewTab()
        return .result()
    }
}

@available(iOS 16.0, *)
struct SearchInMahoIntent: AppIntent {
    static var title: LocalizedStringResource = "Search in Maho"
    static var description = IntentDescription("Search the web using Maho.")
    static var openAppWhenRun: Bool = true

    @Parameter(title: "Query")
    var query: String

    @MainActor
    func perform() async throws -> some IntentResult {
        let trimmed = query.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else { return .result() }
        DeepLinkHandler.shared.openSearch(trimmed)
        return .result()
    }
}

@available(iOS 16.0, *)
struct MahoShortcutsProvider: AppShortcutsProvider {
    static var appShortcuts: [AppShortcut] {
        return [
            AppShortcut(
                intent: OpenInMahoIntent(),
                phrases: [
                    "Open URL in \(.applicationName)"
                ],
                shortTitle: "Open in Maho",
                systemImageName: "safari"
            ),
            AppShortcut(
                intent: NewTabIntent(),
                phrases: [
                    "New tab in \(.applicationName)"
                ],
                shortTitle: "New Tab",
                systemImageName: "plus.square"
            ),
            AppShortcut(
                intent: SearchInMahoIntent(),
                phrases: [
                    "Search in \(.applicationName)"
                ],
                shortTitle: "Search in Maho",
                systemImageName: "magnifyingglass"
            ),
            AppShortcut(
                intent: OpenMahoIntent(),
                phrases: [
                    "Open \(.applicationName)",
                    "Search with \(.applicationName)"
                ],
                shortTitle: "Open Maho",
                systemImageName: "magnifyingglass"
            )
        ]
    }
}

final class ShortcutsManager {
    static let shared = ShortcutsManager()
    private init() {}
}
