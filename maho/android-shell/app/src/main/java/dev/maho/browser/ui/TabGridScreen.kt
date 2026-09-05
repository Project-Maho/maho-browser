@file:OptIn(ExperimentalMaterial3Api::class)

package dev.maho.browser.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.rememberModalBottomSheetState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import dev.maho.browser.bridge.BridgeSpaces
import dev.maho.browser.bridge.BridgeTabs
import dev.maho.browser.ui.tab.orderTabsForDeck
import dev.maho.browser.models.SpaceId
import dev.maho.browser.models.SpaceViewModel
import dev.maho.browser.models.TabId
import dev.maho.browser.models.TabViewModel
import dev.maho.browser.ui.space.SpaceChipRow
import dev.maho.browser.ui.space.SpaceSwitcherSheet
import dev.maho.browser.ui.tab.TabGrid
import dev.maho.browser.ui.tab.TabGridToolbar
import dev.maho.browser.ui.theme.BrowserShellTheme
import dev.maho.browser.ui.icons.MahoIcon
import androidx.compose.ui.res.painterResource

@Composable
fun TabGridScreen(
    activeTabId: TabId?,
    isIncognito: Boolean,
    onDismiss: () -> Unit,
    onTabSelected: (TabId?) -> Unit,
    onNewTabCreated: (spaceId: SpaceId, tabId: TabId) -> Unit,
    modifier: Modifier = Modifier,
) {
    val shellColors = BrowserShellTheme.colors
    val sheetState = rememberModalBottomSheetState(skipPartiallyExpanded = true)
    var tabs by remember { mutableStateOf<List<TabViewModel>>(emptyList()) }
    var spaces by remember { mutableStateOf<List<SpaceViewModel>>(emptyList()) }
    var activeSpaceId by remember { mutableStateOf<SpaceId?>(null) }

    fun refresh() {
        tabs = BridgeTabs.getTabViewModels()
        spaces = BridgeSpaces.getSpaceViewModels()
        activeSpaceId = BridgeSpaces.getActiveSpaceId()
    }

    fun createTabInActiveSpace(): Pair<SpaceId, TabId>? {
        val resolvedSpaceId = activeSpaceId
            ?: BridgeSpaces.getActiveSpaceId()
            ?: spaces.firstOrNull()?.id
            ?: BridgeSpaces.getSpaceViewModels().firstOrNull()?.id

        if (resolvedSpaceId == null) {
            refresh()
            return null
        }

        if (activeSpaceId != resolvedSpaceId) {
            BridgeSpaces.activateSpace(resolvedSpaceId)
        }

        val createdTabId = BridgeTabs.createTab(resolvedSpaceId)
            .filterIsInstance<dev.maho.browser.models.CoreUpdate.TabCreated>()
            .firstOrNull()
            ?.tab
            ?.id
            ?: BridgeTabs.getActiveTabId()

        if (createdTabId == null) {
            refresh()
            return null
        }

        BridgeTabs.activateTab(createdTabId)
        BridgeSpaces.activateSpace(resolvedSpaceId)
        refresh()
        return resolvedSpaceId to createdTabId
    }

    LaunchedEffect(Unit) {
        refresh()
    }

    val visibleTabs = orderTabsForDeck(
        tabs.filter { tab -> activeSpaceId == null || tab.spaceId == activeSpaceId },
        activeTabId,
    ) { it.id }
    val activeSpaceName = spaces.firstOrNull { it.id == activeSpaceId }?.name ?: "Current space"
    val containerColor = if (isIncognito) shellColors.incognitoSurface else shellColors.overlaySurface
    val elevatedSurfaceColor = if (isIncognito) shellColors.incognitoBackground else shellColors.overlaySurfaceHigh
    val accentColor = if (isIncognito) shellColors.incognitoAccent else shellColors.accent
    val deckBrush = Brush.verticalGradient(
        colors = if (isIncognito) {
            listOf(shellColors.incognitoBackground, shellColors.incognitoSurface)
        } else {
            listOf(shellColors.overlayBackground, shellColors.overlaySurface)
        },
    )

    ModalBottomSheet(
        onDismissRequest = onDismiss,
        sheetState = sheetState,
        modifier = modifier,
        containerColor = containerColor,
        contentColor = shellColors.textPrimary,
    ) {
        Scaffold(
            containerColor = containerColor,
        ) { padding ->
            Column(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(padding)
                    .background(deckBrush)
                    .testTag("tabSwitcherShell")
                    .semantics {
                        stateDescription = if (isIncognito) "incognito tab switcher" else "tab switcher"
                    },
            ) {
                Column(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(horizontal = 16.dp, vertical = 8.dp),
                    verticalArrangement = Arrangement.spacedBy(10.dp),
                ) {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.SpaceBetween,
                    ) {
                        Row(
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(8.dp),
                        ) {
                            Icon(
                                painter = painterResource(id = if (isIncognito) MahoIcon.VisibilityOff.drawableRes else MahoIcon.Tab.drawableRes),
                                contentDescription = null,
                                tint = accentColor,
                                modifier = Modifier.size(18.dp),
                            )
                            Column(verticalArrangement = Arrangement.spacedBy(2.dp)) {
                                Text(
                                    text = activeSpaceName,
                                    style = MaterialTheme.typography.titleLarge,
                                    color = shellColors.textPrimary,
                                    modifier = Modifier.testTag("tabSwitcherTitle"),
                                )
                                Text(
                                    text = "${visibleTabs.size} tab${if (visibleTabs.size != 1) "s" else ""}",
                                    style = MaterialTheme.typography.bodySmall,
                                    color = shellColors.textSecondary,
                                )
                            }
                        }

                        Row(
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(8.dp),
                        ) {
                            if (isIncognito) {
                                Surface(
                                    modifier = Modifier.testTag("tabSwitcherStateChip"),
                                    shape = RoundedCornerShape(999.dp),
                                    color = elevatedSurfaceColor,
                                    contentColor = shellColors.textPrimary,
                                ) {
                                    Row(
                                        modifier = Modifier.padding(horizontal = 10.dp, vertical = 7.dp),
                                        verticalAlignment = Alignment.CenterVertically,
                                        horizontalArrangement = Arrangement.spacedBy(6.dp),
                                    ) {
                                        Icon(
                                            painter = painterResource(id = MahoIcon.VisibilityOff.drawableRes),
                                            contentDescription = null,
                                            tint = accentColor,
                                            modifier = Modifier.size(14.dp),
                                        )
                                        Text(
                                            text = "Private",
                                            style = MaterialTheme.typography.labelMedium,
                                        )
                                    }
                                }
                            }
                        }
                    }
                }

                var showSpaceSwitcher by remember { mutableStateOf(false) }

                SpaceChipRow(
                    spaces = spaces,
                    activeSpaceId = activeSpaceId,
                    onSpaceSelected = { spaceId ->
                        BridgeSpaces.activateSpace(spaceId)
                        refresh()
                    },
                    onOverflowClick = { showSpaceSwitcher = true },
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(horizontal = 16.dp, vertical = 2.dp),
                )

                if (showSpaceSwitcher) {
                    SpaceSwitcherSheet(
                        onDismiss = { showSpaceSwitcher = false },
                        onSpaceSelected = { spaceId ->
                            refresh()
                        }
                    )
                }

                TabGrid(
                    tabs = visibleTabs,
                    spaceId = activeSpaceId,
                    activeTabId = activeTabId,
                    isIncognito = isIncognito,
                    onTabSelected = { tabId ->
                        BridgeTabs.activateTab(tabId)
                        refresh()
                        onTabSelected(tabId)
                        onDismiss()
                    },
                    onTabArchived = { tabId ->
                        BridgeTabs.archiveTab(tabId)
                        refresh()
                        onTabSelected(BridgeTabs.getActiveTabId())
                    },
                    onTabClosed = { tabId ->
                        BridgeTabs.closeTab(tabId)
                        refresh()
                        onTabSelected(BridgeTabs.getActiveTabId())
                    },
                    modifier = Modifier
                        .weight(1f)
                        .padding(horizontal = 16.dp, vertical = 6.dp),
                )

                TabGridToolbar(
                    tabCount = visibleTabs.size,
                    activeSpaceName = activeSpaceName,
                    onNewTab = {
                        createTabInActiveSpace()?.let { (spaceId, tabId) ->
                            onNewTabCreated(spaceId, tabId)
                        }
                    },
                    onCloseAll = {
                        visibleTabs.forEach { BridgeTabs.closeTab(it.id) }
                        refresh()
                    },
                    onDone = onDismiss,
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(horizontal = 16.dp, vertical = 8.dp),
                )
            }
        }
    }
}
