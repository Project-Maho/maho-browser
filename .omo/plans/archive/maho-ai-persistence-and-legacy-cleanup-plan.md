# Maho AI: Persistence Performance + Legacy Chat Cleanup

**Status:** New plan, written 2026-05-23. Builds on the live findings from the
ACP-migration session.

## 1. Objective

Two independent but adjacent cleanups in `maho_ai_page_handler.cc`:

1. **Persistence performance.** `PersistEventLog` and `PersistSessionList`
   currently `base::JSONWriter::Write` on the main thread before handing the
   result to `PrefService`. Disk I/O itself is already async via
   `JsonPrefStore::ScheduleWrite`, but the CPU-bound serialization stays on
   the main thread and the persisted session graph keeps growing forever
   (no LRU cap on `session_event_logs`).
2. **Legacy chat path removal.** `SendMessage`, `MahoLegacyChatAdapter`,
   `chat_history_`, `kMaxHistory`, and the `ChatMessage` history Mojo plumbing
   are dead code — React calls `submitPrompt` exclusively, no caller invokes
   `sendMessage` anywhere in tree. Keeping them around adds maintenance noise
   and confuses the runtime model.

## 2. Source of truth

This file. Supersedes/extends:
- `.sisyphus/plans/maho-ai-acp-migration-plan.md` — ACP migration is the
  baseline this plan operates on top of.

## 3. Verified facts

| Fact | Evidence |
|---|---|
| `kMaxPersistedEvents = 500`, `kMaxHistory = 200` | `maho_ai_page_handler.cc:31-32` |
| `PersistEventLog` serializes the last 500 events synchronously on main thread | `maho_ai_page_handler.cc:1120-1140` (`base::JSONWriter::Write` then `ScopedDictPrefUpdate`) |
| `PersistSessionList` does the same for session metadata | same file, similar pattern |
| `prefs_->SetString` / dict updates are in-memory only; disk write is `JsonPrefStore::ScheduleWrite` on `file_task_runner_` (BG) | `chromium/src/components/prefs/json_pref_store.h` |
| `SendMessage` exists and is wired into mojom + page handler + `MahoLegacyChatAdapter`, but the React layer never calls it | `grep -r 'sendMessage'` over `maho-chromium/browser/resources/maho_ai/` returns only the d.ts type definition — zero call sites |
| `chat_history_` is a separate `std::vector<ChatMessagePtr>` capped at 200 — distinct from `session_event_log_` (the 500-cap one) | `maho_ai_page_handler.cc:31, 314, 318, 328-396, 984` |
| `MahoLegacyChatAdapter` is built in `BUILD.gn` and selected by `MahoAiRuntimeRouter` only when the runtime adapter for OpenCode-ACP is unavailable — i.e. effectively never on current default builds | `maho_ai_runtime_router.cc`, `BUILD.gn` |

## 4. Non-goals

1. Do **not** migrate `session_event_logs` to LevelDB / SQLite. The PrefService
   model (in-memory dict + background JSON write) is appropriate for this
   data scale; the actual fix is moving serialization off the main thread and
   capping retention. SQLite migration would be schema design + migration
   plumbing for negative ROI at current scale.
2. Do **not** introduce a new on-disk store. Reuse `PrefService` for session
   metadata; reuse the existing dict-of-string scheme for event logs.
3. Do **not** touch the ACP adapter, OpenCode runtime, or any AI-behavior
   logic in this plan. This is a pure local-storage + dead-code cleanup.
4. Do **not** repurpose the freed Mojo enum slot from `SendMessage` removal.
   Leave the gap; mojom enums are stable.

## 5. Phased migration

Each phase is independently shippable. Order is the recommended execution
order; later phases assume earlier phases landed.

### Phase 1 — Legacy chat path removal

**Goal:** Delete `SendMessage`, `MahoLegacyChatAdapter`, `chat_history_`,
`kMaxHistory`, and any single-purpose helpers that only the legacy path
used. Keep everything that the new ACP path also touches (e.g. session
status, persisted event log).

**Atomic steps:**

1. Confirm zero callers of `sendMessage` / `SendMessage` across the whole
   workspace one more time (TypeScript + C++ + tests). If any call exists,
   migrate it to `submitPrompt` first.
2. In `maho-chromium/browser/ai/`:
   - Delete `maho_legacy_chat_adapter.cc` and `maho_legacy_chat_adapter.h`.
3. In `maho-chromium/browser/BUILD.gn`:
   - Drop the `source_set("maho_legacy_chat_adapter")` block.
   - Drop `:maho_legacy_chat_adapter` from `:maho_ai_runtime_adapter`'s
     `deps` list.
