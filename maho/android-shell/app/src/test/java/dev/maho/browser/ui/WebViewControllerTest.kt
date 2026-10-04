package dev.maho.browser.ui

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.webkit.ValueCallback
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(application = android.app.Application::class)
class WebViewControllerTest {
    private val context: Context = RuntimeEnvironment.getApplication()
    @Test
    fun `main-frame request uses its own URL as the source`() {
        val state = MainDocumentUrlState().apply {
            onPageStarted("https://previous.example/page")
        }

        assertEquals(
            "https://next.example/document",
            state.sourceFor(
                requestUrl = "https://next.example/document",
                isForMainFrame = true,
                requestHeaders = mapOf("Referer" to "https://referrer.example/"),
            ),
        )
    }

    @Test
    fun `subresource uses lifecycle-cached main document URL`() {
        val state = MainDocumentUrlState().apply {
            onPageStarted("https://page.example/started")
            onPageFinished("https://page.example/finished")
        }

        assertEquals(
            "https://page.example/finished",
            state.sourceFor(
                requestUrl = "https://cdn.example/script.js",
                isForMainFrame = false,
                requestHeaders = mapOf("Referer" to "https://fallback.example/"),
            ),
        )
    }

    @Test
    fun `subresource falls back to referer before a lifecycle URL is cached`() {
        val sourceUrl = MainDocumentUrlState().sourceFor(
            requestUrl = "https://cdn.example/script.js",
            isForMainFrame = false,
            requestHeaders = mapOf("referer" to "https://page.example/from-header"),
        )

        assertEquals("https://page.example/from-header", sourceUrl)
    }

    @Test
    fun `interception decision needs only request data and cached lifecycle state`() {
        val state = MainDocumentUrlState().apply {
            onPageStarted("https://page.example/article")
        }
        var observedDecision: Triple<String, String, String>? = null

        val blocked = shouldBlockWebRequest(
            requestUrl = "https://ads.example/banner.js",
            isForMainFrame = false,
            requestHeaders = emptyMap(),
            sourceState = state,
            isSourceWhitelisted = { false },
            queryFilterDecision = { url, sourceUrl, requestType ->
                observedDecision = Triple(url, sourceUrl, requestType)
                true
            },
        )

        assertTrue(blocked)
        assertEquals(
            Triple(
                "https://ads.example/banner.js",
                "https://page.example/article",
                "other",
            ),
            observedDecision,
        )
    }

    @Test
    fun `whitelisted source bypasses filter query`() {
        val state = MainDocumentUrlState().apply {
            onPageStarted("https://allowed.example/article")
        }
        var filterQueried = false

        val blocked = shouldBlockWebRequest(
            requestUrl = "https://ads.example/banner.js",
            isForMainFrame = false,
            requestHeaders = emptyMap(),
            sourceState = state,
            isSourceWhitelisted = { host -> host == "allowed.example" },
            queryFilterDecision = { _, _, _ ->
                filterQueried = true
                true
            },
        )

        assertFalse(blocked)
        assertFalse(filterQueried)
    }

    @Test
    fun `handleExternalSchemeUrl returns false for standard web schemes and dangerous schemes`() {
        assertFalse(handleExternalSchemeUrl(context, "https://example.com/path"))
        assertFalse(handleExternalSchemeUrl(context, "http://localhost:3000"))
        assertFalse(handleExternalSchemeUrl(context, "about:blank"))
        assertFalse(handleExternalSchemeUrl(context, "javascript:console.log('hi')"))
        assertFalse(handleExternalSchemeUrl(context, "data:text/html,<h1>hi</h1>"))
        assertFalse(handleExternalSchemeUrl(context, "blob:https://example.com/uuid"))
        assertFalse(handleExternalSchemeUrl(context, "file:///etc/hosts"))
        assertFalse(handleExternalSchemeUrl(context, "content://media/external/images/media/1"))
    }

    @Test
    fun `handleExternalSchemeUrl returns true when app resolves and false when unresolvable`() {
        val shadowPackageManager = org.robolectric.Shadows.shadowOf(context.packageManager)
        val telIntent = Intent(Intent.ACTION_VIEW, Uri.parse("tel:01012345678")).apply {
            addCategory(Intent.CATEGORY_BROWSABLE)
        }
        shadowPackageManager.addResolveInfoForIntent(telIntent, android.content.pm.ResolveInfo())

        assertTrue(handleExternalSchemeUrl(context, "tel:01012345678"))
        assertFalse(handleExternalSchemeUrl(context, "uninstalledapp://deep-link"))
    }

    @Test
    fun `handleExternalSchemeUrl executes browser fallback url for unhandled intent`() {
        val intentUrl = "intent://scan/#Intent;scheme=customapp;S.browser_fallback_url=https%3A%2F%2Fexample.com%2Ffallback;end"
        var invokedFallback: String? = null

        val handled = handleExternalSchemeUrl(context, intentUrl) { fallback ->
            invokedFallback = fallback
        }

        assertTrue(handled)
        assertEquals("https://example.com/fallback", invokedFallback)
    }

