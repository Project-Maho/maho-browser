# Maho Sync — Remediation Roadmap

**Date:** 2026-07-07
**Status:** Locked design, ready for phase 1 implementation
**Scope:** Rust core sync pipeline (`maho-core`, `maho-storage`, `maho/relay`) + Chromium shell integration points

---

## 0. Executive Summary

### Current state (2026-07-07 audit)

- `SyncEntityType` enum declares 10 syncable types.
- **Only 2 types are wired end-to-end**: `Shortcut` (fully) and `Extension` (partially).
- The remaining 8 — `Space`, `Tab`, `Bookmark`, `Note`, `Boost`, `ReadingListItem`, `SearchEngine`, `Memory` — have zero push functions, zero apply handlers, and zero mutation-site wiring. They exist as declared intent only.
- 13+ additional persisted state types are not even declared in `SyncEntityType` (Settings, Passwords, Autofill, Permissions, Downloads, Easels, CssMods, Profiles, Conversations, Session, Chromium prefs, ATC rules, per-site zoom, shared collections).
- The protocol itself has **4 CRITICAL defects** that cause silent data loss even for the 2 types that appear to sync.

### Root cause categories

1. **Protocol defects** — clock-based versioning without tiebreak, unbounded queue that is never dequeued, offline queue loaded before sync manager is initialized, and shortcut sync split across two independent channels with mismatched clock units.
2. **Coverage gaps** — 8 declared types with no implementation, 13+ types not even declared.
3. **Bootstrap gap** — 30-day relay GC permanently loses state older than the retention window; no snapshot mechanism.

### Design decisions (locked 2026-07-07)

| Decision | Choice | Rationale |
|---|---|---|
| **Sync granularity** | Layered: `Tab` entity carries content fields, `Space` entity carries structural fields | 90%+ of mutations touch Tab-only; aligns with existing LMDB `"spaces"` / `"tabs"` split; standard pattern (Brave/Chrome/Firefox) |
| **Cross-entity refs** | Soft references + best-effort apply | Zero protocol change; industry standard; orphans self-heal on later arrival |
| **Bootstrap** | Client-side periodic encrypted snapshot uploaded to relay | Preserves E2EE; resolves 30-day GC state loss; bounds bootstrap replay volume |

### Phase overview

- **Phase 1 — Protocol Foundation (4 CRITICAL fixes + supporting HIGH fixes).** Nothing else works reliably until this is done.
- **Phase 2 — Entity Coverage (8 missing types wired end-to-end).** Includes the Tab/Space Layered granularity work that transitively fixes the "favorite" issue.
- **Phase 3 — Bootstrap Snapshot Protocol (periodic encrypted snapshots).** Resolves 30-day retention loss.
- **Phase 4 — Extended Coverage (Settings, Autofill, Easels, CssMods, Conversations, etc.).** Requires per-type policy decisions.

---

## 1. Design Principles (from locked decisions)

### 1.1 Layered granularity

Every user-facing tree state splits into two entity classes:

- **Content entity** — per-instance data that describes "what a thing is". Small deltas, high-frequency mutations. Concurrent edits on different instances do not conflict.
- **Structure entity** — container/ordering data that describes "how things are arranged". Low-frequency mutations. LWW acceptable because concurrent structural rearrangements are rare and equally-valid outcomes.

Applied concretely:

| Entity | Class | Owns |
|---|---|---|
| `Tab` | Content | `id, url, title, custom_title, favicon, scroll_position, zoom_level, is_muted, is_pinned, is_favorite, favorite_order, state (lifecycle), pinned_url, last_active_at, created_at, space_id (soft ref)` |
| `Space` | Structure | `id, profile_id, name, color, theme, icon, tab_order, root_order, folders, atc_rules, is_active, last_active_tab_id` |
| `Folder` | (nested in Space) | Serialized as part of Space entity; not a separate sync entity |
| `Bookmark` | Content | `id, url, title, favicon, folder_id (soft ref), created_at, position` |
| `BookmarkFolder` | Structure | `id, name, parent_id (soft ref), created_at, child_order` |
| `Boost` | Content | `id, domain, name, color, typography, zap_selectors, custom_css, created_at, updated_at` |
| `Note` | Content | `id, linked_tab_id (soft ref), linked_url, content, created_at, updated_at` |
| `ReadingListItem` | Content | `id, url, title, excerpt, site_name, favicon_url, preview_image_url, added_at, read_at, is_read, tags` |
| `SearchEngine` | Content | `id, name, url_template, shortcut, icon_url, is_default` |
| `Memory` | Content | `id, fact, source, session_id, categories, importance, metadata, embedding` |

### 1.2 Soft references

All cross-entity references (Tab.space_id, Space.tab_order[], Folder.tab_ids[], Bookmark.folder_id, etc.) are **soft**:

- Applying an entity whose referenced target is missing is **not an error**. Store the entity as-is.
- Renderers **must** treat unresolved references as "pending":
  - `Tab.space_id → nonexistent Space` → Tab goes to an "Unassigned" pseudo-space in UI, real space column is preserved.
  - `Space.tab_order[X, Y, Z]` where `Y` is not present locally → render X and Z, leave Y's slot empty. When Y arrives, it slots in.
  - `Folder.tab_ids[]` same as `tab_order`.
- No pending-apply queue, no retry graph. Just apply and let renderers be tolerant.

### 1.3 Encrypted opaque payloads

Sync entity metadata (`entity_type`, `entity_id`, `version`, `modified_at`, `deleted`) remains plaintext for relay dedupe/ordering. Only `payload_json` is encrypted with the sync key. Snapshot uploads follow the same rule — snapshot body encrypted, metadata plaintext.

### 1.4 LWW with hybrid logical clock

`SyncEntity.version` is replaced with `(hlc_ts: u64, device_id: u32)` where:

