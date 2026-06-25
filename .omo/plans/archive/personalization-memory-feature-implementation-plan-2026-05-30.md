# Personalization & Memory Feature — Implementation Plan

**Created:** 2026-05-30
**Status:** Plan only — implementation deferred until working tree is clean
**Last verified commit:** `9a4921c feat(location-bar): render extension icon grid with action runner, context menu, and live refresh`
**Verification depth:** Full code-level audit across Rust + C++ + TS + macOS Swift (9 parallel exploration agents over 2 verification rounds)
**Document size:** ~3500 lines, ~150 KB. Read in sequence; §11 is the authoritative file-level reference.

## Document Map

| § | Section | Purpose |
|---|---|---|
| §0 | Why deferred | Context for why implementation didn't proceed |
| §1 | Executive summary | What we're building + 3 critical revisions |
| §2 | Code-level verified facts | All file:line citations from agent audits |
| §3 | Final architecture | Three-tier memory, embedding strategy, sync model |
| §4 | Data model | SQL schema (5 tables) |
| §5 | File-by-file inventory | High-level change table (refer to §11 for detail) |
| §6 | Code skeletons | Reference Rust/TSX skeletons |
| §7 | Phased roadmap | W0–W6 with acceptance tests |
| §8 | Risk register | R1–R10 with mitigations |
| §9 | How to resume | Step-by-step restart instructions |
| §10 | References | All source files audited |
| **§11** | **File-by-file change manifest** | **Verbatim before/after diffs for all 32 files** |
| §12 | Summary counts | Total files / new vs modified |
| §13 | Per-phase file touch map | Which files each W-phase touches |
| §14 | Validation checklist | Per-phase acceptance criteria |

---

## 0. Why This Plan Was Deferred

When implementation was attempted on 2026-05-30, the working tree had **255 uncommitted files** from another session (Boost zen-port + AI test infra + mail Tauri app + space_config WebUI). Many of those dirty files **directly overlap** with the files this plan would modify (e.g., `maho_chat_orchestration_service.cc`, `maho_settings_page_handler.cc`, `domain_panes.tsx`, `maho_ai/store.ts`, `sqlite.rs`, `maho_core.rs`, `maho-ffi/src/lib.rs`).

**Resume condition**: Working tree is clean (or in-flight work is on a separate branch), and a fresh `feat/memory` branch is created from main.

---

## 1. Executive Summary

### What we're building
A "Personalization" feature in the Maho AI panel — analogous to Dia browser's Personalization screen — composed of:

1. **Personalization settings pane** (`chrome://maho-settings/?pane=personalization`)
   - 3 toggles: Personalize new chats, Show suggestions on new tab, Memory enabled
   - 4 free-text textareas: taste / about-you / writing-style / coding-prefs
   - Memory enable button + management
2. **Memory backend** — extracts facts from chat sessions and browse activity, stores them locally, retrieves them as AI context
3. **Memory sync** — opt-in/opt-out per-entity-type sync via existing relay
4. **AI panel topbar icon #2** (`settingsGear`) — currently navigates to generic settings; should land on Personalization pane

### Critical architecture revisions (after code-level verification)
| Earlier assumption | Verified reality | Final decision |
|---|---|---|
| Use `sqlite-vec` extension for ANN | rusqlite `["bundled"]` only — no `load_extension`. iOS App Store sandbox blocks `dlopen()`. Android needs `.so` per ABI. | **Pure-Rust HNSW (`instant-distance` 0.6)** with embedding stored as BLOB |
| Use opencode runtime for fact extraction | opencode-go has hardcoded ~500-line coder system prompt that can't be overridden. Even `task` agent has GlobTool/ViewTool. JSON output unreliable | **Direct LLM API call from Rust** with `response_format: json_object` |
| Single embedding strategy | fastembed's `ort` crate auto-downloads ORT binaries only for desktop targets | **Per-target dispatch**: `fastembed` on macOS/Linux/Windows, `tract-onnx` (pure Rust) on iOS/Android |

---

## 2. Code-Level Verified Facts

All facts below are confirmed by reading the actual files. File:line citations are accurate as of commit `9a4921c`.

### 2.1 AI panel topbar (current state)
- File: `maho-chromium/browser/resources/maho_ai/react/features/compact/compact-topbar.tsx`
- 4 icons: edit (line 43), settingsGear (line 51), viewModeToggle (line 60), xmark (line 80)
- `settingsGear` onClick → `onOpenSettings` → `store.openSettings()` (`store.ts:381`) → Mojo `OpenSettings` → C++ `MahoAIPageHandler::OpenSettings()` (`maho_ai_page_handler.cc:1181-1193`)
- C++ does: `browser_->OpenURL(GURL("chrome://maho-settings/"), ..., SINGLETON_TAB)`
- **Lands on first pane (General)** — no `?pane=` query parameter passed today

### 2.2 Existing settings UI (does NOT have Personalization)
- `setting_schema.ts` (249 lines) — full enumeration: zero Personalization keys
- `panes.ts` — 16 panes total, none for Personalization. Domains: `browsing`, `look-and-feel`, `identity`, `productivity`, `protection`, `platform` (reserved, no panes)
- `AISettings` mojom struct (`maho_settings.mojom:25-33`) — 7 fields: `has_api_key`, `provider`, `base_url`, `model`, `opencode_runtime_enabled`, `approval_policy`, `session_persistence_enabled`. No personalization fields.
- `TextareaControl` exists (`app.tsx:640-670`) but **zero settings register `control: 'textarea'`** — infrastructure ready, unused

### 2.3 Settings UI components — verbatim signatures

**`AsyncToggleButton`** (`domain_panes.tsx`):
```tsx
function AsyncToggleButton({enabled, label, onToggle}: {
  enabled: boolean;
  label: string;
  onToggle: (nextEnabled: boolean) => Promise<void> | void;
})
```

**`ManagedSettingRow`** (`domain_panes.tsx`):
```tsx
function ManagedSettingRow({children, controlClassName, description, isFirstRow = false, title}: {
  children?: React.ReactNode;
  controlClassName?: string;
  description: string;
  isFirstRow?: boolean;
  title: string;
})
```

**`commitSettingValue`** (`store.ts`):
```typescript
async commitSettingValue(key: string, value: string): Promise<boolean>
```
- Returns `true` on success
- Guards double-commit via `savingKeys`
- After save, C++ calls `ScheduleNotify()` (50ms debounced) → renderer receives updated settings array

**`TextareaControl`** uses only `meta.label` (aria-label) and `meta.placeholder`. Commits on `onBlur`. No `default`, no `maxLength` field on `SettingMetadata`.

### 2.4 Storage layer (rusqlite + LMDB)

**`maho-storage/Cargo.toml`** verbatim:
```toml
rusqlite = { version = "0.33", features = ["bundled"] }
heed = "0.20"
```
- Resolves to rusqlite 0.33.0 / libsqlite3-sys 0.31.0
- **`load_extension` feature is NOT enabled** (so sqlite-vec is impossible without rebuild + iOS sandbox issue)
- FTS5 IS available (bundled by default)
- No sqlx in maho-storage; sqlx-sqlite only in isolated `relay/` workspace

**Schema mechanism** (`sqlite.rs`):
- `initialize_schema()` runs `execute_batch()` of `CREATE TABLE IF NOT EXISTS` at lines 115-324
- `run_migrations()` lines 54-101 is a bag of `let _ = self.conn.execute("ALTER TABLE ... ADD COLUMN ...")` — errors swallowed (idempotent)
- **No `PRAGMA user_version` mechanism exists** — recommended to add for clean v2

**iOS/Android linkage**: Both use bundled SQLite via libsqlite3-sys (compiled into Rust static lib + .so). No system SQLite reference. No `load_extension` calls anywhere in workspace.

**Cargo.lock verified**: no `sqlite-vec`, `vectorlite`, `hnsw_rs`, `instant-distance`, `usearch` currently — confirming we're adding fresh deps.

### 2.5 Sync system

**`SyncEntityType` enum** (`sync_models.rs:93`):
```rust
Space, Tab, Bookmark, Note, Boost, ReadingListItem, SearchEngine, Shortcut
```
**`Memory` variant does NOT exist yet.**

**`SyncConfig`** has no `disabled_entity_types`. The only "toggle" is `auto_sync: bool` (global).

**Push/pull paths** (`sync_manager.rs:81`, `:135`): NO entity-type guard. Sync is all-or-nothing.

**`AccountInfo.sync_enabled`** (`account.rs:21`) is **vestigial for routing** — only read at `maho_core.rs:4002` for UI display.

**Encryption** (`sync_crypto.rs`):
- AES-256-GCM, key derived from 32-byte seed via Argon2id (salt `b"maho-sync-v1-key"`)
- Key lives in `SyncManager.encryption_key: Option<EncryptionKey>` — **in-memory only**
- **Plaintext fallback exists** at `sync_manager.rs:99-101` — sends unencrypted if no key. Memory entities MUST block this fallback.

**Seed persistence**:
- maho-core does NOT persist the seed
- macOS shell stores raw `sync_key` string in `UserDefaults` under `"maho_sync_key"` (`SyncManager.swift:20`, `AccountSyncSettingsView.swift:161/275`)
- **No Keychain usage** (macos-shell/AGENTS.md forbids it)
- Should migrate to `SecureLocalStore` (AES-GCM file store, already exists for AI API keys)

**Existing FFI sync exports** (11 total, in `maho-ffi/src/lib.rs` and `maho_ffi.h`):
```
maho_core_start_sync, stop_sync, get_sync_status, get_connected_devices,
send_tab, generate_sync_key, join_sync, remove_sync_device,
apply_remote_entities, drain_outgoing, send_tab_to_device
```

**SignOut handler** (`maho_core.rs:2335-2343`):
- Clears `AccountInfo` and resets `SyncState::Idle`
- Does **NOT** call `stop_sync()`
- Does **NOT** wipe local entity data
- **Memory wipe must be added here**

### 2.6 Maho-core architecture

**`MahoCore` struct** (`maho_core.rs:673-711`):
- `storage: Option<SqliteStorage>` — **private, no `Arc<>`**
- `lmdb_storage: Option<LmdbStorage>` — private
- `sync_manager: Option<SyncManager>` — private
- All managers (LLMManager, McpManager, etc.) are owned values, not Arc

**Storage access pattern**:
- Sub-managers do NOT hold storage handles
- `MahoCore` mediates persistence: calls `self.storage.as_ref()` directly after sub-manager work completes

**Background task pattern** (`chat_session.rs:206-209`):
```rust
fn get_runtime() -> &'static Runtime {
    static RT: OnceLock<Runtime> = OnceLock::new();
    RT.get_or_init(|| Runtime::new().expect("Failed to create tokio runtime"))
}
```
Then `get_runtime().spawn(async move { ... })` and store `JoinHandle` on the manager.

### 2.7 Opencode/ACP runtime

**`AcpClient`** (`maho-acp/src/client.rs:23-31`):
- **NOT Clone** (no derives at all)
- `shutdown(self)` consumes self
- Solution: wrap in `Arc<AcpClient>`. Final shutdown via `Arc::try_unwrap().unwrap().shutdown().await`

**`maho-acp` is NOT yet a dep of `maho-core`** — must add to `Cargo.toml` if used

**`PromptHandle` is not re-exported from lib.rs** — use `maho_acp::client::PromptHandle`

**ACP transport**: stdio nd-JSON, opencode is external binary (PATH or `runtime_command` pref)

**Single-session limitation**: `MahoOpenCodeAcpAdapter` (UI panel) is single-session. Background tasks must use a **separate `AcpClient` instance** to avoid stomping the active panel session.

**However: this plan does NOT use opencode for memory extraction** (see §3 architectural revision)

---

## 3. Final Architecture (Revisions Applied)

### 3.1 Three-tier memory representation
```
HOT  (always injected, ≤2K tokens)  → user preferences + persona blocks
WARM (injected if budget allows)     → recent browsing summary + last conv summary
COLD (semantic search top-5)         → extracted facts via HNSW
```

Total context budget: 4K tokens hard cap.

### 3.2 Embedding backend
```toml
# maho-core/Cargo.toml — per-target dispatch
[target.'cfg(not(any(target_os = "ios", target_os = "android")))'.dependencies]
fastembed = { version = "5", default-features = false, features = ["hf-hub-rustls-tls"] }

[target.'cfg(any(target_os = "ios", target_os = "android"))'.dependencies]
tract-onnx = "0.21"
tokenizers = { version = "0.22", default-features = false, features = ["onig"] }
```

**Model choice**: `BGESmallENV15Q` (384-dim INT8 quantized, ~6 MB ONNX file)
- Ships ONNX file with the app bundle (no first-run download for end-user)
- Or downloads on first use with progress UI (~6 MB)

**FTS5 fallback**: When embedding model unavailable or inference fails → SQL full-text search via `memories_fts MATCH ?`

### 3.3 Vector storage: pure-Rust HNSW
```toml
# maho-storage/Cargo.toml
instant-distance = "0.6"
```
- Embeddings stored as BLOB (384 × f32 LE = 1536 bytes per row)
- HNSW index built **in-memory** at `SqliteStorage::open()` time (200-5000 facts → 10-50ms)
- New facts: insert into SQLite + `index.add()` to HNSW
- Search: `index.search(query_vec, ...)` returns top-K fact_ids, then `SELECT * FROM memories WHERE id IN (...)`

### 3.4 Fact extraction: direct LLM API (NOT opencode)
- New module `maho-core/src/llm_client.rs` (or extend existing if any)
- POST to `{base_url}/chat/completions` with `response_format: {"type": "json_object"}`
- Reuses user's already-configured provider/model/api_key from `maho.ai.*` prefs
- Bypasses opencode entirely — extraction is a pure LLM call, no tools, no sessions

---

## 4. Data Model (Final SQL Schema)

To be appended to `maho-storage/src/sqlite.rs:115-324` `execute_batch!`:

```sql
-- HOT/WARM tier: label-keyed blocks
CREATE TABLE IF NOT EXISTS memory_blocks (
    id           TEXT PRIMARY KEY,
    label        TEXT UNIQUE NOT NULL,
    content      TEXT NOT NULL,
    tier         TEXT NOT NULL DEFAULT 'hot',
    token_count  INTEGER DEFAULT 0,
    sync_enabled INTEGER DEFAULT 1,
    version      INTEGER DEFAULT 0,
    updated_at   TEXT DEFAULT CURRENT_TIMESTAMP
);

-- COLD tier: extracted facts with embeddings
CREATE TABLE IF NOT EXISTS memories (
    id            TEXT PRIMARY KEY,
    content       TEXT NOT NULL,
    source_type   TEXT NOT NULL,    -- 'chat' | 'browse' | 'user_input'
    source_id     TEXT,
    categories    TEXT,             -- JSON array
    importance    REAL DEFAULT 0.5,
    superseded_by TEXT REFERENCES memories(id),
    embedding     BLOB,             -- 384 × f32 LE = 1536 bytes
    sync_enabled  INTEGER DEFAULT 1,
    version       INTEGER DEFAULT 0,
    created_at    TEXT DEFAULT CURRENT_TIMESTAMP
);
CREATE INDEX IF NOT EXISTS idx_memories_active
    ON memories(superseded_by) WHERE superseded_by IS NULL;

-- FTS5 fallback (zero new deps — bundled in rusqlite)
CREATE VIRTUAL TABLE IF NOT EXISTS memories_fts USING fts5(
    memory_id UNINDEXED,
    content,
    tokenize='porter unicode61'
);

-- Raw conversation log (source for fact extraction)
CREATE TABLE IF NOT EXISTS conversations (
    id          TEXT PRIMARY KEY,
    session_id  TEXT NOT NULL,
    role        TEXT NOT NULL,
    content     TEXT NOT NULL,
    url_context TEXT,
    created_at  TEXT DEFAULT CURRENT_TIMESTAMP
);
CREATE INDEX IF NOT EXISTS idx_conv_session ON conversations(session_id, created_at);

-- Browse activity log (source for nightly summarization)
CREATE TABLE IF NOT EXISTS browse_sessions (
    id           TEXT PRIMARY KEY,
    url          TEXT NOT NULL,
    title        TEXT,
    summary      TEXT,
    sync_enabled INTEGER DEFAULT 1,
    visited_at   TEXT DEFAULT CURRENT_TIMESTAMP
);
```

---

## 5. File-by-File Change Inventory

> **NOTE:** This section gives a high-level table of files affected. For **verbatim before/after diffs** with exact code at exact insertion points, see **§11 "File-by-File Change Manifest"** below.

All line numbers are verified against commit `9a4921c`. May drift after merges — re-verify before editing.

### 5.1 Rust crates

