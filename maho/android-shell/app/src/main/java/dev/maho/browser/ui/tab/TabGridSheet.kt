@file:OptIn(ExperimentalMaterial3Api::class)

package dev.maho.browser.ui.tab

import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.rememberModalBottomSheetState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import dev.maho.browser.bridge.BridgeSpaces
import dev.maho.browser.bridge.BridgeTabs
import dev.maho.browser.models.SpaceId
import dev.maho.browser.models.SpaceViewModel
import dev.maho.browser.models.TabId
import dev.maho.browser.models.TabViewModel
import dev.maho.browser.ui.space.SpaceChipRow
import dev.maho.browser.ui.space.SpaceSwitcherSheet
import dev.maho.browser.ui.theme.BrowserShellTheme

@Composable
fun TabGridSheet(
    onDismiss: () -> Unit,
    onTabSelected: (TabId) -> Unit,
    modifier: Modifier = Modifier,
) {
    val shellColors = BrowserShellTheme.colors
    val sheetState = rememberModalBottomSheetState(skipPartiallyExpanded = true)

    var tabs by remember { mutableStateOf<List<TabViewModel>>(emptyList()) }
    var spaces by remember { mutableStateOf<List<SpaceViewModel>>(emptyList()) }
    var activeSpaceId by remember { mutableStateOf<SpaceId?>(null) }
    var activeTabId by remember { mutableStateOf<TabId?>(null) }

    LaunchedEffect(Unit) {
        tabs = BridgeTabs.getTabViewModels()
        spaces = BridgeSpaces.getSpaceViewModels()
        activeSpaceId = BridgeSpaces.getActiveSpaceId()
        activeTabId = BridgeTabs.getActiveTabId()
    }

    val filteredTabs = tabs.filter { it.spaceId == activeSpaceId }
    val activeSpaceName = spaces.firstOrNull { it.id == activeSpaceId }?.name ?: "Current space"

    ModalBottomSheet(
        onDismissRequest = onDismiss,
        sheetState = sheetState,
        modifier = modifier,
        containerColor = shellColors.overlaySurface,
        contentColor = shellColors.textPrimary,
    ) {
        var showSpaceSwitcher by remember { mutableStateOf(false) }

        SpaceChipRow(
            spaces = spaces,
            activeSpaceId = activeSpaceId,
            onSpaceSelected = { spaceId ->
                BridgeSpaces.activateSpace(spaceId)
                activeSpaceId = spaceId
                spaces = BridgeSpaces.getSpaceViewModels()
                tabs = BridgeTabs.getTabViewModels()
            },
            onOverflowClick = { showSpaceSwitcher = true },
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 16.dp, vertical = 8.dp),
        )

        if (showSpaceSwitcher) {
            SpaceSwitcherSheet(
                onDismiss = { showSpaceSwitcher = false },
                onSpaceSelected = { spaceId ->
                    activeSpaceId = spaceId
                    spaces = BridgeSpaces.getSpaceViewModels()
                    tabs = BridgeTabs.getTabViewModels()
                }
            )
        }

        TabGridToolbar(
            tabCount = filteredTabs.size,
            activeSpaceName = activeSpaceName,
            onNewTab = {
                val spaceId = activeSpaceId ?: return@TabGridToolbar
                BridgeTabs.createTab(spaceId)
                tabs = BridgeTabs.getTabViewModels()
                spaces = BridgeSpaces.getSpaceViewModels()
            },
            onCloseAll = {
                val spaceId = activeSpaceId ?: return@TabGridToolbar
                val keepTab = activeTabId ?: return@TabGridToolbar
                BridgeTabs.closeAllTabs(spaceId, keepTab)
                tabs = BridgeTabs.getTabViewModels()
                spaces = BridgeSpaces.getSpaceViewModels()
            },
            onDone = onDismiss,
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 16.dp, vertical = 4.dp),
        )

        TabGrid(
            tabs = filteredTabs,
            spaceId = activeSpaceId,
            activeTabId = activeTabId,
            isIncognito = false,
            onTabSelected = { tabId ->
                BridgeTabs.activateTab(tabId)
                onTabSelected(tabId)
                onDismiss()
            },
            onTabArchived = { tabId ->
                BridgeTabs.archiveTab(tabId)
                tabs = BridgeTabs.getTabViewModels()
                spaces = BridgeSpaces.getSpaceViewModels()
                activeTabId = BridgeTabs.getActiveTabId()
            },
            onTabClosed = { tabId ->
                BridgeTabs.closeTab(tabId)
                tabs = BridgeTabs.getTabViewModels()
                spaces = BridgeSpaces.getSpaceViewModels()
                activeTabId = BridgeTabs.getActiveTabId()
            },
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 8.dp),
        )
    }
}
