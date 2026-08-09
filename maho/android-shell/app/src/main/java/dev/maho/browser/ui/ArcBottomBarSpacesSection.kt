package dev.maho.browser.ui

import androidx.compose.material3.DropdownMenuItem
import androidx.compose.runtime.Composable
import dev.maho.browser.models.SpaceViewModel
import dev.maho.browser.ui.theme.BrowserShellColors

@Composable
internal fun ArcBottomBarSpacesSection(
    spaces: List<SpaceViewModel>,
    shellColors: BrowserShellColors,
    onDismissMenu: () -> Unit,
    onSelectSpace: (String) -> Unit,
) {
    ArcMenuSectionLabel(
        label = "Spaces",
        shellColors = shellColors,
    )
    spaces.forEach { space ->
        DropdownMenuItem(
            text = {
                ArcMenuLabel(
                    title = space.name,
                    subtitle = "${space.tabCount} tab${if (space.tabCount == 1) "" else "s"}",
                    shellColors = shellColors,
                )
            },
            onClick = {
                onDismissMenu()
                onSelectSpace(space.id)
            },
        )
    }
}
