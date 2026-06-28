# Memory & Performance Audit Report

**Scope:** `chromium_src/`, `browser/resources/`, `browser/ui/views/`  
**Date:** 2026-06-28

---

## Critical

### 1. Static per-Browser map never evicts entries

**File:** `browser/ui/views/boost/maho_boost_window_controller.cc:35-39`

```cpp
std::map<Browser*, std::unique_ptr<MahoBoostWindowController>>&
GetBrowserControllersMap() {
  static auto* map =
      new std::map<Browser*, std::unique_ptr<MahoBoostWindowController>>();
  return *map;
}
```

**Impact:** Entries keyed by raw `Browser*` are inserted via `GetForBrowser()` but never removed when the Browser is destroyed. Over a long session with many browser windows opened/closed, the map accumulates dangling keys holding dead controllers. A recycled pointer address could return a stale controller.

**Fix:** Make `MahoBoostWindowController` a `BrowserUserData<>` keyed helper (auto-cleans with Browser), or add a `BrowserListObserver::OnBrowserRemoved` call that erases the entry.

---

### 2. Synchronous FFI + JSON parse on UI thread per sidebar rebuild

**File:** `browser/ui/views/sidebar/maho_sidebar_state_adapter.cc:895, 89-149, 729`

```cpp
// BuildTabListModel — called on every tab switch / sidebar update
auto parsed_tree = CallCoreFfi(maho_core_get_sidebar_tree, space_id_json);

// GetCoreFooterSpaceState — 2 FFI calls + JSON parse
char* json_str = maho_core_get_space_view_models(core);
char* active_id_str = maho_core_get_active_space_id(core);

// BuildFavoritesModelForSpaceIdJson — another FFI + JSON parse
char* json_str = maho_core_get_favorite_tabs(core, active_space_id_json.c_str());
```

**Impact:** Every sidebar state rebuild (triggered by tab switch, navigation, space change) performs 3-4 synchronous FFI calls into maho-core, each returning a JSON string that is parsed with `base::JSONReader::Read` on the UI thread. For users with many tabs/spaces, this can be 5-20ms of blocking work per event. Combined with the favicon issue below, this is the most significant performance bottleneck.

**Fix:** Cache sidebar tree state and invalidate on specific maho-core change signals rather than rebuilding from scratch on every event. Alternatively, move FFI + parse to a background `SequencedTaskRunner` and update the View asynchronously.

---

### 3. Favicon data serialized as JSON integer arrays

**File:** `browser/ui/views/sidebar/maho_sidebar_state_adapter.cc:340-352`

```cpp
if (const auto* data_list = favicon_dict->FindList("data")) {
  std::vector<uint8_t> bytes;
  bytes.reserve(data_list->size());
  for (const auto& v : *data_list) {
    if (auto byte = v.GetIfInt()) {
      bytes.push_back(static_cast<uint8_t>(*byte & 0xFF));
    }
  }
```

**Impact:** maho-core returns favicon PNG data as a JSON array of integers (`[137,80,78,71,13,10,...]`). A typical 16x16 favicon (~500-2000 bytes) becomes a JSON array of 500-2000 `base::Value` objects, each heap-allocated. For 50 tabs with favicons, this is ~50,000-100,000 individual JSON value parse + integer extraction operations on the UI thread per sidebar rebuild.

**Fix:** Return favicon data as base64 strings from maho-core (or a shared-memory buffer), eliminating per-byte JSON overhead. A 1KB favicon as base64 is ~1.4KB of string vs ~5KB of JSON array text with far less parse overhead.

---

## High

### 4. `base::Unretained(helper)` crosses thread boundary — ad-block throttle

**File:** `chromium_src/chrome/browser/chrome_content_browser_client.cc:26-28` (patch doc)

```cpp
on_blocked = base::BindRepeating(
    &MahoAdBlockTabHelper::IncrementBlockedCount,
    base::Unretained(helper));
```

**Impact:** `MahoAdBlockThrottle` lives on the network thread; `MahoAdBlockTabHelper` (a `WebContentsUserData`) lives on the UI thread. If a network request completes after the tab closes, calling through the dangling pointer is a use-after-free.

**Fix:** Use `base::SafeRef` or `WeakPtr` + `PostTask` to UI thread:
```cpp
on_blocked = base::BindRepeating(
    [](base::WeakPtr<MahoAdBlockTabHelper> weak) {
      content::GetUIThreadTaskRunner({})->PostTask(FROM_HERE,
          base::BindOnce(&MahoAdBlockTabHelper::IncrementBlockedCount, weak));
    }, helper->GetWeakPtr());
```

