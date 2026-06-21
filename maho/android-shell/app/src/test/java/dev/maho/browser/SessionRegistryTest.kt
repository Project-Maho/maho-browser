package dev.maho.browser.bridge

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Test
import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicLong

class SessionRegistryTest {

    // -------------------------------------------------------------------------
    // Helpers
    // -------------------------------------------------------------------------

    /** Build a registry whose nativeFree tallies freed pointers rather than calling JNI. */
    private fun makeRegistry(freed: AtomicLong = AtomicLong()): Pair<SessionRegistry, AtomicLong> {
        val freeCount = freed
        val registry = SessionRegistry { ptr ->
            if (ptr != 0L) freeCount.incrementAndGet()
        }
        return Pair(registry, freeCount)
    }

    // -------------------------------------------------------------------------
    // Basic functionality
    // -------------------------------------------------------------------------

    @Test
    fun `register returns non-blank UUID`() {
        val (registry, _) = makeRegistry()
        val id = registry.register(42L)
        assert(id.isNotBlank()) { "Expected non-blank UUID, got: '$id'" }
    }

    @Test
    fun `get returns pointer after register`() {
        val (registry, _) = makeRegistry()
        val id = registry.register(99L)
        assertEquals(99L, registry.get(id))
    }

    @Test
    fun `get returns null for unknown id`() {
        val (registry, _) = makeRegistry()
        assertNull(registry.get("00000000-0000-0000-0000-000000000000"))
    }

    @Test
    fun `release frees the pointer and removes from map`() {
        val (registry, freeCount) = makeRegistry()
        val id = registry.register(1L)
        registry.release(id)
        assertEquals(1L, freeCount.get())
        assertNull(registry.get(id))
    }

    @Test
    fun `release of unknown id is a no-op`() {
        val (registry, freeCount) = makeRegistry()
        registry.release("does-not-exist")
        assertEquals(0L, freeCount.get())
    }

    @Test
    fun `release of already-released id is a no-op`() {
        val (registry, freeCount) = makeRegistry()
        val id = registry.register(7L)
        registry.release(id)
        registry.release(id) // second call — should not double-free
        assertEquals(1L, freeCount.get())
    }

    @Test
    fun `releaseAll frees every registered session`() {
        val (registry, freeCount) = makeRegistry()
        val count = 20
        repeat(count) { registry.register(it.toLong() + 1) }
        registry.releaseAll()
        assertEquals(count.toLong(), freeCount.get())
    }

    // -------------------------------------------------------------------------
    // Concurrency — 10 threads × 100 register+release cycles each
    // -------------------------------------------------------------------------

    @Test
    fun `concurrent register-release does not crash and frees all pointers`() {
        val threadCount = 10
        val opsPerThread = 100
        val freeCount = AtomicLong()
        val (registry, _) = makeRegistry(freeCount)

        val startLatch = CountDownLatch(1)
        val doneLatch = CountDownLatch(threadCount)
        val executor = Executors.newFixedThreadPool(threadCount)

        repeat(threadCount) { thread ->
            executor.submit {
                try {
                    startLatch.await()
                    repeat(opsPerThread) { op ->
                        val ptr = (thread * opsPerThread + op + 1).toLong()
                        val id = registry.register(ptr)
                        registry.release(id)
                    }
                } finally {
                    doneLatch.countDown()
                }
            }
        }

        startLatch.countDown()
        doneLatch.await()
        executor.shutdown()

        val expected = (threadCount * opsPerThread).toLong()
        assertEquals("Expected $expected freed pointers, got ${freeCount.get()}", expected, freeCount.get())
    }

    // -------------------------------------------------------------------------
    // IDs are unique across concurrent registrations
    // -------------------------------------------------------------------------

    @Test
    fun `each registered session gets a distinct id`() {
        val (registry, _) = makeRegistry()
        val ids = (1..100).map { registry.register(it.toLong()) }.toSet()
        assertEquals(100, ids.size)
    }
}
