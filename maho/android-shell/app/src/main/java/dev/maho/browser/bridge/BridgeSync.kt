package dev.maho.browser.bridge

import dev.maho.browser.MahoBridge
import dev.maho.browser.models.ConnectedDevice
import dev.maho.browser.models.CoreUpdate
import dev.maho.browser.models.ShellEvent
import dev.maho.browser.sync.RelaySessionStore
import dev.maho.browser.sync.RelaySyncBootstrap
import dev.maho.browser.sync.SyncStateResponse
import dev.maho.browser.sync.SyncStateResult
import dev.maho.browser.support.MahoJson
import kotlinx.serialization.json.contentOrNull
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive

object BridgeSync {

    /**
     * Reads the structured sync state from core.
     *
     * A malformed payload is reported as [SyncStateResult.Invalid] rather than
     * being swallowed, so the UI can show a real error instead of a false idle.
     */
    fun getSyncState(): SyncStateResult {
        val json = MahoBridge.getSyncStatus() ?: return SyncStateResult.Unavailable
        return decodeSyncState(json)
    }

    internal fun decodeSyncState(json: String): SyncStateResult {
        if (json.isBlank()) return SyncStateResult.Unavailable
        return try {
            SyncStateResult.Available(MahoJson.instance.decodeFromString<SyncStateResponse>(json))
        } catch (error: Exception) {
            SyncStateResult.Invalid(error.message ?: "Malformed sync state payload")
        }
    }

    fun getConnectedDevices(): List<ConnectedDevice> {
        val json = MahoBridge.getConnectedDevices() ?: return emptyList()
        return try {
            MahoJson.instance.decodeFromString<List<ConnectedDevice>>(json)
        } catch (_: Exception) {
            emptyList()
        }
    }

    fun getCurrentDeviceId(): String? {
        return RelaySessionStore.load()?.device?.id
    }

    fun generateSyncKey(): String? {
        return MahoBridge.generateSyncKey()
    }

    fun generateSyncBootstrap(): String? {
        return MahoBridge.generateSyncBootstrap()
    }

    internal fun parseGeneratedSyncBootstrap(json: String?): RelaySyncBootstrap? {
        val value = json?.takeIf(String::isNotBlank) ?: return null
        return runCatching {
            val payload = MahoJson.instance.parseToJsonElement(value).jsonObject
            val version = payload["version"]?.jsonPrimitive?.content?.toInt() ?: return null
            val roomId = payload["roomId"]?.jsonPrimitive?.contentOrNull ?: return null
            val seed = payload["seed"]?.jsonPrimitive?.contentOrNull ?: return null
            RelaySyncBootstrap(version, roomId, seed, 0)
        }.getOrNull()
    }

    fun getSyncRoomId(): String? {
        return MahoBridge.getSyncRoomId()
    }

    fun signIn(
        email: String,
        displayName: String? = null,
        accessToken: String? = null,
        userId: String? = null,
        deviceId: String? = null,
    ): List<CoreUpdate> {
        return runCatching {
            MahoBridge.sendEvent(
                ShellEvent.SignIn(
                    email = email,
                    displayName = displayName,
                    accessToken = accessToken,
                    userId = userId,
                    deviceId = deviceId,
                )
            )
        }.getOrDefault(emptyList())
    }

    fun signOut(): List<CoreUpdate> {
        return runCatching {
            MahoBridge.sendEvent(ShellEvent.SignOut)
        }.getOrDefault(emptyList())
    }

    fun toggleSync(): List<CoreUpdate> {
        return MahoBridge.sendEvent(ShellEvent.ToggleSync)
    }

    fun sendTabToDevice(url: String, title: String, deviceId: String) {
        MahoBridge.sendTab(url = url, title = title, targetDeviceId = deviceId)
    }

    fun joinSync(recoveryPhrase: String): String? {
        return MahoBridge.joinSync(recoveryPhrase)
    }

    fun configureSyncBootstrap(serverUrl: String, bootstrapSeed: String): String? {
        return MahoBridge.configureSyncBootstrap(serverUrl, bootstrapSeed)
    }

    internal fun bootstrapConfigured(result: String?): Boolean {
        val value = result?.takeIf(String::isNotBlank) ?: return false
        return runCatching {
            MahoJson.instance.parseToJsonElement(value)
                .jsonObject["success"]
                ?.jsonPrimitive
                ?.content == "true"
        }.getOrDefault(false)
    }

    fun removeSyncDevice(deviceId: String) {
        MahoBridge.removeSyncDevice(deviceId)
    }
}
