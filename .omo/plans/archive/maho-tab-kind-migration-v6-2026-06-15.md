# Maho Tab Kind Migration — Plan v6

**Date:** 2026-06-15
**Status:** DRAFT — needs user/momus review
**Branch:** (new) `feature/tab-kind-enum`
**Scope:** Replace `is_favorite: bool` + `is_pinned: bool` with `enum TabKind { Normal, Pinned, Favorite }`

---

## 1. Goal

Introduce a single `TabKind` enum that makes favorite/pinned/normal status **mutually exclusive** at the type level. A tab exists in exactly one bucket — it cannot simultaneously be pinned AND favorited. This eliminates the class of bugs where a tab appears in multiple sidebar sections.

---

## 2. User Intent (Mental Model)

Users think of their tabs as belonging to one of three buckets:
- **Favorites** — persistent shortcuts in the top grid; survive close (suspend instead)
- **Pinned** — always-visible tabs locked at the top of the tab list; URL-reset on close
- **Normal** — regular ephemeral tabs in the scrollable list

A tab **cannot be both** a favorite and pinned. Dragging from one section to another is a **kind transition**, not an additive toggle.

---

## 3. Current Architecture

### 3.1 Type Definition

```
maho/crates/maho-types/src/tab.rs:38-41
```
```rust
pub is_pinned: bool,
pub is_favorite: bool,
pub favorite_order: Option<u32>,
```

Two independent booleans allow 4 combinations: `(false,false)`, `(true,false)`, `(false,true)`, **(true,true)** — the last being semantically invalid but structurally possible.

### 3.2 TabViewModel (FFI Contract)

```
maho/crates/maho-types/src/traits/shell_renderer.rs:22-25
```
```rust
pub is_pinned: bool,
pub is_favorite: bool,
pub favorite_order: Option<u32>,
```

Serialized as JSON `{"isPinned": bool, "isFavorite": bool, "favoriteOrder": u32|null}`.

### 3.3 TabStateUpdate (Incremental Updates)

```
maho/crates/maho-types/src/traits/shell_renderer.rs:219-227
```
```rust
pub is_pinned: Option<bool>,
pub is_favorite: Option<bool>,
pub favorite_order: Option<Option<u32>>,
```

### 3.4 Query Functions

| Function | File:Line | Filter Logic |
|----------|-----------|--------------|
| `get_favorite_tabs` | `maho_core.rs:4074-4100` | `tab.is_favorite && !Archived` (profile-scoped) |
| `get_pinned_tabs` | `maho_core.rs:4011-4019` | `tab.is_pinned && !Archived` (space-scoped) |
| `get_space_tabs` | `maho_core.rs:4021-4029` | `!tab.is_pinned && !Archived` |
| `get_space_tabs_for_window` | `maho_core.rs:4034-4050` | `!tab.is_pinned && window_id match` |
| `get_today_tabs` | `tab_lifecycle.rs:832-842` | `!is_pinned && !is_favorite && Active` |
| `is_auto_lifecycle_exempt` | `tab_lifecycle.rs:49-51` | `is_pinned || is_favorite` |

### 3.5 Mutation Functions

| Function | File:Line | Mutation |
|----------|-----------|----------|
| `pin_tab` | `tab_lifecycle.rs:397-418` | Sets `is_pinned=true`, `pinned_url=Some(url)` |
| `unpin_tab` | `tab_lifecycle.rs:420-441` | Sets `is_pinned=false`, `pinned_url=None` |
| `favorite_tab` | `tab_lifecycle.rs:443-479` | Sets `is_favorite=true`, assigns `favorite_order` |
| `unfavorite_tab` | `tab_lifecycle.rs:481-502` | Sets `is_favorite=false`, clears `favorite_order` |
| `MahoCore::favorite_tab` | `maho_core.rs:4206-4245` | Calls above + strips from root_orders |
| `MahoCore::unfavorite_tab` | `maho_core.rs:4247-4257` | Calls above + restores to root_order |

### 3.6 LMDB Persistence

```
maho/crates/maho-core/src/maho_core.rs:1218-1221
```
Tabs are stored as a **single JSON blob** at key `"tabs"`. There is no columnar schema — the entire `Tab` struct (including booleans) is serialized as JSON.

### 3.7 Chromium C++ Parsing

