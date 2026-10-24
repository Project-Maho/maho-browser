package dev.maho.browser.ui.settings

import android.content.Context
import android.webkit.CookieManager
import android.webkit.WebStorage
import android.webkit.WebView
import dev.maho.browser.bridge.BridgeHistory
import dev.maho.browser.ui.PrivateBrowsingProfile
import dev.maho.browser.ui.WebViewStateStore

/** Shared by Settings, Privacy & Security and Clear Data on Exit. Main thread only (WebView APIs). */
object BrowsingDataCleaner {

    fun clearAll(context: Context) {
        CookieManager.getInstance().removeAllCookies(null)
        CookieManager.getInstance().flush()
        WebStorage.getInstance().deleteAllData()
        WebView(context.applicationContext).apply {
            clearCache(true)
            clearFormData()
            destroy()
        }
        BridgeHistory.clearAllHistory()
        WebViewStateStore.clear()
        PrivateBrowsingProfile.endSession()
    }
}

internal fun shouldClearDataOnExit(
    isFinishing: Boolean,
    isChangingConfigurations: Boolean,
    clearDataOnExit: Boolean,
): Boolean = isFinishing && !isChangingConfigurations && clearDataOnExit
