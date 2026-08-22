package dev.maho.browser.sync

import android.content.Context
import android.content.SharedPreferences
import java.security.GeneralSecurityException
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(application = android.app.Application::class)
class RelaySessionStoreTest {
    private val context: Context = RuntimeEnvironment.getApplication()
    private val prefs: SharedPreferences = context.getSharedPreferences("relay-store-test", Context.MODE_PRIVATE)
    private val crypto = FakeTokenCrypto()
    private val store = RelaySessionStore.SessionStore(prefs, crypto)

    @After
    fun cleanUp() {
        prefs.edit().clear().commit()
    }

    @Test
    fun `exact retired relay session is invalidated without retargeting credentials`() {
        prefs.edit()
            .putString("email", "legacy@example.com")
            .putString("access_token", "legacy-access")
            .putString("refresh_token", "legacy-refresh")
            .putString("server_url", "http://192.168.0.39:8787")
            .putString("sync_bootstrap_seed", "legacy-bootstrap-seed")
            .commit()

        val outcome = store.migrateRetiredRelay()
        assertEquals(LegacyRelayMigrationOutcome.INVALIDATED_SESSION, outcome)
        assertNull(store.load())
        assertEquals("https://relay.mahobrowser.com", store.getServerUrl("unused"))
        assertEquals(mapOf("server_url" to "https://relay.mahobrowser.com"), prefs.all)
    }

    @Test
    fun `exact retired relay setting without credentials migrates to production`() {
        prefs.edit()
            .putString("server_url", "http://192.168.0.39:8787")
            .commit()

        val outcome = store.migrateRetiredRelay()
        assertEquals(LegacyRelayMigrationOutcome.ENDPOINT_UPDATED_NO_CREDENTIALS, outcome)
        assertEquals("https://relay.mahobrowser.com", store.getServerUrl("unused"))
        assertEquals(mapOf("server_url" to "https://relay.mahobrowser.com"), prefs.all)
    }

    @Test
    fun `exact retired relay bootstrap seed is invalidated without credentials`() {
        prefs.edit()
            .putString("server_url", "http://192.168.0.39:8787")
            .putString("sync_bootstrap_seed", "legacy-bootstrap-seed")
            .commit()

        val outcome = store.migrateRetiredRelay()

        assertEquals(LegacyRelayMigrationOutcome.INVALIDATED_SESSION, outcome)
        assertEquals(mapOf("server_url" to "https://relay.mahobrowser.com"), prefs.all)
    }

    @Test
    fun `custom relay endpoint and credentials are preserved`() {
        prefs.edit()
            .putString("email", "custom@example.com")
            .putString("access_token", "custom-access")
            .putString("refresh_token", "custom-refresh")
            .putString("server_url", "https://relay.example")
            .commit()

        val outcome = store.migrateRetiredRelay()
        assertEquals(LegacyRelayMigrationOutcome.NONE, outcome)
        val loaded = store.load()

        assertEquals("https://relay.example", loaded?.serverUrl)
        assertEquals("custom-access", loaded?.accessToken)
        assertEquals("https://relay.example", store.getServerUrl("unused"))
    }

    @Test
    fun `save persists encrypted tokens without plaintext copies`() {
        val session = relaySession("access-secret", "refresh-secret")

        assertTrue(store.save(session))

        assertEquals("enc:access-secret", prefs.getString("access_token_encrypted", null))
        assertEquals("enc:refresh-secret", prefs.getString("refresh_token_encrypted", null))
        assertFalse(prefs.contains("access_token"))
        assertFalse(prefs.contains("refresh_token"))
        assertEquals(1_700_000_000L, prefs.getLong("expires_at", 0L))
        assertEquals(session, store.load())
        assertEquals("google", prefs.getString("account_oauth_provider", null))
        assertEquals("google-subject", prefs.getString("account_oauth_provider_sub", null))
    }

