import SwiftUI

enum SearchPresentationOrigin: Equatable {
    case home
    case browsing(TabId)

    var isBrowsing: Bool {
        if case .browsing = self { return true }
        return false
    }

    var tabId: TabId? {
        guard case .browsing(let tabId) = self else { return nil }
        return tabId
    }
}

enum SearchSubmissionPlan: Equatable {
    case reuseActiveTab(tabId: TabId, url: String)
    case createNewTab(url: String)
}

enum MainBrowserPresentation {
    static func searchPrefillQuery(for origin: SearchPresentationOrigin, currentUrl: String) -> String {
        guard case .browsing = origin else { return "" }
        let trimmed = currentUrl.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty, trimmed != "about:blank" else { return "" }
        return trimmed
    }

    static func searchSubmissionPlan(
        for origin: SearchPresentationOrigin?,
        query: String,
        activeTabId: TabId?
    ) -> SearchSubmissionPlan? {
        let trimmed = query.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else { return nil }
        let destination = normalizedURL(from: trimmed)

        guard let origin else {
            return .createNewTab(url: destination)
        }

        switch origin {
        case .home:
            return .createNewTab(url: destination)
        case .browsing(let tabId):
            guard !tabId.isEmpty, let activeTabId, !activeTabId.isEmpty else {
                return .createNewTab(url: destination)
            }
            return .reuseActiveTab(tabId: activeTabId, url: destination)
        }
    }

    static func desktopUserAgent(from userAgent: String) -> String {
        userAgent
            .replacingOccurrences(of: #"\s+Mobile/[^ ]+"#, with: "", options: .regularExpression)
            .replacingOccurrences(of: #"\s+CriOS/[^ ]+"#, with: " Version/17.0", options: .regularExpression)
    }

    static func normalizedURL(from input: String) -> String {
        let trimmed = input.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else { return "" }

        if trimmed.hasPrefix("http://") || trimmed.hasPrefix("https://") || trimmed.hasPrefix("about:") || trimmed.hasPrefix("file://") {
            return trimmed
        }

        if trimmed.hasPrefix("localhost") || trimmed.hasPrefix("127.0.0.1") {
            return "http://\(trimmed)"
        }

        if trimmed.contains(".") && !trimmed.contains(" ") {
            return "https://\(trimmed)"
        }

        let encoded = trimmed.addingPercentEncoding(withAllowedCharacters: .urlQueryAllowed) ?? trimmed
        return "https://www.google.com/search?q=\(encoded)"
    }

    static func displayHost(for urlString: String) -> String? {
        guard let components = URLComponents(string: urlString) else { return nil }
        return components.host ?? urlString
    }

    static func tintColor(for tab: TabViewModel) -> Color {
        if tab.isPinned {
            return .orange
        }
        if tab.isFavorite {
            return .yellow
        }
        if let host = displayHost(for: tab.url) {
            return colorSeed(for: host)
        }
        return ShellTheme.Palette.accent
    }

    private static func colorSeed(for string: String) -> Color {
        let scalarSum = string.unicodeScalars.reduce(0) { partialResult, scalar in
            partialResult + Int(scalar.value)
        }
        let hue = Double(scalarSum % 360) / 360.0
        return Color(hue: hue, saturation: 0.56, brightness: 0.9)
    }
}
