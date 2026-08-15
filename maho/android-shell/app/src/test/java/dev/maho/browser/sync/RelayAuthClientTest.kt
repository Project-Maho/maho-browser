package dev.maho.browser.sync

import okhttp3.OkHttpClient
import okhttp3.mockwebserver.MockResponse
import okhttp3.mockwebserver.MockWebServer
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertThrows
import org.junit.Before
import org.junit.Test

class RelayAuthClientTest {
    private lateinit var server: MockWebServer
    private lateinit var client: RelayAuthClient

    @Before
    fun setUp() {
        server = MockWebServer()
        server.start()
        client = RelayAuthClient(OkHttpClient())
    }

    @After
    fun tearDown() {
        server.shutdown()
    }

    @Test
    fun `google response parses optional canonical identity metadata`() {
        server.enqueue(authResponse("\"oauth_provider\":\"google\",\"oauth_provider_sub\":\"subject-123\","))

        val session = client.signInWithGoogle(server.url("/").toString(), "id-token", "nonce")

        assertEquals("google", session.account?.oauthProvider)
        assertEquals("subject-123", session.account?.oauthProviderSub)
    }

    @Test
    fun `auth response parses access token expiry`() {
        server.enqueue(authResponse("", expiresAt = 1_700_000_000L))

        val session = client.logIn(server.url("/").toString(), "user@example.com", "password")

        assertEquals(1_700_000_000L, session.expiresAt)
    }

    @Test
    fun `password response without identity metadata remains independent`() {
        server.enqueue(authResponse(""))

        val session = client.logIn(server.url("/").toString(), "user@example.com", "password")

        assertNull(session.account?.oauthProvider)
        assertNull(session.account?.oauthProviderSub)
    }

    @Test
    fun `malformed whitespace identity metadata is discarded as a pair`() {
        server.enqueue(authResponse("\"oauth_provider\":\" google \",\"oauth_provider_sub\":\"   \","))

        val session = client.signInWithGoogle(server.url("/").toString(), "id-token", "nonce")

        assertNull(session.account?.oauthProvider)
        assertNull(session.account?.oauthProviderSub)
    }

    @Test
    fun `sync bootstrap is retrieved with the authenticated account token`() {
        server.enqueue(
            MockResponse()
                .setResponseCode(200)
                .setHeader("Content-Type", "application/json")
                .setBody(
                    """
                    {
                      "version": 1,
                      "room_id": "0123456789abcdef0123456789abcdef",
                      "seed": "WlpaWlpaWlpaWlpaWlpaWlpaWlpaWlpaWlpaWlpaWlo=",
                      "created_at": 42
                    }
                    """.trimIndent(),
                ),
        )

        val bootstrap = client.getSyncBootstrap(server.url("/").toString(), "access")

        assertEquals("0123456789abcdef0123456789abcdef", bootstrap.roomId)
        val request = server.takeRequest()
        assertEquals("GET", request.method)
        assertEquals("/sync/bootstrap", request.path)
        assertEquals("Bearer access", request.getHeader("Authorization"))
    }

    @Test
    fun `missing sync bootstrap never becomes a usable seed`() {
        server.enqueue(MockResponse().setResponseCode(404).setBody("""{"error":"not_found"}"""))

        val error = assertThrows(RelayApiException::class.java) {
            client.getSyncBootstrap(server.url("/").toString(), "access")
        }

        assertEquals(404, error.code)
    }

    private fun authResponse(identityFields: String, expiresAt: Long? = null) = MockResponse()
        .setResponseCode(200)
        .setHeader("Content-Type", "application/json")
        .setBody(
            """
            {
              "access_token":"access",
              "refresh_token":"refresh",
              ${expiresAt?.let { "\"expires_at\":$it," }.orEmpty()}
              "account":{
                "id":42,
                "email":"user@example.com",
                ${identityFields}
                "display_name":"User"
              }
            }
            """.trimIndent(),
        )
}
