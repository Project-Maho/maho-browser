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
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Mic
import androidx.compose.material.icons.filled.MicOff
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.scale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat

@Composable
fun VoiceSearchButton(
    manager: VoiceSearchManager,
    onResult: (String) -> Unit,
    modifier: Modifier = Modifier,
) {
    val context = LocalContext.current
    val permissionLauncher = rememberLauncherForActivityResult(
        ActivityResultContracts.RequestPermission(),
    ) { granted ->
        if (granted) {
            manager.startListening()
        } else {
            manager.reset()
        }
    }

    LaunchedEffect(manager.resultText) {
        if (manager.resultText.isNotEmpty()) {
            onResult(manager.resultText)
            manager.reset()
        }
    }

    val isListening = manager.state == VoiceSearchState.Listening
    val isError = manager.state == VoiceSearchState.Error

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

        IconButton(
            onClick = {
                when (manager.state) {
                    VoiceSearchState.Listening -> manager.stopListening()
                    else -> {
                        manager.reset()
                        val hasPermission = ContextCompat.checkSelfPermission(
                            context,
                            Manifest.permission.RECORD_AUDIO,
                        ) == PackageManager.PERMISSION_GRANTED

                        if (hasPermission) {
                            manager.startListening()
                        } else {
                            permissionLauncher.launch(Manifest.permission.RECORD_AUDIO)
                        }
                    }
                }
            },
        ) {
            Icon(
                imageVector = if (isError) Icons.Default.MicOff else Icons.Default.Mic,
                contentDescription = if (isListening) "Stop voice search" else "Voice search",
                tint = when {
                    isListening -> MaterialTheme.colorScheme.primary
                    isError -> MaterialTheme.colorScheme.error
                    else -> MaterialTheme.colorScheme.onSurfaceVariant
                },
            )
        }
    }
}
