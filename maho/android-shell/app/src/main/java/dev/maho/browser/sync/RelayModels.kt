package dev.maho.browser.sync

import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable

@Serializable
data class RelayAccount(
    val id: String? = null,
    val email: String,
    @SerialName("display_name") val displayName: String? = null,
)

@Serializable
data class RelayDevice(
    val id: String? = null,
    val name: String? = null,
    @SerialName("device_type") val deviceType: String? = null,
)

@Serializable
data class RelaySessionInfo(
    val id: String? = null,
)

@Serializable
data class RelaySession(
    val email: String,
    @SerialName("access_token") val accessToken: String,
    @SerialName("refresh_token") val refreshToken: String,
    @SerialName("server_url") val serverUrl: String? = null,
    val account: RelayAccount? = null,
    val device: RelayDevice? = null,
    val session: RelaySessionInfo? = null,
)

@Serializable
data class RelayAuthRequest(
    val email: String,
    val password: String,
)

@Serializable
data class RelayRefreshRequest(
    @SerialName("refresh_token") val refreshToken: String,
)
