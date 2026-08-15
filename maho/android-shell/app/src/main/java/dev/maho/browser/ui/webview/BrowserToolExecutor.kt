package dev.maho.browser.ui.webview

import dev.maho.browser.bridge.BridgeBookmarks
import dev.maho.browser.bridge.BridgeHistory
import dev.maho.browser.bridge.BridgeNavigation
import dev.maho.browser.bridge.BridgeSpaces
import dev.maho.browser.bridge.BridgeTabs
import org.json.JSONArray
import org.json.JSONObject

class BrowserToolExecutor(
    private val invokeTool: (String, JSONObject) -> JSONObject = ::invokeLiveTool,
) {
    fun invoke(name: String, args: JSONObject): JSONObject = invokeTool(name, args)
}

private fun invokeLiveTool(name: String, args: JSONObject): JSONObject = when (name) {
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

private fun success(result: Any): JSONObject = JSONObject().put("ok", true).put("result", result)

private fun failure(error: String): JSONObject = JSONObject().put("ok", false).put("error", error)

private fun <T> List<T>.toJsonArray(map: (T) -> JSONObject): JSONArray =
    JSONArray().also { output -> forEach { item -> output.put(map(item)) } }
