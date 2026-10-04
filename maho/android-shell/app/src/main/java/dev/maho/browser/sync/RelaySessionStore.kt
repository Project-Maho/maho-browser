package dev.maho.browser.sync

import android.content.Context
import android.content.SharedPreferences
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import android.util.Base64
import java.nio.charset.StandardCharsets
import java.security.GeneralSecurityException
import java.security.KeyStore
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec

enum class LegacyRelayMigrationOutcome {
    NONE,
    ENDPOINT_UPDATED_NO_CREDENTIALS,
    INVALIDATED_SESSION,
}

object RelaySessionStore {
    private const val PREFS_NAME = "maho_relay_session"
    private var store: SessionStore? = null

    fun init(context: Context) {
        val prefs = context.applicationContext.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        store = SessionStore(prefs, AndroidKeystoreTokenCrypto())
    }

    internal val isInitialized: Boolean
        get() = store != null

    fun save(session: RelaySession): Boolean = store?.save(session) ?: false
    fun load(): RelaySession? = store?.load()
    fun migrateRetiredRelaySession(): LegacyRelayMigrationOutcome =
        store?.migrateRetiredRelay() ?: LegacyRelayMigrationOutcome.NONE
    fun updateServerUrl(serverUrl: String) = store?.updateServerUrl(serverUrl) ?: Unit
    fun getServerUrl(defaultUrl: String): String = store?.getServerUrl(defaultUrl) ?: defaultUrl
    fun clear(): Boolean = store?.clear() ?: false
    fun hasSession(): Boolean = load() != null
    fun isSyncEnabled(): Boolean = store?.isSyncEnabled() ?: false
    fun setSyncEnabled(enabled: Boolean): Boolean = store?.setSyncEnabled(enabled) ?: false

    interface TokenCrypto {
        @Throws(GeneralSecurityException::class)
        fun encrypt(plaintext: String): String

        @Throws(GeneralSecurityException::class)
        fun decrypt(ciphertext: String): String
    }

