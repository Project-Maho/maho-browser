package dev.maho.browser.ui.theme

import androidx.compose.material3.ColorScheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
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
    val cardBackground: Color,
    val cardBackgroundElevated: Color,
    val cardBorder: Color,
    val fieldBackground: Color,
    val fieldBorder: Color,
    val bottomBarBase: Color,
    val tabCard: Color,
    val tabCardActive: Color,
    val tabCardBorder: Color,
    val accent: Color,
    val accentMuted: Color,
    val textPrimary: Color,
    val textSecondary: Color,
    val textTertiary: Color,
    val divider: Color,
    val incognitoBackground: Color,
    val incognitoSurface: Color,
    val incognitoAccent: Color,
    val success: Color,
    val warning: Color,
    val error: Color,
)

@Immutable
data class BrowserShellMetrics(
    val panelCorner: Dp,
    val cardCorner: Dp,
    val groupedCorner: Dp,
    val fieldCorner: Dp,
    val compactCorner: Dp,
    val barCorner: Dp,
    val heroSearchCorner: Dp,
    val liftedShadow: Dp,
    val floatingShadow: Dp,
    val sectionSpacing: Dp,
    val itemSpacing: Dp,
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
fun brandColorScheme(darkTheme: Boolean = BrowserThemeController.isDarkTheme()): ColorScheme {
    val accent = colorResource(R.color.shell_accent)
    return if (darkTheme) {
        val windowBackground = colorResource(R.color.shell_window_background)
        val overlaySurface = colorResource(R.color.shell_overlay_surface)
        val overlaySurfaceHigh = colorResource(R.color.shell_overlay_surface_high)
        val accentMuted = colorResource(R.color.shell_accent_muted)
        val textPrimary = colorResource(R.color.shell_text_primary)
        val textSecondary = colorResource(R.color.shell_text_secondary)
        val divider = colorResource(R.color.shell_divider)
        darkColorScheme(
            primary = accent,
            onPrimary = windowBackground,
            primaryContainer = accentMuted,
            onPrimaryContainer = textPrimary,
            secondary = accent,
            onSecondary = windowBackground,
            secondaryContainer = accentMuted,
            onSecondaryContainer = textPrimary,
            tertiary = accent,
            onTertiary = windowBackground,
            tertiaryContainer = accentMuted,
            onTertiaryContainer = textPrimary,
            background = windowBackground,
            onBackground = textPrimary,
            surface = overlaySurface,
            onSurface = textPrimary,
            surfaceVariant = overlaySurfaceHigh,
            onSurfaceVariant = textSecondary,
            outline = divider,
            outlineVariant = divider,
        )
    } else {
        val onLight = Color(0xFF0D1116)
        val lightBackground = Color(0xFFF2F2F7)
        val accentContainer = accent.copy(alpha = 0.24f)
        lightColorScheme(
            primary = accent,
            onPrimary = onLight,
            primaryContainer = accentContainer,
            onPrimaryContainer = onLight,
            secondary = accent,
            onSecondary = onLight,
            secondaryContainer = accentContainer,
            onSecondaryContainer = onLight,
            tertiary = accent,
            onTertiary = onLight,
            tertiaryContainer = accentContainer,
            onTertiaryContainer = onLight,
            background = lightBackground,
            onBackground = Color.Black,
            surface = Color.White,
            onSurface = Color.Black,
            surfaceVariant = lightBackground,
            onSurfaceVariant = Color.Black.copy(alpha = 0.6f),
            outline = Color.Black.copy(alpha = 0.12f),
            outlineVariant = Color.Black.copy(alpha = 0.08f),
        )
    }
}

@Composable
fun ProvideBrowserShellTheme(
    darkTheme: Boolean = BrowserThemeController.isDarkTheme(),
    content: @Composable () -> Unit,
) {
    val isDark = darkTheme
    val windowBackground = colorResource(R.color.shell_window_background)
    val browsingBackground = colorResource(R.color.shell_browsing_background)
    val homeBackgroundStart = colorResource(R.color.shell_home_background_start)
    val homeBackgroundMid = colorResource(R.color.shell_home_background_mid)
    val homeBackgroundEnd = colorResource(R.color.shell_home_background_end)
    val overlayBackground = if (isDark) Color(0xFF000000) else Color(0xFFF2F2F7)
    val overlaySurface = if (isDark) Color(0xFF1C1C1E) else Color(0xFFFFFFFF)
    val overlaySurfaceHigh = if (isDark) Color(0xFF2C2C2E) else Color(0xFFE5E5EA)
    val cardBackground = if (isDark) Color(0xFF1C1C1E) else Color(0xFFFFFFFF)
    val cardBackgroundElevated = if (isDark) Color(0xFF2C2C2E) else Color(0xFFF2F2F7)
    val cardBorder = if (isDark) Color(0xFF38383A).copy(alpha = 0.6f) else Color(0xFFD1D1D6).copy(alpha = 0.6f)
    val fieldBackground = if (isDark) Color(0xFF2C2C2E) else Color(0xFFE5E5EA)
    val fieldBorder = if (isDark) Color.White.copy(alpha = 0.08f) else Color.Black.copy(alpha = 0.06f)
    val bottomBarBase = colorResource(R.color.shell_bottom_bar)
    val tabCard = colorResource(R.color.shell_tab_card)
    val tabCardActive = colorResource(R.color.shell_tab_card_active)
    val tabCardBorder = colorResource(R.color.shell_tab_card_border)
    val accent = colorResource(R.color.shell_accent)
    val accentMuted = colorResource(R.color.shell_accent_muted)
    val textPrimary = if (isDark) Color(0xFFFFFFFF) else Color(0xFF000000)
    val textSecondary = if (isDark) Color(0xFF8E8E93) else Color(0xFF8E8E93)
    val textTertiary = if (isDark) Color(0xFF48484A) else Color(0xFFC7C7CC)
    val divider = if (isDark) Color(0xFF38383A).copy(alpha = 0.65f) else Color(0xFFC6C6C8).copy(alpha = 0.65f)
    val incognitoBackground = colorResource(R.color.shell_incognito_background)
    val incognitoSurface = colorResource(R.color.shell_incognito_surface)
    val incognitoAccent = colorResource(R.color.shell_incognito_accent)
    val statusSuccess = colorResource(R.color.shell_status_success)
    val statusWarning = colorResource(R.color.shell_status_warning)
    val statusError = colorResource(R.color.shell_status_error)

    val shellColors = remember(
        windowBackground,
        browsingBackground,
        homeBackgroundStart,
        homeBackgroundMid,
        homeBackgroundEnd,
        overlayBackground,
        overlaySurface,
        overlaySurfaceHigh,
        cardBackground,
        cardBackgroundElevated,
        cardBorder,
        fieldBackground,
        fieldBorder,
        bottomBarBase,
        tabCard,
        tabCardActive,
        tabCardBorder,
        accent,
        accentMuted,
        textPrimary,
        textSecondary,
        textTertiary,
        divider,
        incognitoBackground,
        incognitoSurface,
        incognitoAccent,
        statusSuccess,
        statusWarning,
        statusError,
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
            cardBackground = cardBackground,
            cardBackgroundElevated = cardBackgroundElevated,
            cardBorder = cardBorder,
            fieldBackground = fieldBackground,
            fieldBorder = fieldBorder,
            bottomBarBase = bottomBarBase,
            tabCard = tabCard,
            tabCardActive = tabCardActive,
            tabCardBorder = tabCardBorder,
            accent = accent,
            accentMuted = accentMuted,
            textPrimary = textPrimary,
            textSecondary = textSecondary,
            textTertiary = textTertiary,
            divider = divider,
            incognitoBackground = incognitoBackground,
            incognitoSurface = incognitoSurface,
            incognitoAccent = incognitoAccent,
            success = statusSuccess,
            warning = statusWarning,
            error = statusError,
        )
    }

    val shellMetrics = remember {
        BrowserShellMetrics(
            panelCorner = 24.dp,
            cardCorner = 18.dp,
            groupedCorner = 14.dp,
            fieldCorner = 12.dp,
            compactCorner = 10.dp,
            barCorner = 28.dp,
            heroSearchCorner = 22.dp,
            liftedShadow = 8.dp,
            floatingShadow = 16.dp,
            sectionSpacing = 24.dp,
            itemSpacing = 12.dp,
            compactSpacing = 8.dp,
            suggestionPreviewHeight = 220.dp,
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
