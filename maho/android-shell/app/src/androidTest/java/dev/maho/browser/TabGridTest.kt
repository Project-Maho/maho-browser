package dev.maho.browser

import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithText
import dev.maho.browser.models.TabViewModel
import dev.maho.browser.ui.tab.TabGrid
import org.junit.Rule
import org.junit.Test

class TabGridTest {

    @get:Rule
    val composeTestRule = createComposeRule()

    @Test
    fun emptyTabGridShowsNoTabs() {
        composeTestRule.setContent {
            TabGrid(
                tabs = emptyList(),
                activeTabId = null,
                isIncognito = false,
                onTabSelected = {},
                onTabClosed = {},
            )
        }
        composeTestRule.onNodeWithText("No tabs yet").assertIsDisplayed()
    }

    @Test
    fun tabGridWithTabsDoesNotShowNoTabs() {
        val tabs = listOf(
            TabViewModel(
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
        )
        composeTestRule.setContent {
            TabGrid(
                tabs = tabs,
                activeTabId = "tab-1",
                isIncognito = false,
                onTabSelected = {},
                onTabClosed = {},
            )
        }
        composeTestRule.onNodeWithText("Example").assertIsDisplayed()
    }

    @Test
    fun tabGridRendersSwitcherStateForMultipleTabs() {
        val tabs = listOf(
            TabViewModel(
                id = "tab-1",
                spaceId = "space-1",
                title = "One",
                url = "https://one.example",
                isLoading = false,
                isPinned = false,
                isFavorite = false,
                isMuted = false,
                isPlayingAudio = false,
                lifecycleState = "active",
                children = emptyList(),
                createdAt = "2024-01-01T00:00:00Z",
                lastActiveAt = "2024-01-01T00:00:00Z",
            ),
            TabViewModel(
                id = "tab-2",
                spaceId = "space-1",
                title = "Two",
                url = "https://two.example",
                isLoading = false,
                isPinned = false,
                isFavorite = false,
                isMuted = false,
                isPlayingAudio = false,
                lifecycleState = "active",
                children = emptyList(),
                createdAt = "2024-01-01T00:00:00Z",
                lastActiveAt = "2024-01-01T00:00:00Z",
            ),
        )

        composeTestRule.setContent {
            TabGrid(
                tabs = tabs,
                activeTabId = "tab-1",
                isIncognito = false,
                onTabSelected = {},
                onTabClosed = {},
            )
        }

        composeTestRule.onNodeWithText("One").assertIsDisplayed()
        composeTestRule.onNodeWithText("Two").assertIsDisplayed()
    }
}