4. In `maho-chromium/browser/ai/maho_ai_runtime_router.cc`:
   - Drop the include.
   - Drop the fallback construction (the router becomes ACP-only — same
     pattern Phase 7 used for the HTTP adapter).
5. In `maho-chromium/browser/ui/webui/maho_ai/maho_ai_page_handler.cc`/`.h`:
   - Remove `SendMessage` method (declaration + implementation).
   - Remove `chat_history_` member, `kMaxHistory`, every push/erase/clone
     against it, and any helper that exists only to populate it.
   - Verify `clearHistory()` still has a useful body (it currently clears
     `chat_history_` AND calls `adapter->Reset()`); after removal it
     becomes adapter-only.
6. In `maho-chromium/browser/ui/webui/maho_ai/maho_ai.mojom`:
   - Remove `SendMessage(ChatIntent intent, string message);`.
   - Decide for `ChatMessage` struct: if no other Mojo method references
     it, remove. Otherwise keep.
7. Regenerate Mojo bindings via the build (no manual edits to generated
   files).
8. Run `cargo test -p maho-acp -p maho-ffi` (no Rust changes, but ensure
   nothing transitively broke).
9. Run `python3 maho-chromium/build/scripts/build_maho.py`. If GN regen
   fails on dangling refs introduced by these deletions, follow the same
   pattern Phase 7 used (drop refs from `chrome/test/BUILD.gn` only via
   the existing `apply_chromium_src_overrides.py`, not by hand-editing
   upstream).

**Exit criteria:**
- `grep -r 'MahoLegacyChatAdapter\|SendMessage\|chat_history_\|kMaxHistory'`
  over `maho-chromium/` returns zero hits except in this plan file.
- React panel continues to work (no behavioral change since it never used
  the legacy path).
- `lsp_diagnostics` clean on every changed file.

**Rollback:** revert the commit. Legacy path was inert, so no data loss
risk.

### Phase 2 — Off-main-thread serialization

**Goal:** Move `base::JSONWriter::Write` calls in `PersistEventLog` and
`PersistSessionList` onto a background thread. The `prefs_->SetString` /
`ScopedDictPrefUpdate` step stays on the main sequence (PrefService isn't
thread-safe for writes), but it now receives an already-serialized string,
so its main-thread cost drops from "serialize 500 events" to "store one
string in a dict" (sub-millisecond).

**Atomic steps:**

1. In `maho_ai_page_handler.cc`:
   - Replace the body of `PersistEventLog(const std::string& session_id)`
     with:
     - Pre-conditions check (existing).
     - Build a `base::ListValue` from the relevant slice of
       `session_event_log_` (this part is fast — no JSON yet).
     - `base::ThreadPool::PostTaskAndReplyWithResult(FROM_HERE,
        {base::TaskPriority::BEST_EFFORT, base::MayBlock()},
        base::BindOnce(&SerializeListToJson, std::move(list)),
        base::BindOnce(&MahoAIPageHandler::OnEventLogSerialized,
                       weak_factory_.GetWeakPtr(), session_id))`.
     - Where `SerializeListToJson(base::ListValue list) -> std::string`
       calls `base::JSONWriter::Write` on a background thread.
     - Where `OnEventLogSerialized(session_id, json)` does the
       `ScopedDictPrefUpdate` on the main sequence with
       `DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_)`.
   - Apply the same pattern to `PersistSessionList`.
2. Add a tiny inflight-tracker per session id: if `PersistEventLog` is
   called again while a previous serialization is still in flight, skip
   (the in-flight one is stale anyway, the next one will write the latest
   state). Or: cancel the previous via `WeakPtr` invalidation. The latter
   is cleaner.
3. Add a 250ms debounce wrapper: rapid event bursts (e.g. streaming token
   chunks where each one ticks Persist) collapse to one disk write.
   Implementation: `base::OneShotTimer` reset on each `RequestPersist()`
   call; on fire, the actual `PersistEventLog` is called.

**Exit criteria:**
- `PersistEventLog` and `PersistSessionList` no longer call
  `base::JSONWriter::Write` on the main sequence (verifiable via grep
  for the call within those functions).
- Manual stress test: run a session with rapid streaming, observe no main-
  thread jank (use Chromium's `chrome://tracing` or simply note no UI
  freezes).
- All persisted state still loads correctly on restart (round-trip tested
  by submitting a prompt, restarting Maho, and verifying the prior
  session+events appear).

**Rollback:** revert; in-memory state and pref schema are unchanged.

### Phase 3 — Session-list LRU + lazy event loading

**Goal:** Bound the unbounded growth of `session_event_logs` (one dict
entry per ever-seen session), and stop loading all event logs at startup.

