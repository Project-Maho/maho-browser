package dev.maho.browser

import android.content.Context
import android.webkit.WebView
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.junit4.createAndroidComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.test.espresso.Espresso.onView
import androidx.test.espresso.assertion.ViewAssertions.matches
import androidx.test.espresso.matcher.ViewMatchers.isAssignableFrom
import androidx.test.espresso.matcher.ViewMatchers.isDisplayed
import org.junit.Rule
import org.junit.Test

class BrowserLaunchTest {

    @get:Rule
    val composeTestRule = createAndroidComposeRule<MainActivity>()

    @Test
    fun appLaunchesAndShowsItsStartSurface() {
        composeTestRule.onNodeWithTag("androidShellRoot").assertIsDisplayed()

        val onboardingCompleted = composeTestRule.activity
            .getSharedPreferences("maho_browser_ui_prefs", Context.MODE_PRIVATE)
            .getBoolean("onboardingCompleted", false)
        if (onboardingCompleted) {
            composeTestRule.onNodeWithTag("homeSearchView").assertIsDisplayed()
        } else {
            onView(isAssignableFrom(WebView::class.java)).check(matches(isDisplayed()))
        }
    }
}
