package dev.maho.browser.sync

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class SyncManagerLogoutTest {
    @Test
    fun `failed durable clear does not complete local sign out`() {
        val events = mutableListOf<String>()

        val signedOut = completeLocalLogout(
            clearSession = {
                events += "clear"
                false
            },
            stopSync = { events += "stop-sync" },
            signOutCore = { events += "sign-out-core" },
        )

        assertFalse(signedOut)
        assertEquals(listOf("clear"), events)
    }

    @Test
    fun `successful durable clear completes local sign out`() {
        val events = mutableListOf<String>()

        val signedOut = completeLocalLogout(
            clearSession = {
                events += "clear"
                true
            },
            stopSync = { events += "stop-sync" },
            signOutCore = { events += "sign-out-core" },
        )

        assertTrue(signedOut)
        assertEquals(listOf("clear", "stop-sync", "sign-out-core"), events)
    }

    @Test
    fun `legacy retired relay migration with invalidated session clears stale core auth and stops sync`() {
        val events = mutableListOf<String>()

        handleLegacyRelayMigration(
            migrationOutcome = LegacyRelayMigrationOutcome.INVALIDATED_SESSION,
            stopSync = { events += "stop-sync" },
            signOutCore = { events += "sign-out-core" },
        )

        assertEquals(listOf("stop-sync", "sign-out-core"), events)
    }

    @Test
    fun `legacy retired relay setting migration without credentials does not sign out core`() {
        val events = mutableListOf<String>()

        handleLegacyRelayMigration(
            migrationOutcome = LegacyRelayMigrationOutcome.ENDPOINT_UPDATED_NO_CREDENTIALS,
            stopSync = { events += "stop-sync" },
            signOutCore = { events += "sign-out-core" },
        )

        assertTrue(events.isEmpty())
    }

    @Test
    fun `no migration needed does not sign out core or stop sync`() {
        val events = mutableListOf<String>()

        handleLegacyRelayMigration(
            migrationOutcome = LegacyRelayMigrationOutcome.NONE,
            stopSync = { events += "stop-sync" },
            signOutCore = { events += "sign-out-core" },
        )

        assertTrue(events.isEmpty())
    }
}