| File:Line | What |
|-----------|------|
| `maho_sidebar_state_adapter.cc:332-336` | Parses `"isPinned"` and `"isFavorite"` from JSON into `SidebarTreeNode` |
| `maho_sidebar_tab_core_query.cc:51-55` | Parses `"isPinned"` into `PersistedTabInfo` |
| `maho_spaces_board_data.cc:61-63` | Parses both booleans for spaces overlay |
| `maho_sidebar_state_models.h:219-220` | `SidebarTreeNode::is_pinned`, `::is_favorite` fields |
| `maho_sidebar_tab_core_query.h:14` | `PersistedTabInfo::is_pinned` |
| `maho_spaces_board_data.h:28-30` | `bool is_pinned`, `bool is_favorite` |

### 3.8 Drop Planner

```
maho-chromium/browser/ui/views/sidebar/maho_sidebar_drop_planner.cc:48-140
```
- `source_is_pinned = (payload.origin == kPinnedSection)` — derived from drag origin, NOT from `node.is_pinned`
- `from_favorites = (payload.origin == kFavorites)` — provenance signal
- Emits `pin_tab`/`unpin_tab`/`favorite_tab`/`unfavorite_tab` step sequences

### 3.9 Crash Recovery

```
maho/crates/maho-core/src/crash_recovery.rs:26-27
```
```rust
pub is_pinned: bool,
pub is_favorite: bool,
```

### 3.10 Extension Bridge

```
maho/crates/maho-core/src/extension_bridge.rs:45, 151
```
- `pinned: vm.is_pinned` — maps to extension API field
- `if tab.is_pinned != pinned { ... }` — extension-driven pin toggle

---

## 4. Gap Analysis

| Problem | Root Cause | Impact |
|---------|-----------|--------|
| Tab can be `is_pinned=true` AND `is_favorite=true` | Two independent booleans | Tab renders in favorites grid AND pinned section simultaneously |
| `get_space_tabs` only filters `!is_pinned`, not `!is_favorite` | Incomplete filter logic | Favorited tabs can ghost-appear in normal list |
| No compile-time exhaustiveness | Boolean doesn't force exhaustive match | New features must remember to check both flags |
| DnD transitions require paired mutations | `unpin → favorite` needs two events | Race window between events |
| `apply_tab_residency` takes `is_favorite: bool` parameter | Boolean threading | Easy to pass stale value |

---

## 5. Architecture Change

### 5.1 New Type: `TabKind`

```rust
// maho-types/src/tab.rs
#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum TabKind {
    Normal,
    Pinned,
    Favorite,
}

impl Default for TabKind {
    fn default() -> Self { Self::Normal }
}
```

### 5.2 Tab Struct Migration

```rust
pub struct Tab {
    pub kind: TabKind,
    pub favorite_order: Option<u32>,
    pub pinned_url: Option<Url>,
    // REMOVED: is_pinned, is_favorite
}
```

### 5.3 Backward-Compatible Deserialization

To handle old LMDB data:

```rust
impl Tab {
    pub fn migrate_kind(is_favorite: bool, is_pinned: bool) -> TabKind {
        if is_favorite { TabKind::Favorite }
        else if is_pinned { TabKind::Pinned }
        else { TabKind::Normal }
    }
}
```

Custom `Deserialize` impl:
1. Tries to read `"kind"` field → if present, use directly
2. If absent, reads legacy `"isFavorite"` / `"isPinned"` → derives via priority rule
3. Always serializes as `"kind"` (drops legacy fields)

### 5.4 TabViewModel Update

```rust
pub struct TabViewModel {
    pub kind: String,  // "normal" | "pinned" | "favorite"
    pub favorite_order: Option<u32>,
}
```

### 5.5 TabStateUpdate

```rust
pub struct TabStateUpdate {
    pub kind: Option<String>,
    pub favorite_order: Option<Option<u32>>,
}
```

### 5.6 Query Function Refactoring

| Function | Old Filter | New Filter |
|----------|-----------|-----------|
| `get_favorite_tabs` | `tab.is_favorite` | `tab.kind == TabKind::Favorite` |
| `get_pinned_tabs` | `tab.is_pinned` | `tab.kind == TabKind::Pinned` |
| `get_space_tabs` | `!tab.is_pinned` | `tab.kind == TabKind::Normal` |
| `get_today_tabs` | `!is_pinned && !is_favorite` | `tab.kind == TabKind::Normal` |
| `is_auto_lifecycle_exempt` | `is_pinned || is_favorite` | `tab.kind != TabKind::Normal` |