| File | Change |
|---|---|
| `maho/crates/maho-storage/Cargo.toml` | Add: `instant-distance = "0.6"` |
| `maho/crates/maho-storage/src/sqlite.rs:115-324` | Append 5 new `CREATE TABLE` blocks (memory_blocks, memories, memories_fts, conversations, browse_sessions) inside `execute_batch!` |
| `maho/crates/maho-storage/src/sqlite.rs:54` `run_migrations()` start | (Optional but recommended) Add `PRAGMA user_version` v2 guard |
| `maho/crates/maho-storage/src/sqlite.rs` (new methods) | `delete_all_memories()`, `delete_memory_blocks_by_label()`, `insert_memory(...)`, `update_memory_block(...)`, `search_memories_fts(query, limit)`, `load_all_memory_embeddings() -> Vec<(id, Vec<f32>)>`, `insert_conversation(...)`, `insert_browse_session(...)`. Pattern: copy `clear_history` (line 362), `delete_all_shortcuts` (line 1431) |
| `maho/crates/maho-core/Cargo.toml` | Add: `instant-distance = "0.6"`, target-conditional `fastembed`/`tract-onnx`+`tokenizers` (see §3.2) |
| `maho/crates/maho-core/src/embedding.rs` (NEW) | `cfg(target_os)` dispatch for embed function. See §6.1 skeleton |
| `maho/crates/maho-core/src/memory_manager.rs` (NEW) | `MemoryManager` struct, `OnceLock<Runtime>` pattern, in-memory HNSW. See §6.2 skeleton |
| `maho/crates/maho-core/src/llm_client.rs` (NEW or extend) | `extract_facts_json(auth, text) -> Vec<RawFact>` using `response_format: json_object`. See §6.3 skeleton |
| `maho/crates/maho-core/src/maho_core.rs:673-711` `MahoCore` struct | Add `memory_manager: MemoryManager` field |
| `maho/crates/maho-core/src/maho_core.rs` (`MahoCore::new`/`with_storage`) | Initialize `memory_manager`. Wire result-channel drain into tick loop |
| `maho/crates/maho-core/src/maho_core.rs:2335` SignOut handler | Add `if let Some(ref s) = self.storage { let _ = s.delete_all_memories(); let _ = s.delete_memory_blocks_by_label("preferences"); ... }` |
| `maho/crates/maho-core/src/sync_models.rs:93` `SyncEntityType` enum | Add `Memory` variant |
| `maho/crates/maho-core/src/sync_models.rs` `SyncConfig` | Add `disabled_entity_types: HashSet<SyncEntityType>` field |
| `maho/crates/maho-core/src/sync_manager.rs:81` `push_entity()` | Add guard: `if self.config.disabled_entity_types.contains(&entity.entity_type) { return; }` |
| `maho/crates/maho-core/src/sync_manager.rs:135` `apply_remote_entity()` | Same guard |
| `maho/crates/maho-core/src/sync_manager.rs:99-101` plaintext fallback | For `entity.entity_type == SyncEntityType::Memory && self.encryption_key.is_none()` → return without push (hard-error). Other entities: keep existing behavior or also block (TBD) |
| `maho/crates/maho-core/src/sync_manager.rs:68` (after `update_config`) | Add `set_entity_sync_enabled(&mut self, t: SyncEntityType, enabled: bool)` and `is_entity_sync_enabled(&self, &SyncEntityType) -> bool` |
| `maho/crates/maho-core/src/maho_core.rs` | Add `pub fn set_entity_sync_enabled(&mut self, type_str: &str, enabled: bool)` wrapper. Add `parse_sync_entity_type` "Memory" arm at `:5533` |
| `maho/crates/maho-ffi/src/lib.rs` | New exports: `maho_core_set_entity_sync_enabled(ptr, type_str, enabled)`, `maho_core_extract_facts_from_session(ptr, session_id) -> task_id`, `maho_core_get_relevant_memories(ptr, query, top_k) -> json`, `maho_core_set_memory_block(ptr, label, content)`, `maho_core_get_memory_block(ptr, label) -> string`, `maho_core_clear_all_memories(ptr)`. Pattern: void-setter precedent at existing `maho_core_start_sync` |
| `maho/crates/maho-types/src/settings.rs` | (If extending shared settings type for memory toggles) — add minimal fields, OR keep memory toggles only in C++ PrefService and don't sync via maho-core settings |

### 5.2 C++ Chromium overlay

| File | Change |
|---|---|
| `maho-chromium/third_party/maho/maho_ffi.h` | Declare 6 new FFI functions matching Rust exports |
| `maho-chromium/third_party/maho/maho_bridge.cc/.h` | Add C++ wrappers if any (some FFI functions may be called directly) |
| `maho/{ios,macos}-shell/include/maho_ffi.h` | Mirror header changes |
| `maho-chromium/browser/ui/webui/maho_ai_prefs.h` | Add 8 new pref keys: `kPersonalizationTaste`, `kPersonalizationAboutYou`, `kPersonalizationWritingStyle`, `kPersonalizationCodingPrefs`, `kPersonalizeNewChats`, `kShowSuggestionsNewTab`, `kMemoryEnabled`, `kSyncMemoryEnabled` |
| `maho-chromium/browser/ui/webui/maho_ai/maho_ai_prefs_registration.cc` | `RegisterStringPref` for 4 textareas (default `""`); `RegisterBooleanPref` for 4 toggles (sensible defaults: personalize_new_chats=true, show_suggestions=false, memory_enabled=false initially, sync_memory=false initially) |
| `maho-chromium/browser/ui/webui/maho_ai/maho_ai_page_handler.cc:1181` `OpenSettings()` | Change URL to `chrome://maho-settings/?pane=personalization` |
| `maho-chromium/browser/ui/webui/maho_settings/maho_settings_page_handler.cc:1174` `SetSetting` | Add cases for 8 new keys. Pref-backed: read/write Chromium PrefService. Memory sync toggle: also call `maho_core_set_entity_sync_enabled(core, "Memory", enabled)`. Then `ScheduleNotify()` |
| `maho-chromium/browser/ui/webui/maho_settings/maho_settings_page_handler.cc` `BuildSettingsSnapshot` | Append 8 new settings to snapshot |
| `maho-chromium/browser/ui/webui/maho_settings/maho_settings.mojom` | Either extend `AISettings` with personalization fields OR (recommended) add new struct `PersonalizationSettings`. If keeping FFI route: nothing in mojom — handle via `SetSetting`/snapshot |
| `maho-chromium/browser/ai/maho_chat_orchestration_service.cc` (around line 777 `kSummarizeCurrentPage`) | At session start: build memory context from `MahoCore::get_relevant_memories(query, 5)` + hot blocks from PrefService. Inject as system message before page context. Token budget enforcement |
| `maho-chromium/browser/ui/webui/maho_ai/maho_ai.mojom` | (If passing memory_context as Mojo struct field in `ChatRequestContext`) — add `memory_context: string?` field. Otherwise no change |
| `maho-chromium/components/constants/webui_url_constants.h` | (Possibly add) `chrome://maho-memory/` if separate management UI is wanted; not required for v1 |

### 5.3 TypeScript (settings UI)

| File | Change |
|---|---|
| `maho-chromium/browser/resources/maho_settings/models.ts` | Add `'personalization'` to `contentKind` union. (Optional) Add `maxLength?: number` to `SettingMetadata` if textarea char limits desired |
| `maho-chromium/browser/resources/maho_settings/schema/setting_schema.ts` | Add 8 setting definitions:<br>- `personalization.personalize_new_chats` (toggle)<br>- `personalization.show_suggestions` (toggle)<br>- `personalization.memory_enabled` (toggle)<br>- `personalization.taste` (textarea)<br>- `personalization.about_you` (textarea)<br>- `personalization.writing_style` (textarea)<br>- `personalization.coding_prefs` (textarea)<br>- `sync.memory_enabled` (toggle, used in SyncPane) |
| `maho-chromium/browser/resources/maho_settings/schema/panes.ts` | Add new pane: `{key:'personalization', symbol:'Pe', navTitle:'Personalization', title:'Personalization', kind:'live', domain:'productivity', contentKind:'personalization'}`. Position in `productivity` domain (alongside Maho AI) |
| `maho-chromium/browser/resources/maho_settings/react/domain_panes.tsx` | Add `PersonalizationPane` component (custom layout with 3 toggles + 4 textareas + Memory enable button + clear-memory action). Add case to `DomainPaneContent` switch. Modify `SyncPane` to render Memory Sync toggle row when `state.data.status.isSyncing` is true |
| `maho-chromium/browser/resources/maho_settings/react/store.ts` | (Already has `commitSettingValue`) — no changes unless adding dedicated Mojo methods for memory ops |

### 5.4 macOS shell (optional, recommended for sync seed security)

| File | Change |
|---|---|
| `maho/macos-shell/Sources/Sync/SyncManager.swift:20` | Migrate `UserDefaults.standard.string(forKey:"maho_sync_key")` → `SecureLocalStore.shared.read(key:"sync.recovery_phrase")` |
| `maho/macos-shell/Sources/Settings/AccountSyncSettingsView.swift:161,275` | Same migration |
| One-time migration code | On first launch after upgrade: if UserDefaults has `maho_sync_key`, copy to SecureLocalStore + remove from UserDefaults |

---

## 6. Code Skeletons

### 6.1 `maho-core/src/embedding.rs`

```rust
//! Cross-platform embedding backend for Memory feature.
//!
//! Desktop (macOS/Linux/Windows): fastembed → ONNX Runtime
//! Mobile (iOS/Android): tract-onnx (pure Rust, slower but no C++ deps)

use crate::error::CoreError;

const EMBED_DIM: usize = 384;

#[cfg(not(any(target_os = "ios", target_os = "android")))]
mod backend {
    use super::*;
    use fastembed::{TextEmbedding, InitOptions, EmbeddingModel};
    use std::sync::OnceLock;

    static MODEL: OnceLock<TextEmbedding> = OnceLock::new();

    pub fn embed(texts: &[String]) -> Result<Vec<Vec<f32>>, CoreError> {
        let model = MODEL.get_or_init(|| {
            TextEmbedding::try_new(InitOptions::new(EmbeddingModel::BGESmallENV15Q))
                .expect("init embedding model")
        });
        Ok(model.embed(texts.to_vec(), None)
            .map_err(|e| CoreError::Embedding(e.to_string()))?)
    }
}

#[cfg(any(target_os = "ios", target_os = "android"))]
mod backend {
    use super::*;
    // tract-onnx + tokenizers implementation
    // Loads BGESmall ONNX from app bundle resources
    // ~2-4x slower than ORT on CPU but compiles cleanly without C++ deps
    pub fn embed(texts: &[String]) -> Result<Vec<Vec<f32>>, CoreError> {
        // TODO: implement tract pipeline
        // 1. Load BGESmall ONNX (bundled as Resources/bge-small-en-v1.5-q.onnx)
        // 2. Tokenize via huggingface tokenizers
        // 3. Run inference
        // 4. Mean-pool last_hidden_state → 384-dim vec
        // 5. L2 normalize
        unimplemented!("tract backend pending W3 PoC")
    }
}

pub use backend::embed;
pub fn dim() -> usize { EMBED_DIM }
```

### 6.2 `maho-core/src/memory_manager.rs`

```rust
//! Memory feature backend: extracts facts from chat sessions, embeds them,
//! retrieves them as AI context. Operates as a sub-manager owned by MahoCore.

use std::sync::OnceLock;
use tokio::runtime::Runtime;
use tokio::sync::mpsc;
use tokio::task::JoinHandle;
use serde::{Serialize, Deserialize};
use instant_distance::{Builder, Hnsw, Search};

use crate::embedding;
use crate::llm_client;
use maho_storage::sqlite::SqliteStorage;

fn get_runtime() -> &'static Runtime {
    static RT: OnceLock<Runtime> = OnceLock::new();
    RT.get_or_init(|| Runtime::new().expect("memory rt"))
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct RawFact {
    pub fact: String,
    #[serde(default)]
    pub category: String,
    #[serde(default = "default_importance")]
    pub importance: f32,
}
fn default_importance() -> f32 { 0.5 }

#[derive(Debug)]
pub enum MemoryResult {
    FactsExtracted { session_id: String, facts: Vec<RawFact> },
    EmbeddingsReady { fact_id: String, embedding: Vec<f32> },
    BrowsingSummaryReady { content: String },
    Failed { reason: String },
}

/// Wrapper for instant-distance Point trait
#[derive(Clone, Debug)]
pub struct Embedding(pub Vec<f32>);
impl instant_distance::Point for Embedding {
    fn distance(&self, other: &Self) -> f32 {
        // cosine distance: 1 - cosine_similarity
        // (or use squared L2 if vectors are L2-normalized — equivalent)
        let dot: f32 = self.0.iter().zip(&other.0).map(|(a, b)| a * b).sum();
        let na: f32 = self.0.iter().map(|v| v * v).sum::<f32>().sqrt();
        let nb: f32 = other.0.iter().map(|v| v * v).sum::<f32>().sqrt();
        1.0 - (dot / (na * nb + 1e-8))
    }
}

#[derive(Clone)]
pub struct AuthSnapshot {
    pub provider: String,
    pub base_url: String,
    pub api_key: String,
    pub model: String,
}

pub struct MemoryManager {
    /// In-memory ANN index (fact_id keyed)
    hnsw: Option<Hnsw<Embedding, String>>,
    result_rx: mpsc::UnboundedReceiver<MemoryResult>,
    result_tx: mpsc::UnboundedSender<MemoryResult>,
    active_tasks: Vec<JoinHandle<()>>,
    auth: Option<AuthSnapshot>,
}

impl MemoryManager {
    pub fn new() -> Self {
        let (tx, rx) = mpsc::unbounded_channel();
        Self {
            hnsw: None,
            result_rx: rx,
            result_tx: tx,
            active_tasks: Vec::new(),
            auth: None,
        }
    }

    pub fn set_auth(&mut self, auth: Option<AuthSnapshot>) {
        self.auth = auth;
    }

    /// Build HNSW from all stored embeddings. Call at MahoCore startup.
    pub fn rebuild_index(&mut self, storage: &SqliteStorage) -> Result<(), crate::error::CoreError> {
        let entries = storage.load_all_memory_embeddings()?;  // Vec<(id: String, vec: Vec<f32>)>
        if entries.is_empty() {
            self.hnsw = None;
            return Ok(());
        }
        let (ids, vecs): (Vec<String>, Vec<Embedding>) =
            entries.into_iter().map(|(id, v)| (id, Embedding(v))).unzip();
        let hnsw = Builder::default().build(vecs, ids);
        self.hnsw = Some(hnsw);
        Ok(())
    }

    /// Add a single new fact's embedding. For incremental updates.
    /// instant-distance doesn't support online insert — for now we mark
    /// pending and rebuild on next batch. Or rebuild whole index (cheap for <5k).
    pub fn note_pending_index_rebuild(&mut self) {
        // TODO: in v1 just trigger full rebuild after each batch insert.
        // In v2 consider switching to hnsw_rs which supports online insert.
    }

    /// Top-K semantic search.
    pub fn search(&self, query_embedding: &[f32], top_k: usize) -> Vec<String> {
        let Some(ref hnsw) = self.hnsw else { return Vec::new(); };
        let q = Embedding(query_embedding.to_vec());
        let mut search = Search::default();
        hnsw.search(&q, &mut search)
            .take(top_k)
            .map(|item| item.value.clone())
            .collect()
    }

    /// Triggered at session-end. Sync method spawns an async extraction.
    pub fn extract_from_session(&mut self, session_id: String, raw_text: String) {
        let Some(auth) = self.auth.clone() else { return; };
        let tx = self.result_tx.clone();
        let h = get_runtime().spawn(async move {
            match llm_client::extract_facts_json(&auth, &raw_text).await {
                Ok(facts) => { let _ = tx.send(MemoryResult::FactsExtracted { session_id, facts }); }
                Err(e)    => { let _ = tx.send(MemoryResult::Failed { reason: format!("extract: {e}") }); }
            }
        });
        self.active_tasks.push(h);
    }

    /// Triggered for nightly browsing summary.
    pub fn summarize_browsing(&mut self, raw_text: String) {
        let Some(auth) = self.auth.clone() else { return; };
        let tx = self.result_tx.clone();
        let h = get_runtime().spawn(async move {
            match llm_client::summarize_browsing(&auth, &raw_text).await {
                Ok(content) => { let _ = tx.send(MemoryResult::BrowsingSummaryReady { content }); }
                Err(e)      => { let _ = tx.send(MemoryResult::Failed { reason: format!("summary: {e}") }); }
            }
        });
        self.active_tasks.push(h);
    }

    /// Spawn embedding job for a fact.
    pub fn embed_fact(&mut self, fact_id: String, content: String) {
        let tx = self.result_tx.clone();
        let h = get_runtime().spawn(async move {
            // embedding::embed is sync — call via spawn_blocking
            let result = tokio::task::spawn_blocking(move || {
                embedding::embed(&[content])
            }).await;
            match result {
                Ok(Ok(mut vecs)) if !vecs.is_empty() => {
                    let _ = tx.send(MemoryResult::EmbeddingsReady { fact_id, embedding: vecs.remove(0) });
                }
                Ok(Err(e)) => { let _ = tx.send(MemoryResult::Failed { reason: format!("embed: {e}") }); }
                Err(e)     => { let _ = tx.send(MemoryResult::Failed { reason: format!("embed join: {e}") }); }
                _ => { let _ = tx.send(MemoryResult::Failed { reason: "empty embedding".into() }); }
            }
        });
        self.active_tasks.push(h);
    }

    /// Drain results — called by MahoCore each tick. Caller persists via storage.
    pub fn drain_results(&mut self) -> Vec<MemoryResult> {
        let mut out = Vec::new();
        while let Ok(r) = self.result_rx.try_recv() { out.push(r); }
        // Reap finished tasks
        self.active_tasks.retain(|h| !h.is_finished());
        out
    }
}
```

### 6.3 `maho-core/src/llm_client.rs` (Memory-relevant additions)

```rust
//! Direct LLM API client for Memory feature: bypasses opencode for
//! deterministic JSON extraction with `response_format: json_object`.

use serde::Deserialize;
use serde_json::json;
use crate::memory_manager::{AuthSnapshot, RawFact};
use crate::error::CoreError;

const FACT_EXTRACTION_SYSTEM: &str =
    "You extract durable user facts from text. Return ONLY a JSON object: \
     {\"facts\":[{\"fact\":\"<statement>\",\"category\":\"preference|setting|decision|knowledge\",\"importance\":0.0-1.0}]}. \
     Skip transient or task-specific state. Maximum 5 facts. Empty array if none.";

const BROWSING_SUMMARY_SYSTEM: &str =
    "You summarize browsing activity into a concise paragraph (≤150 words) \
     describing the user's interests and recent topics. Plain text, no markdown.";

#[derive(Deserialize)]
struct ChatCompletionResp {
    choices: Vec<Choice>,
}
#[derive(Deserialize)]
struct Choice {
    message: ChatMessage,
}
#[derive(Deserialize)]
struct ChatMessage {
    content: String,
}

#[derive(Deserialize)]
struct FactsEnvelope {
    facts: Vec<RawFact>,
}

pub async fn extract_facts_json(auth: &AuthSnapshot, text: &str) -> Result<Vec<RawFact>, CoreError> {
    let body = json!({
        "model": auth.model,
        "messages": [
            {"role": "system", "content": FACT_EXTRACTION_SYSTEM},
            {"role": "user",   "content": format!("Extract facts:\n\n{}", text)}
        ],
        "temperature": 0.1,
        "max_tokens": 512,
        "response_format": { "type": "json_object" }
    });

    let url = format!("{}/chat/completions", auth.base_url.trim_end_matches('/'));
    let resp: ChatCompletionResp = reqwest::Client::new()
        .post(&url)
        .bearer_auth(&auth.api_key)
        .json(&body)
        .send().await
        .map_err(|e| CoreError::Network(e.to_string()))?
        .error_for_status()
        .map_err(|e| CoreError::Network(e.to_string()))?
        .json().await
        .map_err(|e| CoreError::Network(e.to_string()))?;

    let content = resp.choices.into_iter().next()
        .ok_or_else(|| CoreError::Parse("no choices".into()))?
        .message.content;

    let env: FactsEnvelope = serde_json::from_str(&content)
        .map_err(|e| CoreError::Parse(format!("json: {e}; raw: {content}")))?;
    Ok(env.facts)
}

pub async fn summarize_browsing(auth: &AuthSnapshot, text: &str) -> Result<String, CoreError> {
    let body = json!({
        "model": auth.model,
        "messages": [
            {"role": "system", "content": BROWSING_SUMMARY_SYSTEM},
            {"role": "user",   "content": format!("Recent activity:\n\n{}", text)}
        ],
        "temperature": 0.2,
        "max_tokens": 256
    });
    let url = format!("{}/chat/completions", auth.base_url.trim_end_matches('/'));
    let resp: ChatCompletionResp = reqwest::Client::new()
        .post(&url)
        .bearer_auth(&auth.api_key)
        .json(&body)
        .send().await
        .map_err(|e| CoreError::Network(e.to_string()))?
        .error_for_status()
        .map_err(|e| CoreError::Network(e.to_string()))?
        .json().await
        .map_err(|e| CoreError::Network(e.to_string()))?;
    Ok(resp.choices.into_iter().next()
        .ok_or_else(|| CoreError::Parse("no choices".into()))?
        .message.content)
}
```

