package dev.maho.browser.ui.theme

import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.Immutable
import androidx.compose.runtime.ReadOnlyComposable
import androidx.compose.runtime.remember
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.compositeOver
import androidx.compose.ui.res.colorResource
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import dev.maho.browser.R

@Immutable
data class BrowserShellColors(
    val windowBackground: Color,
    val browsingBackground: Color,
    val homeBackgroundStart: Color,
    val homeBackgroundMid: Color,
    val homeBackgroundEnd: Color,
    val overlayBackground: Color,
    val overlaySurface: Color,
    val overlaySurfaceHigh: Color,
    val bottomBarBase: Color,
    val tabCard: Color,
    val tabCardActive: Color,
    val tabCardBorder: Color,
    val accent: Color,
    val accentMuted: Color,
    val textPrimary: Color,
    val textSecondary: Color,
    val divider: Color,
    val incognitoBackground: Color,
    val incognitoSurface: Color,
    val incognitoAccent: Color,
)

@Immutable
data class BrowserShellMetrics(
    val panelCorner: Dp,
    val cardCorner: Dp,
    val compactCorner: Dp,
    val barCorner: Dp,
    val heroSearchCorner: Dp,
    val liftedShadow: Dp,
    val floatingShadow: Dp,
    val sectionSpacing: Dp,
    val compactSpacing: Dp,
    val suggestionPreviewHeight: Dp,
)

private val LocalBrowserShellColors = staticCompositionLocalOf<BrowserShellColors> {
    error("BrowserShellColors not provided")
}

private val LocalBrowserShellMetrics = staticCompositionLocalOf<BrowserShellMetrics> {
    error("BrowserShellMetrics not provided")
}

object BrowserShellTheme {
    val colors: BrowserShellColors
        @Composable
        @ReadOnlyComposable
        get() = LocalBrowserShellColors.current

    val metrics: BrowserShellMetrics
        @Composable
        @ReadOnlyComposable
        get() = LocalBrowserShellMetrics.current
}

@Composable
fun ProvideBrowserShellTheme(content: @Composable () -> Unit) {
    val windowBackground = colorResource(R.color.shell_window_background)
    val browsingBackground = colorResource(R.color.shell_browsing_background)
    val homeBackgroundStart = colorResource(R.color.shell_home_background_start)
    val homeBackgroundMid = colorResource(R.color.shell_home_background_mid)
    val homeBackgroundEnd = colorResource(R.color.shell_home_background_end)
    val overlayBackground = colorResource(R.color.shell_overlay_background)
    val overlaySurface = colorResource(R.color.shell_overlay_surface)
    val overlaySurfaceHigh = colorResource(R.color.shell_overlay_surface_high)
    val bottomBarBase = colorResource(R.color.shell_bottom_bar)
    val tabCard = colorResource(R.color.shell_tab_card)
    val tabCardActive = colorResource(R.color.shell_tab_card_active)
    val tabCardBorder = colorResource(R.color.shell_tab_card_border)
    val accent = colorResource(R.color.shell_accent)
    val accentMuted = colorResource(R.color.shell_accent_muted)
    val textPrimary = colorResource(R.color.shell_text_primary)
    val textSecondary = colorResource(R.color.shell_text_secondary)
    val divider = colorResource(R.color.shell_divider)
    val incognitoBackground = colorResource(R.color.shell_incognito_background)
    val incognitoSurface = colorResource(R.color.shell_incognito_surface)
    val incognitoAccent = colorResource(R.color.shell_incognito_accent)

    val shellColors = remember(
        windowBackground,
        browsingBackground,
        homeBackgroundStart,
        homeBackgroundMid,
        homeBackgroundEnd,
        overlayBackground,
        overlaySurface,
        overlaySurfaceHigh,
        bottomBarBase,
        tabCard,
        tabCardActive,
        tabCardBorder,
        accent,
        accentMuted,
        textPrimary,
        textSecondary,
        divider,
        incognitoBackground,
        incognitoSurface,
        incognitoAccent,
    ) {
        BrowserShellColors(
            windowBackground = windowBackground,
            browsingBackground = browsingBackground,
            homeBackgroundStart = homeBackgroundStart,
            homeBackgroundMid = homeBackgroundMid,
            homeBackgroundEnd = homeBackgroundEnd,
            overlayBackground = overlayBackground,
            overlaySurface = overlaySurface,
            overlaySurfaceHigh = overlaySurfaceHigh,
            bottomBarBase = bottomBarBase,
            tabCard = tabCard,
            tabCardActive = tabCardActive,
            tabCardBorder = tabCardBorder,
            accent = accent,
            accentMuted = accentMuted,
            textPrimary = textPrimary,
            textSecondary = textSecondary,
            divider = divider,
            incognitoBackground = incognitoBackground,
            incognitoSurface = incognitoSurface,
            incognitoAccent = incognitoAccent,
        )
    }

    val shellMetrics = remember {
        BrowserShellMetrics(
            panelCorner = 22.dp,
            cardCorner = 20.dp,
            compactCorner = 16.dp,
            barCorner = 24.dp,
            heroSearchCorner = 30.dp,
            liftedShadow = 4.dp,
            floatingShadow = 8.dp,
            sectionSpacing = 20.dp,
            compactSpacing = 12.dp,
            suggestionPreviewHeight = 120.dp,
        )
    }

    CompositionLocalProvider(
        LocalBrowserShellColors provides shellColors,
        LocalBrowserShellMetrics provides shellMetrics,
    ) {
        content()
    }
}

fun BrowserShellColors.homeBackgroundBrush(isIncognito: Boolean): Brush {
    val gradientStops = if (isIncognito) {
        listOf(incognitoBackground, incognitoSurface)
    } else {
        listOf(homeBackgroundStart, homeBackgroundEnd)
    }
    return Brush.verticalGradient(colors = gradientStops)
}

fun BrowserShellColors.bottomBarContainerColor(
    tintColor: Color,
    isIncognito: Boolean,
): Color {
    return when {
        isIncognito -> incognitoSurface.copy(alpha = 0.92f)
        tintColor != Color.Unspecified -> tintColor.copy(alpha = 0.12f).compositeOver(bottomBarBase)
        else -> bottomBarBase
    }
}