### 5.7 Mutation Refactoring

```rust
pub fn set_tab_kind(&mut self, tab_id: &TabId, new_kind: TabKind) {
    let tab = self.tabs.get_mut(tab_id)?;
    let old_kind = tab.kind;
    if old_kind == new_kind { return; }

    match old_kind {
        TabKind::Favorite => { tab.favorite_order = None; }
        TabKind::Pinned => { tab.pinned_url = None; }
        TabKind::Normal => {}
    }

    match new_kind {
        TabKind::Favorite => {
            tab.favorite_order = Some(self.next_favorite_order());
        }
        TabKind::Pinned => {
            tab.pinned_url = Some(tab.url.clone());
        }
        TabKind::Normal => {}
    }

    tab.kind = new_kind;
}
```

Keep thin wrappers `favorite_tab`, `unfavorite_tab`, `pin_tab`, `unpin_tab` that delegate to `set_tab_kind` for backward compatibility with existing `ShellEvent` variants.

### 5.8 ShellEvent — No Breaking Changes

Keep existing event variants:
- `ShellEvent::PinTab { tab_id }` → calls `set_tab_kind(tab_id, Pinned)`
- `ShellEvent::UnpinTab { tab_id }` → calls `set_tab_kind(tab_id, Normal)`
- `ShellEvent::FavoriteTab { tab_id }` → calls `set_tab_kind(tab_id, Favorite)`
- `ShellEvent::UnfavoriteTab { tab_id }` → calls `set_tab_kind(tab_id, Normal)`

### 5.9 C++ Side Changes

Replace boolean field parsing with kind-string parsing:

```cpp
const std::string* kind_str = dict->FindString("kind");
if (kind_str) {
    if (*kind_str == "pinned") {
        node.is_pinned = true;
    } else if (*kind_str == "favorite") {
        node.is_favorite = true;
    }
}
```

Minimize C++ structural changes by keeping `SidebarTreeNode::is_pinned` and `::is_favorite` as **derived booleans** parsed from the single `kind` field.

### 5.10 apply_tab_residency Update

```rust
pub fn apply_tab_residency(
    &mut self,
    space_id: &SpaceId,
    tab_id: &TabId,
    kind: TabKind,
    is_in_folder: bool,
) {
    if kind == TabKind::Favorite || is_in_folder {
        self.remove_tab_from_root_order(space_id, tab_id);
    } else {
        self.add_tab_to_root_order_if_absent(space_id, tab_id);
    }
}
```

---

## 6. File Diff Plan

### 6.1 Rust Changes

| File | Change |
|------|--------|
| `maho-types/src/tab.rs` | Remove `is_pinned`, `is_favorite`; add `kind: TabKind` + TabKind enum |
| `maho-types/src/traits/shell_renderer.rs` | TabViewModel + TabStateUpdate field rename |
| `maho-core/src/tab_lifecycle.rs` | All filters + mutations refactored to `kind` |
| `maho-core/src/maho_core.rs` | All is_pinned/is_favorite refs → kind |
| `maho-core/src/event_dispatcher.rs` | `apply_tab_residency` sig update |
| `maho-core/src/space_manager.rs` | sig + folder-create helpers |
| `maho-core/src/crash_recovery.rs` | Replace 2 booleans → `kind: String` |
| `maho-core/src/extension_bridge.rs` | `vm.is_pinned` → `vm.kind == "pinned"` |
| `maho-ffi/src/lib.rs` | Inline TabViewModel construction |
| `maho-core/tests/test_batch_1.rs` | ~30 assertions |
| `maho-types/tests/shared_contract_test.rs` | Field renames |

### 6.2 C++ Changes

