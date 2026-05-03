package dev.maho.browser.integration

import android.content.Intent
import android.net.Uri
import dev.maho.browser.MahoBridge
import dev.maho.browser.bridge.sendEvent
import dev.maho.browser.models.ShellEvent

object ShareTargetHandler {

    fun handleShareIntent(intent: Intent): Boolean {
        return when (intent.action) {
            Intent.ACTION_SEND -> handleSingleSend(intent)
            Intent.ACTION_SEND_MULTIPLE -> handleMultipleSend(intent)
            else -> false
        }
    }

    private fun handleSingleSend(intent: Intent): Boolean {
        val sharedText = intent.getStringExtra(Intent.EXTRA_TEXT) ?: return false
        val url = extractUrl(sharedText)
        return if (url != null) {
            MahoBridge.sendEvent(ShellEvent.CreateTab(spaceId = getActiveSpaceId(), url = url))
            true
        } else {
            false
        }
    }

    private fun handleMultipleSend(intent: Intent): Boolean {
        val texts = intent.getStringArrayListExtra(Intent.EXTRA_TEXT) ?: return false
        if (texts.isEmpty()) return false

        var handled = false
        for (text in texts) {
            val url = extractUrl(text)
            if (url != null) {
                MahoBridge.sendEvent(ShellEvent.CreateTab(spaceId = getActiveSpaceId(), url = url))
                handled = true
            }
        }
        return handled
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

    private fun getActiveSpaceId(): String {
        return MahoBridge.getActiveSpaceId() ?: ""
    }
}