    internal class SessionStore(
        private val prefs: SharedPreferences,
        private val crypto: TokenCrypto,
    ) {
        fun save(session: RelaySession): Boolean {
            val encryptedAccess: String
            val encryptedRefresh: String
            try {
                encryptedAccess = crypto.encrypt(session.accessToken)
                encryptedRefresh = crypto.encrypt(session.refreshToken)
            } catch (_: GeneralSecurityException) {
                return false
            }
            return prefs.edit()
                .putString(KEY_EMAIL, session.email)
                .putString(KEY_ACCESS_TOKEN_ENCRYPTED, encryptedAccess)
                .putString(KEY_REFRESH_TOKEN_ENCRYPTED, encryptedRefresh)
                .remove(KEY_ACCESS_TOKEN)
                .remove(KEY_REFRESH_TOKEN)
                .putLongOrRemove(KEY_EXPIRES_AT, session.expiresAt)
                .putString(KEY_SERVER_URL, session.serverUrl)
                .putString(KEY_ACCOUNT_ID, session.account?.id)
                .putString(KEY_ACCOUNT_EMAIL, session.account?.email)
                .putString(KEY_ACCOUNT_DISPLAY_NAME, session.account?.displayName)
                .putLinkedIdentity(session.account?.oauthProvider, session.account?.oauthProviderSub)
                .putString(KEY_DEVICE_ID, session.device?.id)
                .putString(KEY_DEVICE_NAME, session.device?.name)
                .putString(KEY_DEVICE_TYPE, session.device?.deviceType)
                .putString(KEY_SESSION_ID, session.session?.id)
                .commit()
        }

        fun load(): RelaySession? {
            migrateRetiredRelay()
            val email = prefs.getString(KEY_EMAIL, null)?.takeIf { it.isNotBlank() }
            val encryptedAccess = prefs.getString(KEY_ACCESS_TOKEN_ENCRYPTED, null)?.takeIf { it.isNotBlank() }
            val encryptedRefresh = prefs.getString(KEY_REFRESH_TOKEN_ENCRYPTED, null)?.takeIf { it.isNotBlank() }
            val legacyAccess = prefs.getString(KEY_ACCESS_TOKEN, null)?.takeIf { it.isNotBlank() }
            val legacyRefresh = prefs.getString(KEY_REFRESH_TOKEN, null)?.takeIf { it.isNotBlank() }

            if (email == null) {
                if (encryptedAccess != null || encryptedRefresh != null || legacyAccess != null || legacyRefresh != null) clear()
                return null
            }

            val tokens = when {
                encryptedAccess != null && encryptedRefresh != null -> decryptTokens(encryptedAccess, encryptedRefresh)
                    ?: return clearStale()
                encryptedAccess != null || encryptedRefresh != null -> return clearStale()
                legacyAccess != null && legacyRefresh != null -> {
                    migrateLegacyTokens(legacyAccess, legacyRefresh)
                    legacyAccess to legacyRefresh
                }
                legacyAccess != null || legacyRefresh != null -> return clearStale()
                else -> return clearStale()
            }

            val accountId = prefs.getString(KEY_ACCOUNT_ID, null)?.takeIf { it.isNotBlank() }
            val accountEmail = prefs.getString(KEY_ACCOUNT_EMAIL, null)?.takeIf { it.isNotBlank() } ?: email
            val accountDisplayName = prefs.getString(KEY_ACCOUNT_DISPLAY_NAME, null)?.takeIf { it.isNotBlank() }
            val oauthProvider = prefs.getString(KEY_ACCOUNT_OAUTH_PROVIDER, null)?.trim()?.takeIf { it.isNotEmpty() }
            val oauthProviderSub = prefs.getString(KEY_ACCOUNT_OAUTH_PROVIDER_SUB, null)?.trim()?.takeIf { it.isNotEmpty() }
            val hasLinkedIdentity = oauthProvider != null && oauthProviderSub != null
            if (!hasLinkedIdentity && (oauthProvider != null || oauthProviderSub != null)) {
                prefs.edit()
                    .remove(KEY_ACCOUNT_OAUTH_PROVIDER)
                    .remove(KEY_ACCOUNT_OAUTH_PROVIDER_SUB)
                    .commit()
            }
            val deviceId = prefs.getString(KEY_DEVICE_ID, null)?.takeIf { it.isNotBlank() }
            val deviceName = prefs.getString(KEY_DEVICE_NAME, null)?.takeIf { it.isNotBlank() }
            val deviceType = prefs.getString(KEY_DEVICE_TYPE, null)?.takeIf { it.isNotBlank() }
            val sessionId = prefs.getString(KEY_SESSION_ID, null)?.takeIf { it.isNotBlank() }
            return RelaySession(
                email = email,
                accessToken = tokens.first,
                refreshToken = tokens.second,
                expiresAt = prefs.getLongOrNull(KEY_EXPIRES_AT),
                serverUrl = prefs.getString(KEY_SERVER_URL, null)?.takeIf { it.isNotBlank() },
                account = RelayAccount(
                    id = accountId,
                    email = accountEmail,
                    displayName = accountDisplayName,
                    oauthProvider = oauthProvider.takeIf { hasLinkedIdentity },
                    oauthProviderSub = oauthProviderSub.takeIf { hasLinkedIdentity },
                ),
                device = if (deviceId != null || deviceName != null || deviceType != null) {
                    RelayDevice(id = deviceId, name = deviceName, deviceType = deviceType)
                } else null,
                session = sessionId?.let { RelaySessionInfo(id = it) },
            )
        }

        fun updateServerUrl(serverUrl: String) {
            prefs.edit().putString(KEY_SERVER_URL, serverUrl.ifBlank { null }).apply()
        }

        fun getServerUrl(defaultUrl: String): String {
            migrateRetiredRelay()
            return prefs.getString(KEY_SERVER_URL, defaultUrl)?.takeIf { it.isNotBlank() } ?: defaultUrl
        }

        fun isSyncEnabled(): Boolean = prefs.getBoolean(KEY_SYNC_ENABLED, false)

        fun setSyncEnabled(enabled: Boolean): Boolean =
            prefs.edit().putBoolean(KEY_SYNC_ENABLED, enabled).commit()

        fun clear(): Boolean = prefs.edit().clear().commit()

        fun migrateRetiredRelay(): LegacyRelayMigrationOutcome {
            if (prefs.getString(KEY_SERVER_URL, null) != RETIRED_SERVER_URL) {
                return LegacyRelayMigrationOutcome.NONE
            }
            val hasSessionMaterial = listOf(
                KEY_ACCESS_TOKEN,
                KEY_REFRESH_TOKEN,
                KEY_ACCESS_TOKEN_ENCRYPTED,
                KEY_REFRESH_TOKEN_ENCRYPTED,
                KEY_SYNC_BOOTSTRAP_SEED,
            ).any { !prefs.getString(it, null).isNullOrBlank() }
            val editor = prefs.edit()
            if (hasSessionMaterial) editor.clear()
            if (!editor.putString(KEY_SERVER_URL, PRODUCTION_SERVER_URL).commit()) {
                return LegacyRelayMigrationOutcome.NONE
            }
            return if (hasSessionMaterial) {
                LegacyRelayMigrationOutcome.INVALIDATED_SESSION
            } else {
                LegacyRelayMigrationOutcome.ENDPOINT_UPDATED_NO_CREDENTIALS
            }
        }

        private fun decryptTokens(access: String, refresh: String): Pair<String, String>? = try {
            crypto.decrypt(access) to crypto.decrypt(refresh)
        } catch (_: GeneralSecurityException) {
            null
        } catch (_: IllegalArgumentException) {
            null
        }

        private fun migrateLegacyTokens(access: String, refresh: String) {
            try {
                val encryptedAccess = crypto.encrypt(access)
                val encryptedRefresh = crypto.encrypt(refresh)
                prefs.edit()
                    .putString(KEY_ACCESS_TOKEN_ENCRYPTED, encryptedAccess)
                    .putString(KEY_REFRESH_TOKEN_ENCRYPTED, encryptedRefresh)
                    .remove(KEY_ACCESS_TOKEN)
                    .remove(KEY_REFRESH_TOKEN)
                    .commit()
            } catch (_: GeneralSecurityException) {
                // Preserve the readable legacy session so migration can retry next launch.
            }
        }

        private fun SharedPreferences.Editor.putLongOrRemove(
            key: String,
            value: Long?,
        ): SharedPreferences.Editor = if (value != null) putLong(key, value) else remove(key)

        private fun SharedPreferences.getLongOrNull(key: String): Long? =
            if (contains(key)) getLong(key, 0L) else null

        private fun SharedPreferences.Editor.putLinkedIdentity(
            provider: String?,
            providerSub: String?,
        ): SharedPreferences.Editor {
            val normalizedProvider = provider?.trim()?.takeIf { it.isNotEmpty() }
            val normalizedProviderSub = providerSub?.trim()?.takeIf { it.isNotEmpty() }
            return if (normalizedProvider != null && normalizedProviderSub != null) {
                putString(KEY_ACCOUNT_OAUTH_PROVIDER, normalizedProvider)
                    .putString(KEY_ACCOUNT_OAUTH_PROVIDER_SUB, normalizedProviderSub)
            } else {
                remove(KEY_ACCOUNT_OAUTH_PROVIDER)
                    .remove(KEY_ACCOUNT_OAUTH_PROVIDER_SUB)
            }
        }

        private fun clearStale(): RelaySession? {
            clear()
            return null
        }
    }

