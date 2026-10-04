package dev.maho.browser

import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithText
import dev.maho.browser.models.TabRole
import dev.maho.browser.models.TabViewModel
import dev.maho.browser.ui.tab.TabGrid
import dev.maho.browser.ui.theme.ProvideBrowserShellTheme
import org.junit.Rule
import org.junit.Test

class TabGridTest {

    @get:Rule
    val composeTestRule = createComposeRule()

    @Test
    fun emptyTabGridShowsNoTabs() {
        composeTestRule.setContent {
            TestTheme {
                TabGrid(
                    tabs = emptyList(),
                    spaceId = null,
                    activeTabId = null,
                    isIncognito = false,
                    onTabSelected = {},
                    onTabArchived = {},
                    onTabClosed = {},
                )
            }
        }
        composeTestRule.onNodeWithText("No tabs yet").assertIsDisplayed()
    }

    @Test
    fun tabGridWithTabsDoesNotShowNoTabs() {
        val tabs = listOf(
            testTab(
                id = "tab-1",
                title = "Example",
                url = "https://example.com",
            )
        )
        composeTestRule.setContent {
            TestTheme {
                TabGrid(
                    tabs = tabs,
                    spaceId = null,
                    activeTabId = "tab-1",
                    isIncognito = false,
                    onTabSelected = {},
                    onTabArchived = {},
                    onTabClosed = {},
                )
            }
        }
        composeTestRule.onNodeWithText("example.com").assertIsDisplayed()
    }

    @Test
    fun tabGridRendersSwitcherStateForMultipleTabs() {
        val tabs = listOf(
            testTab(
                id = "tab-1",
                title = "One",
                url = "https://one.example",
            ),
            testTab(
                id = "tab-2",
                title = "Two",
                url = "https://two.example",
            ),
        )

        composeTestRule.setContent {
            TestTheme {
                TabGrid(
                    tabs = tabs,
                    spaceId = null,
                    activeTabId = "tab-1",
                    isIncognito = false,
                    onTabSelected = {},
                    onTabArchived = {},
                    onTabClosed = {},
                )
            }
        }

        composeTestRule.onNodeWithText("one.example").assertIsDisplayed()
        composeTestRule.onNodeWithText("two.example").assertIsDisplayed()
    }

    @Composable
    private fun TestTheme(content: @Composable () -> Unit) {
        MaterialTheme {
            ProvideBrowserShellTheme(content = content)
        }
    }

    private fun testTab(
        id: String,
        title: String,
        url: String,
    ): TabViewModel {
        return TabViewModel(
            id = id,
            spaceId = "space-1",
            title = title,
            url = url,
            isLoading = false,
            isMuted = false,
            isPlayingAudio = false,
            lifecycleState = "active",
            children = emptyList(),
            createdAt = "2024-01-01T00:00:00Z",
            lastActiveAt = "2024-01-01T00:00:00Z",
            role = TabRole(type = "normal"),
        )
    }
}
