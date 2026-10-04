package dev.maho.browser.support

import kotlinx.serialization.json.Json

object MahoJson {
    val instance: Json = Json {
        classDiscriminator = "kind"
        ignoreUnknownKeys = true
        isLenient = true
        encodeDefaults = true
        explicitNulls = false
    }
}

inline fun <reified T> T.toJson(): String =
    MahoJson.instance.encodeToString(kotlinx.serialization.serializer<T>(), this)

inline fun <reified T> String.fromJson(): T =
    MahoJson.instance.decodeFromString<T>(this)

inline fun <reified T> String.fromJsonOrNull(): T? =
    try { MahoJson.instance.decodeFromString<T>(this) } catch (_: Exception) { null }
