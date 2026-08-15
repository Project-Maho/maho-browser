package dev.maho.browser.support

import android.Manifest
import android.content.pm.PackageManager
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.scale
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.compose.ui.semantics.Role
import androidx.core.content.ContextCompat
import dev.maho.browser.ui.icons.MahoIcon
import androidx.compose.ui.res.painterResource
import dev.maho.browser.ui.voice.VoiceAssistantOverlay

@Composable
fun VoiceSearchButton(
    manager: VoiceSearchManager,
    onResult: (String) -> Unit,
    modifier: Modifier = Modifier,
    startListeningOnLaunch: Boolean = false,
    compact: Boolean = false,
    buttonModifier: Modifier = Modifier,
    iconModifier: Modifier = Modifier,
    idleTint: Color? = null,
    listeningTint: Color? = null,
    errorTint: Color? = null,
    isIncognito: Boolean = false,
) {
    val context = LocalContext.current
    var showAssistantOverlay by rememberSaveable { mutableStateOf(false) }
    val permissionLauncher = rememberLauncherForActivityResult(
        ActivityResultContracts.RequestPermission(),
    ) { granted ->
        if (granted) {
            showAssistantOverlay = true
            manager.reset()
            manager.startListening()
        } else {
            showAssistantOverlay = false
            manager.reset()
        }
    }

    fun requestVoiceCapture() {
        showAssistantOverlay = true
        val permission = Manifest.permission.RECORD_AUDIO
        val hasPermission = ContextCompat.checkSelfPermission(
            context,
            permission,
        ) == PackageManager.PERMISSION_GRANTED

        if (hasPermission) {
            if (manager.state != VoiceSearchState.Listening && manager.state != VoiceSearchState.Processing) {
                manager.reset()
                manager.startListening()
            }
        } else {
            permissionLauncher.launch(permission)
        }
    }

    LaunchedEffect(startListeningOnLaunch) {
        if (startListeningOnLaunch && manager.state == VoiceSearchState.Idle) {
            requestVoiceCapture()
        }
    }

    LaunchedEffect(manager.finalResultText) {
        val finalText = manager.finalResultText
        if (finalText.isNotEmpty()) {
            onResult(finalText)
            showAssistantOverlay = false
            manager.reset()
        }
    }

    val isListening = manager.state == VoiceSearchState.Listening
    val isError = manager.state == VoiceSearchState.Error
    val buttonClick: () -> Unit = {
        when (manager.state) {
            VoiceSearchState.Listening,
            VoiceSearchState.Processing,
            -> showAssistantOverlay = true
            else -> requestVoiceCapture()
        }
    }
    val iconTint = when {
        isListening -> listeningTint ?: MaterialTheme.colorScheme.primary
        isError -> errorTint ?: MaterialTheme.colorScheme.error
        else -> idleTint ?: MaterialTheme.colorScheme.onSurfaceVariant
    }
    val contentDescription = if (isListening) "Stop voice search" else "Voice search"

    Box(contentAlignment = Alignment.Center, modifier = modifier) {
        if (isListening) {
            val infiniteTransition = rememberInfiniteTransition(label = "pulse")
            val scale by infiniteTransition.animateFloat(
                initialValue = 1f,
                targetValue = 1.5f,
                animationSpec = infiniteRepeatable(
                    animation = tween(800),
                    repeatMode = RepeatMode.Reverse,
                ),
                label = "pulseScale",
            )
            Box(
                modifier = Modifier
                    .size(44.dp)
                    .scale(scale)
                    .background(
                        color = MaterialTheme.colorScheme.primary.copy(alpha = 0.15f),
                        shape = CircleShape,
                    ),
            )
        }

        if (compact) {
            Box(
                modifier = buttonModifier.clickable(
                    role = Role.Button,
                    onClick = buttonClick,
                ),
                contentAlignment = Alignment.Center,
            ) {
                Icon(
                    painter = painterResource(id = if (isError) MahoIcon.MicOff.drawableRes else MahoIcon.Mic.drawableRes),
                    contentDescription = contentDescription,
                    modifier = iconModifier,
                    tint = iconTint,
                )
            }
        } else {
            IconButton(
                onClick = buttonClick,
                modifier = buttonModifier,
            ) {
                Icon(
                    painter = painterResource(id = if (isError) MahoIcon.MicOff.drawableRes else MahoIcon.Mic.drawableRes),
                    contentDescription = contentDescription,
                    modifier = iconModifier,
                    tint = iconTint,
                )
            }
        }
    }

    if (showAssistantOverlay) {
        VoiceAssistantOverlay(
            manager = manager,
            onDismiss = {
                manager.stopListening()
                manager.reset()
                showAssistantOverlay = false
            },
            onRetry = {
                requestVoiceCapture()
            },
            isIncognito = isIncognito,
        )
    }
}