**Atomic steps:**

1. Add `kMaxPersistedSessions = 100` (or similar, tuned) constant near
   `kMaxPersistedEvents`.
2. In `PersistSessionList`, after building the new list, evict entries
   beyond the cap (oldest by `created_at` first). Also evict the
   corresponding entries from the `kSessionEventLogs` dict to free the
   bigger blob storage.
3. In `LoadPersistedSessions`, only populate session metadata (the small
   list). Do **not** call `LoadPersistedEventLog` for every session.
4. Add an explicit `OpenSession(session_id)` API that lazily loads
   `LoadPersistedEventLog(session_id)` on first access. Update React
   accordingly: when user clicks a session in the list, call
   `OpenSession`; until then, just show the metadata.
5. Add a one-time migration on first boot of the new code: walk the
   existing `kSessionEventLogs` dict, drop entries whose `session_id` is
   no longer in `kSessionListJson` (orphan cleanup).

**Exit criteria:**
- `kSessionEventLogs` size on disk stops growing without bound.
- Boot time `LoadPersistedSessions` does no JSON parsing of event logs
  (only the list).
- Opening an old session still shows the persisted events.

**Rollback:** revert; orphan cleanup is idempotent and safe to run again.

### Phase 4 — (Optional / contingent) Move event logs out of PrefService

**Goal:** Only if Phase 2+3 prove insufficient under real workloads
(measurable jank or boot-time regression in the wild), migrate
`session_event_logs` to a dedicated on-disk store (LevelDB or a tiny
SQLite via `sql::Database`).

**Trigger criteria:**
- `chrome://tracing` shows main-thread JSON parse > 50ms during boot, or
- `chrome://maho-ai` jank correlated with PersistEventLog calls in the
  trace.

**Sketch (do not pre-implement):**
- Per-profile `MahoAiSessionStore` keyed off the profile path.
- Schema: `(session_id TEXT PRIMARY KEY, created_at REAL, status INTEGER,
  event_blob BLOB)` — one row per session, BLOB is the serialized event
  list (or use a separate events table for finer-grain updates).
- Migration from the existing `kSessionEventLogs` pref dict on first use,
  then delete the pref dict.

This phase is **explicitly deferred** until evidence forces it.

## 6. Risks and mitigations

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| Phase 1 deletion breaks an unseen caller | Low | Medium | Pre-flight grep + LSP diagnostics; revert is one command. |
| Phase 2 introduces a races where concurrent serializations write stale state | Medium | Low | Debounce + WeakPtr invalidation; the only consumer of the persisted state is restart-time load, so worst case is "second-most-recent state persisted" which is acceptable. |
| Phase 3 LRU eviction loses sessions the user wanted | Low | Medium | Cap is generous (100); evict oldest by `created_at`; UI surfaces the cap if approached. |
| Mojo IDL change in Phase 1 (removing `SendMessage`) breaks tools that introspect generated bindings | Low | Low | Check that React/TypeScript bindings build clean; no external consumers. |

## 7. Verification strategy

1. **Phase 1 unit:** existing `maho_ai_runtime_router_unittests` continues
   to pass (it currently asserts `opencode-acp` adapter unconditionally).
2. **Phase 2 manual:** start a long streaming-token prompt, verify UI
   smoothness; capture a `chrome://tracing` trace and confirm
   `JSONWriter::Write` is on a `ThreadPool` thread, not the main UI thread.
3. **Phase 3 manual:** create > 100 sessions in a test profile, restart
   Maho, confirm boot is fast and only `kMaxPersistedSessions` worth
   appear in the sidebar.
4. **Round-trip:** submit a prompt → close panel → reopen panel → the
   message and assistant reply still render from persisted state (Phase 2
   and 3 must not break replay).

## 8. Recommended execution order

```
Phase 1 — Legacy chat removal              ← lowest risk, immediate value
Phase 2 — Off-main-thread serialization    ← depends on 1 only for cleaner diff
Phase 3 — LRU + lazy event loading         ← independent of 2; either order works
Phase 4 — (deferred) external store        ← only if 2+3 prove insufficient
```

Phase 1 is the smallest and lands first. Phases 2 and 3 are independent and
can be parallelized across two implementation passes.

## 9. Honesty statement

This plan rejects the previous SQLite/LevelDB recommendation. The previous
analyst correctly identified pain points but mis-attributed them: PrefService
disk I/O is already async, the actual main-thread cost is the
`base::JSONWriter::Write` call (which Phase 2 fixes), and the ever-growing
state is the eviction story (which Phase 3 fixes). A new on-disk store is
not warranted at current scale and would introduce schema/migration
plumbing for no measurable user-facing improvement.
