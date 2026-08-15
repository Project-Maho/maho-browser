package dev.maho.browser.ui.webview

import android.content.Context
import android.os.Bundle
import android.os.VibrationEffect
import android.os.Vibrator
import android.os.VibratorManager
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.webkit.JavascriptInterface
import androidx.fragment.app.Fragment

/**
 * AIWebViewFragment — hosts the web-ai bundle in a WebView for AI screens.
 *
 * Usage (from Compose via AndroidView or FragmentContainerView):
 *
 *   val fragment = AIWebViewFragment.newInstance(screen = "byok")
 *   supportFragmentManager.beginTransaction()
 *       .replace(R.id.ai_container, fragment)
 *       .commit()
 *
 * The screen is selected via the URL hash:
 *   file:///android_asset/web-ai/index.html#byok
 *   file:///android_asset/web-ai/index.html#chat?sessionId=abc
 *
 * Feature flag:
 *   Enable via BuildConfig.WEB_AI_BYOK_ENABLED (etc.) — see FeatureFlags.kt.
 *   When the flag is off, callers fall back to the existing native Compose screen.
 *
 * Dev mode:
 *   Set MAHO_WEB_AI_DEV=1 in local.properties to load from the Vite dev server
 *   at http://10.0.2.2:5173 (Android emulator host alias).
 */
class AIWebViewFragment : Fragment() {

    companion object {
        private const val ARG_SCREEN = "screen"
        private const val ARG_PARAMS = "params"

        /**
         * @param screen  Hash fragment name, e.g. "byok", "chat", "conversations".
         * @param params  Optional query string to append, e.g. "sessionId=abc".
         */
        fun newInstance(screen: String, params: String = ""): AIWebViewFragment =
            AIWebViewFragment().apply {
                arguments = Bundle().apply {
                    putString(ARG_SCREEN, screen)
                    putString(ARG_PARAMS, params)
                }
            }
    }

    private var host: AiWebViewHost? = null

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?,
    ): View {
        val screen = arguments?.getString(ARG_SCREEN) ?: "byok"
        val params = arguments?.getString(ARG_PARAMS) ?: ""

        val aiHost = AiWebViewFactory.createHost(requireContext(), screen, params)
        this.host = aiHost
        return aiHost.webView
    }

    override fun onDestroyView() {
        host?.destroy()
        host = null
        super.onDestroyView()
    }

    fun onBackPressed(): Boolean {
        return false
    }
}

/**
 * NativeActionBridge — handles the small set of native-only RPC calls that
 * cannot be serviced by WebViewBridge (camera, haptics, screenshot, settings).
 *
 * Calls arrive from JS as:
 *   window.MahoBridgeNative.rpc(jsonRpcRequestString)
 *
 * and are resolved via window.__mahoBridgeResponse(responseJson).
 */
internal class NativeActionBridge(
    private val context: Context,
    private val evaluateJs: (String) -> Unit,
) {
    @JavascriptInterface
    fun rpc(requestJson: String) {
        val req = runCatching { org.json.JSONObject(requestJson) }.getOrNull() ?: return
        val id: Any = if (req.has("id") && !req.isNull("id")) req.get("id") else org.json.JSONObject.NULL
        val method = req.optString("method", "")

        when (method) {
            "hapticFeedback" -> {
                val style = hapticStyle(req.opt("params"))
                vibrate(style)
                respond(id, true)
            }
            "openSettings" -> {
                // Signal to the host Activity to open the settings screen
                evaluateJs("window.__mahoNativeEvent && window.__mahoNativeEvent('openSettings', null)")
                respond(id, null)
            }
            // capturePhoto and captureScreenshot require Activity result callbacks;
            // the host Activity must override onActivityResult and call resolveCapture().
            else -> respond(id, null)
        }
    }

    /** Reads `style` from a named object `{style}` (new bundle) or positional `[style]` (old). */
    private fun hapticStyle(params: Any?): String = when (params) {
        is org.json.JSONObject -> params.optString("style", "medium")
        is org.json.JSONArray -> params.optString(0, "medium")
        else -> "medium"
    }

    private fun vibrate(style: String) {
        val durationMs = when (style) {
            "light" -> 10L
            "heavy" -> 30L
            else -> 20L
        }
        try {
            if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.S) {
                val vm = context.getSystemService(Context.VIBRATOR_MANAGER_SERVICE) as VibratorManager
                vm.defaultVibrator.vibrate(
                    VibrationEffect.createOneShot(durationMs, VibrationEffect.DEFAULT_AMPLITUDE),
                )
            } else {
                @Suppress("DEPRECATION")
                val vibrator = context.getSystemService(Context.VIBRATOR_SERVICE) as Vibrator
                if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.O) {
                    vibrator.vibrate(
                        VibrationEffect.createOneShot(durationMs, VibrationEffect.DEFAULT_AMPLITUDE),
                    )
                } else {
                    @Suppress("DEPRECATION")
                    vibrator.vibrate(durationMs)
                }
            }
        } catch (_: Exception) {
            // Vibration is non-critical; swallow errors silently
        }
    }

    private fun respond(id: Any, result: Any?) {
        val obj = org.json.JSONObject()
        obj.put("id", id)
        if (result == null) obj.put("result", org.json.JSONObject.NULL)
        else obj.put("result", result)
        val json = obj.toString()
        val escaped = "'${json.replace("\\", "\\\\").replace("'", "\\'")}'"
        evaluateJs("window.__mahoBridgeResponse($escaped)")
    }
}
