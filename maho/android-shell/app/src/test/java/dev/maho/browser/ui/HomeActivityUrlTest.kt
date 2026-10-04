package dev.maho.browser.ui

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class HomeActivityUrlTest {

    @Test
    fun isSystemSurfaceUrl_returnsTrueForSystemPages() {
        assertTrue(isSystemSurfaceUrl("chrome://newtab"))
        assertTrue(isSystemSurfaceUrl("chrome://newtab/"))
        assertTrue(isSystemSurfaceUrl("about:blank"))
        assertTrue(isSystemSurfaceUrl("chrome://downloads"))
        assertTrue(isSystemSurfaceUrl("chrome://history"))
        assertTrue(isSystemSurfaceUrl(""))
        assertTrue(isSystemSurfaceUrl("   "))
    }

    @Test
    fun isSystemSurfaceUrl_returnsFalseForStandardWebPages() {
        assertFalse(isSystemSurfaceUrl("https://example.com"))
        assertFalse(isSystemSurfaceUrl("http://newtab.example.com"))
        assertFalse(isSystemSurfaceUrl("https://news.ycombinator.com"))
    }
}