**Provider compatibility note**: OpenAI, Mistral, Together AI, OpenRouter support `response_format: {"type":"json_object"}`. Anthropic Claude does NOT — for Anthropic the workaround is strict prompt instructions + JSON repair pass on parse failure. Add provider switch later.

### 6.4 PersonalizationPane (TypeScript skeleton)

```tsx
// In maho-chromium/browser/resources/maho_settings/react/domain_panes.tsx

interface PersonalizationPaneProps {
  pane: PaneDefinition;
  settings: SettingValue[];
  store: MahoSettingsStore;
}

function PersonalizationPane({pane, settings, store}: PersonalizationPaneProps) {
  const personalizeNewChats = settings.find(s => s.key === 'personalization.personalize_new_chats')?.value !== 'false';
  const showSuggestions = settings.find(s => s.key === 'personalization.show_suggestions')?.value === 'true';
  const memoryEnabled = settings.find(s => s.key === 'personalization.memory_enabled')?.value === 'true';

  const taste = settings.find(s => s.key === 'personalization.taste')?.value || '';
  const aboutYou = settings.find(s => s.key === 'personalization.about_you')?.value || '';
  const writingStyle = settings.find(s => s.key === 'personalization.writing_style')?.value || '';
  const codingPrefs = settings.find(s => s.key === 'personalization.coding_prefs')?.value || '';

  return (
    <DomainPaneSection pane={pane}>
      <SectionHeader title="Personalization" />
      <div className="preferences-card__rows">
        <ManagedSettingRow
          isFirstRow
          title="Personalize new chats"
          description="Maho can tailor its responses with Memory and what you teach it here."
        >
          <AsyncToggleButton
            enabled={personalizeNewChats}
            label="Personalize new chats"
            onToggle={enabled => store.commitSettingValue('personalization.personalize_new_chats', enabled ? 'true' : 'false')}
          />
        </ManagedSettingRow>
        <ManagedSettingRow
          title="Show suggestions on new tab"
          description="Maho will present personalized suggestions using Memory and other tools."
        >
          <AsyncToggleButton
            enabled={showSuggestions}
            label="Show suggestions on new tab"
            onToggle={enabled => store.commitSettingValue('personalization.show_suggestions', enabled ? 'true' : 'false')}
          />
        </ManagedSettingRow>
      </div>

      <SectionHeader title="Memory" />
      <div className="preferences-card__rows">
        <ManagedSettingRow
          isFirstRow
          title="Personalize responses using Memory"
          description="Maho will remember your recent sites, chats, and preferences. Activity is summarized locally and saved on your device."
        >
          <AsyncToggleButton
            enabled={memoryEnabled}
            label="Memory"
            onToggle={enabled => store.commitSettingValue('personalization.memory_enabled', enabled ? 'true' : 'false')}
          />
        </ManagedSettingRow>
      </div>

      <SectionHeader title="Teach Maho about yourself" />
      <div className="preferences-card__rows">
        {/* Render 4 textareas via the live setting renderer or inline TextareaControl */}
        {(['personalization.taste', 'personalization.about_you', 'personalization.writing_style', 'personalization.coding_prefs'] as const).map(key => {
          const item = settings.find(s => s.key === key) ?? {key, value: ''};
          const meta = SETTING_DEFINITIONS.find(d => d.key === key)?.metadata;
          if (!meta) return null;
          return (
            <ManagedSettingRow
              key={key}
              title={meta.label}
              description={meta.description}
              controlClassName="setting-row__control--stacked"
            >
              <TextareaControl item={item} meta={meta} store={store} />
            </ManagedSettingRow>
          );
        })}
      </div>
    </DomainPaneSection>
  );
}
```

---

## 7. Phased Roadmap

### W0 — De-risking PoCs (3 days)

Before any feature work, validate the 3 highest-risk assumptions:

| PoC | Goal | Pass criterion | Fail mitigation |
|---|---|---|---|
| **W0.1** instant-distance benchmark | Confirm pure-Rust HNSW handles 200-5000 facts at acceptable latency | Build <100ms, top-5 search <10ms | Fall back to brute-force cosine scan (200 facts × 384 dim × f32 dot product = trivial) |
| **W0.2** iOS tract-onnx build | Confirm `tract-onnx` + BGESmall ONNX compiles for `aarch64-apple-ios` and runs inference on simulator | iOS build passes, single embed <500ms on device | Disable embedding on iOS/Android in v1 → FTS5 only on mobile |
| **W0.3** LLM JSON extraction reliability | Confirm `response_format: json_object` returns parseable JSON from configured providers | ≥98% parse success on 100 sample texts (OpenAI + at least one openai-compatible) | Add JSON repair pass; downgrade unsupported providers to strict-prompt mode |

**Each PoC is a small standalone cargo bin or test in `maho-core/tests/`** — does not modify production code paths. Commit only if all 3 pass.

### W1 — Personalization UI + Hot block injection (1 week)

**Goal**: User can fill in Personalization fields and see them reflected in new AI sessions.

Steps:
1. Add 8 pref keys + registration (C++)
2. Add 8 setting definitions (TS)
3. Add `personalization` pane to schema/panes.ts + new PersonalizationPane component
4. Add Personalization case to DomainPaneContent switch
5. Wire `commitSettingValue` for all 8 keys
6. Change `OpenSettings` C++ to navigate with `?pane=personalization`
7. C++ `MahoChatOrchestrationService` — at session start, fetch 4 textarea prefs from PrefService, inject as system message prefix:
   ```
   ## User preferences
   {taste, if non-empty}
   ## About the user
   {about_you, if non-empty}
   ## Writing style
   {writing_style, if non-empty}
   ## Coding preferences
   {coding_prefs, if non-empty}
   ```
8. Token budget guard: cap total injection at 2K tokens, truncate from end if exceeded

**Acceptance test**: User enters "I live in Tokyo" in `about_you` textarea → opens new chat → asks "Where do I live?" → AI responds "Tokyo".

**No backend Memory work in W1** — purely UI + static pref injection. This is the lowest-risk highest-visibility chunk.

### W2 — Raw log tables + session-end trigger (1 week)

**Goal**: Sessions and browse activity are persistently logged in SQLite.

Steps:
1. Add `conversations`, `browse_sessions` tables to sqlite.rs (skip `memory_blocks`/`memories` for now)
2. Add storage methods: `insert_conversation`, `insert_browse_session`, `load_conversation_by_session`, `load_browse_sessions_since`
3. Wire C++ `MahoChatOrchestrationService` to call `MahoCore` to persist each turn
4. Wire C++ history observer to persist browse sessions (bridge to existing browsing history API)
5. Add `ShellEvent::SessionEnded { session_id }` if not present
6. `maho_core.rs` SignOut handler: also wipe `conversations` + `browse_sessions`

**Acceptance test**: Have a chat → check sqlite `SELECT * FROM conversations` → all turns present.

### W3 — MemoryManager + extraction + embedding + HNSW (1.5 weeks)

**Goal**: At session end, facts are extracted, embedded, indexed.

Steps:
1. Add `instant-distance` to `maho-storage` and `maho-core`
2. Add `fastembed`/`tract-onnx` per-target deps to `maho-core`
3. Create `embedding.rs` with cfg dispatch
4. Create `memory_manager.rs` with `OnceLock<Runtime>` pattern
5. Create `llm_client.rs` `extract_facts_json` and `summarize_browsing`
6. Add `memory_blocks`, `memories`, `memories_fts` tables
7. Wire `MahoCore` tick loop to drain `MemoryResult` and persist facts + trigger embedding job
8. Add HNSW rebuild on storage open and after each batch insert
9. Add FFI exports
10. Wire `ShellEvent::SessionEnded` → `memory_manager.extract_from_session`

**Acceptance test**: End a chat about cooking → check `SELECT * FROM memories` → has 1-3 facts about cooking preferences with embeddings.

### W4 — Context injection + nightly browsing summary (1 week)

**Goal**: Past memories surface in new chats automatically.

Steps:
1. C++ `MahoChatOrchestrationService` at session start:
   - Fetch hot blocks from PrefService (preferences from W1)
   - Embed user's first message → `MahoCore::get_relevant_memories(query, 5)`
   - Inject memory context as system message
2. Token budget enforcement: 4K total, 2K hot, 1K warm, 1K cold
3. Nightly task scheduler: tokio interval in `MahoCore` triggers `summarize_browsing` if last summary >24h
4. Updated browsing summary stored in `memory_blocks WHERE label='browsing_summary'`

**Acceptance test**: Have a chat about Python → 3 days later → start new chat → ask "what do I usually code in?" → AI answers "Python" via retrieved memory.

### W5 — Memory sync + opt-out (1 week)

**Goal**: Memories sync across devices with explicit per-type opt-out.

Steps:
1. Add `SyncEntityType::Memory` to `sync_models.rs`
2. Add `disabled_entity_types` to `SyncConfig`
3. Add push/pull guards to `sync_manager.rs`
4. Block plaintext fallback for Memory type
5. Add `set_entity_sync_enabled` API + FFI export
6. Add `sync.memory_enabled` setting + handler in `SetSetting`
7. SyncPane UI: add Memory Sync toggle row when `isSyncing`
8. SignOut: wipe all memory tables (added)
9. macOS shell: migrate sync_seed to SecureLocalStore

**Acceptance test**:
- Two devices with same recovery phrase, Memory toggle ON: facts sync across
- Toggle Memory OFF on device A: device B's memories no longer leak to A
- Sign out on A: all memories wiped locally
- Memory entity push with no encryption key: pushes are dropped (no plaintext leak)

### W6 — Management UI + QA (3-5 days)

**Goal**: User can review/delete memories and view memory storage usage.

Steps:
1. Add memory management section to PersonalizationPane:
   - "Memory storage: X facts, Y MB"
   - "View all memories" button → modal/sub-pane listing all memories with delete
   - "Clear all memories" button (confirm dialog)
2. Mojo methods: `GetAllMemories`, `DeleteMemory`, `ClearAllMemories`
3. Wire to `maho-core` storage methods
4. Update HNSW after deletions

**Acceptance test**: Delete one memory in UI → it's gone from sqlite + HNSW + does not surface in next chat.

---

## 8. Risk Register

| ID | Risk | Severity | Mitigation |
|---|---|---|---|
| R1 | iOS tract-onnx build doesn't compile | High | W0.2 PoC validates first. Fallback: disable embedding on iOS/Android, FTS5 only |
| R2 | Anthropic doesn't support `response_format: json_object` | Medium | Provider switch in `llm_client.rs` — Anthropic uses strict prompt + JSON repair pass |
| R3 | Embedding model first-run download UX (~6 MB) | Low | Bundle ONNX file in app resources OR download silently with FTS5 fallback during download |
| R4 | Sync seed in UserDefaults is plaintext | Medium | W5 includes SecureLocalStore migration |
| R5 | Memory facts hallucinated / wrong | High | importance < 0.7 not auto-injected. User can review/delete in W6 management UI. ADD-only with `superseded_by` chain prevents data loss |
| R6 | LLM extraction costs (every session-end = 1 API call) | Low-Med | User's own API key; offer to disable extraction for cost-sensitive users |
| R7 | HNSW index drift on incremental adds | Low | instant-distance doesn't support online add — full rebuild on each batch (cheap for <5k facts: ~50ms). v2 could switch to hnsw_rs for online insert |
| R8 | Token budget exceeded by hot blocks | Med | Hard cap 2K hot, truncate from end. Test with maximum-fill personalization fields |
| R9 | Memory sync without encryption key leaks plaintext | High | Hard-block in `sync_manager.rs:99-101` for `SyncEntityType::Memory` |
| R10 | SQL migration breaks existing users | Med | Use `CREATE TABLE IF NOT EXISTS` (idempotent) + optional `PRAGMA user_version` v2 guard |

---

## 9. How to Resume This Plan

When working tree is clean:

1. **Create branch**:
   ```bash
   git checkout main && git pull
   git checkout -b feat/memory-personalization
   ```

2. **Re-verify line numbers** (this plan was created at commit `9a4921c`; line numbers may have drifted):
   - `maho-storage/src/sqlite.rs:115-324` — execute_batch insertion point
   - `maho-storage/src/sqlite.rs:54` — run_migrations start
   - `maho_core.rs:2335` — SignOut handler
   - `sync_manager.rs:81, :99-101, :135` — push_entity, plaintext fallback, apply_remote_entity
   - `compact-topbar.tsx:51` — settingsGear icon
   - `maho_ai_page_handler.cc:1181` — OpenSettings
   - `maho_settings_page_handler.cc:1174` — SetSetting
   - `domain_panes.tsx` — PasswordsPane / SyncPane patterns

3. **Start with W0 PoCs** in `maho/crates/maho-core/tests/`:
   - `tests/memory_hnsw_poc.rs`
   - `tests/memory_embedding_ios_poc.rs`
   - `tests/memory_extraction_reliability_poc.rs`

4. **Follow W1-W6 sequentially**. Each phase has independent acceptance tests — do not start phase N+1 before phase N passes.

5. **Read this plan top to bottom before starting**. The architecture decisions in §3 are not negotiable lightly — they were made after 5 parallel verification agents and are the foundation of the rest.

---

## 10. References

This plan is grounded in:
- `compact-topbar.tsx`, `maho_ai_page_handler.cc`, `setting_schema.ts`, `panes.ts`, `domain_panes.tsx`, `store.ts`, `maho_settings_page_handler.cc`, `app.tsx` (Settings UI inventory)
- `sqlite.rs`, `maho-storage/Cargo.toml`, `Cargo.lock` (storage layer audit)
- `sync_manager.rs`, `sync_models.rs`, `sync_crypto.rs`, `account_manager.rs`, `account.rs`, `maho_core.rs:2335-5546`, `maho-ffi/src/lib.rs`, `SyncManager.swift`, `AccountSyncSettingsView.swift` (sync system audit)
- `maho-acp/src/{client,events,process,lib}.rs`, `tests/turn_e2e.rs`, `chat_session.rs:206-209` (Rust async patterns)
- `opencode-ai/opencode` Go source (config.go, agent-tool.go) (extraction feasibility)
- fastembed-rs, instant-distance, tract-onnx (embedding stack research)
- mem0, Letta/MemGPT, ChatGPT memory RE, Claude CLAUDE.md, LangGraph, LlamaIndex (memory architecture patterns)

All claims in §2 ("Code-Level Verified Facts") have file:line citations confirmed by direct read. Lines may drift after merges; re-verify on resume.

---

## 11. File-by-File Change Manifest

This section provides exact verbatim before/after diffs for every file the Memory feature will touch. **Line numbers verified at commit `9a4921c` against current dirty working tree.** Re-verify line numbers before applying — they may drift after the in-flight Boost zen-port and AI test-infra work merges.

Files are grouped by layer:
- §11.1 Rust workspace (13 files)
- §11.2 C++ Chromium overlay (12 files)
- §11.3 TypeScript Settings UI (4 files)
- §11.4 macOS shell (3 files)

**Total: 32 files modified or created.**

---

### §11.1 Rust workspace

#### §11.1.1 `maho/crates/maho-storage/Cargo.toml` — MODIFY

**Current state (full file):**
```toml
[dependencies]
maho-types = { path = "../maho-types" }
serde = { workspace = true }
serde_json = { workspace = true }
rusqlite = { version = "0.33", features = ["bundled"] }
heed = "0.20"
thiserror = { workspace = true }
chrono = { version = "0.4", features = ["serde"] }

[dev-dependencies]
tempfile = "3"
```

**Change:** Add one dependency.

**Replacement (add line after `chrono`):**
```toml
chrono = { version = "0.4", features = ["serde"] }
instant-distance = "0.6"
```

**Rationale:** Pure-Rust HNSW for in-memory ANN search over memory embeddings. Zero platform risk (no C/C++ deps, no `.so`, no `.dylib`). Compiles cleanly for macOS, iOS, Android, Linux, Windows.

**Do NOT add:** `sqlite-vec`, `vectorlite`, `usearch` — all blocked by iOS App Store sandbox (`dlopen` prohibition). `load_extension` rusqlite feature must remain disabled.

---

#### §11.1.2 `maho/crates/maho-storage/src/sqlite.rs` — MODIFY

**Current state (relevant ranges):**
- DB open (lines 38–52): `Connection::open(path)` with default flags, no PRAGMAs
- `initialize_schema()` (lines 103–327): single `execute_batch!` of `CREATE TABLE IF NOT EXISTS` + indexes
- `run_migrations()` (lines 54–101): bag of `ALTER TABLE ADD COLUMN` with errors swallowed
- No `PRAGMA user_version` mechanism
- `clear_history()` at line 362, `delete_all_shortcuts()` at line 1431 — pattern templates for new bulk-delete methods

**Change A: Append 5 new tables to `execute_batch!` (insert before line 324, the closing of `execute_batch!`)**

