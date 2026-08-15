package dev.maho.browser.ui

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class WebViewControllerTest {
    @Test
    fun `main-frame request uses its own URL as the source`() {
        val state = MainDocumentUrlState().apply {
            onPageStarted("https://previous.example/page")
        }

        assertEquals(
            "https://next.example/document",
            state.sourceFor(
                requestUrl = "https://next.example/document",
                isForMainFrame = true,
                requestHeaders = mapOf("Referer" to "https://referrer.example/"),
            ),
        )
    }

    @Test
    fun `subresource uses lifecycle-cached main document URL`() {
        val state = MainDocumentUrlState().apply {
            onPageStarted("https://page.example/started")
            onPageFinished("https://page.example/finished")
        }

        assertEquals(
            "https://page.example/finished",
            state.sourceFor(
                requestUrl = "https://cdn.example/script.js",
                isForMainFrame = false,
                requestHeaders = mapOf("Referer" to "https://fallback.example/"),
            ),
        )
    }

    @Test
    fun `subresource falls back to referer before a lifecycle URL is cached`() {
        val sourceUrl = MainDocumentUrlState().sourceFor(
            requestUrl = "https://cdn.example/script.js",
            isForMainFrame = false,
            requestHeaders = mapOf("referer" to "https://page.example/from-header"),
        )

        assertEquals("https://page.example/from-header", sourceUrl)
    }

    @Test
    fun `interception decision needs only request data and cached lifecycle state`() {
        val state = MainDocumentUrlState().apply {
            onPageStarted("https://page.example/article")
        }
        var observedDecision: Triple<String, String, String>? = null

        val blocked = shouldBlockWebRequest(
            requestUrl = "https://ads.example/banner.js",
            isForMainFrame = false,
            requestHeaders = emptyMap(),
            sourceState = state,
            isSourceWhitelisted = { false },
            queryFilterDecision = { url, sourceUrl, requestType ->
                observedDecision = Triple(url, sourceUrl, requestType)
                true
            },
        )

        assertTrue(blocked)
        assertEquals(
            Triple(
                "https://ads.example/banner.js",
                "https://page.example/article",
                "other",
            ),
            observedDecision,
        )
    }

    @Test
    fun `whitelisted source bypasses filter query`() {
        val state = MainDocumentUrlState().apply {
            onPageStarted("https://allowed.example/article")
        }
        var filterQueried = false

        val blocked = shouldBlockWebRequest(
            requestUrl = "https://ads.example/banner.js",
            isForMainFrame = false,
            requestHeaders = emptyMap(),
            sourceState = state,
            isSourceWhitelisted = { host -> host == "allowed.example" },
            queryFilterDecision = { _, _, _ ->
                filterQueried = true
                true
            },
        )

        assertFalse(blocked)
        assertFalse(filterQueried)
    }
}
