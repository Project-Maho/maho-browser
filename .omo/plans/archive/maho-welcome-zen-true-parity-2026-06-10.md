# Maho Welcome — Zen Browser True Parity Implementation Plan

**Created:** 2026-06-10
**Branch:** `feat/welcome-zen-parity`
**Status:** Ready for implementation
**Estimated Total Effort:** ~3 weeks (3 parallel streams)

---

## Architecture Overview

```
┌──────────────────────────────────────────────────────────────────────┐
│  Frontend (React 19 + Mojo WebUI)                                     │
│  browser/resources/maho_welcome/react/                                │
│   ├── pages/import-data.tsx  ← multi-data-type UI                     │
│   ├── pages/essentials.tsx   ← maho-core favorite dispatch            │
│   ├── pages/completion.tsx   ← slide-out + heartPulse animation       │
│   └── store.ts               ← state machine, Mojo proxy             │
├──────────────────────────────────────────────────────────────────────┤
│  Mojo IDL (maho_welcome.mojom)                                        │
│   ← Extended: OnImportItemProgress, FavoriteEssentialSites,           │
│     OpenMainBrowserWindow                                             │
├──────────────────────────────────────────────────────────────────────┤
│  Backend PageHandler (maho_welcome_page_handler.cc)                   │
│   ← Orchestrates importer, calls maho-core for essentials             │
├──────────────────────────────────────────────────────────────────────┤
│  Importer Module (browser/importer/)                                  │
│   ├── chromium_history_parser.cc  ← NEW: History SQLite parser        │
│   ├── chromium_password_parser.cc ← NEW: Login Data + OSCrypt         │
│   ├── chromium_autofill_parser.cc ← NEW: Web Data autofill            │
│   ├── firefox_history_parser.cc   ← NEW: places.sqlite history        │
│   ├── firefox_password_parser.cc  ← NEW: logins.json + NSS            │
│   ├── safari_bookmark_parser.cc   ← NEW: Bookmarks.plist binary plist │
│   ├── favicon_parser.cc           ← NEW: Favicons DB                  │
│   └── maho_bookmark_importer.cc   ← MODIFIED: merge-to-tree option    │
├──────────────────────────────────────────────────────────────────────┤
│  Chromium Services (write targets)                                    │
│   ├── HistoryService::AddPagesWithDetails                             │
│   ├── PasswordStoreInterface::AddLogin                                │
│   ├── FaviconService::SetImportedFavicons                             │
│   └── AutofillWebDataService::UpdateAutocompleteEntries               │
├──────────────────────────────────────────────────────────────────────┤
│  maho-core FFI (maho_ffi.h)                                          │
│   ├── maho_core_handle_event → "create_tab" / "favorite_tab"         │
│   └── maho_core_derive_oscrypt_key ← already exists!                  │
└──────────────────────────────────────────────────────────────────────┘
```

---

## Phase Breakdown (14 Gaps)

### Phase 1: History Import (Chromium + Firefox)

**Gap:** History import missing entirely.

**Files to Create/Modify:**
| Action | Path |
|--------|------|
| CREATE | `maho-chromium/browser/importer/chromium_history_parser.h` |
| CREATE | `maho-chromium/browser/importer/chromium_history_parser.cc` |
| CREATE | `maho-chromium/browser/importer/firefox_history_parser.h` |
| CREATE | `maho-chromium/browser/importer/firefox_history_parser.cc` |
| MODIFY | `maho-chromium/browser/importer/maho_browser_detector.cc` — add `kHistory = 1 << 0` to `services_supported` |
| MODIFY | `maho-chromium/browser/importer/BUILD.gn` — add new sources + `//components/history/core/browser` dep |
| MODIFY | `maho-chromium/browser/ui/webui/maho_welcome/maho_welcome_page_handler.cc` — call history write |
| MODIFY | `maho-chromium/browser/ui/webui/maho_welcome/BUILD.gn` — add `//chrome/browser/history` dep |

**APIs:**
- Read (thread pool): Chromium `History` SQLite — `SELECT urls.url, urls.title, urls.visit_count, urls.last_visit_time, visits.visit_time FROM urls JOIN visits ON urls.id = visits.url`
- Read (thread pool): Firefox `places.sqlite` — `SELECT moz_places.url, moz_places.title, moz_historyvisits.visit_date FROM moz_places JOIN moz_historyvisits ON moz_places.id = moz_historyvisits.place_id`
- Write (UI thread): `HistoryService* hs = HistoryServiceFactory::GetForProfile(profile, ServiceAccessType::EXPLICIT_ACCESS); hs->AddPagesWithDetails(rows, history::VisitSource::SOURCE_IMPORTED);`

**Threading:** SQLite reads on thread pool with `base::MayBlock()`; `AddPagesWithDetails` on UI thread.

**Edge cases:** Locked SQLite (browser running) → copy file to temp before open. WebKit epoch (1601-01-01) vs Firefox microseconds (Unix epoch). >100k entries → batch chunks of 5000.

