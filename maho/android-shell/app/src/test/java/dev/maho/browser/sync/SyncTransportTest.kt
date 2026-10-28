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
import java.util.concurrent.TimeUnit

/**
 * Models core's outbox lease honestly: a lease removes envelopes from the
 * queue until they are acknowledged or requeued, and a new lease first
 * requeues any outstanding one (as core does).
 */
private class FakeV2BridgeAdapter : SyncBridgeAdapter {
    var roomId: String? = "0123456789abcdef0123456789abcdef"
    var outgoing = mutableListOf<SyncEnvelopeV2>()
    val leased = mutableListOf<SyncEnvelopeV2>()
    var requeueCount = 0
    var cursor = 0L
    val acknowledged = mutableListOf<RelayAckV2>()
    val applied = mutableListOf<SyncEnvelopeV2>()
    val states = mutableListOf<SyncTransportStateReport>()

    override fun getSyncRoomId(): String? = roomId
    override fun leaseOutgoingEnvelopes(limit: Int): List<SyncEnvelopeV2> {
        outgoing.addAll(0, leased)
        leased.clear()
        val batch = outgoing.take(limit)
        outgoing.subList(0, batch.size).clear()
        leased += batch
        return batch
    }
    override fun requeueLeasedEnvelopes(): Boolean {
        requeueCount += 1
        outgoing.addAll(0, leased)
        leased.clear()
        return true
    }
    override fun acknowledge(ack: RelayAckV2): Boolean {
        acknowledged += ack
        return leased.removeAll { it.deliveryId == ack.deliveryId }
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

private class InMemoryRecoveryGate : SyncRecoveryGate {
    var requirement: SyncRecoveryRequirement? = null
    override fun current(): SyncRecoveryRequirement? = requirement
    override fun require(requirement: SyncRecoveryRequirement) {
        this.requirement = requirement
    }
}

class SyncTransportTest {
    private lateinit var server: MockWebServer
    private lateinit var bridge: FakeV2BridgeAdapter
    private lateinit var persistence: InMemoryV2SessionPersistence
    private lateinit var transport: SyncTransport
    private lateinit var recoveryGate: InMemoryRecoveryGate

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
        recoveryGate = InMemoryRecoveryGate()
        transport = SyncTransport(
            bridgeAdapter = bridge,
            authClient = authClient,
            sessionPersistence = persistence,
            recoveryGate = recoveryGate,
        )
    }

    @After
    fun tearDown() {
        transport.cancel()
        server.shutdown()
    }

    @Test
    fun sharedV2FixtureUsesAndroidRelaySchema() {
        // Wire sample of the relay's v2 push/ack. The shared fixture file stays in the
        // private relay repo, so the client keeps its own copy of the two messages it parses.
        val payload = """
            {
              "push": {
                "room_id": "0123456789abcdef0123456789abcdef",
                "messages": [
                  {
                    "protocol_version": 2,
                    "delivery_id": "550e8400-e29b-41d4-a716-446655440000",
                    "payload": "AA==",
                    "relay_seq": null
                  }
                ]
              },
              "ack": {
                "delivery_id": "550e8400-e29b-41d4-a716-446655440000",
                "seq": 5
              }
            }
        """.trimIndent()
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
        bridge.outgoing = mutableListOf(outgoing)
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
    fun one_cycle_pushes_a_large_offline_outbox_in_relay_sized_batches() {
        val ids = (0 until 70).map { "550e8400-e29b-41d4-a716-${"%012d".format(it)}" }
        bridge.outgoing = ids.map {
            SyncEnvelopeV2(protocolVersion = SYNC_PROTOCOL_VERSION, deliveryId = it, payload = "AA==")
        }.toMutableList()
        listOf(ids.take(50), ids.drop(50)).forEachIndexed { batch, batchIds ->
            val acks = batchIds.mapIndexed { i, id -> """{"delivery_id":"$id","seq":${batch * 50 + i + 1}}""" }
            server.enqueue(MockResponse().setResponseCode(200).setBody("""{"acks":[${acks.joinToString(",")}]}"""))
        }
        server.enqueue(MockResponse().setResponseCode(200).setBody("""{"messages":[],"has_more":false}"""))

        val result = transport.runBoundedCycle()

        assertEquals(SyncCycleOutcome.SUCCESS, result.outcome)
        assertEquals(70, bridge.acknowledged.size)
        assertTrue(bridge.outgoing.isEmpty())
        val batchSizes = (0 until 2).map {
            Regex("\"delivery_id\"").findAll(server.takeRequest(5, TimeUnit.SECONDS)!!.body.readUtf8()).count()
        }
        assertEquals(listOf(50, 20), batchSizes)
    }

    @Test
    fun mismatched_ack_set_keeps_outbox_unacknowledged() {
        val outgoing = SyncEnvelopeV2(
            protocolVersion = SYNC_PROTOCOL_VERSION,
            deliveryId = "550e8400-e29b-41d4-a716-446655440000",
            payload = "AA==",
        )
        bridge.outgoing = mutableListOf(outgoing)
        server.enqueue(
            MockResponse().setResponseCode(200).setBody(
                """{"acks":[{"delivery_id":"550e8400-e29b-41d4-a716-446655440099","seq":3}]}""",
            ),
        )

        val result = transport.runBoundedCycle()

        assertEquals(SyncCycleOutcome.PERMANENT_FAILURE, result.outcome)
        assertTrue(bridge.acknowledged.isEmpty())
        assertTrue(bridge.states.last().kind == "error")
        assertEquals(listOf(outgoing), bridge.outgoing)
        assertTrue(bridge.leased.isEmpty())
    }

    @Test
    fun failed_push_requeues_lease_and_next_cycle_repushes_same_delivery_ids() {
        val first = SyncEnvelopeV2(
            protocolVersion = SYNC_PROTOCOL_VERSION,
            deliveryId = "550e8400-e29b-41d4-a716-446655440010",
            payload = "AA==",
        )
        val second = SyncEnvelopeV2(
            protocolVersion = SYNC_PROTOCOL_VERSION,
            deliveryId = "550e8400-e29b-41d4-a716-446655440011",
            payload = "AQ==",
        )
        bridge.outgoing = mutableListOf(first, second)
        server.enqueue(MockResponse().setResponseCode(503).setBody("unavailable"))

        val failed = transport.runBoundedCycle()

        assertEquals(SyncCycleOutcome.TRANSIENT_FAILURE, failed.outcome)
        assertEquals(1, bridge.requeueCount)
        assertEquals(listOf(first, second), bridge.outgoing)
        assertTrue(bridge.leased.isEmpty())
        val failedPush = server.takeRequest(5, TimeUnit.SECONDS)!!.body.readUtf8()

        server.enqueue(
            MockResponse().setResponseCode(200).setBody(
                """{"acks":[{"delivery_id":"${first.deliveryId}","seq":1},{"delivery_id":"${second.deliveryId}","seq":2}]}""",
            ),
        )
        server.enqueue(MockResponse().setResponseCode(200).setBody("""{"messages":[],"has_more":false}"""))

        val retried = transport.runBoundedCycle()

        assertEquals(SyncCycleOutcome.SUCCESS, retried.outcome)
        val retriedPush = server.takeRequest(5, TimeUnit.SECONDS)!!
        assertTrue(retriedPush.path!!.contains("/sync/push"))
        val retriedBody = retriedPush.body.readUtf8()
        listOf(first, second).forEach { envelope ->
            assertTrue(failedPush.contains(envelope.deliveryId))
            assertTrue(retriedBody.contains(envelope.deliveryId))
        }
        assertEquals(
            listOf(RelayAckV2(first.deliveryId, 1), RelayAckV2(second.deliveryId, 2)),
            bridge.acknowledged,
        )
        assertTrue(bridge.outgoing.isEmpty())
        assertTrue(bridge.leased.isEmpty())
    }

    @Test
    fun refreshed_token_reenables_core_sync_only_when_sync_is_running() {
        val session = RelaySession(email = "sync@example.com", accessToken = "new", refreshToken = "r")
        val running = mutableListOf<String>()
        reauthenticateCoreAfterRefresh(
            session = session,
            syncRunning = true,
            signInCore = { running += "sign-in:${it.accessToken}" },
            toggleCoreSync = { running += "toggle" },
        )
        assertEquals(listOf("sign-in:new", "toggle"), running)

        val idle = mutableListOf<String>()
        reauthenticateCoreAfterRefresh(
            session = session,
            syncRunning = false,
            signInCore = { idle += "sign-in:${it.accessToken}" },
            toggleCoreSync = { idle += "toggle" },
        )
        assertEquals(listOf("sign-in:new"), idle)
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

    @Test
    fun room_retired_push_pauses_sync_and_asks_for_recovery_phrase() {
        val outgoing = SyncEnvelopeV2(
            protocolVersion = SYNC_PROTOCOL_VERSION,
            deliveryId = "550e8400-e29b-41d4-a716-446655440000",
            payload = "AA==",
        )
        bridge.outgoing = mutableListOf(outgoing)
        server.enqueue(
            MockResponse().setResponseCode(410).setBody("""{"error":"room_retired","code":"room_retired"}"""),
        )

        val result = transport.runBoundedCycle()

        assertEquals(SyncCycleOutcome.PERMANENT_FAILURE, result.outcome)
        assertEquals(SyncRecoveryRequirement.ROOM_RETIRED, result.recoveryRequirement)
        assertEquals(SyncRecoveryRequirement.ROOM_RETIRED, recoveryGate.requirement)
        assertEquals(SyncRecoveryRequirement.ROOM_RETIRED.message, result.error)
        assertEquals(1, bridge.requeueCount)
        assertEquals(listOf(outgoing), bridge.outgoing)
        assertEquals(1, server.requestCount)

        // Paused: the next cycle never reaches the relay.
        val paused = transport.runBoundedCycle()
        assertEquals(SyncRecoveryRequirement.ROOM_RETIRED, paused.recoveryRequirement)
        assertEquals(1, server.requestCount)
    }

    @Test
    fun room_retired_pull_pauses_sync_and_asks_for_recovery_phrase() {
        server.enqueue(
            MockResponse().setResponseCode(410).setBody("""{"error":"room_retired","code":"room_retired"}"""),
        )

        val result = transport.runBoundedCycle()

        assertEquals(SyncCycleOutcome.PERMANENT_FAILURE, result.outcome)
        assertEquals(SyncRecoveryRequirement.ROOM_RETIRED, recoveryGate.requirement)
        assertTrue(server.takeRequest(5, TimeUnit.SECONDS)!!.path!!.contains("/sync/pull"))
        assertEquals(0L, bridge.cursor)
        assertTrue(bridge.states.last().lastError!!.contains("recovery phrase"))
    }

    @Test
    fun plain_410_without_room_retired_code_is_not_a_recovery_pause() {
        server.enqueue(MockResponse().setResponseCode(410).setBody("Gone"))

        val result = transport.runBoundedCycle()

        assertEquals(SyncCycleOutcome.PERMANENT_FAILURE, result.outcome)
        assertEquals(null, result.recoveryRequirement)
        assertEquals(null, recoveryGate.requirement)
    }
}
