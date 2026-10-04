package dev.maho.browser

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class MemoryThreadNativeFixtureTest {
    @Test
    fun sessionBecomesUnusableWhenItsNativeOwnerIsFreed() {
        // Given: the boundary fake owns an allocated, usable native session.
        val native = MemoryThreadNative()
        val handle = native.agentCreateSession("fixture-contract")
        assertTrue(native.agentSendMessage(handle, "before-free"))

        // When: the native ownership is released.
        native.agentFreeSession(handle)

        // Then: stale operations cannot succeed and the one allocation has one free.
        val staleResult = runCatching { native.agentSendMessage(handle, "after-free") }
        assertTrue(staleResult.exceptionOrNull() is IllegalStateException)
        assertFalse(staleResult.isSuccess)
        native.assertExactlyFreed(1)
    }

    @Test
    fun duplicateFreeIsRecordedWhenCleanupExceptionsCouldBeSwallowed() {
        // Given: a session was already freed.
        val native = MemoryThreadNative()
        val handle = native.agentCreateSession("fixture-duplicate")
        native.agentFreeSession(handle)

        // When: a second ownership path attempts to free the same allocation.
        native.agentFreeSession(handle)

        // Then: even a production empty catch cannot hide the boundary violation.
        assertEquals(listOf("duplicate_or_wrong_kind_free"), native.failures.toList())
    }
}
