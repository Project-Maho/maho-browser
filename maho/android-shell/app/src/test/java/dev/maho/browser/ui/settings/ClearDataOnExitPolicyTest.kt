package dev.maho.browser.ui.settings

import org.junit.Assert.assertEquals
import org.junit.Test

class ClearDataOnExitPolicyTest {
    @Test
    fun `clears only when finishing for real with the setting on`() {
        val cases = listOf(
            Triple(true, false, true) to true,
            Triple(true, false, false) to false,
            Triple(true, true, true) to false,
            Triple(false, false, true) to false,
        )
        for ((input, expected) in cases) {
            val (finishing, changingConfig, enabled) = input
            assertEquals(
                "finishing=$finishing changingConfig=$changingConfig enabled=$enabled",
                expected,
                shouldClearDataOnExit(finishing, changingConfig, enabled),
            )
        }
    }
}
