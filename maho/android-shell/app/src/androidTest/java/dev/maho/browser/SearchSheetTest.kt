package dev.maho.browser

import androidx.compose.ui.test.assertHasClickAction
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.performScrollToNode
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.getValue
import androidx.compose.runtime.setValue
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.test.assertCountEquals
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.click
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onAllNodesWithTag
import androidx.compose.ui.test.onAllNodesWithText
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.onRoot
import androidx.compose.ui.test.performTouchInput
import dev.maho.browser.models.SuggestionType
import dev.maho.browser.models.SuggestionViewModel
import dev.maho.browser.ui.search.SearchSheet
import dev.maho.browser.ui.search.SearchSuggestionProvider
import dev.maho.browser.ui.theme.ProvideBrowserShellTheme
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test

class SearchSheetTest {
    @get:Rule
    val composeTestRule = createComposeRule()

    @Test
    fun normalEmptyStateShowsFieldVoiceAndPrivateControls() {
        setSearchSheetContent(
            provider = StaticSuggestionProvider(emptyList()),
        )

        // Root overlay: assertExists (full-screen overlay node in the tree).
        composeTestRule.onNodeWithTag("searchSheet", useUnmergedTree = true).assertExists()
        // Panel content: verify presence/visibility in the unmerged layout tree.
        composeTestRule.onNodeWithTag("searchSheetField", useUnmergedTree = true).assertIsDisplayed()
        composeTestRule.onNodeWithTag("searchSheetTextField", useUnmergedTree = true).assertIsDisplayed()
        composeTestRule.onNodeWithTag("homeSearchField", useUnmergedTree = true).assertIsDisplayed()
        composeTestRule.onNodeWithTag("searchSheetEmptyState", useUnmergedTree = true).assertIsDisplayed()
        composeTestRule.onNodeWithText("Search or enter a website", useUnmergedTree = true).assertIsDisplayed()
        composeTestRule.onNodeWithTag("searchSheetMicButton").assertIsDisplayed()
        composeTestRule.onNodeWithTag("searchSheetVoiceButton", useUnmergedTree = true).assertExists()
        composeTestRule.onNodeWithTag("searchSheetPrivateToggle").assertIsDisplayed()
    }

    @Test
    fun incognitoEmptyStateHidesVoiceAndShowsPrivateExitControl() {
        setSearchSheetContent(
            isIncognito = true,
            provider = StaticSuggestionProvider(emptyList()),
        )

        // Incognito empty-state box is inside the merged-descendant panel column.
        composeTestRule.onNodeWithTag("searchSheetIncognitoEmptyState", useUnmergedTree = true).assertIsDisplayed()
        composeTestRule.onNodeWithText("You're browsing Incognito", useUnmergedTree = true).assertIsDisplayed()
        composeTestRule.onNodeWithTag("searchSheetPrivateToggle").assertIsDisplayed()
        composeTestRule.onAllNodesWithTag("searchSheetMicButton").assertCountEquals(0)
        composeTestRule.onAllNodesWithTag("searchSheetVoiceButton").assertCountEquals(0)
    }

