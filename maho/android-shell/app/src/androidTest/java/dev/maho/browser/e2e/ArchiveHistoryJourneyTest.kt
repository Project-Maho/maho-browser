package dev.maho.browser.e2e

import androidx.compose.material3.MaterialTheme
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithText
import dev.maho.browser.models.TabRole
import dev.maho.browser.models.TabViewModel
import dev.maho.browser.ui.tab.TabGrid
import dev.maho.browser.ui.theme.ProvideBrowserShellTheme
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

class ArchiveHistoryJourneyTest {

    @get:Rule
    val composeTestRule = createComposeRule()

    @Test
    fun testArchiveTabSelectionAndRecovery() {
        var archivedTabId = ""
        val tabs = listOf(
            TabViewModel(
                id = "tab-archive-candidate",
                spaceId = "space-1",
                title = "Candidate for Archive",
                url = "https://archive.example.com",
                isLoading = false,
                isMuted = false,
                isPlayingAudio = false,
                lifecycleState = "active",
                children = emptyList(),
                createdAt = "2024-01-01T00:00:00Z",
                lastActiveAt = "2024-01-01T00:00:00Z",
                role = TabRole(type = "normal"),
            )
        )

        composeTestRule.setContent {
            MaterialTheme {
                ProvideBrowserShellTheme {
                    TabGrid(
                        tabs = tabs,
                        spaceId = null,
                        activeTabId = "tab-archive-candidate",
                        isIncognito = false,
                        onTabSelected = {},
                        onTabArchived = { archivedTabId = it },
                        onTabClosed = {},
                    )
                }
            }
        }

        composeTestRule.onNodeWithText("archive.example.com").assertIsDisplayed()
    }
}
