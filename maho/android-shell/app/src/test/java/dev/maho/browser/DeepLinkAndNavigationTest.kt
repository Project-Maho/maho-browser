package dev.maho.browser

import dev.maho.browser.bridge.BridgeNavigation
import dev.maho.browser.models.CoreUpdate
import dev.maho.browser.models.TabLifecycleState
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class DeepLinkAndNavigationTest {

    @Test
    fun normalizeBrowserInput_preservesHttpsUrl() {
        assertEquals(
            "https://example.com/path",
            normalizeBrowserInput("  https://example.com/path  ", "Google"),
        )
    }

    @Test
    fun normalizeBrowserInput_promotesDomainToHttps() {
        assertEquals("https://example.com", normalizeBrowserInput("example.com", "Google"))
    }

    @Test
    fun normalizeBrowserInput_encodesSearchQueries() {
        assertEquals(
            "https://www.google.com/search?q=cats+%26+dogs",
            normalizeBrowserInput("cats & dogs", "Google"),
        )
    }

    @Test
    fun normalizeBrowserInput_returnsBlankForBlankInput() {
        assertEquals("", normalizeBrowserInput("   ", "Google"))
    }

    @Test
    fun isNavigationUpdate_onlyAcceptsNavigationRelatedUpdates() {
        val navUpdate = CoreUpdate.NavigationStateChanged(
            tabId = "tab-1",
            url = "https://example.com",
            title = "Example",
            canGoBack = true,
            canGoForward = false,
            isLoading = false,
            progress = 0.42,
        )
        val unrelatedUpdate = CoreUpdate.SpaceDeleted(spaceId = "space-1")

        assertTrue(BridgeNavigation.isNavigationUpdate(navUpdate))
        assertFalse(BridgeNavigation.isNavigationUpdate(unrelatedUpdate))
    }

    @Test
    fun snapshotFrom_preservesNavigationState() {
        val update = CoreUpdate.NavigationStateChanged(
            tabId = "tab-2",
            url = "https://example.com/docs",
            title = "Docs",
            canGoBack = false,
            canGoForward = true,
            isLoading = true,
            progress = 0.88,
        )

        val snapshot = BridgeNavigation.snapshotFrom(update)

        assertEquals("tab-2", snapshot.tabId)
        assertEquals("https://example.com/docs", snapshot.url)
        assertEquals("Docs", snapshot.title)
        assertFalse(snapshot.canGoBack)
        assertTrue(snapshot.canGoForward)
        assertTrue(snapshot.isLoading)
        assertEquals(0.88, snapshot.progress, 0.0)
        assertNull(snapshot.zoomLevel)
    }

    @Test
    fun tabLifecycleChanged_roundTripsThroughSerialization() {
        val original = CoreUpdate.TabLifecycleChanged(
            tabId = "tab-3",
            state = TabLifecycleState.Archived(metadataOnly = true),
        )

        val json = dev.maho.browser.support.MahoJson.instance
        val encoded = json.encodeToString(CoreUpdate.serializer(), original)
        val decoded = json.decodeFromString(CoreUpdate.serializer(), encoded)

        assertEquals(original, decoded)
    }
}

private fun normalizeBrowserInput(input: String, searchEngine: String): String {
    val targetClass = Class.forName("dev.maho.browser.ui.MainBrowserScreenKt")
    val method = targetClass.getDeclaredMethod("normalizeBrowserInput", String::class.java, String::class.java)
    method.isAccessible = true
    return method.invoke(null, input, searchEngine) as String
}
