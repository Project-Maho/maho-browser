package dev.maho.browser.ui

fun isSystemSurfaceUrl(raw: String): Boolean {
    val s = raw.trim()
    if (s.isBlank()) return true
    
    val scheme: String?
    val host: String?
    if (s.contains("://")) {
        val parts = s.split("://", limit = 2)
        scheme = parts.getOrNull(0)?.lowercase()
        val rest = parts.getOrNull(1) ?: ""
        host = rest.split('/', '?', '#', limit = 2).firstOrNull()?.lowercase()
    } else if (s.contains(':')) {
        val parts = s.split(':', limit = 2)
        scheme = parts.getOrNull(0)?.lowercase()
        host = null
    } else {
        scheme = null
        host = null
    }

    return when (scheme) {
        "about" -> true
        "chrome" -> host in setOf("newtab", "downloads", "history")
        else -> false
    }
}
