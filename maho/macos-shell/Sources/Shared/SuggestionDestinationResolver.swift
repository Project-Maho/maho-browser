import Foundation

enum SuggestionDestinationResolver {

    private static let safeKeyPrefixes: [String] = ["nav:", "history:", "search:"]

    /// Resolution order: executionPayload → safe key-prefix strip → nil.
    /// NEVER falls back to title/subtitle/opaque key.
    static func resolve(
        kind: SuggestionType,
        key: String,
        title: String,
        subtitle: String?,
        executionPayload: String?
    ) -> String? {
        switch kind {
        case .tab, .archivedTab, .closedTab:
            return nil
        default:
            break
        }

        if let payload = executionPayload, !payload.isEmpty {
            return payload
        }

        for prefix in safeKeyPrefixes {
            if key.hasPrefix(prefix) {
                let value = String(key.dropFirst(prefix.count))
                return value.isEmpty ? nil : value
            }
        }

        return nil
    }
}
