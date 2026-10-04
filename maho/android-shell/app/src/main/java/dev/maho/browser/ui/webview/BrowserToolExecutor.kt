package dev.maho.browser.ui.webview

import android.os.Looper
import android.webkit.WebView
import dev.maho.browser.bridge.BridgeBookmarks
import dev.maho.browser.bridge.BridgeHistory
import dev.maho.browser.bridge.BridgeNavigation
import dev.maho.browser.bridge.BridgeSpaces
import dev.maho.browser.bridge.BridgeTabs
import java.lang.ref.WeakReference
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import org.json.JSONArray
import org.json.JSONObject

class BrowserToolExecutor(
    private val invokeTool: (String, JSONObject) -> JSONObject = ::invokeLiveTool,
) {
    fun invoke(name: String, args: JSONObject): JSONObject = invokeTool(name, args)
}

internal sealed interface AgenticPageEvaluation {
    data class Success(val json: String) : AgenticPageEvaluation
    data class Failure(val code: String, val message: String? = null) : AgenticPageEvaluation
}

/**
 * Holds only the user-visible browsing WebView. The web-ai/agent overlay WebView
 * must never be registered here: DOM agent tools are intentionally scoped to
 * the currently rendered page.
 */
object AgenticBrowsingWebViewRegistry {
    @Volatile
    private var activeWebView = WeakReference<WebView>(null)

    fun attach(webView: WebView) {
        activeWebView = WeakReference(webView)
    }

    fun detach(webView: WebView) {
        if (activeWebView.get() === webView) {
            activeWebView.clear()
        }
    }

    fun current(): WebView? = activeWebView.get()

    internal fun evaluate(script: String, timeoutMs: Long = 3_000L): AgenticPageEvaluation {
        val webView = activeWebView.get()
            ?: return AgenticPageEvaluation.Failure("no_active_page")
        if (Looper.myLooper() == Looper.getMainLooper()) {
            return AgenticPageEvaluation.Failure("page_script_main_thread")
        }

        val latch = CountDownLatch(1)
        var rawResult: String? = null
        var dispatchFailed = false
        val posted = webView.post {
            runCatching {
                webView.evaluateJavascript(script) { raw ->
                    rawResult = raw
                    latch.countDown()
                }
            }.onFailure {
                dispatchFailed = true
                latch.countDown()
            }
        }
        if (!posted) return AgenticPageEvaluation.Failure("page_script_failed")
        if (!latch.await(timeoutMs, TimeUnit.MILLISECONDS)) {
            return AgenticPageEvaluation.Failure("page_script_timeout")
        }
        if (dispatchFailed) return AgenticPageEvaluation.Failure("page_script_failed")

        val raw = rawResult
            ?: return AgenticPageEvaluation.Failure("page_script_empty_result")
        if (raw == "null" || raw == "undefined") {
            return AgenticPageEvaluation.Failure("page_script_empty_result")
        }
        val decoded = runCatching { JSONArray("[$raw]").getString(0) }.getOrElse { raw }
        return AgenticPageEvaluation.Success(decoded)
    }
}

private val AGENTIC_BROWSING_TOOLS = setOf(
    "get_page_elements",
    "get_page_snapshot",
    "click_element",
    "fill_input",
    "scroll_page",
)

private fun invokeLiveTool(name: String, args: JSONObject): JSONObject {
    if (name in AGENTIC_BROWSING_TOOLS) {
        return invokeAgenticBrowsingTool(name, args, AgenticBrowsingWebViewRegistry::evaluate)
    }

    return when (name) {
        "search_bookmarks" -> success(
            BridgeBookmarks.searchBookmarkEntries(args.optString("query")).toJsonArray { bookmark ->
                JSONObject()
                    .put("id", bookmark.id)
                    .put("url", bookmark.url)
                    .put("title", bookmark.title)
            },
        )
        "list_tabs" -> success(
            BridgeTabs.getTabViewModels().toJsonArray { tab ->
                JSONObject()
                    .put("id", tab.id)
                    .put("spaceId", tab.spaceId)
                    .put("url", tab.url)
                    .put("title", tab.title)
            },
        )
        "get_page_info" -> {
            val activeTabId = BridgeTabs.getActiveTabId()
            val tab = activeTabId?.let { id -> BridgeTabs.getTabViewModels().firstOrNull { it.id == id } }
            if (tab == null) failure("no_active_tab") else success(
                JSONObject().put("id", tab.id).put("url", tab.url).put("title", tab.title),
            )
        }
        "search_history" -> success(
            BridgeHistory.searchHistoryEntries(args.optString("query"), 20).toJsonArray { entry ->
                JSONObject()
                    .put("id", entry.id)
                    .put("url", entry.url)
                    .put("title", entry.title)
            },
        )
        "open_tab" -> {
            val url = args.optString("url")
            val spaceId = BridgeSpaces.getActiveSpaceId()
            if (url.isBlank() || spaceId == null) failure("missing_url_or_active_space") else {
                BridgeTabs.createTab(spaceId, url)
                success(JSONObject().put("url", url))
            }
        }
        "navigate" -> {
            val url = args.optString("url")
            val tabId = BridgeTabs.getActiveTabId()
            if (url.isBlank() || tabId == null) failure("missing_url_or_active_tab") else {
                BridgeNavigation.navigate(tabId, url)
                success(JSONObject().put("tab_id", tabId).put("url", url))
            }
        }
        "close_tab" -> {
            val tabId = args.optString("tab_id")
            if (tabId.isBlank()) failure("missing_tab_id") else {
                BridgeTabs.closeTab(tabId)
                success(JSONObject().put("tab_id", tabId))
            }
        }
        "create_bookmark" -> {
            val url = args.optString("url")
            if (url.isBlank()) failure("missing_url") else {
                BridgeBookmarks.addBookmarkEntry(url, args.optString("title", url))
                success(JSONObject().put("url", url))
            }
        }
        else -> failure("unknown_tool")
    }
}