- `hlc_ts` is a monotonically increasing per-device counter derived from `max(wall_clock_ms, last_seen_ts) + 1` at push time. Survives restarts (persisted in SQLite).
- `device_id` is a stable persistent UUID (32-bit hash), assigned on first launch and stored in the account state.
- Comparison: `(a.hlc_ts, a.device_id) > (b.hlc_ts, b.device_id)` lexicographically.

This eliminates clock skew data loss (CRITICAL-3).

### 1.5 Idempotent apply

Every apply handler is idempotent by version comparison. `apply_remote_entity` records `(entity_type, entity_id) → (hlc_ts, device_id)` in a **persistent** `entity_versions` table (currently in-memory LRU). This survives restarts and prevents Issue 7 (re-apply on reconnect).

---

## 2. Phase 1 — Protocol Foundation

**Goal:** Fix the 4 CRITICAL protocol defects and 3 HIGH-severity supporting issues. After Phase 1, `Shortcut` and `Extension` sync (the 2 currently-wired types) actually work reliably.

**Estimated effort:** 8–12 engineer-days.

### 2.1 Ordering

Phase 1 tasks must be completed in this order due to dependencies:

```
1.1 (persistent device_id) ─┐
                            ├→ 1.3 (HLC version) ──┐
1.2 (persistent last_pulled_version per type) ─────┼→ 1.5 (idempotent apply + persistent entity_versions)
                                                    │
1.4 (queue lifecycle: enqueue/mark_sent/ack) ──────┼→ 1.6 (fix load_sync_queue init order)
                                                    │
                                                    └→ 1.7 (collapse shortcut dual channel)
```

### 2.2 Task 1.1 — Persistent device_id

**Fixes:** MEDIUM-12 (device_id changes every restart).

**Files:**
- `maho/crates/maho-core/src/maho_core.rs:1154` — replace nanosecond-derived ID
- `maho/crates/maho-core/src/account_manager.rs` — add `device_id` field to persisted account state
- `maho/crates/maho-storage/src/lmdb.rs` — reuse existing `"account"` key

**Design:**
- On `AccountManager::load()`, if no `device_id` present, generate `Uuid::new_v4()` and persist immediately.
- Expose `AccountManager::device_id() -> Uuid` (or 32-bit hash for compactness in HLC).
- `MahoCore::sync_device_id` becomes a computed getter returning the persisted value.
- Migration: on first launch after this change, generate and persist. No back-compat needed (device_id is not exposed anywhere externally yet).

**Testing:**
- Unit: `device_id` stable across `AccountManager::load()` calls on the same LMDB store.
- Integration: restart the process, verify same `device_id`.

### 2.3 Task 1.2 — Per-type persistent `last_pulled_version`

**Fixes:** CRITICAL-4 (only Tab tracked, all other types full-resync every reconnect + in-memory only).

**Files:**
- `maho/crates/maho-core/src/sync_manager.rs:148` — remove Tab-only guard
- `maho/crates/maho-storage/src/sqlite.rs` — table `sync_state` already exists with `(entity_type TEXT PK, last_pulled_version INTEGER)`; add helpers if missing.
- `maho/crates/maho-core/src/maho_core.rs` — wire load/save on start_sync and after each apply.

**Design:**
- Change `SyncManager::last_pulled_version: u64` to `last_pulled_versions: HashMap<SyncEntityType, u64>`.
- On `apply_remote_entity`, unconditionally update `last_pulled_versions[entity.entity_type] = max(current, entity.version)`.
- On `start_sync`, load from SQLite `sync_state` table.
- Persist on a debounced timer (e.g., every 5s if dirty) and on graceful shutdown.
- `EntityPull` messages send per-type `since_version` values.

**Testing:**
- Unit: after applying entities of types A and B with versions 10 and 20, `last_pulled_versions[A]=10, last_pulled_versions[B]=20`.
- Integration: restart, verify per-type versions survive.

### 2.4 Task 1.3 — HLC-based versioning

**Fixes:** CRITICAL-3 (wall-clock ms causes silent data loss under clock skew).

**Files:**
- `maho/crates/maho-core/src/sync_models.rs:110–116` — extend `SyncEntity` with `device_id: u32`, rename `version` semantically (still `u64`, but now HLC timestamp).
- `maho/crates/maho-core/src/maho_core.rs:4525, 5620, 5636` — replace `chrono::Utc::now().timestamp_millis()` with `self.next_hlc_ts()`.
- `maho/crates/maho-core/src/sync_manager.rs:145` — comparison logic `(entity.version, entity.device_id) > (local.version, local.device_id)`.
- `maho/crates/maho-storage/src/sqlite.rs` — add `hlc_state` table `(id INTEGER PK CHECK (id=0), last_hlc_ts INTEGER)` for persistence.

**Design:**

HLC increment algorithm at push time:
```
now_ms = wall_clock_now_ms()
last_hlc = load_or_zero()
new_hlc = max(now_ms, last_hlc + 1)
persist(new_hlc)
return new_hlc
```

HLC update on receive (for causal ordering):
```
new_local_hlc = max(local_hlc, received_hlc, wall_clock_now_ms()) + 1
persist(new_local_hlc)
```

Comparison for LWW: `(a.hlc_ts, a.device_id) > (b.hlc_ts, b.device_id)` — this ensures that even at the same HLC ts (very rare), device_id breaks ties deterministically.

**Wire format migration:**
- `SyncEntity` payload gains `device_id: u32` field. Old peers without this field are treated as `device_id: 0`, which is fine for a one-shot migration.
- Add `schema_version: u8` to `SyncEntity` if not already there, currently 1.

**Testing:**
- Unit: HLC never regresses across `next_hlc_ts()` calls, even with mocked backward wall clock.
- Unit: LWW with same hlc_ts and different device_ids resolves deterministically.
- Integration: two devices, one with clock skew +10s, both push same entity in physical succession — later physical write wins.

### 2.5 Task 1.4 — Complete queue lifecycle (enqueue → mark_sent → ack)

**Fixes:** CRITICAL-2 (queue never dequeued; unbounded growth; startup replay storm).