| File | Change |
|------|--------|
| `maho_sidebar_state_adapter.cc` | Parse `"kind"` instead of `"isPinned"`+`"isFavorite"` |
| `maho_sidebar_tab_core_query.cc` | Parse `"kind"` string |
| `maho_sidebar_tab_core_query.h` | Field 유지 (derived) |
| `maho_spaces_board_data.cc` | Parse `"kind"` |
| `maho_spaces_board_data.h` | Parse from `kind` |
| `maho_sidebar_state_models.h` | Keep derived booleans from `kind` |
| `maho_sidebar_drop_planner.cc` | No change (uses `payload.origin`) |
| `maho_sidebar_*_unittest.cc` (5+) | Test setup updates |
| Importer parsers (arc_sidebar, zen_workspaces) | Output `"kind"` |
| `maho_test_ui.cc`, etc. | Misc consumers |

### 6.3 Platform Impact

| Platform | Impact | Reason |
|----------|--------|--------|
| macOS | Full — FFI JSON contract change | Primary dev platform |
| Windows | Full — same binary | Same Chromium overlay |
| Linux | Full — same binary | Same Chromium overlay |
| iOS | Minimal — uses shell not Chromium | `maho-types` change only |
| Android | Minimal — uses JNI bridge | `maho-types` change |

---

## 7. Phasing (TDD-Ordered Atomic Commits)

### Phase 1: Foundation — TabKind Enum (Day 1, ~2h)
**Commit 1.1**: Add `TabKind` enum + migration helper
**Commit 1.2**: Add `kind: TabKind` field to `Tab` struct (dual-mode with serde)

### Phase 2: Core Internals — TabLifecycleManager (Day 1-2, ~4h)
**Commit 2.1**: Add `set_tab_kind` to `TabLifecycleManager`
**Commit 2.2**: Migrate query functions to use `tab.kind`
**Commit 2.3**: Migrate mutation callers

### Phase 3: TabViewModel + TabStateUpdate (Day 2, ~3h)
**Commit 3.1**: Migrate `TabViewModel` to `kind: String`
**Commit 3.2**: Migrate `TabStateUpdate` to `kind: Option<String>`

### Phase 4: Supporting Modules (Day 2-3, ~2h)
**Commit 4.1**: Update `event_dispatcher.rs`
**Commit 4.2**: Update `space_manager.rs`
**Commit 4.3**: Update auxiliary modules (crash_recovery, extension_bridge, FFI inline)

### Phase 5: Remove Legacy Fields (Day 3, ~2h)
**Commit 5.1**: Remove `is_pinned` and `is_favorite` from `Tab` struct
**Commit 5.2**: Final test sweep + LMDB migration verification

### Phase 6: Chromium C++ Migration (Day 3-4, ~4h)
**Commit 6.1**: Update JSON parsing in state adapter (3 files)
**Commit 6.2**: Update importer outputs (arc_sidebar, zen_workspaces)
**Commit 6.3**: Update test files
**Commit 6.4**: Update remaining C++ consumers

### Phase 7: Integration Verification (Day 4-5, ~3h)
**Commit 7.1**: End-to-end migration test (Rust integration test)
**Commit 7.2**: Build + manual smoke verification

**Total: 4-5 days**

---

## 8. Tests

### 8.1 New Unit Tests (Rust, ~10)

| Test | Location | Validates |
|------|----------|-----------|
| `tab_kind_serde_roundtrip` | `maho-types/src/tab.rs` | Enum serializes as `"normal"/"pinned"/"favorite"` |
| `tab_kind_from_legacy_both_true` | `maho-types/src/tab.rs` | `(true, true)` → `Favorite` |
| `tab_kind_from_legacy_neither` | `maho-types/src/tab.rs` | `(false, false)` → `Normal` |
| `set_tab_kind_normal_to_favorite` | `tab_lifecycle.rs` | Transition correctness |
| `set_tab_kind_favorite_to_pinned` | `tab_lifecycle.rs` | favorite_order cleared, pinned_url set |
| `set_tab_kind_pinned_to_normal` | `tab_lifecycle.rs` | pinned_url cleared |
| `set_tab_kind_idempotent` | `tab_lifecycle.rs` | Same kind → no update |
| `load_state_legacy_migration` | `maho_core.rs` | Old JSON → correct TabKind |
| `get_space_tabs_excludes_favorites` | `maho_core.rs` | Favorite never in normal list |
| `kind_transition_dnd_atomic` | `maho_core.rs` | Single event, no intermediate state |

### 8.2 Migrated Existing Tests (~35)

