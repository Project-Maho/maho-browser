package dev.maho.browser.ui.tab

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import dev.maho.browser.models.TabViewModel
import dev.maho.browser.ui.theme.BrowserShellTheme
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.combinedClickable
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.runtime.getValue
import androidx.compose.runtime.setValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import dev.maho.browser.bridge.BridgeSync
import dev.maho.browser.bridge.BridgeTabs
import dev.maho.browser.models.ConnectedDevice
import dev.maho.browser.ui.icons.MahoIcon
import androidx.compose.ui.res.painterResource

@OptIn(ExperimentalFoundationApi::class)
@Composable
fun TabCard(
    tab: TabViewModel,
    isActive: Boolean,
    isIncognito: Boolean,
    visualIndex: Int,
    onTap: () -> Unit,
    onArchive: () -> Unit,
    onClose: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val shellColors = BrowserShellTheme.colors
    val metrics = BrowserShellTheme.metrics
    val accentColor = if (isIncognito) shellColors.incognitoAccent else shellColors.accent
    val borderColor = when {
        isActive && isIncognito -> shellColors.incognitoAccent.copy(alpha = 0.85f)
        isActive -> shellColors.tabCardBorder.copy(alpha = 0.86f)
        isIncognito -> shellColors.incognitoSurface.copy(alpha = 0.84f)
        else -> shellColors.divider
    }
    val borderStroke = if (isActive) {
        BorderStroke(1.15.dp, borderColor)
    } else {
        BorderStroke(1.dp, borderColor.copy(alpha = 0.24f))
    }
    val cardColor = when {
        isIncognito && isActive -> shellColors.incognitoSurface.copy(alpha = 0.96f)
        isIncognito -> shellColors.incognitoBackground.copy(alpha = 0.96f)
        isActive -> shellColors.tabCardActive.copy(alpha = 0.96f)
        else -> shellColors.tabCard.copy(alpha = 0.94f)
    }
    val previewBrush = Brush.verticalGradient(
        colors = when {
            isIncognito -> listOf(
                shellColors.incognitoSurface.copy(alpha = 0.96f),
                shellColors.incognitoBackground.copy(alpha = 0.92f),
            )
            isActive -> listOf(
                shellColors.tabCardActive.copy(alpha = 0.96f),
                shellColors.overlaySurface.copy(alpha = 0.96f),
            )
            else -> listOf(
                shellColors.overlaySurfaceHigh.copy(alpha = 0.92f),
                shellColors.tabCard.copy(alpha = 0.94f),
            )
        },
    )
    val previewFrameColor = if (isActive) {
        accentColor.copy(alpha = 0.16f)
    } else {
        shellColors.divider.copy(alpha = 0.18f)
    }
    val statusSurfaceColor = if (isActive) {
        accentColor.copy(alpha = 0.14f)
    } else {
        shellColors.windowBackground.copy(alpha = 0.16f)
    }
    val hostChipColor = if (isIncognito) {
        shellColors.incognitoSurface.copy(alpha = 0.5f)
    } else {
        shellColors.windowBackground.copy(alpha = 0.16f)
    }
    val metaChipColor = if (isActive) {
        accentColor.copy(alpha = 0.14f)
    } else {
        shellColors.windowBackground.copy(alpha = 0.14f)
    }
    val title = tab.title.ifBlank { "New tab" }
    val host = displayHost(tab.url.ifBlank { "about:blank" })
    val statusLabel = when {
        tab.isLoading -> "Loading"
        isActive -> "Current"
        else -> tab.lifecycleState.replaceFirstChar { it.uppercase() }
    }
    val details = buildList {
        if (tab.children.isNotEmpty()) add("${tab.children.size} linked")
        if (tab.isFavorite) add("Saved")
    }

    var showMenu by remember { mutableStateOf(false) }
    var showDevicePicker by remember { mutableStateOf(false) }
    var devicePickerDevices by remember { mutableStateOf<List<ConnectedDevice>>(emptyList()) }

    Card(
        shape = RoundedCornerShape(metrics.cardCorner),
        border = borderStroke,
        colors = CardDefaults.cardColors(containerColor = cardColor),
        elevation = CardDefaults.cardElevation(
            defaultElevation = if (isActive) 4.dp else 1.dp,
        ),
        modifier = modifier
            .fillMaxWidth()
            .testTag("tabCard_$visualIndex")
            .semantics {
                stateDescription = buildTabStateDescription(tab = tab, isActive = isActive, isIncognito = isIncognito)
            }
            .combinedClickable(
                onClick = onTap,
                onLongClick = { showMenu = true }
            ),
    ) {
        Box {
            DropdownMenu(
                expanded = showMenu,
                onDismissRequest = { showMenu = false }
            ) {
                DropdownMenuItem(
                    text = { Text("Duplicate") },
                    onClick = {
                        showMenu = false
                        BridgeTabs.duplicateTab(tab.id)
                    }
                )
                DropdownMenuItem(
                    text = { Text(if (tab.isPinned) "Unpin" else "Pin") },
                    onClick = {
                        showMenu = false
                        if (tab.isPinned) {
                            BridgeTabs.unpinTab(tab.id)
                        } else {
                            BridgeTabs.pinTab(tab.id)
                        }
                    }
                )
                DropdownMenuItem(
                    text = { Text(if (tab.isFavorite) "Remove from Favorites" else "Add to Favorites") },
                    onClick = {
                        showMenu = false
                        BridgeTabs.favoriteTab(tab.id)
                    }
                )
                DropdownMenuItem(
                    text = { Text("Send to Device") },
                    onClick = {
                        val currentDeviceId = BridgeSync.getCurrentDeviceId()
                        devicePickerDevices = BridgeSync.getConnectedDevices()
                            .filterNot { device -> device.id == currentDeviceId }
                        showMenu = false
                        showDevicePicker = true
                    }
                )
                DropdownMenuItem(
                    text = { Text(if (tab.isMuted) "Unmute" else "Mute") },
                    onClick = {
                        showMenu = false
                        if (tab.isMuted) {
                            BridgeTabs.unmuteTab(tab.id)
                        } else {
                            BridgeTabs.muteTab(tab.id)
                        }
                    }
                )
                androidx.compose.material3.HorizontalDivider()
                DropdownMenuItem(
                    text = { Text("Archive") },
                    onClick = {
                        showMenu = false
                        onArchive()
                    }
                )
                DropdownMenuItem(
                    text = { Text("Close") },
                    onClick = {
                        showMenu = false
                        onClose()
                    }
                )
            }

            DropdownMenu(
                expanded = showDevicePicker,
                onDismissRequest = { showDevicePicker = false }
            ) {
                if (devicePickerDevices.isEmpty()) {
                    DropdownMenuItem(
                        text = { Text("No devices") },
                        enabled = false,
                        onClick = {},
                    )
                } else {
                    devicePickerDevices.forEach { device ->
                        DropdownMenuItem(
                            text = { Text(sendDeviceLabel(device)) },
                            onClick = {
                                showDevicePicker = false
                                BridgeSync.sendTabToDevice(
                                    url = tab.url,
                                    title = tab.title,
                                    deviceId = device.id,
                                )
                            },
                        )
                    }
                }
            }

            Column(modifier = Modifier.padding(11.dp)) {
                val preview = TabPreviewStore.get(tab.id) as? android.graphics.Bitmap
                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(144.dp)
                        .clip(RoundedCornerShape(metrics.cardCorner))
                        .background(previewBrush)
                        .border(1.dp, previewFrameColor, RoundedCornerShape(metrics.cardCorner)),
                ) {
                if (preview != null && !isIncognito) {
                    androidx.compose.foundation.Image(
                        bitmap = preview.asImageBitmap(),
                        contentDescription = null,
                        contentScale = androidx.compose.ui.layout.ContentScale.Crop,
                        alignment = Alignment.TopCenter,
                        modifier = Modifier.fillMaxSize(),
                    )
                }
                Column(
                    modifier = Modifier
                        .fillMaxSize()
                        .padding(14.dp),
                    verticalArrangement = Arrangement.SpaceBetween,
                ) {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.SpaceBetween,
                    ) {
                        Row(
                            horizontalArrangement = Arrangement.spacedBy(5.dp),
                            verticalAlignment = Alignment.CenterVertically,
                        ) {
                            TabCardChip(
                                label = statusLabel,
                                containerColor = statusSurfaceColor,
                                contentColor = accentColor,
                            )
                            if (tab.children.isNotEmpty()) {
                                TabCardChip(
                                    label = "${tab.children.size} linked",
                                    containerColor = hostChipColor,
                                    contentColor = shellColors.textSecondary,
                                )
                            }
                        }

                        if (tab.isLoading) {
                            CircularProgressIndicator(
                                modifier = Modifier.size(14.dp),
                                strokeWidth = 1.6.dp,
                                color = accentColor,
                            )
                        }
                    }

                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(6.dp),
                    ) {
                        tabCardLabels(tab = tab)
                            .take(2)
                            .forEach { label ->
                                TabCardChip(
                                    label = label,
                                    containerColor = metaChipColor,
                                    contentColor = shellColors.textPrimary,
                                )
                            }
                    }
                }
                }

                Spacer(modifier = Modifier.height(11.dp))

                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    modifier = Modifier.fillMaxWidth(),
                ) {
                    Column(
                        modifier = Modifier.weight(1f),
                        verticalArrangement = Arrangement.spacedBy(4.dp),
                    ) {
                        Text(
                            text = host,
                            style = MaterialTheme.typography.bodySmall,
                            color = shellColors.textSecondary,
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis,
                        )
                        if (details.isNotEmpty()) {
                            Text(
                                text = details.joinToString(" · "),
                                style = MaterialTheme.typography.labelSmall,
                                color = shellColors.textSecondary.copy(alpha = 0.88f),
                                maxLines = 1,
                                overflow = TextOverflow.Ellipsis,
                            )
                        }
                    }

                    Surface(
                        shape = CircleShape,
                        color = shellColors.windowBackground.copy(alpha = 0.12f),
                    ) {
                        IconButton(
                            onClick = onClose,
                            modifier = Modifier.size(28.dp),
                        ) {
                            Icon(
                                painter = painterResource(id = MahoIcon.Close.drawableRes),
                                contentDescription = "Close tab",
                                modifier = Modifier.size(14.dp),
                                tint = shellColors.textSecondary,
                            )
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun TabCardChip(
    label: String,
    containerColor: Color,
    contentColor: Color,
) {
    Surface(
        shape = RoundedCornerShape(999.dp),
        color = containerColor,
        contentColor = contentColor,
    ) {
        Text(
            text = label,
            style = MaterialTheme.typography.labelSmall,
            modifier = Modifier.padding(horizontal = 8.dp, vertical = 4.dp),
        )
    }
}

private fun tabCardLabels(
    tab: TabViewModel,
): List<String> = buildList {
    if (tab.isPinned) add("Pinned")
    if (tab.isPlayingAudio) add("Audio")
    if (tab.isMuted) add("Muted")
}

private fun sendDeviceLabel(device: ConnectedDevice): String {
    val offlineSuffix = if (device.isOnline == false) " (offline)" else ""
    return "${device.name}$offlineSuffix"
}

private fun buildTabStateDescription(
    tab: TabViewModel,
    isActive: Boolean,
    isIncognito: Boolean,
): String = buildString {
    append(if (isIncognito) "private tab" else "tab")
    append(", ")
    append(tab.title.ifBlank { "new tab" })
    if (isActive) append(", current")
    if (tab.isLoading) append(", loading")
    if (tab.isPinned) append(", pinned")
    if (tab.isFavorite) append(", saved")
    if (tab.isPlayingAudio) append(", playing audio")
    if (tab.isMuted) append(", muted")
    append(", state ${tab.lifecycleState}")
}

private fun displayHost(url: String): String {
    return runCatching {
        java.net.URI(url).host?.removePrefix("www.") ?: url
    }.getOrDefault(url)
}