Insert verbatim:
```rust
            -- ──────────────────────────────────────────────────────────────
            -- Memory feature schema (v2 migration)
            -- ──────────────────────────────────────────────────────────────

            CREATE TABLE IF NOT EXISTS memory_blocks (
                id           TEXT PRIMARY KEY,
                label        TEXT UNIQUE NOT NULL,
                content      TEXT NOT NULL,
                tier         TEXT NOT NULL DEFAULT 'hot',
                token_count  INTEGER DEFAULT 0,
                sync_enabled INTEGER DEFAULT 1,
                version      INTEGER DEFAULT 0,
                updated_at   TEXT DEFAULT CURRENT_TIMESTAMP
            );

            CREATE TABLE IF NOT EXISTS memories (
                id            TEXT PRIMARY KEY,
                content       TEXT NOT NULL,
                source_type   TEXT NOT NULL,
                source_id     TEXT,
                categories    TEXT,
                importance    REAL DEFAULT 0.5,
                superseded_by TEXT REFERENCES memories(id),
                embedding     BLOB,
                sync_enabled  INTEGER DEFAULT 1,
                version       INTEGER DEFAULT 0,
                created_at    TEXT DEFAULT CURRENT_TIMESTAMP
            );
            CREATE INDEX IF NOT EXISTS idx_memories_active
                ON memories(superseded_by) WHERE superseded_by IS NULL;

            CREATE VIRTUAL TABLE IF NOT EXISTS memories_fts USING fts5(
                memory_id UNINDEXED,
                content,
                tokenize='porter unicode61'
            );

            CREATE TABLE IF NOT EXISTS conversations (
                id          TEXT PRIMARY KEY,
                session_id  TEXT NOT NULL,
                role        TEXT NOT NULL,
                content     TEXT NOT NULL,
                url_context TEXT,
                created_at  TEXT DEFAULT CURRENT_TIMESTAMP
            );
            CREATE INDEX IF NOT EXISTS idx_conv_session
                ON conversations(session_id, created_at);

            CREATE TABLE IF NOT EXISTS browse_sessions (
                id           TEXT PRIMARY KEY,
                url          TEXT NOT NULL,
                title        TEXT,
                summary      TEXT,
                sync_enabled INTEGER DEFAULT 1,
                visited_at   TEXT DEFAULT CURRENT_TIMESTAMP
            );
            CREATE INDEX IF NOT EXISTS idx_browse_visited
                ON browse_sessions(visited_at DESC);
```

**Change B: Add 12 new public methods to `impl SqliteStorage`**

Insert at end of `impl SqliteStorage { ... }` block (after `delete_all_shortcuts` at line 1431, before the closing `}` of the impl block):

```rust
    // ──────────────────────────────────────────────────────────────
    // Memory feature: memory_blocks operations
    // ──────────────────────────────────────────────────────────────

    pub fn upsert_memory_block(
        &self,
        label: &str,
        content: &str,
        tier: &str,
        token_count: i64,
    ) -> Result<(), StorageError> {
        let id = uuid::Uuid::new_v4().to_string();
        self.conn.execute(
            "INSERT INTO memory_blocks (id, label, content, tier, token_count, version, updated_at)
             VALUES (?1, ?2, ?3, ?4, ?5, 1, CURRENT_TIMESTAMP)
             ON CONFLICT(label) DO UPDATE SET
                content = excluded.content,
                tier = excluded.tier,
                token_count = excluded.token_count,
                version = version + 1,
                updated_at = CURRENT_TIMESTAMP",
            rusqlite::params![id, label, content, tier, token_count],
        )?;
        Ok(())
    }

    pub fn get_memory_block(&self, label: &str) -> Result<Option<String>, StorageError> {
        let mut stmt = self.conn.prepare(
            "SELECT content FROM memory_blocks WHERE label = ?1",
        )?;
        let mut rows = stmt.query(rusqlite::params![label])?;
        if let Some(row) = rows.next()? {
            Ok(Some(row.get(0)?))
        } else {
            Ok(None)
        }
    }

    pub fn list_memory_blocks_by_tier(&self, tier: &str)
        -> Result<Vec<(String, String)>, StorageError>
    {
        let mut stmt = self.conn.prepare(
            "SELECT label, content FROM memory_blocks WHERE tier = ?1 ORDER BY label",
        )?;
        let rows = stmt.query_map(rusqlite::params![tier], |row| {
            Ok((row.get::<_, String>(0)?, row.get::<_, String>(1)?))
        })?;
        rows.collect::<Result<Vec<_>, _>>().map_err(StorageError::from)
    }

    pub fn delete_memory_block(&self, label: &str) -> Result<bool, StorageError> {
        let n = self.conn.execute(
            "DELETE FROM memory_blocks WHERE label = ?1",
            rusqlite::params![label],
        )?;
        Ok(n > 0)
    }

    // ──────────────────────────────────────────────────────────────
    // Memory feature: memories table operations
    // ──────────────────────────────────────────────────────────────

    pub fn insert_memory(
        &self,
        id: &str,
        content: &str,
        source_type: &str,
        source_id: Option<&str>,
        categories: &str,
        importance: f64,
        embedding: Option<&[u8]>,
    ) -> Result<(), StorageError> {
        let tx = self.conn.unchecked_transaction()?;
        tx.execute(
            "INSERT INTO memories
                (id, content, source_type, source_id, categories, importance, embedding, version, created_at)
             VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, 1, CURRENT_TIMESTAMP)",
            rusqlite::params![id, content, source_type, source_id, categories, importance, embedding],
        )?;
        tx.execute(
            "INSERT INTO memories_fts (memory_id, content) VALUES (?1, ?2)",
            rusqlite::params![id, content],
        )?;
        tx.commit()?;
        Ok(())
    }

    pub fn supersede_memory(&self, old_id: &str, new_id: &str) -> Result<(), StorageError> {
        self.conn.execute(
            "UPDATE memories SET superseded_by = ?1, version = version + 1 WHERE id = ?2",
            rusqlite::params![new_id, old_id],
        )?;
        Ok(())
    }

    pub fn load_all_active_memory_embeddings(&self)
        -> Result<Vec<(String, Vec<u8>)>, StorageError>
    {
        let mut stmt = self.conn.prepare(
            "SELECT id, embedding FROM memories
             WHERE superseded_by IS NULL AND embedding IS NOT NULL",
        )?;
        let rows = stmt.query_map([], |row| {
            Ok((row.get::<_, String>(0)?, row.get::<_, Vec<u8>>(1)?))
        })?;
        rows.collect::<Result<Vec<_>, _>>().map_err(StorageError::from)
    }

    pub fn get_memories_by_ids(&self, ids: &[String])
        -> Result<Vec<(String, String, f64)>, StorageError>
    {
        if ids.is_empty() {
            return Ok(Vec::new());
        }
        let placeholders = ids.iter().map(|_| "?").collect::<Vec<_>>().join(",");
        let sql = format!(
            "SELECT id, content, importance FROM memories
             WHERE superseded_by IS NULL AND id IN ({})",
            placeholders
        );
        let mut stmt = self.conn.prepare(&sql)?;
        let params: Vec<&dyn rusqlite::ToSql> =
            ids.iter().map(|s| s as &dyn rusqlite::ToSql).collect();
        let rows = stmt.query_map(params.as_slice(), |row| {
            Ok((
                row.get::<_, String>(0)?,
                row.get::<_, String>(1)?,
                row.get::<_, f64>(2)?,
            ))
        })?;
        rows.collect::<Result<Vec<_>, _>>().map_err(StorageError::from)
    }

    pub fn search_memories_fts(&self, query: &str, limit: usize)
        -> Result<Vec<(String, String, f64)>, StorageError>
    {
        let mut stmt = self.conn.prepare(
            "SELECT m.id, m.content, m.importance
             FROM memories_fts fts JOIN memories m ON fts.memory_id = m.id
             WHERE fts.content MATCH ?1 AND m.superseded_by IS NULL
             ORDER BY rank LIMIT ?2",
        )?;
        let rows = stmt.query_map(rusqlite::params![query, limit as i64], |row| {
            Ok((
                row.get::<_, String>(0)?,
                row.get::<_, String>(1)?,
                row.get::<_, f64>(2)?,
            ))
        })?;
        rows.collect::<Result<Vec<_>, _>>().map_err(StorageError::from)
    }

    pub fn delete_all_memories(&self) -> Result<(), StorageError> {
        let tx = self.conn.unchecked_transaction()?;
        tx.execute("DELETE FROM memories_fts", [])?;
        tx.execute("DELETE FROM memories", [])?;
        tx.execute("DELETE FROM memory_blocks", [])?;
        tx.execute("DELETE FROM conversations", [])?;
        tx.execute("DELETE FROM browse_sessions", [])?;
        tx.commit()?;
        Ok(())
    }

    // ──────────────────────────────────────────────────────────────
    // Memory feature: conversations log
    // ──────────────────────────────────────────────────────────────

    pub fn insert_conversation_turn(
        &self,
        id: &str,
        session_id: &str,
        role: &str,
        content: &str,
        url_context: Option<&str>,
    ) -> Result<(), StorageError> {
        self.conn.execute(
            "INSERT INTO conversations (id, session_id, role, content, url_context)
             VALUES (?1, ?2, ?3, ?4, ?5)",
            rusqlite::params![id, session_id, role, content, url_context],
        )?;
        Ok(())
    }

    pub fn load_conversation_session(&self, session_id: &str)
        -> Result<Vec<(String, String)>, StorageError>
    {
        let mut stmt = self.conn.prepare(
            "SELECT role, content FROM conversations
             WHERE session_id = ?1 ORDER BY created_at",
        )?;
        let rows = stmt.query_map(rusqlite::params![session_id], |row| {
            Ok((row.get::<_, String>(0)?, row.get::<_, String>(1)?))
        })?;
        rows.collect::<Result<Vec<_>, _>>().map_err(StorageError::from)
    }

    // ──────────────────────────────────────────────────────────────
    // Memory feature: browse_sessions log
    // ──────────────────────────────────────────────────────────────

    pub fn insert_browse_session(
        &self,
        id: &str,
        url: &str,
        title: Option<&str>,
    ) -> Result<(), StorageError> {
        self.conn.execute(
            "INSERT INTO browse_sessions (id, url, title) VALUES (?1, ?2, ?3)",
            rusqlite::params![id, url, title],
        )?;
        Ok(())
    }

    pub fn list_browse_sessions_since(&self, since_iso: &str)
        -> Result<Vec<(String, String, Option<String>)>, StorageError>
    {
        let mut stmt = self.conn.prepare(
            "SELECT id, url, title FROM browse_sessions
             WHERE visited_at >= ?1 ORDER BY visited_at DESC",
        )?;
        let rows = stmt.query_map(rusqlite::params![since_iso], |row| {
            Ok((
                row.get::<_, String>(0)?,
                row.get::<_, String>(1)?,
                row.get::<_, Option<String>>(2)?,
            ))
        })?;
        rows.collect::<Result<Vec<_>, _>>().map_err(StorageError::from)
    }
```

**Change C (optional but recommended): Add `PRAGMA user_version` v2 guard**

At top of `run_migrations()` (insert after line 54, the function opening):
```rust
        // v2 migration guard for Memory feature
        let user_version: i64 = self.conn.query_row(
            "PRAGMA user_version", [], |row| row.get(0)
        ).unwrap_or(0);
        if user_version < 2 {
            // Memory tables already created by initialize_schema's IF NOT EXISTS,
            // but bump version so future migrations can branch on it
            self.conn.execute("PRAGMA user_version = 2", [])?;
        }
```

---

#### §11.1.3 `maho/crates/maho-core/Cargo.toml` — MODIFY

**Current state (full file):**
```toml
[package]
name = "maho-core"
version = "0.1.0"
edition = "2021"

[dependencies]
maho-types = { path = "../maho-types" }
maho-storage = { path = "../maho-storage" }
uuid = { workspace = true }
chrono = { workspace = true }
regex = { workspace = true }
serde = { workspace = true }
serde_json = { workspace = true }
base64 = "0.22"
psl = { workspace = true }
adblock = "0.12.2"
aes-gcm = "0.10"
argon2 = "0.5"
rand = "0.8"
bip39 = "2.1"
pbkdf2 = "0.12"
sha1 = "0.10"
hmac = "0.12"
reqwest = { version = "0.12", features = ["json", "stream"] }
tokio = { version = "1", features = ["rt", "rt-multi-thread", "sync"] }
futures-util = "0.3"

[dev-dependencies]
criterion = { version = "0.5", features = ["html_reports"] }
tempfile = "3"
serde_json = { workspace = true }

[[bench]]
name = "core_bench"
harness = false
```

**Change A:** Add `time` and `macros` to tokio features (needed for `tokio::time::interval` and async tests).

Replace line:
```toml
tokio = { version = "1", features = ["rt", "rt-multi-thread", "sync"] }
```
With:
```toml
tokio = { version = "1", features = ["rt", "rt-multi-thread", "sync", "time", "macros"] }
```

**Change B:** Insert Memory feature deps after `futures-util = "0.3"` (line 26):

```toml
futures-util = "0.3"

# Memory feature
instant-distance = "0.6"

[target.'cfg(not(any(target_os = "ios", target_os = "android")))'.dependencies]
fastembed = { version = "5", default-features = false, features = ["hf-hub-rustls-tls"] }

[target.'cfg(any(target_os = "ios", target_os = "android"))'.dependencies]
tract-onnx = "0.21"
tokenizers = { version = "0.22", default-features = false, features = ["onig"] }
```

**Note on `maho-acp`**: Earlier draft proposed `maho-acp = { path = "../maho-acp" }`. The final architecture uses **direct LLM API calls via `reqwest`** (already a dep), bypassing opencode/ACP entirely for fact extraction. So `maho-acp` does NOT need to be added to `maho-core` for the Memory feature.


---

#### §11.1.4 `maho/crates/maho-core/src/lib.rs` — MODIFY

**Current state (full file, 38 lines):**
```rust
pub mod account_manager;
pub mod atc_manager;
pub mod autofill_manager;
pub mod backup_manager;
pub mod bookmark_manager;
pub mod boost_manager;
pub mod command_bar;
pub mod css_mod_manager;
pub mod content_blocker;
pub mod crash_recovery;

pub mod download_manager;
pub mod easel_manager;
pub mod event_dispatcher;
pub mod extension_bridge;
pub mod find_manager;
pub mod import_export_manager;
pub mod llm_manager;
pub mod maho_core;
pub mod mcp_manager;
pub mod note_manager;
pub mod notification_manager;
pub mod password_manager;
pub mod permission_manager;
pub mod profile_manager;
pub mod reading_list_manager;
pub mod settings_manager;
pub mod sharing_manager;
pub mod shortcut_manager;
pub mod skills_manager;
pub mod space_manager;
pub mod sync_crypto;
pub mod sync_manager;
pub mod sync_models;
pub mod tab_lifecycle;
pub mod tab_preview_manager;
pub mod oscrypt;
pub mod chat_session;
```

**Change:** Add 3 new module declarations.

Insert after `pub mod easel_manager;` (alphabetical):
```rust
pub mod easel_manager;
pub mod embedding;
pub mod event_dispatcher;
```

Insert after `pub mod llm_manager;`:
```rust
pub mod llm_manager;
pub mod llm_client;
pub mod maho_core;
```

Insert after `pub mod mcp_manager;`:
```rust
pub mod mcp_manager;
pub mod memory_manager;
pub mod note_manager;
```

---

#### §11.1.5 `maho/crates/maho-core/src/embedding.rs` — CREATE NEW

**Full file content:**
```rust
//! Cross-platform embedding backend for Memory feature.
//!
//! Desktop (macOS/Linux/Windows): fastembed → ONNX Runtime
//! Mobile (iOS/Android): tract-onnx (pure Rust, slower but no C++ deps)
//!
//! Model: BGESmallENV15Q (384-dim INT8, ~6 MB ONNX file).
//! Embeddings are L2-normalized for cosine similarity via dot product.

use crate::error::CoreError;

pub const EMBED_DIM: usize = 384;

#[cfg(not(any(target_os = "ios", target_os = "android")))]
mod backend {
    use super::*;
    use fastembed::{TextEmbedding, InitOptions, EmbeddingModel};
    use std::sync::OnceLock;
    use std::sync::Mutex;

    static MODEL: OnceLock<Mutex<TextEmbedding>> = OnceLock::new();

    pub fn embed(texts: &[String]) -> Result<Vec<Vec<f32>>, CoreError> {
        let cell = MODEL.get_or_init(|| {
            let m = TextEmbedding::try_new(
                InitOptions::new(EmbeddingModel::BGESmallENV15Q)
            ).expect("init embedding model");
            Mutex::new(m)
        });
        let mut model = cell.lock().map_err(|_| CoreError::Embedding("poisoned".into()))?;
        model.embed(texts.to_vec(), None)
            .map_err(|e| CoreError::Embedding(e.to_string()))
    }
}

#[cfg(any(target_os = "ios", target_os = "android"))]
mod backend {
    use super::*;
    // TODO(W3 PoC): tract-onnx + tokenizers implementation.
    // Loads BGESmall ONNX from app bundle resources.
    // Pipeline: tokenize → run inference → mean-pool → L2 normalize → 384-dim Vec<f32>.
    pub fn embed(_texts: &[String]) -> Result<Vec<Vec<f32>>, CoreError> {
        Err(CoreError::Embedding(
            "tract-onnx backend not yet implemented; W3 PoC required".into()
        ))
    }
}

pub use backend::embed;

/// Convert Vec<f32> to little-endian byte representation for SQLite BLOB.
pub fn vec_to_bytes(v: &[f32]) -> Vec<u8> {
    let mut out = Vec::with_capacity(v.len() * 4);
    for f in v {
        out.extend_from_slice(&f.to_le_bytes());
    }
    out
}

/// Convert SQLite BLOB bytes back to Vec<f32>.
pub fn bytes_to_vec(b: &[u8]) -> Result<Vec<f32>, CoreError> {
    if b.len() % 4 != 0 {
        return Err(CoreError::Embedding(format!("blob length {} not multiple of 4", b.len())));
    }
    let mut out = Vec::with_capacity(b.len() / 4);
    for chunk in b.chunks_exact(4) {
        let arr: [u8; 4] = chunk.try_into().unwrap();
        out.push(f32::from_le_bytes(arr));
    }
    Ok(out)
}
```

**Note on `CoreError`**: Add a new variant `Embedding(String)` to the existing error enum (location: search for `pub enum CoreError` in `maho-core` — likely in `src/error.rs` or inline in `maho_core.rs`).

---

#### §11.1.6 `maho/crates/maho-core/src/llm_client.rs` — CREATE NEW

