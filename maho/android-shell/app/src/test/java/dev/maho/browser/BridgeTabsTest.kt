package dev.maho.browser

import dev.maho.browser.models.ArchivedTabViewModel
import dev.maho.browser.models.CoreUpdate
import dev.maho.browser.models.ShellEvent
import dev.maho.browser.models.TabViewModel
import dev.maho.browser.support.MahoJson
import org.junit.Test
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue

class BridgeTabsTest {

    private val json = MahoJson.instance

    @Test
    fun `createTab ShellEvent serializes correctly`() {
        val event = ShellEvent.CreateTab(spaceId = "space-1", url = "https://example.com", parentId = null)
        val encoded = json.encodeToString(ShellEvent.serializer(), event)
        assertTrue(encoded.contains("\"kind\":\"create_tab\""))
        assertTrue(encoded.contains("\"space_id\":\"space-1\""))
        assertTrue(encoded.contains("\"url\":\"https://example.com\""))
    }

    @Test
    fun `closeTab ShellEvent serializes correctly`() {
        val event = ShellEvent.CloseTab(tabId = "tab-1")
        val encoded = json.encodeToString(ShellEvent.serializer(), event)
        assertTrue(encoded.contains("\"kind\":\"close_tab\""))
        assertTrue(encoded.contains("\"tab_id\":\"tab-1\""))
    }

    @Test
    fun `archiveTab ShellEvent serializes correctly`() {
        val event = ShellEvent.ArchiveTabById(tabId = "tab-1")
        val encoded = json.encodeToString(ShellEvent.serializer(), event)
        assertTrue(encoded.contains("\"kind\":\"archive_tab_by_id\""))
        assertTrue(encoded.contains("\"tab_id\":\"tab-1\""))
    }

    @Test
    fun `activateTab ShellEvent serializes correctly`() {
        val event = ShellEvent.ActivateTab(tabId = "tab-1")
        val encoded = json.encodeToString(ShellEvent.serializer(), event)
        assertTrue(encoded.contains("\"kind\":\"activate_tab\""))
    }

    @Test
    fun `reorderTab ShellEvent serializes correctly`() {
        val event = ShellEvent.ReorderTab(tabId = "tab-1", beforeTabId = "tab-3")
        val encoded = json.encodeToString(ShellEvent.serializer(), event)
        assertTrue(encoded.contains("\"kind\":\"reorder_tab\""))
        assertTrue(encoded.contains("\"before_tab_id\":\"tab-3\""))
    }

    @Test
    fun `createTab round-trips correctly`() {
        val original = ShellEvent.CreateTab(spaceId = "space-1", url = "https://maho.dev", parentId = "tab-parent")
        val encoded = json.encodeToString(ShellEvent.serializer(), original)
        val decoded = json.decodeFromString(ShellEvent.serializer(), encoded)
        assertEquals(original, decoded)
    }

    @Test
    fun `tabCreated CoreUpdate decodes from JSON`() {
        val jsonStr = """
        {
            "kind": "tab_created",
            "tab": {
                "id": "tab-1",
                "spaceId": "space-1",
                "title": "Example",
                "url": "https://example.com",
                "favicon": null,
                "isLoading": false,
                "isPinned": false,
                "isFavorite": false,
                "isMuted": false,
                "isPlayingAudio": false,
                "lifecycleState": "active",
                "children": [],
                "createdAt": "2024-01-01T00:00:00Z",
                "lastActiveAt": "2024-01-01T00:00:00Z",
                "role": {"type": "normal"}
            }
        }
        """.trimIndent()
        val decoded = json.decodeFromString(CoreUpdate.serializer(), jsonStr)
        assertTrue(decoded is CoreUpdate.TabCreated)
        assertEquals("tab-1", (decoded as CoreUpdate.TabCreated).tab.id)
    }

    @Test
    fun `tabClosed CoreUpdate decodes from JSON`() {
        val jsonStr = """{"kind":"tab_closed","tab_id":"tab-2","animated":true}"""
        val decoded = json.decodeFromString(CoreUpdate.serializer(), jsonStr)
        assertTrue(decoded is CoreUpdate.TabClosed)
        assertEquals("tab-2", (decoded as CoreUpdate.TabClosed).tabId)
    }

    @Test
    fun `ArchivedTabViewModel deserializes core archive payload`() {
        val archived = json.decodeFromString(
            ArchivedTabViewModel.serializer(),
            """{"id":"tab-1","spaceId":"space-1","title":"Known article","url":"https://example.com/article","favicon":null,"archivedAt":"2026-08-09T12:00:00Z"}""",
        )

        assertEquals("tab-1", archived.id)
        assertEquals("Known article", archived.title)
        assertEquals("2026-08-09T12:00:00Z", archived.archivedAt)
    }

    @Test
    fun `TabViewModel deserializes from JSON`() {
        val jsonStr = """
        {
            "id": "tab-1",
            "spaceId": "space-1",
            "title": "Maho Browser",
            "url": "https://maho.dev",
            "favicon": null,
            "isLoading": false,
            "isPinned": true,
            "isFavorite": false,
            "isMuted": false,
            "isPlayingAudio": false,
            "lifecycleState": "active",
            "children": [],
            "createdAt": "2024-01-01T00:00:00Z",
            "lastActiveAt": "2024-01-01T00:00:00Z",
            "role": {"type": "pinned"}
        }
        """.trimIndent()
        val tab = json.decodeFromString(TabViewModel.serializer(), jsonStr)
        assertEquals("tab-1", tab.id)
        assertEquals("space-1", tab.spaceId)
        assertEquals("Maho Browser", tab.title)
        assertTrue(tab.isPinned)
    }
}
