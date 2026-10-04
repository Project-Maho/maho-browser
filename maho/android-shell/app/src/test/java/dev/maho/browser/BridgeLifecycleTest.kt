package dev.maho.browser

import dev.maho.browser.models.CoreUpdate
import dev.maho.browser.models.MemoryPressureLevel
import dev.maho.browser.models.ShellEvent
import dev.maho.browser.support.MahoJson
import org.junit.Test
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue

class BridgeLifecycleTest {

    private val json = MahoJson.instance

    @Test
    fun `appLaunched serializes with correct kind`() {
        val event = ShellEvent.AppLaunched
        val encoded = json.encodeToString(ShellEvent.serializer(), event)
        assertTrue(encoded.contains("\"kind\":\"app_launched\""))
    }

    @Test
    fun `appBackgrounded serializes with correct kind`() {
        val event = ShellEvent.AppWillTerminate
        val encoded = json.encodeToString(ShellEvent.serializer(), event)
        assertTrue(encoded.contains("\"kind\":\"app_will_terminate\""))
    }

    @Test
    fun `appForegrounded serializes with correct kind`() {
        val event = ShellEvent.AppLaunched
        val encoded = json.encodeToString(ShellEvent.serializer(), event)
        assertTrue(encoded.contains("\"kind\":\"app_launched\""))
    }

    @Test
    fun `saveState serializes with correct kind`() {
        val event = ShellEvent.MemoryWarning(level = MemoryPressureLevel.Warning)
        val encoded = json.encodeToString(ShellEvent.serializer(), event)
        assertTrue(encoded.contains("\"kind\":\"memory_warning\""))
    }

    @Test
    fun `loadState serializes with correct kind`() {
        val event = ShellEvent.ToggleSync
        val encoded = json.encodeToString(ShellEvent.serializer(), event)
        assertTrue(encoded.contains("\"kind\":\"toggle_sync\""))
    }

    @Test
    fun `lifecycle ShellEvents round-trip correctly`() {
        val events = listOf(
            ShellEvent.AppLaunched,
            ShellEvent.AppWillTerminate,
            ShellEvent.MemoryWarning(level = MemoryPressureLevel.Critical),
            ShellEvent.ToggleSync,
        )
        for (event in events) {
            val encoded = json.encodeToString(ShellEvent.serializer(), event)
            val decoded = json.decodeFromString(ShellEvent.serializer(), encoded)
            assertEquals(event, decoded)
        }
    }

    @Test
    fun `stateRestored CoreUpdate decodes from JSON`() {
        val jsonStr = """{"kind":"tab_lifecycle_changed","tab_id":"tab-1","state":{"kind":"active"}}"""
        val decoded = json.decodeFromString(CoreUpdate.serializer(), jsonStr)
        assertTrue(decoded is CoreUpdate.TabLifecycleChanged)
    }

    @Test
    fun `stateSaved CoreUpdate decodes from JSON`() {
        val jsonStr = """{"kind":"tab_lifecycle_changed","tab_id":"tab-1","state":{"kind":"frozen"}}"""
        val decoded = json.decodeFromString(CoreUpdate.serializer(), jsonStr)
        assertTrue(decoded is CoreUpdate.TabLifecycleChanged)
    }
}
