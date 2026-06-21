import Foundation
import CoreSpotlight
import UniformTypeIdentifiers

@MainActor
final class SpotlightManager {
    static let shared = SpotlightManager()

    private let index = CSSearchableIndex.default()
    private let conversationDomainIdentifier = "dev.maho.browser.conversations"
    private let historyDomainIdentifier = "dev.maho.browser.history"

    private init() {}

    // MARK: - Conversations

    func indexConversation(id: String, title: String, lastMessageAt: String) {
        let attributes = CSSearchableItemAttributeSet(contentType: .text)
        attributes.title = title.isEmpty ? "Untitled chat" : title
        attributes.contentDescription = "Chat conversation in Maho"
        attributes.lastUsedDate = parseISO8601(lastMessageAt) ?? Date()
        let item = CSSearchableItem(
            uniqueIdentifier: "conversation:\(id)",
            domainIdentifier: conversationDomainIdentifier,
            attributeSet: attributes
        )
        index.indexSearchableItems([item]) { _ in }
    }

    func removeConversation(id: String) {
        index.deleteSearchableItems(withIdentifiers: ["conversation:\(id)"]) { _ in }
    }

    // MARK: - History

    func indexHistory(url: String, title: String, visitedAt: String) {
        let attributes = CSSearchableItemAttributeSet(contentType: .url)
        attributes.title = title.isEmpty ? url : title
        attributes.contentURL = URL(string: url)
        attributes.contentDescription = url
        attributes.lastUsedDate = parseISO8601(visitedAt) ?? Date()
        let identifier = "history:\(url.hashValue)"
        let item = CSSearchableItem(
            uniqueIdentifier: identifier,
            domainIdentifier: historyDomainIdentifier,
            attributeSet: attributes
        )
        index.indexSearchableItems([item]) { _ in }
    }

    // MARK: - Maintenance

    func clearAll() {
        index.deleteAllSearchableItems { _ in }
    }

    // MARK: - Deep link routing

    /// Returns the conversation id if the CSSearchableItem uniqueIdentifier is a conversation link.
    static func conversationId(from uniqueIdentifier: String) -> String? {
        guard uniqueIdentifier.hasPrefix("conversation:") else { return nil }
        return String(uniqueIdentifier.dropFirst("conversation:".count))
    }

    // MARK: - Helpers

    private func parseISO8601(_ s: String) -> Date? {
        let formatterWithFractional: ISO8601DateFormatter = {
            let f = ISO8601DateFormatter()
            f.formatOptions = [.withInternetDateTime, .withFractionalSeconds]
            return f
        }()
        let formatterPlain: ISO8601DateFormatter = {
            let f = ISO8601DateFormatter()
            f.formatOptions = [.withInternetDateTime]
            return f
        }()
        let sqliteFormatter: DateFormatter = {
            let f = DateFormatter()
            f.locale = Locale(identifier: "en_US_POSIX")
            f.timeZone = TimeZone(secondsFromGMT: 0)
            f.dateFormat = "yyyy-MM-dd HH:mm:ss"
            return f
        }()
        return formatterWithFractional.date(from: s)
            ?? formatterPlain.date(from: s)
            ?? sqliteFormatter.date(from: s)
    }
}
