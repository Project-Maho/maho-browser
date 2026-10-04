package dev.maho.browser

import android.content.Context
import dev.maho.browser.support.AssetFileEntry
import dev.maho.browser.support.AssetManifest
import dev.maho.browser.support.AssetProvisioner
import kotlinx.serialization.encodeToString
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config
import java.io.File

@RunWith(RobolectricTestRunner::class)
@Config(application = android.app.Application::class)
class AssetProvisionerTest {
    private val context: Context = RuntimeEnvironment.getApplication()
    private lateinit var testDestDir: File

    @Before
    fun setUp() {
        testDestDir = File(context.cacheDir, "test_assets_${System.currentTimeMillis()}").also {
            it.mkdirs()
        }
        context.getSharedPreferences(AssetProvisioner.PREFS_NAME, Context.MODE_PRIVATE)
            .edit()
            .clear()
            .commit()
    }

    @After
    fun tearDown() {
        testDestDir.deleteRecursively()
        context.getSharedPreferences(AssetProvisioner.PREFS_NAME, Context.MODE_PRIVATE)
            .edit()
            .clear()
            .commit()
    }

    @Test
    fun `initial provisioning unpacks assets and updates stored version`() {
        assertTrue(AssetProvisioner.isProvisioningNeeded(context, testDestDir))

        val provisioned = AssetProvisioner.provisionAssetsIfNeeded(context, force = false, destDir = testDestDir)
        assertTrue(provisioned)

        val appVersion = AssetProvisioner.getAppVersionCode(context)
        val storedVersion = AssetProvisioner.getStoredAssetVersion(context)
        assertEquals(appVersion, storedVersion)
        assertTrue(testDestDir.exists())
    }

    @Test
    fun `cold start fast path skips unpacking when version matches and assets exist`() {
        // First pass
        val initial = AssetProvisioner.provisionAssetsIfNeeded(context, force = false, destDir = testDestDir)
        assertTrue(initial)

        // Ensure fake asset file exists in destDir to simulate populated folder
        val marker = File(testDestDir, "marker.txt").also { it.writeText("ok") }
        assertTrue(marker.exists())

        // Fast path check
        assertFalse(AssetProvisioner.isProvisioningNeeded(context, testDestDir))

        // Second pass: fast path must return false without doing work
        val second = AssetProvisioner.provisionAssetsIfNeeded(context, force = false, destDir = testDestDir)
        assertFalse(second)
    }

    @Test
    fun `force provisioning bypasses fast path`() {
        // Setup initial provisioned state
        AssetProvisioner.provisionAssetsIfNeeded(context, force = false, destDir = testDestDir)
        File(testDestDir, "marker.txt").writeText("ok")

        // Force provision
        val forced = AssetProvisioner.provisionAssetsIfNeeded(context, force = true, destDir = testDestDir)
        assertTrue(forced)
    }

    @Test
    fun `re-provisions when stored version is outdated`() {
        // Store an outdated version
        context.getSharedPreferences(AssetProvisioner.PREFS_NAME, Context.MODE_PRIVATE)
            .edit()
            .putLong(AssetProvisioner.KEY_STAGED_ASSET_VERSION, 0L)
            .commit()

        File(testDestDir, "old.txt").writeText("old")
        assertTrue(AssetProvisioner.isProvisioningNeeded(context, testDestDir))

        val result = AssetProvisioner.provisionAssetsIfNeeded(context, force = false, destDir = testDestDir)
        assertTrue(result)

        val appVersion = AssetProvisioner.getAppVersionCode(context)
        val storedVersion = AssetProvisioner.getStoredAssetVersion(context)
        assertEquals(appVersion, storedVersion)
    }

    @Test
    fun `re-provisions when destination directory is missing even if preference version matches`() {
        val appVersion = AssetProvisioner.getAppVersionCode(context)
        context.getSharedPreferences(AssetProvisioner.PREFS_NAME, Context.MODE_PRIVATE)
            .edit()
            .putLong(AssetProvisioner.KEY_STAGED_ASSET_VERSION, appVersion)
            .commit()

        // Delete destDir
        testDestDir.deleteRecursively()
        assertFalse(testDestDir.exists())

        // Missing destDir requires provisioning
        assertTrue(AssetProvisioner.isProvisioningNeeded(context, testDestDir))

        val result = AssetProvisioner.provisionAssetsIfNeeded(context, force = false, destDir = testDestDir)
        assertTrue(result)
        assertTrue(testDestDir.exists())
    }

