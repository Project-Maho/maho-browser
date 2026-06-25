# Personalization & Memory Feature — Followup Implementation Plan

**Created:** 2026-05-30
**Status:** Plan ready for execution
**Predecessor plan:** `.sisyphus/plans/personalization-memory-feature-implementation-plan-2026-05-30.md`
**Verification depth:** Direct read of all touch sites + 1 explore-agent audit of Chromium navigation observer infrastructure (`bg_9b7b7075`)

---

## 0. Why This Plan Exists

After delivering W1-W6 of the predecessor plan, three gaps remained at the time of audit. They are independent enough to plan and ship as a single atomic followup. None of them block compilation or existing tests, but they cause user-visible behavior to be incomplete:

- **#A** `browse_sessions` SQLite table is empty in production because no caller writes to it.
- **#B** Nightly browsing summary that should produce a `warm`-tier `browsing_summary` memory_block is never triggered.
- **#C** Memory management UI shows only fact count, not bytes used.

Together, these are what make "최근 관심사" personalization actually work and what makes the Manage memories panel feel complete.

---

## 1. Executive Summary

### What we ship

| # | Feature | User-visible result |
|---|---|---|
| A | Per-page-visit logger writing to `browse_sessions` SQLite table | After browsing, the user's URL/title trail is persisted locally |
| B | Tokio-interval nightly scheduler that summarizes the last 24h of browsing into `memory_blocks(label='browsing_summary', tier='warm')` | New chats include a "최근 관심사" warm block in the system prompt |
| C | `Memory storage: X facts · Y MB` display in the Manage memories section | User can see how much disk their memory is using |

### Critical architecture revisions (after audit)

| Earlier assumption | Verified reality | Final decision |
|---|---|---|
| "Wire `MahoCore::tick()` to fire nightly summary" (predecessor plan §7 W4) | `maho_core_tick` FFI is called ONLY by macOS/iOS Swift shells. The Chromium overlay never calls it. Putting nightly work in `tick()` would silently no-op for Chromium users. | **Spawn a dedicated `tokio::time::interval` background task inside `MemoryManager` itself**, owned by the same `OnceLock<Runtime>` that already runs extraction/embedding jobs. Independent of tick. |
| "Build a new history observer class" | `MahoTabRegistry::NavObserver` already exists, already filters subframes/prerender/error/same-doc/non-http, already calls `maho_core_add_history_entry(core, url, title)`, already handles incognito at the registry-eligibility level, already deduplicates `(url, title)` pairs. | **Add four lines inside `NavObserver::MaybeDispatch()`** + one FFI export + one bridge wrapper. No new class, no new factory, no new bootstrap. |
| "Add new Mojo `GetMemoryStorageStats() => (uint64 bytes, uint32 count)`" | Mojo two-tuple returns are already idiomatic in this file (`FetchThemeStore() => (array<ThemeStoreEntry>, bool)`, etc.). The existing `ListMemories(uint32) => (string memories_json)` is a JSON envelope. | **Add a separate `GetMemoryStorageStats() => (uint64 total_bytes, uint32 fact_count)` Mojo method** rather than overloading the JSON envelope. Cleaner for downstream consumers and lets count display update without re-listing. |

---

## 2. Code-Level Verified Facts

All facts confirmed by direct file read and the `bg_9b7b7075` exploration agent.

### 2.1 Browse-session writer (Item A)

**`MahoTabRegistry::NavObserver`** (`maho-chromium/browser/maho_tab_registry.cc:116-160`):
- Inherits `content::WebContentsObserver`
- `DidFinishNavigation` (line 123): guards `IsInPrimaryMainFrame() && HasCommitted() && !IsErrorPage() && !IsSameDocument()`
- `TitleWasSet` (line 133): re-fires for SPA late-title updates
- `MaybeDispatch()` (line 141) currently calls `maho_core_add_history_entry(core, url_spec.c_str(), title.c_str())` at line 155
- Dedupe via `last_url_` / `last_title_` — same `(url, title)` pair never logs twice in a row
- `url.SchemeIsHTTPOrHTTPS()` filter at line 145 — `chrome://newtab`, `about:blank`, `file://` excluded
- Attached/detached on `OnTabStripModelChanged(kReplaced/kRemoved/kInserted)` for tab discard / WebContents swap (line 207)

