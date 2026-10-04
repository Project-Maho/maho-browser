package dev.maho.browser.support

import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import android.util.Log
import androidx.core.content.pm.PackageInfoCompat
import kotlinx.serialization.Serializable
import kotlinx.serialization.encodeToString
import kotlinx.serialization.json.Json
import java.io.File
import java.io.FileOutputStream
import java.security.MessageDigest
import java.util.UUID

@Serializable
data class AssetFileEntry(
    val size: Long = 0L,
    val sha256: String? = null,
    val modifiedTimestamp: Double? = null,
)

@Serializable
data class AssetManifest(
    val schemaVersion: Int = 1,
    val bundleVersion: String = "",
    val buildTimestamp: Double = 0.0,
    val files: Map<String, AssetFileEntry> = emptyMap(),
)

object AssetProvisioner {
    private const val TAG = "AssetProvisioner"
    const val PREFS_NAME = "maho_asset_prefs"
    const val KEY_STAGED_ASSET_VERSION = "staged_asset_version_code"

    val json = Json {
        ignoreUnknownKeys = true
        isLenient = true
        encodeDefaults = true
        prettyPrint = true
    }

    fun parseManifestFromAssets(context: Context): AssetManifest? {
        val candidates = listOf("asset-manifest.json", "web-ai/asset-manifest.json")
        for (candidate in candidates) {
            try {
                context.assets.open(candidate).use { input ->
                    val content = input.bufferedReader().use { it.readText() }
                    return json.decodeFromString<AssetManifest>(content)
                }
            } catch (_: Exception) {
            }
        }
        return null
    }

    fun parseManifestFromFile(dirOrFile: File): AssetManifest? {
        val manifestFile = when {
            dirOrFile.isFile && dirOrFile.exists() -> dirOrFile
            File(dirOrFile, "asset-manifest.json").exists() -> File(dirOrFile, "asset-manifest.json")
            File(dirOrFile, "web-ai/asset-manifest.json").exists() -> File(dirOrFile, "web-ai/asset-manifest.json")
            else -> return null
        }
        return try {
            json.decodeFromString<AssetManifest>(manifestFile.readText())
        } catch (e: Exception) {
            Log.w(TAG, "Failed to parse stored manifest from ${manifestFile.absolutePath}", e)
            null
        }
    }

    fun calculateSha256(file: File): String {
        val digest = MessageDigest.getInstance("SHA-256")
        file.inputStream().use { fis ->
            val buffer = ByteArray(8192)
            var read: Int
            while (fis.read(buffer).also { read = it } != -1) {
                digest.update(buffer, 0, read)
            }
        }
        return digest.digest().joinToString("") { "%02x".format(it) }
    }

    fun getStoredAssetVersion(context: Context): Long {
        val prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        return prefs.getLong(KEY_STAGED_ASSET_VERSION, -1L)
    }

