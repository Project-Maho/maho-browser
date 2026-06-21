package dev.maho.browser.ui.chat

import android.graphics.BitmapFactory
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Image
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import dev.maho.browser.ui.camera.CameraCaptureScreen
import dev.maho.browser.ui.theme.BrowserShellTheme

private const val VisualSearchMime = "image/jpeg"

@Composable
fun VisualSearchScreen(
    isIncognito: Boolean,
    onDismiss: () -> Unit,
    onImageReady: (mime: String, data: ByteArray) -> Unit,
) {
    var capturedImage by remember { mutableStateOf<ByteArray?>(null) }
    val shellColors = BrowserShellTheme.colors

    Dialog(
        onDismissRequest = onDismiss,
        properties = DialogProperties(
            usePlatformDefaultWidth = false,
            decorFitsSystemWindows = false,
        ),
    ) {
        Surface(
            modifier = Modifier
                .fillMaxSize()
                .testTag("aiScreenVisualSearch"),
            color = if (isIncognito) {
                shellColors.incognitoBackground
            } else {
                shellColors.overlayBackground
            },
            tonalElevation = 0.dp,
        ) {
            if (capturedImage == null) {
                CameraCaptureScreen(
                    onCaptured = { bytes ->
                        capturedImage = bytes
                    },
                    onCancel = onDismiss,
                )
            } else {
                VisualSearchPreview(
                    imageBytes = capturedImage!!,
                    isIncognito = isIncognito,
                    onRetake = { capturedImage = null },
                    onDismiss = onDismiss,
                    onSend = {
                        onImageReady(VisualSearchMime, capturedImage!!)
                        onDismiss()
                    },
                )
            }
        }
    }
}

@Composable
private fun VisualSearchPreview(
    imageBytes: ByteArray,
    isIncognito: Boolean,
    onRetake: () -> Unit,
    onDismiss: () -> Unit,
    onSend: () -> Unit,
) {
    val shellColors = BrowserShellTheme.colors
    val metrics = BrowserShellTheme.metrics
    val accentColor = if (isIncognito) shellColors.incognitoAccent else shellColors.accent
    val previewImage = remember(imageBytes) {
        BitmapFactory.decodeByteArray(imageBytes, 0, imageBytes.size)?.asImageBitmap()
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(
                if (isIncognito) shellColors.incognitoBackground else shellColors.overlayBackground,
            )
            .safeDrawingPadding()
            .navigationBarsPadding()
            .padding(horizontal = metrics.sectionSpacing, vertical = metrics.compactSpacing),
        verticalArrangement = Arrangement.spacedBy(metrics.sectionSpacing),
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.SpaceBetween,
        ) {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(metrics.compactSpacing),
            ) {
                Surface(
                    shape = RoundedCornerShape(metrics.compactCorner),
                    color = accentColor.copy(alpha = 0.14f),
                ) {
                    Icon(
                        imageVector = Icons.Default.Image,
                        contentDescription = null,
                        tint = accentColor,
                        modifier = Modifier
                            .padding(10.dp)
                            .size(22.dp),
                    )
                }

                Column(verticalArrangement = Arrangement.spacedBy(2.dp)) {
                    Text(
                        text = "Visual search",
                        style = MaterialTheme.typography.headlineSmall,
                        fontWeight = FontWeight.SemiBold,
                        color = shellColors.textPrimary,
                    )
                    Text(
                        text = "Review the captured image before sending it to chat.",
                        style = MaterialTheme.typography.bodyMedium,
                        color = shellColors.textSecondary,
                    )
                }
            }

            TextButton(onClick = onDismiss) {
                Text("Close")
            }
        }

        Surface(
            modifier = Modifier
                .fillMaxWidth()
                .weight(1f),
            shape = RoundedCornerShape(metrics.cardCorner),
            color = if (isIncognito) {
                shellColors.incognitoSurface.copy(alpha = 0.9f)
            } else {
                shellColors.overlaySurfaceHigh.copy(alpha = 0.92f)
            },
            tonalElevation = 0.dp,
            shadowElevation = metrics.floatingShadow,
        ) {
            Box(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(metrics.compactSpacing),
                contentAlignment = Alignment.Center,
            ) {
                if (previewImage != null) {
                    Image(
                        bitmap = previewImage,
                        contentDescription = "Captured image preview",
                        modifier = Modifier.fillMaxSize(),
                        contentScale = ContentScale.Fit,
                    )
                } else {
                    Column(
                        horizontalAlignment = Alignment.CenterHorizontally,
                        verticalArrangement = Arrangement.spacedBy(metrics.compactSpacing),
                    ) {
                        Icon(
                            imageVector = Icons.Default.Image,
                            contentDescription = null,
                            tint = shellColors.textSecondary,
                            modifier = Modifier.size(36.dp),
                        )
                        Text(
                            text = "Preview unavailable",
                            style = MaterialTheme.typography.titleMedium,
                            color = shellColors.textPrimary,
                        )
                        Text(
                            text = "You can retake the image or send it as-is.",
                            style = MaterialTheme.typography.bodyMedium,
                            color = shellColors.textSecondary,
                        )
                    }
                }
            }
        }

        Spacer(modifier = Modifier.height(4.dp))

        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(metrics.compactSpacing),
        ) {
            Button(
                onClick = onRetake,
                modifier = Modifier.weight(1f),
                shape = RoundedCornerShape(metrics.cardCorner),
                colors = ButtonDefaults.buttonColors(
                    containerColor = if (isIncognito) {
                        shellColors.incognitoSurface
                    } else {
                        shellColors.overlaySurface
                    },
                    contentColor = shellColors.textPrimary,
                ),
            ) {
                Text("Retake")
            }

            Button(
                onClick = onSend,
                modifier = Modifier.weight(1f),
                shape = RoundedCornerShape(metrics.cardCorner),
                colors = ButtonDefaults.buttonColors(
                    containerColor = accentColor,
                ),
            ) {
                Text("Send")
            }
        }
    }
}
