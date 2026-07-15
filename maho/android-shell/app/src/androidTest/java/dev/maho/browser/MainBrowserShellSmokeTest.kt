package dev.maho.browser

import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.assertCountEquals
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onAllNodesWithTag
import androidx.compose.ui.test.onAllNodesWithText
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.material3.MaterialTheme
import dev.maho.browser.ui.ArcBottomBar
import dev.maho.browser.ui.HomeSearchScreen
import dev.maho.browser.models.SpaceViewModel
import dev.maho.browser.models.TabRole
import dev.maho.browser.models.TabViewModel
import dev.maho.browser.models.SpaceColor
import dev.maho.browser.ui.theme.ProvideBrowserShellTheme
import org.junit.Rule
import org.junit.Test

class MainBrowserShellSmokeTest {

    @get:Rule
    val composeTestRule = createComposeRule()

    @Test
    fun homeSearchScreenShowsReturningHomeSectionsWithoutInlineSearch() {
        composeTestRule.setContent {
            MaterialTheme {
                ProvideBrowserShellTheme {
                    HomeSearchScreen(
                        isIncognito = false,
                        activeSpaceName = "Space",
                        activeSpaceTabCount = 2,
                        recentTabs = listOf(
                            testTab(
                                id = "tab-1",
                                title = "Recent tab",
                                url = "https://example.com/recent",
                            ),
                        ),
                        topSites = listOf(
                            testTab(
                                id = "tab-2",
                                title = "Top site",
                                url = "https://www.top.example.com/top",
                            ),
                        ),
                        onSelectSite = {},
                        onResumeTab = {},
                    )
                }
            }
        }

        composeTestRule.onNodeWithTag("homeSearchView").assertIsDisplayed()
        composeTestRule.onNodeWithTag("homeHeroPanel").assertIsDisplayed()
        composeTestRule.onNodeWithTag("homeRecentTabsSection").assertIsDisplayed()
        composeTestRule.onNodeWithTag("homeTopSitesSection").assertIsDisplayed()
        composeTestRule.onNodeWithText("Continue").assertIsDisplayed()
        composeTestRule.onNodeWithText("Pick up where you left off").assertIsDisplayed()
        composeTestRule.onNodeWithText("Top sites").assertIsDisplayed()
        composeTestRule.onNodeWithText("Fast ways back in").assertIsDisplayed()
        composeTestRule.onNodeWithText("Recent tab").assertIsDisplayed()
        composeTestRule.onNodeWithText("Top site").assertIsDisplayed()
        composeTestRule.onNodeWithText("example.com").assertIsDisplayed()
        composeTestRule.onAllNodesWithTag("homeSearchBar").assertCountEquals(0)
    }

    @Test
    fun homeSearchScreenEmptyHomeShowsOnlyLogoSurface() {
        composeTestRule.setContent {
            MaterialTheme {
                ProvideBrowserShellTheme {
                    HomeSearchScreen(
                        isIncognito = false,
                        activeSpaceName = "Space",
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
        composeTestRule.onAllNodesWithTag("homeRecentTabsSection").assertCountEquals(0)
        composeTestRule.onAllNodesWithTag("homeTopSitesSection").assertCountEquals(0)
        composeTestRule.onAllNodesWithTag("homeSearchBar").assertCountEquals(0)
        composeTestRule.onAllNodesWithText("Continue").assertCountEquals(0)
        composeTestRule.onAllNodesWithText("Top sites").assertCountEquals(0)
    }

    @Test
    fun homeSearchScreenUsesIostyleMaximums() {
        composeTestRule.setContent {
            MaterialTheme {
                ProvideBrowserShellTheme {
                    HomeSearchScreen(
                        isIncognito = true,
                        activeSpaceName = "Private",
                        activeSpaceTabCount = 12,
                        recentTabs = (1..4).map { index ->
                            testTab(
                                id = "recent-$index",
                                title = "Recent $index",
                                url = "https://recent$index.example.com",
                            )
                        },
                        topSites = (1..9).map { index ->
                            testTab(
                                id = "top-$index",
                                title = "Top $index",
                                url = "https://top$index.example.com",
                            )
                        },
                        onSelectSite = {},
                        onResumeTab = {},
                    )
                }
            }
        }

        composeTestRule.onNodeWithTag("homeRecentTabsSection").assertIsDisplayed()
        composeTestRule.onNodeWithTag("homeTopSitesSection").assertIsDisplayed()
        composeTestRule.onAllNodesWithText("Recent 3").assertCountEquals(1)
        composeTestRule.onAllNodesWithText("Recent 4").assertCountEquals(0)
        composeTestRule.onAllNodesWithText("Top 8").assertCountEquals(1)
        composeTestRule.onAllNodesWithText("Top 9").assertCountEquals(0)
        composeTestRule.onAllNodesWithTag("homeSearchBar").assertCountEquals(0)
    }

    @Test
    fun arcBottomBarShowsChromeControls() {
        composeTestRule.setContent {
            MaterialTheme {
                ProvideBrowserShellTheme {
                    ArcBottomBar(
                        tabCount = 2,
                        spaces = listOf(
                            SpaceViewModel(
                                id = "space-1",
                                name = "Space",
                                color = SpaceColor(hue = 0.5, saturation = 0.7, brightness = 0.8),
                                tabCount = 2,
                                isActive = true,
                            )
                        ),
                        isIncognito = false,
                        isHomeMode = false,
                        homeQuery = "",
                        isReaderMode = false,
                        isDesktopMode = false,
                        currentTitle = "Example",
                        currentUrl = "https://example.com",
                        isLoading = false,
                        zoomLevel = 1.0f,
                        tintColor = androidx.compose.ui.graphics.Color.Unspecified,
                        onHomeQueryChange = {},
                        onHomeSubmit = {},
                        onTabsClick = {},
                        onSelectSpace = {},
                        onSearchClick = {},
                        onGoBack = {},
                        onGoForward = {},
                        onPreviousTab = {},
                        onNextTab = {},
                        onToggleIncognito = {},
                        onFindInPage = {},
                        onArchive = {},
                        onOpenSettings = {},
                        onShare = {},
                        onReload = {},
                        onToggleReaderMode = {},
                        onZoomIn = {},
                        onZoomOut = {},
                        onToggleDesktopMode = {},
                    )
                }
            }
        }

        composeTestRule.onNodeWithTag("arcBottomBarContainer").assertIsDisplayed()
        composeTestRule.onNodeWithTag("arcBottomBarTabsButton").assertIsDisplayed()
        composeTestRule.onNodeWithTag("arcBottomBarBackButton").assertIsDisplayed()
        composeTestRule.onNodeWithTag("arcBottomBarForwardButton").assertIsDisplayed()
        composeTestRule.onNodeWithTag("arcBottomBarPageButton").assertIsDisplayed()
        composeTestRule.onNodeWithTag("arcBottomBarMoreButton").assertIsDisplayed()
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