**Verification (binary):**
```js
// In DevTools on chrome://history after import:
chrome.history.search({text: '', maxResults: 10}, r => console.log(r.length));
// Expected: > 0
```

**Effort:** M | **Dependencies:** None | **Risk:** Low

---

### Phase 2: Password Import (Chromium + Firefox)

**Gap:** Passwords import missing entirely.

**Files to Create/Modify:**
| Action | Path |
|--------|------|
| CREATE | `maho-chromium/browser/importer/chromium_password_parser.{h,cc}` |
| CREATE | `maho-chromium/browser/importer/firefox_password_parser.{h,cc}` |
| CREATE | `maho-chromium/browser/importer/nss_decrypt_helper.{h,cc}` |
| MODIFY | `maho-chromium/browser/importer/maho_browser_detector.cc` — add `kPasswords = 1 << 3` |
| MODIFY | `maho-chromium/browser/importer/BUILD.gn` |
| MODIFY | `maho-chromium/browser/ui/webui/maho_welcome/maho_welcome_page_handler.cc` |

**APIs:**
- **Chromium OSCrypt:** `maho_core_derive_oscrypt_key(path, out_key_16bytes, &status)` — already in `maho_ffi.h:1395`
  - AES-128-CBC decrypt: IV = 16 spaces, strip "v10" 3-byte prefix
- **Read:** `Login Data` SQLite — `SELECT origin_url, action_url, username_value, password_value FROM logins`
- **Firefox NSS:** Parse `logins.json` → 4 algorithm variants:
  - Variant A (Firefox <75): 3DES
  - Variant B (75-143): AES-256-CBC
  - Variant C (146+): SHA384 + AES-256-CBC (detect via `metaData.item1` length: 48 vs 20)
  - Variant D (144+): AES-256 in logins.json (detect via key length 48 vs 24)
- **Write:** `PasswordStoreFactory::GetForProfile(...)→GetBackend()->AddLogin(PasswordForm, ...)`

**Threading:** OSCrypt key derivation on thread pool (Keychain ACL prompt blocks). SQLite + decrypt on thread pool. `AddLogin` on UI thread.

**Edge cases:** Keychain ACL deny → "Keychain access denied" message. v10 (AES-128-CBC) vs v11 (AES-256-GCM) prefix detection. Firefox master password set → "Cannot import, master password protected".

**Verification:** `chrome://password-manager/passwords` → imported credentials visible.

**Effort:** L | **Dependencies:** None | **Risk:** HIGH (Keychain UX, NSS variants)

---

### Phase 3: Autofill Import

**Gap:** Autofill form data import missing.

**Files:**
| Action | Path |
|--------|------|
| CREATE | `maho-chromium/browser/importer/chromium_autofill_parser.{h,cc}` |
| MODIFY | `maho-chromium/browser/importer/maho_browser_detector.cc` — add `kAutofill = 1 << 6` |
| MODIFY | `maho-chromium/browser/importer/BUILD.gn` |

**APIs:**
- Read: `Web Data` SQLite — `SELECT name, value, count FROM autofill ORDER BY count DESC`
- Write: `AutofillWebDataServiceFactory::GetForProfile(...)→AddFormFields(entries)`

**Effort:** S | **Dependencies:** None | **Risk:** Low

---

### Phase 4: Favicon Import

**Gap:** Favicons not imported.

**Files:**
| Action | Path |
|--------|------|
| CREATE | `maho-chromium/browser/importer/favicon_parser.{h,cc}` |
| MODIFY | `maho-chromium/browser/importer/BUILD.gn` — add `//components/favicon/core` |

**APIs:**
- Read: Chromium `Favicons` SQLite + Firefox `favicons.sqlite`
- Write: `FaviconServiceFactory::GetForProfile(...)→SetImportedFavicons(list)`

**Effort:** S | **Dependencies:** Phase 1 (history URLs needed for favicon attachment) | **Risk:** Low

---

### Phase 5: Safari Real Import (FDA + Bookmarks.plist)

**Gap:** Safari detected but only shows "Grant FDA" message.

**Files:**
| Action | Path |
|--------|------|
| CREATE | `maho-chromium/browser/importer/safari_bookmark_parser.{h,cc}` |
| MODIFY | `maho-chromium/browser/importer/maho_bookmark_importer.cc` — handle Safari case |
| MODIFY | `maho-chromium/browser/importer/BUILD.gn` — CoreFoundation framework dep |
| MODIFY | `maho-chromium/browser/resources/maho_welcome/react/pages/import-data.tsx` — FDA retry button |

**APIs:**
- FDA check: `base::PathExists(safari_bookmarks)` — if readable, access granted
- Open Settings: `[[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:@"x-apple.systempreferences:com.apple.preference.security?Privacy_AllFiles"]]`
- Parse: `CFPropertyListCreateWithData` → walk `Children` array recursively

**Effort:** M | **Dependencies:** Phase 7 (merge behavior) | **Risk:** Medium (FDA UX)

---

### Phase 6: Cookies Import — **INCLUDED** (per user decision 2026-06-10)

