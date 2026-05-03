package dev.maho.browser.sync

interface SyncBridgeAdapter {
    fun applyRemoteEntities(json: String)
    fun drainOutgoing(): String?
}