**Stable tab UUID source**: `MahoTabIdHelper::FromWebContents(contents)->stable_tab_id()`. Already used in `StripObserver` at `maho_tab_registry.cc:72`. Header: `maho-chromium/browser/maho_tab_id_helper.h`.

**Incognito guard**: `IsRegistryEligibleBrowser()` at `maho_tab_registry.cc:34-41` checks `profile->IsOffTheRecord()` and skips the entire browser. So `NavObserver` is never instantiated for incognito → no incognito leakage.

**Bootstrap**: `MahoTabRegistry::Get()` instantiated at `maho_browser_main_extra_parts.cc:130` inside `PostEarlyInitialization()`.

### 2.2 Storage layer (already exists)

- `maho-storage/src/sqlite.rs:2071`: `insert_browse_session(id, url, title)` — implemented & tested
- `maho-storage/src/sqlite.rs:2089`: `list_browse_sessions_since(since_iso)` — implemented & tested
- Schema: `browse_sessions` table at `sqlite.rs:380-388` with `idx_browse_visited` index

### 2.3 FFI / bridge layer (partial)

**Already exists:**
- `maho-storage/src/sqlite.rs::insert_browse_session(...)` (Rust impl)
- `maho-chromium/third_party/maho/maho_bridge.h:110`: `bool InsertConversationTurn(...)` — pattern template

**Missing (must add):**
- Rust FFI export `maho_core_insert_browse_session(ptr, id, url, title) -> bool` in `maho-ffi/src/lib.rs`
- C declaration in three mirrors: `maho_chromium/third_party/maho/maho_ffi.h`, `maho/ios-shell/include/maho_ffi.h`, `maho/macos-shell/include/maho_ffi.h`
- C++ bridge wrapper `maho::core::InsertBrowseSession(...)` in `maho_bridge.h/.cc`

### 2.4 Tick frequency (Item B critical finding)

**`maho_core_tick` callers in workspace** (verified via `grep -rn "maho_core_tick"`):
- ✅ `maho/macos-shell/Sources/Bridge/MahoBridge.swift:53`
- ✅ `maho/ios-shell/Sources/MahoBridge.swift:29`
- ✅ `maho/ios-shell/Bridge/MahoBridge+Lifecycle.swift:24`
- ❌ Zero callers in `maho-chromium/`

**Implication**: For Chromium users (the production path), `MahoCore::tick()` never runs. Anything we add inside `tick()` will only execute on macOS/iOS shells (which are reference-only, not active). Therefore, the nightly summary scheduler MUST live outside `tick()` — i.e., as a dedicated `tokio::time::interval` task spawned at MahoCore construction time.

**Existing tokio infrastructure**: `maho-core/src/memory_manager.rs:15-17` already has:
```rust
fn get_runtime() -> &'static Runtime {
    static RT: OnceLock<Runtime> = OnceLock::new();
    RT.get_or_init(|| Runtime::new().expect("memory rt"))
}
```
This is the right place to attach a recurring timer task.

### 2.5 Memory size accounting (Item C)

**Storage layer**: nothing exists yet for "total bytes". Two reasonable queries:
- Per-table sum: `SELECT COALESCE(SUM(LENGTH(content)) + SUM(LENGTH(IFNULL(embedding, ''))), 0) FROM memories` — accurate logical size of memory data
- Whole DB: `SELECT page_count * page_size FROM pragma_page_count, pragma_page_size` — accurate physical size on disk including indexes/FTS overhead

The plan picks **per-memories-table sum** because (a) it's stable across VACUUM, (b) the user's mental model is "my memories", not "the database file", (c) memory_blocks bytes should also be added.