**Full file content:**
```rust
//! Direct LLM API client for Memory feature.
//!
//! Bypasses opencode for deterministic JSON extraction with
//! `response_format: json_object`. Reuses user-configured provider/key/model
//! (read at MemoryManager::set_auth time from C++ prefs).

use serde::Deserialize;
use serde_json::json;
use crate::memory_manager::{AuthSnapshot, RawFact};
use crate::error::CoreError;

const FACT_EXTRACTION_SYSTEM: &str =
    "You extract durable user facts from text. Return ONLY a JSON object with this shape: \
     {\"facts\":[{\"fact\":\"<statement>\",\"category\":\"preference|setting|decision|knowledge\",\"importance\":0.0-1.0}]}. \
     Skip transient or task-specific state. Maximum 5 facts. Empty array if no durable facts.";

const BROWSING_SUMMARY_SYSTEM: &str =
    "You summarize browsing activity into a concise paragraph (≤150 words) describing \
     the user's interests and recent topics. Plain text. No markdown. No headers.";

#[derive(Deserialize)]
struct ChatCompletionResp {
    choices: Vec<Choice>,
}
#[derive(Deserialize)]
struct Choice {
    message: ChatMessage,
}
#[derive(Deserialize)]
struct ChatMessage {
    content: String,
}

#[derive(Deserialize)]
struct FactsEnvelope {
    #[serde(default)]
    facts: Vec<RawFact>,
}

pub async fn extract_facts_json(auth: &AuthSnapshot, text: &str) -> Result<Vec<RawFact>, CoreError> {
    let body = json!({
        "model": auth.model,
        "messages": [
            {"role": "system", "content": FACT_EXTRACTION_SYSTEM},
            {"role": "user",   "content": format!("Extract facts:\n\n{}", text)}
        ],
        "temperature": 0.1,
        "max_tokens": 512,
        "response_format": { "type": "json_object" }
    });

    let url = format!("{}/chat/completions", auth.base_url.trim_end_matches('/'));
    let client = reqwest::Client::new();
    let resp: ChatCompletionResp = client
        .post(&url)
        .bearer_auth(&auth.api_key)
        .json(&body)
        .send().await
        .map_err(|e| CoreError::Network(e.to_string()))?
        .error_for_status()
        .map_err(|e| CoreError::Network(e.to_string()))?
        .json().await
        .map_err(|e| CoreError::Network(e.to_string()))?;

    let content = resp.choices.into_iter().next()
        .ok_or_else(|| CoreError::Parse("no choices".into()))?
        .message.content;

    let env: FactsEnvelope = serde_json::from_str(&content)
        .map_err(|e| CoreError::Parse(format!("json: {e}; raw: {content}")))?;
    Ok(env.facts)
}

pub async fn summarize_browsing(auth: &AuthSnapshot, text: &str) -> Result<String, CoreError> {
    let body = json!({
        "model": auth.model,
        "messages": [
            {"role": "system", "content": BROWSING_SUMMARY_SYSTEM},
            {"role": "user",   "content": format!("Recent activity:\n\n{}", text)}
        ],
        "temperature": 0.2,
        "max_tokens": 256
    });
    let url = format!("{}/chat/completions", auth.base_url.trim_end_matches('/'));
    let client = reqwest::Client::new();
    let resp: ChatCompletionResp = client
        .post(&url)
        .bearer_auth(&auth.api_key)
        .json(&body)
        .send().await
        .map_err(|e| CoreError::Network(e.to_string()))?
        .error_for_status()
        .map_err(|e| CoreError::Network(e.to_string()))?
        .json().await
        .map_err(|e| CoreError::Network(e.to_string()))?;
    Ok(resp.choices.into_iter().next()
        .ok_or_else(|| CoreError::Parse("no choices".into()))?
        .message.content)
}
```

**Provider compatibility note (carried from §3):**
- OpenAI / Mistral / Together / OpenRouter: `response_format: json_object` works as written.
- Anthropic Claude: `response_format` not supported. For Anthropic, drop the `response_format` field and rely on strict prompt + JSON repair pass on parse failure.
- Google Gemini: uses `response_mime_type: application/json` instead. Different endpoint shape entirely.

For v1: detect provider via `auth.provider` string and branch in `extract_facts_json`. For W1, restrict to OpenAI-compatible providers; add Anthropic/Gemini in W3.

---

#### §11.1.7 `maho/crates/maho-core/src/memory_manager.rs` — CREATE NEW

**Full file content:** (See §6.2 of this plan for the complete skeleton — 130 lines, reproduced verbatim there.)

Key design points already covered in §6.2:
- `Embedding(Vec<f32>)` newtype implementing `instant_distance::Point` (cosine distance)
- `MemoryResult` enum with 4 variants (FactsExtracted / EmbeddingsReady / BrowsingSummaryReady / Failed)
- `OnceLock<Runtime>` static via `get_runtime()`
- `MemoryManager` struct with `hnsw`, `result_rx`, `result_tx`, `active_tasks`, `auth`
- Public methods: `new()`, `set_auth()`, `rebuild_index(&storage)`, `search(query, top_k)`, `extract_from_session(...)`, `summarize_browsing(...)`, `embed_fact(...)`, `drain_results()`

---

#### §11.1.8 `maho/crates/maho-core/src/maho_core.rs` — MODIFY

**Current state (relevant ranges):**
- Imports (lines 1–54)
- `MahoCore` struct definition (lines 673–711) — 36 fields
- Constructor `new()` (lines 720–955) — large function with command_bar setup + struct literal init
- `with_storage()` (lines 996–1023)
- `handle_event()` (lines 2183–2986+) — large match with early-return arms before generic dispatch
- `SignOut` arm (lines 2335–2343) — current behavior: `account_manager.sign_out()` only
- `tick()` (lines 3104–3120) — only tab archive work
- `drain_sync_outgoing()` (lines 5477–5483) — pattern template
- `ChatMessage` arm (lines 2896–2903) — chat entry point
- `LlmResult` arm (lines 2905–2970) — chat completion handling
- No persistence of conversation turns to SQLite (gap that W2 closes)

**Change A: Add 3 imports at top of `use crate::*` block** (after line 49, `use crate::tab_preview_manager::TabPreviewManager`):

```rust
use crate::tab_preview_manager::TabPreviewManager;
use crate::memory_manager::{MemoryManager, MemoryResult, AuthSnapshot, RawFact};
```

**Change B: Add field to `MahoCore` struct** (insert after `sharing_manager: SharingManager,` at line 710, before closing `}` at line 711):

```rust
    sharing_manager: SharingManager,
    memory_manager: MemoryManager,
}
```

**Change C: Initialize `memory_manager` in `new()`** (insert after `sharing_manager: SharingManager::new(),` near line 951, before closing `};`):

```rust
        sharing_manager: SharingManager::new(),
        memory_manager: MemoryManager::new(),
    };
```

**Change D: Build HNSW index in `with_storage()`** (insert at end of the `if let Some(ref storage) = self.storage {...}` block around line 1023, before `}` of `if let Some`):

```rust
            // Memory feature: build HNSW from persisted embeddings
            let _ = self.memory_manager.rebuild_index(storage);
        }
```

**Change E: Wire ChatMessage to persist turn + ChatSessionEnded handler**

Replace the existing `ShellEvent::ChatMessage` arm (lines 2896–2903):
```rust
            ShellEvent::ChatMessage {
                ref message,
                ref context,
            } => {
                return vec![self.llm_manager.request_chat_completion(message, context)];
            }
```

With:
```rust
            ShellEvent::ChatMessage {
                ref message,
                ref context,
            } => {
                // Memory: persist user turn for future fact extraction
                if let Some(ref storage) = self.storage {
                    let id = uuid::Uuid::new_v4().to_string();
                    let session_id = context.session_id.clone()
                        .unwrap_or_else(|| "default".to_string());
                    let url_ctx = context.page_url.clone();
                    let _ = storage.insert_conversation_turn(
                        &id, &session_id, "user", message, url_ctx.as_deref(),
                    );
                }
                return vec![self.llm_manager.request_chat_completion(message, context)];
            }
```

(Note: this assumes `ChatRequestContext` already has `session_id: Option<String>` and `page_url: Option<String>`. Verify on resume; if absent, add via §11.1.11 `ChatRequestContext` extension.)

**Change F: Persist assistant turn after `LlmResult`** (insert at start of `LlmResult` arm at line 2905, before the json parse):

```rust
            ShellEvent::LlmResult {
                ref request_id,
                ref result,
            } => {
                // Memory: persist assistant turn
                if let Some(ref storage) = self.storage {
                    if let Some((session_id, content)) =
                        self.llm_manager.lookup_session_for_request(request_id)
                    {
                        let id = uuid::Uuid::new_v4().to_string();
                        let _ = storage.insert_conversation_turn(
                            &id, &session_id, "assistant", &content, None,
                        );
                    }
                }
                // ... existing LlmResult body ...
```

(Note: requires adding `lookup_session_for_request` helper to `LLMManager`. Alternative: pass session_id back through `CoreUpdate::ChatCompletionReady` and persist when shell forwards it. See §11.1.11 for type-level support.)

**Change G: Add `ChatSessionEnded` event handler** (insert as new match arm in `handle_event()`, alongside other early-return arms, near line 2960):

```rust
            ShellEvent::ChatSessionEnded { ref session_id } => {
                if let Some(ref storage) = self.storage {
                    if let Ok(turns) = storage.load_conversation_session(session_id) {
                        let raw_text = turns.iter()
                            .map(|(role, content)| format!("[{}] {}", role, content))
                            .collect::<Vec<_>>()
                            .join("\n");
                        if !raw_text.is_empty() {
                            self.memory_manager.extract_from_session(
                                session_id.clone(), raw_text,
                            );
                        }
                    }
                }
                return vec![];
            }
```

**Change H: Update `SignOut` arm** (replace lines 2335–2343):

Before:
```rust
            ShellEvent::SignOut => {
                self.account_manager.sign_out();
                return vec![
                    CoreUpdate::AccountChanged { account: None },
                    CoreUpdate::AccountAuthStateChanged {
                        auth_state: self.account_manager.get_auth_state(),
                    },
                ];
            }
```

After:
```rust
            ShellEvent::SignOut => {
                self.account_manager.sign_out();
                // Memory: wipe all user-scoped memory data on sign-out
                if let Some(ref storage) = self.storage {
                    let _ = storage.delete_all_memories();
                }
                let _ = self.memory_manager.rebuild_index(
                    self.storage.as_ref().expect("storage absent")
                );
                return vec![
                    CoreUpdate::AccountChanged { account: None },
                    CoreUpdate::AccountAuthStateChanged {
                        auth_state: self.account_manager.get_auth_state(),
                    },
                ];
            }
```

(Note: defensive `.as_ref().expect` is wrong — use `if let Some(ref s) = self.storage { let _ = self.memory_manager.rebuild_index(s); }`. Refactor on application.)

**Change I: Drain memory job results in `tick()`** (insert in `tick()` after line 3119, before `updates`):

Replace:
```rust
        let updates = self.tab_manager.tick_with_settings(archive_timeout, today_timeout);

        let evicted = self.tab_manager.enforce_archive_cap();
        for (tab_id, space_id) in &evicted {
            self.space_manager.remove_tab_from_space(space_id, tab_id);
        }

        updates
    }
```

With:
```rust
        let mut updates = self.tab_manager.tick_with_settings(archive_timeout, today_timeout);

        let evicted = self.tab_manager.enforce_archive_cap();
        for (tab_id, space_id) in &evicted {
            self.space_manager.remove_tab_from_space(space_id, tab_id);
        }

        // Memory: drain background extraction/embedding/summary jobs
        for result in self.memory_manager.drain_results() {
            self.handle_memory_result(result, &mut updates);
        }

        updates
    }
```

**Change J: Add `handle_memory_result` private method** (insert as new method in `impl MahoCore`, near other private helpers):

```rust
    fn handle_memory_result(&mut self, result: MemoryResult, updates: &mut Vec<CoreUpdate>) {
        match result {
            MemoryResult::FactsExtracted { session_id, facts } => {
                if let Some(ref storage) = self.storage {
                    let mut new_ids = Vec::new();
                    for fact in &facts {
                        let id = uuid::Uuid::new_v4().to_string();
                        let categories = serde_json::json!([fact.category]).to_string();
                        let _ = storage.insert_memory(
                            &id, &fact.fact, "chat", Some(&session_id),
                            &categories, fact.importance as f64, None,
                        );
                        // Trigger embedding job
                        self.memory_manager.embed_fact(id.clone(), fact.fact.clone());
                        new_ids.push(id);
                    }
                    updates.push(CoreUpdate::MemoryFactsExtracted {
                        session_id, fact_count: facts.len(),
                    });
                }
            }
            MemoryResult::EmbeddingsReady { fact_id, embedding } => {
                if let Some(ref storage) = self.storage {
                    let bytes = crate::embedding::vec_to_bytes(&embedding);
                    let _ = storage.conn.execute(
                        "UPDATE memories SET embedding = ?1 WHERE id = ?2",
                        rusqlite::params![bytes, fact_id],
                    );
                    let _ = self.memory_manager.rebuild_index(storage);
                    updates.push(CoreUpdate::MemoryUpdated {
                        changed_ids: vec![fact_id],
                    });
                }
            }
            MemoryResult::BrowsingSummaryReady { content } => {
                if let Some(ref storage) = self.storage {
                    let token_count = (content.len() / 4) as i64;
                    let _ = storage.upsert_memory_block(
                        "browsing_summary", &content, "warm", token_count,
                    );
                }
            }
            MemoryResult::Failed { reason } => {
                // Log only; non-fatal. Telemetry hook later.
                eprintln!("memory: job failed: {}", reason);
            }
        }
    }
```

**Change K: Add public API surface for FFI** (insert as new methods in `impl MahoCore`):

```rust
    /// Set Memory auth from the shell (called when AI prefs change)
    pub fn set_memory_auth(&mut self, auth: Option<AuthSnapshot>) {
        self.memory_manager.set_auth(auth);
    }

    /// Get top-K relevant memory ids for a query embedding
    pub fn search_memories(&self, query_embedding: &[f32], top_k: usize) -> Vec<String> {
        self.memory_manager.search(query_embedding, top_k)
    }

    /// Set or replace a hot/warm memory block (used by Personalization UI)
    pub fn set_memory_block(&self, label: &str, content: &str, tier: &str)
        -> Result<(), crate::error::CoreError>
    {
        if let Some(ref storage) = self.storage {
            let token_count = (content.len() / 4) as i64;
            storage.upsert_memory_block(label, content, tier, token_count)
                .map_err(|e| crate::error::CoreError::Storage(e.to_string()))?;
        }
        Ok(())
    }

    pub fn get_memory_block(&self, label: &str) -> Option<String> {
        self.storage.as_ref()?.get_memory_block(label).ok().flatten()
    }

    /// Toggle Memory entity sync per opt-out UX
    pub fn set_entity_sync_enabled(&mut self, type_str: &str, enabled: bool) {
        let entity_type = match type_str {
            "Memory" => crate::sync_models::SyncEntityType::Memory,
            _ => return,
        };
        if let Some(ref mut sm) = self.sync_manager {
            sm.set_entity_sync_enabled(entity_type, enabled);
        }
    }

    /// Hot block compose for system prompt injection
    pub fn build_personalization_system_prompt(&self) -> Option<String> {
        let storage = self.storage.as_ref()?;
        let blocks = storage.list_memory_blocks_by_tier("hot").ok()?;
        if blocks.is_empty() { return None; }
        let mut out = String::new();
        for (label, content) in &blocks {
            if content.trim().is_empty() { continue; }
            out.push_str(&format!("## {}\n{}\n\n", label, content));
        }
        if out.is_empty() { None } else { Some(out) }
    }
```

**Change L: Add `parse_sync_entity_type` Memory arm** (at line ~5533 or wherever the function is):

Before:
```rust
fn parse_sync_entity_type(s: &str) -> SyncEntityType {
    match s {
        "Tab" => SyncEntityType::Tab,
        "Bookmark" => SyncEntityType::Bookmark,
        // ... existing arms ...
        _ => SyncEntityType::Tab,
    }
}
```

After:
```rust
fn parse_sync_entity_type(s: &str) -> SyncEntityType {
    match s {
        "Tab" => SyncEntityType::Tab,
        "Bookmark" => SyncEntityType::Bookmark,
        // ... existing arms ...
        "Memory" => SyncEntityType::Memory,
        _ => SyncEntityType::Tab,
    }
}
```


---

#### §11.1.9 `maho/crates/maho-core/src/sync_models.rs` — MODIFY

**Current state (line 93 area):**
```rust
pub enum SyncEntityType {
    Space,
    Tab,
    Bookmark,
    Note,
    Boost,
    ReadingListItem,
    SearchEngine,
    Shortcut,
}
```

**Change A: Add `Memory` variant.**

After:
```rust
pub enum SyncEntityType {
    Space,
    Tab,
    Bookmark,
    Note,
    Boost,
    ReadingListItem,
    SearchEngine,
    Shortcut,
    Memory,
}
```

**Change B: Add `disabled_entity_types` to `SyncConfig`.**

Locate `SyncConfig` struct (search the file for `pub struct SyncConfig`). Add field:

```rust
use std::collections::HashSet;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SyncConfig {
    pub server_url: String,
    pub sync_key: Option<String>,
    pub device_id: DeviceId,
    pub device_name: String,
    pub auto_sync: bool,
    pub sync_interval_secs: u64,
    #[serde(default)]
    pub disabled_entity_types: HashSet<SyncEntityType>,
}
```

(Adding `#[serde(default)]` keeps existing serialized configs forward-compatible — old persisted configs without this field deserialize with empty HashSet.)

**Change C: Derive `Hash + Eq` on `SyncEntityType`** (required for `HashSet<SyncEntityType>`):

Current derives:
```rust
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
```

Add `Hash`:
```rust
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize, Deserialize)]
```

---

#### §11.1.10 `maho/crates/maho-core/src/sync_manager.rs` — MODIFY

**Current state (relevant lines):**
- Lines 14–25: `SyncManager` struct with private fields (config, status, encryption_key, etc.)
- Lines 68–75: existing `config()` and `update_config(config)`
- Line 81: `push_entity()` — no entity-type guard
- Lines 99–101: plaintext fallback (sends unencrypted if no key set)
- Line 135: `apply_remote_entity()` — no guard

