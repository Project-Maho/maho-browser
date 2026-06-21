package dev.maho.browser.support

import android.graphics.Bitmap
import android.graphics.Canvas
import android.webkit.WebView
import java.io.ByteArrayOutputStream

suspend fun captureWebViewScreenshot(webView: WebView): ByteArray? {
    val width = webView.width.takeIf { it > 0 } ?: return null
    val height = webView.height.takeIf { it > 0 } ?: return null

    val bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888)

    val captured = try {
        webView.draw(Canvas(bitmap))
        true
    } catch (_: Exception) {
        false
    }

    if (!captured) {
        bitmap.recycle()
        return null
    }

    return try {
        ByteArrayOutputStream().use { out ->
            bitmap.compress(Bitmap.CompressFormat.JPEG, 80, out)
            out.toByteArray()
        }
    } finally {
        bitmap.recycle()
    }
}
