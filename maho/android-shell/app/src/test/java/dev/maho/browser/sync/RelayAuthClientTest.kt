package dev.maho.browser.sync

import okhttp3.OkHttpClient
import okhttp3.mockwebserver.MockResponse
import okhttp3.mockwebserver.MockWebServer
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test

class RelayAuthClientTest {
    private lateinit var server: MockWebServer
    private lateinit var client: RelayAuthClient

    @Before
    fun setUp() {
        server = MockWebServer()
        server.start()
        client = RelayAuthClient(
            OkHttpClient.Builder()
                .addInterceptor { chain ->
                    val request = chain.request()
                    val localUrl = request.url.newBuilder()
                        .scheme("http")
                        .host(server.hostName)
                        .port(server.port)
                        .build()
                    chain.proceed(request.newBuilder().url(localUrl).build())
                }
                .build(),
        )
    }

    @After
    fun tearDown() {
        server.shutdown()
    }

    @Test
    fun `relay auth rejects cleartext before sending credentials`() {
        val error = assertThrows(IllegalArgumentException::class.java) {
            client.logIn(server.url("/").toString(), "user@example.com", "password")
        }

        assertTrue(error.message!!.contains("HTTPS"))
        assertEquals(0, server.requestCount)
    }

    @Test
    fun `sync bootstrap rejects cleartext before sending bearer token`() {
        val error = assertThrows(IllegalArgumentException::class.java) {
            client.getSyncBootstrap(server.url("/").toString(), "access")
        }

        assertTrue(error.message!!.contains("HTTPS"))
        assertEquals(0, server.requestCount)
    }

    @Test
    fun `google response parses optional canonical identity metadata`() {
        server.enqueue(authResponse("\"oauth_provider\":\"google\",\"oauth_provider_sub\":\"subject-123\","))

        val session = client.signInWithGoogle(secureServerUrl(), "id-token", "nonce")

        assertEquals("google", session.account?.oauthProvider)
        assertEquals("subject-123", session.account?.oauthProviderSub)
    }

    @Test
    fun `auth response parses access token expiry`() {
        server.enqueue(authResponse("", expiresAt = 1_700_000_000L))

        val session = client.logIn(secureServerUrl(), "user@example.com", "password")

        assertEquals(1_700_000_000L, session.expiresAt)
    }

    @Test
    fun `password response without identity metadata remains independent`() {
        server.enqueue(authResponse(""))

        val session = client.logIn(secureServerUrl(), "user@example.com", "password")

        assertNull(session.account?.oauthProvider)
        assertNull(session.account?.oauthProviderSub)
    }

    @Test
    fun `malformed whitespace identity metadata is discarded as a pair`() {
        server.enqueue(authResponse("\"oauth_provider\":\" google \",\"oauth_provider_sub\":\"   \","))

        val session = client.signInWithGoogle(secureServerUrl(), "id-token", "nonce")

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

        val bootstrap = client.getSyncBootstrap(secureServerUrl(), "access")

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
            client.getSyncBootstrap(secureServerUrl(), "access")
        }

        assertEquals(404, error.code)
    }

    private fun secureServerUrl(): String = server.url("/").newBuilder().scheme("https").build().toString()

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
