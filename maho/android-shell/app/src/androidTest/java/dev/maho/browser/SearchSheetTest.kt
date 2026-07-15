package dev.maho.browser

import androidx.compose.material3.MaterialTheme
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

        // Verify the suggestions list exists in the layout tree.
        composeTestRule.onNodeWithTag("searchSheetSuggestions", useUnmergedTree = true).assertExists()
        val primaryTop = topOfText("Search ma")
        val historyHeaderTop = topOfText("HISTORY")
        val historyTop = topOfText("Example History")
        val commandsHeaderTop = topOfText("COMMANDS")
        val commandTop = topOfText("New Space")

        assertTrue(primaryTop < historyHeaderTop)
        assertTrue(historyHeaderTop < historyTop)
        assertTrue(historyTop < commandsHeaderTop)
        assertTrue(commandsHeaderTop < commandTop)
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

        composeTestRule.onAllNodesWithTag("searchSheetBrowseForMeButton").assertCountEquals(2)
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