    @Test
    fun `asset manifest serialization round trip`() {
        val manifest = AssetManifest(
            schemaVersion = 1,
            bundleVersion = "2.0.0",
            buildTimestamp = 1700000000.0,
            files = mapOf(
                "web-ai/index.html" to AssetFileEntry(size = 1024, sha256 = "abc123hash", modifiedTimestamp = 1700000000.0),
                "readability.js" to AssetFileEntry(size = 90000, sha256 = "def456hash", modifiedTimestamp = 1700000000.0)
            )
        )

        val encoded = AssetProvisioner.json.encodeToString(manifest)
        val decoded = AssetProvisioner.json.decodeFromString<AssetManifest>(encoded)

        assertEquals(1, decoded.schemaVersion)
        assertEquals("2.0.0", decoded.bundleVersion)
        assertEquals(1700000000.0, decoded.buildTimestamp, 0.001)
        assertEquals(2, decoded.files.size)
        assertEquals(1024L, decoded.files["web-ai/index.html"]?.size)
        assertEquals("abc123hash", decoded.files["web-ai/index.html"]?.sha256)
    }

    @Test
    fun `sha256 calculation matches expected digest`() {
        val testFile = File(testDestDir, "hash_test.txt").also {
            it.writeText("hello world\n")
        }
        val sha256 = AssetProvisioner.calculateSha256(testFile)
        assertEquals("a948904f2f0f479b8f8197694b30184b0d2ed1c1cd2a1ec0fb85d299a192a447", sha256.lowercase())
    }

    @Test
    fun `manifest file parsing correctly loads stored manifest`() {
        File(testDestDir, "asset-manifest.json").also {
            it.writeText(
                """
                {
                    "schemaVersion": 1,
                    "bundleVersion": "1.5.0",
                    "buildTimestamp": 1700000000.0,
                    "files": {
                        "web-ai/ai-bundle.js": {
                            "size": 5000,
                            "sha256": "abcdef"
                        }
                    }
                }
                """.trimIndent()
            )
        }

        val parsed = AssetProvisioner.parseManifestFromFile(testDestDir)
        assertNotNull(parsed)
        assertEquals("1.5.0", parsed?.bundleVersion)
        assertEquals(1, parsed?.schemaVersion)
        assertEquals(1, parsed?.files?.size)
        assertEquals(5000L, parsed?.files?.get("web-ai/ai-bundle.js")?.size)
        assertEquals("abcdef", parsed?.files?.get("web-ai/ai-bundle.js")?.sha256)
    }

    @Test
    fun `parseManifestFromFile returns null for non-existent manifest`() {
        val emptyDir = File(testDestDir, "empty_dir").also { it.mkdirs() }
        val parsed = AssetProvisioner.parseManifestFromFile(emptyDir)
        assertNull(parsed)
    }

    @Test
    fun `corrupted sha256 digest in destination causes reprovisioning`() {
        // Initial provisioning copies files and places asset-manifest.json
        val initial = AssetProvisioner.provisionAssetsIfNeeded(context, force = false, destDir = testDestDir)
        assertTrue(initial)
        assertFalse(AssetProvisioner.isProvisioningNeeded(context, testDestDir))

        // Target an existing provisioned file and corrupt its content
        val targetFile = File(testDestDir, "web-ai/index.html")
        assertTrue("Expected web-ai/index.html to exist in testDestDir", targetFile.exists())
        val originalContent = targetFile.readText()
        val originalHash = AssetProvisioner.calculateSha256(targetFile)

        // Corrupt the destination file
        targetFile.writeText("<!-- corrupted content with mismatching sha256 digest -->")
        val corruptedHash = AssetProvisioner.calculateSha256(targetFile)
        org.junit.Assert.assertNotEquals(originalHash, corruptedHash)

        // Verifying that corrupted SHA-256 digest causes isProvisioningNeeded to return true
        assertTrue("Corrupted SHA-256 digest in destination must trigger reprovisioning", AssetProvisioner.isProvisioningNeeded(context, testDestDir))

        // Re-provisioning must execute and repair the corrupted destination file
        val reprovisioned = AssetProvisioner.provisionAssetsIfNeeded(context, force = false, destDir = testDestDir)
        assertTrue("Reprovisioning must execute to heal corrupted destination file", reprovisioned)

        // Destination must now be healed and valid
        assertFalse("Provisioning must no longer be needed after healing", AssetProvisioner.isProvisioningNeeded(context, testDestDir))
        val restoredHash = AssetProvisioner.calculateSha256(targetFile)
        assertEquals(originalHash, restoredHash)
        assertEquals(originalContent, targetFile.readText())
    }
}
