package dev.maho.browser.ui

internal fun resolveAddressBarPrefill(currentUrl: String?, isHomeMode: Boolean = false): String {
    if (isHomeMode) return ""
    val trimmed = currentUrl?.trim().orEmpty()
    if (trimmed.isEmpty() || trimmed == "about:blank" || isSystemSurfaceUrl(trimmed)) {
        return ""
    }
    return trimmed
}

internal fun shouldReuseActiveTab(isBrowsingRoute: Boolean, hasActiveTab: Boolean): Boolean =
    isBrowsingRoute && hasActiveTab

internal fun resolveBrowsingUrl(
    requestedUrl: String?,
    activeTabUrl: String?,
    currentUrl: String,
): String =
    requestedUrl?.takeIf { it.isNotBlank() }
        ?: activeTabUrl?.takeIf { it.isNotBlank() }
        ?: currentUrl.takeIf { it.isNotBlank() }
        ?: "about:blank"

internal fun shouldLoadRequestedUrl(
    requestedUrl: String,
    renderedUrl: String?,
    lastRequestedUrl: String?,
): Boolean {
    if (requestedUrl.isBlank()) return false
    if (renderedUrl == requestedUrl) return false
    return lastRequestedUrl != requestedUrl
}
