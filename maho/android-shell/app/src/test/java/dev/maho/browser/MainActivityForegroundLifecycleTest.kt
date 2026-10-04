package dev.maho.browser

import org.junit.Assert.assertEquals
import org.junit.Test

class MainActivityForegroundLifecycleTest {
    @Test
    fun `two resumes sweep idempotently and check for updates afterward`() {
        val calls = mutableListOf<String>()
        var activeConversation = true

        repeat(2) {
            runMainActivityForegroundResume(
                sweepConversations = {
                    calls += "sweep"
                    if (activeConversation) {
                        activeConversation = false
                        1
                    } else {
                        0
                    }
                },
                checkForUpdate = { calls += "update" },
            )
        }

        assertEquals(
            listOf("sweep", "update", "sweep", "update"),
            calls,
        )
        assertEquals(false, activeConversation)
    }
}
