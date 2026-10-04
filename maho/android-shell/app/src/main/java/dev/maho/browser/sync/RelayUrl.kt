package dev.maho.browser.sync

fun normalizeRelayBaseUrl(url: String): String {
    val trimmed = url.trim().removeSuffix("/")
    if (trimmed.isBlank()) return trimmed
    return when {
        trimmed.startsWith("wss://") -> "https://${trimmed.removePrefix("wss://") }".trim()
        trimmed.startsWith("ws://") -> "http://${trimmed.removePrefix("ws://") }".trim()
        trimmed.startsWith("http://") || trimmed.startsWith("https://") -> trimmed
        else -> "https://$trimmed"
    }
}
