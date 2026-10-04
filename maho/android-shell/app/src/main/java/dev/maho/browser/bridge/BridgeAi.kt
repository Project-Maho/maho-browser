package dev.maho.browser.bridge

import dev.maho.browser.MahoBridge
import dev.maho.browser.sync.RelaySessionStore
import dev.maho.browser.ui.webview.ManagedChatConfig
import org.json.JSONObject

object BridgeAi {
    private const val BYOK_PREFS = "maho_byok_keys"
    private const val DB_KEYS_PREFS = "maho_db_keys"
    private const val SQLCIPHER_KEY_ENTRY = "sqlcipher_key"
    private const val AI_SETTINGS_PREFS = "maho_ai_settings"
    private const val AI_PROVIDER_KEY = "provider"
    private const val AI_BASE_URL_KEY = "base_url"
    private const val AI_API_KEY = "api_key"
    private const val AI_MODEL_KEY = "model"

    // Mirrors the authoritative maho-agent managed defaults for Android's native bridge.
    private const val MANAGED_PROXY_BASE_URL = "https://proxy.maho.co/v1"
    private const val MANAGED_DEFAULT_MODEL = "google/gemini-3-flash-lite:free"

    private object SecureStorageHelper {
        private const val KEY_PROVIDER = "AndroidKeyStore"
        private const val KEY_ALIAS = "MahoBYOKSecureKey"
        private const val TRANSFORMATION = "AES/GCM/NoPadding"
        private const val TAG_LENGTH_BITS = 128

        private fun getSecretKey(): javax.crypto.SecretKey {
            val keyStore = java.security.KeyStore.getInstance(KEY_PROVIDER)
            keyStore.load(null)
            val key = keyStore.getKey(KEY_ALIAS, null) as? javax.crypto.SecretKey
            if (key != null) return key

            val keyGenerator = javax.crypto.KeyGenerator.getInstance(
                android.security.keystore.KeyProperties.KEY_ALGORITHM_AES,
                KEY_PROVIDER,
            )
            val keySpec = android.security.keystore.KeyGenParameterSpec.Builder(
                KEY_ALIAS,
                android.security.keystore.KeyProperties.PURPOSE_ENCRYPT or
                    android.security.keystore.KeyProperties.PURPOSE_DECRYPT,
            )
                .setBlockModes(android.security.keystore.KeyProperties.BLOCK_MODE_GCM)
                .setEncryptionPaddings(android.security.keystore.KeyProperties.ENCRYPTION_PADDING_NONE)
                .setKeySize(256)
                .build()
            keyGenerator.init(keySpec)
            return keyGenerator.generateKey()
        }

        fun encrypt(plaintext: String): String {
            val cipher = javax.crypto.Cipher.getInstance(TRANSFORMATION)
            cipher.init(javax.crypto.Cipher.ENCRYPT_MODE, getSecretKey())
            val iv = cipher.iv
            val ciphertext = cipher.doFinal(plaintext.toByteArray(Charsets.UTF_8))
            val combined = ByteArray(1 + iv.size + ciphertext.size)
            combined[0] = iv.size.toByte()
            System.arraycopy(iv, 0, combined, 1, iv.size)
            System.arraycopy(ciphertext, 0, combined, 1 + iv.size, ciphertext.size)
            return android.util.Base64.encodeToString(combined, android.util.Base64.NO_WRAP)
        }

        fun decrypt(encryptedBase64: String): String? {
            return try {
                val combined = android.util.Base64.decode(encryptedBase64, android.util.Base64.NO_WRAP)
                if (combined.isEmpty()) return null
                val ivSize = combined[0].toInt()
                if (ivSize <= 0 || ivSize > combined.size - 1) return null
                val iv = ByteArray(ivSize)
                System.arraycopy(combined, 1, iv, 0, ivSize)
                val ciphertext = ByteArray(combined.size - 1 - ivSize)
                System.arraycopy(combined, 1 + ivSize, ciphertext, 0, ciphertext.size)
                val cipher = javax.crypto.Cipher.getInstance(TRANSFORMATION)
                cipher.init(
                    javax.crypto.Cipher.DECRYPT_MODE,
                    getSecretKey(),
                    javax.crypto.spec.GCMParameterSpec(TAG_LENGTH_BITS, iv),
                )
                String(cipher.doFinal(ciphertext), Charsets.UTF_8)
            } catch (e: Exception) {
                e.printStackTrace()
                null
            }
        }
    }

    fun byokGetProviders(): String? = "[\"openai\", \"anthropic\"]"

    fun byokGetKey(provider: String): String? {
        val context = MahoBridge.appContext ?: return null
        val encrypted = context.getSharedPreferences(BYOK_PREFS, android.content.Context.MODE_PRIVATE)
            .getString(provider, null) ?: return null
        return SecureStorageHelper.decrypt(encrypted)
    }

    fun byokSetKey(provider: String, key: String): Boolean {
        val context = MahoBridge.appContext ?: return false
        val encrypted = SecureStorageHelper.encrypt(key)
        return context.getSharedPreferences(BYOK_PREFS, android.content.Context.MODE_PRIVATE)
            .edit().putString(provider, encrypted).commit()
    }

    fun byokDeleteKey(provider: String): Boolean {
        val context = MahoBridge.appContext ?: return false
        return context.getSharedPreferences(BYOK_PREFS, android.content.Context.MODE_PRIVATE)
            .edit().remove(provider).commit()
    }

