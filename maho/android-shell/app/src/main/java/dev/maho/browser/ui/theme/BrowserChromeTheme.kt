package dev.maho.browser.ui.theme

import androidx.compose.runtime.Immutable
import androidx.compose.ui.graphics.Color

@Immutable
data class BrowserChromeTheme(
    val tintColor: Color,
    val statusBarColor: Color,
    val useDarkStatusIcons: Boolean,
)

fun resolveBrowserChromeTheme(
    shellColors: BrowserShellColors,
    inverseSurface: Color,
    activeSpaceColor: Color?,
    isIncognito: Boolean,
    isSystemDarkTheme: Boolean,
    showSearchSheet: Boolean,
    isCleanHomeChrome: Boolean,
): BrowserChromeTheme {
    val tintColor = when {
        isIncognito -> inverseSurface
        activeSpaceColor != null -> activeSpaceColor
        else -> Color.Unspecified
    }
    val statusBarColor = when {
        showSearchSheet && isIncognito -> Color(0xFF101633)
        showSearchSheet && isSystemDarkTheme -> shellColors.windowBackground
        showSearchSheet -> Color(0xFF666668)
        isCleanHomeChrome && isIncognito -> Color(0xFF1A2352)
        isCleanHomeChrome && isSystemDarkTheme -> shellColors.homeBackgroundStart
        isCleanHomeChrome -> Color(0xFFF2F2F7)
        else -> shellColors.windowBackground
    }
    val useDarkStatusIcons = when {
        showSearchSheet && isIncognito -> false
        showSearchSheet && isSystemDarkTheme -> false
        showSearchSheet -> true
        else -> isCleanHomeChrome && !isIncognito && !isSystemDarkTheme
    }

    return BrowserChromeTheme(
        tintColor = tintColor,
        statusBarColor = statusBarColor,
        useDarkStatusIcons = useDarkStatusIcons,
    )
}
