package dev.maho.browser.sync

import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable
import kotlinx.serialization.Transient

@Serializable
enum class RelayIdentityMetadataState {
    ABSENT,
    VALID_GOOGLE,
    INVALID,
}

@Serializable
data class RelayAccount(
    val id: String? = null,
    val email: String,
    @SerialName("display_name") val displayName: String? = null,
    @SerialName("oauth_provider") val oauthProvider: String? = null,
    @SerialName("oauth_provider_sub") val oauthProviderSub: String? = null,
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
    @SerialName("expires_at") val expiresAt: Long? = null,
    @SerialName("server_url") val serverUrl: String? = null,
    val account: RelayAccount? = null,
    val device: RelayDevice? = null,
    val session: RelaySessionInfo? = null,
    @Transient val identityMetadataState: RelayIdentityMetadataState = RelayIdentityMetadataState.ABSENT,
)

sealed interface RelayRefreshMergeResult {
    data class Accepted(val session: RelaySession) : RelayRefreshMergeResult
    data class Rejected(val reason: String) : RelayRefreshMergeResult
}

fun mergeRelayRefreshSession(
    current: RelaySession,
    refreshed: RelaySession,
): RelayRefreshMergeResult = when (refreshed.identityMetadataState) {
    RelayIdentityMetadataState.VALID_GOOGLE -> RelayRefreshMergeResult.Accepted(refreshed)
    RelayIdentityMetadataState.INVALID -> RelayRefreshMergeResult.Rejected(
        "Relay refresh returned invalid OAuth identity metadata",
    )
    RelayIdentityMetadataState.ABSENT -> {
        val priorProvider = current.account?.oauthProvider?.trim()
        val priorProviderSub = current.account?.oauthProviderSub?.trim()
        val priorIsValidGoogle = priorProvider == "google" && !priorProviderSub.isNullOrEmpty()
        val mergedAccount = if (priorIsValidGoogle) {
            (refreshed.account ?: RelayAccount(email = refreshed.email)).copy(
                oauthProvider = "google",
                oauthProviderSub = priorProviderSub,
            )
        } else {
            refreshed.account
        }
        RelayRefreshMergeResult.Accepted(refreshed.copy(account = mergedAccount))
    }
}

@Serializable
data class RelayAuthRequest(
    val email: String,
    val password: String,
)

@Serializable
data class RelayGoogleOAuthRequest(
    @SerialName("id_token") val idToken: String,
    val nonce: String,
)

@Serializable
data class RelayRefreshRequest(
    @SerialName("refresh_token") val refreshToken: String,
)
