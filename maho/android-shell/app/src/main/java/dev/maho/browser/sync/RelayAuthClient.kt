package dev.maho.browser.sync

import dev.maho.browser.support.MahoJson
import kotlinx.serialization.encodeToString
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.contentOrNull
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive
import kotlinx.serialization.json.jsonArray
import okhttp3.HttpUrl.Companion.toHttpUrl
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.RequestBody.Companion.toRequestBody
import java.io.IOException
import java.util.concurrent.TimeUnit

class RelayAuthClient(
    private val client: OkHttpClient = defaultClient(),
) {
    companion object {
        private const val AUTH_SIGNUP = "auth/signup"
        private const val AUTH_LOGIN = "auth/login"
        private const val AUTH_OAUTH_GOOGLE = "auth/oauth/google"
        private const val AUTH_REFRESH = "auth/refresh"
        private const val AUTH_LOGOUT = "auth/logout"
        private const val SYNC_PUSH = "sync/push"
        private const val SYNC_PULL = "sync/pull"
        private const val SYNC_BOOTSTRAP = "sync/bootstrap"
        private val JSON_MEDIA_TYPE = "application/json; charset=utf-8".toMediaType()

        private fun defaultClient(): OkHttpClient = OkHttpClient.Builder()
            .connectTimeout(15, TimeUnit.SECONDS)
            .readTimeout(30, TimeUnit.SECONDS)
            .writeTimeout(30, TimeUnit.SECONDS)
            .callTimeout(45, TimeUnit.SECONDS)
            .build()
    }

    fun cancelActiveCalls() {
        client.dispatcher.cancelAll()
    }

    private val json = MahoJson.instance

    fun signUp(serverUrl: String, email: String, password: String): RelaySession {
        return authenticate(AUTH_SIGNUP, serverUrl, email, password)
    }

    fun logIn(serverUrl: String, email: String, password: String): RelaySession {
        return authenticate(AUTH_LOGIN, serverUrl, email, password)
    }

    fun signInWithGoogle(
        serverUrl: String,
        idToken: String,
        nonce: String,
        fallbackEmail: String = "",
    ): RelaySession {
        val body = json.encodeToString(
            RelayGoogleOAuthRequest.serializer(),
            RelayGoogleOAuthRequest(idToken = idToken, nonce = nonce),
        ).toRequestBody(JSON_MEDIA_TYPE)
        val responseBody = execute(
            method = "POST",
            serverUrl = serverUrl,
            path = AUTH_OAUTH_GOOGLE,
            bearerToken = null,
            body = body,
        )
        return parseSession(responseBody, fallbackEmail, normalizeRelayBaseUrl(serverUrl))
    }

    fun refresh(serverUrl: String, refreshToken: String, fallbackEmail: String? = null): RelaySession {
        val body = json.encodeToString(RelayRefreshRequest.serializer(), RelayRefreshRequest(refreshToken))
            .toRequestBody(JSON_MEDIA_TYPE)
        val responseBody = execute(
            method = "POST",
            serverUrl = serverUrl,
            path = AUTH_REFRESH,
            bearerToken = null,
            body = body,
        )
        return parseSession(responseBody, fallbackEmail ?: "", normalizeRelayBaseUrl(serverUrl))
    }

    fun logout(serverUrl: String, accessToken: String, refreshToken: String? = null) {
        val payload = buildLogoutPayload(refreshToken)
        execute(
            method = "POST",
            serverUrl = serverUrl,
            path = AUTH_LOGOUT,
            bearerToken = accessToken,
            body = payload?.toRequestBody(JSON_MEDIA_TYPE),
        )
    }

    fun pushSync(
        serverUrl: String,
        accessToken: String,
        roomId: String,
        envelopes: List<SyncEnvelopeV2>,
    ): RelaySyncPushResponse {
        val body = json.encodeToString(
            RelaySyncPushRequest.serializer(),
            RelaySyncPushRequest(roomId, envelopes),
        )
            .toRequestBody(JSON_MEDIA_TYPE)
        val request = Request.Builder()
            .url(buildUrl(serverUrl, SYNC_PUSH))
            .addHeader("Authorization", "Bearer $accessToken")
            .post(body)
            .build()
        val responseBody = executeRequest(request)
            ?: throw IOException("Relay sync push response was empty")
        return json.decodeFromString(RelaySyncPushResponse.serializer(), responseBody)
    }

    fun pullSync(
        serverUrl: String,
        accessToken: String,
        roomId: String,
        afterSeq: Long,
        limit: Int,
    ): RelaySyncPullResponse {
        val requestUrl = buildUrl(serverUrl, SYNC_PULL).newBuilder()
            .addQueryParameter("room_id", roomId)
            .addQueryParameter("after_seq", afterSeq.toString())
            .addQueryParameter("limit", limit.toString())
            .build()
        val request = Request.Builder()
            .url(requestUrl)
            .addHeader("Authorization", "Bearer $accessToken")
            .get()
            .build()
        val responseBody = executeRequest(request)
        return decodePullResponse(responseBody)
    }

    fun getSyncBootstrap(serverUrl: String, accessToken: String): RelaySyncBootstrap {
        val body = execute(
            method = "GET",
            serverUrl = serverUrl,
            path = SYNC_BOOTSTRAP,
            bearerToken = accessToken,
            body = null,
        ) ?: throw IOException("Relay Sync bootstrap response was empty")
        return json.decodeFromString(RelaySyncBootstrap.serializer(), body)
    }

    fun putSyncBootstrap(
        serverUrl: String,
        accessToken: String,
        bootstrap: RelaySyncBootstrap,
    ): RelaySyncBootstrap {
        val body = json.encodeToString(
            RelaySyncBootstrapRequest.serializer(),
            RelaySyncBootstrapRequest(
                version = bootstrap.version,
                roomId = bootstrap.roomId,
                seed = bootstrap.seed,
            ),
        )
            .toRequestBody(JSON_MEDIA_TYPE)
        val response = execute(
            method = "PUT",
            serverUrl = serverUrl,
            path = SYNC_BOOTSTRAP,
            bearerToken = accessToken,
            body = body,
        ) ?: throw IOException("Relay Sync bootstrap response was empty")
        return json.decodeFromString(RelaySyncBootstrap.serializer(), response)
    }

    private fun authenticate(path: String, serverUrl: String, email: String, password: String): RelaySession {
        val body = json.encodeToString(RelayAuthRequest.serializer(), RelayAuthRequest(email = email, password = password))
            .toRequestBody(JSON_MEDIA_TYPE)
        val responseBody = execute(
            method = "POST",
            serverUrl = serverUrl,
            path = path,
            bearerToken = null,
            body = body,
        )
        return parseSession(responseBody, email, normalizeRelayBaseUrl(serverUrl))
    }

    private fun execute(
        method: String,
        serverUrl: String,
        path: String,
        bearerToken: String?,
        body: okhttp3.RequestBody?,
    ): String? {
        val requestBuilder = Request.Builder()
            .url(buildUrl(serverUrl, path))
            .method(method, body)
        if (bearerToken != null) {
            requestBuilder.addHeader("Authorization", "Bearer $bearerToken")
        }
        return executeRequest(requestBuilder.build())
    }

    private fun executeRequest(request: Request): String? {
        client.newCall(request).execute().use { response ->
            val body = response.body?.string()
            if (!response.isSuccessful) {
                throw RelayApiException(response.code, body ?: response.message)
            }
            return body?.takeIf { it.isNotBlank() }
        }
    }

    private fun parseSession(body: String?, fallbackEmail: String, serverUrl: String): RelaySession {
        val jsonBody = body?.takeIf { it.isNotBlank() }
            ?: throw IOException("Relay auth response was empty")
        val obj = json.parseToJsonElement(jsonBody).jsonObject
        val accessToken = obj.stringValue("access_token", "accessToken")
            ?: throw IOException("Relay auth response missing access token")
        val refreshToken = obj.stringValue("refresh_token", "refreshToken")
            ?: throw IOException("Relay auth response missing refresh token")
        val expiresAt = obj.longValue("expires_at", "expiresAt")
        val email = obj.stringValue("email") ?: obj.nestedStringValue("account", "email") ?: fallbackEmail
        val parsedAccount = obj.nestedAccount()
        val account = parsedAccount?.account
        val device = obj.nestedDevice()
        val session = obj.nestedSession()
        return RelaySession(
            email = email,
            accessToken = accessToken,
            refreshToken = refreshToken,
            expiresAt = expiresAt,
            serverUrl = serverUrl,
            account = account ?: RelayAccount(email = email),
            device = device,
            session = session,
            identityMetadataState = parsedAccount?.identityMetadataState
                ?: RelayIdentityMetadataState.ABSENT,
        )
    }

    private fun decodePullResponse(body: String?): RelaySyncPullResponse {
        val jsonBody = body?.takeIf { it.isNotBlank() }
            ?: return RelaySyncPullResponse()
        return json.decodeFromString(RelaySyncPullResponse.serializer(), jsonBody)
    }

    private fun buildUrl(serverUrl: String, path: String) = normalizeRelayBaseUrl(serverUrl).toHttpUrl().also {
        require(it.isHttps) { "Relay endpoints must use HTTPS" }
    }.newBuilder()
        .addPathSegments(path.trimStart('/'))
        .build()

    private fun buildLogoutPayload(refreshToken: String?): String? {
        if (refreshToken.isNullOrBlank()) return null
        return json.encodeToString(RelayRefreshRequest.serializer(), RelayRefreshRequest(refreshToken))
    }

    private fun JsonObject.stringValue(vararg keys: String): String? {
        for (key in keys) {
            val value = this[key]?.jsonPrimitive?.contentOrNull
            if (!value.isNullOrBlank()) return value
        }
        return null
    }

    private fun JsonObject.nestedStringValue(containerKey: String, vararg keys: String): String? {
        val container = this[containerKey]?.jsonObject ?: return null
        return container.stringValue(*keys)
    }

    private fun JsonObject.longValue(vararg keys: String): Long? {
        for (key in keys) {
            val value = this[key]?.jsonPrimitive?.contentOrNull?.toLongOrNull()
            if (value != null) return value
        }
        return null
    }

    private data class ParsedRelayAccount(
        val account: RelayAccount,
        val identityMetadataState: RelayIdentityMetadataState,
    )

    private fun JsonObject.nestedAccount(): ParsedRelayAccount? {
        val container = this["account"]?.jsonObject ?: return null
        val email = container.stringValue("email") ?: return null
        val providerElement = container["oauth_provider"] ?: container["oauthProvider"]
        val providerSubElement = container["oauth_provider_sub"] ?: container["oauthProviderSub"]
        val provider = providerElement?.jsonPrimitive?.contentOrNull?.trim()
        val providerSub = providerSubElement?.jsonPrimitive?.contentOrNull?.trim()
        val identityMetadataState = when {
            providerElement == null && providerSubElement == null -> RelayIdentityMetadataState.ABSENT
            provider == "google" && !providerSub.isNullOrEmpty() -> RelayIdentityMetadataState.VALID_GOOGLE
            else -> RelayIdentityMetadataState.INVALID
        }
        return ParsedRelayAccount(
            account = RelayAccount(
                id = container.stringValue("id"),
                email = email,
                displayName = container.stringValue("display_name", "displayName"),
                oauthProvider = provider.takeIf { identityMetadataState == RelayIdentityMetadataState.VALID_GOOGLE },
                oauthProviderSub = providerSub.takeIf { identityMetadataState == RelayIdentityMetadataState.VALID_GOOGLE },
            ),
            identityMetadataState = identityMetadataState,
        )
    }

    private fun JsonObject.nestedDevice(): RelayDevice? {
        val container = this["device"]?.jsonObject ?: return null
        return RelayDevice(
            id = container.stringValue("id"),
            name = container.stringValue("name"),
            deviceType = container.stringValue("device_type", "deviceType"),
        )
    }

    private fun JsonObject.nestedSession(): RelaySessionInfo? {
        val container = this["session"]?.jsonObject ?: return null
        return RelaySessionInfo(
            id = container.stringValue("id"),
        )
    }
}

class RelayApiException(
    val code: Int,
    message: String,
) : IOException(message)