    @Test
    fun suggestionsRenderPrimaryHistoryCommandsOrdering() {
        val provider = StaticSuggestionProvider(
            listOf(
                suggestion(
                    kind = SuggestionType.History,
                    key = "history:https://history.example",
                    title = "Example History",
                    subtitle = "https://history.example",
                    executionPayload = "https://history.example",
                ),
                suggestion(
                    kind = SuggestionType.Action,
                    key = "action:new_space",
                    title = "New Space",
                    subtitle = "Browser",
                ),
                suggestion(
                    kind = SuggestionType.Search,
                    key = "search:ma",
                    title = "Search ma",
                    executionPayload = "https://www.google.com/search?q=ma",
                ),
            ),
        )

        setSearchSheetContent(
            initialQuery = "ma",
            provider = provider,
        )
        waitForText("Search ma")

        composeTestRule.onNodeWithTag("searchSheetSuggestions", useUnmergedTree = true).assertExists()

        // Section headers are real product surface (SuggestionsList emits
        // "History".uppercase() / "Commands".uppercase()); assert they render
        // and that the full primary -> history -> commands order holds.
        waitForText("HISTORY")
        waitForText("COMMANDS")

        val primaryTop = topOfText("Search ma")
        val historyHeaderTop = topOfText("HISTORY")
        val historyTop = topOfText("Example History")
        val commandsHeaderTop = topOfText("COMMANDS")
        val commandTop = topOfText("New Space")

        assertTrue("primary must precede HISTORY header", primaryTop < historyHeaderTop)
        assertTrue("HISTORY header must precede its row", historyHeaderTop < historyTop)
        assertTrue("history row must precede COMMANDS header", historyTop < commandsHeaderTop)
        assertTrue("COMMANDS header must precede its row", commandsHeaderTop < commandTop)

        // With only three suggestions the whole list fits, so every row and
        // header must be visible, not merely present.
        composeTestRule.onNodeWithText("HISTORY", useUnmergedTree = true).assertIsDisplayed()
        composeTestRule.onNodeWithText("COMMANDS", useUnmergedTree = true).assertIsDisplayed()
        composeTestRule.onNodeWithText("Example History", useUnmergedTree = true).assertIsDisplayed()
        composeTestRule.onNodeWithText("New Space", useUnmergedTree = true).assertIsDisplayed()
    }

    @Test
    fun browseForMePillShowsForSearchAndTypedHistoryOnly() {
        val provider = StaticSuggestionProvider(
            listOf(
                suggestion(
                    kind = SuggestionType.Search,
                    key = "search:maho",
                    title = "Search maho",
                    executionPayload = "https://www.google.com/search?q=maho",
                ),
                suggestion(
                    kind = SuggestionType.History,
                    key = "history:https://maho.dev",
                    title = "Maho",
                    subtitle = "https://maho.dev",
                    executionPayload = "https://maho.dev",
                ),
                suggestion(
                    kind = SuggestionType.Action,
                    key = "action:settings",
                    title = "Settings",
                    subtitle = "Browser",
                ),
            ),
        )

        setSearchSheetContent(
            initialQuery = "maho",
            provider = provider,
        )
        waitForText("Search maho")

        // showsBrowseForMe(): Search always qualifies, History qualifies while
        // the query is non-empty, Action never does -- so exactly two of these
        // three suggestions carry a pill.
        waitForText("Settings")
        composeTestRule.onAllNodesWithTag("searchSheetBrowseForMeButton")
            .assertCountEquals(2)

        // Each qualifying row is scrolled into view and asserted visible along
        // with its pills, rather than asserting several rows at once while some
        // may be off-screen. Both pills are clickable actions, not just present
        // nodes.
        assertRowVisible("Search maho")
        assertRowVisible("Maho")

        composeTestRule.onAllNodesWithTag("searchSheetBrowseForMeButton")[0]
            .assertHasClickAction()
        composeTestRule.onAllNodesWithTag("searchSheetBrowseForMeButton")[1]
            .assertHasClickAction()

        // The Action suggestion must never carry a pill: scrolling to it must
        // not raise the pill count above the two qualifying rows.
        assertRowVisible("Settings")
        composeTestRule.onAllNodesWithTag("searchSheetBrowseForMeButton")
            .assertCountEquals(2)
    }

    private fun assertRowVisible(rowTitle: String) {
        composeTestRule.onNodeWithTag("searchSheetSuggestions", useUnmergedTree = true)
            .performScrollToNode(hasText(rowTitle))
        composeTestRule.onNodeWithText(rowTitle, useUnmergedTree = true).assertIsDisplayed()
    }