    @Test
    fun `onShowFileChooser invokes onFileChooser lambda and returns true`() {
        var observedCallback: ValueCallback<Array<Uri>>? = null
        val dummyCallback = ValueCallback<Array<Uri>> { }

        val client = MahoWebChromeClient(
            tabIdProvider = { "tab-1" },
            onProgressChanged = {},
            onTitleChanged = {},
            onFileChooser = { callback, _ ->
                observedCallback = callback
                true
            },
        )

        val result = client.onShowFileChooser(null, dummyCallback, null)

        assertTrue(result)
        assertEquals(dummyCallback, observedCallback)
    }

    @Test
    fun `onShowFileChooser cancels callback when onFileChooser is null`() {
        var cancelled = false
        val dummyCallback = ValueCallback<Array<Uri>> { value ->
            if (value == null) {
                cancelled = true
            }
        }

        val client = MahoWebChromeClient(
            tabIdProvider = { "tab-1" },
            onProgressChanged = {},
            onTitleChanged = {},
            onFileChooser = null,
        )

        val result = client.onShowFileChooser(null, dummyCallback, null)

        assertFalse(result)
        assertTrue(cancelled)
    }

    @Test
    fun `onShowCustomView and onHideCustomView invoke registered callbacks`() {
        var customViewShown = false
        var customViewHidden = false
        val dummyView = android.view.View(context)
        val dummyCallback = android.webkit.WebChromeClient.CustomViewCallback { }

        val client = MahoWebChromeClient(
            tabIdProvider = { "tab-1" },
            onProgressChanged = {},
            onTitleChanged = {},
            onCustomViewShow = { view, _ ->
                if (view == dummyView) customViewShown = true
            },
            onCustomViewHide = {
                customViewHidden = true
            },
        )

        client.onShowCustomView(dummyView, dummyCallback)
        assertTrue(customViewShown)

        client.onHideCustomView()
        assertTrue(customViewHidden)
    }

    @Test
    fun `onGeolocationPermissionsShowPrompt denies permission fail-closed when context is null`() {
        var resultOrigin: String? = null
        var isAllow = true
        var isRetain = true

        val client = MahoWebChromeClient(
            tabIdProvider = { "tab-1" },
            onProgressChanged = {},
            onTitleChanged = {},
            contextProvider = { null },
        )

        val callback = android.webkit.GeolocationPermissions.Callback { origin, allow, retain ->
            resultOrigin = origin
            isAllow = allow
            isRetain = retain
        }

        client.onGeolocationPermissionsShowPrompt("https://maps.google.com", callback)

        assertEquals("https://maps.google.com", resultOrigin)
        assertFalse(isAllow)
        assertFalse(isRetain)
    }

    @Test
    fun `onPermissionRequest denies permission fail-closed when context is null`() {
        var wasDenied = false

        val client = MahoWebChromeClient(
            tabIdProvider = { "tab-1" },
            onProgressChanged = {},
            onTitleChanged = {},
            contextProvider = { null },
        )

        val request = object : android.webkit.PermissionRequest() {
            override fun getOrigin(): android.net.Uri = android.net.Uri.parse("https://meet.google.com")
            override fun getResources(): Array<String> = arrayOf(RESOURCE_VIDEO_CAPTURE)
            override fun grant(resources: Array<out String>?) { }
            override fun deny() {
                wasDenied = true
            }
        }

        client.onPermissionRequest(request)
        assertTrue(wasDenied)
    }

    @Test
    fun `findActivity extracts Activity from ContextWrapper chain`() {
        val activityController = org.robolectric.Robolectric.buildActivity(android.app.Activity::class.java).setup()
        val activity = activityController.get()
        val wrapper1 = android.content.ContextWrapper(activity)
        val wrapper2 = android.content.ContextWrapper(wrapper1)

        assertEquals(activity, wrapper2.findActivity())
        assertEquals(activity, wrapper1.findActivity())
        assertEquals(activity, activity.findActivity())
        assertEquals(null, context.findActivity())
    }

    @Test
    fun `OriginPermissionStore stores and retrieves origin permissions`() {
        OriginPermissionStore.clear()
        val origin = "https://meet.google.com"
        val res = android.webkit.PermissionRequest.RESOURCE_VIDEO_CAPTURE

        assertEquals(null, OriginPermissionStore.getPermission(origin, res))
        OriginPermissionStore.setPermission(origin, res, true)
        assertEquals(true, OriginPermissionStore.getPermission(origin, res))

        OriginPermissionStore.setPermission(origin, res, false)
        assertEquals(false, OriginPermissionStore.getPermission(origin, res))
        OriginPermissionStore.clear()
    }

