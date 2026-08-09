package dev.maho.browser.ui

import android.util.Log
import android.webkit.WebView
import dev.maho.browser.bridge.BridgeSettings
import dev.maho.browser.models.ExtractedArticle
import dev.maho.browser.models.ReaderSettings
import dev.maho.browser.models.ReaderTheme
import dev.maho.browser.models.TextDirection
import dev.maho.browser.support.renderReaderHtml
import org.json.JSONObject
import org.json.JSONTokener
import java.util.UUID

internal fun runMainBrowserReadabilityExtraction(
    webView: WebView,
    onReaderUrlCaptured: (String?) -> Unit,
) {
    val readabilityScript = try {
        webView.context.assets.open("readability.js").bufferedReader().use { it.readText() }
    } catch (error: Exception) {
        Log.e("MahoReader", "Failed to load readability.js", error)
        ""
    }
    val extractionScript = """
        $readabilityScript
        (function() {
            try {
                var docClone = document.cloneNode(true);
                var article = new Readability(docClone).parse();
                if (article) {
                    return JSON.stringify({
                        title: article.title || "",
                        content: article.content || "",
                        textContent: article.textContent || "",
                        byline: article.byline || "",
                        dir: article.dir || "",
                        excerpt: article.excerpt || null,
                        siteName: article.siteName || null,
                        length: article.length || null,
                        publishedTime: article.publishedTime || null
                    });
                }
                return JSON.stringify({ error: "Failed to parse page content." });
            } catch (e) {
                return JSON.stringify({ error: e.toString() });
            }
        })();
    """.trimIndent()

    webView.evaluateJavascript(extractionScript) { json ->
        if (json == null || json == "null" || json.startsWith("\"error")) return@evaluateJavascript
        try {
            val rawJson = if (json.startsWith("\"") && json.endsWith("\"")) {
                JSONObject.quote(json).substring(1, json.length - 1)
                    .replace("\\\"", "\"")
                    .replace("\\\\", "\\")
            } else json
            val parsedJson = if (rawJson.startsWith("\"")) {
                JSONTokener(rawJson).nextValue() as? String ?: rawJson
            } else rawJson
            val unescaped = JSONObject(parsedJson)
            if (unescaped.has("error")) {
                Log.e("MahoReader", "Readability extraction error: " + unescaped.getString("error"))
                return@evaluateJavascript
            }
            val dir = unescaped.optString("dir")
            val textDirection = try {
                TextDirection.valueOf(dir.lowercase())
            } catch (_: Exception) {
                TextDirection.auto
            }
            val article = ExtractedArticle(
                id = UUID.randomUUID().toString(),
                title = unescaped.optString("title"),
                content = unescaped.optString("content"),
                textContent = unescaped.optString("textContent"),
                byline = unescaped.optString("byline"),
                textDirection = textDirection,
                excerpt = unescaped.optionalString("excerpt"),
                siteName = unescaped.optionalString("siteName"),
                length = if (unescaped.isNull("length")) null else unescaped.optInt("length"),
                publishedTime = unescaped.optionalString("publishedTime"),
            )
            val settings = BridgeSettings.getSettingsTyped()?.reader ?: ReaderSettings(
                fontFamily = "System",
                fontSize = 18.0,
                theme = ReaderTheme.Light,
            )
            onReaderUrlCaptured(webView.url)
            webView.loadDataWithBaseURL(
                webView.url,
                renderReaderHtml(article, settings),
                "text/html",
                "UTF-8",
                null,
            )
        } catch (error: Exception) {
            Log.e("MahoReader", "Failed to parse json: " + error.message)
        }
    }
}

private fun JSONObject.optionalString(key: String): String? =
    if (isNull(key)) null else optString(key)