    fun byokValidateKey(provider: String, key: String): Boolean = true

    fun getAiSettings(): String {
        val context = MahoBridge.appContext
        val prefs = context?.getSharedPreferences(AI_SETTINGS_PREFS, android.content.Context.MODE_PRIVATE)
        return JSONObject()
            .put("provider", prefs?.getString(AI_PROVIDER_KEY, "").orEmpty())
            .put("baseUrl", prefs?.getString(AI_BASE_URL_KEY, "").orEmpty())
            .put("model", prefs?.getString(AI_MODEL_KEY, "").orEmpty())
            .put("hasApiKey", getCustomAiApiKey()?.isNotEmpty() == true)
            .put("hasByokOpenai", byokGetKey("openai")?.isNotEmpty() == true)
            .put("hasByokAnthropic", byokGetKey("anthropic")?.isNotEmpty() == true)
            .toString()
    }

    fun setAiProvider(provider: String): Boolean = putAiSetting(AI_PROVIDER_KEY, provider)
    fun setAiBaseUrl(url: String): Boolean = putAiSetting(AI_BASE_URL_KEY, url)

    fun setAiApiKey(key: String): Boolean {
        val context = MahoBridge.appContext ?: return false
        val editor = context.getSharedPreferences(AI_SETTINGS_PREFS, android.content.Context.MODE_PRIVATE).edit()
        if (key.isEmpty()) editor.remove(AI_API_KEY) else editor.putString(AI_API_KEY, SecureStorageHelper.encrypt(key))
        return editor.commit()
    }

    fun setAiModel(model: String): Boolean = putAiSetting(AI_MODEL_KEY, model)

    private fun putAiSetting(name: String, value: String): Boolean {
        val context = MahoBridge.appContext ?: return false
        return context.getSharedPreferences(AI_SETTINGS_PREFS, android.content.Context.MODE_PRIVATE)
            .edit().putString(name, value).commit()
    }

    private fun getCustomAiApiKey(): String? {
        val context = MahoBridge.appContext ?: return null
        val encrypted = context.getSharedPreferences(AI_SETTINGS_PREFS, android.content.Context.MODE_PRIVATE)
            .getString(AI_API_KEY, null) ?: return null
        return SecureStorageHelper.decrypt(encrypted)
    }

    fun resolveChatCredential(provider: String): String? = when (provider) {
        "openai-compatible" -> getCustomAiApiKey()?.takeIf { it.isNotEmpty() }
        else -> null
    }

    fun resolveManagedChatConfig(): ManagedChatConfig? {
        val session = RelaySessionStore.load() ?: return null
        if (session.expiresAt?.let { it <= System.currentTimeMillis() / 1000 } == true) return null
        val accessToken = session.accessToken.takeIf { it.isNotBlank() } ?: return null
        return ManagedChatConfig(
            apiKey = accessToken,
            endpoint = "$MANAGED_PROXY_BASE_URL/chat/completions",
            model = MANAGED_DEFAULT_MODEL,
        )
    }

    internal fun getOrCreateSqlcipherKeyHex(): String? {
        val context = MahoBridge.appContext ?: return null
        val prefs = context.getSharedPreferences(DB_KEYS_PREFS, android.content.Context.MODE_PRIVATE)
        val stored = prefs.getString(SQLCIPHER_KEY_ENTRY, null)
        if (stored != null) {
            val decrypted = SecureStorageHelper.decrypt(stored)
            if (decrypted != null) return decrypted
            android.util.Log.e(
                "MahoBridge",
                "SQLCipher key blob present but decryption failed; refusing to regenerate to avoid bricking an encrypted DB",
            )
            return null
        }
        val bytes = ByteArray(32)
        java.security.SecureRandom().nextBytes(bytes)
        val hex = bytes.joinToString("") { "%02x".format(it) }
        prefs.edit().putString(SQLCIPHER_KEY_ENTRY, SecureStorageHelper.encrypt(hex)).commit()
        return hex
    }

    internal fun getAgentSecureStorage(provider: String): String? {
        val context = MahoBridge.appContext ?: return null
        val prefs = context.getSharedPreferences(AI_SETTINGS_PREFS, android.content.Context.MODE_PRIVATE)
        val storedProvider = prefs.getString(AI_PROVIDER_KEY, "").orEmpty()
        val customKey = getCustomAiApiKey()
        if (!customKey.isNullOrEmpty() && (storedProvider == provider || storedProvider == "$provider-compatible")) {
            return JSONObject()
                .put("key", customKey)
                .put("base_url", prefs.getString(AI_BASE_URL_KEY, "").orEmpty().takeIf { it.isNotEmpty() } ?: JSONObject.NULL)
                .put("model", prefs.getString(AI_MODEL_KEY, "").orEmpty().takeIf { it.isNotEmpty() } ?: JSONObject.NULL)
                .toString()
        }
        val byokKey = when (provider) {
            "openai" -> byokGetKey("openai")
            "anthropic" -> byokGetKey("anthropic")
            else -> null
        }?.takeIf { it.isNotEmpty() } ?: return null
        return JSONObject()
            .put("key", byokKey)
            .put("base_url", JSONObject.NULL)
            .put("model", JSONObject.NULL)
            .toString()
    }
}
