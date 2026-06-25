# Sidebar Favicon Fallback & URL Collision Fix

## Context
Two remaining issues from the sidebar new-tab fix:
1. Empty favicon when tab has no favicon data (Swift uses globe fallback)
2. `BuildUrlToIndexMap` URL collision — duplicate URLs map to same strip index

## Task 1: Favicon Globe Fallback

### Problem
`SidebarTabRowView` receives `ui::ImageModel` favicon. When `TabUIHelper::GetFavicon()` returns empty model (no favicon available), the row displays nothing. Swift shell uses SF Symbol `"globe"` as fallback.

### Fix Location
`maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc` — `SidebarTabRowView` constructor, lines 651-658.

### What To Do
In the `else` branch (line 656-657), check if `favicon.IsEmpty()`. If empty, use globe vector icon instead:
```cpp
} else {
  if (favicon.IsEmpty()) {
    favicon_view_->SetImage(ui::ImageModel::FromVectorIcon(
        vector_icons::kGlobeIcon, ui::kColorSysOnSurfaceSubtle,
        kRowIconSizeDp));
  } else {
    favicon_view_->SetImage(std::move(favicon));
  }
}
```

### Why Here (Not at Callers)
There are 3 caller sites that create `fav`. Putting the fallback in the constructor handles all of them in one place.

### Icon Verification
- `vector_icons::kGlobeIcon` — confirmed valid. Source: `chromium/src/components/vector_icons/globe.icon`.
- Already used in `maho_sidebar_search_view.cc:348,355` and `maho_sidebar_favorites_grid_view.cc:378`.
- Include already present: `components/vector_icons/vector_icons.h` at line 37 of `maho_sidebar_tab_list_view.cc`.

### Verification
- `lsp_diagnostics` on changed file — no new errors beyond pre-existing upstream header issues.
- No other files need changes.

---

## Task 2: BuildUrlToIndexMap URL Collision

### Problem
`BuildUrlToIndexMap()` in `maho_sidebar_state_adapter.cc:222-238` returns `std::map<std::string, int>`. When multiple Chromium tabs share the same URL, last-write-wins (`result[url.spec()] = i`). All core tabs with that URL then get the SAME `tab_strip_index`, causing wrong tab activation and broken state.

### Fix Approach: Consumed-Index Pattern
Change the map to store a **queue of indices** per URL. When `TabNodeFromCoreJson` matches a URL, it consumes (pops) the first available index. This ensures each core tab maps to a unique strip index.

### Files to Change
1. `maho-chromium/browser/ui/views/sidebar/maho_sidebar_state_adapter.cc`

### Step-by-Step

#### Step 2a: Change `BuildUrlToIndexMap` return type
```cpp
// BEFORE (line 222)
std::map<std::string, int> BuildUrlToIndexMap(TabStripModel* model) {
  std::map<std::string, int> result;
  ...
  result[url.spec()] = i;
  ...
  return result;
}

// AFTER
std::map<std::string, std::vector<int>> BuildUrlToIndexMap(TabStripModel* model) {
  std::map<std::string, std::vector<int>> result;
  ...
  result[url.spec()].push_back(i);
  ...
  return result;
}
```

#### Step 2b: Change `TabNodeFromCoreJson` signature — non-const reference
```cpp
// BEFORE (line 240-242)
SidebarTreeNode TabNodeFromCoreJson(const base::DictValue& dict,
                                    TabStripModel* model,
                                    const std::map<std::string, int>& url_index,
                                    ...

// AFTER
SidebarTreeNode TabNodeFromCoreJson(const base::DictValue& dict,
                                    TabStripModel* model,
                                    std::map<std::string, std::vector<int>>& url_index,
                                    ...
```

#### Step 2c: Change URL lookup in `TabNodeFromCoreJson` — consume first index
```cpp
// BEFORE (lines 268-270)
auto it = url_index.find(*url);
if (it != url_index.end()) {
  node.tab_strip_index = it->second;

// AFTER
auto it = url_index.find(*url);
if (it != url_index.end() && !it->second.empty()) {
  node.tab_strip_index = it->second.front();
  it->second.erase(it->second.begin());
  if (it->second.empty()) {
    url_index.erase(it);
  }
```

#### Step 2d: Change `BuildTreeSection` signature — non-const reference
```cpp
// BEFORE (line 442)
const std::map<std::string, int>& url_index,

// AFTER
std::map<std::string, std::vector<int>>& url_index,
```

#### Step 2e: Change `BuildTabListModel` — update local variable type
In `BuildTabListModel` (line 713), `auto` already infers the type from `BuildUrlToIndexMap()`, so `auto url_index = BuildUrlToIndexMap(tab_strip_model);` should just work. But verify the two `BuildTreeSection` calls at lines 747-752 pass `url_index` correctly (they already pass by reference).

### Verification
- `lsp_diagnostics` on `maho_sidebar_state_adapter.cc` — no new errors.
- No other files reference `BuildUrlToIndexMap` or its return type directly (it's a namespace-local function).
- The `auto` type deduction in `BuildTabListModel` handles the type change automatically.

---

## DO NOT
- Touch any files outside the two listed above
- Change the `SidebarTreeNode` struct
- Add new includes unless absolutely necessary
- Change the flat fallback path (it doesn't use URL matching)
- Refactor or rename anything beyond the scope above
