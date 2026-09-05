package dev.maho.browser.ui.theme

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class BrowserThemeControllerTest {

    @Test
    fun colorSchemeScript_doesNotForceDarkOntoLightOnlyPages() {
        val script = BrowserThemeController.buildColorSchemeScript(mode = "dark", resolved = "dark")
        val collapsed = script.replace(Regex("\\s+"), "")

        assertFalse(
            "assigning style.colorScheme makes light-only sites render light text " +
                "on their own light background",
            collapsed.contains("style.colorScheme="),
        )
    }

    @Test
    fun colorSchemeScript_stillExposesResolvedSchemeToPages() {
        val script = BrowserThemeController.buildColorSchemeScript(mode = "dark", resolved = "dark")

        assertTrue(script.contains("data-maho-color-scheme"))
        assertTrue(script.contains("data-maho-resolved-color-scheme"))
    }

    @Test
    fun colorSchemeScript_carriesTheRequestedMode() {
        assertTrue(BrowserThemeController.buildColorSchemeScript("light", "light").contains("'light'"))
        assertTrue(BrowserThemeController.buildColorSchemeScript("system", "dark").contains("'system'"))
    }
}
