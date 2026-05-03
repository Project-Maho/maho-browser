package dev.maho.browser

import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import dev.maho.browser.ui.ArcBottomBar
import dev.maho.browser.ui.HomeSearchScreen
import dev.maho.browser.models.SpaceViewModel
import dev.maho.browser.models.TabViewModel
import dev.maho.browser.models.SpaceColor
import org.junit.Rule
import org.junit.Test

class MainBrowserShellSmokeTest {

    @get:Rule
    val composeTestRule = createComposeRule()

    @Test
    fun homeSearchScreenShowsCoreHomeShellSurfaces() {
        composeTestRule.setContent {
            HomeSearchScreen(
                isIncognito = false,
                activeSpaceName = "Space",
                activeSpaceTabCount = 2,
                recentTabs = listOf(
                    TabViewModel(
                        id = "tab-1",
                        spaceId = "space-1",
                        title = "Recent tab",
                        url = "https://example.com/recent",
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
                ),
                topSites = listOf(
                    TabViewModel(
                        id = "tab-2",
                        spaceId = "space-1",
                        title = "Top site",
                        url = "https://example.com/top",
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
                ),
                onSearch = {},
                onSelectSite = {},
                onResumeTab = {},
            )
        }

        composeTestRule.onNodeWithTag("homeSearchView").assertIsDisplayed()
        composeTestRule.onNodeWithTag("homeHeroPanel").assertIsDisplayed()
        composeTestRule.onNodeWithTag("homeSearchBar").assertIsDisplayed()
        composeTestRule.onNodeWithTag("homeRecentTabsSection").assertIsDisplayed()
        composeTestRule.onNodeWithTag("homeTopSitesSection").assertIsDisplayed()
    }

    @Test
    fun arcBottomBarShowsChromeControls() {
        composeTestRule.setContent {
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
                isReaderMode = false,
                isDesktopMode = false,
                currentTitle = "Example",
                currentUrl = "https://example.com",
                isLoading = false,
                zoomLevel = 1.0f,
                tintColor = androidx.compose.ui.graphics.Color.Unspecified,
                onTabsClick = {},
                onSelectSpace = {},
                onSearchClick = {},
                onGoBack = {},
                onGoForward = {},
                onPreviousTab = {},
                onNextTab = {},
                onToggleIncognito = {},
                onFindInPage = {},
                onOpenSettings = {},
                onShare = {},
                onReload = {},
                onToggleReaderMode = {},
                onZoomIn = {},
                onZoomOut = {},
                onToggleDesktopMode = {},
            )
        }

        composeTestRule.onNodeWithTag("arcBottomBarContainer").assertIsDisplayed()
        composeTestRule.onNodeWithTag("arcBottomBarTabsButton").assertIsDisplayed()
        composeTestRule.onNodeWithTag("arcBottomBarBackButton").assertIsDisplayed()
        composeTestRule.onNodeWithTag("arcBottomBarForwardButton").assertIsDisplayed()
        composeTestRule.onNodeWithTag("arcBottomBarPageButton").assertIsDisplayed()
        composeTestRule.onNodeWithTag("arcBottomBarMoreButton").assertIsDisplayed()
    }
}