**Change A: Add per-entity-type accessor methods** (insert after line 75, after `update_config`):

```rust
    /// Toggle sync for a specific entity type at runtime
    pub fn set_entity_sync_enabled(&mut self, entity_type: SyncEntityType, enabled: bool) {
        if enabled {
            self.config.disabled_entity_types.remove(&entity_type);
        } else {
            self.config.disabled_entity_types.insert(entity_type);
        }
    }

    pub fn is_entity_sync_enabled(&self, entity_type: &SyncEntityType) -> bool {
        !self.config.disabled_entity_types.contains(entity_type)
    }
```

**Change B: Guard `push_entity()`** (insert at top of `push_entity` function, line 81):

Before:
```rust
    pub fn push_entity(&mut self, entity: SyncEntity) {
        // (existing body)
```

After:
```rust
    pub fn push_entity(&mut self, entity: SyncEntity) {
        // Memory feature: respect per-type opt-out
        if !self.is_entity_sync_enabled(&entity.entity_type) {
            return;
        }
        // (existing body)
```

**Change C: Guard `apply_remote_entity()`** (insert at top of `apply_remote_entity`, line 135):

Before:
```rust
    pub fn apply_remote_entity(&mut self, entity: &SyncEntity) -> bool {
        // (existing body)
```

After:
```rust
    pub fn apply_remote_entity(&mut self, entity: &SyncEntity) -> bool {
        // Memory feature: respect per-type opt-out
        if !self.is_entity_sync_enabled(&entity.entity_type) {
            return false;
        }
        // (existing body)
```

**Change D: Block plaintext fallback for Memory** (lines 99–101).

Current (illustrative — re-verify exact lines):
```rust
        let message = if let Some(key) = &self.encryption_key {
            let plaintext = serde_json::to_vec(&SyncMessage::EntityPush { entity: entity.clone() })
                .unwrap_or_default();
            let ciphertext = crate::sync_crypto::encrypt_update(key, &plaintext)
                .unwrap_or_default();
            SyncMessage::Encrypted { data: ciphertext }
        } else {
            SyncMessage::EntityPush { entity }
        };
```

After:
```rust
        let message = if let Some(key) = &self.encryption_key {
            let plaintext = serde_json::to_vec(&SyncMessage::EntityPush { entity: entity.clone() })
                .unwrap_or_default();
            let ciphertext = crate::sync_crypto::encrypt_update(key, &plaintext)
                .unwrap_or_default();
            SyncMessage::Encrypted { data: ciphertext }
        } else {
            // Memory feature: NEVER send plaintext for sensitive personalization data
            if entity.entity_type == SyncEntityType::Memory {
                eprintln!("sync: refusing to push Memory entity without encryption key");
                return;
            }
            SyncMessage::EntityPush { entity }
        };
```

---

#### §11.1.11 `maho/crates/maho-types/src/events/shell_event.rs` — MODIFY

**Current state:** ~825-line file with `ShellEvent` enum. The `LLM / AI` group ends at line 664 with `UseSkill`.

**Change: Add `ChatSessionEnded` variant** (after `UseSkill` block, before closing `}` of enum at line 665):

```rust
    UseSkill {
        skill_id: String,
        user_input: String,
        #[serde(default)]
        page_context: Option<PageContext>,
    },
    /// Memory feature: signal that a chat session has ended; core triggers
    /// fact extraction on the persisted conversation log.
    ChatSessionEnded {
        session_id: String,
    },
}
```

**Note on `ChatRequestContext`:** If `session_id` and `page_url` are not yet present on `ChatRequestContext` (used in `ChatMessage` event), add them:

Locate `maho/crates/maho-types/src/chat.rs` (or wherever `ChatRequestContext` is defined). Current shape:
```rust
pub struct ChatRequestContext {
    pub page: Option<PageContext>,
    // ...
}
```

Add:
```rust
pub struct ChatRequestContext {
    pub page: Option<PageContext>,
    #[serde(default)]
    pub session_id: Option<String>,
    #[serde(default)]
    pub page_url: Option<String>,
    // ...
}
```

---

#### §11.1.12 `maho/crates/maho-types/src/events/core_update.rs` — MODIFY

**Current state:** ~527-line file with `CoreUpdate` enum. Last variant is `BoostDeleted` at line 440.

**Change: Add 2 new variants** (after `BoostDeleted`, before closing `}` at line 441):

```rust
    BoostDeleted {
        boost_id: BoostId,
        domain: String,
    },
    /// Memory feature: facts were extracted from a chat session
    MemoryFactsExtracted {
        session_id: String,
        fact_count: usize,
    },
    /// Memory feature: stored memories changed (insert/update/delete)
    MemoryUpdated {
        changed_ids: Vec<String>,
    },
}
```

**Note on `MemoryAction`**: A `MemoryAction` enum is already imported in `event_dispatcher.rs:5` (`use maho_types::events::core_update::{CoreUpdate, FolderUpdate, MemoryAction};`). Verify it is for tab memory pressure (likely `MemoryWarning` event), not AI memory. If conflict, rename our new types to `AIMemoryFactsExtracted` / `AIMemoryUpdated` to avoid namespace collision.

---

#### §11.1.13 `maho/crates/maho-ffi/src/lib.rs` — MODIFY

**Current state:** ~3379+ line file. Sync FFI exports between lines 1163–1278. Pattern templates at lines 79–97 (json return) and 326–335 (void setter).

**Change: Add 7 new FFI exports** (insert after the last sync-related export, around line 1418, after `maho_string_free`):

```rust
// ──────────────────────────────────────────────────────────────
// Memory feature
// ──────────────────────────────────────────────────────────────

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `type_str` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_set_entity_sync_enabled(
    ptr: *mut MahoCore,
    type_str: *const c_char,
    enabled: bool,
) {
    if ptr.is_null() || type_str.is_null() {
        return;
    }
    let core = &mut *ptr;
    let s = match CStr::from_ptr(type_str).to_str() {
        Ok(s) => s,
        Err(_) => return,
    };
    core.set_entity_sync_enabled(s, enabled);
}

/// # Safety
/// `ptr` must be valid. `auth_json` may be NULL to clear auth.
#[no_mangle]
pub unsafe extern "C" fn maho_core_set_memory_auth(
    ptr: *mut MahoCore,
    auth_json: *const c_char,
) {
    if ptr.is_null() {
        return;
    }
    let core = &mut *ptr;
    if auth_json.is_null() {
        core.set_memory_auth(None);
        return;
    }
    let s = match CStr::from_ptr(auth_json).to_str() {
        Ok(s) => s,
        Err(_) => return,
    };
    let auth: Option<maho_core::memory_manager::AuthSnapshot> = serde_json::from_str(s).ok();
    core.set_memory_auth(auth);
}

/// # Safety
/// `ptr` must be valid. `label` and `content` must be valid null-terminated C strings.
/// `tier` must be one of "hot" or "warm".
#[no_mangle]
pub unsafe extern "C" fn maho_core_set_memory_block(
    ptr: *mut MahoCore,
    label: *const c_char,
    content: *const c_char,
    tier: *const c_char,
) -> bool {
    if ptr.is_null() || label.is_null() || content.is_null() || tier.is_null() {
        return false;
    }
    let core = &*ptr;
    let label_str = match CStr::from_ptr(label).to_str() { Ok(s) => s, Err(_) => return false };
    let content_str = match CStr::from_ptr(content).to_str() { Ok(s) => s, Err(_) => return false };
    let tier_str = match CStr::from_ptr(tier).to_str() { Ok(s) => s, Err(_) => return false };
    core.set_memory_block(label_str, content_str, tier_str).is_ok()
}

/// # Safety
/// `ptr` must be valid. Caller must free returned string with `maho_string_free`.
/// Returns NULL if block does not exist.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_memory_block(
    ptr: *mut MahoCore,
    label: *const c_char,
) -> *mut c_char {
    if ptr.is_null() || label.is_null() {
        return ptr::null_mut();
    }
    let core = &*ptr;
    let label_str = match CStr::from_ptr(label).to_str() { Ok(s) => s, Err(_) => return ptr::null_mut() };
    match core.get_memory_block(label_str) {
        Some(content) => CString::new(content).map(CString::into_raw).unwrap_or(ptr::null_mut()),
        None => ptr::null_mut(),
    }
}

/// # Safety
/// `ptr` must be valid. Returns JSON: `{"systemPrompt": "..."}` or `{"systemPrompt": null}`.
/// Caller must free with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_build_personalization_prompt(
    ptr: *mut MahoCore,
) -> *mut c_char {
    if ptr.is_null() {
        return ptr::null_mut();
    }
    let core = &*ptr;
    let prompt = core.build_personalization_system_prompt();
    let json = serde_json::json!({ "systemPrompt": prompt });
    to_json_cstring(&json)
}

/// # Safety
/// `ptr` must be valid. `session_id` must be a valid null-terminated C string.
/// Triggers async fact extraction; result delivered via tick → CoreUpdate::MemoryFactsExtracted.
#[no_mangle]
pub unsafe extern "C" fn maho_core_chat_session_ended(
    ptr: *mut MahoCore,
    session_id: *const c_char,
) {
    if ptr.is_null() || session_id.is_null() {
        return;
    }
    let core = &mut *ptr;
    let s = match CStr::from_ptr(session_id).to_str() { Ok(s) => s, Err(_) => return };
    let event = maho_types::events::shell_event::ShellEvent::ChatSessionEnded {
        session_id: s.to_string(),
    };
    core.handle_event(event);
}

/// # Safety
/// `ptr` must be valid. Returns JSON array of memory contents matching FTS query.
/// Caller must free with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_search_memories_fts(
    ptr: *mut MahoCore,
    query: *const c_char,
    limit: usize,
) -> *mut c_char {
    if ptr.is_null() || query.is_null() {
        return ptr::null_mut();
    }
    let core = &*ptr;
    let q = match CStr::from_ptr(query).to_str() { Ok(s) => s, Err(_) => return ptr::null_mut() };
    let storage = match &core.storage_ref() {
        Some(s) => s,
        None => return ptr::null_mut(),
    };
    let results = storage.search_memories_fts(q, limit).unwrap_or_default();
    let json: Vec<serde_json::Value> = results.into_iter().map(|(id, content, importance)| {
        serde_json::json!({"id": id, "content": content, "importance": importance})
    }).collect();
    to_json_cstring(&json)
}
```

**Note: `core.storage_ref()` accessor**: `MahoCore.storage` is private. Add a `pub fn storage_ref(&self) -> Option<&maho_storage::sqlite::SqliteStorage> { self.storage.as_ref() }` to `maho_core.rs`. Or restructure to expose via a getter.


---

### §11.2 C++ Chromium overlay

#### §11.2.1 `maho-chromium/third_party/maho/maho_ffi.h` — MODIFY

**Current state:** 1618-line cbindgen-generated header (manually maintained despite "DO NOT EDIT" comment). Sync exports between lines 1168–1221. End at line 1614 (`#ifdef __cplusplus` close).

**Change:** Insert 7 new C declarations before the closing `#ifdef __cplusplus` (line 1614), grouped in a Memory section. Match the exact `void` / `char*` / `bool` return types from §11.1.13.

**Insert after the last existing FFI declaration (line ~1612), before line 1614:**

```c
// ──────────────────────────────────────────────────────────────
// Memory feature
// ──────────────────────────────────────────────────────────────

/**
 * # Safety
 * `ptr` must be a valid pointer returned by `maho_core_new`.
 * `type_str` must be a valid null-terminated C string.
 */
void maho_core_set_entity_sync_enabled(struct MahoCore *ptr,
                                        const char *type_str,
                                        bool enabled);

/**
 * # Safety
 * `ptr` must be valid. `auth_json` may be NULL to clear auth.
 */
void maho_core_set_memory_auth(struct MahoCore *ptr, const char *auth_json);

/**
 * # Safety
 * `ptr` must be valid. `label`, `content`, `tier` must be valid null-terminated strings.
 */
bool maho_core_set_memory_block(struct MahoCore *ptr,
                                 const char *label,
                                 const char *content,
                                 const char *tier);

/**
 * # Safety
 * `ptr` must be valid. Caller must free returned string with `maho_string_free`.
 * Returns NULL if block does not exist.
 */
char *maho_core_get_memory_block(struct MahoCore *ptr, const char *label);

/**
 * # Safety
 * `ptr` must be valid. Returns JSON: `{"systemPrompt": "..."}` or `{"systemPrompt": null}`.
 * Caller must free with `maho_string_free`.
 */
char *maho_core_build_personalization_prompt(struct MahoCore *ptr);

/**
 * # Safety
 * `ptr` must be valid. `session_id` must be a valid null-terminated C string.
 * Triggers async fact extraction; result delivered via tick → CoreUpdate::MemoryFactsExtracted.
 */
void maho_core_chat_session_ended(struct MahoCore *ptr, const char *session_id);

/**
 * # Safety
 * `ptr` must be valid. Returns JSON array of memory contents matching FTS query.
 * Caller must free with `maho_string_free`.
 */
char *maho_core_search_memories_fts(struct MahoCore *ptr,
                                     const char *query,
                                     size_t limit);
```

---

#### §11.2.2 `maho-chromium/third_party/maho/maho_bridge.h` — MODIFY

**Current state:** 91-line file. `}  // namespace maho::core` at line 89. Last declaration `void InstallLogCallback();` at line 87.

**Change:** Insert Memory wrapper declarations before line 89 (`}  // namespace maho::core`):

```cpp
void InstallLogCallback();

// ──────────────────────────────────────────────────────────────
// Memory feature
// ──────────────────────────────────────────────────────────────
void SetEntitySyncEnabled(MahoCore* core, const char* type_str, bool enabled);
void SetMemoryAuth(MahoCore* core, const std::string& auth_json);
bool SetMemoryBlock(MahoCore* core,
                    const std::string& label,
                    const std::string& content,
                    const std::string& tier);
std::string GetMemoryBlock(MahoCore* core, const std::string& label);
std::string BuildPersonalizationPrompt(MahoCore* core);
void ChatSessionEnded(MahoCore* core, const std::string& session_id);
std::string SearchMemoriesFts(MahoCore* core,
                              const std::string& query,
                              size_t limit);

}  // namespace maho::core
```

---

#### §11.2.3 `maho-chromium/third_party/maho/maho_bridge.cc` — MODIFY

**Current state:** 346 lines. `}  // namespace maho::core` at line 346. Helpers freely use `maho_string_free` for FFI-returned `char*`.

**Change:** Insert Memory wrapper implementations before line 346 (closing namespace):

```cpp
// ──────────────────────────────────────────────────────────────
// Memory feature
// ──────────────────────────────────────────────────────────────

void SetEntitySyncEnabled(MahoCore* core, const char* type_str, bool enabled) {
  if (!core || !type_str) {
    return;
  }
  maho_core_set_entity_sync_enabled(core, type_str, enabled);
}

void SetMemoryAuth(MahoCore* core, const std::string& auth_json) {
  if (!core) {
    return;
  }
  maho_core_set_memory_auth(core, auth_json.empty() ? nullptr : auth_json.c_str());
}

bool SetMemoryBlock(MahoCore* core,
                    const std::string& label,
                    const std::string& content,
                    const std::string& tier) {
  if (!core) {
    return false;
  }
  return maho_core_set_memory_block(core, label.c_str(), content.c_str(), tier.c_str());
}

std::string GetMemoryBlock(MahoCore* core, const std::string& label) {
  if (!core) {
    return std::string();
  }
  char* result = maho_core_get_memory_block(core, label.c_str());
  if (!result) {
    return std::string();
  }
  std::string ret(result);
  maho_string_free(result);
  return ret;
}

std::string BuildPersonalizationPrompt(MahoCore* core) {
  if (!core) {
    return std::string();
  }
  char* json = maho_core_build_personalization_prompt(core);
  if (!json) {
    return std::string();
  }
  std::string ret(json);
  maho_string_free(json);
  // Caller parses {"systemPrompt": "..."} to extract the prompt body.
  return ret;
}

void ChatSessionEnded(MahoCore* core, const std::string& session_id) {
  if (!core) {
    return;
  }
  maho_core_chat_session_ended(core, session_id.c_str());
}

std::string SearchMemoriesFts(MahoCore* core,
                              const std::string& query,
                              size_t limit) {
  if (!core) {
    return "[]";
  }
  char* json = maho_core_search_memories_fts(core, query.c_str(), limit);
  if (!json) {
    return "[]";
  }
  std::string ret(json);
  maho_string_free(json);
  return ret;
}
```

---

#### §11.2.4 `maho/ios-shell/include/maho_ffi.h` — MODIFY

**Current state:** 1432-line file. Same as Chromium copy but missing chat/theme exports. Closing `#ifdef __cplusplus` at line 1428.

**Change:** Add **identical** 7 declarations as in §11.2.1, but using C-style `#include <stdint.h>` / `<stddef.h>` headers (already in file). Insert before line 1428.

The C declarations are byte-identical to §11.2.1. No changes to types or signatures — only the header style differs (C vs C++).

---

#### §11.2.5 `maho/macos-shell/include/maho_ffi.h` — MODIFY

**Current state:** Byte-for-byte identical to ios-shell copy.

**Change:** Add **identical** 7 declarations as in §11.2.4. Insert before line 1428.

---

#### §11.2.6 `maho-chromium/browser/ui/webui/maho_ai_prefs.h` — MODIFY

**Current state (full file, 34 lines):**
```cpp
namespace maho::ai_prefs {

inline constexpr char kProvider[] = "maho.ai.provider";
inline constexpr char kBaseUrl[] = "maho.ai.base_url";
inline constexpr char kApiKey[] = "maho.ai.api_key";
inline constexpr char kModel[] = "maho.ai.model";
inline constexpr char kStructuredContextV2Enabled[] =
    "maho.ai.structured_context_v2_enabled";
inline constexpr char kBrowserToolsV1Enabled[] = "maho.ai.browser_tools_v1_enabled";
inline constexpr char kChatOrchestratorV2Enabled[] =
    "maho.ai.chat_orchestrator_v2_enabled";
inline constexpr char kProviderAdapterV2Enabled[] =
    "maho.ai.provider_adapter_v2_enabled";
inline constexpr char kUiThinModeEnabled[] = "maho.ai.ui_thin_mode_enabled";
inline constexpr char kPendingInitialQuery[] = "maho.ai.pending_initial_query";
inline constexpr char kOpenCodeRuntimeEnabled[] =
    "maho.ai.opencode_runtime_enabled";
inline constexpr char kSessionListJson[] = "maho.ai.session_list_json";
inline constexpr char kApprovalPolicy[] = "maho.ai.approval_policy";
inline constexpr char kSessionPersistenceEnabled[] =
    "maho.ai.session_persistence_enabled";
inline constexpr char kSessionEventLogs[] =
    "maho.ai.session_event_logs";
inline constexpr char kRuntimeCommand[] = "maho.ai.runtime_command";
inline constexpr char kAiViewMode[] = "maho.ai.view_mode";

}  // namespace maho::ai_prefs
```

