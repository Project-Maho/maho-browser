package dev.maho.browser.sync

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The relay bootstrap (`GET /sync/bootstrap`) must be fetched only when no usable
 * seed is stored on the device; sync restarts after a token refresh reuse the seed.
 */
class SyncSeedBootstrapTest {
    private class Harness(var stored: String? = null, var coreAccepts: Boolean = true) {
        var bootstrapFetches = 0
        val applied = mutableListOf<String>()

        fun configure(): Boolean = configureSyncFromStoredSeed(
            storedSeed = { stored },
            applySeed = { seed ->
                applied += seed
                coreAccepts
            },
            clearSeed = { stored = null },
            fetchAndConfigure = {
                bootstrapFetches += 1
                applied += "relay-seed"
                stored = "relay-seed"
                coreAccepts = true
                true
            },
        )
    }

    @Test
    fun `second sync start after a first fetch reuses the stored seed with zero further fetches`() {
        val harness = Harness()
        assertTrue(harness.configure())
        assertEquals(1, harness.bootstrapFetches)

        assertTrue(harness.configure())

        assertEquals(1, harness.bootstrapFetches)
        assertEquals(listOf("relay-seed", "relay-seed"), harness.applied)
    }

    @Test
    fun `stored seed present means bootstrap call count is zero`() {
        val harness = Harness(stored = "device-seed")

        assertTrue(harness.configure())

        assertEquals(0, harness.bootstrapFetches)
        assertEquals(listOf("device-seed"), harness.applied)
    }

    @Test
    fun `missing stored seed fetches the bootstrap exactly once`() {
        val harness = Harness(stored = null)

        assertTrue(harness.configure())

        assertEquals(1, harness.bootstrapFetches)
        assertEquals("relay-seed", harness.stored)
    }

    @Test
    fun `seed rejected by core is cleared and the bootstrap fetched once`() {
        val harness = Harness(stored = "stale-seed", coreAccepts = false)

        assertTrue(harness.configure())

        assertEquals(1, harness.bootstrapFetches)
        assertEquals(listOf("stale-seed", "relay-seed"), harness.applied)
    }

    @Test
    fun `failed fetch without stored seed reports failure`() {
        var fetches = 0
        val configured = configureSyncFromStoredSeed(
            storedSeed = { null },
            applySeed = { true },
            clearSeed = {},
            fetchAndConfigure = {
                fetches += 1
                false
            },
        )

        assertFalse(configured)
        assertEquals(1, fetches)
    }

    @Test
    fun `sign out during an in-flight fetch discards the fetched seed`() {
        val guard = SyncSeedPersistGuard()
        var stored: String? = "device-seed"
        val token = guard.begin()

        // Sign-out lands while the relay fetch is still in flight.
        assertTrue(guard.invalidate {
            stored = null
            true
        })

        assertFalse(guard.isCurrent(token))
        assertFalse(guard.persistIfCurrent(token) {
            stored = "relay-seed"
            true
        })
        assertEquals(null, stored)

        val next = guard.begin()
        assertTrue(guard.persistIfCurrent(next) {
            stored = "relay-seed"
            true
        })
        assertEquals("relay-seed", stored)
    }
}
