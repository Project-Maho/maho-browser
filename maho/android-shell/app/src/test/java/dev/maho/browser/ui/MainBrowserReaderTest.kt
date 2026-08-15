package dev.maho.browser.ui

import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive
import dev.maho.browser.support.MahoJson
import org.junit.Assert.assertEquals
import org.junit.Test

class MainBrowserReaderTest {
    @Test
    fun `keeps Reader mode through generated document navigation`() {
        val readerUrl = "https://www.akamai.com/ko/what-is-https"

        assertEquals(false, shouldResetReaderModeOnNavigation(readerUrl, readerUrl))
        assertEquals(true, shouldResetReaderModeOnNavigation(readerUrl, "https://example.com/next"))
        assertEquals(true, shouldResetReaderModeOnNavigation(null, readerUrl))
    }

    @Test
    fun `decodes quoted evaluateJavascript readability payload`() {
        val evaluationResult =
            "\"{\\\"title\\\":\\\"Known article\\\",\\\"content\\\":\\\"<p>Extracted body</p>\\\",\\\"textContent\\\":\\\"Extracted body\\\"}\""

        val article = MahoJson.instance.parseToJsonElement(decodeReadabilityResult(evaluationResult)).jsonObject

        assertEquals("Known article", article.getValue("title").jsonPrimitive.content)
        assertEquals("<p>Extracted body</p>", article.getValue("content").jsonPrimitive.content)
        assertEquals("Extracted body", article.getValue("textContent").jsonPrimitive.content)
    }

    @Test
    fun `decodes direct readability payload`() {
        val article = MahoJson.instance.parseToJsonElement(
            decodeReadabilityResult(
                """{"title":"Known article","content":"<p>Extracted body</p>"}""",
            ),
        ).jsonObject

        assertEquals("Known article", article.getValue("title").jsonPrimitive.content)
        assertEquals("<p>Extracted body</p>", article.getValue("content").jsonPrimitive.content)
    }
}
