package dev.maho.browser.ui

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class AddressSearchBehaviorTest {

    @Test
    fun resolveBrowsingUrl_prefersRequestedNavigationOverStaleTabUrl() {
        assertEquals(
            "https://example.com",
            resolveBrowsingUrl(
                requestedUrl = "https://example.com",
                activeTabUrl = "https://www.google.com/",
                currentUrl = "https://www.google.com/",
            ),
        )
    }

    @Test
    fun resolveBrowsingUrl_fallsBackToTabUrlWhenNoRequestPending() {
        assertEquals(
            "https://example.com/page",
            resolveBrowsingUrl(
                requestedUrl = null,
                activeTabUrl = "https://example.com/page",
                currentUrl = "",
            ),
        )
    }

    @Test
    fun resolveBrowsingUrl_fallsBackToCurrentUrlWhenTabUrlBlank() {
        assertEquals(
            "https://example.com/live",
            resolveBrowsingUrl(
                requestedUrl = null,
                activeTabUrl = "",
                currentUrl = "https://example.com/live",
            ),
        )
    }

    @Test
    fun resolveBrowsingUrl_returnsAboutBlankWhenNothingKnown() {
        assertEquals(
            "about:blank",
            resolveBrowsingUrl(requestedUrl = null, activeTabUrl = null, currentUrl = ""),
        )
    }

    @Test
    fun shouldLoadRequestedUrl_loadsWhenRenderedDocumentDiffersFromRequest() {
        assertTrue(
            shouldLoadRequestedUrl(
                requestedUrl = "https://example.com",
                renderedUrl = "https://www.google.com/",
                lastRequestedUrl = null,
            ),
        )
    }

    @Test
    fun shouldLoadRequestedUrl_doesNotReloadRedirectedDestinationOfSameRequest() {
        assertFalse(
            shouldLoadRequestedUrl(
                requestedUrl = "https://example.com",
                renderedUrl = "https://example.com/landing",
                lastRequestedUrl = "https://example.com",
            ),
        )
    }

    @Test
    fun shouldLoadRequestedUrl_loadsAgainWhenUserResubmitsSameUrlAfterLeaving() {
        assertTrue(
            shouldLoadRequestedUrl(
                requestedUrl = "https://example.com",
                renderedUrl = "https://news.example.org/",
                lastRequestedUrl = "https://news.example.org/",
            ),
        )
    }

    @Test
    fun resolveBrowsingUrl_afterBackNavigationFollowsTheRestoredDocument() {
        assertEquals(
            "https://example.com",
            resolveBrowsingUrl(
                requestedUrl = null,
                activeTabUrl = "https://example.com",
                currentUrl = "https://example.com",
            ),
        )
    }

    @Test
    fun shouldLoadRequestedUrl_skipsBlankRequest() {
        assertFalse(
            shouldLoadRequestedUrl(requestedUrl = "", renderedUrl = "https://example.com", lastRequestedUrl = null),
        )
    }

    @Test
    fun resolveAddressBarPrefill_returnsCurrentUrlWhenBrowsingWebPage() {
        assertEquals("https://example.com/path", resolveAddressBarPrefill("https://example.com/path", isHomeMode = false))
        assertEquals("https://maho.dev", resolveAddressBarPrefill("  https://maho.dev  ", isHomeMode = false))
        assertEquals("http://localhost:8080", resolveAddressBarPrefill("http://localhost:8080", isHomeMode = false))
    }

    @Test
    fun resolveAddressBarPrefill_returnsEmptyForBlankAndAboutBlank() {
        assertEquals("", resolveAddressBarPrefill("", isHomeMode = false))
        assertEquals("", resolveAddressBarPrefill("   ", isHomeMode = false))
        assertEquals("", resolveAddressBarPrefill(null, isHomeMode = false))
        assertEquals("", resolveAddressBarPrefill("about:blank", isHomeMode = false))
        assertEquals("", resolveAddressBarPrefill("   about:blank   ", isHomeMode = false))
    }

    @Test
    fun resolveAddressBarPrefill_returnsEmptyForSystemSurfaceUrls() {
        assertEquals("", resolveAddressBarPrefill("chrome://newtab", isHomeMode = false))
        assertEquals("", resolveAddressBarPrefill("chrome://newtab/", isHomeMode = false))
        assertEquals("", resolveAddressBarPrefill("chrome://history", isHomeMode = false))
        assertEquals("", resolveAddressBarPrefill("chrome://downloads", isHomeMode = false))
    }

    @Test
    fun resolveAddressBarPrefill_returnsEmptyInHomeMode() {
        assertEquals("", resolveAddressBarPrefill("https://example.com", isHomeMode = true))
        assertEquals("", resolveAddressBarPrefill("chrome://newtab", isHomeMode = true))
        assertEquals("", resolveAddressBarPrefill("", isHomeMode = true))
        assertEquals("", resolveAddressBarPrefill(null, isHomeMode = true))
    }

    @Test
    fun shouldReuseActiveTab_returnsTrueOnlyWhenBrowsingWithActiveTab() {
        assertTrue(shouldReuseActiveTab(isBrowsingRoute = true, hasActiveTab = true))
        assertFalse(shouldReuseActiveTab(isBrowsingRoute = true, hasActiveTab = false))
        assertFalse(shouldReuseActiveTab(isBrowsingRoute = false, hasActiveTab = true))
        assertFalse(shouldReuseActiveTab(isBrowsingRoute = false, hasActiveTab = false))
    }
}