---

### 5. `BuildTabsJson` serializes all tabs on UI thread per search keystroke

**File:** `browser/ui/views/command/maho_command_model.cc:461-498`

```cpp
std::string MahoCommandModel::BuildTabsJson(Browser* browser) {
  base::Value tabs(base::Value::Type::LIST);
  ForEachCurrentBrowserWindowInterfaceOrderedByActivation([&tabs, ...] { ... });
  std::string json;
  base::JSONWriter::Write(tabs, &json);
  return json;
}
```

**Impact:** Called from `FireSearch()` on the UI thread before posting to the thread pool. Iterates every tab in every browser window, constructs `base::Value::Dict` per tab (title + URL + metadata), then serializes to JSON. At 100+ tabs this is 10-50ms of synchronous main-thread work during interactive palette typing (after 150ms debounce).

**Fix:** Cache tabs JSON and invalidate via `TabStripModelObserver`. Or move the entire iteration + serialization to the background task that already handles the AI search.

---

### 6. `MahoFullPageCaptureClient` raw-new with conditional self-delete

**File:** `browser/ui/views/location_bar/maho_full_page_capture_client.cc:37-56`

```cpp
auto* client = new MahoFullPageCaptureClient();
client->agent_host_ = agent_host;
if (!agent_host->AttachClient(client)) {
  delete client;
  return;
}
// ... dispatches protocol message, expects response callback to trigger delete
```

**Impact:** The client leaks if the DevTools agent host neither dispatches a response nor fires `AgentHostClosed`. This can happen if the renderer crashes between attach and response, or if the CDP domain is not enabled. The `finished_` flag prevents double-free but cannot prevent no-free.

**Fix:** Add a timeout via `base::OneShotTimer` that calls `Finish()` after a reasonable deadline (e.g., 30s). Or use `base::SequenceBound` / weak pointers instead of raw `new` + `delete this`.

---

## Medium

### 7. `widget_.release()` in non-destroying path leaks Widget

**File:** `browser/ui/views/boost/maho_boost_window_controller.cc:418`

```cpp
void MahoBoostWindowController::OnDelegateWindowClosing() {
  // ...
  if (widget_) {
    widget_->RemoveObserver(this);
    widget_.release();  // Widget NOT destroyed — ownership abandoned
  }
}
```

**Impact:** In `OnDelegateWindowClosing()`, the widget is released without being destroyed. If the platform fires this delegate callback before `OnWidgetDestroying` (possible on some window manager paths), the widget is leaked.

**Fix:** Let `OnWidgetDestroying` be the sole ownership-release path. In `OnDelegateWindowClosing`, only clear state:
```cpp
void OnDelegateWindowClosing() {
  current_domain_.clear();
  StopObservingTab();
  // widget_ ownership handled by OnWidgetDestroying
}
```

---

### 8. `base::Unretained(this)` in button/toggle callbacks (multiple files)

**Files:**
- `browser/ui/views/command/maho_command_overlay_view.cc:422, 624`
- `browser/ui/views/shields/maho_shield_bubble_view.cc:79`
- `browser/ui/views/location_bar/maho_location_bar_utility_panel_view.cc:282, 666, 808`
- `browser/ui/views/location_bar/maho_dom_screenshot_action_dialog_view.cc:291, 310`

```cpp
base::BindRepeating(&View::OnSomething, base::Unretained(this))
```

**Impact:** While child views normally don't outlive parents in Views framework, during async teardown or if callbacks fire after parent destruction begins, `Unretained(this)` can dangle. Some of these are in `BindRepeating` (persistent) callbacks rather than `BindOnce`.

**Fix:** Use `weak_factory_.GetWeakPtr()` for all persistent callbacks. The `MahoDomScreenshotActionDialogView` already uses WeakPtr correctly in `ActivateRow`/`ActivateHeader` — apply the same pattern to button creation callbacks.

---

### 9. `SearchIconView::OnPaint()` allocates SkPath per paint call

**File:** `browser/ui/views/command/maho_command_overlay_view.cc:269-293`

```cpp
void OnPaint(gfx::Canvas* canvas) override {
  const SkPath outer_path = command_palette::BuildSparklePath(
      sparkle_rect, command_palette::kSparkleOuterPoints);
  const SkPath inner_path = command_palette::BuildSparklePath(
      sparkle_rect, command_palette::kSparkleInnerPoints);
  // DrawPath x2
}
```