**Gap:** Cookie import missing. Sites force re-auth after import without it.

**Files to Create/Modify:**
| Action | Path |
|--------|------|
| CREATE | `maho-chromium/browser/importer/chromium_cookie_parser.{h,cc}` |
| CREATE | `maho-chromium/browser/importer/firefox_cookie_parser.{h,cc}` |
| MODIFY | `maho-chromium/browser/importer/maho_browser_detector.cc` — add `kCookies = 1 << 5` flag |
| MODIFY | `maho-chromium/browser/importer/BUILD.gn` — add deps `//services/network/public/mojom`, `//net/cookies` |
| MODIFY | page handler — add `ImportCookies()` Mojo method |

**Implementation:**
1. Chromium: read `Cookies` SQLite (table `cookies`, columns `host_key, name, value, encrypted_value, path, expires_utc, is_secure, is_httponly, samesite, source_scheme`). Decrypt `encrypted_value` via shared OSCrypt helper from Phase 2a (same `v10` AES-128-CBC).
2. Firefox: read `cookies.sqlite` table `moz_cookies` (no encryption — plaintext on disk).
3. Push to Maho cookie store via `CookieManager::SetCanonicalCookie` Mojo (network service binding via `StoragePartition::GetCookieManagerForBrowserProcess`).
4. Filter session cookies (no `expires_utc`) — skip them.
5. Map `samesite` enum: 0=NoRestriction, 1=Lax, 2=Strict, -1=Unspecified.
6. Batch 1000 cookies per Mojo round-trip; report progress.

**Risk:** Network service is sandboxed. Must hop through `StoragePartition::GetCookieManagerForBrowserProcess()` on UI thread. Cannot decrypt v20 (App-Bound Encryption v127+) cookies for Chrome — fallback: skip those rows with warning.

**Verify:** Import from Chrome → restart Maho → google.com still logged in.

---

### Phase 7: Bookmark Merge (No Wrapper Folder)

**Gap:** Bookmarks land in "Imported from X" wrapper folder.

**Files:**
| Action | Path |
|--------|------|
| MODIFY | `maho-chromium/browser/importer/maho_bookmark_importer.cc` — remove wrapper, write to bar directly |
| MODIFY | `maho-chromium/browser/importer/maho_bookmark_importer.h` — add merge option |

Current code at `maho_bookmark_importer.cc:109-115`:
```cpp
const bookmarks::BookmarkNode* import_root =
    model->AddFolder(bar, bar->children().size(), folder_name);
WriteBookmarksToModel(model, import_root, bookmarks);
```

New behavior:
```cpp
WriteBookmarksToModel(model, bar, bookmarks);  // direct merge
```

**Effort:** S | **Dependencies:** None | **Risk:** Low

---

### Phase 8: True Essentials via maho-core (Favorites Grid)

**Gap:** Essentials use `SetTabPinned`. Zen uses favorites grid via `favorite_tab`.

**Files:**
| Action | Path |
|--------|------|
| MODIFY | `maho-chromium/browser/ui/webui/maho_welcome/maho_welcome.mojom` — rename to `FavoriteEssentialSites` |
| MODIFY | `maho-chromium/browser/ui/webui/maho_welcome/maho_welcome_page_handler.{h,cc}` |
| MODIFY | `maho-chromium/browser/ui/webui/maho_welcome/BUILD.gn` — add `//maho/third_party/maho` |
| MODIFY | `maho-chromium/browser/resources/maho_welcome/react/store.ts` |

**Implementation pattern (from `maho_test_ui.cc:580-868`):**
```cpp
void MahoWelcomePageHandler::FavoriteEssentialSites(const std::vector<std::string>& urls) {
  auto* core = maho::core::GetGlobalInstance();
  std::string space_id = MahoSpaceProfileBridge::GetInstance()->GetActiveSpaceId();

  for (const auto& url : urls) {
    base::Value::Dict create_event;
    create_event.Set("kind", "create_tab");
    create_event.Set("space_id", space_id);
    create_event.Set("url", url);
    // ... dispatch via maho_core_handle_event, parse tab_id from response

    base::Value::Dict fav_event;
    fav_event.Set("kind", "favorite_tab");
    fav_event.Set("tab_id", tab_id);
    // ... dispatch
  }
}
```

This automatically gives us **Phase 9 (lazy tabs)** — no `WebContents` created, no network load.

**Effort:** M | **Dependencies:** None | **Risk:** Medium (must verify core handle available)

---

### Phase 9: Lazy Essential Tabs — **INCLUDED IN PHASE 8**

Solved inherently by using `maho_core_handle_event("create_tab")` instead of `Navigate()`.

---

### Phase 10: Theme Page — Real Mode Chips

**Gap:** Hardcoded gradient cards. Zen embeds actual theme picker.

**Files:**
| Action | Path |
|--------|------|
| MODIFY | `maho-chromium/browser/resources/maho_welcome/react/pages/space-theme.tsx` — add mode chips (dark/light/auto) + hue control |
| MODIFY | `maho-chromium/browser/resources/maho_welcome/welcome.css` |