**Files:**
- `maho/crates/maho-core/src/sync_manager.rs` — track outbound message IDs, correlate acks
- `maho/crates/maho-core/src/maho_core.rs:6470` (`sync_push_entity_persisted`) — capture queue row ID on enqueue, pass to sync manager
- `maho/crates/maho-storage/src/sqlite.rs:1785, 1797` — existing `mark_sync_entities_sent` and `ack_sync_entities` (unused today)

**Design:**

Queue rows have three states:
- `sent_at IS NULL` → pending, not yet handed to sync manager
- `sent_at IS NOT NULL AND acked_at IS NULL` → in-flight, waiting for relay ack
- `acked_at IS NOT NULL` → delivered, eligible for hard delete

Flow:
1. `enqueue_sync_entity()` returns `queue_row_id`.
2. `sync_push_entity_persisted()` calls `sm.push_entity(entity, queue_row_id)`.
3. When sync manager sends over WebSocket, it records `(msg_seq → queue_row_id)` map and calls `storage.mark_sync_entities_sent([queue_row_id])`.
4. When relay sends `{"type":"ack","seq":N}` for a push, look up `queue_row_id`, call `storage.ack_sync_entities([queue_row_id])` which deletes the row.
5. Add `acked_at` column to `sync_queue` if not present. Prune ack'd rows after 24h (safety window in case client needs to re-inspect).

**Migration:**
- Add `acked_at INTEGER` column to `sync_queue`.
- On first run after upgrade, one-time cleanup: delete all rows with `sent_at IS NOT NULL` older than 30 days (stale in-flight from before this fix).

**Testing:**
- Unit: enqueue → mark_sent → ack → row deleted.
- Unit: ack of unknown row is no-op.
- Integration: push 100 entities, receive 100 acks, verify queue is empty.

### 2.6 Task 1.5 — Persistent idempotent apply state

**Fixes:** HIGH-7 (LRU in-memory + 10K cap → old entities re-applied on reconnect).

**Files:**
- `maho/crates/maho-core/src/sync_manager.rs:34, 145` — `entity_versions` becomes persistent
- `maho/crates/maho-storage/src/sqlite.rs` — new table

**Design:**

New table:
```sql
CREATE TABLE entity_versions (
    entity_type TEXT NOT NULL,
    entity_id TEXT NOT NULL,
    hlc_ts INTEGER NOT NULL,
    device_id INTEGER NOT NULL,
    PRIMARY KEY (entity_type, entity_id)
);
CREATE INDEX idx_entity_versions_type ON entity_versions(entity_type);
```

- `apply_remote_entity` before applying: query `entity_versions` for `(type, id)`.
- If `(entity.hlc_ts, entity.device_id) > (local.hlc_ts, local.device_id)`, apply and upsert.
- Else, reject (already applied or older).

**Cache:** Keep the in-memory LRU (10K entries) as a **cache in front of** the persistent table. Reads hit LRU first, miss falls through to SQLite. Writes update both.

**Testing:**
- Unit: apply same entity twice → second is a no-op.
- Unit: after restart, apply the same entity again → rejected (persistent state survives).
- Unit: apply newer version of an existing entity → accepted; then re-apply older → rejected.

### 2.7 Task 1.6 — Fix `load_sync_queue` init order

**Fixes:** CRITICAL-1 (queue silently dropped because `sync_manager` is `None` when queue loads).

**Files:**
- `maho/crates/maho-core/src/maho_core.rs:1708, 6186` (both `load_state` and `start_sync`)

**Design:**

Two options; use Option A.

**Option A (recommended): Deferred flush.**
- Remove the call from `load_state`.
- Add call at the end of `start_sync()`, after `sync_manager = Some(...)` is assigned.
- Guarantees the manager exists before flush.

**Option B: Buffer approach.**
- Keep call in `load_state`, but have it push into a `Vec<SyncEntity>` buffer stored on `MahoCore`.
- On `start_sync`, drain the buffer into the sync manager.
- More code, no benefit over Option A.

**Testing:**
- Unit: create SQLite with queue rows, call `MahoCore::new()` + `start_sync()`, verify `sync_manager.pending_pushes.len() > 0`.
- Regression: verify a queue row enqueued while online is NOT double-flushed on subsequent restart (queue rows should be dequeued before next start via Task 1.4).

### 2.8 Task 1.7 — Collapse shortcut dual sync channels

**Fixes:** CRITICAL-2b (shortcut WebSocket-channel vs REST-channel with mismatched clock units; REST channel permanently locked out once WebSocket touches a shortcut).

**Files:**
- `maho/crates/maho-core/src/maho_core.rs:5644-5663` (`trigger_sync_shortcuts_to_relay_async`) — **delete**
- `maho/crates/maho-core/src/maho_core.rs` — `sync_shortcuts_from_relay` — **delete**
- `maho/relay/server/src/shortcuts.rs` — remove `PUT /api/v1/shortcuts` and `GET /api/v1/shortcuts` endpoints
- `maho/relay/server/src/main.rs` — remove shortcut route registration
- Remove any `sync_trigger_suppressed` special-cases specific to shortcut REST path.

**Design:**

All shortcut sync goes through the standard WebSocket entity channel that already works (`push_shortcut_sync` + `apply_sync_remote_entities` Shortcut arm). Delete the parallel REST path entirely.

**Migration:**
- Some devices may push to the REST endpoint after upgrade if server is upgraded first. Server should 404 the old paths cleanly.
- Some devices may still call `sync_shortcuts_from_relay` after client upgrade — remove all callers in one commit; verify no dangling call sites.

**Testing:**
- Integration: two devices, both edit shortcuts, verify convergence via WebSocket channel only.
- Regression: verify REST endpoints return 404 on the upgraded relay.

### 2.9 Phase 1 acceptance criteria

- All 4 CRITICAL issues have failing tests that now pass.
- Shortcut sync between two devices survives:
  - Sender restart mid-sync (queue survives, is delivered on reconnect).
  - Receiver restart (persistent entity_versions prevents double-apply).
  - Clock skew of ±30s between devices (HLC ensures newer physical write wins).
  - Offline period of 60+ days on receiver (Phase 3 required for full fix — Phase 1 alone still loses state older than 30-day relay retention; documented as known limitation until Phase 3).
