package dev.maho.browser.sync

import android.os.Bundle
import com.google.android.libraries.identity.googleid.GoogleIdTokenCredential
import java.util.concurrent.Executors
import kotlinx.coroutines.asCoroutineDispatcher
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(application = android.app.Application::class)
class GoogleSignInFlowTest {

    @Test
    fun `credential parser rejects an unrelated custom credential`() {
        val result = GoogleCredentialParser.parse("not-google", Bundle())

        assertTrue(result is GoogleCredentialResult.Failure)
        assertTrue((result as GoogleCredentialResult.Failure).message.contains("Unsupported"))
    }

    @Test
    fun `credential parser rejects malformed Google credential data`() {
        val result = GoogleCredentialParser.parse(
            GoogleIdTokenCredential.TYPE_GOOGLE_ID_TOKEN_CREDENTIAL,
            Bundle(),
        )

        assertTrue(result is GoogleCredentialResult.Failure)
        assertTrue((result as GoogleCredentialResult.Failure).message.contains("Malformed"))
    }

    @Test
    fun `cancelled credential sheet preserves the current relay session`() = runBlocking {
        val existing = relaySession("old-access", "old-refresh")
        var current = existing
        var exchangeCalled = false
        val coordinator = GoogleSignInCoordinator(
            credentials = GoogleCredentialSource { _, _ -> GoogleCredentialResult.Cancelled },
            exchange = GoogleRelayExchange { _, _, _, _ ->
                exchangeCalled = true
                relaySession("new-access", "new-refresh")
            },
            commit = GoogleSessionCommit { current = it },
            nonceFactory = { "fixed-nonce" },
        )

        val outcome = coordinator.signIn("https://relay.example", "web-client-id")

        assertSame(GoogleSignInOutcome.Cancelled, outcome)
        assertSame(existing, current)
        assertFalse(exchangeCalled)
    }

    @Test
    fun `malformed credential preserves the current relay session`() = runBlocking {
        val existing = relaySession("old-access", "old-refresh")
        var current = existing
        val coordinator = GoogleSignInCoordinator(
            credentials = GoogleCredentialSource { _, _ -> GoogleCredentialResult.Failure("Malformed Google credential") },
            exchange = GoogleRelayExchange { _, _, _, _ -> relaySession("new-access", "new-refresh") },
            commit = GoogleSessionCommit { current = it },
            nonceFactory = { "fixed-nonce" },
        )

        val outcome = coordinator.signIn("https://relay.example", "web-client-id")

        assertEquals(GoogleSignInOutcome.Failure("Malformed Google credential"), outcome)
        assertSame(existing, current)
    }

    @Test
    fun `relay interruption does not replace the current session`() = runBlocking {
        val existing = relaySession("old-access", "old-refresh")
        var current = existing
        val coordinator = GoogleSignInCoordinator(
            credentials = GoogleCredentialSource { _, nonce ->
                assertEquals("fixed-nonce", nonce)
                GoogleCredentialResult.IdToken("google-id-token", "person@example.com")
            },
            exchange = GoogleRelayExchange { _, _, _, _ -> error("network interrupted") },
            commit = GoogleSessionCommit { current = it },
            nonceFactory = { "fixed-nonce" },
        )

        val outcome = coordinator.signIn("https://relay.example", "web-client-id")

        assertEquals(GoogleSignInOutcome.Failure("network interrupted"), outcome)
        assertSame(existing, current)
    }

    @Test
    fun `successful credential uses one nonce for provider and relay then commits`() = runBlocking {
        val expected = relaySession("new-access", "new-refresh")
        var committed: RelaySession? = null
        val coordinator = GoogleSignInCoordinator(
            credentials = GoogleCredentialSource { clientId, nonce ->
                assertEquals("web-client-id", clientId)
                assertEquals("deterministic-nonce", nonce)
                GoogleCredentialResult.IdToken("google-id-token", "person@example.com")
            },
            exchange = GoogleRelayExchange { serverUrl, idToken, nonce, email ->
                assertEquals("https://relay.example", serverUrl)
                assertEquals("google-id-token", idToken)
                assertEquals("deterministic-nonce", nonce)
                assertEquals("person@example.com", email)
                expected
            },
            commit = GoogleSessionCommit { committed = it },
            nonceFactory = { "deterministic-nonce" },
        )

        val outcome = coordinator.signIn("https://relay.example", "web-client-id")

        assertEquals(GoogleSignInOutcome.SignedIn(expected), outcome)
        assertSame(expected, committed)
    }

    @Test
    fun `credential manager stays on caller while relay exchange runs on IO dispatcher`() {
        val relayExecutor = Executors.newSingleThreadExecutor { runnable -> Thread(runnable, "relay-io") }
        val relayDispatcher = relayExecutor.asCoroutineDispatcher()
        try {
            var callerThreadId: Long? = null
            var credentialThreadId: Long? = null
            var exchangeThread: String? = null
            val coordinator = GoogleSignInCoordinator(
                credentials = GoogleCredentialSource { _, _ ->
                    credentialThreadId = Thread.currentThread().id
                    GoogleCredentialResult.IdToken("google-id-token", "person@example.com")
                },
                exchange = GoogleRelayExchange { _, _, _, _ ->
                    exchangeThread = Thread.currentThread().name
                    relaySession("new-access", "new-refresh")
                },
                commit = GoogleSessionCommit {},
                exchangeDispatcher = relayDispatcher,
            )

            runBlocking {
                callerThreadId = Thread.currentThread().id
                coordinator.signIn("https://relay.example", "web-client-id")
            }

            assertEquals(callerThreadId, credentialThreadId)
            assertTrue(exchangeThread!!.startsWith("relay-io"))
        } finally {
            relayDispatcher.close()
            relayExecutor.shutdownNow()
        }
    }

    @Test
    fun `missing client id fails before opening credential sheet`() = runBlocking {
        var credentialCalled = false
        val coordinator = GoogleSignInCoordinator(
            credentials = GoogleCredentialSource { _, _ ->
                credentialCalled = true
                GoogleCredentialResult.Cancelled
            },
            exchange = GoogleRelayExchange { _, _, _, _ -> relaySession("a", "r") },
            commit = GoogleSessionCommit {},
        )

        val outcome = coordinator.signIn("https://relay.example", "  ")

        assertTrue(outcome is GoogleSignInOutcome.Failure)
        assertFalse(credentialCalled)
    }

    private fun relaySession(accessToken: String, refreshToken: String) = RelaySession(
        email = "person@example.com",
        accessToken = accessToken,
        refreshToken = refreshToken,
        serverUrl = "https://relay.example",
    )
}
