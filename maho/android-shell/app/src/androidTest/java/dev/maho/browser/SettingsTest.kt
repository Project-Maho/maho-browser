package dev.maho.browser

import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithText
import dev.maho.browser.ui.settings.SettingsScreen
import dev.maho.browser.ui.theme.ProvideBrowserShellTheme
import org.junit.Rule
import org.junit.Test

class SettingsTest {

    @get:Rule
    val composeTestRule = createComposeRule()

    @Test
    fun settingsScreenShowsTitle() {
        composeTestRule.setContent {
            ProvideBrowserShellTheme {
                SettingsScreen()
            }
        }
        composeTestRule.onNodeWithText("Settings").assertIsDisplayed()
    }

    @Test
    fun settingsScreenShowsCategories() {
        composeTestRule.setContent {
            ProvideBrowserShellTheme {
                SettingsScreen()
            }
        }
        composeTestRule.onNodeWithText("Sync").assertExists()
        composeTestRule.onNodeWithText("Search Engine").assertExists()
        composeTestRule.onNodeWithText("General").assertExists()
        composeTestRule.onNodeWithText("Appearance").assertExists()
        composeTestRule.onNodeWithText("Privacy & Security").assertExists()
        composeTestRule.onNodeWithText("Reader Mode").assertExists()
        composeTestRule.onNodeWithText("Autofill & Passwords").assertExists()
        composeTestRule.onNodeWithText("Notifications").assertExists()
        composeTestRule.onNodeWithText("Max AI").assertExists()
        composeTestRule.onNodeWithText("Web Agent").assertExists()
        composeTestRule.onNodeWithText("App Icon").assertExists()
        composeTestRule.onNodeWithText("Feedback").assertExists()
    }

    @Test
    fun settingsScreenKeepsCoreShellEntryPointsVisible() {
        composeTestRule.setContent {
            ProvideBrowserShellTheme {
                SettingsScreen()
            }
        }

        composeTestRule.onNodeWithText("Sync").assertIsDisplayed()
        composeTestRule.onNodeWithText("General").assertIsDisplayed()
        composeTestRule.onNodeWithText("Appearance").assertIsDisplayed()
    }
}
