package dev.maho.browser.bridge

import dev.maho.browser.MahoBridge
import dev.maho.browser.models.CoreUpdate
import dev.maho.browser.models.ShellEvent
import dev.maho.browser.support.MahoJson

object BridgeContentBlocker {

    data class ContentBlockerState(
        val enabled: Boolean,
        val popupBlocking: Boolean,
        val ruleCount: Long,
    )

    fun getState(): ContentBlockerState {
        val ruleCount = MahoBridge.getContentRuleCount()
        val settingsJson = MahoBridge.getSettings()
        val settings = settingsJson?.let {
            MahoJson.instance.decodeFromString<dev.maho.browser.models.Settings>(it)
        }
        return ContentBlockerState(
            enabled = settings?.privacy?.isNativeBlockingEnabled ?: false,
            popupBlocking = settings?.privacy?.popupBlockerEnabled ?: false,
            ruleCount = ruleCount,
        )
    }

    fun getCompiledRulesJson(): String? = MahoBridge.getContentRules()

    fun getFilterListsJson(): String? = MahoBridge.getFilterLists()

    fun toggleContentBlocker(enabled: Boolean): List<CoreUpdate> =
        MahoBridge.sendEvent(ShellEvent.ToggleContentBlocker(enabled = enabled))

    fun togglePopupBlocking(enabled: Boolean): List<CoreUpdate> =
        MahoBridge.sendEvent(ShellEvent.TogglePopupBlocking(enabled = enabled))

    fun addFilterList(id: String, name: String, url: String): List<CoreUpdate> =
        MahoBridge.sendEvent(ShellEvent.AddFilterList(id = id, name = name, url = url))

    fun removeFilterList(id: String): List<CoreUpdate> =
        MahoBridge.sendEvent(ShellEvent.RemoveFilterList(id = id))

    fun toggleFilterList(id: String, enabled: Boolean): List<CoreUpdate> =
        MahoBridge.sendEvent(ShellEvent.ToggleFilterList(id = id, enabled = enabled))

    fun rebuildRules() {
        MahoBridge.rebuildContentRules()
    }

    fun isContentBlockerUpdate(update: CoreUpdate): Boolean = when (update) {
        is CoreUpdate.ContentRulesCompiled -> true
        is CoreUpdate.ContentBlockerStateChanged -> true
        is CoreUpdate.FilterListUpdated -> true
        else -> false
    }

    fun snapshotFrom(update: CoreUpdate.ContentBlockerStateChanged): ContentBlockerState =
        ContentBlockerState(
            enabled = update.enabled,
            popupBlocking = update.popupBlocking,
            ruleCount = MahoBridge.getContentRuleCount(),
        )

    // === Air Traffic Control ===

    fun getAtcRules(): List<dev.maho.browser.models.TrafficRule> {
        val json = MahoBridge.getAtcRules() ?: return emptyList()
        return try {
            MahoJson.instance.decodeFromString<List<dev.maho.browser.models.TrafficRule>>(json)
        } catch (_: Exception) {
            emptyList()
        }
    }

    fun addAtcRule(rule: dev.maho.browser.models.TrafficRule): String {
        val ruleJson = MahoJson.instance.encodeToString(dev.maho.browser.models.TrafficRule.serializer(), rule)
        return MahoBridge.addAtcRule(ruleJson) ?: ""
    }

    fun removeAtcRule(id: String): Boolean {
        return MahoBridge.removeAtcRule(id) != null
    }

    fun toggleAtcRule(id: String, enabled: Boolean) {
        MahoBridge.toggleAtcRule(id, enabled)
    }

    fun updateFilterListContent(id: String, content: String) {
        MahoBridge.updateFilterListContent(id, content)
    }
}
