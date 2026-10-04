package dev.maho.browser.integration

import android.content.Intent
import android.net.Uri
import dev.maho.browser.MahoBridge
import dev.maho.browser.bridge.sendEvent
import dev.maho.browser.models.ShellEvent

object IntentRouter {

    fun routeIntent(intent: Intent): Boolean {
        if (intent.action != Intent.ACTION_VIEW) return false

        val data = intent.data
        return when (data?.scheme?.lowercase()) {
            "maho" -> routeCustomScheme(data)
            "http", "https" -> routeWebUrl(data)
            else -> false
        }
    }

    private fun routeCustomScheme(data: Uri): Boolean {
        return when (data.host?.lowercase()) {
            "open" -> {
                val url = data.getQueryParameter("url")
                if (url != null) {
                    MahoBridge.sendEvent(ShellEvent.CreateTab(spaceId = getActiveSpaceId(), url = url))
                    true
                } else {
                    false
                }
            }
            "newtab" -> {
                MahoBridge.sendEvent(ShellEvent.CreateTab(spaceId = getActiveSpaceId(), url = null))
                true
            }
            "search" -> {
                val query = data.getQueryParameter("q")
                if (query != null) {
                    val searchUrl = "maho:search?q=${Uri.encode(query)}"
                    MahoBridge.sendEvent(ShellEvent.CreateTab(spaceId = getActiveSpaceId(), url = searchUrl))
                    true
                } else {
                    false
                }
            }
            else -> false
        }
    }

    private fun routeWebUrl(data: Uri): Boolean {
        val url = data.toString()
        MahoBridge.sendEvent(ShellEvent.CreateTab(spaceId = getActiveSpaceId(), url = url))
        return true
    }

    fun routeIntentToShellEvent(intent: Intent): ShellEvent? {
        if (intent.action != Intent.ACTION_VIEW) return null

        val data = intent.data
        return when (data?.scheme?.lowercase()) {
            "maho" -> when (data.host?.lowercase()) {
                "open" -> data.getQueryParameter("url")?.let {
                    ShellEvent.CreateTab(spaceId = getActiveSpaceId(), url = it)
                }
                "newtab" -> ShellEvent.CreateTab(spaceId = getActiveSpaceId(), url = null)
                "search" -> data.getQueryParameter("q")?.let {
                    ShellEvent.CreateTab(spaceId = getActiveSpaceId(), url = "maho:search?q=${Uri.encode(it)}")
                }
                else -> null
            }
            "http", "https" -> ShellEvent.CreateTab(spaceId = getActiveSpaceId(), url = data.toString())
            else -> null
        }
    }

    private fun getActiveSpaceId(): String {
        return MahoBridge.getActiveSpaceId() ?: ""
    }
}
