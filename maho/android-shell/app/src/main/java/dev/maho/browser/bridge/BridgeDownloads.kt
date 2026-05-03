package dev.maho.browser.bridge

import dev.maho.browser.MahoBridge
import dev.maho.browser.models.DownloadViewModel
import dev.maho.browser.support.MahoJson

object BridgeDownloads {
    fun getDownloads(): List<DownloadViewModel> {
        val json = MahoBridge.getDownloadViewModels() ?: return emptyList()
        return try {
            MahoJson.instance.decodeFromString<List<DownloadViewModel>>(json)
        } catch (_: Exception) {
            emptyList()
        }
    }

    fun pauseDownload(id: String) {
        MahoBridge.pauseDownload(id)
    }

    fun resumeDownload(id: String) {
        MahoBridge.resumeDownload(id)
    }

    fun cancelDownload(id: String) {
        MahoBridge.cancelDownload(id)
    }

    fun removeDownload(id: String) {
        MahoBridge.removeDownload(id)
    }
}
