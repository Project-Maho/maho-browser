package dev.maho.browser.ui.space

import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ListItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import dev.maho.browser.models.SpaceColor
import dev.maho.browser.models.SpaceViewModel

@OptIn(ExperimentalFoundationApi::class)
@Composable
fun SpaceCard(
    space: SpaceViewModel,
    isActive: Boolean,
    onTap: () -> Unit,
    onRename: (String) -> Unit,
    onRecolor: (SpaceColor) -> Unit,
    onDelete: () -> Unit,
    modifier: Modifier = Modifier,
) {
    var showMenu by remember { mutableStateOf(false) }
    var showRenameDialog by remember { mutableStateOf(false) }
    var renameText by remember { mutableStateOf(space.name) }
    var showColorDialog by remember { mutableStateOf(false) }

    val spaceColor = Color.hsv(
        space.color.hue.toFloat(),
        space.color.saturation.toFloat(),
        space.color.brightness.toFloat(),
    )

    ListItem(
        headlineContent = {
            Text(
                text = space.name,
                style = if (isActive) {
                    MaterialTheme.typography.titleMedium
                } else {
                    MaterialTheme.typography.bodyLarge
                },
            )
        },
        supportingContent = {
            Text(
                text = "${space.tabCount} tab${if (space.tabCount != 1) "s" else ""}",
                style = MaterialTheme.typography.bodySmall,
            )
        },
        leadingContent = {
            Box(
                modifier = Modifier
                     .size(24.dp)
                     .clip(CircleShape)
                     .background(spaceColor),
            )
        },
        modifier = modifier.combinedClickable(
            onClick = onTap,
            onLongClick = { showMenu = true },
        ),
    )

    DropdownMenu(
        expanded = showMenu,
        onDismissRequest = { showMenu = false },
    ) {
        DropdownMenuItem(
            text = { Text("Rename") },
            onClick = {
                showMenu = false
                showRenameDialog = true
            },
        )
        DropdownMenuItem(
            text = { Text("Change Color") },
            onClick = {
                showMenu = false
                showColorDialog = true
            },
        )
        DropdownMenuItem(
            text = { Text("Delete") },
            onClick = {
                showMenu = false
                onDelete()
            },
        )
    }

    if (showRenameDialog) {
        AlertDialog(
            onDismissRequest = { showRenameDialog = false },
            title = { Text("Rename Space") },
            text = {
                OutlinedTextField(
                    value = renameText,
                    onValueChange = { renameText = it },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth()
                )
            },
            confirmButton = {
                TextButton(onClick = {
                    showRenameDialog = false
                    onRename(renameText)
                }) {
                    Text("OK")
                }
            },
            dismissButton = {
                TextButton(onClick = { showRenameDialog = false }) {
                    Text("Cancel")
                }
            }
        )
    }

    if (showColorDialog) {
        AlertDialog(
            onDismissRequest = { showColorDialog = false },
            title = { Text("Choose Color") },
            text = {
                val presetColors = listOf(
                    SpaceColor(0.0, 0.85, 0.9),      // Red
                    SpaceColor(30.0, 0.85, 0.9),     // Orange
                    SpaceColor(60.0, 0.85, 0.9),     // Yellow
                    SpaceColor(120.0, 0.85, 0.9),    // Green
                    SpaceColor(200.0, 0.85, 0.9),    // Blue
                    SpaceColor(270.0, 0.85, 0.9)     // Purple
                )
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceEvenly
                ) {
                    presetColors.forEach { presetColor ->
                        val composeColor = Color.hsv(
                            presetColor.hue.toFloat(),
                            presetColor.saturation.toFloat(),
                            presetColor.brightness.toFloat()
                        )
                        Box(
                            modifier = Modifier
                                .size(36.dp)
                                .clip(CircleShape)
                                .background(composeColor)
                                .clickable {
                                    showColorDialog = false
                                    onRecolor(presetColor)
                                }
                        )
                    }
                }
            },
            confirmButton = {
                TextButton(onClick = { showColorDialog = false }) {
                    Text("Close")
                }
            }
        )
    }
}
