import SwiftUI

enum MainBrowserPresentation {
    static func desktopUserAgent(from userAgent: String) -> String {
        userAgent
            .replacingOccurrences(of: #"\s+Mobile/[^ ]+"#, with: "", options: .regularExpression)
            .replacingOccurrences(of: #"\s+CriOS/[^ ]+"#, with: " Version/17.0", options: .regularExpression)
    }

    static func normalizedURL(from input: String) -> String {
        if input.contains(".") && !input.contains(" ") {
            return input.hasPrefix("http://") || input.hasPrefix("https://") ? input : "https://\(input)"
        }

        let encoded = input.addingPercentEncoding(withAllowedCharacters: .urlQueryAllowed) ?? input
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
