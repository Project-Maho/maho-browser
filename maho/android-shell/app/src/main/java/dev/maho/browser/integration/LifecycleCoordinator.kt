package dev.maho.browser.integration

import android.os.Bundle
import androidx.lifecycle.DefaultLifecycleObserver
import androidx.lifecycle.LifecycleOwner
import androidx.lifecycle.ProcessLifecycleOwner
import dev.maho.browser.MahoBridge

object LifecycleCoordinator : DefaultLifecycleObserver {

    private const val KEY_ACTIVE_SPACE = "active_space_id"
    private const val KEY_ACTIVE_TAB = "active_tab_id"

    @Volatile
    private var isObserving = false

    fun start() {
        if (isObserving) return
        isObserving = true
        ProcessLifecycleOwner.get().lifecycle.addObserver(this)
    }

    fun stop() {
        if (!isObserving) return
        isObserving = false
        ProcessLifecycleOwner.get().lifecycle.removeObserver(this)
    }

    override fun onStart(owner: LifecycleOwner) {
        MahoBridge.loadState()
    }

    override fun onStop(owner: LifecycleOwner) {
        MahoBridge.saveState()
    }

    fun saveInstanceState(outState: Bundle) {
        val activeSpace = MahoBridge.getActiveSpaceId()
        outState.putString(KEY_ACTIVE_SPACE, activeSpace)
    }

    fun restoreInstanceState(savedInstanceState: Bundle?) {
        savedInstanceState ?: return
        val activeSpace = savedInstanceState.getString(KEY_ACTIVE_SPACE)
        if (activeSpace != null) {
            MahoBridge.activateSpace(activeSpace)
        }
    }
}