internal fun invokeAgenticBrowsingTool(
    name: String,
    args: JSONObject,
    evaluate: (String) -> AgenticPageEvaluation,
): JSONObject {
    if (name !in AGENTIC_BROWSING_TOOLS) return failure("unknown_tool")
    val script = agenticBrowsingScript(name, args)
    return when (val evaluation = evaluate(script)) {
        is AgenticPageEvaluation.Failure -> failure(evaluation.code, evaluation.message)
        is AgenticPageEvaluation.Success -> {
            runCatching { JSONObject(evaluation.json) }
                .getOrElse { failure("invalid_page_script_result") }
        }
    }
}

internal fun agenticBrowsingToolDescriptorsJson(): String = JSONObject()
    .put(
        "tools",
        JSONArray()
            .put(agenticDescriptor(
                capabilityId = "get_page_elements",
                description = "List visible interactive page elements with stable numeric refs, labels, and bounding boxes.",
                schema = JSONObject().put("type", "object").put("additionalProperties", false).put("properties", JSONObject()),
                sensitive = false,
                permission = "auto_approve",
            ))
            .put(agenticDescriptor(
                capabilityId = "get_page_snapshot",
                description = "Get the current page viewport plus visible interactive elements with numeric refs.",
                schema = JSONObject().put("type", "object").put("additionalProperties", false).put("properties", JSONObject()),
                sensitive = false,
                permission = "auto_approve",
            ))
            .put(agenticDescriptor(
                capabilityId = "click_element",
                description = "Click a page element by numeric ref or CSS selector.",
                schema = targetSchema(),
                sensitive = true,
                permission = "always_ask",
            ))
            .put(agenticDescriptor(
                capabilityId = "fill_input",
                description = "Fill an input, textarea, or contenteditable element and fire input/change events.",
                schema = targetSchema().apply {
                    getJSONObject("properties").put("text", JSONObject().put("type", "string"))
                    put("required", JSONArray().put("text"))
                },
                sensitive = true,
                permission = "always_ask",
            ))
            .put(agenticDescriptor(
                capabilityId = "scroll_page",
                description = "Smoothly scroll the current page up, down, left, or right.",
                schema = JSONObject()
                    .put("type", "object")
                    .put("additionalProperties", false)
                    .put("properties", JSONObject()
                        .put("direction", JSONObject().put("type", "string").put("enum", JSONArray(listOf("up", "down", "left", "right"))))
                        .put("amount", JSONObject().put("type", "number").put("minimum", 1).put("maximum", 10000)))
                    .put("required", JSONArray().put("direction")),
                sensitive = false,
                permission = "auto_approve",
            )),
    )
    .toString()

private fun agenticDescriptor(
    capabilityId: String,
    description: String,
    schema: JSONObject,
    sensitive: Boolean,
    permission: String,
): JSONObject = JSONObject()
    .put("capabilityId", capabilityId)
    .put("name", capabilityId)
    .put("description", description)
    .put("inputSchema", schema)
    .put("schemaVersion", 1)
    .put("policy", JSONObject().put("sensitive", sensitive).put("permission", permission))

private fun targetSchema(): JSONObject = JSONObject()
    .put("type", "object")
    .put("additionalProperties", false)
    .put("properties", JSONObject()
        .put("id", JSONObject().put("type", "integer").put("minimum", 1))
        .put("selector", JSONObject().put("type", "string").put("minLength", 1)))

