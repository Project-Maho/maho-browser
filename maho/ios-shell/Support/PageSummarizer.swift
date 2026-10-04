import Foundation
import NaturalLanguage
import WebKit

@MainActor
final class PageSummarizer: ObservableObject {
    enum State: Equatable {
        case idle
        case loading
        case result([String])
        case error(String)
    }

    @Published var state: State = .idle

    static let extractionJS = """
    (function() {
        var el = document.querySelector('article') ||
                 document.querySelector('main') ||
                 document.querySelector('[role="main"]') ||
                 document.body;
        var text = (el ? el.innerText : '') || '';
        return text.substring(0, 5000);
    })();
    """

    func summarize(webView: WKWebView?) {
        guard let webView else {
            state = .error("No content to summarize")
            return
        }

        state = .loading

        webView.evaluateJavaScript(Self.extractionJS) { [weak self] result, error in
            Task { @MainActor in
                guard let self else { return }

                if let error {
                    self.state = .error("Failed to extract page content: \(error.localizedDescription)")
                    return
                }

                guard let text = result as? String, !text.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty else {
                    self.state = .error("No content to summarize")
                    return
                }

                let sentences = Self.splitIntoSentences(text)
                guard !sentences.isEmpty else {
                    self.state = .error("No content to summarize")
                    return
                }

                let summary = Self.extractiveSummary(sentences: sentences)
                self.state = .result(summary)
            }
        }
    }

    func reset() {
        state = .idle
    }

    // MARK: - NLP

    private static func splitIntoSentences(_ text: String) -> [String] {
        let tokenizer = NLTokenizer(unit: .sentence)
        tokenizer.string = text

        var sentences: [String] = []
        tokenizer.enumerateTokens(in: text.startIndex..<text.endIndex) { range, _ in
            let sentence = String(text[range]).trimmingCharacters(in: .whitespacesAndNewlines)
            if !sentence.isEmpty {
                sentences.append(sentence)
            }
            return true
        }
        return sentences
    }

    private static func extractiveSummary(sentences: [String], maxSentences: Int = 5) -> [String] {
        guard sentences.count > maxSentences else { return sentences }

        let wordFrequency = buildWordFrequency(sentences: sentences)
        let totalSentences = Double(sentences.count)

        var scored: [(index: Int, score: Double, sentence: String)] = []

        for (index, sentence) in sentences.enumerated() {
            let positionScore = 1.0 - (Double(index) / totalSentences)

            let wordCount = sentence.split(separator: " ").count
            let lengthScore: Double
            if wordCount < 5 {
                lengthScore = 0.2
            } else if wordCount > 40 {
                lengthScore = 0.4
            } else {
                lengthScore = 1.0
            }

            let keywordScore = keywordDensity(sentence: sentence, frequency: wordFrequency)

            let total = positionScore * 0.3 + lengthScore * 0.2 + keywordScore * 0.5
            scored.append((index, total, sentence))
        }

        let topIndices = scored
            .sorted { $0.score > $1.score }
            .prefix(maxSentences)
            .map(\.index)
            .sorted()

        return topIndices.map { sentences[$0] }
    }

    private static func buildWordFrequency(sentences: [String]) -> [String: Int] {
        var freq: [String: Int] = [:]
        let stopWords: Set<String> = ["the", "a", "an", "is", "are", "was", "were", "be",
                                       "been", "being", "have", "has", "had", "do", "does",
                                       "did", "will", "would", "could", "should", "may",
                                       "might", "shall", "can", "to", "of", "in", "for",
                                       "on", "with", "at", "by", "from", "as", "into",
                                       "through", "during", "before", "after", "and", "but",
                                       "or", "nor", "not", "so", "yet", "both", "either",
                                       "neither", "each", "every", "all", "any", "few",
                                       "more", "most", "other", "some", "such", "no",
                                       "only", "own", "same", "than", "too", "very",
                                       "just", "because", "if", "when", "where", "how",
                                       "what", "which", "who", "whom", "this", "that",
                                       "these", "those", "it", "its", "i", "me", "my",
                                       "we", "our", "you", "your", "he", "him", "his",
                                       "she", "her", "they", "them", "their"]

        for sentence in sentences {
            let words = sentence.lowercased()
                .components(separatedBy: .alphanumerics.inverted)
                .filter { $0.count > 2 && !stopWords.contains($0) }
            for word in words {
                freq[word, default: 0] += 1
            }
        }
        return freq
    }

    private static func keywordDensity(sentence: String, frequency: [String: Int]) -> Double {
        let words = sentence.lowercased()
            .components(separatedBy: .alphanumerics.inverted)
            .filter { $0.count > 2 }
        guard !words.isEmpty else { return 0 }

        let maxFreq = Double(frequency.values.max() ?? 1)
        let totalScore = words.reduce(0.0) { sum, word in
            sum + Double(frequency[word] ?? 0) / maxFreq
        }
        return totalScore / Double(words.count)
    }
}