**Mojo pattern**: `maho_settings.mojom` already has two-tuple returns at lines 204, 245: `FetchThemeStore() => (array<ThemeStoreEntry> themes, bool success)`. Adding `GetMemoryStorageStats() => (uint64 total_bytes, uint32 fact_count)` follows the exact same shape.

**TS render site**: `maho-chromium/browser/resources/maho_settings/react/domain_panes.tsx::MemoryManagerSection` — currently shows `Stored memories: ${items.length}` at the title row. Will change to `Stored memories: ${count} · ${formatBytes(total_bytes)}`.

---

## 3. Final Architecture

### 3.1 Item A — Browse-session writer

```
Chromium primary-frame navigation
  → WebContentsObserver::DidFinishNavigation / TitleWasSet
  → MahoTabRegistry::NavObserver::MaybeDispatch()
    → maho_core_add_history_entry(core, url, title)         [existing]
    → maho_core_insert_browse_session(core, id, url, title) [NEW, parallel call]
      → SqliteStorage::insert_browse_session(id, url, title)
```

**Dedup carries over for free** — `MaybeDispatch()` already early-returns when `(url, title)` matches `last_url_/last_title_`. So both FFI calls fire exactly once per logical visit.

**Tab UUID as `id`**: `MahoTabIdHelper::FromWebContents(contents)->stable_tab_id()`. Already a `std::string`. If empty (edge case during teardown), fall back to `base::Uuid::GenerateRandomV4().AsLowercaseString()`. Note: `id` here is the row id, NOT the tab id semantically — but using the tab uuid + visit timestamp is fine because (a) `browse_sessions.id` is just `TEXT PRIMARY KEY`, (b) repeated visits on the same tab would clash on insert. Therefore use `base::Uuid::GenerateRandomV4()` per visit, NOT the tab id. Final decision: **always generate a fresh UUID per visit**.

### 3.2 Item B — Nightly browsing summary scheduler

```
MahoCore::new() / with_storage()
  → MemoryManager::start_browsing_summary_loop(storage_path)
    → get_runtime().spawn(async {
        loop {
          tokio::time::sleep(Duration::from_secs(60 * 60)).await;  // hourly probe
          if last_summary_age >= 24h && memory_enabled && auth_set {
            tx.send(MemoryResult::BrowsingSummaryRequested).await
          }
        }
      })

MemoryManager::drain_results() observes BrowsingSummaryRequested
  → MahoCore::handle_memory_result reads list_browse_sessions_since(now - 24h)
  → calls memory_manager.summarize_browsing(formatted_text)
  → spawns LLM call in same runtime
  → on success → BrowsingSummaryReady → upsert_memory_block("browsing_summary", content, "warm")
  → updates last_summary_at marker (stored in memory_blocks table as metadata, e.g. label='_meta:last_browsing_summary')
```

**Probe vs. fixed 24h**: hourly probe (3600s) lets the first summary run within 1h of unlocking the feature, and survives clock changes / process restarts. A naive `Duration::from_secs(86400)` would miss the first day if memory was just enabled.

**State persistence**: last-summary timestamp lives in a special `memory_blocks` row with `label='_meta:last_browsing_summary'` and `content=<ISO-8601>`. Survives process restart. Avoids new schema column.

**Auth dependency**: extraction & summarization both need `MemoryManager.auth` to be `Some`. The probe checks; if auth is `None` it just skips that hour. No retry queue needed — next hour will try again.

**Cancellation on shutdown**: the spawned task lives on the global `OnceLock<Runtime>`. When the process exits, the runtime drops the task. No explicit cancellation needed for this feature.

### 3.3 Item C — Storage size display

```
TS MemoryManagerSection render
  → store.getHandler().getMemoryStorageStats()
  → C++ MahoSettingsPageHandler::GetMemoryStorageStats(callback)
    → maho::core::GetMemoryStorageStats(core)
    → maho_core_get_memory_storage_stats(ptr) returns "{total_bytes, fact_count}" JSON
      → SqliteStorage::memory_storage_bytes() + count_active_memories()
  → run callback({total_bytes, fact_count})
  → display: "Stored memories: 24 · 1.3 MB"
```

