package dev.maho.browser.sync

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class SyncLifecycleControllerTest {
    @Test
    fun `fresh authenticated sign in starts transport only after bootstrap`() {
        val events = mutableListOf<String>()
        val harness = Harness(events).apply { sessionPresent = true }
        val controller = harness.controller()

        assertTrue(controller.onAuthenticated())

        assertEquals(listOf("bootstrap", "persist:true", "core-toggle", "cycle", "enqueue"), events)
        assertTrue(controller.isRunning)
    }

    @Test
    fun `bootstrap failure leaves fresh sign in disabled and idle`() {
        val events = mutableListOf<String>()
        val harness = Harness(events).apply {
            sessionPresent = true
            bootstrapSucceeds = false
        }
        val controller = harness.controller()

        assertFalse(controller.onAuthenticated())

        assertEquals(listOf("bootstrap"), events)
        assertFalse(controller.isRunning)
        assertFalse(harness.enabled)
    }

    @Test
    fun `enabled sync restores after process restart and re-enables restored core auth`() {
        val events = mutableListOf<String>()
        val harness = Harness(events).apply {
            sessionPresent = true
            enabled = true
        }
        val controller = harness.controller()

        assertTrue(controller.restore())

        assertEquals(listOf("restore-core-auth", "bootstrap", "core-toggle", "cycle", "enqueue"), events)
        assertTrue(controller.isRunning)
        assertTrue(harness.coreSyncEnabled)
    }

    @Test
    fun `fresh authenticated sign in does not emit duplicate core auth restoration`() {
        val events = mutableListOf<String>()
        val harness = Harness(events).apply { sessionPresent = true }
        val controller = harness.controller()

        assertTrue(controller.onAuthenticated())

        assertEquals(listOf("bootstrap", "persist:true", "core-toggle", "cycle", "enqueue"), events)
        assertFalse(events.contains("restore-core-auth"))
        assertTrue(controller.isRunning)
    }

    @Test
    fun `disabled or signed out restart does not schedule transport`() {
        val disabledEvents = mutableListOf<String>()
        val disabled = Harness(disabledEvents).apply { sessionPresent = true }.controller()
        assertFalse(disabled.restore())
        assertTrue(disabledEvents.isEmpty())

        val signedOutEvents = mutableListOf<String>()
        val signedOut = Harness(signedOutEvents).apply { enabled = true }.controller()
        assertFalse(signedOut.restore())
        assertTrue(signedOutEvents.isEmpty())
    }

    @Test
    fun `disabling cancels active transport and queued work`() {
        val events = mutableListOf<String>()
        val harness = Harness(events).apply {
            sessionPresent = true
            enabled = true
        }
        val controller = harness.controller()
        controller.restore()
        events.clear()

        controller.disable()

        assertEquals(listOf("persist:false", "core-toggle", "cancel-transport", "cancel-work"), events)
        assertFalse(controller.isRunning)
        assertFalse(harness.enabled)
    }

    @Test
    fun `logout cancellation is safe even when runtime was not restored`() {
        val events = mutableListOf<String>()
        val harness = Harness(events).apply { enabled = true }
        val controller = harness.controller()

        controller.disable()

        assertEquals(listOf("persist:false", "cancel-transport", "cancel-work"), events)
        assertFalse(harness.enabled)
    }

    @Test
    fun `background and foreground lifecycle only run for restored sync`() {
        val events = mutableListOf<String>()
        val harness = Harness(events).apply {
            sessionPresent = true
            enabled = true
        }
        val controller = harness.controller()
        controller.restore()
        events.clear()

        controller.onBackground()
        controller.onForeground()

        assertEquals(listOf("cancel-transport", "enqueue", "cycle"), events)
    }

    @Test
    fun `restore persisted session passes stored credentials to core sign in exactly once`() {
        val session = RelaySession(
            email = "person@example.com",
            accessToken = "access-token",
            refreshToken = "refresh-token",
            account = RelayAccount(
                id = "usr_123",
                email = "account@example.com",
                displayName = "User",
            ),
            device = RelayDevice(
                id = "dev_456",
                name = "Phone",
                deviceType = "android",
            ),
        )
        val signIns = mutableListOf<RelaySession>()

        restorePersistedCoreSession(
            loadSession = { session },
            signInCore = { signIns += it },
        )

        assertEquals(listOf(session), signIns)
    }

    @Test
    fun `restore persisted session does not invoke core sign in when session is null`() {
        val signIns = mutableListOf<RelaySession>()

        restorePersistedCoreSession(
            loadSession = { null },
            signInCore = { signIns += it },
        )

        assertTrue(signIns.isEmpty())
    }

    private class Harness(private val events: MutableList<String>) {
        var sessionPresent = false
        var enabled = false
        var coreSyncEnabled = false
        var bootstrapSucceeds = true

        fun controller() = SyncLifecycleController(
            hasSession = { sessionPresent },
            isEnabled = { enabled },
            persistEnabled = {
                events += "persist:$it"
                enabled = it
                true
            },
            restoreCoreAuth = {
                events += "restore-core-auth"
                coreSyncEnabled = false
            },
            configureBootstrap = {
                events += "bootstrap"
                bootstrapSucceeds
            },
            toggleCoreSync = {
                events += "core-toggle"
                coreSyncEnabled = !coreSyncEnabled
            },
            runCycle = { events += "cycle" },
            enqueueWork = { events += "enqueue" },
            cancelTransport = { events += "cancel-transport" },
            cancelWork = { events += "cancel-work" },
        )
    }
}