    @Test
    fun `legacy plaintext session migrates atomically on load`() {
        prefs.edit()
            .putString("email", "legacy@example.com")
            .putString("access_token", "legacy-access")
            .putString("refresh_token", "legacy-refresh")
            .putString("server_url", "https://relay.example")
            .commit()

        val loaded = store.load()

        assertEquals("legacy-access", loaded?.accessToken)
        assertEquals("legacy-refresh", loaded?.refreshToken)
        assertEquals("enc:legacy-access", prefs.getString("access_token_encrypted", null))
        assertEquals("enc:legacy-refresh", prefs.getString("refresh_token_encrypted", null))
        assertFalse(prefs.contains("access_token"))
        assertFalse(prefs.contains("refresh_token"))
        assertNull(loaded?.account?.oauthProvider)
        assertNull(loaded?.account?.oauthProviderSub)
    }

    @Test
    fun `failed legacy migration preserves plaintext session for retry`() {
        crypto.failEncryption = true
        prefs.edit()
            .putString("email", "legacy@example.com")
            .putString("access_token", "legacy-access")
            .putString("refresh_token", "legacy-refresh")
            .commit()

        val loaded = store.load()

        assertEquals("legacy-access", loaded?.accessToken)
        assertEquals("legacy-access", prefs.getString("access_token", null))
        assertFalse(prefs.contains("access_token_encrypted"))
    }

    @Test
    fun `corrupt encrypted tokens are stale and cleared`() {
        prefs.edit()
            .putString("email", "person@example.com")
            .putString("access_token_encrypted", "corrupt")
            .putString("refresh_token_encrypted", "enc:refresh")
            .commit()

        assertNull(store.load())
        assertTrue(prefs.all.isEmpty())
    }

    @Test
    fun `production format invalid base64 ciphertext is stale and cleared`() {
        val productionStore = RelaySessionStore.SessionStore(
            prefs,
            RelaySessionStore.AndroidKeystoreTokenCrypto(),
        )
        prefs.edit()
            .putString("email", "person@example.com")
            .putString("access_token_encrypted", "%%%not-base64%%%.%%%bad%%%")
            .putString("refresh_token_encrypted", "%%%not-base64%%%.%%%bad%%%")
            .commit()

        assertNull(productionStore.load())
        assertTrue(prefs.all.isEmpty())
    }

    @Test
    fun `partial legacy session is stale and cleared`() {
        prefs.edit()
            .putString("email", "person@example.com")
            .putString("access_token", "access-only")
            .commit()

        assertNull(store.load())
        assertTrue(prefs.all.isEmpty())
    }

    @Test
    fun `malformed whitespace oauth metadata is discarded as a pair`() {
        assertTrue(store.save(relaySession("access", "refresh").copy(
            account = RelayAccount(
                id = "account",
                email = "person@example.com",
                displayName = "Person",
                oauthProvider = " google ",
                oauthProviderSub = "   ",
            ),
        )))

        val loaded = store.load()

        assertNull(loaded?.account?.oauthProvider)
        assertNull(loaded?.account?.oauthProviderSub)
        assertFalse(prefs.contains("account_oauth_provider"))
        assertFalse(prefs.contains("account_oauth_provider_sub"))
    }

    @Test
    fun `sync enabled intent persists independently of runtime state`() {
        assertFalse(store.isSyncEnabled())

        assertTrue(store.setSyncEnabled(true))
        assertTrue(store.isSyncEnabled())

        assertTrue(store.setSyncEnabled(false))
        assertFalse(store.isSyncEnabled())
    }

    @Test
    fun `logout clearing removes oauth metadata repeatedly`() {
        assertTrue(store.save(relaySession("access", "refresh")))

        assertTrue(store.clear())
        assertTrue(store.clear())

        assertNull(store.load())
        assertTrue(prefs.all.isEmpty())
    }

    @Test
    fun `failed clear reports failure and preserves durable session`() {
        assertTrue(store.save(relaySession("access", "refresh")))
        val failingPrefs = CommitFailingPreferences(prefs)
        val failingStore = RelaySessionStore.SessionStore(failingPrefs, crypto)

        assertFalse(failingStore.clear())

        assertEquals("access", store.load()?.accessToken)
        assertEquals("refresh", store.load()?.refreshToken)
    }