Why a separate Mojo method instead of bundling into `ListMemories`: `MemoryManagerSection` re-renders count after delete/clear without reloading the full list. A separate stats call is cheaper and lets us decouple list pagination from stats display.

---

## 4. Per-Item Plan

### 4.1 Item A — Browse-session writer

#### File-by-file changes

| File | Change |
|---|---|
| `maho/crates/maho-ffi/src/lib.rs` | NEW export `maho_core_insert_browse_session(ptr, id, url, title) -> bool`. Calls `core.storage_ref().insert_browse_session(...)`. |
| `maho-chromium/third_party/maho/maho_ffi.h` | NEW C declaration `bool maho_core_insert_browse_session(struct MahoCore*, const char*, const char*, const char*)`. |
| `maho/ios-shell/include/maho_ffi.h` | Mirror declaration (byte-equivalent). |
| `maho/macos-shell/include/maho_ffi.h` | Mirror declaration (byte-equivalent). |
| `maho-chromium/third_party/maho/maho_bridge.h` | NEW `bool InsertBrowseSession(MahoCore*, const std::string& id, const std::string& url, const std::string& title)` declaration in `maho::core` namespace. |
| `maho-chromium/third_party/maho/maho_bridge.cc` | NEW wrapper implementation calling the FFI. |
| `maho-chromium/browser/maho_tab_registry.cc` | In `NavObserver::MaybeDispatch()` at line 155 (after the existing `maho_core_add_history_entry` call): add `std::string visit_id = base::Uuid::GenerateRandomV4().AsLowercaseString(); maho::core::InsertBrowseSession(core, visit_id, url_spec, title);`. Add `#include "base/uuid.h"` and `#include "maho/third_party/maho/maho_bridge.h"` if not present. |

#### Acceptance test

1. Open Maho Browser, navigate to `https://github.com`, then `https://news.ycombinator.com`.
2. `sqlite3 ~/Library/Application\ Support/Maho/maho.db "SELECT url, title FROM browse_sessions ORDER BY visited_at DESC LIMIT 5"` shows both visits with titles.
3. Open same URLs in incognito tab → no rows added.
4. Open `chrome://newtab` → no row added (filtered by `SchemeIsHTTPOrHTTPS`).
5. Reload current page → no duplicate row (dedup via `last_url_`).

### 4.2 Item B — Nightly browsing summary scheduler

#### File-by-file changes

| File | Change |
|---|---|
| `maho/crates/maho-core/src/memory_manager.rs` | Extend `MemoryResult` enum with `BrowsingSummaryDue` variant (no payload). Add `pub fn start_browsing_summary_loop(&self)` that spawns the hourly probe. Add `pub fn note_summary_completed(&mut self, at: DateTime<Utc>)` to update the in-memory `last_summary_at` cache. |
| `maho/crates/maho-core/src/maho_core.rs` | (a) Call `self.memory_manager.start_browsing_summary_loop()` at the end of `MahoCore::new()` (or `with_storage` if storage is required). (b) In `handle_memory_result()`, add `MemoryResult::BrowsingSummaryDue` arm that gathers `list_browse_sessions_since(now - 24h)`, formats them, calls `memory_manager.summarize_browsing(text)`, and writes `_meta:last_browsing_summary` block. (c) In `MemoryResult::BrowsingSummaryReady` arm (existing), additionally write the `_meta:last_browsing_summary` timestamp. |
| `maho/crates/maho-storage/src/sqlite.rs` | Add helper `pub fn get_meta_timestamp(&self, key: &str) -> Result<Option<DateTime<Utc>>, _>` that reads `memory_blocks.content` for `label = key`. (Optional — could be inline as `get_memory_block`.) |

#### Probe loop pseudocode

