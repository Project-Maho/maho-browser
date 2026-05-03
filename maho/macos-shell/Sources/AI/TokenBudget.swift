import Foundation

enum TokenBudget {
    // Rough estimation: ~4 chars per token for English, ~1.5 chars per token for CJK.
    static func estimateTokens(_ text: String) -> Int {
        guard !text.isEmpty else { return 0 }

        var asciiChars = 0
        var cjkChars = 0
        var otherChars = 0

        for character in text {
            if character.isASCII {
                asciiChars += 1
            } else if isCJK(character) {
                cjkChars += 1
            } else {
                otherChars += 1
            }
        }

        let asciiTokens = (asciiChars + 3) / 4
        let cjkTokens = Int(ceil(Double(cjkChars) / 1.5))
        let otherTokens = (otherChars + 3) / 4
        return asciiTokens + cjkTokens + otherTokens
    }

    // Default context window sizes per provider
    static let defaultMaxTokens: Int = 8192
    static let responseReserveTokens: Int = 2048

    static var availableContextTokens: Int {
        return defaultMaxTokens - responseReserveTokens
    }

    // Truncate conversation history to fit within budget
    // Keep system prompt + last N messages that fit within maxTokens
    // Always keep the latest user message intact
    static func truncateMessages(_ messages: [LLMMessage], maxTokens: Int) -> [LLMMessage] {
        guard !messages.isEmpty else { return [] }
        guard maxTokens > 0 else {
            return messages.filter { $0.role == .system }
        }

        let systemMessages = messages.filter { $0.role == .system }
        let systemTokens = estimateTokens(systemMessages.map(\.content).joined(separator: "\n"))

        let nonSystemMessages = messages.filter { $0.role != .system }
        guard !nonSystemMessages.isEmpty else {
            return systemMessages
        }

        let latestUserIndex = nonSystemMessages.lastIndex(where: { $0.role == .user }) ?? (nonSystemMessages.count - 1)
        let trailingMessages = Array(nonSystemMessages[latestUserIndex...])
        let trailingTokens = estimateTokens(trailingMessages.map(\.content).joined(separator: "\n"))

        var keptMessages = trailingMessages
        var currentTokens = systemTokens + trailingTokens

        if currentTokens > maxTokens {
            return systemMessages + trailingMessages
        }

        if latestUserIndex > 0 {
            for message in nonSystemMessages[..<latestUserIndex].reversed() {
                let messageTokens = estimateTokens(message.content)
                if currentTokens + messageTokens <= maxTokens {
                    keptMessages.insert(message, at: 0)
                    currentTokens += messageTokens
                } else {
                    break
                }
            }
        }

        return systemMessages + keptMessages
    }

    // Truncate page context text to fit a given token budget
    static func truncateContext(_ text: String, maxTokens: Int) -> String {
        guard maxTokens > 0 else { return "" }
        guard estimateTokens(text) > maxTokens else { return text }

        let characters = Array(text)
        guard !characters.isEmpty else { return text }

        var lowerBound = 0
        var upperBound = characters.count
        var bestLength = 0

        while lowerBound <= upperBound {
            let mid = (lowerBound + upperBound) / 2
            let prefix = String(characters.prefix(mid))
            let tokens = estimateTokens(prefix)

            if tokens <= maxTokens {
                bestLength = mid
                lowerBound = mid + 1
            } else {
                upperBound = mid - 1
            }
        }

        return String(characters.prefix(bestLength))
    }

    private static func isCJK(_ character: Character) -> Bool {
        for scalar in character.unicodeScalars {
            switch scalar.value {
            case 0x3040...0x30FF, // Hiragana, Katakana
                 0x3400...0x4DBF, // CJK Unified Ideographs Extension A
                 0x4E00...0x9FFF, // CJK Unified Ideographs
                 0xAC00...0xD7AF, // Hangul Syllables
                 0xF900...0xFAFF, // CJK Compatibility Ideographs
                 0x2E80...0x2EFF, // CJK Radicals Supplement
                 0x2F00...0x2FDF, // Kangxi Radicals
                 0x31C0...0x31EF, // CJK Strokes
                 0x31F0...0x31FF, // Katakana Phonetic Extensions
                 0xFF00...0xFFEF: // Halfwidth and Fullwidth Forms
                return true
            default:
                continue
            }
        }
        return false
    }
}
