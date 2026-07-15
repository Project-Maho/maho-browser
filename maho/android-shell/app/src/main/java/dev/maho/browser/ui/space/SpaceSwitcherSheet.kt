@file:OptIn(ExperimentalMaterial3Api::class)

package dev.maho.browser.ui.space

import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FloatingActionButton
import androidx.compose.material3.Icon
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.rememberModalBottomSheetState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import dev.maho.browser.bridge.BridgeSpaces
import dev.maho.browser.models.SpaceColor
import dev.maho.browser.models.SpaceId
import dev.maho.browser.models.SpaceViewModel
import dev.maho.browser.ui.icons.MahoIcon
import androidx.compose.ui.res.painterResource

@Composable
fun SpaceSwitcherSheet(
    onDismiss: () -> Unit,
    onSpaceSelected: (SpaceId) -> Unit,
    modifier: Modifier = Modifier,
) {
    val sheetState = rememberModalBottomSheetState(skipPartiallyExpanded = true)

    var spaces by remember { mutableStateOf<List<SpaceViewModel>>(emptyList()) }
    var activeSpaceId by remember { mutableStateOf<SpaceId?>(null) }
    var showCreateDialog by remember { mutableStateOf(false) }
    var newSpaceName by remember { mutableStateOf("") }

    LaunchedEffect(Unit) {
        spaces = BridgeSpaces.getSpaceViewModels()
        activeSpaceId = BridgeSpaces.getActiveSpaceId()
    }

    ModalBottomSheet(
        onDismissRequest = onDismiss,
        sheetState = sheetState,
        modifier = modifier,
    ) {
        Scaffold(
            topBar = {
                TopAppBar(title = { Text("Spaces") })
            },
            floatingActionButton = {
                FloatingActionButton(onClick = {
                    newSpaceName = ""
                    showCreateDialog = true
                }) {
                    Icon(painter = painterResource(id = MahoIcon.Add.drawableRes), contentDescription = "New Space")
                }
            },
        ) { padding ->
            androidx.compose.foundation.lazy.LazyColumn(
                modifier = Modifier
                    .padding(padding)
                    .fillMaxWidth(),
            ) {
                items(
                    count = spaces.size,
                    key = { spaces[it].id },
                ) { index ->
                    val space = spaces[index]
                    SpaceCard(
                        space = space,
                        isActive = space.id == activeSpaceId,
                        onTap = {
                            BridgeSpaces.activateSpace(space.id)
                            onSpaceSelected(space.id)
                            onDismiss()
                        },
                        onRename = { newName ->
                            BridgeSpaces.renameSpace(space.id, newName)
                            spaces = BridgeSpaces.getSpaceViewModels()
                        },
                        onRecolor = { newColor ->
                            BridgeSpaces.recolorSpace(space.id, newColor)
                            spaces = BridgeSpaces.getSpaceViewModels()
                        },
                        onDelete = {
                            BridgeSpaces.deleteSpace(space.id)
                            spaces = BridgeSpaces.getSpaceViewModels()
                        },
                    )
                }
            }
        }
    }

    if (showCreateDialog) {
        AlertDialog(
            onDismissRequest = { showCreateDialog = false },
            title = { Text("Create Space") },
            text = {
                OutlinedTextField(
                    value = newSpaceName,
                    onValueChange = { newSpaceName = it },
                    label = { Text("Space Name") },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth()
                )
            },
            confirmButton = {
                TextButton(onClick = {
                    showCreateDialog = false
                    val defaultColor = SpaceColor(hue = 210.0, saturation = 0.7, brightness = 0.8)
                    val activeProfileId = ""
                    BridgeSpaces.createSpace(
                        name = newSpaceName.takeIf { it.isNotBlank() } ?: "New Space",
                        color = defaultColor,
                        profileId = activeProfileId,
                    )
                    spaces = BridgeSpaces.getSpaceViewModels()
                }) {
                    Text("Create")
                }
            },
            dismissButton = {
                TextButton(onClick = { showCreateDialog = false }) {
                    Text("Cancel")
                }
            }
        )
    }
}
