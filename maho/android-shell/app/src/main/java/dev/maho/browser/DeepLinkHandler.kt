package dev.maho.browser

import android.content.Intent
import android.net.Uri

/**
 * Parses incoming deep-link intents for the Maho browser.
 *
 * App Links verification:
 * To enable autoVerify for https URLs, host an assetlinks.json at
 * https://<domain>/.well-known/assetlinks.json with the following content:
 * [
 *   {
 *     "relation": ["delegate_permission/common.handle_all_urls"],
 *     "target": {
 *       "namespace": "android_app",
 *       "package_name": "dev.maho.browser",
 *       "sha256_cert_fingerprints": ["<YOUR_SHA256_FINGERPRINT>"]
 *     }
 *   }
 * ]
 */
object DeepLinkHandler {

    fun parse(intent: Intent): String? {
        val action = intent.action
        val data = intent.data
        val extras = intent.extras

        return when (action) {
            Intent.ACTION_VIEW -> parseViewUri(data)
            Intent.ACTION_SEND -> parseSendText(extras)
            Intent.ACTION_SEND_MULTIPLE -> parseSendText(extras)
            else -> null
        }
    }

    private fun parseViewUri(data: Uri?): String? {
        if (data == null) return null
        return when (data.scheme?.lowercase()) {
            "maho" -> parseMahoUri(data)
            "http", "https" -> data.toString()
            else -> null
        }
    }

    private fun parseMahoUri(data: Uri): String? {
        return when (data.host?.lowercase()) {
            "open" -> data.getQueryParameter("url")
            "search" -> {
                val query = data.getQueryParameter("q")
                if (query != null) "maho:search?q=${Uri.encode(query)}" else null
            }
            "newtab" -> "maho:newtab"
            else -> null
        }
    }

    private fun parseSendText(extras: android.os.Bundle?): String? {
        val text = extras?.getString(Intent.EXTRA_TEXT) ?: return null
        return extractUrl(text)
    }

    private fun extractUrl(text: String): String? {
        val trimmed = text.trim()
        return when {
            trimmed.startsWith("http://", ignoreCase = true) ||
                trimmed.startsWith("https://", ignoreCase = true) -> trimmed
            else -> {
                val regex = Regex("(https?://[^\\s]+)", RegexOption.IGNORE_CASE)
                regex.find(trimmed)?.value
            }
        }
    }
}
