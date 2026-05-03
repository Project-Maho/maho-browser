package dev.maho.browser.ui.space

import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ListItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
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
                onRename("${space.name} (renamed)")
            },
        )
        DropdownMenuItem(
            text = { Text("Change Color") },
            onClick = {
                showMenu = false
                val newColor = SpaceColor(
                    hue = (space.color.hue + 60.0) % 360.0,
                    saturation = space.color.saturation,
                    brightness = space.color.brightness,
                )
                onRecolor(newColor)
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
}
