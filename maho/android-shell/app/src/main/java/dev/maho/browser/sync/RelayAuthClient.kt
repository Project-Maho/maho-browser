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
        private const val AUTH_REFRESH = "auth/refresh"
        private const val AUTH_LOGOUT = "auth/logout"
        private const val SYNC_PUSH = "sync/push"
        private const val SYNC_PULL = "sync/pull"
        private val JSON_MEDIA_TYPE = "application/json; charset=utf-8".toMediaType()

        private fun defaultClient(): OkHttpClient = OkHttpClient.Builder()
            .connectTimeout(15, TimeUnit.SECONDS)
            .readTimeout(30, TimeUnit.SECONDS)
            .writeTimeout(30, TimeUnit.SECONDS)
            .callTimeout(45, TimeUnit.SECONDS)
            .build()
    }

    private val json = MahoJson.instance

    fun signUp(serverUrl: String, email: String, password: String): RelaySession {
        return authenticate(AUTH_SIGNUP, serverUrl, email, password)
    }

    fun logIn(serverUrl: String, email: String, password: String): RelaySession {
        return authenticate(AUTH_LOGIN, serverUrl, email, password)
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

    fun pushSync(serverUrl: String, accessToken: String, blobs: List<String>): String? {
        val body = json.encodeToString(RelaySyncPushRequest.serializer(), RelaySyncPushRequest(blobs))
            .toRequestBody(JSON_MEDIA_TYPE)
        val request = Request.Builder()
            .url(buildUrl(serverUrl, SYNC_PUSH))
            .addHeader("Authorization", "Bearer $accessToken")
            .post(body)
            .build()
        return executeRequest(request)
    }

    fun pullSync(serverUrl: String, accessToken: String, sinceSeq: Long, limit: Int): RelaySyncPullResponse {
        val requestUrl = buildUrl(serverUrl, SYNC_PULL).newBuilder()
            .addQueryParameter("since_seq", sinceSeq.toString())
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
        val email = obj.stringValue("email") ?: obj.nestedStringValue("account", "email") ?: fallbackEmail
        val account = obj.nestedAccount()
        val device = obj.nestedDevice()
        val session = obj.nestedSession()
        return RelaySession(
            email = email,
            accessToken = accessToken,
            refreshToken = refreshToken,
            serverUrl = serverUrl,
            account = account ?: RelayAccount(email = email),
            device = device,
            session = session,
        )
    }

    private fun decodePullResponse(body: String?): RelaySyncPullResponse {
        val jsonBody = body?.takeIf { it.isNotBlank() }
            ?: return RelaySyncPullResponse()
        return json.decodeFromString(RelaySyncPullResponse.serializer(), jsonBody)
    }

    private fun buildUrl(serverUrl: String, path: String) = normalizeRelayBaseUrl(serverUrl).toHttpUrl().newBuilder()
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

    private fun JsonObject.nestedAccount(): RelayAccount? {
        val container = this["account"]?.jsonObject ?: return null
        return RelayAccount(
            id = container.stringValue("id"),
            email = container.stringValue("email") ?: return null,
            displayName = container.stringValue("display_name", "displayName"),
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