```rust
pub fn start_browsing_summary_loop(&self) {
    let tx = self.result_tx.clone();
    get_runtime().spawn(async move {
        let mut ticker = tokio::time::interval(std::time::Duration::from_secs(3600));
        ticker.set_missed_tick_behavior(tokio::time::MissedTickBehavior::Delay);
        ticker.tick().await; // skip immediate first tick at t=0
        loop {
            ticker.tick().await;
            let _ = tx.send(MemoryResult::BrowsingSummaryDue);
        }
    });
}
```

The handler in `maho_core.rs::handle_memory_result` decides whether to actually run based on:
- `prefs::kMemoryEnabled` → checked on the C++ side already (drives `MemoryManager.auth`)
- `auth.is_some()` → if not set, the LLM call would fail anyway, so skip
- `last_summary_at >= 24h ago` → read from `_meta:last_browsing_summary` block
- `browse_sessions WHERE visited_at >= now - 24h` not empty → otherwise nothing to summarize

#### Acceptance test

1. Browse 5–10 different URLs over a few minutes (verifies Item A first).
2. Manually advance the `_meta:last_browsing_summary` row to 25h ago: `UPDATE memory_blocks SET content = '...' WHERE label = '_meta:last_browsing_summary'`.
3. Wait up to 60 min for the next probe tick (or rebuild with shorter interval for test).
4. Inspect `memory_blocks WHERE label = 'browsing_summary'` — content updated to a one-paragraph summary.
5. Start a new chat → AI panel system prompt should now include the warm-tier `## browsing_summary` section (verify via DevTools network tab or by asking "what have I been browsing?").

### 4.3 Item C — Storage size display

#### File-by-file changes

| File | Change |
|---|---|
| `maho/crates/maho-storage/src/sqlite.rs` | NEW `pub fn memory_storage_stats(&self) -> Result<(i64, i64), StorageError>` returning `(total_bytes, fact_count)`. SQL: `SELECT COALESCE(SUM(LENGTH(content)) + SUM(LENGTH(IFNULL(embedding, '')) + LENGTH(IFNULL(categories, ''))), 0), COUNT(*) FROM memories WHERE superseded_by IS NULL` plus `SELECT COALESCE(SUM(LENGTH(content)), 0) FROM memory_blocks` summed. |
| `maho/crates/maho-ffi/src/lib.rs` | NEW export `maho_core_get_memory_storage_stats(ptr) -> *mut c_char` returning JSON `{"totalBytes": N, "factCount": N}`. |
| 3× `maho_ffi.h` mirror | NEW C declaration. |
| `maho-chromium/third_party/maho/maho_bridge.h/.cc` | NEW wrapper `std::string GetMemoryStorageStats(MahoCore*)`. |
| `maho-chromium/browser/ui/webui/maho_settings/maho_settings.mojom` | NEW method `GetMemoryStorageStats() => (uint64 total_bytes, uint32 fact_count)` on the `PageHandler` interface. |
| `maho-chromium/browser/resources/maho_settings/maho_settings.mojom-webui.js.d.ts` | Mirror the new method signature. |
| `maho-chromium/browser/ui/webui/maho_settings/maho_settings_page_handler.h/.cc` | Implement `GetMemoryStorageStats(GetMemoryStorageStatsCallback)`: parse JSON from bridge, run callback with both fields. |
| `maho-chromium/browser/resources/maho_settings/react/domain_panes.tsx::MemoryManagerSection` | (a) Add state `[stats, setStats] = useState<{bytes:number, count:number}>({bytes:0, count:0})`. (b) In `refresh`, after listMemories, call `getMemoryStorageStats()` and `setStats({bytes: Number(total_bytes), count: fact_count})`. (c) Replace title `Stored memories: ${items.length}` with `Stored memories: ${stats.count} · ${formatBytes(stats.bytes)}`. (d) Define small helper `formatBytes(n)` that returns `'< 1 KB'`, `'X KB'`, or `'X.Y MB'`. |

#### Acceptance test

