package dev.maho.browser.ui

import android.webkit.WebView
import androidx.compose.ui.graphics.Color
import dev.maho.browser.models.SpaceColor
import dev.maho.browser.models.SuggestionViewModel

internal fun normalizeBrowserInputValue(input: String, searchEngine: String): String {
    val trimmed = input.trim()
    if (trimmed.isBlank()) return ""
    if (trimmed.startsWith("http://") || trimmed.startsWith("https://")) return trimmed
    if (trimmed.contains(".") && !trimmed.contains(" ")) return "https://$trimmed"
    val encoded = java.net.URLEncoder.encode(trimmed, "UTF-8")
    return when (searchEngine) {
        "Bing" -> "https://www.bing.com/search?q=$encoded"
        "DuckDuckGo" -> "https://duckduckgo.com/?q=$encoded"
        "Ecosia" -> "https://www.ecosia.org/search?q=$encoded"
        "Perplexity" -> "https://www.perplexity.ai/search?q=$encoded"
        "Kagi" -> "https://kagi.com/search?q=$encoded"
        else -> "https://www.google.com/search?q=$encoded"
    }
}

internal fun suggestionNavigationTarget(suggestion: SuggestionViewModel): String {
    val payload = suggestion.executionPayload?.trim()
    if (!payload.isNullOrEmpty()) return payload.navigationPayloadValue()
    return when {
        suggestion.key.startsWith("nav:") -> suggestion.key.removePrefix("nav:")
        suggestion.key.startsWith("search:") -> suggestion.key.removePrefix("search:")
        suggestion.key.startsWith("history:") -> suggestion.key.removePrefix("history:")
        suggestion.key.startsWith("closed:") -> suggestion.key.removePrefix("closed:")
        else -> suggestion.key
    }
}

private fun String.navigationPayloadValue(): String {
    if (!startsWith("{")) return this
    return runCatching {
        val json = org.json.JSONObject(this)
        json.optString("url").takeIf { it.isNotBlank() }
            ?: json.optString("query").takeIf { it.isNotBlank() }
            ?: this
    }.getOrDefault(this)
}

internal fun aiSearchQueryText(suggestion: SuggestionViewModel): String {
    val aiPrefix = "ai_search:"
    return if (suggestion.key.startsWith(aiPrefix)) suggestion.key.removePrefix(aiPrefix) else suggestion.title
}

internal fun SpaceColor.toComposeColor(): Color = Color.hsv(
    hue = hue.toFloat(),
    saturation = saturation.toFloat().coerceIn(0f, 1f),
    value = brightness.toFloat().coerceIn(0f, 1f),
)

internal fun WebView.applyDesktopMode(isDesktopMode: Boolean) {
    settings.userAgentString = if (isDesktopMode) {
        "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/123.0.0.0 Safari/537.36"
    } else null
    settings.useWideViewPort = isDesktopMode
    settings.loadWithOverviewMode = true
}

internal fun WebView.applyDisplayOptions(isReaderMode: Boolean, zoomLevel: Float) {
    val readerStyles = if (isReaderMode) {
        "body{max-width:720px !important;margin:0 auto !important;padding:24px !important;font-size:1.15em !important;line-height:1.8 !important;background:#f8f5ef !important;color:#1f1a17 !important;}header,nav,aside,footer,[role='navigation'],[role='complementary'],.sidebar,.nav,.navigation,.footer{display:none !important;}img,video{max-width:100% !important;height:auto !important;}"
    } else ""
    val escapedStyles = readerStyles.replace("\\", "\\\\").replace("'", "\\'").replace("\n", "")
    val script = """
        (function() {
            var styleId = 'maho-reader-style';
            var existing = document.getElementById(styleId);
            if (!existing) {
                existing = document.createElement('style');
                existing.id = styleId;
                document.head.appendChild(existing);
            }
            existing.textContent = '${escapedStyles}';
            document.body.style.zoom = '${"%.2f".format(java.util.Locale.US, zoomLevel)}';
        })();
    """.trimIndent()
    evaluateJavascript(script, null)
}