    @Test
    fun `onPermissionRequest uses delegate when provided`() {
        var observedRequest: android.webkit.PermissionRequest? = null
        var observedPerms: List<String>? = null

        val client = MahoWebChromeClient(
            tabIdProvider = { "tab-1" },
            onProgressChanged = {},
            onTitleChanged = {},
            contextProvider = { context },
            onPermissionRequestDelegate = { req, perms ->
                observedRequest = req
                observedPerms = perms
            },
        )

        val request = object : android.webkit.PermissionRequest() {
            override fun getOrigin(): android.net.Uri = android.net.Uri.parse("https://meet.google.com")
            override fun getResources(): Array<String> = arrayOf(RESOURCE_VIDEO_CAPTURE)
            override fun grant(resources: Array<out String>?) { }
            override fun deny() { }
        }

        client.onPermissionRequest(request)
        assertEquals(request, observedRequest)
        assertEquals(listOf(android.Manifest.permission.CAMERA), observedPerms)
    }

    @Test
    fun `onGeolocationPermissionsShowPrompt uses delegate when provided`() {
        var observedOrigin: String? = null
        var observedNeedsOs: Boolean? = null

        val client = MahoWebChromeClient(
            tabIdProvider = { "tab-1" },
            onProgressChanged = {},
            onTitleChanged = {},
            contextProvider = { context },
            onGeolocationRequestDelegate = { origin, _, needsOs ->
                observedOrigin = origin
                observedNeedsOs = needsOs
            },
        )

        val callback = android.webkit.GeolocationPermissions.Callback { _, _, _ -> }
        client.onGeolocationPermissionsShowPrompt("https://maps.google.com", callback)

        assertEquals("https://maps.google.com", observedOrigin)
        assertEquals(true, observedNeedsOs)
    }

    @Test
    fun `checkWebResourcesGranted evaluates all-granted and partial-grant branches correctly`() {
        val camAndMic = arrayOf(
            android.webkit.PermissionRequest.RESOURCE_VIDEO_CAPTURE,
            android.webkit.PermissionRequest.RESOURCE_AUDIO_CAPTURE,
        )

        // Both granted
        val bothGranted = mapOf(
            android.Manifest.permission.CAMERA to true,
            android.Manifest.permission.RECORD_AUDIO to true,
        )
        assertTrue(checkWebResourcesGranted(camAndMic, bothGranted))

        // Camera denied
        val camDenied = mapOf(
            android.Manifest.permission.CAMERA to false,
            android.Manifest.permission.RECORD_AUDIO to true,
        )
        assertFalse(checkWebResourcesGranted(camAndMic, camDenied))

        // Audio denied
        val audioDenied = mapOf(
            android.Manifest.permission.CAMERA to true,
            android.Manifest.permission.RECORD_AUDIO to false,
        )
        assertFalse(checkWebResourcesGranted(camAndMic, audioDenied))

        // Non-OS resource (e.g. protected media) does not require OS perms
        val protectedMedia = arrayOf(android.webkit.PermissionRequest.RESOURCE_PROTECTED_MEDIA_ID)
        assertTrue(checkWebResourcesGranted(protectedMedia, emptyMap()))

        // Empty resources
        assertTrue(checkWebResourcesGranted(emptyArray(), emptyMap()))
    }

    @Test
    fun `onReceivedSslError cancels handler fail-closed and calls onError`() {
        var observedError: String? = null
        var observedUrl: String? = null
        var wasCancelled = false

        val client = MahoWebViewClient(
            tabIdProvider = { "tab-1" },
            onUrlChanged = {},
            onLoadingChanged = {},
            onNavigationStateChanged = { _, _ -> },
            onError = { _, desc, url ->
                observedError = desc
                observedUrl = url
            },
            isSourceWhitelisted = { false },
        )

        val handlerConstructor = android.webkit.SslErrorHandler::class.java.declaredConstructors.first().apply {
            isAccessible = true
        }
        val sslHandler = handlerConstructor.newInstance() as android.webkit.SslErrorHandler
        val shadowHandler = org.robolectric.Shadows.shadowOf(sslHandler)

        val cert = android.net.http.SslCertificate("issuedTo", "issuedBy", "2024-01-01", "2025-01-01")
        val sslError = android.net.http.SslError(android.net.http.SslError.SSL_UNTRUSTED, cert, "https://expired.badssl.com")
        val webView = android.webkit.WebView(context)

        client.onReceivedSslError(webView, sslHandler, sslError)

        assertTrue(shadowHandler.wasCancelCalled())
        assertTrue(observedError?.contains("certificate authority is not trusted") == true)
        assertEquals("https://expired.badssl.com", observedUrl)
    }

    @Test
    fun `onCreateWindow rejects non-user-gesture popup`() {
        val client = MahoWebChromeClient(
            tabIdProvider = { "tab-1" },
            onProgressChanged = {},
            onTitleChanged = {},
            onNewTabRequested = {}
        )
        val webView = android.webkit.WebView(context)
        val result = client.onCreateWindow(webView, isDialog = false, isUserGesture = false, resultMsg = null)
        assertFalse(result)
    }
}
