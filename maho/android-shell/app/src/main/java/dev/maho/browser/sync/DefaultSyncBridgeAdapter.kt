package dev.maho.browser.sync

import dev.maho.browser.MahoBridge

class DefaultSyncBridgeAdapter : SyncBridgeAdapter {
    override fun applyRemoteEntities(json: String) {
        MahoBridge.applyRemoteEntities(json)
    }

    override fun drainOutgoing(): String? {
        return MahoBridge.drainOutgoing()
    }
}