**Recommended approach:** Keep color swatches BUT add three mode chips (sparkle/sun/moon = auto/light/dark) above them. Existing `PreviewTheme`/`ApplyTheme` handler already calls `MahoSpaceThemeState` correctly — the gap is purely UI representation.

**Effort:** M | **Dependencies:** None | **Risk:** Low

---

### Phase 11: Completion Animation

**Gap:** Static empty div. Zen has slide-out + heart pulse.

**Files:**
| Action | Path |
|--------|------|
| MODIFY | `maho-chromium/browser/resources/maho_welcome/react/pages/completion.tsx` — add heart SVG |
| MODIFY | `maho-chromium/browser/resources/maho_welcome/welcome.css` — add `@keyframes pageSlideOutRight` |
| MODIFY | `maho-chromium/browser/resources/maho_welcome/react/store.ts` — animation timing |

CSS already has `@keyframes heartPulse` at `welcome.css:574`. Just needs wiring + slide-out:
```css
@keyframes pageSlideOutRight {
  from { transform: translateX(0); opacity: 1; }
  to   { transform: translateX(100%); opacity: 0; }
}
```

**Effort:** S | **Dependencies:** Phase 12 | **Risk:** Low

---

### Phase 12: Open Main Browser After Welcome Closes

**Gap:** Zero windows after welcome closes.

**Files:**
| Action | Path |
|--------|------|
| MODIFY | `maho-chromium/browser/ui/webui/maho_welcome/maho_welcome.mojom` — add `OpenMainBrowserWindow()` |
| MODIFY | `maho-chromium/browser/ui/webui/maho_welcome/maho_welcome_page_handler.{h,cc}` |
| MODIFY | `maho-chromium/browser/resources/maho_welcome/react/store.ts` |

**Implementation:**
```cpp
void MahoWelcomePageHandler::OpenMainBrowserWindow() {
  Browser::CreateParams params(profile_, true);
  params.creation_source = Browser::CreationSource::kStartupCreator;
  Browser* main_browser = Browser::Create(params);
  main_browser->window()->Show();

  BrowserView* bv = BrowserView::GetBrowserViewForBrowser(main_browser);
  if (bv) {
    MahoNotificationOverlay::GetForBrowserView(bv)->Show(
        u"Welcome to Maho!", u"Your browser is set up and ready to go.");
  }
}
```

**Effort:** S | **Dependencies:** None | **Risk:** Low

---

### Phase 13: Welcome Toast — Included in Phase 12

Add `IDS_MAHO_TOAST_WELCOME_FINISHED` string to `apply_chromium_src_overrides.py`.

**Effort:** S | **Dependencies:** Phase 12

---

### Phase 14: Workspace-as-Space Import (Zen / Arc) — **NEW (user requirement 2026-06-10)**

**Gap:** Currently bookmarks flatten into a single Maho Space. Zen has Workspaces, Arc has Spaces — each with their own pinned tabs, essentials/favorites, folders, theme. We must reconstruct each source workspace as a separate Maho Space using `maho_core_create_space` + `create_tab`/`pin_tab`/`favorite_tab`/`create_folder` events.

**Requirement (verbatim):** "zen/arc 등은 스페이스 자체로 import를 해올 수가 있어야함."

**Files to Create:**
| Action | Path |
|--------|------|
| CREATE | `maho-chromium/browser/importer/zen_workspaces_parser.{h,cc}` |
| CREATE | `maho-chromium/browser/importer/arc_sidebar_parser.{h,cc}` |
| CREATE | `maho-chromium/browser/importer/mozlz4_decompressor.{h,cc}` (Mozilla `mozLz40\0` magic + lz4_block) |
| CREATE | `maho-chromium/browser/importer/maho_workspace_importer.{h,cc}` (orchestrator: parses → dispatches FFI) |
| CREATE | `maho-chromium/browser/importer/test_data/zen-sessions.sample.jsonlz4` (fixture) |
| CREATE | `maho-chromium/browser/importer/test_data/StorableSidebar.sample.json` (fixture) |
| MODIFY | `maho-chromium/browser/importer/BUILD.gn` — add new sources, deps `//third_party/lz4` |
| MODIFY | `maho-chromium/browser/importer/maho_browser_detector.cc` — add `kWorkspaces = 1 << 6` flag, set true for Zen + Arc only |
| MODIFY | `maho-chromium/browser/ui/webui/maho_welcome/maho_welcome.mojom` — add `ImportWorkspaces(string browser_id) => (WorkspaceImportResult result);` |
| MODIFY | page handler `.cc` — add `ImportWorkspaces` handler that calls `MahoWorkspaceImporter::Run()` |
| MODIFY | `maho-chromium/browser/resources/maho_welcome/react/pages/import-data.tsx` — add "Spaces & Tabs" checkbox (default-checked when source is Zen/Arc) |