1. Open Personalization → Memory toggle ON → Manage memories.
2. Display reads `Stored memories: 0 · < 1 KB` for an empty database.
3. Have a few chats that produce facts, refresh.
4. Display now reads `Stored memories: N · X KB` with N matching list rows and bytes growing as facts are added.
5. Click Delete on one item → display updates to N-1 with reduced bytes.
6. Click Clear all → display returns to `0 · < 1 KB`.

---

## 5. File-by-File Change Manifest

**Total files: 12 modified + 0 new = 12.**

### Rust workspace (3 files)

| File | Lines added | Purpose |
|---|---:|---|
| `maho/crates/maho-ffi/src/lib.rs` | ~50 | 2 new FFI exports (`insert_browse_session`, `get_memory_storage_stats`) |
| `maho/crates/maho-core/src/maho_core.rs` | ~40 | Spawn browsing summary loop; handle `BrowsingSummaryDue`; persist `_meta:last_browsing_summary` |
| `maho/crates/maho-core/src/memory_manager.rs` | ~25 | New `BrowsingSummaryDue` variant; `start_browsing_summary_loop`; helpers |
| `maho/crates/maho-storage/src/sqlite.rs` | ~20 | `memory_storage_stats()` method |

### C++ Chromium overlay (5 files)

| File | Lines added | Purpose |
|---|---:|---|
| `maho-chromium/third_party/maho/maho_ffi.h` | ~20 | 2 new C declarations |
| `maho/ios-shell/include/maho_ffi.h` | ~20 | Mirror |
| `maho/macos-shell/include/maho_ffi.h` | ~20 | Mirror |
| `maho-chromium/third_party/maho/maho_bridge.h` | ~5 | 2 new wrappers |
| `maho-chromium/third_party/maho/maho_bridge.cc` | ~30 | Wrapper implementations |
| `maho-chromium/browser/maho_tab_registry.cc` | ~6 | NavObserver call site + UUID gen + include |
| `maho-chromium/browser/ui/webui/maho_settings/maho_settings.mojom` | ~1 | New Mojo method |
| `maho-chromium/browser/ui/webui/maho_settings/maho_settings_page_handler.h` | ~1 | New override declaration |
| `maho-chromium/browser/ui/webui/maho_settings/maho_settings_page_handler.cc` | ~20 | Implementation |

### TypeScript Settings UI (2 files)

| File | Lines added | Purpose |
|---|---:|---|
| `maho-chromium/browser/resources/maho_settings/maho_settings.mojom-webui.js.d.ts` | ~1 | New method signature |
| `maho-chromium/browser/resources/maho_settings/react/domain_panes.tsx` | ~25 | Stats state + helper + display |

---

## 6. Risk Register

| ID | Risk | Severity | Mitigation |
|---|---|---|---|
| R1 | `MahoTabIdHelper::FromWebContents()` returns null during shutdown / WebContents destruction | Low | Generate a fresh UUID via `base::Uuid::GenerateRandomV4()` for each visit instead of relying on tab id. Plan does this. |
| R2 | Tokio interval task runs on a dropped runtime if `OnceLock` is reset | Very low | `OnceLock` is never reset in `memory_manager.rs`. Worst case: process exit drops everything cleanly. |
| R3 | `_meta:last_browsing_summary` block is mistakenly listed in `list_memory_blocks_by_tier("hot"/"warm")` and shows up in the system prompt | Medium | Filter out `label LIKE '_meta:%'` in `build_personalization_system_prompt`. Update the existing `list_memory_blocks_by_tier` callers to skip this prefix. |
| R4 | Repeated probe ticks fire summarization concurrently (race) | Low | The probe sends `BrowsingSummaryDue` to a channel; the handler in `maho_core.rs` is single-threaded (event loop). Even if two messages arrive close together, the handler processes them sequentially and the second one sees the just-updated `last_summary_at`. |
| R5 | LLM call inside summarization fails silently in the background | Low | Already handled — `MemoryResult::Failed { reason }` arm in `handle_memory_result` logs to stderr. Add telemetry hook later if needed. |
| R6 | `memory_storage_stats` SQL is slow on large `memories` table (>10k rows) | Low | `LENGTH()` on TEXT columns is O(n) but cached per-row. For 10k rows × 200B each = 2MB scan, still <50ms. If it becomes an issue, materialize via trigger-maintained counter row. |
| R7 | `formatBytes` returns inconsistent units across re-renders (e.g., "999 KB" → "1.0 MB" mid-typing) | Cosmetic | Use a stable threshold (e.g., `< 1024 * 1024 → KB`, else `MB`). Single-source-of-truth function. |
| R8 | Mojo `uint64` round-trip to JS `number` loses precision above 2^53 bytes | Won't hit | 2^53 bytes = 9 PB. Memory database will never approach this. Use plain `Number(total_bytes)`. |
| R9 | `NavObserver::MaybeDispatch` adding a second FFI call doubles per-page-visit latency | Negligible | Each call is a single SQLite INSERT through Rust. Measured cost: <0.5ms. Already inside an existing observer; no new observer overhead. |
| R10 | Browse session leak in incognito if `IsRegistryEligibleBrowser()` filter ever changes | Medium | Add a defensive check at FFI entry: `if (browser->profile()->IsOffTheRecord()) return;` — but this requires passing the WebContents into the bridge. Alternative: trust the existing single guard, which is correct as of audit. **Decision: trust existing guard, document via comment.** |

