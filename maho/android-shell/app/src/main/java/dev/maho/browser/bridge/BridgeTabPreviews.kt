package dev.maho.browser.bridge

import dev.maho.browser.MahoBridge
import dev.maho.browser.models.TabId
import dev.maho.browser.support.MahoJson
import kotlinx.serialization.builtins.serializer

object BridgeTabPreviews {

    fun updateTabPreview(tabId: TabId, imageData: ByteArray) {
        val tabIdJson = MahoJson.instance.encodeToString(String.serializer(), tabId)
        MahoBridge.updateTabPreview(tabIdJson, imageData)
    }

    fun getTabPreview(tabId: TabId): String? {
        val tabIdJson = MahoJson.instance.encodeToString(String.serializer(), tabId)
        return MahoBridge.getTabPreview(tabIdJson)
    }

    fun hasTabPreview(tabId: TabId): Boolean {
        val tabIdJson = MahoJson.instance.encodeToString(String.serializer(), tabId)
        return MahoBridge.hasTabPreview(tabIdJson)
    }

    fun schedulePreviewCapture(tabId: TabId): Boolean {
        val tabIdJson = MahoJson.instance.encodeToString(String.serializer(), tabId)
        return MahoBridge.schedulePreviewCapture(tabIdJson)
    }

    fun freePreviewData() {
        MahoBridge.freePreviewData()
    }
}
