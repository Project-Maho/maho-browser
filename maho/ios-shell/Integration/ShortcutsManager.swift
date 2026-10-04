import Foundation
import AppIntents

@available(iOS 16.0, *)
struct OpenInMahoIntent: AppIntent {
    static var title: LocalizedStringResource = "Open in Maho"
    static var description = IntentDescription("Open a URL in Maho browser.")

    @Parameter(title: "URL")
    var urlString: String

    func perform() async throws -> some IntentResult {
        guard let url = URL(string: urlString), let spaceId = MahoBridge.shared.getActiveSpaceId() else {
            return .result()
        }
        MahoBridge.shared.sendEvent(.createTab(spaceId: spaceId, url: Url(url.absoluteString), parentId: nil, isPrivate: false))
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
        if let onNewTab = DeepLinkHandler.shared.onNewTab {
            onNewTab()
        } else {
            DeepLinkHandler.shared.pendingNewTabSearch = true
        }
        return .result()
    }
}

@available(iOS 16.0, *)
struct SearchInMahoIntent: AppIntent {
    static var title: LocalizedStringResource = "Search in Maho"
    static var description = IntentDescription("Search the web using Maho.")

    @Parameter(title: "Query")
    var query: String

    func perform() async throws -> some IntentResult {
        guard let spaceId = MahoBridge.shared.getActiveSpaceId() else {
            return .result()
        }
        let searchURL = "https://www.google.com/search?q=\(query.addingPercentEncoding(withAllowedCharacters: .urlQueryAllowed) ?? query)"
        MahoBridge.shared.sendEvent(.createTab(spaceId: spaceId, url: Url(searchURL), parentId: nil, isPrivate: false))
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