**Schema (Zen `zen-sessions.jsonlz4`)** — confirmed via librarian research:
```
~/Library/Application Support/zen/Profiles/<profile>/zen-sessions.jsonlz4

mozLz40 magic (8 bytes "mozLz40\0") + uncompressed-size (4 bytes LE u32) + lz4 block
↓ decompressed JSON:
{
  "spaces":  [{ uuid, name, icon, containerTabId, theme:{type,gradientColors,opacity,rotation,texture} }, ...],
  "tabs":    [{ entries[], pinned, image, zenWorkspace, zenEssential, zenSyncId, zenStaticLabel, ... }, ...],
  "folders": [{ id, name, pinned, workspaceId, parentId, prevSiblingInfo, userIcon }, ...],
  "groups":  [...], "splitViewData": [...]
}
```
- Active space: parse `prefs.js` line `user_pref("zen.workspaces.active", "<uuid>");`.
- `zenEssential: true` → goes to favorites grid; `pinned: true` (no zenEssential) → pinned tab; otherwise regular tab.
- Fallback (old install only): `places.sqlite` tables `zen_workspaces` + `zen_pins`.

**Schema (Arc `StorableSidebar.json`)** — confirmed via librarian research:
```
~/Library/Application Support/Arc/StorableSidebar.json

Path B (preferred): root.firebaseSyncState.syncData.{spaceModels, items}
Path A (fallback):  root.sidebar.containers[1].{spaces, items, topAppsContainerIDs}

ALTERNATING ARRAY FORMAT (critical):
  [uuid, obj, uuid, obj, ...]  — odd positions are UUIDs, even are objects
  Some items wrapped: {"value": {...}} → must unwrap
  childrenIds may be: array | {"_0": [...]} | {"items": [...]}

Space:
  { title, customInfo: {iconType: {icon | emoji_v2}, windowTheme}, profile,
    containerIDs: [type, uuid, type, uuid] (where type ∈ "pinned"|"unpinned") }
Item discriminator:
  data.tab present       → tab (savedURL, savedTitle, timeLastActiveAt)
  data.list / data.itemContainer → folder (recurse via childrenIds)
  data.splitView         → split view (skip for v1)
Favorites:
  topAppsContainerIDs is per-Chrome-profile; match space.profile to profile tag.
Apple epoch: createdAt/timeLastActiveAt are seconds since 2001-01-01 (add 978307200 for Unix).
```

**FFI Dispatch Sequence (from explore agent findings):**
```cpp
MahoCore* core = maho::GetCore();

for (const ParsedWorkspace& ws : parsed_workspaces) {
  // 1. Create the Maho Space (returns SpaceViewModel JSON)
  std::string color_json = MapWorkspaceColorToMahoColor(ws.theme);  // {hue,sat,bri,grain}
  char* svm = maho_core_create_space(core, ws.name.c_str(), color_json.c_str(), "");
  std::string space_id = ParseJsonField(svm, "id");
  maho_string_free(svm);

  // 2. Apply icon/theme via update_space_config event (camelCase keys in `changes`!)
  DispatchUpdateSpaceConfig(core, space_id, ws.icon, ws.theme);

  // 3. For each pinned tab: create_tab + pin_tab
  for (const ParsedTab& t : ws.pinned_tabs) {
    std::string tab_id = DispatchCreateTab(core, space_id, t.url);
    DispatchShellEvent("pin_tab", {{"tab_id", tab_id}});
  }

  // 4. For each essential/favorite: create_tab + favorite_tab
  for (const ParsedTab& t : ws.favorites) {
    std::string tab_id = DispatchCreateTab(core, space_id, t.url);
    DispatchShellEvent("favorite_tab", {{"tab_id", tab_id}});
  }

  // 5. For each folder (recursively): create_folder + move_tab_to_folder
  RecreateFolderTree(core, space_id, ws.folder_root);
}

// 6. Activate the original active space (Zen) or first imported (Arc)
maho_core_activate_space(core, primary_space_id.c_str());

// 7. Notify sidebar once (batched repaint)
MahoSpaceProfileBridge::GetInstance()->NotifyChanged();
```

**Color Mapping:**
- Zen `theme.gradientColors[].color` (hex) → primary stop → hex→HSV → `{hue, saturation, brightness, grain:0.0}`.
- Arc `customInfo.windowTheme` → similar HSV extraction.
- Fallback if no theme: `{hue: hash(name) % 360, saturation: 0.6, brightness: 0.85, grain: 0.0}`.

**Icon Mapping:**
- Zen: emoji char or `.svg` URL. If emoji → pass through. If SVG → fallback to first letter.
- Arc: `iconType.icon` (SF-Symbol name like "briefcase") → lookup table to nearest emoji; OR `iconType.emoji_v2` → pass through.

**Other Browsers (Chrome/Brave/Edge/Vivaldi/Opera/Firefox):**
- No Spaces concept. Import as a single space named after the browser (e.g. "Imported from Chrome"). Bookmark bar tabs become regular tabs in that space; Chromium pinned tabs become Maho pinned tabs.
- Firefox Container Tabs: respect only via advanced opt-in flag (deferred from v1).

