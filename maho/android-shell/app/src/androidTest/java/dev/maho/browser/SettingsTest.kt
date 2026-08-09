package dev.maho.browser

import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithText
import dev.maho.browser.ui.settings.SettingsScreen
import org.junit.Rule
import org.junit.Test

class SettingsTest {

    @get:Rule
    val composeTestRule = createComposeRule()

    @Test
    fun settingsScreenShowsTitle() {
        composeTestRule.setContent {
            SettingsScreen()
        }
        composeTestRule.onNodeWithText("Settings").assertIsDisplayed()
    }

    @Test
    fun settingsScreenShowsCategories() {
        composeTestRule.setContent {
            SettingsScreen()
        }
        composeTestRule.onNodeWithText("General").assertIsDisplayed()
        composeTestRule.onNodeWithText("Appearance").assertIsDisplayed()
        composeTestRule.onNodeWithText("Privacy & Security").assertIsDisplayed()
        composeTestRule.onNodeWithText("Reader Mode").assertIsDisplayed()
        composeTestRule.onNodeWithText("Autofill & Passwords").assertIsDisplayed()
        composeTestRule.onNodeWithText("Notifications").assertIsDisplayed()
        composeTestRule.onNodeWithText("Max AI").assertIsDisplayed()
        composeTestRule.onNodeWithText("API Keys").assertIsDisplayed()
        composeTestRule.onNodeWithText("Advanced").assertIsDisplayed()
        composeTestRule.onNodeWithText("Reset All Settings").assertIsDisplayed()
    }

    @Test
    fun settingsScreenKeepsCoreShellEntryPointsVisible() {
        composeTestRule.setContent {
            SettingsScreen()
        }

        composeTestRule.onNodeWithText("General").assertIsDisplayed()
        composeTestRule.onNodeWithText("Bookmarks").assertIsDisplayed()
        composeTestRule.onNodeWithText("History").assertIsDisplayed()
    }
}
