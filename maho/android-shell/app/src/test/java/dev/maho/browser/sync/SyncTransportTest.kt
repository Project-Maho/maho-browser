package dev.maho.browser.sync

import dev.maho.browser.support.MahoJson
import kotlinx.serialization.json.jsonObject
import okhttp3.OkHttpClient
import okhttp3.mockwebserver.MockResponse
import okhttp3.mockwebserver.MockWebServer
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import java.io.File
import java.util.concurrent.TimeUnit

private class FakeV2BridgeAdapter : SyncBridgeAdapter {
    var roomId: String? = "0123456789abcdef0123456789abcdef"
    var outgoing = emptyList<SyncEnvelopeV2>()
    var cursor = 0L
    val acknowledged = mutableListOf<RelayAckV2>()
    val applied = mutableListOf<SyncEnvelopeV2>()
    val states = mutableListOf<SyncTransportStateReport>()

    override fun getSyncRoomId(): String? = roomId
    override fun leaseOutgoingEnvelopes(limit: Int): List<SyncEnvelopeV2> = outgoing.take(limit)
    override fun acknowledge(ack: RelayAckV2): Boolean {
        acknowledged += ack
        return outgoing.any { it.deliveryId == ack.deliveryId }
    }
    override fun getReceiveCursor(roomId: String): Long = cursor
    override fun applyEnvelopeAndAdvanceCursor(roomId: String, envelope: SyncEnvelopeV2): Boolean {
        applied += envelope
        cursor = envelope.relaySeq ?: return false
        return true
    }
    override fun reportTransportState(report: SyncTransportStateReport) {
        states += report
    }
}

private class InMemoryV2SessionPersistence(
    private var session: RelaySession?,
) : RelaySessionPersistence {
    override fun load(): RelaySession? = session
    override fun save(session: RelaySession): Boolean {
        this.session = session
        return true
    }
    override fun getServerUrl(defaultUrl: String): String = session?.serverUrl ?: defaultUrl
}

class SyncTransportTest {
    private lateinit var server: MockWebServer
    private lateinit var bridge: FakeV2BridgeAdapter
    private lateinit var persistence: InMemoryV2SessionPersistence
    private lateinit var transport: SyncTransport

    @Before
    fun setUp() {
        server = MockWebServer()
        server.start()
        bridge = FakeV2BridgeAdapter()
        val authClient = RelayAuthClient(
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
        persistence = InMemoryV2SessionPersistence(
            RelaySession(
                email = "sync@example.com",
                accessToken = "access",
                refreshToken = "refresh",
                serverUrl = server.url("/").newBuilder().scheme("https").build().toString(),
            ),
        )
        transport = SyncTransport(
            bridgeAdapter = bridge,
            authClient = authClient,
            sessionPersistence = persistence,
        )
    }

    @After
    fun tearDown() {
        transport.cancel()
        server.shutdown()
    }

    @Test
    fun sharedV2FixtureUsesAndroidRelaySchema() {
        val fixture = File(System.getProperty("user.dir"), "../../tests/fixtures/sync-v2/protocol.json")
        val payload = fixture.readText()
        val push = MahoJson.instance.decodeFromString<RelaySyncPushRequest>(
            MahoJson.instance.parseToJsonElement(payload)
                .jsonObject["push"]!!
                .toString(),
        )
        val ack = MahoJson.instance.decodeFromString<RelayAckV2>(
            MahoJson.instance.parseToJsonElement(payload)
                .jsonObject["ack"]!!
                .toString(),
        )

        assertEquals("0123456789abcdef0123456789abcdef", push.roomId)
        assertEquals(ack.deliveryId, push.messages.single().deliveryId)
    }

    @Test
    fun bounded_cycle_pushes_v2_envelope_acks_and_pulls_shared_room() {
        val outgoing = SyncEnvelopeV2(
            protocolVersion = SYNC_PROTOCOL_VERSION,
            deliveryId = "550e8400-e29b-41d4-a716-446655440000",
            payload = "AA==",
        )
        bridge.outgoing = listOf(outgoing)
        val inbound = SyncEnvelopeV2(
            protocolVersion = SYNC_PROTOCOL_VERSION,
            deliveryId = "550e8400-e29b-41d4-a716-446655440001",
            payload = "AQ==",
            relaySeq = 4,
        )
        server.enqueue(
            MockResponse().setResponseCode(200).setBody(
                """{"acks":[{"delivery_id":"${outgoing.deliveryId}","seq":3}]}""",
            ),
        )
        server.enqueue(
            MockResponse().setResponseCode(200).setBody(
                """{"messages":[{"protocol_version":2,"delivery_id":"${inbound.deliveryId}","payload":"AQ==","relay_seq":4}],"has_more":false}""",
            ),
        )

        val result = transport.runBoundedCycle()

        assertEquals(SyncCycleOutcome.SUCCESS, result.outcome)
        assertEquals(listOf(RelayAckV2(outgoing.deliveryId, 3)), bridge.acknowledged)
        assertEquals(listOf(inbound), bridge.applied)
        assertEquals(4, bridge.cursor)
        val push = server.takeRequest(5, TimeUnit.SECONDS)!!
        assertTrue(push.path!!.contains("/sync/push"))
        val pushBody = push.body.readUtf8()
        assertTrue(pushBody.contains(bridge.roomId!!))
        assertTrue(pushBody.contains(outgoing.deliveryId))
        val pull = server.takeRequest(5, TimeUnit.SECONDS)!!
        assertTrue(pull.path!!.contains("/sync/pull"))
        assertTrue(pull.path!!.contains("room_id=${bridge.roomId}"))
        assertTrue(pull.path!!.contains("after_seq=0"))
    }

    @Test
    fun mismatched_ack_set_keeps_outbox_unacknowledged() {
        val outgoing = SyncEnvelopeV2(
            protocolVersion = SYNC_PROTOCOL_VERSION,
            deliveryId = "550e8400-e29b-41d4-a716-446655440000",
            payload = "AA==",
        )
        bridge.outgoing = listOf(outgoing)
        server.enqueue(
            MockResponse().setResponseCode(200).setBody(
                """{"acks":[{"delivery_id":"550e8400-e29b-41d4-a716-446655440099","seq":3}]}""",
            ),
        )

        val result = transport.runBoundedCycle()

        assertEquals(SyncCycleOutcome.PERMANENT_FAILURE, result.outcome)
        assertTrue(bridge.acknowledged.isEmpty())
        assertTrue(bridge.states.last().kind == "error")
    }

    @Test
    fun missing_recovery_derived_room_fails_before_network() {
        bridge.roomId = null

        val result = transport.runBoundedCycle()

        assertEquals(SyncCycleOutcome.PERMANENT_FAILURE, result.outcome)
        assertTrue(result.error!!.contains("recovery phrase"))
        assertEquals(0, server.requestCount)
        assertFalse(bridge.states.isEmpty())
    }
}