**FFI Quirks (CRITICAL — from explore findings):**
- `space_id` quoting is **inconsistent** across FFI: raw for `activate_space/rename_space/recolor_space/delete_space`; JSON-quoted (via `base::GetQuotedJSONString`) for `get_space_tabs/get_favorite_tabs/get_folder_view_models/get_sidebar_tree`.
- `update_space_config.changes` requires **camelCase** keys (`spaceId`, `gradientColors`) due to serde `rename_all = "camelCase"`.
- `maho_core_handle_event` returns JSON array of CoreUpdate objects; for `create_tab` find `{"kind":"tab_created","tab":{"id":"..."}}` to extract new tab ID.
- All returned `char*` must be freed with `maho_string_free()`.

**TDD Order:**
1. Test: `mozlz4_decompressor` against known fixture → byte-equal plaintext.
2. Test: `zen_workspaces_parser` with sample JSON → asserts space count, tab→workspace mapping, essential flags.
3. Test: `arc_sidebar_parser` with Path B fixture → asserts spaces + tabs + folders + favorites.
4. Test: `arc_sidebar_parser` with Path A fallback fixture → same assertions.
5. Integration: mock `maho_core_*` records dispatches; orchestrator emits correct sequence.
6. Manual E2E (Zen): real install → 3 spaces in sidebar dot rail, switch works, essentials populated.
7. Manual E2E (Arc): real install → spaces with correct icons, favorites row matches Arc.

**Verify:**
- Sidebar dot rail shows N spaces (one per source workspace).
- Switching spaces loads correct pinned/favorite/folder set.
- Active space matches source's active.
- Folder nesting preserved up to 5 levels (Zen limit).
- Korean/CJK workspace names render correctly.

**Risk:**
- lz4 decompression edge cases (corrupted backups). Mitigation: try `clean.jsonlz4` then `recovery.baklz4` if primary fails.
- Arc schema drift (containerIDs vs newContainerIDs, childrenIds vs childIDs). Mitigation: try-all-keys per arc-browser-mcp parser.
- Zen pre-migration installs (only `places.sqlite`). Mitigation: SQLite fallback path.
- maho-core handle unavailable → graceful "Spaces import skipped" with bookmark fallback to default space.

**Effort:** L | **Dependencies:** Phase 8 (FFI dispatch infrastructure)

---

## Execution Order & Parallelism

### Three Parallel Streams

```
┌─── STREAM A (Data Import) ──────────────────────┐
│ Phase 7  (Bookmark merge)        [S, day 1]      │
│ Phase 1  (History)               [M, days 1-3]   │
│ Phase 3  (Autofill)              [S, day 2]      │
│ Phase 4  (Favicons)              [S, day 3-4]    │
│ Phase 5  (Safari FDA)            [M, days 4-6]   │
│ Phase 6  (Cookies — INCLUDED)    [M, days 5-7]   │
│ Phase 14 (Workspace-as-Space)    [L, days 6-12]  │
└──────────────────────────────────────────────────┘

┌─── STREAM B (Passwords — highest risk) ─────────┐
│ Phase 2a (Chromium OSCrypt)    [M, days 1-4]    │
│ Phase 2b (Firefox NSS)         [L, days 5-10]   │
└──────────────────────────────────────────────────┘

┌─── STREAM C (UX/Animation) ─────────────────────┐
│ Phase 8 (True essentials)      [M, days 1-3]     │
│ Phase 12+13 (Browser + toast)  [S, day 2]        │
│ Phase 10 (Theme improvement)   [M, days 3-5]     │
│ Phase 11 (Completion anim)     [S, day 4]        │
└──────────────────────────────────────────────────┘
```

### Critical Path

- Phase 8 → Phase 14 (workspace dispatch reuses essentials FFI infrastructure)
- Phase 8 → Phase 12 → Phase 11 (animation depends on browser open working)
- Phase 2a → Phase 2b (Firefox NSS depends on Chromium OSCrypt infra)
- Phase 2a → Phase 6 (cookie OSCrypt decrypt reuses password decrypt helper)

---

## Atomic Commit Strategy

### Stream A
```
feat(importer): remove bookmark wrapper folder — merge directly into bar
feat(importer): add chromium history parser with SQLite reader
feat(importer): add firefox history parser from places.sqlite
feat(welcome): wire history import to HistoryService::AddPagesWithDetails
feat(importer): add chromium autofill parser from Web Data
feat(welcome): wire autofill import to AutofillWebDataService
feat(importer): add favicon parser for chromium/firefox
feat(welcome): wire favicon import to FaviconService::SetImportedFavicons
feat(importer): add safari bookmark parser with CFPropertyList
feat(welcome): add FDA retry flow for Safari import UI
feat(importer): add chromium cookie parser with OSCrypt v10 decrypt
feat(importer): add firefox cookie parser from cookies.sqlite
feat(welcome): wire cookie import via CookieManager Mojo
feat(importer): add mozLz40 decompressor with byte-equal fixture test
feat(importer): add zen workspaces parser (jsonlz4 + prefs.js)
feat(importer): add arc sidebar parser (Path B + Path A fallback, alternating arrays)
feat(importer): add maho_workspace_importer orchestrator (FFI dispatch)
feat(welcome): wire ImportWorkspaces Mojo method end-to-end
feat(detector): detect history/passwords/autofill/cookies/workspaces in services_supported
```

