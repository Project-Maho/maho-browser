package dev.maho.browser.ui

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class AddressSearchBehaviorTest {

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