    @Test
    fun queryResetsWhenInitialQueryChanges() {
        var queryState by androidx.compose.runtime.mutableStateOf("https://first.example.com")
        composeTestRule.setContent {
            MaterialTheme {
                ProvideBrowserShellTheme {
                    SearchSheet(
                        initialQuery = queryState,
                        isIncognito = false,
                        onDismiss = {},
                        onSubmit = {},
                        onToggleIncognito = {},
                        onSelectSuggestion = {},
                        onBrowseForMe = {},
                        suggestionProvider = StaticSuggestionProvider(emptyList()),
                        requestKeyboardOnAppear = false,
                    )
                }
            }
        }

        composeTestRule.onNodeWithText("https://first.example.com").assertIsDisplayed()
        queryState = "https://second.example.com"
        composeTestRule.onNodeWithText("https://second.example.com").assertIsDisplayed()
    }

    @Test
    fun queryClearsWhenInitialQueryBecomesEmpty() {
        var queryState by androidx.compose.runtime.mutableStateOf("https://existing.example.com")
        composeTestRule.setContent {
            MaterialTheme {
                ProvideBrowserShellTheme {
                    SearchSheet(
                        initialQuery = queryState,
                        isIncognito = false,
                        onDismiss = {},
                        onSubmit = {},
                        onToggleIncognito = {},
                        onSelectSuggestion = {},
                        onBrowseForMe = {},
                        suggestionProvider = StaticSuggestionProvider(emptyList()),
                        requestKeyboardOnAppear = false,
                    )
                }
            }
        }

        composeTestRule.onNodeWithText("https://existing.example.com").assertIsDisplayed()
        queryState = ""
        composeTestRule.onNodeWithText("Search or enter a website", useUnmergedTree = true).assertIsDisplayed()
    }

    @Test
    fun backdropDismissesWithoutSelectingPanel() {
        var dismissed = false
        setSearchSheetContent(
            provider = StaticSuggestionProvider(emptyList()),
            onDismiss = { dismissed = true },
        )

        // Simulate a gesture click at the top portion of the screen — well above the
        // bottom-sheet panel — so the touch lands on the scrim and not the panel.
        composeTestRule.onRoot().performTouchInput {
            // The panel occupies roughly the bottom half of the screen. Click near the top.
            click(Offset(center.x, top + 10f))
        }
        composeTestRule.waitForIdle()
        assertTrue(dismissed)
    }

    private fun setSearchSheetContent(
        initialQuery: String = "",
        isIncognito: Boolean = false,
        provider: SearchSuggestionProvider,
        onDismiss: () -> Unit = {},
    ) {
        composeTestRule.setContent {
            MaterialTheme {
                ProvideBrowserShellTheme {
                    SearchSheet(
                        initialQuery = initialQuery,
                        isIncognito = isIncognito,
                        onDismiss = onDismiss,
                        onSubmit = {},
                        onToggleIncognito = {},
                        onSelectSuggestion = {},
                        onBrowseForMe = {},
                        suggestionProvider = provider,
                        requestKeyboardOnAppear = false,
                    )
                }
            }
        }
    }

    private fun waitForText(text: String) {
        composeTestRule.waitUntil(timeoutMillis = 2_000) {
            composeTestRule.onAllNodesWithText(text).fetchSemanticsNodes().isNotEmpty()
        }
    }

    private fun topOfText(text: String): Float =
        composeTestRule.onNodeWithText(text, useUnmergedTree = true).fetchSemanticsNode().boundsInRoot.top

    private fun suggestion(
        kind: SuggestionType,
        key: String,
        title: String,
        subtitle: String? = null,
        executionPayload: String? = null,
    ): SuggestionViewModel = SuggestionViewModel(
        kind = kind,
        key = key,
        title = title,
        subtitle = subtitle,
        executionPayload = executionPayload,
        relevanceScore = 1.0,
        matchRanges = listOf(listOf(0, minOf(2, title.length))),
    )

    private class StaticSuggestionProvider(
        private val suggestions: List<SuggestionViewModel>,
    ) : SearchSuggestionProvider {
        override suspend fun suggestions(query: String, isIncognito: Boolean): List<SuggestionViewModel> = suggestions
    }
}
