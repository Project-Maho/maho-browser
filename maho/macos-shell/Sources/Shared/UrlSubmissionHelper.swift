import Foundation

enum UrlSubmissionHelper {
    static func displayDomain(from urlString: String) -> String {
        guard let url = URL(string: urlString), let host = url.host else {
            return urlString
        }
        return host
    }

    static func looksLikeUrl(_ text: String) -> Bool {
        if text.contains("://") { return true }
        if text.contains(" ") { return false }
        let dotParts = text.split(separator: ".")
        if dotParts.count >= 2, let lastPart = dotParts.last, lastPart.count >= 2 {
            return true
        }
        if text.hasPrefix("localhost") { return true }
        return false
    }

    static func resolvedUrl(from input: String) -> MahoUrl? {
        let trimmed = input.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else { return nil }

        if looksLikeUrl(trimmed) {
            let withScheme = trimmed.contains("://") ? trimmed : "https://\(trimmed)"
            return MahoUrl(withScheme)
        }

        let searchURL = MahoBridge.shared.getSearchURL(query: trimmed) ?? fallbackSearchURL(for: trimmed)
        guard let searchURL else { return nil }
        return MahoUrl(searchURL)
    }

    private static func fallbackSearchURL(for query: String) -> String? {
        var components = URLComponents()
        components.scheme = "https"
        components.host = "www.google.com"
        components.path = "/search"
        components.queryItems = [URLQueryItem(name: "q", value: query)]
        return components.url?.absoluteString
    }
}