### Stream B
```
feat(importer): add chromium password parser with OSCrypt AES-128-CBC decrypt
test(importer): add chromium password decrypt unit tests with known ciphertext
feat(welcome): wire password import to PasswordStore::AddLogin
feat(importer): add NSS decrypt helper (CommonCrypto, all 4 variants)
test(importer): add NSS decrypt tests for variants A-D
feat(importer): add firefox password parser with logins.json
feat(welcome): wire firefox password import end-to-end
```

### Stream C
```
feat(welcome): implement FavoriteEssentialSites via maho-core FFI
refactor(welcome): replace PinEssentialSites with FavoriteEssentialSites
feat(welcome): add OpenMainBrowserWindow Mojo method + toast
feat(welcome): add completion heart pulse + slide-out animation
feat(welcome): add theme mode chips (dark/light/auto) to space-theme page
chore(strings): add IDS_MAHO_TOAST_WELCOME_FINISHED
```

### Integration (Final)
```
feat(welcome): update import-data UI to show all data type checkboxes
test(welcome): add vitest for import state machine with all data types
```

---

## Test/QA Plan

### Unit Tests

| Phase | Test File | What to Test |
|-------|-----------|--------------|
| 1 | `chromium_history_parser_unittest.cc` | Parse known History SQLite fixture, verify URL/title/time |
| 1 | `firefox_history_parser_unittest.cc` | Parse places.sqlite, verify visit_date epoch conversion |
| 2 | `chromium_password_parser_unittest.cc` | Decrypt known v10 ciphertext with known key |
| 2 | `nss_decrypt_helper_unittest.cc` | All 4 NSS variants with known test vectors |
| 3 | `chromium_autofill_parser_unittest.cc` | Parse Web Data fixture |
| 4 | `favicon_parser_unittest.cc` | Extract PNG data from Favicons fixture |
| 5 | `safari_bookmark_parser_unittest.cc` | Parse binary plist fixture |

### Manual QA Demo Script
```bash
# 1. Reset: rm -rf ~/Library/Application\ Support/Maho/Default
# 2. Build: python3 maho-chromium/build/scripts/build_maho.py
# 3. Launch: open chromium/src/out/Default/Maho.app
# 4. Import page → Chrome → check Bookmarks+History+Passwords → Import
# 5. Allow Keychain → verify completion message
# 6. Select essentials → Next → Theme → Next → Dive in!
# 7. Verify: chrome://history, chrome://password-manager, sidebar favorites grid
```

### Regression Guards
```bash
cd maho && bun run test        # Rust tests
cd maho && bun run typecheck   # Cargo check
cd maho-chromium/browser/resources/maho_welcome && bunx vitest run  # Frontend
```

---

## Build/GN Dependency Analysis

### New GN Deps

| Phase | Dep | Cycle Risk |
|-------|-----|-----------|
| 1 | `//components/history/core/browser`, `//chrome/browser/history` | None |
| 2 | `//components/password_manager/core/browser` | None |
| 3 | `//components/autofill/core/browser/webdata` | None |
| 4 | `//components/favicon/core` | None |
| 5 | CoreFoundation.framework (already linked) | None |
| 8 | `//maho/third_party/maho` | None |

All deps are leaf → framework — no cycle risk.

### Entitlements
**None required.** Keychain access uses `SecItemCopyMatching` (standard), macOS prompts user.

### New IDS_* Strings
```
IDS_MAHO_TOAST_WELCOME_FINISHED   "Welcome to Maho!"
IDS_MAHO_IMPORT_KEYCHAIN_DENIED   "Keychain access denied. Passwords were not imported."
IDS_MAHO_IMPORT_FDA_REQUIRED      "Grant Full Disk Access to import Safari data."
IDS_MAHO_IMPORT_MASTER_PASSWORD   "Firefox passwords are master-password protected."
```

---

## Definition of Done

### Binary Acceptance Criteria (all must be true)

1. History import works for all 8 Chromium+Gecko browsers → `chrome://history` shows entries
2. Password import works for Chromium browsers → `chrome://password-manager` shows credentials
3. Firefox password import works (no master password) → credentials visible
4. Autofill import works → autocomplete appears on forms
5. Favicon import works → `chrome://favicon/size/16@1x/<url>` returns icons
6. Safari bookmark import works when FDA granted
7. Bookmarks merge directly into bar (no wrapper folder)
8. Essentials appear in sidebar favorites grid (not pinned tabs)
9. Essentials are lazy (no network until clicked)
10. Theme page has mode selection (dark/light/auto)
11. Completion animation plays (heart pulse + slide-out)
12. Main browser window opens after welcome closes
13. Toast shows on new browser
14. All existing tests pass
15. Build succeeds

