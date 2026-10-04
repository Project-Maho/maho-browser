package dev.maho.browser.integration

import android.app.Activity
import android.app.role.RoleManager
import android.content.Context
import android.content.SharedPreferences
import android.os.Build

object DefaultBrowserManager {

    private const val PREFS_NAME = "maho_onboarding_prefs"
    private const val KEY_DEFAULT_BROWSER_PROMPT_SHOWN = "default_browser_prompt_shown"

    fun isDefaultBrowser(context: Context): Boolean {
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            val roleManager = context.getSystemService(Context.ROLE_SERVICE) as? RoleManager
            roleManager?.isRoleHeld(RoleManager.ROLE_BROWSER) == true
        } else {
            val defaultBrowserPackage = getDefaultBrowserPackage(context)
            defaultBrowserPackage == context.packageName
        }
    }

    fun requestDefault(activity: Activity) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            val roleManager = activity.getSystemService(Context.ROLE_SERVICE) as? RoleManager
            if (roleManager != null && roleManager.isRoleAvailable(RoleManager.ROLE_BROWSER)) {
                val intent = roleManager.createRequestRoleIntent(RoleManager.ROLE_BROWSER)
                activity.startActivityForResult(intent, REQUEST_CODE_DEFAULT_BROWSER)
            }
        } else {
            val intent = android.content.Intent(android.provider.Settings.ACTION_MANAGE_DEFAULT_APPS_SETTINGS)
            activity.startActivity(intent)
        }
        markPromptShown(activity)
    }

    fun hasPromptBeenShown(context: Context): Boolean {
        return getPrefs(context).getBoolean(KEY_DEFAULT_BROWSER_PROMPT_SHOWN, false)
    }

    fun markPromptShown(context: Context) {
        getPrefs(context).edit().putBoolean(KEY_DEFAULT_BROWSER_PROMPT_SHOWN, true).apply()
    }

    private fun getPrefs(context: Context): SharedPreferences {
        return context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
    }

    @Suppress("DEPRECATION")
    private fun getDefaultBrowserPackage(context: Context): String? {
        val intent = android.content.Intent(android.content.Intent.ACTION_VIEW, android.net.Uri.parse("http://"))
        val resolveInfo = context.packageManager.resolveActivity(intent, android.content.pm.PackageManager.MATCH_DEFAULT_ONLY)
        return resolveInfo?.activityInfo?.packageName
    }

    const val REQUEST_CODE_DEFAULT_BROWSER = 1001
}
