package dev.maho.browser.ui

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.Spring
import androidx.compose.animation.core.spring
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInVertically
import androidx.compose.animation.slideOutVertically
import androidx.compose.runtime.Composable
import dev.maho.browser.models.SuggestionViewModel
import dev.maho.browser.ui.search.SearchSheet

@Composable
internal fun MainBrowserSearchOverlay(
    visible: Boolean,
    reduceMotion: Boolean,
    initialQuery: String,
    isIncognito: Boolean,
    onDismiss: () -> Unit,
    onSubmit: (String) -> Unit,
    onToggleIncognito: () -> Unit,
    onSelectSuggestion: (SuggestionViewModel) -> Unit,
    onBrowseForMe: (String) -> Unit,
    onRecordSuggestionSelection: (Int, SuggestionViewModel) -> Unit,
) {
    AnimatedVisibility(
        visible = visible,
        enter = if (reduceMotion) {
            fadeIn(animationSpec = tween(durationMillis = 120))
        } else {
            slideInVertically(
                animationSpec = spring(
                    dampingRatio = 0.88f,
                    stiffness = Spring.StiffnessMediumLow,
                ),
                initialOffsetY = { it },
            ) + fadeIn(animationSpec = tween(durationMillis = 120))
        },
        exit = if (reduceMotion) {
            fadeOut(animationSpec = tween(durationMillis = 120))
        } else {
            slideOutVertically(
                animationSpec = spring(
                    dampingRatio = 0.90f,
                    stiffness = Spring.StiffnessMedium,
                ),
                targetOffsetY = { it },
            ) + fadeOut(animationSpec = tween(durationMillis = 180))
        },
    ) {
        SearchSheet(
            initialQuery = initialQuery,
            isIncognito = isIncognito,
            onDismiss = onDismiss,
            onSubmit = onSubmit,
            onToggleIncognito = onToggleIncognito,
            onSelectSuggestion = onSelectSuggestion,
            onBrowseForMe = onBrowseForMe,
            onRecordSuggestionSelection = onRecordSuggestionSelection,
        )
    }
}
