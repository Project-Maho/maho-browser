package dev.maho.browser.ui.theme

import android.content.res.Configuration
import android.webkit.WebView
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.State
import androidx.compose.runtime.mutableStateOf
import androidx.webkit.WebSettingsCompat
import androidx.webkit.WebViewFeature
import dev.maho.browser.models.Theme
import java.util.WeakHashMap

/**
 * Process-local source of truth for the currently selected app appearance.
 * Persistence remains in the Rust settings core; this state exists so a theme
 * change from native settings or the shared web onboarding can recompose the
 * Android shell and all hosted WebViews immediately.
 */
object BrowserThemeController {
    private val _selectedTheme = mutableStateOf(Theme.System)
    val selectedTheme: State<Theme> get() = _selectedTheme
    private val webViews = WeakHashMap<WebView, Unit>()

    fun setTheme(theme: Theme) {
        _selectedTheme.value = theme
        val snapshot = synchronized(webViews) { webViews.keys.toList() }
        snapshot.forEach { applyToWebView(it, theme) }
    }

    fun registerWebView(webView: WebView) {
        synchronized(webViews) { webViews[webView] = Unit }
        applyToWebView(webView)
    }

    fun unregisterWebView(webView: WebView) {
        synchronized(webViews) { webViews.remove(webView) }
    }

    fun applyToWebView(webView: WebView, theme: Theme = _selectedTheme.value) {
        val dark = resolveDarkTheme(theme, webView.resources.configuration)
        if (WebViewFeature.isFeatureSupported(WebViewFeature.ALGORITHMIC_DARKENING)) {
            WebSettingsCompat.setAlgorithmicDarkeningAllowed(webView.settings, dark)
        }

        val mode = when (theme) {
            Theme.System -> "system"
            Theme.Dark -> "dark"
            Theme.Light -> "light"
        }
        val resolved = if (dark) "dark" else "light"
        val script = buildColorSchemeScript(mode, resolved)
        webView.post { webView.evaluateJavascript(script, null) }
    }

    internal fun buildColorSchemeScript(mode: String, resolved: String): String =
        """
            (function() {
              var media = window.matchMedia('(prefers-color-scheme: dark)');
              if (!window.__mahoApplyColorScheme) {
                window.__mahoThemeMode = 'system';
                window.__mahoApplyColorScheme = function(nextMode) {
                  window.__mahoThemeMode = nextMode || 'system';
                  var nextResolved = window.__mahoThemeMode === 'system'
                    ? (media.matches ? 'dark' : 'light')
                    : window.__mahoThemeMode;
                  document.documentElement.setAttribute('data-maho-color-scheme', nextResolved);
                };
                var onSystemThemeChanged = function() {
                  if (window.__mahoThemeMode === 'system') {
                    window.__mahoApplyColorScheme('system');
                  }
                };
                if (media.addEventListener) media.addEventListener('change', onSystemThemeChanged);
                else if (media.addListener) media.addListener(onSystemThemeChanged);
              }
              window.__mahoApplyColorScheme('$mode');
              document.documentElement.setAttribute('data-maho-resolved-color-scheme', '$resolved');
            })();
        """.trimIndent()

    @Composable
    fun isDarkTheme(): Boolean {
        val systemDark = isSystemInDarkTheme()
        return when (_selectedTheme.value) {
            Theme.Dark -> true
            Theme.Light -> false
            Theme.System -> systemDark
        }
    }

    private fun resolveDarkTheme(theme: Theme, configuration: Configuration): Boolean {
        if (theme == Theme.Dark) return true
        if (theme == Theme.Light) return false
        val nightMode = configuration.uiMode and Configuration.UI_MODE_NIGHT_MASK
        return nightMode == Configuration.UI_MODE_NIGHT_YES
    }
}