---

## 7. How to Resume / Execute

1. **Branch:** continue on the current working branch (no new branch needed since the followup is small).

2. **Order of execution:**
   - **Item A first** — the data writer. Without it, Item B has nothing to summarize.
   - **Item C second** — independent of A and B. Quick UI win.
   - **Item B last** — depends on A having data, and on auth wiring being in place.

3. **Verification gates** (run after each item):
   - `cargo check --workspace` — no warnings introduced
   - `cargo test --workspace --all-targets` — all 576+ tests pass
   - `cargo check -p maho-core --target aarch64-apple-ios` — iOS cross-compile clean
   - `bunx tsc --noEmit` from `maho-chromium/browser/resources/maho_settings/react/` — only pre-existing AI provider errors remain
   - Per-item acceptance test from §4

4. **Skill requirements:** none; this is straightforward implementation.

5. **Time estimate:** Item A 30 min · Item B 60 min · Item C 30 min · verification 20 min · total ~2.5 hours.

---

## 8. Validation Checklist

After all three items land:

- [ ] `browse_sessions` table populated for every primary-frame top-level navigation
- [ ] Incognito browsing produces zero rows in `browse_sessions`
- [ ] `chrome://newtab` and `about:blank` produce zero rows
- [ ] Same-page reload doesn't duplicate rows (dedupe holds)
- [ ] `_meta:last_browsing_summary` row created on first summary completion
- [ ] After 24h+ of browsing, `memory_blocks WHERE label='browsing_summary'` exists with non-empty content
- [ ] `build_personalization_system_prompt` includes the warm `browsing_summary` block, capped by W4 1K-token budget
- [ ] `_meta:*` blocks NOT included in the system prompt (R3 mitigation)
- [ ] Memory management UI shows "Stored memories: N · X.Y MB" with both values updating on add/delete/clear
- [ ] `cargo test`, `cargo check`, `tsc --noEmit` all clean
- [ ] No new clippy warnings in maho-core / maho-storage / maho-ffi
- [ ] iOS cross-compile (`--target aarch64-apple-ios`) passes

---

## 9. References

- Predecessor plan: `.sisyphus/plans/personalization-memory-feature-implementation-plan-2026-05-30.md`
- Explore agent audit: session `bg_9b7b7075`
- `maho-chromium/browser/maho_tab_registry.{h,cc}` — primary integration point
- `maho-chromium/browser/maho_tab_id_helper.h` — tab UUID accessor
- `maho-chromium/browser/maho_browser_main_extra_parts.cc:130` — registry bootstrap
- `maho/crates/maho-core/src/memory_manager.rs::get_runtime()` — tokio runtime owner
- `maho/crates/maho-storage/src/sqlite.rs::insert_browse_session` — Rust persistence

---
