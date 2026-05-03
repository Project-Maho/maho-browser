@file:OptIn(ExperimentalMaterial3Api::class)

package dev.maho.browser.ui.space

import androidx.compose.foundation.horizontalScroll
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.rememberScrollState
import androidx.compose.material3.FilterChip
import androidx.compose.material3.FilterChipDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import dev.maho.browser.models.SpaceId
import dev.maho.browser.models.SpaceViewModel

@Composable
fun SpaceChipRow(
    spaces: List<SpaceViewModel>,
    activeSpaceId: SpaceId?,
    onSpaceSelected: (SpaceId) -> Unit,
    modifier: Modifier = Modifier,
) {
    Row(
        modifier = modifier.horizontalScroll(rememberScrollState()),
        horizontalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        for (space in spaces) {
            val spaceColor = Color.hsv(
                space.color.hue.toFloat(),
                space.color.saturation.toFloat(),
                space.color.brightness.toFloat(),
            )
            val isSelected = space.id == activeSpaceId

            FilterChip(
                selected = isSelected,
                onClick = { onSpaceSelected(space.id) },
                label = { Text(space.name) },
                colors = FilterChipDefaults.filterChipColors(
                    selectedContainerColor = spaceColor.copy(alpha = 0.2f),
                    selectedLabelColor = spaceColor,
                ),
            )
        }
    }
}
