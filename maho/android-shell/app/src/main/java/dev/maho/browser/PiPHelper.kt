package dev.maho.browser

import android.app.Activity
import android.app.PictureInPictureParams
import android.os.Build
import android.webkit.WebView

object PiPHelper {

    fun enterPiP(activity: Activity) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val params = PictureInPictureParams.Builder().build()
            activity.enterPictureInPictureMode(params)
        } else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
            @Suppress("DEPRECATION")
            activity.enterPictureInPictureMode()
        }
    }

    fun isVideoPlaying(webView: WebView?, onResult: (Boolean) -> Unit) {
        webView ?: run {
            onResult(false)
            return
        }
        val script = """
            (function() {
                var video = document.querySelector('video');
                if (video) {
                    return !video.paused && !video.ended && video.currentTime > 0;
                }
                return false;
            })();
        """.trimIndent()
        webView.evaluateJavascript(script) { result ->
            onResult(result == "true")
        }
    }
}