- Extension sync passes same test matrix.
- SQLite `sync_queue` remains bounded (< 100 rows) under sustained sync activity.

---

## 3. Phase 2 — Entity Coverage

**Goal:** Wire the 8 missing declared `SyncEntityType` variants end-to-end using Layered granularity + Soft references. The "favorite" issue is subsumed by Tab entity wiring.

**Estimated effort:** 15–20 engineer-days.

**Prerequisite:** Phase 1 complete.

### 3.1 Ordering

```
2.1 Tab entity ──┐
                  ├→ 2.3 Bookmark ──→ 2.4 Note ──→ 2.5 Boost ──→ 2.6 ReadingListItem ──→ 2.7 SearchEngine ──→ 2.8 Memory
2.2 Space entity ─┘
```

Tab and Space are wired first (they're the most complex and set the pattern). The other 6 entity types follow the same pattern once, one per commit.

### 3.2 Task 2.1 — Tab entity end-to-end

**Files:**
- `maho/crates/maho-core/src/sync_models.rs` — no changes needed (variant already declared)
- `maho/crates/maho-core/src/maho_core.rs` — add `push_tab_sync(tab_id)`, `push_tab_sync_deleted(tab_id)`, apply arm in `apply_sync_remote_entities`
- `maho/crates/maho-core/src/tab_lifecycle.rs` and `event_dispatcher.rs` — call push functions at every tab mutation site
- `maho/crates/maho-types/src/tab.rs` — no changes; entire `Tab` struct is the payload

**Content fields (in Tab entity):** `id, url, title, custom_title, favicon, state (lifecycle), is_pinned, is_favorite, favorite_order, is_muted, zoom_level, created_at, last_active_at, scroll_position, pinned_url, space_id (soft), window_id (soft)`

**Structural fields excluded from Tab entity:** Tab position within `Space.tab_order` — this belongs to Space entity, so reordering doesn't mutate Tab entity.

**Push call sites** (Tab mutations):
- `handle_event(ShellEvent::CreateTab)` — after tab created
- `handle_event(ShellEvent::CloseTab)` — deletion tombstone
- `handle_event(ShellEvent::NavigateTab)` — url/title change
- `handle_event(ShellEvent::UpdateTabMetadata)` — title, favicon change
- `handle_event(ShellEvent::PinTab | UnpinTab)` — is_pinned toggle
- `MahoCore::favorite_tab` at `maho_core.rs:4335` — is_favorite, favorite_order (this is where the current favorite bug is fixed)
- `MahoCore::unfavorite_tab` at `maho_core.rs:4376`
- `handle_event(ShellEvent::MuteTab | UnmuteTab)`
- `handle_event(ShellEvent::SetZoomLevel)`
- `MahoCore::update_tab_scroll_position`
- Tab lifecycle transitions in `tab_lifecycle.rs` (Active ↔ Frozen ↔ Suspended ↔ Archived)

**Apply arm:** In `apply_sync_remote_entities`:
```
else if entity.entity_type == SyncEntityType::Tab {
    if entity.deleted {
        self.tab_manager.remove_tab(&entity_id);
    } else {
        let tab: Tab = serde_json::from_str(&entity.payload_json)?;
        self.tab_manager.upsert_tab(tab);
    }
    self.emit(CoreUpdate::TabsChanged);
}
```

**Soft-reference behavior:**
- Applied Tab whose `space_id` points to unknown Space → still stored. Renderer places it in "Unassigned" section.
- On applying Space that now includes this Tab, next sidebar rebuild naturally slots it in.

**Testing:**
- Unit: `favorite_tab` push generates entity with `is_favorite: true, favorite_order: Some(N)`.
- Integration: Device A favorites tab, Device B receives entity, Device B's tab list shows tab in favorites grid.
- Regression: tab with `space_id` pointing to nonexistent Space renders in "Unassigned" instead of crashing.

### 3.3 Task 2.2 — Space entity end-to-end

**Files:**
- Same pattern as Task 2.1 but for spaces.
- `maho/crates/maho-core/src/space_manager.rs` — no direct changes; still owns in-memory state
- `maho/crates/maho-core/src/maho_core.rs` — add `push_space_sync`, apply arm

**Structural fields (in Space entity):** `id, profile_id, name, color, theme, icon, tab_order, root_order, folders, atc_rules, is_active, last_active_tab_id`

**Push call sites** (Space mutations):
- `create_space` (3744), `delete_space` (3770), `update_space_config` (3777), `rename_space` (3782), `recolor_space` (3796), `reorder_space` (3810)
- Any event that mutates `tab_order` / `root_order` / `folders`: `MoveTab`, `MoveTabToSpace`, `ReopenLastClosed`, `RemoveTabFromFolder`, `CreateTab` (tab is added to space's `root_order`), `CreateFolder`, `DeleteFolder`, `MoveFolder`

**Critical rule for the favorite bug:**
Because Space entity carries `tab_order` and `root_order`, but Tab entity carries `is_favorite`, the invariant "favorited tabs must not appear in `root_order`" must be enforced **on apply**:
- After applying a Space entity, run `strip_favorited_tabs_from_root_orders` locally.
- After applying a Tab entity where `is_favorite=true`, remove tab_id from every locally-known Space's `root_order`.

This is a small local heuristic that prevents the current `synthesize_root_order()` legacy-restore bug from re-surfacing when a Space arrives from another device with a stale `root_order`.

**Testing:**
- Unit: applying Space with `root_order` containing a favorited tab_id → resulting local state has that tab_id NOT in `root_order`.
- Integration: Device A favorites tab X, Device B receives Tab entity, then receives (older) Space entity with X in `root_order` → X still shows only in favorites, not in sidebar tab list.

### 3.4 Tasks 2.3 – 2.8 — Bookmark, Note, Boost, ReadingListItem, SearchEngine, Memory

Each follows the same 4-step pattern:

1. **Add push function** in `maho_core.rs` mirroring `push_shortcut_sync` shape:
   ```
   fn push_bookmark_sync(&mut self, bm: &Bookmark) {
       let payload = serde_json::to_string(bm)?;
       let entity = SyncEntity {
           entity_type: SyncEntityType::Bookmark,
           entity_id: bm.id.to_string(),
           version: self.next_hlc_ts(),
           device_id: self.device_id(),
           modified_at: chrono::Utc::now().timestamp(),
           payload_json: payload,
           deleted: false,
       };
       self.sync_push_entity_persisted(entity);
   }
   ```
2. **Add tombstone helper** for deletes.
3. **Wire mutation sites** — each `*_persisted` method in `maho_core.rs`:
   - Bookmark: `add_bookmark` (1923), `remove_bookmark` (1931), `move_bookmark` (1937), `add_bookmark_entry` (5048), `create_bookmark_folder_persisted` (5093), `delete_bookmark_folder_persisted` (5110)
   - Note: `create_note_persisted` (2037), `update_note_persisted` (2043), `delete_note_persisted` (2050)
   - Boost: `create_boost_persisted` (2058), `update_boost_persisted` (2064), `delete_boost_persisted` (2071), `commit_boost_persisted` (2117), `shuffle_boost_persisted` (2133), `reset_boost_persisted` (2139), `import_boost_persisted` (2149)
   - ReadingListItem: `add_reading_list_item_persisted` (2273), `remove_reading_list_item_persisted` (2299), `mark_reading_list_read_persisted` (2310), `mark_reading_list_unread_persisted` (2321)
   - SearchEngine: `add_search_engine_persisted` (4959), `remove_search_engine_persisted` (4973), `set_default_search_engine_persisted` (4980)
   - Memory: `handle_memory_result` (6516) creates facts and blocks; also `delete_memory` (6622), `clear_all_memories` (6631)
4. **Add apply arm** in `apply_sync_remote_entities` — deserialize payload, call the appropriate `storage.save_*` and manager `upsert_*` methods.

**Bookmark note (Layered):** Bookmarks and BookmarkFolders are both entities. Bookmark is Content, BookmarkFolder is Structure. Same soft-reference rules: bookmark whose `folder_id` points to unknown folder → shown at root level.

**Memory note:** Memory has an existing opt-out toggle infrastructure (`set_entity_sync_enabled` at 6641). Task 2.8 must respect this — if disabled, do not push. This is the only entity type where sync is user-toggleable in current code.

**Testing per entity type:**
- Unit per push function.
- Unit per apply arm.
- Integration two-device convergence.
- Tombstone lifecycle (delete on one, delete propagates).

### 3.5 Phase 2 acceptance criteria

- All 10 `SyncEntityType` variants are wired end-to-end with tests.
- The favorite issue is fixed: favoriting a tab on Device A shows in Device B's favorites grid within one sync cycle; unfavoriting propagates likewise.
- No test in the tab-tree renderer references `is_favorite` on any node routed to `pinned_tree` or `normal_tree` (favorites remain in the dedicated grid).
- Sync-queue growth remains bounded under heavy tab-navigation workload (100 tabs, 1000 URL changes/hour, queue < 500 rows steady-state).

---

## 4. Phase 3 — Bootstrap Snapshot Protocol

**Goal:** Add periodic client-side encrypted snapshots to solve the 30-day relay GC state loss, without breaking E2EE.

**Estimated effort:** 10–15 engineer-days.

**Prerequisite:** Phase 2 complete (so entities have stable schemas to snapshot).

### 4.1 High-level model

- Each client peer periodically serializes its full local state as a single **Snapshot** blob.
- The snapshot is encrypted with the sync key (same key used for entity payloads).
- The client uploads the snapshot to the relay via a dedicated endpoint.
- Relay stores at most ONE snapshot per room, keyed by upload timestamp. Older snapshots are hard-deleted on new upload.
- When a new device joins, it first downloads the current snapshot (if any) + all delta messages since the snapshot's `hlc_ts_ceiling`. When no snapshot exists, it falls back to Phase 1 replay behavior.

### 4.2 Snapshot content

A `Snapshot` is a canonical JSON envelope:

```
{
  "schema_version": 1,
  "hlc_ts_ceiling": <highest HLC ts included>,
  "created_at": <ISO 8601>,
  "creator_device_id": <u32>,
  "entities": {
    "Tab":              [ <serialized entities> ],
    "Space":            [...],
    "Bookmark":         [...],
    "BookmarkFolder":   [...],
    "Note":             [...],
    "Boost":            [...],
    "ReadingListItem":  [...],
    "SearchEngine":     [...],
    "Shortcut":         [...],
    "Extension":        [...],
    "Memory":           [...]
  }
}
```

Each entity carries its full `(payload, version, device_id, modified_at)` so applying a snapshot is equivalent to applying every included entity via the normal apply path.

### 4.3 Task 3.1 — Snapshot serialization

**Files:**
- New: `maho/crates/maho-core/src/snapshot.rs` — build and consume `Snapshot` blobs
- `maho/crates/maho-core/src/maho_core.rs` — expose `build_snapshot()` and `apply_snapshot()` methods

**Design:**
- `build_snapshot()` iterates each per-type manager, extracts current state, wraps in a `SyncEntity` shape with the current HLC ts, and packs into the envelope.
- `apply_snapshot(snap)` iterates entities in the order Space → Tab → Folder → Bookmark → Note → Boost → ReadingListItem → SearchEngine → Shortcut → Extension → Memory. Each entity is applied via the existing `apply_remote_entity` path (which is already idempotent from Task 1.5).
- After successful application, update `last_pulled_versions` per type to `snap.hlc_ts_ceiling`.

**Encryption:** Snapshot body is encrypted just like `SyncEntity.payload_json` (per-room sync key, AES-GCM). Envelope metadata (`hlc_ts_ceiling, created_at, creator_device_id, schema_version`) is plaintext.

**Testing:**
- Unit: `build_snapshot` on a MahoCore with N entities produces envelope with N entries.
- Unit: `apply_snapshot` on empty MahoCore produces state equivalent to sequential entity application.
- Property test: `apply_snapshot(build_snapshot(state)) == state`.

### 4.4 Task 3.2 — Client-side snapshot scheduler

**Files:**
- `maho/crates/maho-core/src/sync_manager.rs` — add snapshot upload loop
- `maho/crates/maho-core/src/maho_core.rs` — snapshot upload triggering

**Design:**
- Every 6 hours, and after 500+ entities have been pushed since the last snapshot, and on graceful shutdown, produce a snapshot if:
  - This device is currently the "snapshot leader" for the room (see 4.5).
  - No other snapshot has been uploaded in the last 30 minutes (dedup window).
- Upload via `POST /snapshots/{room_id}` with body = encrypted snapshot + plaintext metadata.
- Relay accepts if uploader's `hlc_ts_ceiling` > current stored snapshot's `hlc_ts_ceiling`, else 409 Conflict.

**Testing:**
- Unit: scheduler timer fires at 6h intervals under mocked clock.
- Integration: client with 100 pushed entities uploads snapshot; second client with 500 entities uploads and replaces.

### 4.5 Task 3.3 — Snapshot leader election

**Design:**

To avoid all N devices uploading snapshots simultaneously (wasting bandwidth), use a lightweight leader election:

- Leader = the device with the lowest `device_id` (u32) among currently online devices, per relay `DeviceJoined` / `DeviceLeft` messages.
- If leader goes offline, next-lowest online device becomes leader after a 5-minute grace period.
- Only the leader schedules periodic uploads. Non-leaders may still upload on shutdown as a "last known state" mechanism (relay's version check will reject if stale).

**Failure modes:**
- Split brain (two devices both believe they're leader): both upload, relay's `hlc_ts_ceiling` check keeps the newer one.
- No online devices at scheduled time: no snapshot uploaded that cycle; next online cycle catches up.
- Only 1 device ever: it's always leader; snapshot on shutdown suffices.

**Testing:**
- Unit: leader election logic given a device set.
- Integration: 3 devices, one goes offline, verify leadership transfers.

### 4.6 Task 3.4 — Relay snapshot endpoints

**Files:**
- `maho/relay/server/src/sync/routes.rs` — new endpoints:
  - `POST /snapshots/{room_id}` — upload (multipart or JSON body with base64 blob)
  - `GET /snapshots/{room_id}` — download latest
  - `HEAD /snapshots/{room_id}` — metadata only (for clients checking "is there a snapshot newer than what I have?")
- `maho/relay/server/src/storage.rs` — new table `snapshots(room_id TEXT PK, hlc_ts_ceiling INTEGER, created_at INTEGER, creator_device_id INTEGER, blob BLOB)`

**Design:**
- Only one snapshot per room. Upload replaces existing if `new.hlc_ts_ceiling > existing.hlc_ts_ceiling`.
- Snapshot blob capped at 100 MB. Reject larger uploads with 413.
- Snapshot is NOT included in the normal message stream — it's a separate resource.
- Delta pruning: when a snapshot with `hlc_ts_ceiling = X` is uploaded, prune all `messages` rows with `hlc_ts <= X - 24h` (retain 24h of overlap for safety).

**Testing:**
- Integration: upload snapshot, GET returns same blob.
- Integration: upload with older `hlc_ts_ceiling` returns 409.

### 4.7 Task 3.5 — Client join flow using snapshot

**Files:**
- `maho/crates/maho-core/src/sync_manager.rs` — bootstrap sequence in `start_sync`

**Design:**

On `start_sync` when `last_pulled_versions` is empty (or all zero):
1. Call `HEAD /snapshots/{room_id}` to check for a snapshot.
2. If present, `GET /snapshots/{room_id}`, decrypt, call `MahoCore::apply_snapshot(snap)`.
3. Then continue with normal `EntityPull` using `since_version = snap.hlc_ts_ceiling` per type.
4. If no snapshot, fall back to `since_version = 0` (Phase 1 behavior).

On subsequent `start_sync` (device rejoin):
1. Skip snapshot download if local `last_pulled_versions[*] > 0`. Trust the local state.
2. Only re-download snapshot if the client explicitly requests full re-sync.

**Testing:**
- Integration: new device joins room with existing snapshot, ends up with correct state faster than replay.
- Integration: new device joins room without snapshot, replay path still works.
- Regression: existing device restarting does NOT re-download snapshot (would be wasteful).

### 4.8 Phase 3 acceptance criteria

- Room with 6+ months of history and 30-day GC is fully recoverable to a new device via snapshot.
- Snapshot upload/download uses ≤ 10x the storage of a single fresh state (verified via test with 500 tabs / 1000 bookmarks).
- Snapshot leader election converges deterministically under all tested failure modes.
- E2EE property preserved: relay integration tests fail (with logged assertion) if any snapshot body byte is human-readable JSON in server-side logs.

---

## 5. Phase 4 — Extended Coverage (Post-MVP)

**Goal:** Bring the remaining 13+ persisted state types into sync coverage, per-type policy.

**Estimated effort:** 20–30 engineer-days, spread over multiple releases.

Each type below needs (a) explicit product decision on sync intent, (b) if yes, extend `SyncEntityType` enum and follow Phase 2 pattern, (c) update Phase 3 snapshot to include.

### 5.1 Recommended to sync

| Type | Category | Notes |
|---|---|---|
| **CssMod** | Customization | Behaves like Boost; low risk; same pattern. |
| **Easel** | User content | High cross-device value; canvas boards are portable work artifacts. |
| **Conversation + ConversationTurn** | AI history | Two entity types; ConversationTurn references Conversation.id as soft ref. High storage cost; consider quota per room. |
| **AutofillAddress / AutofillPayment** | Autofill | Standard browser expectation. Payment PAN is not stored per current schema — only last4 + expiry — reduces sensitivity. |
| **Settings (`GeneralSettings`, `AppearanceSettings`, `PrivacySettings`, etc.)** | Preferences | Single blob entity `Settings` under key `browser_settings`. Merge policy: LWW on the whole struct is acceptable (settings mutations are user-triggered, infrequent). |
| **SharedCollection** | Sharing | Small dataset; simple entity. |

### 5.2 Requires explicit product decision

| Type | Category | Considerations |
|---|---|---|
| **Password / SavedPassword** | Credentials | Sensitive. Recommend: separate high-security sync channel with per-item E2EE + explicit user opt-in per device. Or defer to external providers (1Password, Bitwarden) which the code already models via `PasswordProviderKind`. |
| **Permission overrides** | Site settings | Some users want per-device (e.g., "camera only on desktop"). Consider per-device toggle like Memory. |
| **Profile / ProfileConfig** | Account | Profile identity is fundamental — syncing profile creation across devices needs conflict resolution UX. |
| **ATCRule / TrafficRule** | Tab management | Rules can conflict when target Space doesn't exist on other device. Space ordering must be Phase 2 first. |
| **Chromium ctrl-tab prefs** | UI prefs | Requires FFI bridge from Chromium prefs to Rust core, then normal sync. Extra layer of complexity. |

### 5.3 Recommended NOT to sync

| Type | Reason |
|---|---|
| **Download** | Files themselves aren't portable across devices. Download history is per-device. |
| **BrowseHistory** | High volume; privacy-sensitive; per-device is standard (Chrome/Safari also don't sync full history by default). |
| **Session restore state (CrashRecoveryManager)** | Currently in-memory only. If persisted, still should be per-device (crash recovery is local concept). |
| **ZoomLevel (per-site)** | Small user preference; low value across devices; Chrome doesn't sync this. |

---

## 6. Cross-cutting concerns

### 6.1 Migration & backward compatibility

Phase 1 introduces schema changes to `SyncEntity` (adds `device_id`). Strategy:

- Add `schema_version: u8` field to `SyncEntity` (default 1 pre-migration, bumped to 2 post-migration).
- Handshake on WebSocket connect includes each peer's supported schema versions.
- Version-2 peers send v2 entities. Version-1 peers ignore the extra field (JSON is forward-tolerant).
- Deprecation window: 90 days. After that, v1-only peers stop syncing until updated.

### 6.2 Testing infrastructure

Phase 1 requires new test harnesses:

- **Two-device integration harness**: In-process spawn of 2 `MahoCore` instances + 1 relay. Deterministic clock injection.
- **Chaos test**: Randomly disconnect, reconnect, restart devices; assert convergence within N cycles.
- **HLC skew test**: Devices with mocked wall clocks ±60s off; verify LWW correctness.
- **Snapshot round-trip property test**: `apply_snapshot(build_snapshot(state)) == state` for arbitrary states.

Add these under `maho/crates/maho-core/tests/sync/`.

### 6.3 Observability

Add structured logging tags for every sync operation:

- `sync.push` — entity_type, entity_id, hlc_ts, device_id, deleted
- `sync.apply.accepted` — same + previous version
- `sync.apply.rejected` — same + reason (older, unknown_type, decrypt_fail, schema_mismatch)
- `sync.queue.enqueue`, `sync.queue.sent`, `sync.queue.ack`
- `sync.snapshot.upload`, `sync.snapshot.download`, `sync.snapshot.applied`

Emit as JSON to `maho-core` telemetry channel. Sample rate 100% during Phase 1, throttle to 1% after stability.

### 6.4 E2EE audit

Before Phase 3 ships, security review:

- Verify `SyncEntity.payload_json` is only ever serialized in encrypted form to the wire.
- Verify snapshot body encryption matches (same AEAD scheme).
- Verify relay-side logs never emit decrypted content.
- Verify metadata leaked to relay (`entity_type`, `entity_id`, HLC ts, device_id) is acceptable per threat model. Document explicitly what a compromised relay can learn (metadata graph: which entities exist, when they change, who changes them — but not the content).

### 6.5 Rollout plan

- **Phase 1** ships as a feature flag `sync.protocol_v2` off-by-default. Test internally for 2 weeks with 2+ devices before flipping on for GA.
- **Phase 2** ships one entity type per release. Each behind its own flag `sync.entity.{tab,space,bookmark,...}`. Enable per-type after 1 week of internal use.
- **Phase 3** ships behind `sync.snapshot`. Requires Phase 2 complete for all entity types to be recoverable.
- **Phase 4** rolls out per-type as product decisions land.

Rollback plan: any phase can be disabled via feature flag. Data written under a phase remains locally persisted; only sync propagation is disabled.

---

## 7. Dependency graph

```
Phase 0 (audit — complete)
    │
    ▼
Phase 1 — Protocol Foundation
    ├─ 1.1 device_id persistence
    │       └─→ 1.3 HLC
    │              └─→ 1.5 idempotent apply
    │                     └─→ 1.7 collapse dual channel
    ├─ 1.2 per-type last_pulled_version
    │       └─→ 1.5 (uses persistent state)
    ├─ 1.4 queue lifecycle
    │       └─→ 1.6 init order fix
    │
    ▼
Phase 2 — Entity Coverage
    ├─ 2.1 Tab entity ──┐
    │                   ├─→ 2.3 Bookmark ─→ 2.4 Note ─→ 2.5 Boost ─→ 2.6 ReadingList ─→ 2.7 SearchEngine ─→ 2.8 Memory
    ├─ 2.2 Space entity ─┘
    │
    ▼
Phase 3 — Bootstrap Snapshot
    ├─ 3.1 Snapshot serialization
    ├─ 3.2 Snapshot scheduler
    ├─ 3.3 Leader election
    ├─ 3.4 Relay endpoints
    └─ 3.5 Client join flow
    │
    ▼
Phase 4 — Extended coverage (product-driven, non-blocking)
```

---

## 8. Open questions

1. **HLC ceiling for snapshot**: When building a snapshot at time T, what if some queue rows have `sent_at IS NULL` (not yet delivered)? Include them in snapshot? Or wait for their ack first? Recommend: include, because the snapshot represents this device's authoritative view, and other devices' apply is idempotent anyway.

2. **Cross-device folder move**: A `Folder` is nested inside `Space.folders`. When user drags folder from Space A to Space B, the mutation is a Space-A update AND a Space-B update. With Soft references + Best-effort, if only Space-A update arrives, the folder appears twice (still in B's `folders` locally, plus A no longer has it). Renderer needs de-dup by `folder.id`. Alternative: promote Folder to a first-class Content entity. Recommend: keep folder nested in Space until a real user pain point emerges.

3. **Memory embeddings**: `memory_facts.embedding BLOB` (HNSW vector) is up to 3 KB per fact. If a user has 10k memory facts, snapshot Memory section is 30 MB. Should embeddings be re-computable from `fact` text (skip in snapshot) or included (bigger snapshot)? Recommend: skip embeddings in sync; each device recomputes on apply. This makes Memory entity payload smaller but requires embedding pipeline available on every device.

4. **Snapshot forced refresh**: Should there be a user-triggered "re-download snapshot from cloud" UX for recovery scenarios? Recommend: yes, in Settings → Sync → "Restore state from cloud", which clears local `last_pulled_versions` and forces snapshot download.

5. **Extension state deeper than enable/disable**: Chromium extensions have their own `chrome.storage.sync` and `chrome.storage.local`. Currently only extension enabled/disabled bit syncs. Do we want to sync extension internal state? Recommend: out of scope for this roadmap; punt to a future extension-sync design.

---

## 9. Appendix A — Files touched by phase

### Phase 1
- `maho/crates/maho-core/src/sync_models.rs` — SyncEntity schema
- `maho/crates/maho-core/src/sync_manager.rs` — LWW logic, queue integration, ack correlation
- `maho/crates/maho-core/src/maho_core.rs` — HLC generator, device_id accessor, sync_push_entity_persisted, load_sync_queue, apply_sync_remote_entities, start_sync ordering
- `maho/crates/maho-core/src/account_manager.rs` — persistent device_id
- `maho/crates/maho-storage/src/sqlite.rs` — new tables (`entity_versions`, `hlc_state`, `sync_queue.acked_at` column)
- `maho/crates/maho-storage/src/lmdb.rs` — account key schema doc
- `maho/relay/server/src/shortcuts.rs` — delete REST endpoints
- `maho/relay/server/src/main.rs` — remove shortcut route

### Phase 2
- `maho/crates/maho-core/src/maho_core.rs` — push functions and apply arms for 8 entity types
- `maho/crates/maho-core/src/tab_lifecycle.rs` — wire tab mutation sites
- `maho/crates/maho-core/src/event_dispatcher.rs` — wire event-driven mutations
- `maho/crates/maho-core/src/space_manager.rs` — no direct changes, invoked from maho_core
- Per-manager files (bookmark_manager.rs, note_manager.rs, boost_manager.rs, reading_list_manager.rs, memory_manager.rs) — no direct sync calls (managers stay pure), but may need helper accessors for maho_core to build push payloads
- Chromium-side renderers must handle soft-reference edge cases (unassigned tabs, missing folder refs)

### Phase 3
- `maho/crates/maho-core/src/snapshot.rs` — NEW
- `maho/crates/maho-core/src/sync_manager.rs` — snapshot scheduler, leader election, join-flow bootstrap
- `maho/crates/maho-core/src/maho_core.rs` — build_snapshot, apply_snapshot
- `maho/relay/server/src/sync/routes.rs` — new snapshot endpoints
- `maho/relay/server/src/storage.rs` — snapshots table
- `maho/crates/maho-ffi/src/lib.rs` — FFI surface for "force snapshot refresh" from UI

---

## 10. Appendix B — Bug closure map

Each defect from the 2026-07-07 audit maps to a phase task that closes it.

| Defect ID | Description | Closed by |
|---|---|---|
| CRITICAL-1 | Queue silently dropped on startup | Task 1.6 |
| CRITICAL-2 | SQLite queue never dequeued | Task 1.4 |
| CRITICAL-3 | Wall-clock ms version, no tiebreak | Task 1.1 + Task 1.3 |
| CRITICAL-4 | Shortcut dual sync channel with mismatched clocks | Task 1.7 |
| HIGH-4 | last_pulled_version only tracks Tab, in-memory | Task 1.2 |
| HIGH-6 | 30-day GC prunes tombstones | Phase 3 (snapshot) |
| HIGH-7 | No apply-side idempotency across restart | Task 1.5 |
| HIGH-8 | Shortcut updated_at (sec) vs entity version (ms) | Task 1.7 (dual channel collapse) |
| HIGH-9 | No multi-entity transactions | Deferred; soft-refs + snapshot mitigate in practice |
| HIGH-10 | New device bootstrap = full replay, >30d lost | Phase 3 |
| MEDIUM-11 | `sync_trigger_suppressed` leak on early exit | Task 1.7 (removes the code) |
| MEDIUM-12 | Non-persistent device_id | Task 1.1 |
| MEDIUM-13 | EntityPull is P2P-only | Deferred; snapshot supersedes for offline peer case |
| LOW-14 | `has_more` in pull response not consumed | Deferred; add client-side pagination in Phase 2 as maintenance |
| Coverage gap: Tab (incl. favorite) | Task 2.1 |
| Coverage gap: Space | Task 2.2 |
| Coverage gap: Bookmark | Task 2.3 |
| Coverage gap: Note | Task 2.4 |
| Coverage gap: Boost | Task 2.5 |
| Coverage gap: ReadingListItem | Task 2.6 |
| Coverage gap: SearchEngine | Task 2.7 |
| Coverage gap: Memory | Task 2.8 |
| Extended coverage: 13+ types | Phase 4 |

---

**End of roadmap.**