    internal class AndroidKeystoreTokenCrypto : TokenCrypto {
        override fun encrypt(plaintext: String): String {
            val cipher = Cipher.getInstance(TRANSFORMATION)
            cipher.init(Cipher.ENCRYPT_MODE, getOrCreateKey())
            val ciphertext = cipher.doFinal(plaintext.toByteArray(StandardCharsets.UTF_8))
            return Base64.encodeToString(cipher.iv, Base64.NO_WRAP) + "." +
                Base64.encodeToString(ciphertext, Base64.NO_WRAP)
        }

        override fun decrypt(ciphertext: String): String {
            try {
                val parts = ciphertext.split('.', limit = 2)
                if (parts.size != 2) throw GeneralSecurityException("Malformed encrypted relay token")
                val iv = Base64.decode(parts[0], Base64.NO_WRAP)
                val payload = Base64.decode(parts[1], Base64.NO_WRAP)
                val cipher = Cipher.getInstance(TRANSFORMATION)
                cipher.init(Cipher.DECRYPT_MODE, getOrCreateKey(), GCMParameterSpec(128, iv))
                return String(cipher.doFinal(payload), StandardCharsets.UTF_8)
            } catch (e: IllegalArgumentException) {
                throw GeneralSecurityException("Malformed encrypted relay token", e)
            }
        }

        private fun getOrCreateKey(): SecretKey {
            val keyStore = KeyStore.getInstance(ANDROID_KEYSTORE).apply { load(null) }
            (keyStore.getKey(KEY_ALIAS, null) as? SecretKey)?.let { return it }
            val generator = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, ANDROID_KEYSTORE)
            generator.init(
                KeyGenParameterSpec.Builder(
                    KEY_ALIAS,
                    KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT,
                )
                    .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                    .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                    .setRandomizedEncryptionRequired(true)
                    .build(),
            )
            return generator.generateKey()
        }
    }

    private const val ANDROID_KEYSTORE = "AndroidKeyStore"
    private const val KEY_ALIAS = "maho_relay_session_tokens_v1"
    private const val TRANSFORMATION = "AES/GCM/NoPadding"
    private const val KEY_EMAIL = "email"
    private const val KEY_ACCESS_TOKEN = "access_token"
    private const val KEY_REFRESH_TOKEN = "refresh_token"
    private const val KEY_ACCESS_TOKEN_ENCRYPTED = "access_token_encrypted"
    private const val KEY_REFRESH_TOKEN_ENCRYPTED = "refresh_token_encrypted"
    private const val KEY_EXPIRES_AT = "expires_at"
    private const val KEY_SERVER_URL = "server_url"
    private const val KEY_ACCOUNT_ID = "account_id"
    private const val KEY_ACCOUNT_EMAIL = "account_email"
    private const val KEY_ACCOUNT_DISPLAY_NAME = "account_display_name"
    private const val KEY_ACCOUNT_OAUTH_PROVIDER = "account_oauth_provider"
    private const val KEY_ACCOUNT_OAUTH_PROVIDER_SUB = "account_oauth_provider_sub"
    private const val KEY_DEVICE_ID = "device_id"
    private const val KEY_DEVICE_NAME = "device_name"
    private const val KEY_DEVICE_TYPE = "device_type"
    private const val KEY_SESSION_ID = "session_id"
    private const val KEY_SYNC_ENABLED = "sync_enabled"
    private const val KEY_SYNC_BOOTSTRAP_SEED = "sync_bootstrap_seed"
    private const val RETIRED_SERVER_URL = "http://192.168.0.39:8787"
    private const val PRODUCTION_SERVER_URL = "https://relay.mahobrowser.com"
}
