package dev.maho.browser.bridge

import dev.maho.browser.MahoBridge
import dev.maho.browser.models.CoreUpdate
import dev.maho.browser.support.MahoJson

fun MahoBridge.saveAndGetResult(): Boolean = saveState()

fun MahoBridge.loadAndGetResult(): Boolean = loadState()

fun MahoBridge.tickUpdates(): List<CoreUpdate> {
    val json = tick() ?: return emptyList()
    return try {
        MahoJson.instance.decodeFromString<List<CoreUpdate>>(json)
    } catch (_: Exception) {
        emptyList()
    }
}
