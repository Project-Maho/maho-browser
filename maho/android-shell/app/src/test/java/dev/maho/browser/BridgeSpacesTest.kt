package dev.maho.browser

import dev.maho.browser.models.CoreUpdate
import dev.maho.browser.models.FolderViewModel
import dev.maho.browser.models.ShellEvent
import dev.maho.browser.models.SpaceColor
import dev.maho.browser.models.SpaceViewModel
import dev.maho.browser.support.MahoJson
import org.junit.Test
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue

class BridgeSpacesTest {

    private val json = MahoJson.instance

    @Test
    fun `createSpace ShellEvent serializes with SpaceColor`() {
        val color = SpaceColor(hue = 0.5, saturation = 0.8, brightness = 0.9)
        val event = ShellEvent.CreateSpace(name = "Work", color = color, profileId = "profile-1")
        val encoded = json.encodeToString(ShellEvent.serializer(), event)
        assertTrue(encoded.contains("\"kind\":\"create_space\""))
        assertTrue(encoded.contains("\"name\":\"Work\""))
        assertTrue(encoded.contains("\"hue\":0.5"))
        assertTrue(encoded.contains("\"saturation\":0.8"))
        assertTrue(encoded.contains("\"brightness\":0.9"))
    }

    @Test
    fun `SpaceColor round-trips correctly`() {
        val original = SpaceColor(hue = 0.3, saturation = 0.6, brightness = 0.7)
        val encoded = json.encodeToString(SpaceColor.serializer(), original)
        val decoded = json.decodeFromString(SpaceColor.serializer(), encoded)
        assertEquals(original, decoded)
    }

    @Test
    fun `createSpace round-trips correctly`() {
        val color = SpaceColor(hue = 0.5, saturation = 0.8, brightness = 0.9)
        val original = ShellEvent.CreateSpace(name = "Work", color = color, profileId = "profile-1")
        val encoded = json.encodeToString(ShellEvent.serializer(), original)
        val decoded = json.decodeFromString(ShellEvent.serializer(), encoded)
        assertEquals(original, decoded)
    }

    @Test
    fun `deleteSpace ShellEvent serializes correctly`() {
        val event = ShellEvent.DeleteSpace(spaceId = "space-1")
        val encoded = json.encodeToString(ShellEvent.serializer(), event)
        assertTrue(encoded.contains("\"kind\":\"delete_space\""))
        assertTrue(encoded.contains("\"space_id\":\"space-1\""))
    }

    @Test
    fun `activateSpace round-trips correctly`() {
        val original = ShellEvent.ActivateSpace(spaceId = "space-1")
        val encoded = json.encodeToString(ShellEvent.serializer(), original)
        val decoded = json.decodeFromString(ShellEvent.serializer(), encoded)
        assertEquals(original, decoded)
    }

    @Test
    fun `spaceCreated CoreUpdate decodes from JSON`() {
        val jsonStr = """
        {
            "kind": "space_created",
            "space": {
                "id": "space-1",
                "name": "Personal",
                "color": {"hue": 0.3, "saturation": 0.7, "brightness": 0.8},
                "tabCount": 3,
                "isActive": true,
                "icon": null
            }
        }
        """.trimIndent()
        val decoded = json.decodeFromString(CoreUpdate.serializer(), jsonStr)
        assertTrue(decoded is CoreUpdate.SpaceCreated)
        assertEquals("space-1", (decoded as CoreUpdate.SpaceCreated).space.id)
        assertEquals("Personal", decoded.space.name)
        assertEquals(3, decoded.space.tabCount)
    }

    @Test
    fun `SpaceViewModel deserializes from JSON`() {
        val jsonStr = """
        {
            "id": "space-1",
            "name": "Work",
            "color": {"hue": 0.5, "saturation": 0.8, "brightness": 0.9},
            "tabCount": 5,
            "isActive": true,
            "icon": null
        }
        """.trimIndent()
        val space = json.decodeFromString(SpaceViewModel.serializer(), jsonStr)
        assertEquals("space-1", space.id)
        assertEquals("Work", space.name)
        assertEquals(5, space.tabCount)
    }

    @Test
    fun `FolderViewModel deserializes from JSON`() {
        val jsonStr = """
        {
            "id": "folder-1",
            "name": "Research",
            "tabCount": 3,
            "isExpanded": true,
            "tabIds": ["tab-1", "tab-2", "tab-3"],
            "isPinned": false,
            "parentFolderId": null
        }
        """.trimIndent()
        val folder = json.decodeFromString(FolderViewModel.serializer(), jsonStr)
        assertEquals("folder-1", folder.id)
        assertEquals("Research", folder.name)
        assertEquals(3, folder.tabIds.size)
    }
}
