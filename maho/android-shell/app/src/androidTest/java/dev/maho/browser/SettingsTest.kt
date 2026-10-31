package dev.maho.browser

import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollTo
import dev.maho.browser.ui.settings.SettingsScreen
import dev.maho.browser.ui.theme.ProvideBrowserShellTheme
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

class SettingsTest {

    @get:Rule
    val composeTestRule = createComposeRule()

    @Test
    fun settingsScreenShowsTitle() {
        setSettingsContent()

        composeTestRule.onNodeWithText("Settings").assertIsDisplayed()
    }

    /**
     * SettingsScreen is a single verticalScroll Column, so lower rows start
     * outside the viewport. Each category is scrolled into view and then
     * asserted visible; a bare assertExists would pass even if a row were
     * laid out with zero size or fully clipped.
     */
    @Test
    fun settingsScreenShowsCategories() {
        setSettingsContent()

        val categories = listOf(
            "Sync",
            "Search Engine",
            "General",
            "Appearance",
            "Privacy & Security",
            "Clear Browsing Data",
            "Reader Mode",
            "Autofill & Passwords",
            "Notifications",
            "Max AI",
            "Web Agent",
            "App Icon",
            "Feedback",
        )

        categories.forEach { category ->
            composeTestRule.onNodeWithText(category).performScrollTo().assertIsDisplayed()
        }
    }

    /**
     * The first-screen entry points must be visible without any scrolling --
     * this is the property that regressed into assertExists. No scroll call
     * here on purpose.
     */
    @Test
    fun settingsScreenKeepsCoreShellEntryPointsVisibleWithoutScrolling() {
        setSettingsContent()

        composeTestRule.onNodeWithText("Sync").assertIsDisplayed()
        composeTestRule.onNodeWithText("General").assertIsDisplayed()
        composeTestRule.onNodeWithText("Appearance").assertIsDisplayed()
    }

    /**
     * Entry-point wiring only: tapping a category row invokes the matching
     * navigation callback. This proves the row is hit-testable and routed; it
     * does NOT exercise sync, networking, or relay behavior.
     */
    @Test
    fun settingsRowsInvokeTheirNavigationCallbacks() {
        val navigations = mutableListOf<String>()
        composeTestRule.setContent {
            ProvideBrowserShellTheme {
                SettingsScreen(
                    onNavigateToSync = { navigations += "sync" },
                    onNavigateToGeneral = { navigations += "general" },
                    onNavigateToAppearance = { navigations += "appearance" },
                )
            }
        }

        composeTestRule.onNodeWithText("Sync").performScrollTo().performClick()
        composeTestRule.onNodeWithText("Appearance").performScrollTo().performClick()
        composeTestRule.waitForIdle()

        assertEquals(listOf("sync", "appearance"), navigations)
    }

    private fun setSettingsContent() {
        composeTestRule.setContent {
            ProvideBrowserShellTheme {
                SettingsScreen()
            }
        }
    }
}