| File | Count | Migration |
|------|-------|-----------|
| `maho-core/src/maho_core.rs` (mod tests) | ~12 | `.is_favorite` → `.kind == "favorite"` |
| `maho-core/tests/test_batch_1.rs` | ~15 | Same pattern |
| `maho-core/src/tab_lifecycle.rs` (mod tests) | ~5 | Direct field access change |
| `maho-types/tests/shared_contract_test.rs` | ~3 | Field construction |

### 8.3 C++ Tests

| File | Count | Migration |
|------|-------|-----------|
| `maho_sidebar_dnd_dispatch_unittest.cc` | ~33 lines | Node construction with derived booleans from `kind` |
| `maho_sidebar_planner_unittest.cc` | ~7 lines | No change (uses origin) |
| `maho_sidebar_contract_test.cc` | ~3 assertions | Keep checking derived `is_pinned`/`is_favorite` |

---

## 9. Risks

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| **LMDB data loss on schema mismatch** | Low | Critical | Custom deserializer; never deletes old keys; round-trip test |
| **Both booleans true in old data** | Medium | Medium | Priority `Favorite > Pinned > Normal` documented |
| **C++ crash on missing "kind" field** | Low | High | C++ parser fallback to legacy boolean parsing |
| **Extension API breakage** | Low | Medium | Keep `pinned` field in extension API (derived) |
| **DnD race between unpin+favorite** | Eliminated | — | Atomic `set_tab_kind` |
| **Performance regression** | Very Low | Low | JSON parse is bottleneck, not field count |
| **Merge conflicts with in-flight PRs** | Medium | Medium | Announce migration; complete in <5 days |
| **Android/iOS shell breaks** | Low | Low | `kind` field additive; legacy still parsed |

---

## 10. Acceptance Criteria

- [ ] `cargo test --workspace` passes with zero `is_pinned`/`is_favorite` usage in non-test code
- [ ] `cargo clippy --workspace -- -D warnings` clean
- [ ] LMDB load test: `{"isPinned":true,"isFavorite":false}` → loads as `kind: Pinned`
- [ ] LMDB load test: `{"isPinned":true,"isFavorite":true}` → loads as `kind: Favorite`
- [ ] `python3 maho-chromium/build/scripts/build_maho.py --skip-rust --ninja-target unit_tests` passes
- [ ] Sidebar contract test: favorited tab appears ONLY in favorites grid
- [ ] Sidebar contract test: pinned tab appears ONLY in pinned section
- [ ] DnD test: drag favorite → normal section → becomes Normal
- [ ] DnD test: drag normal → favorites grid → becomes Favorite only
- [ ] No regression in `maho_sidebar_dnd_dispatch_unittest`
- [ ] Extension API still reports `pinned: true/false` (derived)
- [ ] Crash recovery snapshot uses `kind` field

---

## 11. Open Questions

| # | Question | Proposed Answer |
|---|----------|----------------|
| 1 | Priority when `is_favorite=true AND is_pinned=true` in old data? | Favorite wins (more intentional) |
| 2 | Add `ShellEvent::SetTabKind { tab_id, kind }` for atomic DnD? | Yes, follow-up |
| 3 | Extension API: expose `kind` or keep `pinned` boolean? | Keep `pinned`; add `kind` |
| 4 | `favorite_order` persist after Favorite→Normal→Favorite? | No — fresh order on re-favorite |
| 5 | Window-scoped tabs: pinned tab can be window-scoped? | Keep current `window_id: None` semantic |
| 6 | Feature flag for staged rollout? | No — atomic ship |

---

## Wave Execution Plan

```
Wave 1 (Day 1):
└── Phase 1: TabKind enum + migration helper

Wave 2 (Day 1-2, depends Phase 1):
└── Phase 2: Core internals (lifecycle + maho_core)

Wave 3 (Day 2-3, parallel after Phase 2):
├── Phase 3: TabViewModel + TabStateUpdate
└── Phase 4: Supporting modules (event_dispatcher, space_manager, crash_recovery)

Wave 4 (Day 3-4, parallel after Phase 3+4):
├── Phase 5: Remove legacy fields (Rust)
└── Phase 6: C++ migration

Wave 5 (Day 4-5):
└── Phase 7: Integration verification

Critical Path: Phase 1 → 2 → 3 → 5 → 7
Estimated Duration: 4-5 days
Parallel Speedup: ~30%
```
