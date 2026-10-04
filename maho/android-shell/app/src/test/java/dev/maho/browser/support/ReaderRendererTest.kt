package dev.maho.browser.support

import dev.maho.browser.models.ExtractedArticle
import dev.maho.browser.models.ReaderSettings
import dev.maho.browser.models.ReaderTheme
import dev.maho.browser.models.TextDirection
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class ReaderRendererTest {
    private val article = ExtractedArticle(
        id = "article-1",
        title = "Reader title",
        content = "<p>Reader content</p>",
        textContent = "Reader content",
        byline = "Maho Author",
        textDirection = TextDirection.ltr,
        excerpt = "Excerpt",
        siteName = "Maho",
        length = 14,
        publishedTime = "2026-08-08T12:00:00Z",
    )

    @Test
    fun `renders every reader theme and font combination`() {
        val themes = listOf(
            ReaderTheme.Sepia to ThemeCss("#faf2df", "#5a402a", "#666"),
            ReaderTheme.Dark to ThemeCss("#1e1e1e", "#dcdcdc", "#888"),
            ReaderTheme.Light to ThemeCss("#ffffff", "#000000", "#666"),
        )
        val fonts = listOf(
            "System" to "sans-serif",
            "Georgia" to "Georgia, serif",
            "Courier New" to "'Courier New', monospace",
        )

        themes.forEach { (theme, css) ->
            fonts.forEach { (fontFamily, fontStack) ->
                val html = renderReaderHtml(
                    article = article,
                    settings = ReaderSettings(
                        fontFamily = fontFamily,
                        fontSize = 19.5,
                        theme = theme,
                    ),
                )

                assertContains(html, "background-color: ${css.background};")
                assertContains(html, "color: ${css.foreground};")
                assertContains(html, "color: ${css.byline};")
                assertContains(html, "font-family: $fontStack;")
                assertContains(html, "font-size: 19.5px;")
                assertContains(html, "<h1 class=\"title\">Reader title</h1>")
                assertContains(html, "<p class=\"byline\">By Maho Author</p>")
                assertContains(html, "<p>Reader content</p>")
            }
        }
    }

    @Test
    fun `omits the byline element when the article has no byline`() {
        val html = renderReaderHtml(
            article = article.copy(byline = ""),
            settings = ReaderSettings(
                fontFamily = "System",
                fontSize = 18.0,
                theme = ReaderTheme.Light,
            ),
        )

        assertFalse(html.contains("class=\"byline\""))
    }

    private fun assertContains(html: String, expected: String) {
        assertTrue("Expected rendered HTML to contain: $expected", html.contains(expected))
    }

    private data class ThemeCss(
        val background: String,
        val foreground: String,
        val byline: String,
    )
}
