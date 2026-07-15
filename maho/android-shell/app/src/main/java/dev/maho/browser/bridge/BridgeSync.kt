package dev.maho.browser.bridge

import dev.maho.browser.MahoBridge
import dev.maho.browser.models.ConnectedDevice
import dev.maho.browser.models.CoreUpdate
import dev.maho.browser.models.ShellEvent
import dev.maho.browser.models.SyncStatusResponse
import dev.maho.browser.sync.RelaySessionStore
import dev.maho.browser.support.MahoJson

object BridgeSync {

    fun getSyncStatus(): SyncStatusResponse? {
        val json = MahoBridge.getSyncStatus() ?: return null
        return try {
            MahoJson.instance.decodeFromString<SyncStatusResponse>(json)
        } catch (_: Exception) {
            null
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

    fun removeSyncDevice(deviceId: String) {
        MahoBridge.removeSyncDevice(deviceId)
    }
}
