package dev.maho.browser

import android.app.Activity
import android.util.Log
import androidx.activity.ComponentActivity
import androidx.activity.result.ActivityResultLauncher
import androidx.activity.result.IntentSenderRequest
import androidx.activity.result.contract.ActivityResultContracts
import com.google.android.play.core.appupdate.AppUpdateInfo
import com.google.android.play.core.appupdate.AppUpdateManager
import com.google.android.play.core.appupdate.AppUpdateManagerFactory
import com.google.android.play.core.appupdate.AppUpdateOptions
import com.google.android.play.core.install.InstallStateUpdatedListener
import com.google.android.play.core.install.model.AppUpdateType
import com.google.android.play.core.install.model.InstallStatus
import com.google.android.play.core.install.model.UpdateAvailability

class MahoAppUpdateManager(private val activity: ComponentActivity) {

    private val enabled = false

    private val appUpdateManager: AppUpdateManager = AppUpdateManagerFactory.create(activity)
    private var updateResultLauncher: ActivityResultLauncher<IntentSenderRequest>? = null

    private val installStateListener = InstallStateUpdatedListener { state ->
        if (state.installStatus() == InstallStatus.DOWNLOADED) {
            Log.i(TAG, "Update downloaded, completing update")
            appUpdateManager.completeUpdate()
        }
    }

    fun register() {
        updateResultLauncher = activity.registerForActivityResult(
            ActivityResultContracts.StartIntentSenderForResult()
        ) { result ->
            if (result.resultCode != Activity.RESULT_OK) {
                Log.w(TAG, "Update flow cancelled or failed: ${result.resultCode}")
            }
        }
        appUpdateManager.registerListener(installStateListener)
    }

    fun checkForUpdate() {
        if (!enabled) return
        appUpdateManager.appUpdateInfo.addOnSuccessListener { info ->
            if (info.updateAvailability() == UpdateAvailability.UPDATE_AVAILABLE &&
                info.isUpdateTypeAllowed(AppUpdateType.FLEXIBLE)
            ) {
                Log.i(TAG, "Update available: ${info.availableVersionCode()}")
                startUpdate(info)
            } else if (info.installStatus() == InstallStatus.DOWNLOADED) {
                appUpdateManager.completeUpdate()
            }
        }.addOnFailureListener { e ->
            Log.w(TAG, "Update check failed", e)
        }
    }

    private fun startUpdate(info: AppUpdateInfo) {
        val launcher = updateResultLauncher ?: return
        appUpdateManager.startUpdateFlowForResult(
            info,
            launcher,
            AppUpdateOptions.defaultOptions(AppUpdateType.FLEXIBLE)
        )
    }

    fun unregister() {
        appUpdateManager.unregisterListener(installStateListener)
    }

    companion object {
        private const val TAG = "MahoAppUpdate"
    }
}
