package dev.maho.browser.ui.camera

import android.Manifest
import android.content.Context
import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.PickVisualMediaRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.camera.view.PreviewView
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.PhotoLibrary
import androidx.compose.material3.Button
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalLifecycleOwner
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import dev.maho.browser.support.CameraManager
import kotlinx.coroutines.launch
import java.io.ByteArrayOutputStream

@Composable
fun CameraCaptureScreen(
    onCaptured: (ByteArray) -> Unit,
    onCancel: () -> Unit,
) {
    val context = LocalContext.current
    val lifecycleOwner = LocalLifecycleOwner.current
    val scope = rememberCoroutineScope()

    var hasPermission by remember { mutableStateOf(false) }
    var permissionDenied by remember { mutableStateOf(false) }
    var isCapturing by remember { mutableStateOf(false) }

    val cameraManager = remember {
        CameraManager(context, lifecycleOwner)
    }

    val permissionLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.RequestPermission(),
    ) { granted ->
        hasPermission = granted
        permissionDenied = !granted
    }

    val galleryLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.PickVisualMedia(),
    ) { uri: Uri? ->
        if (uri != null) {
            val bytes = uriToJpegBytes(context, uri)
            if (bytes != null) {
                onCaptured(bytes)
            }
        }
    }

    DisposableEffect(Unit) {
        permissionLauncher.launch(Manifest.permission.CAMERA)
        onDispose {
            cameraManager.unbind()
        }
    }

    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(Color.Black)
            .testTag("aiScreenCameraCapture"),
    ) {
        if (permissionDenied) {
            PermissionDeniedContent(
                onCancel = onCancel,
                onRetry = { permissionLauncher.launch(Manifest.permission.CAMERA) },
            )
        } else if (hasPermission) {
            // Camera preview
            AndroidView(
                factory = { ctx ->
                    PreviewView(ctx).also { previewView ->
                        cameraManager.bindToPreview(previewView)
                    }
                },
                modifier = Modifier.fillMaxSize(),
            )

            // Controls overlay
            CameraControls(
                isCapturing = isCapturing,
                onCapture = {
                    scope.launch {
                        isCapturing = true
                        try {
                            val bytes = cameraManager.capturePhoto()
                            onCaptured(bytes)
                        } catch (_: Exception) {
                            // State updated in CameraManager
                        } finally {
                            isCapturing = false
                        }
                    }
                },
                onCancel = onCancel,
                onGallery = {
                    galleryLauncher.launch(
                        PickVisualMediaRequest(ActivityResultContracts.PickVisualMedia.ImageOnly),
                    )
                },
                modifier = Modifier.align(Alignment.BottomCenter),
            )
        }
    }
}

@Composable
private fun CameraControls(
    isCapturing: Boolean,
    onCapture: () -> Unit,
    onCancel: () -> Unit,
    onGallery: () -> Unit,
    modifier: Modifier = Modifier,
) {
    Row(
        modifier = modifier
            .fillMaxWidth()
            .padding(horizontal = 32.dp, vertical = 40.dp),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically,
    ) {
        // Cancel button
        IconButton(
            onClick = onCancel,
            modifier = Modifier.size(48.dp),
        ) {
            Icon(
                imageVector = Icons.Default.Close,
                contentDescription = "Cancel",
                tint = Color.White,
                modifier = Modifier.size(28.dp),
            )
        }

        // Capture button
        IconButton(
            onClick = { if (!isCapturing) onCapture() },
            modifier = Modifier
                .size(72.dp)
                .border(3.dp, Color.White, CircleShape)
                .padding(4.dp)
                .clip(CircleShape)
                .background(if (isCapturing) Color.Gray else Color.White, CircleShape),
        ) {
            // Empty — solid circle is the shutter button
        }

        // Gallery button
        IconButton(
            onClick = onGallery,
            modifier = Modifier.size(48.dp),
        ) {
            Icon(
                imageVector = Icons.Default.PhotoLibrary,
                contentDescription = "Pick from gallery",
                tint = Color.White,
                modifier = Modifier.size(28.dp),
            )
        }
    }
}

@Composable
private fun PermissionDeniedContent(
    onCancel: () -> Unit,
    onRetry: () -> Unit,
) {
    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(32.dp),
        verticalArrangement = Arrangement.Center,
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Text(
            text = "Camera permission required",
            style = MaterialTheme.typography.titleMedium,
            color = Color.White,
        )
        Spacer(modifier = Modifier.height(8.dp))
        Text(
            text = "Allow camera access to take photos for visual search.",
            style = MaterialTheme.typography.bodyMedium,
            color = Color.White.copy(alpha = 0.7f),
        )
        Spacer(modifier = Modifier.height(24.dp))
        Button(onClick = onRetry) {
            Text("Grant Permission")
        }
        Spacer(modifier = Modifier.height(12.dp))
        Button(onClick = onCancel) {
            Text("Cancel")
        }
    }
}

private fun uriToJpegBytes(context: Context, uri: Uri): ByteArray? {
    return try {
        context.contentResolver.openInputStream(uri)?.use { inputStream ->
            val buffer = ByteArrayOutputStream()
            val data = ByteArray(4096)
            var bytesRead: Int
            while (inputStream.read(data).also { bytesRead = it } != -1) {
                buffer.write(data, 0, bytesRead)
            }
            buffer.toByteArray()
        }
    } catch (_: Exception) {
        null
    }
}
