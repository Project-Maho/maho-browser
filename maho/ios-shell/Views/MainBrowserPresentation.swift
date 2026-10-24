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

struct SearchEngineOption: Identifiable {
    let id: String
    let name: String
    let urlTemplate: String
}

/// Single source of truth for the user's search engine. Settings writes the
/// selection; the address bar, deep links and Shortcuts all resolve through it.
enum SearchEngineCatalog {
    static let selectedEngineDefaultsKey = "general.selectedSearchEngine"

    static let options: [SearchEngineOption] = [
        SearchEngineOption(id: "google", name: "Google", urlTemplate: "https://www.google.com/search?q=%@"),
        SearchEngineOption(id: "bing", name: "Bing", urlTemplate: "https://www.bing.com/search?q=%@"),
        SearchEngineOption(id: "duckduckgo", name: "DuckDuckGo", urlTemplate: "https://duckduckgo.com/?q=%@"),
        SearchEngineOption(id: "ecosia", name: "Ecosia", urlTemplate: "https://www.ecosia.org/search?q=%@"),
        SearchEngineOption(id: "perplexity", name: "Perplexity", urlTemplate: "https://www.perplexity.ai/search?q=%@"),
        SearchEngineOption(id: "kagi", name: "Kagi", urlTemplate: "https://kagi.com/search?q=%@")
    ]

    static var defaultOption: SearchEngineOption { options[0] }

    static var selected: SearchEngineOption {
        let name = UserDefaults.standard.string(forKey: selectedEngineDefaultsKey)
        return options.first { $0.name == name } ?? defaultOption
    }

    static func searchURL(for query: String, using option: SearchEngineOption = selected) -> String {
        let encoded = query.addingPercentEncoding(withAllowedCharacters: queryValueAllowed) ?? query
        return option.urlTemplate.replacingOccurrences(of: "%@", with: encoded)
    }

    // `.urlQueryAllowed` keeps `&`, `+`, `=` and `#`, which would split or
    // truncate the query once it is placed inside the `q=` value.
    private static let queryValueAllowed: CharacterSet = {
        var allowed = CharacterSet.urlQueryAllowed
        allowed.remove(charactersIn: "&+=?#")
        return allowed
    }()
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
        activeTabId: TabId?,
        searchEngine: SearchEngineOption = SearchEngineCatalog.selected
    ) -> SearchSubmissionPlan? {
        let trimmed = query.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else { return nil }
        let destination = normalizedURL(from: trimmed, searchEngine: searchEngine)

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

    static func normalizedURL(
        from input: String,
        searchEngine: SearchEngineOption = SearchEngineCatalog.selected
    ) -> String {
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

        return SearchEngineCatalog.searchURL(for: trimmed, using: searchEngine)
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
