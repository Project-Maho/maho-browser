package dev.maho.browser.bridge

import dev.maho.browser.MahoBridge
import dev.maho.browser.models.CoreUpdate
import dev.maho.browser.models.ProfileConfig
import dev.maho.browser.models.ShellEvent
import dev.maho.browser.support.MahoJson

object BridgeProfiles {

    fun getAllProfiles(): List<ProfileConfig> {
        val json = MahoBridge.listProfiles() ?: return emptyList()
        return try {
            MahoJson.instance.decodeFromString<List<ProfileConfig>>(json)
        } catch (_: Exception) {
            emptyList()
        }
    }

    fun getActiveProfile(): String? {
        return MahoBridge.getActiveProfileId()
    }

    fun createNewProfile(name: String): List<CoreUpdate> {
        return MahoBridge.sendEvent(ShellEvent.CreateProfile(name = name))
    }

    fun updateExistingProfile(
        profileId: String,
        name: String? = null,
        avatarColor: String? = null,
        downloadPath: String? = null,
    ): List<CoreUpdate> {
        return MahoBridge.sendEvent(
            ShellEvent.UpdateProfile(
                profileId = profileId,
                name = name,
                avatarColor = avatarColor,
                downloadPath = downloadPath,
            ),
        )
    }

    fun deleteExistingProfile(profileId: String): List<CoreUpdate> {
        return MahoBridge.sendEvent(ShellEvent.DeleteProfile(profileId = profileId))
    }

    fun switchToProfile(profileId: String): Boolean {
        return MahoBridge.switchProfile(profileId)
    }
}