**Change:** Insert 8 new pref keys after `kAiViewMode` (line 30), before `}  // namespace`:

```cpp
inline constexpr char kAiViewMode[] = "maho.ai.view_mode";

// ──────────────────────────────────────────────────────────────
// Memory / Personalization feature
// ──────────────────────────────────────────────────────────────
inline constexpr char kPersonalizeNewChats[] =
    "maho.ai.personalization.personalize_new_chats";
inline constexpr char kShowSuggestionsNewTab[] =
    "maho.ai.personalization.show_suggestions";
inline constexpr char kMemoryEnabled[] =
    "maho.ai.personalization.memory_enabled";
inline constexpr char kPersonalizationTaste[] =
    "maho.ai.personalization.taste";
inline constexpr char kPersonalizationAboutYou[] =
    "maho.ai.personalization.about_you";
inline constexpr char kPersonalizationWritingStyle[] =
    "maho.ai.personalization.writing_style";
inline constexpr char kPersonalizationCodingPrefs[] =
    "maho.ai.personalization.coding_prefs";
inline constexpr char kSyncMemoryEnabled[] =
    "maho.ai.sync.memory_enabled";

}  // namespace maho::ai_prefs
```

---

#### §11.2.7 `maho-chromium/browser/ui/webui/maho_ai/maho_ai_prefs_registration.cc` — MODIFY

**Current state (full file, 36 lines):** All registrations end at line 33 (`kAiViewMode`).

**Change:** Insert 8 new registrations after line 33, before closing `}` of `RegisterProfilePrefs`:

```cpp
  registry->RegisterStringPref(ai_prefs::kAiViewMode, "sidebar");

  // Memory / Personalization
  registry->RegisterBooleanPref(ai_prefs::kPersonalizeNewChats, true);
  registry->RegisterBooleanPref(ai_prefs::kShowSuggestionsNewTab, false);
  registry->RegisterBooleanPref(ai_prefs::kMemoryEnabled, false);
  registry->RegisterStringPref(ai_prefs::kPersonalizationTaste, std::string());
  registry->RegisterStringPref(ai_prefs::kPersonalizationAboutYou, std::string());
  registry->RegisterStringPref(ai_prefs::kPersonalizationWritingStyle, std::string());
  registry->RegisterStringPref(ai_prefs::kPersonalizationCodingPrefs, std::string());
  registry->RegisterBooleanPref(ai_prefs::kSyncMemoryEnabled, false);
}
```