### Acceptable Approximations (not parity-blocking)

| Zen Feature | Maho Approximation |
|-------------|-------------------|
| Spring physics | CSS `cubic-bezier(0.4, 0, 0.2, 1)` |
| Embedded `gZenThemePicker` | Mode chips + color swatches |
| Import progress percentage | Status text messages |

---

## Out-of-Scope

| Item | Justification |
|------|---------------|
| **Safari password import** | Requires Apple-private `com.apple.private.tcc.allow` entitlement. Impossible for third-party apps. |
| **Multi-profile Firefox/Zen** | Only default profile imported. Multi-profile UI complexity > marginal gain for v1. |
| **Chromium v20 App-Bound encrypted cookies** (Chrome 127+) | DPAPI-bound; cannot decrypt outside owning Chrome process. Skip with warning. |
| **Firefox Container Tabs** | Imported as plain tabs (no Maho equivalent yet). Advanced flag gates explicit container preservation. |
| **Arc split-view groups** | Imported as separate tabs (Maho has no split-view primitive yet). |
| **History dedup across imports** | `AddPagesWithDetails` merges visit counts naturally. Full dedup is over-engineering. |
| **Opera GX profile variants** | Standard Opera only. |
| **Import progress percentage bar** | Status text sufficient. Percentage requires extra DB pre-count. |

---

## Risk Register

| Risk | Impact | Mitigation |
|------|--------|------------|
| Keychain ACL prompt confuses users | Med | Pre-import explanatory text |
| Firefox NSS variant breaks on new release | High | Graceful "could not decrypt" fallback |
| maho-core handle unavailable during onboarding | High | Null check + fallback to `SetTabPinned` |
| Large history DBs (>500k rows) cause OOM | Med | Batch 5000 rows, 100k cap |
| OSCrypt key derivation fails | Med | Return empty + error message |

---

## File Inventory

### New Files (25)
```
maho-chromium/browser/importer/chromium_history_parser.{h,cc}
maho-chromium/browser/importer/firefox_history_parser.{h,cc}
maho-chromium/browser/importer/chromium_password_parser.{h,cc}
maho-chromium/browser/importer/firefox_password_parser.{h,cc}
maho-chromium/browser/importer/nss_decrypt_helper.{h,cc}
maho-chromium/browser/importer/chromium_autofill_parser.{h,cc}
maho-chromium/browser/importer/favicon_parser.{h,cc}
maho-chromium/browser/importer/safari_bookmark_parser.{h,cc}
maho-chromium/browser/importer/chromium_cookie_parser.{h,cc}
maho-chromium/browser/importer/firefox_cookie_parser.{h,cc}
maho-chromium/browser/importer/mozlz4_decompressor.{h,cc}
maho-chromium/browser/importer/zen_workspaces_parser.{h,cc}
maho-chromium/browser/importer/arc_sidebar_parser.{h,cc}
maho-chromium/browser/importer/maho_workspace_importer.{h,cc}
maho-chromium/browser/importer/test_data/  (parser fixtures)
```

### Modified Files (16)
```
maho-chromium/browser/importer/BUILD.gn
maho-chromium/browser/importer/maho_browser_detector.cc
maho-chromium/browser/importer/maho_bookmark_importer.{h,cc}
maho-chromium/browser/ui/webui/maho_welcome/maho_welcome.mojom
maho-chromium/browser/ui/webui/maho_welcome/maho_welcome_page_handler.{h,cc}
maho-chromium/browser/ui/webui/maho_welcome/BUILD.gn
maho-chromium/browser/resources/maho_welcome/react/{types,store}.ts
maho-chromium/browser/resources/maho_welcome/react/pages/{import-data,essentials,completion,space-theme}.tsx
maho-chromium/browser/resources/maho_welcome/welcome.css
maho-chromium/build/scripts/apply_chromium_src_overrides.py
```

---

## TDD Order Per Parser

1. Write test first with known SQLite/JSON fixture
2. Write parser until test passes
3. Wire page handler integration
4. Manual verification with real browser installation

For UX phases: Mojo IDL → stub handler → wire frontend → implement handler → visual verify.

---

## Open Decisions — **RESOLVED 2026-06-10**

1. ~~Cookie import deferred?~~ → **INCLUDED**. User chose to ship cookies in v1 (Phase 6 active in Stream A). v20 App-Bound rows skipped with warning per Out-of-Scope.
2. ~~Theme picker approach?~~ → **Mode chips + swatches** (native Maho UI; no iframe embed of `gZenThemePicker`).
3. ~~Firefox NSS approach?~~ → **CommonCrypto reimplementation** (~800 LOC, no runtime dep on libnss3.dylib).
4. **Workspace-as-Space import (added 2026-06-10)** → User mandated: "zen/arc 등은 스페이스 자체로 import를 해올 수가 있어야함." → Implemented as Phase 14 in Stream A.
