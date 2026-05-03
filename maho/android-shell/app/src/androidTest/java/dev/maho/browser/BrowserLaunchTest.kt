package dev.maho.browser

import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.junit4.createAndroidComposeRule
import androidx.compose.ui.test.onNodeWithText
import org.junit.Rule
import org.junit.Test

class BrowserLaunchTest {

    @get:Rule
    val composeTestRule = createAndroidComposeRule<MainActivity>()

    @Test
    fun appLaunchesAndShowsTitle() {
        composeTestRule.onNodeWithText("Maho Browser").assertIsDisplayed()
    }
}
