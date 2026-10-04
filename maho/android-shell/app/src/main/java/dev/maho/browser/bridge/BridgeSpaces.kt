package dev.maho.browser.bridge

import dev.maho.browser.MahoBridge
import dev.maho.browser.models.CoreUpdate
import dev.maho.browser.models.ProfileId
import dev.maho.browser.models.ShellEvent
import dev.maho.browser.models.SpaceColor
import dev.maho.browser.models.SpaceId
import dev.maho.browser.models.SpaceViewModel
import dev.maho.browser.support.MahoJson

object BridgeSpaces {

    fun getSpaceViewModels(): List<SpaceViewModel> {
        val json = MahoBridge.getSpaceViewModels() ?: return emptyList()
        return try {
            MahoJson.instance.decodeFromString<List<SpaceViewModel>>(json)
        } catch (_: Exception) {
            emptyList()
        }
    }

    fun getActiveSpaceId(): SpaceId? {
        return MahoBridge.getActiveSpaceId()
    }

    fun createSpace(name: String, color: SpaceColor, profileId: ProfileId): List<CoreUpdate> =
        MahoBridge.sendEvent(ShellEvent.CreateSpace(name = name, color = color, profileId = profileId))

    fun deleteSpace(id: SpaceId): List<CoreUpdate> =
        MahoBridge.sendEvent(ShellEvent.DeleteSpace(spaceId = id))

    fun renameSpace(id: SpaceId, name: String): List<CoreUpdate> =
        MahoBridge.sendEvent(ShellEvent.RenameSpace(spaceId = id, name = name))

    fun recolorSpace(id: SpaceId, color: SpaceColor): List<CoreUpdate> =
        MahoBridge.sendEvent(ShellEvent.RecolorSpace(spaceId = id, color = color))

    fun reorderSpace(id: SpaceId, from: Int, to: Int): List<CoreUpdate> =
        MahoBridge.sendEvent(ShellEvent.ReorderSpace(spaceId = id, from = from, to = to))

    fun activateSpace(id: SpaceId): List<CoreUpdate> =
        MahoBridge.sendEvent(ShellEvent.ActivateSpace(spaceId = id))
}
