package dev.maho.browser.e2e

import androidx.compose.material3.MaterialTheme
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import dev.maho.browser.models.TabRole
import dev.maho.browser.models.TabViewModel
import dev.maho.browser.ui.tab.TabGrid
import dev.maho.browser.ui.theme.ProvideBrowserShellTheme
import org.junit.Rule
import org.junit.Test

class TabDeckIncognitoJourneyTest {

    @get:Rule
    val composeTestRule = createComposeRule()

    @Test
    fun testTabDeckDisplaysTabsAndSupportsIncognitoDeck() {
        val normalTabs = listOf(
            TabViewModel(
                id = "tab-normal-1",
                spaceId = "space-1",
                title = "Maho Browser",
                url = "https://mahobrowser.com",
                isLoading = false,
                isMuted = false,
                isPlayingAudio = false,
                lifecycleState = "active",
                children = emptyList(),
                createdAt = "2024-01-01T00:00:00Z",
                lastActiveAt = "2024-01-01T00:00:00Z",
                role = TabRole(type = "normal"),
            ),
            TabViewModel(
                id = "tab-normal-2",
                spaceId = "space-1",
                title = "Documentation",
                url = "https://docs.mahobrowser.com",
                isLoading = false,
                isMuted = false,
                isPlayingAudio = false,
                lifecycleState = "active",
                children = emptyList(),
                createdAt = "2024-01-01T00:00:00Z",
                lastActiveAt = "2024-01-01T00:00:00Z",
                role = TabRole(type = "normal"),
            ),
        )

        composeTestRule.setContent {
            MaterialTheme {
                ProvideBrowserShellTheme {
                    TabGrid(
                        tabs = normalTabs,
                        spaceId = null,
                        activeTabId = "tab-normal-1",
                        isIncognito = false,
                        onTabSelected = {},
                        onTabArchived = {},
                        onTabClosed = {},
                    )
                }
            }
        }

        composeTestRule.onNodeWithTag("tabGrid").assertIsDisplayed()
        composeTestRule.onNodeWithText("Tab deck").assertIsDisplayed()
        composeTestRule.onNodeWithText("mahobrowser.com").assertIsDisplayed()
        composeTestRule.onNodeWithText("docs.mahobrowser.com").assertIsDisplayed()
    }

    @Test
    fun testTabDeckEmptyStateRendersCleanly() {
        composeTestRule.setContent {
            MaterialTheme {
                ProvideBrowserShellTheme {
                    TabGrid(
                        tabs = emptyList(),
                        spaceId = null,
                        activeTabId = null,
                        isIncognito = true,
                        onTabSelected = {},
                        onTabArchived = {},
                        onTabClosed = {},
                    )
                }
            }
        }

        composeTestRule.onNodeWithText("No private tabs").assertIsDisplayed()
    }
}