**Default rationale:**
- `kPersonalizeNewChats = true`: User opts in by default (matches Dia screenshot's default-on toggle).
- `kShowSuggestionsNewTab = false`: New-tab personalization is off by default until user opts in (matches Dia screenshot's default-off toggle).
- `kMemoryEnabled = false`: Memory is opt-in. User must click "Enable Memory" button.
- `kSyncMemoryEnabled = false`: Memory sync requires both Memory enabled AND user explicitly toggles sync. Default off.

---

#### §11.2.8 `maho-chromium/browser/ui/webui/maho_ai/maho_ai_page_handler.cc` — MODIFY

**Current state (lines 1265–1277):** `OpenSettings()` opens `chrome://maho-settings/` with no `?pane=` query.

**Change:** Route to Personalization pane.

Before:
```cpp
void MahoAIPageHandler::OpenSettings() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!browser_) {
    return;
  }
  content::OpenURLParams params(
      GURL("chrome://maho-settings/"),
      content::Referrer(),
      WindowOpenDisposition::SINGLETON_TAB,
      ui::PAGE_TRANSITION_AUTO_TOPLEVEL,
      /*is_renderer_initiated=*/false);
  browser_->OpenURL(params, /*navigation_handle_callback=*/{});
}
```

After:
```cpp
void MahoAIPageHandler::OpenSettings() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!browser_) {
    return;
  }
  content::OpenURLParams params(
      GURL("chrome://maho-settings/?pane=personalization"),
      content::Referrer(),
      WindowOpenDisposition::SINGLETON_TAB,
      ui::PAGE_TRANSITION_AUTO_TOPLEVEL,
      /*is_renderer_initiated=*/false);
  browser_->OpenURL(params, /*navigation_handle_callback=*/{});
}
```

**Note:** TS settings UI must read `?pane=` query param at startup and pre-select that pane. Verify whether existing settings router already supports this. If not, add: in the bootstrap code of `maho-chromium/browser/resources/maho_settings/app.ts` or root component, read `new URLSearchParams(window.location.search).get('pane')` and set initial active pane.

---

#### §11.2.9 `maho-chromium/browser/ui/webui/maho_settings/maho_settings_page_handler.h` — MODIFY

**Change:** No new public API needed (memory is handled via existing `SetSetting` + new `HandleFfiSetting` branches). However, since memory toggle in SyncPane needs to call the FFI bridge `maho::core::SetEntitySyncEnabled`, the existing access pattern via `maho::GetCore()` already works without adding a member.

**No changes to .h needed.**

---

#### §11.2.10 `maho-chromium/browser/ui/webui/maho_settings/maho_settings_page_handler.cc` — MODIFY

**Change A: Add includes** (top of file, after existing maho_ai_prefs include at line 38):

Before:
```cpp
#include "maho/browser/ui/webui/maho_ai_prefs.h"
#include "maho/third_party/maho/maho_ffi.h"
```

After:
```cpp
#include "maho/browser/ui/webui/maho_ai_prefs.h"
#include "maho/third_party/maho/maho_bridge.h"  // For Memory FFI wrappers
#include "maho/third_party/maho/maho_ffi.h"
```

**Change B: Extend `HandleFfiSetting`** (insert before `if (needs_color_reset)` at line ~1390):

After last existing branch (`tabs.pinned_close_behavior`):
```cpp
  } else if (key == "tabs.pinned_close_behavior") {
    base::DictValue settings;
    base::DictValue general;
    general.Set("pinnedCloseBehavior", value);
    settings.Set("general", std::move(general));
    std::string json;
    base::JSONWriter::Write(base::Value(std::move(settings)), &json);
    maho_core_update_settings(core, json.c_str());
    handled = true;
  } else if (key == ai::kPersonalizeNewChats ||
             key == ai::kShowSuggestionsNewTab ||
             key == ai::kMemoryEnabled) {
    // Boolean prefs — write directly to PrefService
    profile_->GetPrefs()->SetBoolean(key, value == "true");
    if (key == ai::kMemoryEnabled) {
      // Reflect to core: memory feature gate (controls extraction trigger)
      // Eventually: core->set_memory_feature_enabled(value == "true");
    }
    handled = true;
  } else if (key == ai::kPersonalizationTaste ||
             key == ai::kPersonalizationAboutYou ||
             key == ai::kPersonalizationWritingStyle ||
             key == ai::kPersonalizationCodingPrefs) {
    // String prefs (free-text textareas)
    profile_->GetPrefs()->SetString(key, value);
    // Compose hot block for system prompt injection (next chat session)
    UpdatePersonalizationHotBlock();
    handled = true;
  } else if (key == ai::kSyncMemoryEnabled) {
    profile_->GetPrefs()->SetBoolean(key, value == "true");
    // Reflect into Rust core
    maho::core::SetEntitySyncEnabled(core, "Memory", value == "true");
    handled = true;
  }
```

**Note**: The literal pref keys (`"maho.ai.personalization.personalize_new_chats"` etc.) come from `maho_ai_prefs.h` constants. Use `ai::kPersonalizeNewChats` etc. (since `namespace ai = maho::ai_prefs;` already aliased at line 47).

**Change C: Extend `BuildSettingsSnapshot` to include 8 new keys** (the `BuildSettingsSnapshot` function around line 1257):

After:
```cpp
std::vector<maho_settings::mojom::SettingValuePtr>
MahoSettingsPageHandler::BuildSettingsSnapshot() {
  std::vector<maho_settings::mojom::SettingValuePtr> settings;
  auto add = [&](const std::string& key) {
    auto s = maho_settings::mojom::SettingValue::New();
    s->key = key;
    s->value = ReadSetting(key);
    settings.push_back(std::move(s));
  };
  for (const auto& m : kSettingsMap) {
    add(m.maho_key);
  }
  add(kPasswordProviderModeMahoKey);
  add(kPasswordsEnabledMahoKey);

  // Memory / Personalization
  add(ai::kPersonalizeNewChats);
  add(ai::kShowSuggestionsNewTab);
  add(ai::kMemoryEnabled);
  add(ai::kPersonalizationTaste);
  add(ai::kPersonalizationAboutYou);
  add(ai::kPersonalizationWritingStyle);
  add(ai::kPersonalizationCodingPrefs);
  add(ai::kSyncMemoryEnabled);

  AppendFfiSettings(settings);
  return settings;
}
```

**Change D: Extend `ReadSetting` helper** (locate the existing `ReadSetting(maho_key)` private method).

The `ReadSetting` method is currently `static const char* GetPrefPath(const std::string& maho_key)` + `ReadSetting(maho_key)`. The 8 new keys ARE the pref paths (already in `maho.ai.*` namespace), so `GetPrefPath` should return them as-is.

Add to `GetPrefPath` (in the `static const char* GetPrefPath(...)` body):
```cpp
  // Personalization keys are direct pref paths
  if (maho_key == ai::kPersonalizeNewChats) return ai::kPersonalizeNewChats;
  if (maho_key == ai::kShowSuggestionsNewTab) return ai::kShowSuggestionsNewTab;
  if (maho_key == ai::kMemoryEnabled) return ai::kMemoryEnabled;
  if (maho_key == ai::kPersonalizationTaste) return ai::kPersonalizationTaste;
  if (maho_key == ai::kPersonalizationAboutYou) return ai::kPersonalizationAboutYou;
  if (maho_key == ai::kPersonalizationWritingStyle) return ai::kPersonalizationWritingStyle;
  if (maho_key == ai::kPersonalizationCodingPrefs) return ai::kPersonalizationCodingPrefs;
  if (maho_key == ai::kSyncMemoryEnabled) return ai::kSyncMemoryEnabled;
```

And `ReadSetting` already handles boolean and string prefs via `prefs_->GetBoolean(path) ? "true" : "false"` and `prefs_->GetString(path)` patterns.

**Change E: Add private helper `UpdatePersonalizationHotBlock`** (new method in handler):

In `.h` (private section):
```cpp
  void UpdatePersonalizationHotBlock();
```

In `.cc`:
```cpp
void MahoSettingsPageHandler::UpdatePersonalizationHotBlock() {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return;
  }
  std::string composed;
  std::string taste = prefs_->GetString(ai::kPersonalizationTaste);
  std::string about = prefs_->GetString(ai::kPersonalizationAboutYou);
  std::string writing = prefs_->GetString(ai::kPersonalizationWritingStyle);
  std::string coding = prefs_->GetString(ai::kPersonalizationCodingPrefs);

  if (!taste.empty())   composed += "## Inspirations & taste\n" + taste + "\n\n";
  if (!about.empty())   composed += "## About the user\n" + about + "\n\n";
  if (!writing.empty()) composed += "## Writing style\n" + writing + "\n\n";
  if (!coding.empty())  composed += "## Coding preferences\n" + coding + "\n\n";

  maho::core::SetMemoryBlock(core, "preferences", composed, "hot");
}
```

**Change F: Watch personalization prefs for change**

In constructor (line 1064–1066, in the existing `for (const auto& m : kSettingsMap)` loop), after the loop, add:

```cpp
  for (const auto& m : kSettingsMap) {
    // ... existing ...
  }

  // Memory / Personalization pref change observers
  static const char* kPersonalizationPrefs[] = {
    ai::kPersonalizeNewChats, ai::kShowSuggestionsNewTab, ai::kMemoryEnabled,
    ai::kPersonalizationTaste, ai::kPersonalizationAboutYou,
    ai::kPersonalizationWritingStyle, ai::kPersonalizationCodingPrefs,
    ai::kSyncMemoryEnabled,
  };
  for (const char* pref : kPersonalizationPrefs) {
    pref_registrar_.Add(pref,
        base::BindRepeating(&MahoSettingsPageHandler::OnPrefChanged,
                            weak_factory_.GetWeakPtr()));
  }
```


---

#### §11.2.11 `maho-chromium/browser/ai/maho_chat_orchestration_service.cc` — MODIFY

**Current state (lines 210–248):** RUST path constructs `MahoChatConfig` with `system_instruction = ""` (line 216, hardcoded empty).

**Change A:** Add include at top of file (after existing maho_bridge.h or maho_core_holder.h includes):

```cpp
#include "maho/browser/ui/webui/maho_ai_prefs.h"
#include "maho/third_party/maho/maho_bridge.h"
#include "components/prefs/pref_service.h"
```

**Change B:** Replace empty `system_instruction = ""` (line 216) with personalization context.

Before:
```cpp
#if defined(MAHO_RUST_AI)
  if (!rust_session_) {
    MahoChatConfig config;
    std::string api_key = GetConfiguredApiKey(prefs_);
    std::string endpoint = ResolveEndpoint(prefs_);
    std::string model = ResolveModel(prefs_);
    std::string system_instruction = "";

    config.api_key = api_key.c_str();
    config.endpoint = endpoint.c_str();
    config.model = model.c_str();
    config.system_instruction = system_instruction.c_str();
```

After:
```cpp
#if defined(MAHO_RUST_AI)
  if (!rust_session_) {
    MahoChatConfig config;
    std::string api_key = GetConfiguredApiKey(prefs_);
    std::string endpoint = ResolveEndpoint(prefs_);
    std::string model = ResolveModel(prefs_);
    std::string system_instruction = BuildMemorySystemInstruction();

    config.api_key = api_key.c_str();
    config.endpoint = endpoint.c_str();
    config.model = model.c_str();
    config.system_instruction = system_instruction.c_str();
```

**Change C:** Add `BuildMemorySystemInstruction` private helper.

In `.h` (`maho_chat_orchestration_service.h`), private section:
```cpp
  std::string BuildMemorySystemInstruction() const;
```

In `.cc`:
```cpp
std::string MahoChatOrchestrationService::BuildMemorySystemInstruction() const {
  if (!prefs_->GetBoolean(maho::ai_prefs::kPersonalizeNewChats)) {
    return std::string();
  }
  MahoCore* core = maho::GetCore();
  if (!core) {
    return std::string();
  }
  std::string raw = maho::core::BuildPersonalizationPrompt(core);
  if (raw.empty()) {
    return std::string();
  }
  // raw is JSON: {"systemPrompt": "...content..."} or {"systemPrompt": null}
  auto parsed = base::JSONReader::Read(raw, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return std::string();
  }
  const std::string* prompt = parsed->GetDict().FindString("systemPrompt");
  if (!prompt) {
    return std::string();
  }
  return *prompt;
}
```

**Change D:** Signal `ChatSessionEnded` to core when sessions complete.

Locate the session-end / `Reset()` method in `maho_chat_orchestration_service.cc`. Add at the end of session-end handling (typically when the assistant's reply is fully streamed):

```cpp
  // Memory: trigger fact extraction on session end
  if (prefs_->GetBoolean(maho::ai_prefs::kMemoryEnabled)) {
    if (MahoCore* core = maho::GetCore()) {
      maho::core::ChatSessionEnded(core, current_session_id_);
    }
  }
```

(Note: requires a `current_session_id_` member tracked across `HandleMessage` calls. If not present, generate one per session lifecycle and pass through `MahoChatConfig` or via a separate session_id parameter to `maho_core_chat_session_new`.)

**Change E (non-RUST path, optional):** Inject memory context into `ShapeRustEvent`'s event payload.

In `ShapeRustEvent` (lines 368–410), add after the other `event.Set(...)` calls:
```cpp
  // Memory: include personalization system prompt if configured
  // (Rust engine assembles final system message)
  std::string memory_prompt;  // would need core access here too
  if (!memory_prompt.empty()) {
    event.Set("memory_system_prompt", memory_prompt);
  }
```

Actual implementation requires passing prefs/core access into the static method or refactoring. Lower priority — RUST path is the primary code path.

---

#### §11.2.12 `maho-chromium/browser/ui/webui/maho_settings/BUILD.gn` — NO CHANGES NEEDED

**Current state (41 lines):** Already includes:
```
"maho_settings_page_handler.cc",
"maho_settings_page_handler.h",
```
And dep `"//maho/third_party/maho:maho_core_prebuilt"`, `"//maho/third_party/maho:maho_ffi_headers"`.

**Why no change:** All Memory feature additions live inside the existing `maho_settings_page_handler.cc/.h` files. The `maho_bridge.h` is already accessible via existing deps. No new source files added, so `sources` list stays as-is.

---

### §11.3 TypeScript Settings UI

#### §11.3.1 `maho-chromium/browser/resources/maho_settings/models.ts` — MODIFY

**Change A:** Extend `contentKind` union to add `'personalization'`.

Locate `export type ContentKind = ...` (or wherever pane content kinds are unioned). Add `'personalization'`:

```typescript
export type ContentKind =
  | 'profiles'
  | 'sync'
  | 'passwords'
  | 'autofill'
  | 'theme-store'
  | 'shortcuts'
  | 'ai'
  | 'content-blocker'
  | 'extensions'
  | 'atc'
  | 'personalization';
```

**Change B (optional):** Add `maxLength?: number` to `SettingMetadata` for textarea char limits.

Before:
```typescript
export interface SettingMetadata {
  label: string;
  description: string;
  control: ControlKind;
  options?: ControlOption[];
  segmented?: boolean;
  placeholder?: string;
  min?: number;
  max?: number;
  step?: number;
}
```

After:
```typescript
export interface SettingMetadata {
  label: string;
  description: string;
  control: ControlKind;
  options?: ControlOption[];
  segmented?: boolean;
  placeholder?: string;
  min?: number;
  max?: number;
  step?: number;
  maxLength?: number;   // NEW: char limit for textarea
}
```

If adding `maxLength`, also update `TextareaControl` in `app.tsx:640-670` to honor it:
```tsx
<TextArea
  ...existing props...
  maxLength={meta.maxLength}
/>
```

---

#### §11.3.2 `maho-chromium/browser/resources/maho_settings/schema/setting_schema.ts` — MODIFY

**Change:** Add 8 new setting definitions to `SETTING_DEFINITIONS` array (append at end of array, before closing `];`):

```typescript
  {
    key: 'maho.ai.personalization.personalize_new_chats',
    metadata: {
      label: 'Personalize new chats',
      description: 'Maho can tailor its responses with Memory and what you teach it here.',
      control: 'toggle',
    },
  },
  {
    key: 'maho.ai.personalization.show_suggestions',
    metadata: {
      label: 'Show suggestions on new tab',
      description: 'Maho will present personalized suggestions using Memory and other tools.',
      control: 'toggle',
    },
  },
  {
    key: 'maho.ai.personalization.memory_enabled',
    metadata: {
      label: 'Personalize responses using Memory',
      description: 'Maho will remember your recent sites, chats, and preferences. Activity is summarized locally and saved on your device.',
      control: 'toggle',
    },
  },
  {
    key: 'maho.ai.personalization.taste',
    metadata: {
      label: 'Who inspires you or shapes your taste?',
      description: 'List people, brands or products that should influence Maho\u2019s taste.',
      control: 'textarea',
      placeholder: 'List people, brands or products that should influence Maho\u2019s taste',
    },
  },
  {
    key: 'maho.ai.personalization.about_you',
    metadata: {
      label: 'What else should Maho know about you?',
      description: 'Background, location, profession, anything that helps the AI respond better.',
      control: 'textarea',
      placeholder: 'I like short answers with bullets. Use analogies. I live in Brooklyn.',
    },
  },
  {
    key: 'maho.ai.personalization.writing_style',
    metadata: {
      label: 'How do you want Maho to write?',
      description: 'Voice and tone preferences.',
      control: 'textarea',
      placeholder: 'Casual and witty, but emails should be concise and professional.',
    },
  },
  {
    key: 'maho.ai.personalization.coding_prefs',
    metadata: {
      label: 'What are your preferences for coding?',
      description: 'Languages, frameworks, formatting, comment style, etc.',
      control: 'textarea',
      placeholder: 'Answer in Swift, provide snippets and JSDoc comments.',
    },
  },
  {
    key: 'maho.ai.sync.memory_enabled',
    metadata: {
      label: 'Sync Memory across devices',
      description: 'When enabled, your Memory will sync to other devices via your recovery phrase. End-to-end encrypted.',
      control: 'toggle',
    },
  },
```

---

#### §11.3.3 `maho-chromium/browser/resources/maho_settings/schema/panes.ts` — MODIFY

**Change:** Add a new pane definition for Personalization in the `productivity` domain (after `maho-ai` pane):

Before (current productivity domain panes):
```typescript
{key: 'shortcuts',     symbol: '⌘',  navTitle: 'Shortcuts',      kind: 'live', domain: 'productivity',  contentKind: 'shortcuts'},
{key: 'extensions',    symbol: 'EX', navTitle: 'Extensions',     kind: 'live', domain: 'productivity',  contentKind: 'extensions'},
{key: 'maho-ai',       symbol: 'AI', navTitle: 'Maho AI',        kind: 'live', domain: 'productivity',  contentKind: 'ai'},
```

After:
```typescript
{key: 'shortcuts',       symbol: '⌘',  navTitle: 'Shortcuts',      kind: 'live', domain: 'productivity',  contentKind: 'shortcuts'},
{key: 'extensions',      symbol: 'EX', navTitle: 'Extensions',     kind: 'live', domain: 'productivity',  contentKind: 'extensions'},
{key: 'maho-ai',         symbol: 'AI', navTitle: 'Maho AI',        kind: 'live', domain: 'productivity',  contentKind: 'ai'},
{key: 'personalization', symbol: 'Pe', navTitle: 'Personalization', kind: 'live', domain: 'productivity',  contentKind: 'personalization'},
```

---

#### §11.3.4 `maho-chromium/browser/resources/maho_settings/react/domain_panes.tsx` — MODIFY

**Change A:** Add `PersonalizationPane` component (full code; see §6.4 of plan for the verbatim component).

Insert as a new top-level function before the `DomainPaneContent` switch component:

```tsx
interface PersonalizationPaneProps {
  pane: PaneDefinition;
  settings: SettingValue[];
  store: MahoSettingsStore;
}

function PersonalizationPane({pane, settings, store}: PersonalizationPaneProps) {
  const personalizeNewChats =
      settings.find(s => s.key === 'maho.ai.personalization.personalize_new_chats')?.value !== 'false';
  const showSuggestions =
      settings.find(s => s.key === 'maho.ai.personalization.show_suggestions')?.value === 'true';
  const memoryEnabled =
      settings.find(s => s.key === 'maho.ai.personalization.memory_enabled')?.value === 'true';

  const TEXTAREA_KEYS = [
    'maho.ai.personalization.taste',
    'maho.ai.personalization.about_you',
    'maho.ai.personalization.writing_style',
    'maho.ai.personalization.coding_prefs',
  ] as const;

  return (
    <DomainPaneSection pane={pane}>
      <SectionHeader title="Personalization" />
      <div className="preferences-card__rows">
        <ManagedSettingRow
          isFirstRow
          title="Personalize new chats"
          description="Maho can tailor its responses with Memory and what you teach it here."
        >
          <AsyncToggleButton
            enabled={personalizeNewChats}
            label="Personalize new chats"
            onToggle={async enabled => {
              await store.commitSettingValue(
                'maho.ai.personalization.personalize_new_chats',
                enabled ? 'true' : 'false'
              );
            }}
          />
        </ManagedSettingRow>
        <ManagedSettingRow
          title="Show suggestions on new tab"
          description="Maho will present personalized suggestions using Memory and other tools."
        >
          <AsyncToggleButton
            enabled={showSuggestions}
            label="Show suggestions on new tab"
            onToggle={async enabled => {
              await store.commitSettingValue(
                'maho.ai.personalization.show_suggestions',
                enabled ? 'true' : 'false'
              );
            }}
          />
        </ManagedSettingRow>
      </div>

      <SectionHeader title="Memory" />
      <div className="preferences-card__rows">
        <ManagedSettingRow
          isFirstRow
          title="Personalize responses using Memory"
          description="Maho will remember your recent sites, chats, and preferences. Activity is summarized locally and saved on your device."
        >
          <AsyncToggleButton
            enabled={memoryEnabled}
            label="Memory"
            onToggle={async enabled => {
              await store.commitSettingValue(
                'maho.ai.personalization.memory_enabled',
                enabled ? 'true' : 'false'
              );
            }}
          />
        </ManagedSettingRow>
      </div>

      <SectionHeader title="Teach Maho about yourself" />
      <div className="preferences-card__rows">
        {TEXTAREA_KEYS.map((key, idx) => {
          const item = settings.find(s => s.key === key) ?? {key, value: ''};
          const meta = SETTING_DEFINITIONS.find(d => d.key === key)?.metadata;
          if (!meta) return null;
          return (
            <ManagedSettingRow
              key={key}
              isFirstRow={idx === 0}
              title={meta.label}
              description={meta.description}
              controlClassName="setting-row__control--stacked"
            >
              <TextareaControl item={item} meta={meta} store={store} />
            </ManagedSettingRow>
          );
        })}
      </div>
    </DomainPaneSection>
  );
}
```

**Change B:** Add Personalization case to `DomainPaneContent` switch.

Locate the switch in `DomainPaneContent` (search for `switch (pane.contentKind)`):

Before (last few cases):
```tsx
    case 'atc':
      return <ATCPane pane={pane} settings={settings} store={store} />;
    default:
      return <LiveSettingsPane pane={pane} settings={settings} store={store} />;
```

After:
```tsx
    case 'atc':
      return <ATCPane pane={pane} settings={settings} store={store} />;
    case 'personalization':
      return <PersonalizationPane pane={pane} settings={settings} store={store} />;
    default:
      return <LiveSettingsPane pane={pane} settings={settings} store={store} />;
```

**Change C:** Add Memory Sync toggle to `SyncPane` (existing component at lines 799–915).

Locate the section that renders when `state.data.status.isSyncing` is true (the "Connected Devices" / "Stop Sync" branch). Insert a Memory Sync row before "Stop Sync":

Before:
```tsx
              <ManagedSettingRow controlClassName="setting-actions" description="Disconnect this browser from sync." title="Stop Sync">
                <ActionButton label="Stop Sync" onClick={async () => {
                  await handler.stopSync();
                  await loadStatus();
                }} />
              </ManagedSettingRow>
```

After:
```tsx
              <MemorySyncToggleRow store={store} />
              <ManagedSettingRow controlClassName="setting-actions" description="Disconnect this browser from sync." title="Stop Sync">
                <ActionButton label="Stop Sync" onClick={async () => {
                  await handler.stopSync();
                  await loadStatus();
                }} />
              </ManagedSettingRow>
```

And add the `MemorySyncToggleRow` helper component near the other helpers in the file:

```tsx
function MemorySyncToggleRow({store}: {store: MahoSettingsStore}) {
  const settings = useSyncExternalStore(
    cb => store.subscribe(cb),
    () => store.getSnapshot().settings
  );
  const enabled =
      settings.find(s => s.key === 'maho.ai.sync.memory_enabled')?.value === 'true';
  return (
    <ManagedSettingRow
      title="Sync Memory across devices"
      description="When enabled, your Memory will sync to other devices via your recovery phrase. End-to-end encrypted."
    >
      <AsyncToggleButton
        enabled={enabled}
        label="Sync Memory"
        onToggle={async next => {
          await store.commitSettingValue(
            'maho.ai.sync.memory_enabled',
            next ? 'true' : 'false'
          );
        }}
      />
    </ManagedSettingRow>
  );
}
```

(Verify exact `useSyncExternalStore` / store subscription pattern used elsewhere in the file. The `domain_panes.tsx` may use a different pattern — copy from the closest existing settings reader.)

---

### §11.4 macOS shell

#### §11.4.1 `maho/macos-shell/Sources/Sync/SyncManager.swift` — MODIFY

**Current state (lines 19–20):** Reads `maho_sync_server_url` and `maho_sync_key` from UserDefaults.

**Change:** Migrate `maho_sync_key` (the seed) to SecureLocalStore. Keep `maho_sync_server_url` (URL is not secret) in UserDefaults.

Before:
```swift
    func startSync() {
        let serverUrl = UserDefaults.standard.string(forKey: "maho_sync_server_url") ?? "https://sync.maho.app"
        let syncKey = UserDefaults.standard.string(forKey: "maho_sync_key") ?? ""
        startSync(serverUrl: serverUrl, syncKey: syncKey)
    }
```

After:
```swift
    func startSync() {
        let serverUrl = UserDefaults.standard.string(forKey: "maho_sync_server_url") ?? "https://sync.maho.app"
        let syncKey = SecureLocalStore.shared.get(for: "sync.recovery_key") ?? ""
        startSync(serverUrl: serverUrl, syncKey: syncKey)
    }
```

---

#### §11.4.2 `maho/macos-shell/Sources/Settings/AccountSyncSettingsView.swift` — MODIFY

**Current state:**
- Line 161: `syncKeyField.stringValue = UserDefaults.standard.string(forKey: "maho_sync_key") ?? ""`
- Line 275: `UserDefaults.standard.set(syncKeyField.stringValue.trimmingCharacters(in: .whitespacesAndNewlines), forKey: "maho_sync_key")`

**Change A — line 161:**
```swift
syncKeyField.stringValue = SecureLocalStore.shared.get(for: "sync.recovery_key") ?? ""
```

**Change B — line 275:**
```swift
SecureLocalStore.shared.save(
    value: syncKeyField.stringValue.trimmingCharacters(in: .whitespacesAndNewlines),
    for: "sync.recovery_key"
)
```

---

#### §11.4.3 `maho/macos-shell/Sources/App/AppDelegate.swift` — MODIFY

**Current state:** No existing migration patterns. `applicationDidFinishLaunching` at line 118.

**Change:** Add a one-time migration that moves `maho_sync_key` from UserDefaults to SecureLocalStore.

Insert at line ~133 (after `createStorageDirectory`, before `MahoBridge.shared.initialize()`):

```swift
        if let path = storagePath {
            createStorageDirectory(at: path, replacingExisting: isUITesting)
        }

        // Memory feature: one-time migration of sync seed from UserDefaults
        // to SecureLocalStore for at-rest encryption.
        Self.migrateSyncKeyToSecureStoreIfNeeded()

        // ... existing MahoBridge.shared.initialize() etc
```

And add the static helper at the end of `AppDelegate`:

```swift
extension AppDelegate {
    fileprivate static func migrateSyncKeyToSecureStoreIfNeeded() {
        let migrationKey = "maho.migration.syncKey.v1"
        guard !UserDefaults.standard.bool(forKey: migrationKey) else { return }

        if let oldKey = UserDefaults.standard.string(forKey: "maho_sync_key"),
           !oldKey.isEmpty {
            SecureLocalStore.shared.save(value: oldKey, for: "sync.recovery_key")
            UserDefaults.standard.removeObject(forKey: "maho_sync_key")
        }
        UserDefaults.standard.set(true, forKey: migrationKey)
    }
}
```

**Note on iOS:** iOS shell uses raw `Security.framework` Keychain. The migration target on iOS is the iOS Keychain, not SecureLocalStore (which doesn't exist on iOS). For iOS, a separate `SyncKeyStore` following `RelayAuthStore` pattern would be added — but iOS shell is **out of scope for this Memory feature implementation** unless explicitly required. Default decision: macOS-first, iOS migration deferred to a follow-up.

---

## §12. Summary Counts

**Total files touched: 32**

| Layer | New | Modified | Total |
|---|---:|---:|---:|
| Rust workspace | 3 | 10 | 13 |
| C++ Chromium overlay | 0 | 12 | 12 |
| TypeScript Settings UI | 0 | 4 | 4 |
| macOS shell | 0 | 3 | 3 |

**New files:**
1. `maho/crates/maho-core/src/embedding.rs`
2. `maho/crates/maho-core/src/llm_client.rs`
3. `maho/crates/maho-core/src/memory_manager.rs`

**No new BUILD.gn entries needed.** Memory feature additions live entirely inside existing `source_set("maho_ai")`, `source_set("maho_settings")`, and existing `maho-storage` / `maho-core` crates.

**Estimated diff size:** ~2500 LOC added (including new files), ~150 lines modified in existing files.

---

## §13. Per-Phase File Touch Map

For each phase in §7, here are the files that get changed:

| Phase | Files |
|---|---|
| **W0 PoCs** | `maho/crates/maho-core/tests/memory_hnsw_poc.rs` (new), `tests/memory_embedding_ios_poc.rs` (new), `tests/memory_extraction_reliability_poc.rs` (new) — does NOT touch production code |
| **W1 — Personalization UI + hot block** | All of §11.2.6, §11.2.7, §11.2.8, §11.2.10 (parts A-F), §11.2.11 (parts A-C), §11.3.1, §11.3.2, §11.3.3, §11.3.4 (part A+B). Plus §11.1.13 (just `maho_core_set_memory_block` + `maho_core_get_memory_block` + `maho_core_build_personalization_prompt` exports), §11.2.1, §11.2.2, §11.2.3 (matching wrappers), §11.2.4, §11.2.5. Plus §11.1.8 changes K (just `set_memory_block`/`get_memory_block`/`build_personalization_system_prompt` methods). And §11.1.2 changes A+B partial (just `memory_blocks` table + 4 block methods). |
| **W2 — Raw log tables + session-end trigger** | §11.1.2 (full schema + remaining methods), §11.1.8 changes E, F, G (conversation persistence + ChatSessionEnded). §11.1.11 (`ChatSessionEnded` shell event). §11.1.13 partial (`maho_core_chat_session_ended` export). §11.2.11 change D (signal session end). |
| **W3 — MemoryManager + extraction + embedding + HNSW** | §11.1.3 (Cargo.toml deps), §11.1.4 (lib.rs mods), §11.1.5 (embedding.rs new), §11.1.6 (llm_client.rs new), §11.1.7 (memory_manager.rs new), §11.1.8 changes B, C, D, J, K (struct field + handle_memory_result + remaining FFI). §11.1.13 remaining exports. |
| **W4 — Context injection + nightly browsing summary** | §11.2.11 changes B, C (BuildMemorySystemInstruction + system_instruction wiring). §11.1.8 add tick-driven nightly summary scheduler. §11.1.2 add `list_browse_sessions_since` usage. |
| **W5 — Memory sync + opt-out** | §11.1.9 (sync_models.rs Memory enum + disabled_entity_types), §11.1.10 (sync_manager.rs guards + plaintext block), §11.1.8 changes H (SignOut wipe) + L (parse_sync_entity_type). §11.1.13 (`maho_core_set_entity_sync_enabled`). §11.2.10 part B last branch (sync.memory_enabled in HandleFfiSetting). §11.3.4 change C (MemorySyncToggleRow). §11.4.1, §11.4.2, §11.4.3 (macOS sync_seed migration to SecureLocalStore). |
| **W6 — Management UI + QA** | New PersonalizationPane button "Manage Memories" → opens modal listing all memories. Adds `maho_core_list_memories` + `maho_core_delete_memory` FFI exports. Otherwise: just UI iteration on §11.3.4 PersonalizationPane and storage methods on §11.1.2. |

---

## §14. Validation Checklist (Per-File Acceptance)

After each W-phase's file touches, verify:

- [ ] **`cargo check --workspace`** passes from `maho/` root
- [ ] **`bun run typecheck`** passes (covers all settings UI TS files)
- [ ] **No `unused imports`** warnings in modified Rust files
- [ ] **No `as any` / `@ts-ignore`** introduced
- [ ] **No new clippy warnings** (`cargo clippy --workspace -- -D warnings` from `maho/`)
- [ ] **iOS / macOS / Chromium `maho_ffi.h` byte-equivalent** for new declarations (mirror manually)
- [ ] **`HandleFfiSetting` symmetric with `AppendFfiSettings`** — every key written must be readable
- [ ] **PrefService keys registered** in `maho_ai_prefs_registration.cc` BEFORE first read attempt
- [ ] **`tokio::test` used for async unit tests** (requires `tokio = "macros"` feature added in §11.1.3)

---

