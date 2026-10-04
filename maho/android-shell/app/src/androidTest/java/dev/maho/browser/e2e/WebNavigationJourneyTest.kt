package dev.maho.browser.e2e

import androidx.compose.material3.MaterialTheme
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import dev.maho.browser.ui.HomeSearchScreen
import dev.maho.browser.ui.theme.ProvideBrowserShellTheme
import org.junit.Rule
import org.junit.Test

class WebNavigationJourneyTest {

    @get:Rule
    val composeTestRule = createComposeRule()

    @Test
    fun testHomeSearchScreenHeroAndSectionsRenderProperly() {
        composeTestRule.setContent {
            MaterialTheme {
                ProvideBrowserShellTheme {
                    HomeSearchScreen(
                        isIncognito = false,
                        activeSpaceName = "Personal",
                        activeSpaceTabCount = 0,
                        recentTabs = emptyList(),
                        topSites = emptyList(),
                        onSelectSite = {},
                        onResumeTab = {},
                    )
                }
            }
        }

        composeTestRule.onNodeWithTag("homeSearchView").assertIsDisplayed()
        composeTestRule.onNodeWithTag("homeHeroPanel").assertIsDisplayed()
    }
}
