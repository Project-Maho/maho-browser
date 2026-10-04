package dev.maho.browser

import dev.maho.browser.models.*
import dev.maho.browser.support.MahoJson
import kotlinx.serialization.encodeToString
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive
import org.junit.Test
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue

class KindDiscriminatorTest {

    private val json = MahoJson.instance

    @Test
    fun `ShellEvent uses kind discriminator`() {
        val event = ShellEvent.NavigateTo(tabId = "tab-1", url = "https://example.com")
        val encoded = json.encodeToString(ShellEvent.serializer(), event)
        val parsed = json.parseToJsonElement(encoded).jsonObject
        assertTrue(parsed.containsKey("kind"))
        assertEquals("navigate_to", parsed["kind"]?.jsonPrimitive?.content)
    }

    @Test
    fun `CoreUpdate uses kind discriminator`() {
        val update = CoreUpdate.TabClosed(tabId = "tab-1", animated = true)
        val encoded = json.encodeToString(CoreUpdate.serializer(), update)
        val parsed = json.parseToJsonElement(encoded).jsonObject
        assertTrue(parsed.containsKey("kind"))
        assertEquals("tab_closed", parsed["kind"]?.jsonPrimitive?.content)
    }

    @Test
    fun `QuickAction uses kind discriminator`() {
        val action = QuickAction.NewTab
        val encoded = json.encodeToString(QuickAction.serializer(), action)
        val parsed = json.parseToJsonElement(encoded).jsonObject
        assertTrue(parsed.containsKey("kind"))
        assertEquals("new_tab", parsed["kind"]?.jsonPrimitive?.content)
    }

    @Test
    fun `SyncStatus uses kind discriminator`() {
        val status = SyncStatus.Syncing(progress = 0.5)
        val encoded = json.encodeToString(SyncStatus.serializer(), status)
        val parsed = json.parseToJsonElement(encoded).jsonObject
        assertTrue(parsed.containsKey("kind"))
        assertEquals("syncing", parsed["kind"]?.jsonPrimitive?.content)
    }

    @Test
    fun `MemoryAction uses kind discriminator`() {
        val action = MemoryAction.FrozeTabs(count = 3)
        val encoded = json.encodeToString(MemoryAction.serializer(), action)
        val parsed = json.parseToJsonElement(encoded).jsonObject
        assertTrue(parsed.containsKey("kind"))
        assertEquals("froze_tabs", parsed["kind"]?.jsonPrimitive?.content)
    }

    @Test
    fun `TabLifecycleState uses kind discriminator`() {
        val state = TabLifecycleState.Archived(metadataOnly = false)
        val encoded = json.encodeToString(TabLifecycleState.serializer(), state)
        val parsed = json.parseToJsonElement(encoded).jsonObject
        assertTrue(parsed.containsKey("kind"))
        assertEquals("archived", parsed["kind"]?.jsonPrimitive?.content)
    }

    @Test
    fun `ATCAction does NOT use kind discriminator`() {
        val action = ATCAction.Close
        val encoded = kotlinx.serialization.json.Json.encodeToString(ATCAction.serializer(), action)
        val parsed = kotlinx.serialization.json.Json.parseToJsonElement(encoded)
        assertTrue(parsed is kotlinx.serialization.json.JsonPrimitive)
        assertEquals("close", parsed.jsonPrimitive.content)
    }

    @Test
    fun `DefaultLinkBehavior does NOT use kind discriminator`() {
        val behavior = DefaultLinkBehavior.MostRecentSpace
        val encoded = kotlinx.serialization.json.Json.encodeToString(DefaultLinkBehavior.serializer(), behavior)
        val parsed = kotlinx.serialization.json.Json.parseToJsonElement(encoded)
        assertTrue(parsed is kotlinx.serialization.json.JsonPrimitive)
        assertEquals("most_recent_space", parsed.jsonPrimitive.content)
    }

    @Test
    fun `All ShellEvent variants have correct kind values`() {
        val events = listOf(
            ShellEvent.NavigateTo("t", "u") to "navigate_to",
            ShellEvent.GoBack("t") to "go_back",
            ShellEvent.GoForward("t") to "go_forward",
            ShellEvent.Reload("t") to "reload",
            ShellEvent.Stop("t") to "stop",
            ShellEvent.CreateTab("s") to "create_tab",
            ShellEvent.CloseTab("t") to "close_tab",
            ShellEvent.ActivateTab("t") to "activate_tab",
            ShellEvent.AppLaunched to "app_launched",
            ShellEvent.AppWillTerminate to "app_will_terminate",
        )
        for ((event, expectedKind) in events) {
            val encoded = json.encodeToString(ShellEvent.serializer(), event)
            val parsed = json.parseToJsonElement(encoded).jsonObject
            assertEquals("Failed for $event", expectedKind, parsed["kind"]?.jsonPrimitive?.content)
        }
    }

    @Test
    fun `All CoreUpdate variants have correct kind values`() {
        val updates = listOf(
            CoreUpdate.SpaceDeleted("s") to "space_deleted",
            CoreUpdate.SpaceRenamed("s", "n") to "space_renamed",
            CoreUpdate.SpaceRecolored("s", SpaceColor(0.0, 0.0, 0.0)) to "space_recolored",
            CoreUpdate.ActiveSpaceChanged("s") to "active_space_changed",
            CoreUpdate.AllNotificationsDismissed to "all_notifications_dismissed",
            CoreUpdate.NavigateTab("t", "u") to "navigate_tab",
            CoreUpdate.TabLifecycleChanged("t", TabLifecycleState.Active) to "tab_lifecycle_changed",
        )
        for ((update, expectedKind) in updates) {
            val encoded = json.encodeToString(CoreUpdate.serializer(), update)
            val parsed = json.parseToJsonElement(encoded).jsonObject
            assertEquals("Failed for $update", expectedKind, parsed["kind"]?.jsonPrimitive?.content)
        }
    }
}
