package dev.maho.browser.sync

import android.content.Context

object RelaySessionStore {
    private const val PREFS_NAME = "maho_relay_session"
    private const val KEY_EMAIL = "email"
    private const val KEY_ACCESS_TOKEN = "access_token"
    private const val KEY_REFRESH_TOKEN = "refresh_token"
    private const val KEY_SERVER_URL = "server_url"
    private const val KEY_ACCOUNT_ID = "account_id"
    private const val KEY_ACCOUNT_EMAIL = "account_email"
    private const val KEY_ACCOUNT_DISPLAY_NAME = "account_display_name"
    private const val KEY_DEVICE_ID = "device_id"
    private const val KEY_DEVICE_NAME = "device_name"
    private const val KEY_DEVICE_TYPE = "device_type"
    private const val KEY_SESSION_ID = "session_id"

    private var appContext: Context? = null

    fun init(context: Context) {
        appContext = context.applicationContext
    }

    fun save(session: RelaySession) {
        val prefs = prefs() ?: return
        prefs.edit()
            .putString(KEY_EMAIL, session.email)
            .putString(KEY_ACCESS_TOKEN, session.accessToken)
            .putString(KEY_REFRESH_TOKEN, session.refreshToken)
            .putString(KEY_SERVER_URL, session.serverUrl)
            .putString(KEY_ACCOUNT_ID, session.account?.id)
            .putString(KEY_ACCOUNT_EMAIL, session.account?.email)
            .putString(KEY_ACCOUNT_DISPLAY_NAME, session.account?.displayName)
            .putString(KEY_DEVICE_ID, session.device?.id)
            .putString(KEY_DEVICE_NAME, session.device?.name)
            .putString(KEY_DEVICE_TYPE, session.device?.deviceType)
            .putString(KEY_SESSION_ID, session.session?.id)
            .apply()
    }

    fun load(): RelaySession? {
        val prefs = prefs() ?: return null
        val email = prefs.getString(KEY_EMAIL, null)?.takeIf { it.isNotBlank() } ?: return null
        val accessToken = prefs.getString(KEY_ACCESS_TOKEN, null)?.takeIf { it.isNotBlank() } ?: return null
        val refreshToken = prefs.getString(KEY_REFRESH_TOKEN, null)?.takeIf { it.isNotBlank() } ?: return null
        val serverUrl = prefs.getString(KEY_SERVER_URL, null)?.takeIf { it.isNotBlank() }
        val accountId = prefs.getString(KEY_ACCOUNT_ID, null)?.takeIf { it.isNotBlank() }
        val accountEmail = prefs.getString(KEY_ACCOUNT_EMAIL, null)?.takeIf { it.isNotBlank() } ?: email
        val accountDisplayName = prefs.getString(KEY_ACCOUNT_DISPLAY_NAME, null)?.takeIf { it.isNotBlank() }
        val deviceId = prefs.getString(KEY_DEVICE_ID, null)?.takeIf { it.isNotBlank() }
        val deviceName = prefs.getString(KEY_DEVICE_NAME, null)?.takeIf { it.isNotBlank() }
        val deviceType = prefs.getString(KEY_DEVICE_TYPE, null)?.takeIf { it.isNotBlank() }
        val sessionId = prefs.getString(KEY_SESSION_ID, null)?.takeIf { it.isNotBlank() }
        return RelaySession(
            email = email,
            accessToken = accessToken,
            refreshToken = refreshToken,
            serverUrl = serverUrl,
            account = RelayAccount(id = accountId, email = accountEmail, displayName = accountDisplayName),
            device = if (deviceId != null || deviceName != null || deviceType != null) {
                RelayDevice(id = deviceId, name = deviceName, deviceType = deviceType)
            } else {
                null
            },
            session = sessionId?.let { RelaySessionInfo(id = it) },
        )
    }

    fun updateServerUrl(serverUrl: String) {
        prefs()?.edit()?.putString(KEY_SERVER_URL, serverUrl.ifBlank { null })?.apply()
    }

    fun getServerUrl(defaultUrl: String): String {
        return prefs()?.getString(KEY_SERVER_URL, defaultUrl)?.takeIf { it.isNotBlank() } ?: defaultUrl
    }

    fun clear() {
        prefs()?.edit()
            ?.remove(KEY_EMAIL)
            ?.remove(KEY_ACCESS_TOKEN)
            ?.remove(KEY_REFRESH_TOKEN)
            ?.remove(KEY_SERVER_URL)
            ?.remove(KEY_ACCOUNT_ID)
            ?.remove(KEY_ACCOUNT_EMAIL)
            ?.remove(KEY_ACCOUNT_DISPLAY_NAME)
            ?.remove(KEY_DEVICE_ID)
            ?.remove(KEY_DEVICE_NAME)
            ?.remove(KEY_DEVICE_TYPE)
            ?.remove(KEY_SESSION_ID)
            ?.apply()
    }

    fun hasSession(): Boolean = load() != null

    private fun prefs() = appContext?.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
}
