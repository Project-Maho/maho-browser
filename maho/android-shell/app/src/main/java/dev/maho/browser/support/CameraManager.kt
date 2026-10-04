package dev.maho.browser.support

import android.content.Context
import androidx.camera.core.CameraSelector
import androidx.camera.core.ImageCapture
import androidx.camera.core.ImageCaptureException
import androidx.camera.core.ImageProxy
import androidx.camera.core.Preview
import androidx.camera.lifecycle.ProcessCameraProvider
import androidx.camera.view.PreviewView
import androidx.core.content.ContextCompat
import androidx.lifecycle.LifecycleOwner
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import java.io.ByteArrayOutputStream
import kotlin.coroutines.resume
import kotlin.coroutines.resumeWithException
import kotlin.coroutines.suspendCoroutine

class CameraManager(
    private val context: Context,
    private val lifecycleOwner: LifecycleOwner,
) {
    sealed class State {
        data object Idle : State()
        data object Preparing : State()
        data object Capturing : State()
        data class Captured(val bytes: ByteArray) : State()
        data class Error(val message: String) : State()
    }

    private val _state = MutableStateFlow<State>(State.Idle)
    val state: StateFlow<State> = _state.asStateFlow()

    private var cameraProvider: ProcessCameraProvider? = null
    private var imageCapture: ImageCapture? = null
    private var preview: Preview? = null

    fun bindToPreview(previewView: PreviewView) {
        _state.value = State.Preparing

        val cameraProviderFuture = ProcessCameraProvider.getInstance(context)
        cameraProviderFuture.addListener({
            try {
                val provider = cameraProviderFuture.get()
                cameraProvider = provider

                preview = Preview.Builder().build().also {
                    it.setSurfaceProvider(previewView.surfaceProvider)
                }

                imageCapture = ImageCapture.Builder()
                    .setCaptureMode(ImageCapture.CAPTURE_MODE_MINIMIZE_LATENCY)
                    .setJpegQuality(85)
                    .build()

                provider.unbindAll()
                provider.bindToLifecycle(
                    lifecycleOwner,
                    CameraSelector.DEFAULT_BACK_CAMERA,
                    preview,
                    imageCapture,
                )

                _state.value = State.Idle
            } catch (e: Exception) {
                _state.value = State.Error("Camera initialization failed: ${e.message}")
            }
        }, ContextCompat.getMainExecutor(context))
    }

    suspend fun capturePhoto(): ByteArray {
        val capture = imageCapture
            ?: throw IllegalStateException("Camera not initialized")

        _state.value = State.Capturing

        return suspendCoroutine { continuation ->
            capture.takePicture(
                ContextCompat.getMainExecutor(context),
                object : ImageCapture.OnImageCapturedCallback() {
                    override fun onCaptureSuccess(image: ImageProxy) {
                        try {
                            val bytes = imageProxyToJpegBytes(image)
                            image.close()
                            _state.value = State.Captured(bytes)
                            continuation.resume(bytes)
                        } catch (e: Exception) {
                            image.close()
                            _state.value = State.Error("Failed to process image: ${e.message}")
                            continuation.resumeWithException(e)
                        }
                    }

                    override fun onError(exception: ImageCaptureException) {
                        _state.value = State.Error("Capture failed: ${exception.message}")
                        continuation.resumeWithException(exception)
                    }
                },
            )
        }
    }

    fun reset() {
        _state.value = State.Idle
    }

    fun unbind() {
        cameraProvider?.unbindAll()
        cameraProvider = null
        imageCapture = null
        preview = null
        _state.value = State.Idle
    }

    private fun imageProxyToJpegBytes(image: ImageProxy): ByteArray {
        val buffer = image.planes[0].buffer
        val bytes = ByteArray(buffer.remaining())
        buffer.get(bytes)

        // ImageProxy from takePicture with OnImageCapturedCallback returns JPEG directly
        // when the capture mode uses JPEG output format (default)
        return bytes
    }
}
