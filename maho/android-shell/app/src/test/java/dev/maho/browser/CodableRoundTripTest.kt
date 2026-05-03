package dev.maho.browser

import dev.maho.browser.models.*
import dev.maho.browser.support.MahoJson
import kotlinx.serialization.encodeToString
import kotlinx.serialization.json.Json
import org.junit.Test
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue

class CodableRoundTripTest {

    private val json = MahoJson.instance

    @Test
    fun `ShellEvent NavigateTo round trip`() {
        val original = ShellEvent.NavigateTo(tabId = "tab-1", url = "https://example.com")
        val encoded = json.encodeToString(ShellEvent.serializer(), original)
        val decoded = json.decodeFromString(ShellEvent.serializer(), encoded)
        assertEquals(original, decoded)
    }

    @Test
    fun `ShellEvent CreateTab round trip`() {
        val original = ShellEvent.CreateTab(spaceId = "space-1", url = "https://example.com", parentId = "tab-0")
        val encoded = json.encodeToString(ShellEvent.serializer(), original)
        val decoded = json.decodeFromString(ShellEvent.serializer(), encoded)
        assertEquals(original, decoded)
    }

    @Test
    fun `ShellEvent CreateSpace round trip`() {
        val original = ShellEvent.CreateSpace(
            name = "Work",
            color = SpaceColor(hue = 0.5, saturation = 0.8, brightness = 0.9),
            profileId = "profile-1"
        )
        val encoded = json.encodeToString(ShellEvent.serializer(), original)
        val decoded = json.decodeFromString(ShellEvent.serializer(), encoded)
        assertEquals(original, decoded)
    }

    @Test
    fun `CoreUpdate FullState round trip`() {
        val space = SpaceViewModel(
            id = "space-1",
            name = "Work",
            color = SpaceColor(hue = 0.5, saturation = 0.8, brightness = 0.9),
            tabCount = 2,
            isActive = true,
        )
        val tab = TabViewModel(
            id = "tab-1",
            spaceId = "space-1",
            title = "Example",
            url = "https://example.com",
            isLoading = false,
            isPinned = false,
            isFavorite = false,
            isMuted = false,
            isPlayingAudio = false,
            lifecycleState = "active",
            children = emptyList(),
            createdAt = "2024-01-01T00:00:00Z",
            lastActiveAt = "2024-01-01T00:00:00Z",
        )
        val snapshot = AppStateSnapshot(
            spaces = listOf(space),
            activeSpaceId = "space-1",
            tabs = mapOf("space-1" to listOf(tab)),
            syncStatus = SyncStatus.Idle,
        )
        val original = CoreUpdate.FullState(state = snapshot)
        val encoded = json.encodeToString(CoreUpdate.serializer(), original)
        val decoded = json.decodeFromString(CoreUpdate.serializer(), encoded)
        assertEquals(original, decoded)
    }

    @Test
    fun `CoreUpdate TabCreated round trip`() {
        val tab = TabViewModel(
            id = "tab-2",
            spaceId = "space-1",
            title = "New Tab",
            url = "https://newtab.com",
            isLoading = true,
            isPinned = false,
            isFavorite = false,
            isMuted = false,
            isPlayingAudio = false,
            lifecycleState = "active",
            children = emptyList(),
            createdAt = "2024-01-01T00:00:00Z",
            lastActiveAt = "2024-01-01T00:00:00Z",
        )
        val original = CoreUpdate.TabCreated(tab = tab)
        val encoded = json.encodeToString(CoreUpdate.serializer(), original)
        val decoded = json.decodeFromString(CoreUpdate.serializer(), encoded)
        assertEquals(original, decoded)
    }

    @Test
    fun `ATCAction Close serializes as plain string`() {
        val action: ATCAction = ATCAction.Close
        val element = Json.encodeToString(ATCAction.serializer(), action)
        assertEquals("\"close\"", element)
        val decoded = Json.decodeFromString(ATCAction.serializer(), element)
        assertEquals(action, decoded)
    }

    @Test
    fun `ATCAction Route serializes as object`() {
        val action: ATCAction = ATCAction.Route(spaceId = "space-2")
        val element = Json.encodeToString(ATCAction.serializer(), action)
        assertTrue(element.contains("\"route\""))
        assertTrue(element.contains("\"space_id\""))
        val decoded = Json.decodeFromString(ATCAction.serializer(), element)
        assertEquals(action, decoded)
    }

    @Test
    fun `DefaultLinkBehavior CurrentSpace serializes as plain string`() {
        val behavior: DefaultLinkBehavior = DefaultLinkBehavior.CurrentSpace
        val element = Json.encodeToString(DefaultLinkBehavior.serializer(), behavior)
        assertEquals("\"current_space\"", element)
        val decoded = Json.decodeFromString(DefaultLinkBehavior.serializer(), element)
        assertEquals(behavior, decoded)
    }

    @Test
    fun `DefaultLinkBehavior SpecificSpace serializes as object`() {
        val behavior: DefaultLinkBehavior = DefaultLinkBehavior.SpecificSpace(spaceId = "space-3")
        val element = Json.encodeToString(DefaultLinkBehavior.serializer(), behavior)
        assertTrue(element.contains("\"specific_space\""))
        assertTrue(element.contains("\"space_id\""))
        val decoded = Json.decodeFromString(DefaultLinkBehavior.serializer(), element)
        assertEquals(behavior, decoded)
    }

    @Test
    fun `Settings round trip`() {
        val settings = Settings(
            general = GeneralSettings(
                defaultSearchEngine = SettingsSearchEngine(name = "DuckDuckGo", urlTemplate = "https://duckduckgo.com/?q={query}", isDefault = true),
                todayTabTimeoutHours = 24.0,
                restoreOnLaunch = RestorePolicy.RestoreAll,
                downloadPath = "/downloads",
                autoplayPolicy = AutoplayPolicy.BlockAudio,
            ),
            appearance = AppearanceSettings(
                theme = Theme.Dark,
                density = Density.Compact,
                sidebarWidth = 240.0,
                showTabBar = true,
                windowTransparency = false,
            ),
            privacy = PrivacySettings(
                doNotTrack = true,
                blockThirdPartyCookies = true,
                contentBlockerEnabled = true,
                popupBlockerEnabled = true,
            ),
            reader = ReaderSettings(
                fontFamily = "Sans",
                fontSize = 16.0,
                theme = ReaderTheme.Dark,
            ),
            keyboardShortcuts = emptyList(),
            perSiteSettings = emptyList(),
        )
        val encoded = json.encodeToString(Settings.serializer(), settings)
        val decoded = json.decodeFromString(Settings.serializer(), encoded)
        assertEquals(settings, decoded)
    }
}
