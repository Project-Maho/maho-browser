package dev.maho.browser.integration

import android.net.Uri
import android.os.Bundle
import androidx.browser.customtabs.CustomTabsCallback
import androidx.browser.customtabs.CustomTabsService
import dev.maho.browser.MahoBridge
import dev.maho.browser.bridge.sendEvent
import dev.maho.browser.models.ShellEvent

class MahoCustomTabsService : CustomTabsService() {

    override fun onCreate() {
        super.onCreate()
    }

    override fun warmup(flags: Long): Boolean {
        return true
    }

    override fun newSession(token: androidx.browser.customtabs.CustomTabsSessionToken): Boolean {
        return true
    }

    override fun mayLaunchUrl(
        sessionToken: androidx.browser.customtabs.CustomTabsSessionToken,
        url: Uri?,
        extras: Bundle?,
        otherLikelyBundles: MutableList<Bundle>?
    ): Boolean {
        // Pre-warm hint only — do NOT create tabs here.
        // Other apps call mayLaunchUrl to signal URLs the user is likely to visit.
        // Actual tab creation happens when the client launches a Custom Tab intent.
        return true
    }

    override fun extraCommand(commandName: String, args: Bundle?): Bundle? {
        return null
    }

    override fun updateVisuals(
        sessionToken: androidx.browser.customtabs.CustomTabsSessionToken,
        bundle: Bundle?
    ): Boolean {
        return false
    }

    override fun requestPostMessageChannel(
        sessionToken: androidx.browser.customtabs.CustomTabsSessionToken,
        postMessageOrigin: Uri
    ): Boolean {
        return false
    }

    override fun postMessage(
        sessionToken: androidx.browser.customtabs.CustomTabsSessionToken,
        message: String,
        extras: Bundle?
    ): Int {
        return RESULT_FAILURE_DISALLOWED
    }

    override fun validateRelationship(
        sessionToken: androidx.browser.customtabs.CustomTabsSessionToken,
        relation: Int,
        requestOrigin: Uri,
        extras: Bundle?
    ): Boolean {
        return false
    }

    override fun receiveFile(
        sessionToken: androidx.browser.customtabs.CustomTabsSessionToken,
        uri: Uri,
        purpose: Int,
        extras: Bundle?
    ): Boolean {
        return false
    }

    private fun openUrlInMaho(url: String) {
        MahoBridge.sendEvent(ShellEvent.CreateTab(spaceId = getActiveSpaceId(), url = url))
    }

    private fun getActiveSpaceId(): String {
        return MahoBridge.getActiveSpaceId() ?: ""
    }
}
