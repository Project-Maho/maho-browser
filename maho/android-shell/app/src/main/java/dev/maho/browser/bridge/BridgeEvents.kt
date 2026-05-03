package dev.maho.browser.bridge

import dev.maho.browser.MahoBridge
import dev.maho.browser.models.CoreUpdate
import dev.maho.browser.models.ShellEvent
import dev.maho.browser.support.MahoJson

fun MahoBridge.sendEvent(event: ShellEvent): List<CoreUpdate> {
    val eventJson = MahoJson.instance.encodeToString(ShellEvent.serializer(), event)
    val responseJson = handleEvent(eventJson) ?: return emptyList()
    return try {
        MahoJson.instance.decodeFromString<List<CoreUpdate>>(responseJson)
    } catch (_: Exception) {
        emptyList()
    }
}

inline fun <reified T> MahoBridge.decodeResponse(json: String?): T? {
    if (json == null) return null
    return try {
        MahoJson.instance.decodeFromString<T>(json)
    } catch (_: Exception) {
        null
    }
}
