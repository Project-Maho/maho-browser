package dev.maho.browser

import dev.maho.browser.models.*
import dev.maho.browser.support.MahoJson
import kotlinx.serialization.encodeToString
import kotlinx.serialization.json.Json
import org.junit.Test
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
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
            isMuted = false,
            isPlayingAudio = false,
            lifecycleState = "active",
            children = emptyList(),
            createdAt = "2024-01-01T00:00:00Z",
            lastActiveAt = "2024-01-01T00:00:00Z",
            role = TabRole("normal"),
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
            isMuted = false,
            isPlayingAudio = false,
            lifecycleState = "active",
            children = emptyList(),
            createdAt = "2024-01-01T00:00:00Z",
            lastActiveAt = "2024-01-01T00:00:00Z",
            role = TabRole("normal"),
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
                pinnedCloseBehavior = PinnedCloseBehavior.ResetSwitch,
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

    @Test
    fun `PinnedCloseBehavior all wire values round trip`() {
        val cases = mapOf(
            PinnedCloseBehavior.Switch to "\"switch\"",
            PinnedCloseBehavior.Reset to "\"reset\"",
            PinnedCloseBehavior.ResetSwitch to "\"reset-switch\"",
            PinnedCloseBehavior.UnloadSwitch to "\"unload-switch\"",
            PinnedCloseBehavior.ResetUnloadSwitch to "\"reset-unload-switch\"",
            PinnedCloseBehavior.Close to "\"close\"",
        )
        for ((behavior, expectedWire) in cases) {
            val encoded = Json.encodeToString(PinnedCloseBehavior.serializer(), behavior)
            assertEquals("wire value for $behavior", expectedWire, encoded)
            val decoded = Json.decodeFromString(PinnedCloseBehavior.serializer(), encoded)
            assertEquals("round-trip for $behavior", behavior, decoded)
        }
    }

    @Test
    fun `GeneralSettings missing pinnedCloseBehavior defaults to Switch`() {
        val jsonWithoutField = """
            {
                "defaultSearchEngine": {"name": "DDG", "urlTemplate": "https://ddg.gg/?q={query}", "isDefault": true},
                "todayTabTimeoutHours": 8.0,
                "restoreOnLaunch": "restore_all",
                "downloadPath": "/tmp",
                "autoplayPolicy": "allow"
            }
        """.trimIndent()
        val decoded = json.decodeFromString(GeneralSettings.serializer(), jsonWithoutField)
        assertEquals(PinnedCloseBehavior.Switch, decoded.pinnedCloseBehavior)
    }

    @Test
    fun `GeneralSettingsUpdate pinnedCloseBehavior null when absent`() {
        val emptyUpdate = GeneralSettingsUpdate()
        assertEquals(null, emptyUpdate.pinnedCloseBehavior)
        val encoded = json.encodeToString(GeneralSettingsUpdate.serializer(), emptyUpdate)
        val decoded = json.decodeFromString(GeneralSettingsUpdate.serializer(), encoded)
        assertEquals(null, decoded.pinnedCloseBehavior)
    }

    @Test
    fun `GeneralSettingsUpdate pinnedCloseBehavior round trip`() {
        val update = GeneralSettingsUpdate(pinnedCloseBehavior = PinnedCloseBehavior.Close)
        val encoded = json.encodeToString(GeneralSettingsUpdate.serializer(), update)
        assertTrue(encoded.contains("\"close\""))
        val decoded = json.decodeFromString(GeneralSettingsUpdate.serializer(), encoded)
        assertEquals(PinnedCloseBehavior.Close, decoded.pinnedCloseBehavior)
    }

    @Test
    fun `SuggestionType all wire names round trip to exact core values`() {
        val cases = mapOf(
            SuggestionType.Tab         to "\"tab\"",
            SuggestionType.Bookmark    to "\"bookmark\"",
            SuggestionType.History     to "\"history\"",
            SuggestionType.Action      to "\"action\"",
            SuggestionType.Navigation  to "\"navigation\"",
            SuggestionType.Search      to "\"search\"",
            SuggestionType.Calculator  to "\"calculator\"",
            SuggestionType.UnitConversion to "\"unitconversion\"",
            SuggestionType.ArchivedTab to "\"archivedtab\"",
            SuggestionType.ClosedTab   to "\"closedtab\"",
            SuggestionType.Folder      to "\"folder\"",
            SuggestionType.AiAnswer    to "\"aiAnswer\"",
        )
        for ((kind, expectedWire) in cases) {
            val encoded = json.encodeToString(SuggestionType.serializer(), kind)
            assertEquals("wire value for $kind", expectedWire, encoded)
            val decoded = json.decodeFromString(SuggestionType.serializer(), encoded)
            assertEquals("round-trip for $kind", kind, decoded)
        }
    }

    @Test
    fun `CoreUpdate CommandBarResults decodes all new suggestion kinds with executionPayload`() {
        val rawJson = """
            {
                "kind": "command_bar_results",
                "suggestions": [
                    {
                        "kind": "search",
                        "key": "s1",
                        "title": "maho browser",
                        "relevanceScore": 0.9,
                        "executionPayload": "{\"query\":\"maho browser\"}"
                    },
                    {
                        "kind": "aiAnswer",
                        "key": "ai1",
                        "title": "Ask Maho AI",
                        "relevanceScore": 0.85
                    },
                    {
                        "kind": "folder",
                        "key": "f1",
                        "title": "Work",
                        "relevanceScore": 0.7
                    },
                    {
                        "kind": "unitconversion",
                        "key": "uc1",
                        "title": "1 km = 0.621 mi",
                        "relevanceScore": 0.8,
                        "executionPayload": "{\"from\":\"km\",\"to\":\"mi\"}"
                    },
                    {
                        "kind": "archivedtab",
                        "key": "at1",
                        "title": "Old Tab",
                        "relevanceScore": 0.6
                    },
                    {
                        "kind": "closedtab",
                        "key": "ct1",
                        "title": "Recently Closed",
                        "relevanceScore": 0.5
                    }
                ]
            }
        """.trimIndent()

        val update = json.decodeFromString(CoreUpdate.serializer(), rawJson)
        assertTrue(update is CoreUpdate.CommandBarResults)
        val results = update as CoreUpdate.CommandBarResults

        assertEquals(6, results.suggestions.size)

        val search = results.suggestions[0]
        assertEquals(SuggestionType.Search, search.kind)
        assertEquals("s1", search.key)
        assertNotNull(search.executionPayload)
        assertEquals("{\"query\":\"maho browser\"}", search.executionPayload)

        val aiAnswer = results.suggestions[1]
        assertEquals(SuggestionType.AiAnswer, aiAnswer.kind)
        assertNull(aiAnswer.executionPayload)

        val folder = results.suggestions[2]
        assertEquals(SuggestionType.Folder, folder.kind)

        val unitConversion = results.suggestions[3]
        assertEquals(SuggestionType.UnitConversion, unitConversion.kind)
        assertNotNull(unitConversion.executionPayload)

        val archivedTab = results.suggestions[4]
        assertEquals(SuggestionType.ArchivedTab, archivedTab.kind)

        val closedTab = results.suggestions[5]
        assertEquals(SuggestionType.ClosedTab, closedTab.kind)
    }

    @Test
    fun `SuggestionViewModel executionPayload present when non-null`() {
        val suggestion = SuggestionViewModel(
            kind = SuggestionType.Search,
            key = "k1",
            title = "test query",
            executionPayload = "{\"url\":\"https://example.com\"}",
            relevanceScore = 1.0,
        )
        val encoded = json.encodeToString(SuggestionViewModel.serializer(), suggestion)
        assertTrue("must serialize executionPayload key", encoded.contains("\"executionPayload\""))
        val decoded = json.decodeFromString(SuggestionViewModel.serializer(), encoded)
        assertEquals(suggestion.executionPayload, decoded.executionPayload)
    }

    @Test
    fun `SuggestionViewModel executionPayload null when absent`() {
        val rawJson = """
            {
                "kind": "history",
                "key": "h1",
                "title": "Example",
                "relevanceScore": 0.5
            }
        """.trimIndent()
        val decoded = json.decodeFromString(SuggestionViewModel.serializer(), rawJson)
        assertNull(decoded.executionPayload)
    }

    @Test
    fun `CommandBarQuery encodes isIncognito true with is_incognito wire key`() {
        val event = ShellEvent.CommandBarQuery(text = "hello", isIncognito = true)
        val encoded = json.encodeToString(ShellEvent.serializer(), event)
        assertTrue(
            "encoded JSON must contain \"is_incognito\":true",
            encoded.contains("\"is_incognito\":true")
        )
    }

    @Test
    fun `CommandBarQuery encodes isIncognito false by default`() {
        val event = ShellEvent.CommandBarQuery(text = "hello")
        val encoded = json.encodeToString(ShellEvent.serializer(), event)
        // encodeDefaults=true means it will always be emitted; assert false value
        assertTrue(
            "encoded JSON must contain is_incognito key",
            encoded.contains("is_incognito")
        )
        assertTrue(
            "is_incognito must be false by default",
            encoded.contains("\"is_incognito\":false")
        )
    }

    @Test
    fun `CommandBarQuery isIncognito round trips`() {
        val original = ShellEvent.CommandBarQuery(text = "incognito search", isIncognito = true)
        val encoded = json.encodeToString(ShellEvent.serializer(), original)
        val decoded = json.decodeFromString(ShellEvent.serializer(), encoded)
        assertEquals(original, decoded)
    }

    @Test
    fun `CommandBarQuery isIncognito decodes from raw JSON`() {
        val rawJson = """{"kind":"command_bar_query","text":"foo","is_incognito":true}"""
        val decoded = json.decodeFromString(ShellEvent.serializer(), rawJson)
        assertTrue(decoded is ShellEvent.CommandBarQuery)
        val query = decoded as ShellEvent.CommandBarQuery
        assertEquals("foo", query.text)
        assertEquals(true, query.isIncognito)
    }

    @Test
    fun `PrivacySettings decodes all content blocking mode strings`() {
        val baseProps = """"doNotTrack":true,"blockThirdPartyCookies":true,"popupBlockerEnabled":true"""

        val nativeJson = """{$baseProps,"contentBlockingMode":"native","contentBlockerEnabled":true}"""
        val nativeDecoded = json.decodeFromString<PrivacySettings>(nativeJson)
        assertEquals(ContentBlockingMode.NATIVE, nativeDecoded.contentBlockingMode)
        assertTrue(nativeDecoded.isNativeBlockingEnabled)

        val extensionJson = """{$baseProps,"contentBlockingMode":"extension","contentBlockerEnabled":false}"""
        val extensionDecoded = json.decodeFromString<PrivacySettings>(extensionJson)
        assertEquals(ContentBlockingMode.EXTENSION, extensionDecoded.contentBlockingMode)
        assertTrue(!extensionDecoded.isNativeBlockingEnabled)

        val disabledJson = """{$baseProps,"contentBlockingMode":"disabled","contentBlockerEnabled":false}"""
        val disabledDecoded = json.decodeFromString<PrivacySettings>(disabledJson)
        assertEquals(ContentBlockingMode.DISABLED, disabledDecoded.contentBlockingMode)
        assertTrue(!disabledDecoded.isNativeBlockingEnabled)

        val legacyJson = """{$baseProps,"contentBlockerEnabled":true}"""
        val legacyDecoded = json.decodeFromString<PrivacySettings>(legacyJson)
        assertEquals(ContentBlockingMode.NATIVE, legacyDecoded.contentBlockingMode)
        assertTrue(legacyDecoded.isNativeBlockingEnabled)

        val legacyDisabledJson = """{$baseProps,"contentBlockerEnabled":false}"""
        val legacyDisabledDecoded = json.decodeFromString<PrivacySettings>(legacyDisabledJson)
        assertEquals(ContentBlockingMode.DISABLED, legacyDisabledDecoded.contentBlockingMode)
        assertTrue(!legacyDisabledDecoded.isNativeBlockingEnabled)

        val unknownJson = """{$baseProps,"contentBlockingMode":"future_mode","contentBlockerEnabled":true}"""
        val unknownDecoded = json.decodeFromString<PrivacySettings>(unknownJson)
        assertEquals(ContentBlockingMode.UNKNOWN, unknownDecoded.contentBlockingMode)
        assertTrue(!unknownDecoded.isNativeBlockingEnabled)
    }

    @Test
    fun `contradictory transitional mode is authoritative over legacy content blocker bool`() {
        val baseProps = """"doNotTrack":true,"blockThirdPartyCookies":true,"popupBlockerEnabled":true"""

        val extensionTrueJson = """{$baseProps,"contentBlockingMode":"extension","contentBlockerEnabled":true}"""
        val extensionTrue = json.decodeFromString<PrivacySettings>(extensionTrueJson)
        assertEquals(ContentBlockingMode.EXTENSION, extensionTrue.contentBlockingMode)
        assertTrue(
            "extension mode must never enable native blocking even when legacy bool is true",
            !extensionTrue.isNativeBlockingEnabled,
        )

        val disabledTrueJson = """{$baseProps,"contentBlockingMode":"disabled","contentBlockerEnabled":true}"""
        val disabledTrue = json.decodeFromString<PrivacySettings>(disabledTrueJson)
        assertEquals(ContentBlockingMode.DISABLED, disabledTrue.contentBlockingMode)
        assertTrue(
            "disabled mode must never enable native blocking even when legacy bool is true",
            !disabledTrue.isNativeBlockingEnabled,
        )

        val legacyTrueJson = """{$baseProps,"contentBlockerEnabled":true}"""
        val legacyTrue = json.decodeFromString<PrivacySettings>(legacyTrueJson)
        assertEquals(ContentBlockingMode.NATIVE, legacyTrue.contentBlockingMode)
        assertTrue(
            "legacy bool-only true must map to native-on",
            legacyTrue.isNativeBlockingEnabled,
        )

        val nativeFalseJson = """{$baseProps,"contentBlockingMode":"native","contentBlockerEnabled":false}"""
        val nativeFalse = json.decodeFromString<PrivacySettings>(nativeFalseJson)
        assertEquals(ContentBlockingMode.NATIVE, nativeFalse.contentBlockingMode)
        assertTrue(
            "explicit native mode must win over contradictory legacy false",
            nativeFalse.isNativeBlockingEnabled,
        )
    }
}