    fun getAppVersionCode(context: Context): Long {
        return try {
            val pInfo = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                context.packageManager.getPackageInfo(context.packageName, PackageManager.PackageInfoFlags.of(0))
            } else {
                @Suppress("DEPRECATION")
                context.packageManager.getPackageInfo(context.packageName, 0)
            }
            PackageInfoCompat.getLongVersionCode(pInfo)
        } catch (e: Exception) {
            1L
        }
    }

    fun isProvisioningNeeded(context: Context, destDir: File = File(context.filesDir, "assets")): Boolean {
        if (!destDir.exists()) {
            return true
        }

        val bundleManifest = parseManifestFromAssets(context)
        if (bundleManifest != null) {
            val storedManifest = parseManifestFromFile(destDir) ?: return true
            if (bundleManifest.schemaVersion != storedManifest.schemaVersion) {
                return true
            }
            if (bundleManifest.bundleVersion != storedManifest.bundleVersion) {
                return true
            }
            if (bundleManifest.files != storedManifest.files) {
                return true
            }
            for ((relPath, entry) in bundleManifest.files) {
                val file = File(destDir, relPath)
                if (!file.exists() || !file.isFile) {
                    return true
                }
                if (entry.size > 0 && file.length() != entry.size) {
                    return true
                }
                if (!entry.sha256.isNullOrBlank()) {
                    val actualHash = try {
                        calculateSha256(file)
                    } catch (e: Exception) {
                        return true
                    }
                    if (!entry.sha256.equals(actualHash, ignoreCase = true)) {
                        return true
                    }
                }
            }
            return false
        }

        val storedVersion = getStoredAssetVersion(context)
        val appVersion = getAppVersionCode(context)
        if (storedVersion != appVersion) {
            return true
        }
        if (destDir.list().isNullOrEmpty()) {
            return true
        }
        return false
    }

    /**
     * Cold-start fast path:
     * Checks asset version / manifest against stored preference / manifest.
     * If version / manifest matches and assets exist, skips unpacking/scanning assets.
     *
     * @return true if provisioning/unpacking was executed, false if fast path skipped.
     */
    fun provisionAssetsIfNeeded(
        context: Context,
        force: Boolean = false,
        destDir: File = File(context.filesDir, "assets")
    ): Boolean {
        if (!force && !isProvisioningNeeded(context, destDir)) {
            Log.d(TAG, "Assets are up to date (version ${getAppVersionCode(context)}), fast-path skipping scan/unpack.")
            return false
        }

        val appVersion = getAppVersionCode(context)
        val parentDir = destDir.parentFile ?: context.filesDir
        if (!parentDir.exists()) {
            parentDir.mkdirs()
        }

        val stagingDir = File(parentDir, "assets_staging_${UUID.randomUUID()}")
        return try {
            stagingDir.mkdirs()
            unpackAssetsRecursively(context, "web-ai", File(stagingDir, "web-ai"))
            unpackAssetFile(context, "readability.js", File(stagingDir, "readability.js"))
            unpackAssetFile(context, "asset-manifest.json", File(stagingDir, "asset-manifest.json"))

            val bundleManifest = parseManifestFromAssets(context)
            if (bundleManifest != null) {
                val manifestFile = File(stagingDir, "asset-manifest.json")
                if (!manifestFile.exists()) {
                    manifestFile.writeText(json.encodeToString(bundleManifest))
                }
            }

            // Atomic swap
            if (destDir.exists()) {
                val backupDir = File(parentDir, "assets_backup_${UUID.randomUUID()}")
                if (destDir.renameTo(backupDir)) {
                    if (stagingDir.renameTo(destDir)) {
                        backupDir.deleteRecursively()
                    } else {
                        backupDir.renameTo(destDir)
                        throw IllegalStateException("Failed to swap staging directory to destDir")
                    }
                } else {
                    destDir.deleteRecursively()
                    if (!stagingDir.renameTo(destDir)) {
                        throw IllegalStateException("Failed to rename staging directory to destDir")
                    }
                }
            } else {
                if (!stagingDir.renameTo(destDir)) {
                    throw IllegalStateException("Failed to rename staging directory to destDir")
                }
            }

            // Update stored version in SharedPreferences asynchronously
            context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
                .edit()
                .putLong(KEY_STAGED_ASSET_VERSION, appVersion)
                .apply()

            Log.i(TAG, "Asset provisioning complete (version $appVersion) with atomic swap.")
            true
        } catch (e: Exception) {
            Log.e(TAG, "Asset provisioning failed", e)
            stagingDir.deleteRecursively()
            false
        }
    }

    private fun unpackAssetsRecursively(context: Context, assetPath: String, targetDir: File) {
        val list = context.assets.list(assetPath) ?: return
        if (list.isEmpty()) {
            unpackAssetFile(context, assetPath, targetDir)
            return
        }

        if (!targetDir.exists()) {
            targetDir.mkdirs()
        }

        for (item in list) {
            val subAssetPath = if (assetPath.isEmpty()) item else "$assetPath/$item"
            val subTarget = File(targetDir, item)
            val subList = context.assets.list(subAssetPath)
            if (subList != null && subList.isNotEmpty()) {
                unpackAssetsRecursively(context, subAssetPath, subTarget)
            } else {
                unpackAssetFile(context, subAssetPath, subTarget)
            }
        }
    }

    private fun unpackAssetFile(context: Context, assetPath: String, targetFile: File) {
        try {
            targetFile.parentFile?.mkdirs()
            context.assets.open(assetPath).use { input ->
                FileOutputStream(targetFile).use { output ->
                    input.copyTo(output)
                }
            }
        } catch (e: Exception) {
            // Asset might not exist or is a directory
        }
    }
}
