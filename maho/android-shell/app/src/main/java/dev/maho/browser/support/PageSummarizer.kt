package dev.maho.browser.support

import android.icu.text.BreakIterator
import android.webkit.WebView
import java.util.Locale

object PageSummarizer {

    sealed class State {
        data object Idle : State()
        data object Loading : State()
        data class Result(val bullets: List<String>) : State()
        data class Error(val message: String) : State()
    }

    private const val EXTRACTION_JS = """
        (function() {
            var el = document.querySelector('article') ||
                     document.querySelector('main') ||
                     document.querySelector('[role="main"]') ||
                     document.body;
            var text = (el ? el.innerText : '') || '';
            return text.substring(0, 5000);
        })();
    """

    fun extractAndSummarize(
        webView: WebView?,
        onStateChanged: (State) -> Unit,
    ) {
        if (webView == null) {
            onStateChanged(State.Error("No content to summarize"))
            return
        }

        onStateChanged(State.Loading)

        webView.evaluateJavascript(EXTRACTION_JS) { rawResult ->
            val text = rawResult
                ?.trim()
                ?.removeSurrounding("\"")
                ?.replace("\\n", "\n")
                ?.replace("\\t", " ")
                ?.replace("\\\"", "\"")
                ?.trim()

            if (text.isNullOrBlank() || text == "null") {
                onStateChanged(State.Error("No content to summarize"))
                return@evaluateJavascript
            }

            val sentences = splitIntoSentences(text)
            if (sentences.isEmpty()) {
                onStateChanged(State.Error("No content to summarize"))
                return@evaluateJavascript
            }

            val summary = extractiveSummary(sentences)
            onStateChanged(State.Result(summary))
        }
    }

    private fun splitIntoSentences(text: String): List<String> {
        val iterator = BreakIterator.getSentenceInstance(Locale.getDefault())
        iterator.setText(text)

        val sentences = mutableListOf<String>()
        var start = iterator.first()
        var end = iterator.next()

        while (end != BreakIterator.DONE) {
            val sentence = text.substring(start, end).trim()
            if (sentence.isNotEmpty()) {
                sentences.add(sentence)
            }
            start = end
            end = iterator.next()
        }
        return sentences
    }

    private fun extractiveSummary(sentences: List<String>, maxSentences: Int = 5): List<String> {
        if (sentences.size <= maxSentences) return sentences

        val wordFrequency = buildWordFrequency(sentences)
        val totalSentences = sentences.size.toDouble()

        data class Scored(val index: Int, val score: Double)

        val scored = sentences.mapIndexed { index, sentence ->
            val positionScore = 1.0 - (index / totalSentences)

            val wordCount = sentence.split("\\s+".toRegex()).size
            val lengthScore = when {
                wordCount < 5 -> 0.2
                wordCount > 40 -> 0.4
                else -> 1.0
            }

            val keywordScore = keywordDensity(sentence, wordFrequency)

            val total = positionScore * 0.3 + lengthScore * 0.2 + keywordScore * 0.5
            Scored(index, total)
        }

        return scored
            .sortedByDescending { it.score }
            .take(maxSentences)
            .sortedBy { it.index }
            .map { sentences[it.index] }
    }

    private val STOP_WORDS = setOf(
        "the", "a", "an", "is", "are", "was", "were", "be", "been", "being",
        "have", "has", "had", "do", "does", "did", "will", "would", "could",
        "should", "may", "might", "shall", "can", "to", "of", "in", "for",
        "on", "with", "at", "by", "from", "as", "into", "through", "during",
        "before", "after", "and", "but", "or", "nor", "not", "so", "yet",
        "both", "either", "neither", "each", "every", "all", "any", "few",
        "more", "most", "other", "some", "such", "no", "only", "own", "same",
        "than", "too", "very", "just", "because", "if", "when", "where",
        "how", "what", "which", "who", "whom", "this", "that", "these",
        "those", "it", "its", "me", "my", "we", "our", "you", "your",
        "he", "him", "his", "she", "her", "they", "them", "their",
    )

    private fun buildWordFrequency(sentences: List<String>): Map<String, Int> {
        val freq = mutableMapOf<String, Int>()
        for (sentence in sentences) {
            val words = sentence.lowercase()
                .split("\\W+".toRegex())
                .filter { it.length > 2 && it !in STOP_WORDS }
            for (word in words) {
                freq[word] = (freq[word] ?: 0) + 1
            }
        }
        return freq
    }

    private fun keywordDensity(sentence: String, frequency: Map<String, Int>): Double {
        val words = sentence.lowercase()
            .split("\\W+".toRegex())
            .filter { it.length > 2 }
        if (words.isEmpty()) return 0.0

        val maxFreq = (frequency.values.maxOrNull() ?: 1).toDouble()
        val totalScore = words.sumOf { word ->
            (frequency[word] ?: 0).toDouble() / maxFreq
        }
        return totalScore / words.size
    }
}
