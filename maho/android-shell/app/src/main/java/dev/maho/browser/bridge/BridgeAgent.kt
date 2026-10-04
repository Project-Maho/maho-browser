package dev.maho.browser.bridge

import android.content.ClipData
import android.content.Intent
import androidx.core.content.FileProvider
import dev.maho.browser.MahoBridge
import dev.maho.browser.ui.webview.BrowserToolExecutor
import dev.maho.browser.ui.webview.agenticBrowsingToolDescriptorsJson
import java.io.File
import org.json.JSONArray
import org.json.JSONObject

object BridgeAgent {
    private fun artifactRoot(): String? {
        val context = MahoBridge.appContext ?: return null
        return File(context.filesDir, "agent-artifacts").also { it.mkdirs() }.absolutePath
    }

    fun agentCreateSession(sessionId: String): Long {
        val artifactRoot = artifactRoot()
        if (!MahoBridge.isInitialized || sessionId.isBlank() || artifactRoot == null) return 0L
        val handle = MahoBridge.invokeAgentCreateSession(MahoBridge.corePtr, sessionId, artifactRoot)
        if (handle == 0L) return 0L
        if (!nativeEnableAgenticBrowsing(handle)) {
            MahoBridge.invokeAgentFreeSession(handle)
            return 0L
        }
        return handle
    }

    fun agentFreeSession(sessionPtr: Long) {
        if (sessionPtr != 0L) MahoBridge.invokeAgentFreeSession(sessionPtr)
    }

    fun agentSendMessage(sessionPtr: Long, message: String): Boolean =
        sessionPtr != 0L && MahoBridge.invokeAgentSendMessage(sessionPtr, message)

    fun agentCancel(sessionPtr: Long): Boolean =
        sessionPtr != 0L && MahoBridge.invokeAgentCancel(sessionPtr)

    fun agentListTools(sessionPtr: Long): String? =
        if (sessionPtr == 0L) null else MahoBridge.invokeAgentListTools(sessionPtr)

    fun agentPollEvent(sessionPtr: Long): String? =
        if (sessionPtr == 0L) null else MahoBridge.invokeAgentPollEvent(sessionPtr)

    fun agentListArtifacts(sessionHandle: Long): String? =
        if (sessionHandle == 0L) null else MahoBridge.invokeAgentListArtifacts(sessionHandle)

    fun agentReadArtifact(sessionHandle: Long, artifactId: String): ByteArray? {
        if (sessionHandle == 0L || !isOpaqueArtifactId(artifactId)) return null
        return MahoBridge.invokeAgentReadArtifact(sessionHandle, artifactId)
    }

    @JvmStatic
    private external fun nativeEnableAgenticBrowsing(sessionHandle: Long): Boolean

    /** Entry point called from the Rust agent browser-tool bridge. */
    @JvmStatic
    fun invokeAgentBrowserTool(toolName: String, argumentsJson: String): String? {
        if (toolName == "tools/list") return agenticBrowsingToolDescriptorsJson()
        val args = runCatching { JSONObject(argumentsJson) }.getOrNull() ?: return null
        return BrowserToolExecutor().invoke(toolName, args).toString()
    }

    internal fun agentInstallDeterministicFsWriteModel(sessionHandle: Long): Boolean =
        sessionHandle != 0L && MahoBridge.invokeAgentInstallDeterministicFsWriteModel(sessionHandle)

    internal fun agentTestCompletionGeneration(sessionHandle: Long): Long =
        if (sessionHandle == 0L) -1L else MahoBridge.invokeAgentTestCompletionGeneration(sessionHandle)

    internal fun agentTestAwaitCompletion(sessionHandle: Long, generation: Long, timeoutMs: Long): Boolean =
        sessionHandle != 0L && MahoBridge.invokeAgentTestAwaitCompletion(sessionHandle, generation, timeoutMs)

    internal fun agentTestEnqueueArtifactCreatedTwice(sessionHandle: Long, artifactId: String): Boolean =
        sessionHandle != 0L && MahoBridge.invokeAgentTestEnqueueArtifactCreatedTwice(sessionHandle, artifactId)

    internal fun agentTestSeedArtifact(
        sessionHandle: Long,
        artifactId: String,
        storageRelPath: String,
        displayName: String,
        mimeType: String,
    ): Boolean = sessionHandle != 0L && MahoBridge.invokeAgentTestSeedArtifact(
        sessionHandle,
        artifactId,
        storageRelPath,
        displayName,
        mimeType,
    )

    fun artifactShare(sessionHandle: Long, artifactId: String): Boolean {
        val context = MahoBridge.appContext ?: return false
        if (!isOpaqueArtifactId(artifactId)) return false
        val artifacts = runCatching { JSONArray(agentListArtifacts(sessionHandle) ?: return false) }.getOrNull()
            ?: return false
        var displayName: String? = null
        var mimeType = "application/octet-stream"
        for (index in 0 until artifacts.length()) {
            val artifact = artifacts.optJSONObject(index) ?: continue
            if (artifact.optString("artifactId") == artifactId) {
                displayName = artifact.optString("displayName")
                mimeType = artifact.optString("mimeType").ifBlank { mimeType }
                break
            }
        }
        val safeName = shareFileName(displayName ?: return false, artifactId)
        val bytes = agentReadArtifact(sessionHandle, artifactId) ?: return false
        val exportDir = File(context.cacheDir, "artifact_exports").also { it.mkdirs() }
        val exportFile = File(exportDir, "${artifactId.take(32)}-$safeName")
        var temporary: File? = null
        return try {
            temporary = File.createTempFile("artifact-", ".tmp", exportDir)
            temporary.outputStream().use { it.write(bytes) }
            if (!temporary.renameTo(exportFile)) return false
            temporary = null
            val uri = FileProvider.getUriForFile(context, "${context.packageName}.artifact-files", exportFile)
            val shareIntent = Intent(Intent.ACTION_SEND).apply {
                type = mimeType
                putExtra(Intent.EXTRA_STREAM, uri)
                clipData = ClipData.newUri(context.contentResolver, safeName, uri)
                addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_ACTIVITY_NEW_TASK)
            }
            val chooser = Intent.createChooser(shareIntent, null).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            val launched = MahoBridge.artifactShareLauncher?.invoke(context, chooser) ?: run {
                if (chooser.resolveActivity(context.packageManager) == null) false
                else {
                    context.startActivity(chooser)
                    true
                }
            }
            if (!launched) exportFile.delete()
            launched
        } catch (_: Exception) {
            temporary?.delete()
            exportFile.delete()
            false
        }
    }

    private fun isOpaqueArtifactId(artifactId: String): Boolean =
        artifactId.isNotEmpty() && artifactId.length <= 128 && artifactId.all {
            it.isLetterOrDigit() || it == '-' || it == '_' || it == '.'
        } && artifactId != "." && artifactId != ".."

    private fun shareFileName(displayName: String, artifactId: String): String {
        val sanitized = displayName
            .substringAfterLast('/')
            .substringAfterLast('\\')
            .filter { it.code in 32..126 && it != ':' }
            .trim()
            .take(120)
        return sanitized.takeIf { it.isNotEmpty() && it != "." && it != ".." }
            ?: "artifact-${artifactId.take(12)}"
    }
}