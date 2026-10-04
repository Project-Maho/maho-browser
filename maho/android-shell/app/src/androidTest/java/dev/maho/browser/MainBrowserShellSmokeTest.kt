package dev.maho.browser

import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.assertCountEquals
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onAllNodesWithTag
import androidx.compose.ui.test.onAllNodesWithText
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollTo
import androidx.compose.material3.MaterialTheme
import dev.maho.browser.ui.ArcBottomBar
import dev.maho.browser.ui.HomeSearchScreen
import dev.maho.browser.models.SpaceViewModel
import dev.maho.browser.models.TabRole
import dev.maho.browser.models.TabViewModel
import dev.maho.browser.models.SpaceColor
import dev.maho.browser.ui.theme.ProvideBrowserShellTheme
import org.junit.Assert.assertEquals
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
        // HomeSectionHeader renders title.uppercase(), so the semantics text is
        // uppercase even though callers pass "Continue" / "Top Sites".
        composeTestRule.onNodeWithText("CONTINUE").assertIsDisplayed()
        composeTestRule.onNodeWithText("TOP SITES").assertIsDisplayed()
        composeTestRule.onNodeWithText("Recent tab").assertIsDisplayed()
        composeTestRule.onNodeWithText("Top site").assertIsDisplayed()
        composeTestRule.onNodeWithText("https://example.com/recent").assertIsDisplayed()
        composeTestRule.onAllNodesWithTag("homeSearchBar").assertCountEquals(0)
    }

    @Test
    fun homeRecentTabCardResumesTappedTab() {
        val resumed = mutableListOf<String>()
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
                        onSelectSite = { resumed += "site:$it" },
                        onResumeTab = { resumed += "tab:$it" },
                    )
                }
            }
        }

        composeTestRule.onNodeWithText("Recent tab").performScrollTo().performClick()
        composeTestRule.onNodeWithText("Top site").performScrollTo().performClick()
        composeTestRule.waitForIdle()

        assertEquals(
            listOf("tab:tab-1", "site:https://www.top.example.com/top"),
            resumed,
        )
    }

    @Test
    fun homeSearchScreenHidesBothSectionsInIncognito() {
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

        // Incognito must not leak recent tabs or top sites; the hero logo is the
        // only surface left, so the screen falls into its empty-home layout.
        composeTestRule.onNodeWithTag("homeSearchView").assertIsDisplayed()
        composeTestRule.onNodeWithTag("homeHeroPanel").assertIsDisplayed()
        composeTestRule.onAllNodesWithTag("homeRecentTabsSection").assertCountEquals(0)
        composeTestRule.onAllNodesWithTag("homeTopSitesSection").assertCountEquals(0)
        composeTestRule.onAllNodesWithText("Recent 1").assertCountEquals(0)
        composeTestRule.onAllNodesWithText("Top 1").assertCountEquals(0)
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
        // Rendered headers are uppercased by HomeSectionHeader; assert the
        // strings the product actually emits so this cannot pass vacuously.
        composeTestRule.onAllNodesWithText("CONTINUE").assertCountEquals(0)
        composeTestRule.onAllNodesWithText("TOP SITES").assertCountEquals(0)
    }

    /**
     * Caps enforced by the shipped Android composables: HomeSearchScreen keeps
     * `recentTabs.take(3)`, and HomeTopSitesGrid lays out a single Row of
     * `sites.take(4)`. The screen-level `topSites.take(8)` is therefore capped
     * again to 4 by the grid, so entries 5..8 do not render on Android. iOS
     * (HomeSearchView.swift) shows up to 8 -- that divergence is recorded as a
     * product gap, not papered over here.
     */
    @Test
    fun homeSearchScreenAppliesRecentAndTopSiteCaps() {
        composeTestRule.setContent {
            MaterialTheme {
                ProvideBrowserShellTheme {
                    HomeSearchScreen(
                        isIncognito = false,
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

        // Recent tabs: first three render and are visible, the fourth is dropped.
        composeTestRule.onNodeWithText("Recent 1").performScrollTo().assertIsDisplayed()
        composeTestRule.onNodeWithText("Recent 3").performScrollTo().assertIsDisplayed()
        composeTestRule.onAllNodesWithText("Recent 4").assertCountEquals(0)

        // Top sites: the grid row renders exactly the first four.
        composeTestRule.onNodeWithText("Top 1").performScrollTo().assertIsDisplayed()
        composeTestRule.onNodeWithText("Top 4").performScrollTo().assertIsDisplayed()
        composeTestRule.onAllNodesWithText("Top 5").assertCountEquals(0)
        composeTestRule.onAllNodesWithText("Top 8").assertCountEquals(0)
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
