package dev.maho.browser.e2e

import androidx.compose.material3.MaterialTheme
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import dev.maho.browser.models.SuggestionViewModel
import dev.maho.browser.ui.search.SearchSheet
import dev.maho.browser.ui.search.SearchSuggestionProvider
import dev.maho.browser.ui.theme.ProvideBrowserShellTheme
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

class AddressOmniboxJourneyTest {

    @get:Rule
    val composeTestRule = createComposeRule()

    private class EmptySuggestionProvider : SearchSuggestionProvider {
        override suspend fun suggestions(query: String, isIncognito: Boolean): List<SuggestionViewModel> = emptyList()
    }

    @Test
    fun testAddressOmniboxPrefillsActiveTabAndRendersSearchSheet() {
        val activeUrl = "https://example.com"
        var submittedQuery = ""

        composeTestRule.setContent {
            MaterialTheme {
                ProvideBrowserShellTheme {
                    SearchSheet(
                        initialQuery = activeUrl,
                        isIncognito = false,
                        onDismiss = {},
                        onSubmit = { submittedQuery = it },
                        onToggleIncognito = {},
                        onSelectSuggestion = {},
                        onBrowseForMe = {},
                        suggestionProvider = EmptySuggestionProvider(),
                        requestKeyboardOnAppear = false,
                    )
                }
            }
        }

        composeTestRule.onNodeWithTag("searchSheetField", useUnmergedTree = true).assertIsDisplayed()
        composeTestRule.onNodeWithTag("searchSheetTextField", useUnmergedTree = true).assertIsDisplayed()
        composeTestRule.onNodeWithText(activeUrl).assertIsDisplayed()
    }

    @Test
    fun testAddressOmniboxEmptyStateRendersPlaceholder() {
        composeTestRule.setContent {
            MaterialTheme {
                ProvideBrowserShellTheme {
                    SearchSheet(
                        initialQuery = "",
                        isIncognito = false,
                        onDismiss = {},
                        onSubmit = {},
                        onToggleIncognito = {},
                        onSelectSuggestion = {},
                        onBrowseForMe = {},
                        suggestionProvider = EmptySuggestionProvider(),
                        requestKeyboardOnAppear = false,
                    )
                }
            }
        }

        composeTestRule.onNodeWithText("Search or enter a website", useUnmergedTree = true).assertIsDisplayed()
    }
}
