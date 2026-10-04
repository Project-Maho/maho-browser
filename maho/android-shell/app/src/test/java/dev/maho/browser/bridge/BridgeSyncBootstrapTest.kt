package dev.maho.browser.bridge

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
}
