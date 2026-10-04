package dev.maho.browser.ui

import android.graphics.Bitmap
import android.graphics.Color
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(application = android.app.Application::class)
class TabPreviewBlankCheckTest {

    @Test
    fun treatsAFullyUniformCaptureAsBlank() {
        val bitmap = solid(Color.WHITE)

        assertTrue(bitmap.isBlank())
    }

    @Test
    fun keepsAPageWhoseContentSitsAwayFromTheVerticalCentreLine() {
        val bitmap = solid(Color.WHITE)
        for (y in 20 until 90) {
            for (x in 4 until 40) {
                bitmap.setPixel(x, y, Color.BLACK)
            }
        }

        assertFalse(
            "a page with real content must be kept even when its centre column is uniform",
            bitmap.isBlank(),
        )
    }

    private fun solid(color: Int): Bitmap =
        Bitmap.createBitmap(WIDTH, HEIGHT, Bitmap.Config.ARGB_8888).apply {
            eraseColor(color)
        }

    private companion object {
        const val WIDTH = 200
        const val HEIGHT = 400
    }
}
