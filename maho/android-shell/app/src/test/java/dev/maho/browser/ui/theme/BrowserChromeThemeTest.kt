package dev.maho.browser.ui.theme

import androidx.compose.ui.graphics.Color
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class BrowserChromeThemeTest {

    private val colors = BrowserShellColors(
        windowBackground = Color.White, browsingBackground = Color.White,
        homeBackgroundStart = Color.White, homeBackgroundMid = Color.White, homeBackgroundEnd = Color.White,
        overlayBackground = Color.White, overlaySurface = Color.White, overlaySurfaceHigh = Color.White,
        cardBackground = Color.White, cardBackgroundElevated = Color.White, cardBorder = Color.White,
        fieldBackground = Color.White, fieldBorder = Color.White, bottomBarBase = Color.White,
        tabCard = Color.White, tabCardActive = Color.White, tabCardBorder = Color.White,
        accent = Color.Blue, accentMuted = Color.White, textPrimary = Color.Black,
        textSecondary = Color.Gray, textTertiary = Color.Gray, divider = Color.Gray,
        incognitoBackground = Color.Black, incognitoSurface = Color.Black, incognitoAccent = Color.White,
        success = Color.Green, warning = Color.Yellow, error = Color.Red,
    )

    private fun darkIcons(isDark: Boolean, isIncognito: Boolean = false, isCleanHomeChrome: Boolean) =
        resolveBrowserChromeTheme(
            shellColors = colors,
            inverseSurface = Color.Black,
            activeSpaceColor = null,
            isIncognito = isIncognito,
            isSystemDarkTheme = isDark,
            showSearchSheet = false,
            isCleanHomeChrome = isCleanHomeChrome,
        ).useDarkStatusIcons

    @Test
    fun lightTheme_usesDarkStatusIconsOffHomeToo() {
        // Light tab deck / browsing surfaces are light, so white icons would vanish.
        assertTrue(darkIcons(isDark = false, isCleanHomeChrome = false))
        assertTrue(darkIcons(isDark = false, isCleanHomeChrome = true))
    }

    @Test
    fun darkThemeAndIncognito_keepLightStatusIcons() {
        assertFalse(darkIcons(isDark = true, isCleanHomeChrome = false))
        assertFalse(darkIcons(isDark = false, isIncognito = true, isCleanHomeChrome = false))
    }
}
