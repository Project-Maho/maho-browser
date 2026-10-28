package dev.maho.browser.bridge

import dev.maho.browser.support.MahoJson
import dev.maho.browser.sync.AccountBootstrapDecision
import dev.maho.browser.sync.RelayApiException
import dev.maho.browser.sync.RelaySyncBootstrap
import dev.maho.browser.sync.SyncRecoveryRequirement
import dev.maho.browser.sync.RecoveryGateAction
import dev.maho.browser.sync.decideAccountBootstrap
import dev.maho.browser.sync.recoveryGateAction
import dev.maho.browser.sync.recoveryPauseAfterBootstrap
import dev.maho.browser.sync.isRoomRetiredResponse
import org.junit.Assert.assertFalse
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class BridgeSyncBootstrapTest {
    @Test
    fun `bootstrap result requires an explicit successful core response`() {
        assertTrue(BridgeSync.bootstrapConfigured("""{"success":true,"roomId":"room"}"""))
        assertFalse(BridgeSync.bootstrapConfigured("""{"success":false,"error":"invalid"}"""))
        assertFalse(BridgeSync.bootstrapConfigured(null))
        assertFalse(BridgeSync.bootstrapConfigured("not-json"))
    }

    @Test
    fun `generated bootstrap accepts only complete versioned seed material`() {
        val bootstrap = BridgeSync.parseGeneratedSyncBootstrap(
            """{"version":1,"roomId":"0123456789abcdef0123456789abcdef","seed":"Wlpa"}""",
        )

        assertEquals(1, bootstrap?.version)
        assertEquals("0123456789abcdef0123456789abcdef", bootstrap?.roomId)
        assertEquals("Wlpa", bootstrap?.seed)
        assertNull(BridgeSync.parseGeneratedSyncBootstrap("""{"version":1,"roomId":"room"}"""))
    }

    private val clientHeldRoom = "fedcba9876543210fedcba9876543210"

    private fun clientHeldBootstrap(): RelaySyncBootstrap =
        // Exact relay shape: a client-held GET /sync/bootstrap carries no seed.
        MahoJson.instance.decodeFromString(
            RelaySyncBootstrap.serializer(),
            """{"key_mode":"client_held","room_id":"$clientHeldRoom"}""",
        )

    @Test
    fun `client held bootstrap decodes without a seed`() {
        val bootstrap = clientHeldBootstrap()
        assertTrue(bootstrap.isClientHeld)
        assertNull(bootstrap.seed)
        assertNull(bootstrap.version)
        assertEquals(clientHeldRoom, bootstrap.roomId)

        val explicitNull = MahoJson.instance.decodeFromString(
            RelaySyncBootstrap.serializer(),
            """{"key_mode":"client_held","room_id":"abc","seed":null,"seed_b64":null}""",
        )
        assertTrue(explicitNull.isClientHeld)
        assertNull(explicitNull.seed)
    }

    @Test
    fun `escrow bootstrap still decodes its seed`() {
        val bootstrap = MahoJson.instance.decodeFromString(
            RelaySyncBootstrap.serializer(),
            """{"version":1,"room_id":"0123456789abcdef0123456789abcdef","seed":"Wlpa","created_at":42}""",
        )
        assertFalse(bootstrap.isClientHeld)
        assertEquals(
            AccountBootstrapDecision.ConfigureSeed("Wlpa"),
            decideAccountBootstrap(bootstrap, joinedRoomId = null, awaitingJoinVerification = false),
        )
    }

    @Test
    fun `client held bootstrap on a new device asks for the recovery phrase instead of a new seed`() {
        val decision = decideAccountBootstrap(
            clientHeldBootstrap(),
            joinedRoomId = null,
            awaitingJoinVerification = false,
        )
        assertEquals(
            AccountBootstrapDecision.RecoveryPhraseRequired(SyncRecoveryRequirement.CLIENT_HELD_BOOTSTRAP),
            decision,
        )
        assertTrue(SyncRecoveryRequirement.CLIENT_HELD_BOOTSTRAP.message.contains("24-word recovery phrase"))
    }

    @Test
    fun `client held bootstrap syncs once the phrase derived the account room`() {
        assertEquals(
            AccountBootstrapDecision.JoinedClientHeldRoom,
            decideAccountBootstrap(clientHeldBootstrap(), clientHeldRoom, awaitingJoinVerification = true),
        )
    }

    @Test
    fun `wrong recovery phrase reports a mismatch and never configures a seed`() {
        assertEquals(
            AccountBootstrapDecision.RecoveryPhraseRequired(SyncRecoveryRequirement.RECOVERY_PHRASE_MISMATCH),
            decideAccountBootstrap(
                clientHeldBootstrap(),
                joinedRoomId = "00000000000000000000000000000000",
                awaitingJoinVerification = true,
            ),
        )
    }

    @Test
    fun `escrow bootstrap without a seed is rejected`() {
        val bootstrap = RelaySyncBootstrap(roomId = "0123456789abcdef0123456789abcdef")
        assertEquals(
            AccountBootstrapDecision.Invalid,
            decideAccountBootstrap(bootstrap, joinedRoomId = null, awaitingJoinVerification = false),
        )
    }

    @Test
    fun `410 room_retired relay body is recognised`() {
        val body = """{"error":"room_retired","code":"room_retired"}"""
        assertTrue(isRoomRetiredResponse(410, body))
        assertTrue(RelayApiException(410, body).isRoomRetired)
        assertFalse(isRoomRetiredResponse(409, body))
        assertFalse(isRoomRetiredResponse(410, """{"code":"gone"}"""))
        assertFalse(isRoomRetiredResponse(410, "Gone"))
        assertFalse(RelayApiException(503, "unavailable").isRoomRetired)
        assertEquals(
            "This account now uses your own keys - enter your recovery phrase to continue.",
            SyncRecoveryRequirement.ROOM_RETIRED.message,
        )
    }

    @Test
    fun `phrase join keeps the pause until the bootstrap confirms the client held room`() {
        // verify-t20 race: the pause must survive recoveryPhraseJoined() so a
        // worker / foreground cycle cannot push to an unverified room.
        var pause: SyncRecoveryRequirement? = SyncRecoveryRequirement.ROOM_RETIRED
        val awaiting = true // recoveryPhraseJoined() only flags verification

        assertEquals(RecoveryGateAction.VERIFY_JOIN, recoveryGateAction(pause, awaiting))
        assertEquals(SyncRecoveryRequirement.ROOM_RETIRED, pause) // still paused while the GET is in flight

        val decision = decideAccountBootstrap(clientHeldBootstrap(), clientHeldRoom, awaiting)
        pause = recoveryPauseAfterBootstrap(pause, decision)

        assertEquals(AccountBootstrapDecision.JoinedClientHeldRoom, decision)
        assertNull(pause)
        assertEquals(RecoveryGateAction.PROCEED, recoveryGateAction(pause, awaitingJoinVerification = false))
    }

    @Test
    fun `wrong phrase join keeps sync paused with a mismatch`() {
        val pause = SyncRecoveryRequirement.ROOM_RETIRED
        val decision = decideAccountBootstrap(
            clientHeldBootstrap(),
            joinedRoomId = "00000000000000000000000000000000",
            awaitingJoinVerification = true,
        )

        assertEquals(
            SyncRecoveryRequirement.RECOVERY_PHRASE_MISMATCH,
            recoveryPauseAfterBootstrap(pause, decision),
        )
    }

    @Test
    fun `pause is never lifted by anything but a verified client held room`() {
        val pause = SyncRecoveryRequirement.CLIENT_HELD_BOOTSTRAP
        assertEquals(pause, recoveryPauseAfterBootstrap(pause, AccountBootstrapDecision.ConfigureSeed("Wlpa")))
        assertEquals(pause, recoveryPauseAfterBootstrap(pause, AccountBootstrapDecision.Invalid))
        assertEquals(RecoveryGateAction.BLOCKED, recoveryGateAction(pause, awaitingJoinVerification = false))
        assertEquals(RecoveryGateAction.PROCEED, recoveryGateAction(null, awaitingJoinVerification = false))
    }
}