private fun agenticBrowsingScript(name: String, args: JSONObject): String {
    val argsLiteral = JSONObject.quote(args.toString())
    val toolLiteral = JSONObject.quote(name)
    return """
        (function() {
          const tool = $toolLiteral;
          const args = JSON.parse($argsLiteral);
          const refAttr = 'data-maho-agent-ref';
          const ok = (result) => JSON.stringify({ok: true, result: result});
          const fail = (error, message) => JSON.stringify(Object.assign({ok: false, error: error}, message ? {message: String(message).slice(0, 200)} : {}));
          const round = (value) => Math.round(Number(value) * 10) / 10;
          const visible = (el) => {
            if (!(el instanceof Element)) return false;
            const style = getComputedStyle(el);
            if (style.display === 'none' || style.visibility === 'hidden' || Number(style.opacity) === 0) return false;
            const rect = el.getBoundingClientRect();
            if (rect.width < 1 || rect.height < 1) return false;
            return rect.bottom > 0 && rect.right > 0 && rect.top < innerHeight && rect.left < innerWidth;
          };
          const safeSelector = (selector) => {
            if (typeof selector !== 'string' || selector.trim() === '') return null;
            try { return document.querySelector(selector); } catch (_) { return '__invalid_selector__'; }
          };
          const resolveTarget = () => {
            if (Number.isInteger(Number(args.id)) && Number(args.id) > 0) {
              return document.querySelector('[' + refAttr + '="' + Number(args.id) + '"]');
            }
            return safeSelector(args.selector);
          };
          const labelFor = (el) => {
            const aria = (el.getAttribute('aria-label') || '').trim();
            if (aria) return aria.slice(0, 200);
            if (el.id) {
              try {
                const label = document.querySelector('label[for="' + CSS.escape(el.id) + '"]');
                if (label && label.innerText.trim()) return label.innerText.trim().slice(0, 200);
              } catch (_) {}
            }
            const labelledBy = (el.getAttribute('aria-labelledby') || '').trim();
            if (labelledBy) {
              const text = labelledBy.split(/\s+/).map((id) => document.getElementById(id)?.innerText || '').join(' ').trim();
              if (text) return text.slice(0, 200);
            }
            const placeholder = (el.getAttribute('placeholder') || '').trim();
            if (placeholder) return placeholder.slice(0, 200);
            const alt = (el.getAttribute('alt') || '').trim();
            if (alt) return alt.slice(0, 200);
            const title = (el.getAttribute('title') || '').trim();
            if (title) return title.slice(0, 200);
            return (el.innerText || el.textContent || '').replace(/\s+/g, ' ').trim().slice(0, 200);
          };
          const snapshot = () => {
            document.querySelectorAll('[' + refAttr + ']').forEach((el) => el.removeAttribute(refAttr));
            const candidates = Array.from(document.querySelectorAll('a[href],button,input:not([type="hidden"]),textarea,select,[role="button"],[role="link"],[role="checkbox"],[role="radio"],[role="switch"],[role="tab"],[onclick],[contenteditable="true"],[tabindex]:not([tabindex="-1"])'));
            const elements = [];
            let nextId = 1;
            for (const el of candidates) {
              if (elements.length >= 150 || !visible(el)) continue;
              const id = nextId++;
              el.setAttribute(refAttr, String(id));
              const rect = el.getBoundingClientRect();
              const item = {
                id: id,
                tag: el.tagName.toLowerCase(),
                label: labelFor(el),
                selector: '[' + refAttr + '="' + id + '"]',
                bounds: {x: round(rect.left), y: round(rect.top), width: round(rect.width), height: round(rect.height)}
              };
              const role = (el.getAttribute('role') || '').trim();
              if (role) item.role = role;
              const type = (el.getAttribute('type') || '').trim();
              if (type) item.type = type;
              const href = el instanceof HTMLAnchorElement ? el.href : '';
              if (href) item.href = href.slice(0, 1000);
              const text = (el.innerText || '').replace(/\s+/g, ' ').trim().slice(0, 200);
              if (text) item.text = text;
              elements.push(item);
            }
            return ok({
              url: location.href,
              title: document.title || '',
              viewport: {width: innerWidth, height: innerHeight, scrollX: round(scrollX), scrollY: round(scrollY)},
              elements: elements
            });
          };
          try {
            if (tool === 'get_page_elements' || tool === 'get_page_snapshot') return snapshot();
            if (tool === 'click_element') {
              if (args.id == null && (typeof args.selector !== 'string' || args.selector.trim() === '')) return fail('missing_target');
              const target = resolveTarget();
              if (target === '__invalid_selector__') return fail('invalid_selector');
              if (!(target instanceof Element)) return fail('element_not_found');
              if (!visible(target)) return fail('element_not_interactable');
              if (target.matches(':disabled,[aria-disabled="true"]')) return fail('element_disabled');
              if (typeof target.focus === 'function') target.focus({preventScroll: true});
              for (const eventName of ['touchstart', 'touchend']) target.dispatchEvent(new Event(eventName, {bubbles: true, cancelable: true}));
              if (typeof PointerEvent === 'function') {
                target.dispatchEvent(new PointerEvent('pointerdown', {bubbles: true, cancelable: true, pointerType: 'touch', isPrimary: true}));
                target.dispatchEvent(new PointerEvent('pointerup', {bubbles: true, cancelable: true, pointerType: 'touch', isPrimary: true}));
              }
              target.dispatchEvent(new MouseEvent('mousedown', {bubbles: true, cancelable: true, view: window}));
              target.dispatchEvent(new MouseEvent('mouseup', {bubbles: true, cancelable: true, view: window}));
              if (typeof target.click === 'function') target.click();
              else target.dispatchEvent(new MouseEvent('click', {bubbles: true, cancelable: true, view: window}));
              return ok({id: Number(target.getAttribute(refAttr)) || null, selector: args.selector || null, clicked: true});
            }
            if (tool === 'fill_input') {
              if (args.id == null && (typeof args.selector !== 'string' || args.selector.trim() === '')) return fail('missing_target');
              if (typeof args.text !== 'string') return fail('missing_text');
              const target = resolveTarget();
              if (target === '__invalid_selector__') return fail('invalid_selector');
              if (!(target instanceof Element)) return fail('element_not_found');
              if (!visible(target)) return fail('element_not_interactable');
              if (target.matches(':disabled,[aria-disabled="true"]')) return fail('element_disabled');
              if ('readOnly' in target && target.readOnly) return fail('element_readonly');
              const editable = target instanceof HTMLInputElement || target instanceof HTMLTextAreaElement || target.isContentEditable;
              if (!editable) return fail('element_not_editable');
              if (typeof target.focus === 'function') target.focus({preventScroll: true});
              if (target instanceof HTMLInputElement || target instanceof HTMLTextAreaElement) {
                const proto = target instanceof HTMLTextAreaElement ? HTMLTextAreaElement.prototype : HTMLInputElement.prototype;
                const setter = Object.getOwnPropertyDescriptor(proto, 'value')?.set;
                if (setter) setter.call(target, args.text); else target.value = args.text;
              } else {
                target.textContent = args.text;
              }
              try {
                target.dispatchEvent(new InputEvent('input', {bubbles: true, inputType: 'insertText', data: args.text}));
              } catch (_) {
                target.dispatchEvent(new Event('input', {bubbles: true}));
              }
              target.dispatchEvent(new Event('change', {bubbles: true}));
              return ok({id: Number(target.getAttribute(refAttr)) || null, selector: args.selector || null, filled: true, length: args.text.length});
            }
            if (tool === 'scroll_page') {
              const direction = typeof args.direction === 'string' ? args.direction.toLowerCase() : '';
              if (!['up', 'down', 'left', 'right'].includes(direction)) return fail('invalid_direction');
              const defaultAmount = (direction === 'up' || direction === 'down') ? innerHeight * 0.8 : innerWidth * 0.8;
              const amount = args.amount == null ? defaultAmount : Number(args.amount);
              if (!Number.isFinite(amount) || amount <= 0 || amount > 10000) return fail('invalid_amount');
              const fromX = scrollX;
              const fromY = scrollY;
              const dx = direction === 'left' ? -amount : direction === 'right' ? amount : 0;
              const dy = direction === 'up' ? -amount : direction === 'down' ? amount : 0;
              const maxX = Math.max(0, document.documentElement.scrollWidth - innerWidth);
              const maxY = Math.max(0, document.documentElement.scrollHeight - innerHeight);
              const targetX = Math.max(0, Math.min(maxX, fromX + dx));
              const targetY = Math.max(0, Math.min(maxY, fromY + dy));
              window.scrollBy({left: dx, top: dy, behavior: 'smooth'});
              return ok({direction: direction, amount: round(amount), from: {x: round(fromX), y: round(fromY)}, target: {x: round(targetX), y: round(targetY)}});
            }
            return fail('unknown_tool');
          } catch (error) {
            return fail('script_error', error && error.message ? error.message : String(error));
          }
        })();
    """.trimIndent()
}

private fun success(result: Any): JSONObject = JSONObject().put("ok", true).put("result", result)

private fun failure(error: String, message: String? = null): JSONObject = JSONObject()
    .put("ok", false)
    .put("error", error)
    .also { if (!message.isNullOrBlank()) it.put("message", message.take(200)) }

private fun <T> List<T>.toJsonArray(map: (T) -> JSONObject): JSONArray =
    JSONArray().also { output -> forEach { item -> output.put(map(item)) } }