    @Test
    fun `interrupted encrypted save preserves prior valid session`() {
        assertTrue(store.save(relaySession("old-access", "old-refresh")))
        prefs.registerOnSharedPreferenceChangeListener { _, _ -> }
        val failingPrefs = CommitFailingPreferences(prefs)
        val failingStore = RelaySessionStore.SessionStore(failingPrefs, crypto)

        assertFalse(failingStore.save(relaySession("new-access", "new-refresh")))

        assertEquals("old-access", store.load()?.accessToken)
        assertEquals("old-refresh", store.load()?.refreshToken)
    }

    @Test
    fun `failed retired relay migration commit reports none and does not invalidate`() {
        prefs.edit()
            .putString("email", "legacy@example.com")
            .putString("access_token", "legacy-access")
            .putString("refresh_token", "legacy-refresh")
            .putString("server_url", "http://192.168.0.39:8787")
            .commit()

        val failingPrefs = CommitFailingPreferences(prefs)
        val failingStore = RelaySessionStore.SessionStore(failingPrefs, crypto)

        val outcome = failingStore.migrateRetiredRelay()
        assertEquals(LegacyRelayMigrationOutcome.NONE, outcome)
        assertEquals("http://192.168.0.39:8787", prefs.getString("server_url", null))
        assertEquals("legacy-access", prefs.getString("access_token", null))
        assertEquals("legacy-refresh", prefs.getString("refresh_token", null))
    }

    @Test
    fun `failed retired relay migration without credentials reports none`() {
        prefs.edit()
            .putString("server_url", "http://192.168.0.39:8787")
            .commit()

        val failingPrefs = CommitFailingPreferences(prefs)
        val failingStore = RelaySessionStore.SessionStore(failingPrefs, crypto)

        val outcome = failingStore.migrateRetiredRelay()
        assertEquals(LegacyRelayMigrationOutcome.NONE, outcome)
        assertEquals("http://192.168.0.39:8787", prefs.getString("server_url", null))
    }

    private fun relaySession(accessToken: String, refreshToken: String) = RelaySession(
        email = "person@example.com",
        accessToken = accessToken,
        refreshToken = refreshToken,
        expiresAt = 1_700_000_000L,
        serverUrl = "https://relay.example",
        account = RelayAccount(
            id = "account",
            email = "person@example.com",
            displayName = "Person",
            oauthProvider = "google",
            oauthProviderSub = "google-subject",
        ),
        device = RelayDevice(id = "device", name = "Pixel", deviceType = "android"),
        session = RelaySessionInfo(id = "session"),
    )

    private class FakeTokenCrypto : RelaySessionStore.TokenCrypto {
        var failEncryption = false

        override fun encrypt(plaintext: String): String {
            if (failEncryption) throw GeneralSecurityException("keystore unavailable")
            return "enc:$plaintext"
        }

        override fun decrypt(ciphertext: String): String {
            if (!ciphertext.startsWith("enc:")) throw GeneralSecurityException("bad ciphertext")
            return ciphertext.removePrefix("enc:")
        }
    }

    private class CommitFailingPreferences(
        private val delegate: SharedPreferences,
    ) : SharedPreferences by delegate {
        override fun edit(): SharedPreferences.Editor = CommitFailingEditor(delegate.edit())
    }

    private class CommitFailingEditor(
        private val delegate: SharedPreferences.Editor,
    ) : SharedPreferences.Editor by delegate {
        override fun putString(key: String?, value: String?): SharedPreferences.Editor {
            delegate.putString(key, value)
            return this
        }

        override fun putLong(key: String?, value: Long): SharedPreferences.Editor {
            delegate.putLong(key, value)
            return this
        }

        override fun remove(key: String?): SharedPreferences.Editor {
            delegate.remove(key)
            return this
        }

        override fun clear(): SharedPreferences.Editor {
            delegate.clear()
            return this
        }

        override fun commit(): Boolean = false
    }
}
