@file:OptIn(ExperimentalMaterial3Api::class)

package dev.maho.browser.ui.profiles

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FloatingActionButton
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.ListItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SwipeToDismissBox
import androidx.compose.material3.SwipeToDismissBoxValue
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.rememberSwipeToDismissBoxState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import dev.maho.browser.bridge.BridgeProfiles
import dev.maho.browser.models.ProfileConfig
import dev.maho.browser.ui.icons.MahoIcon
import androidx.compose.ui.res.painterResource

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ProfileSettingsScreen(
    onBack: () -> Unit = {},
) {
    var profiles by remember { mutableStateOf<List<ProfileConfig>>(emptyList()) }
    var activeProfileId by remember { mutableStateOf("") }
    var showCreateSheet by remember { mutableStateOf(false) }
    var profileToDelete by remember { mutableStateOf<ProfileConfig?>(null) }

    fun reload() {
        profiles = BridgeProfiles.getAllProfiles()
        activeProfileId = BridgeProfiles.getActiveProfile() ?: ""
    }

    LaunchedEffect(Unit) {
        reload()
    }

    if (profileToDelete != null) {
        AlertDialog(
            onDismissRequest = { profileToDelete = null },
            title = { Text("Delete Profile?") },
            text = { Text("This will permanently delete this profile and all its data.") },
            confirmButton = {
                TextButton(onClick = {
                    profileToDelete?.let { BridgeProfiles.deleteExistingProfile(it.id) }
                    profileToDelete = null
                    reload()
                }) {
                    Text("Delete")
                }
            },
            dismissButton = {
                TextButton(onClick = { profileToDelete = null }) {
                    Text("Cancel")
                }
            },
        )
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Profiles") },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(painter = painterResource(id = MahoIcon.NavBack.drawableRes), contentDescription = "Back")
                    }
                },
            )
        },
        floatingActionButton = {
            FloatingActionButton(onClick = { showCreateSheet = true }) {
                Icon(painter = painterResource(id = MahoIcon.Add.drawableRes), contentDescription = "New Profile")
            }
        },
    ) { padding ->
        if (profiles.isEmpty()) {
            Box(
                modifier = Modifier
                    .padding(padding)
                    .fillMaxSize(),
                contentAlignment = Alignment.Center,
            ) {
                Column(horizontalAlignment = Alignment.CenterHorizontally) {
                    Icon(painter = painterResource(id = MahoIcon.Profiles.drawableRes),
                        contentDescription = null,
                        tint = MaterialTheme.colorScheme.onSurfaceVariant,
                        modifier = Modifier.size(48.dp),
                    )
                    Text(
                        text = "No Profiles",
                        style = MaterialTheme.typography.titleMedium,
                        modifier = Modifier.padding(top = 8.dp),
                    )
                    Text(
                        text = "Create a profile to separate browsing contexts.",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
            }
        } else {
            LazyColumn(
                modifier = Modifier
                    .padding(padding)
                    .fillMaxSize(),
            ) {
                items(items = profiles, key = { it.id }) { profile ->
                    ProfileItem(
                        profile = profile,
                        isActive = profile.id == activeProfileId,
                        onTap = {
                            BridgeProfiles.switchToProfile(profile.id)
                            activeProfileId = profile.id
                        },
                        onDelete = {
                            profileToDelete = profile
                        },
                    )
                }
            }
        }
    }

    if (showCreateSheet) {
        ProfileFormSheet(
            onDismiss = { showCreateSheet = false },
            onSave = { name, _, _ ->
                BridgeProfiles.createNewProfile(name)
                showCreateSheet = false
                reload()
            },
        )
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun ProfileItem(
    profile: ProfileConfig,
    isActive: Boolean,
    onTap: () -> Unit,
    onDelete: () -> Unit,
) {
    val dismissState = rememberSwipeToDismissBoxState(
        confirmValueChange = { value ->
            if (value == SwipeToDismissBoxValue.EndToStart) {
                onDelete()
                true
            } else {
                false
            }
        },
    )

    SwipeToDismissBox(
        state = dismissState,
        backgroundContent = {
            Box(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(horizontal = 16.dp),
                contentAlignment = Alignment.CenterEnd,
            ) {
                Icon(painter = painterResource(id = MahoIcon.Delete.drawableRes), contentDescription = "Delete", tint = Color.White)
            }
        },
        enableDismissFromStartToEnd = false,
    ) {
        ListItem(
            headlineContent = {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(
                        text = profile.name,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                    )
                    if (isActive) {
                        Spacer(modifier = Modifier.width(8.dp))
                        Icon(painter = painterResource(id = MahoIcon.CheckCircle.drawableRes),
                            contentDescription = "Active",
                            tint = MaterialTheme.colorScheme.primary,
                            modifier = Modifier.size(18.dp),
                        )
                    }
                }
            },
            supportingContent = {
                Text(
                    text = profile.downloadPath,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                    style = MaterialTheme.typography.bodySmall,
                )
            },
            leadingContent = {
                Box(
                    modifier = Modifier
                        .size(36.dp)
                        .clip(CircleShape)
                        .background(parseHexColor(profile.avatarColor)),
                    contentAlignment = Alignment.Center,
                ) {
                    Text(
                        text = profile.name.take(1).uppercase(),
                        color = Color.White,
                        fontWeight = FontWeight.Bold,
                    )
                }
            },
            modifier = Modifier.clickable(onClick = onTap),
        )
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun ProfileFormSheet(
    profile: ProfileConfig? = null,
    onDismiss: () -> Unit,
    onSave: (name: String, avatarColor: String, downloadPath: String) -> Unit,
) {
    var name by remember { mutableStateOf(profile?.name ?: "") }
    var avatarColor by remember { mutableStateOf(profile?.avatarColor ?: "#4A90D9") }
    var downloadPath by remember { mutableStateOf(profile?.downloadPath ?: "") }

    ModalBottomSheet(onDismissRequest = onDismiss) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 24.dp, vertical = 16.dp),
        ) {
            Text(
                text = if (profile == null) "New Profile" else "Edit Profile",
                style = MaterialTheme.typography.titleLarge,
                modifier = Modifier.padding(bottom = 16.dp),
            )
            OutlinedTextField(
                value = name,
                onValueChange = { name = it },
                label = { Text("Name") },
                singleLine = true,
                modifier = Modifier.fillMaxWidth(),
            )
            OutlinedTextField(
                value = avatarColor,
                onValueChange = { avatarColor = it },
                label = { Text("Avatar Color (hex)") },
                singleLine = true,
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(top = 8.dp),
            )
            OutlinedTextField(
                value = downloadPath,
                onValueChange = { downloadPath = it },
                label = { Text("Download Path") },
                singleLine = true,
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(top = 8.dp),
            )
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(top = 16.dp, bottom = 24.dp),
            ) {
                Spacer(modifier = Modifier.weight(1f))
                TextButton(onClick = onDismiss) {
                    Text("Cancel")
                }
                TextButton(
                    onClick = { onSave(name, avatarColor, downloadPath) },
                    enabled = name.isNotBlank(),
                ) {
                    Text("Save")
                }
            }
        }
    }
}

private fun parseHexColor(hex: String): Color {
    val cleaned = hex.removePrefix("#")
    return try {
        val value = cleaned.toLong(16)
        Color(
            red = ((value shr 16) and 0xFF) / 255f,
            green = ((value shr 8) and 0xFF) / 255f,
            blue = (value and 0xFF) / 255f,
        )
    } catch (_: Exception) {
        Color.Gray
    }
}
