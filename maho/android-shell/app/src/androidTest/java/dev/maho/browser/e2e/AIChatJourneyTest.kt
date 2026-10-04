package dev.maho.browser.e2e

import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithText
import dev.maho.browser.ui.settings.SettingsScreen
import dev.maho.browser.ui.theme.ProvideBrowserShellTheme
import org.junit.Rule
import org.junit.Test

class AIChatJourneyTest {

    @get:Rule
    val composeTestRule = createComposeRule()

    @Test
    fun testAIEntrypointsAndConfigurationScreen() {
        composeTestRule.setContent {
            ProvideBrowserShellTheme {
                SettingsScreen()
            }
        }

        composeTestRule.onNodeWithText("Settings").assertIsDisplayed()
        composeTestRule.onNodeWithText("Max AI").assertExists()
        composeTestRule.onNodeWithText("Web Agent").assertExists()
    }
}