**Impact:** Two `SkPath` objects with heap-allocated verb/point arrays are constructed every paint call. The command palette's search icon repaints on hover, focus, animation frames. Geometry is deterministic for a given bounds.

**Fix:** Cache paths as member variables, rebuild only in `OnBoundsChanged()`:
```cpp
void OnBoundsChanged(const gfx::Rect& previous_bounds) override {
  outer_path_ = command_palette::BuildSparklePath(...);
  inner_path_ = command_palette::BuildSparklePath(...);
}
```

---

### 10. `base::Unretained(this)` in LocationBarView BindRepeating

**File:** `chromium_src/chrome/browser/ui/views/location_bar/location_bar_view.cc:26` (patch doc)

```cpp
base::BindRepeating(&LocationBarView::ShowUtilityPanel, base::Unretained(this))
```

**Impact:** The utility icon view holds a `BindRepeating` callback with raw pointer to `LocationBarView`. If the icon view is cached or outlives the location bar (e.g., during browser window teardown), the callback dangles.

**Fix:** Use `weak_factory_.GetWeakPtr()` or ensure the icon view is destroyed before the location bar via child-view ownership semantics.

---

## Notes (Acceptable / Non-Issues)

### N1. Mojo remotes in WebUI not explicitly closed

**Files:** `browser/resources/maho_boost/app.ts:415-419`, `maho_space_config/app.ts:1107`, `maho_routines/app.ts:14-17`

Mojo pipes auto-close when the renderer frame tears down. Explicit `close()` calls are unnecessary for single-page WebUI surfaces. **Not a leak.**

### N2. Document-level listeners in Boost WebUI

**File:** `browser/resources/maho_boost/app.ts:691-696`

The `mousemove`/`mouseup` listeners persist for the popup window lifetime. Since Boost runs in a dedicated popup destroyed on close, these don't leak. Acceptable.

### N3. Window error listeners in AI side panel

**File:** `browser/resources/maho_ai/app.ts:46-58`

`window.addEventListener('error')` and `'unhandledrejection'` are never removed. For a long-lived side panel this is acceptable — they're lightweight sentinel handlers.

### N4. `engine_favicon_cache_` bounded by model lifetime

**File:** `browser/ui/views/command/maho_command_model.h:183`

The `flat_map<string, gfx::Image>` is destroyed when the palette closes (model lifetime = one palette session). Not unbounded.

### N5. chromium_src overrides — no hot-path concerns

All `chromium_src/` files are documentation-only references. The documented patches add feature-flag-gated early returns, per-tab helper attachments, and NTP URL rewrites — all O(1) or per-event, not per-frame.

### N6. MahoSpacesOverlayController — well-managed lifetime

**File:** `browser/ui/views/spaces_overlay/maho_spaces_overlay_controller.cc`

Uses `WeakPtr` for all async callbacks, proper `WidgetObserver` cleanup, `CLIENT_OWNS_WIDGET` with `widget_.reset()` on close. No issues.

### N7. MahoSplitViewController — thin, stateless

**File:** `browser/ui/views/split_view/maho_split_view_controller.cc`

Delegates all state to `TabStripModel`. No caches, no observers, no callbacks. No issues.

### N8. MahoDomScreenshotActionDialogView — proper weak_ptr usage

**File:** `browser/ui/views/location_bar/maho_dom_screenshot_action_dialog_view.cc:486-512`

Uses `weak_factory_.GetWeakPtr()` to check self-validity after running activation callbacks. Widget observation is properly reset. Well-designed.

---

## Summary

| Severity | Count | Primary Theme |
|----------|-------|---------------|
| Critical | 3 | Static map leak; synchronous FFI on UI thread; inefficient data encoding |
| High | 3 | UAF risk (Unretained cross-thread); UI-thread blocking; conditional self-delete leak |
| Medium | 4 | Widget ownership; Unretained patterns; per-paint allocations |
| Notes | 8 | Acceptable patterns confirmed safe |

### Top 3 Impact Items (by user-visible effect)

1. **Sidebar FFI + favicon JSON** (Critical #2 + #3) — Directly impacts tab-switch responsiveness for all users. Every sidebar update blocks UI thread with multiple FFI round-trips and per-byte favicon parsing.

2. **BuildTabsJson** (High #5) — Impacts command palette typing latency proportional to total tab count. Users with 50+ tabs will notice input lag.

3. **Boost controller map** (Critical #1) — Memory leak proportional to browser windows opened across session lifetime. Won't crash but grows process RSS indefinitely in long sessions.
