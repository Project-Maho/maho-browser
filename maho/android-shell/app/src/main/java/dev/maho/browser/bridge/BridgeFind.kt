package dev.maho.browser.bridge

import dev.maho.browser.MahoBridge
import dev.maho.browser.models.FindResult
import dev.maho.browser.models.TabId
import dev.maho.browser.support.MahoJson
import kotlinx.serialization.builtins.serializer

object BridgeFind {
    fun startFind(tabId: TabId, query: String): FindResult? {
        val tabIdJson = MahoJson.instance.encodeToString(String.serializer(), tabId)
        val resultJson = MahoBridge.startFind(tabIdJson, query) ?: return null
        return try {
            MahoJson.instance.decodeFromString(FindResult.serializer(), resultJson)
        } catch (_: Exception) {
            null
        }
    }

    fun findNext(): FindResult? {
        val json = MahoBridge.findNext() ?: return null
        return try {
            MahoJson.instance.decodeFromString(FindResult.serializer(), json)
        } catch (_: Exception) {
            null
        }
    }

    fun findPrevious(): FindResult? {
        val json = MahoBridge.findPrevious() ?: return null
        return try {
            MahoJson.instance.decodeFromString(FindResult.serializer(), json)
        } catch (_: Exception) {
            null
        }
    }

    fun dismissFind() {
        MahoBridge.dismissFind()
    }
}
