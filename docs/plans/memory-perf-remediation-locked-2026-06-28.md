# Maho Fork Memory/Perf Remediation — Locked Execution Plan

**Date:** 2026-06-28
**Status:** Gates resolved, ready to execute.
**Source:** `docs/audits/fork-memory-perf-audit-2026-06-28.md` (49 findings: 7C / 15H / 27M)
**Derivation:** Hyperplan adversarial review (degraded — 2 of 5 members captured) → `plan` agent formalization → user-resolved gates.

## Gate decisions (locked)

| Gate | Decision | Consequence |
|------|----------|-------------|
| **Q-2** | **Burn-rewrite** the C2+C5+H6+C3 cluster | Phase 4 becomes typed-task-graph rewrite; old 3A.3 (C5 patch), 4.3 (C2 promote), 4.4 (H6 docs) collapse into single rewrite track |
| **Q-3** | **Sisyphus owns** FFI/threading czar role | Lead sequences + executes Phase 4 directly; no external owner needed |
| **Q-4** | **Parallel** ASAN baseline | Phase 0.3 runs concurrently with Phase 1; results re-prioritize Phase 2 only |
| **Q-5** | **Hard-fail with allowlist** | CI lints (Phase 0.2) block new violations; allowlist absorbs existing violations that get cleared by Phase 1-3 |

## Execution graph

```
                  ┌─────────────────────────────┐
                  │   Phase 0: CI + Baseline    │
                  └────────┬────────────────────┘
                           │
        ┌─────────────┬────┴─────┬──────────────────┐
        ▼             ▼          ▼                   ▼
   ┌────────┐   ┌────────┐  ┌────────┐        ┌────────────┐
   │ 1A     │   │ 1B     │  │ 1C     │        │ 0.3 ASAN   │
   │ Rust   │   │ C++    │  │ Design │        │ baseline   │
   │ UB     │   │ UAF    │  │ doc    │        │ (parallel) │
   └───┬────┘   └───┬────┘  └───┬────┘        └─────┬──────┘
       │            │            │                    │
       └────────────┴────────────┴────────────────────┘
                                 │
                ┌────────────────┴────────────────┐
                ▼                                 ▼
        ┌───────────────┐                ┌────────────────┐
        │ Phase 2       │                │ Phase 3        │
        │ Hot-path perf │                │ Resource leaks │
        │ (3 streams)   │                │ (C4/H5/H7/H11- │
        │ 2A/2B/2C      │                │  H13 only —    │
        │               │                │  C5 deferred   │
        │               │                │  to Phase 4)   │
        └───────┬───────┘                └────────┬───────┘
                │                                 │
                └────────────────┬────────────────┘
                                 ▼
                  ┌──────────────────────────┐
                  │  Phase 4: Rewrite        │
                  │  (Sisyphus = czar)       │
                  │  - AI adapter rewrite    │
                  │    (C2+C5+H6+H13)        │
                  │  - Sidebar binary proto  │
                  │    (C3)                  │
                  │  - C1 thread migration   │
                  │  - H15 import gating     │
                  │    (already done in 1B.2)│
                  └──────────────────────────┘
```

## Phase 0 — Infrastructure (Day 0, ~3h focused work + 24h ASAN soak)

### 0.1 — Q-1 verification [DONE]
`MahoCore` is `!Send + !Sync` (RefCell + Rc fields). C1 confirmed.

### 0.2 — CI lint scaffold (5 scripts, hard-fail + allowlist)
Same as previous plan. Branch: `fix/memory-perf-ci-lints`.
- `clippy.toml` deny `clippy::unwrap_used` scoped to `maho-ffi/src/import.rs`
- `scripts/ci/check_ffi_safety.sh` — unguarded `pub unsafe extern "C" fn`
- `scripts/ci/check_unretained_cross_thread.sh` — `Unretained` + cross-thread `PostTask`
- `scripts/ci/check_unbounded_channel.sh` — `unbounded_channel` in `maho-core/src/`
- `scripts/ci/check_reqwest_new.sh` — `reqwest::Client::new()` in `maho-core/src/`

Each script accepts a `.ci-allowlist` file containing known violations (cleared progressively by Phase 1-3). New violations always hard-fail.

**Commit:** `chore(ci): add regression lint scripts with allowlist (memory/perf audit)`

### 0.3 — ASAN/LSAN/TSAN baseline (PARALLEL with Phase 1)
| Step | Duration |
|------|----------|
| Build `is_asan=true is_lsan=true` | ~2h |
| Build `is_tsan=true` (separate) | ~2h |
| 30-min navigation soak (50-tab open/close, sidebar, AI panel, Boost) | 30min |
| TSAN reproduction (Settings page, sync flow) | 30min |
| Parse + write `docs/audits/asan-tsan-baseline-2026-06-28.md` | 30min |

**Decision rule:** Findings with 0 ASAN/TSAN hits in 30min soak demote one tier (Critical→High, High→Medium). Safety/UB findings (C6, C7, H1, H15) EXEMPT from demotion. Result feeds into Phase 2 prioritization only — Phase 1 ships unchanged.

**Commit:** `docs(audits): add ASAN/TSAN baseline + Phase 2 re-prioritization`

## Phase 1 — Safety/UB (Days 1-3, parallel)

Unchanged from plan agent's output. All 5 sub-tasks (1A.1 C6, 1A.2 C7, 1B.1 H1, 1B.2 H15, 1C.1 C1 design doc) execute in parallel.

Note: 1B.2 (H15 import gating) is part of the broader Phase 4 czar work but is small + isolated enough to land in Phase 1 standalone — keeping it here.

## Phase 2 — Hot-path Perf (Days 3-5, parallel)

Same 3 streams as before:
- **2A Rust core** — H9, H8, H10, H14 (4 commits, ~2h)
- **2B C++ UI-thread** — H2, H3, M16 (3 commits, ~5h)
- **2C Quick safety** — M25, M26, H4 (3 commits, ~15min)

Reordered after Phase 0.3 ASAN results if any finding demotes.

## Phase 3 — Resource Leaks (Days 5-7, parallel — REDUCED SCOPE)

> **Change due to Q-2 burn-rewrite:** C5 (`user_data_handle_` leak) and H13 (`accumulated_text_`/`full_text` cap) are subsumed by Phase 4 rewrite. They no longer get individual patches.

### Stream 3A — C++ static/observer leaks
- 3A.1 — **C4** BoostWindowController → BrowserUserData
- 3A.2 — **H5** NotificationOverlay BrowserListObserver fallback
- 3A.3 — **H7** FullPageCaptureClient watchdog timer
- ~~3A.4 — C5~~ → **moved to Phase 4 rewrite**

### Stream 3B — Rust resource leaks
- 3B.1 — **H11** Drop for MemoryManager
- 3B.2 — **H12** sync_manager entity_versions LRU
- ~~3B.3 — H13~~ → **moved to Phase 4 rewrite**

## Phase 4 — Rewrite Track (Days 7-21, Sisyphus owns)

> **Major change from previous plan:** C2/C5/H6/H13 collapse into one AI-adapter rewrite. C3 remains its own sidebar protocol rewrite. C1 remains the settings-handler migration.

### 4.1 — AI adapter rewrite (C2 + C5 + H6 + H13)

**Scope:** `MahoUnifiedAgentAdapter` + `MahoBrowserToolExecutor` + `MahoChatOrchestrationService` + `MahoAiLlmClient` accumulation buffers + FFI callback contract.

**New design (typed task-graph):**
```
┌────────────────────────────────────────────────────────┐
│  ConversationTask (owns lifecycle)                     │
│  ├─ task_id (stable across async hops)                 │
│  ├─ state machine (Idle → Streaming → ToolCall → Done) │
│  ├─ bounded_accumulator (2MB cap; backpressure)        │
│  └─ cancellation_token (synchronous cancel guarantee)  │
└────────────────────────────────────────────────────────┘
              │ owns
              ▼
┌────────────────────────────────────────────────────────┐
│  ToolExecutionGraph (owns executor instances)          │
│  ├─ MahoBrowserToolExecutor (member, not stack)        │
│  ├─ FFI handles (cancellable; refcounted user_data)    │
│  └─ deadline_timer (per task, 30s default)             │
└────────────────────────────────────────────────────────┘
              │ uses
              ▼
┌────────────────────────────────────────────────────────┐
│  FfiCallbackHandle<T> (typed wrapper)                  │
│  ├─ Arc<TaskState> (refcounted; cancel atomically)     │
│  ├─ thread_affinity: SequenceBound<T>                  │
│  └─ Drop: cancels FFI before dropping state            │
└────────────────────────────────────────────────────────┘
```

**Tasks:**
1. **Design doc + ADR** → `docs/design/phase4-ai-adapter-rewrite.md` (1 day)
2. **`FfiCallbackHandle<T>` primitive** in `browser/ai/maho_ffi_callback_handle.{h,cc}` with unit tests (2 days)
3. **`ConversationTask`** with state machine + bounded_accumulator (2 days)
4. **`ToolExecutionGraph`** with deadline timer (1 day)
5. **Migrate `MahoUnifiedAgentAdapter`** to new primitives (2 days)
6. **Migrate `MahoChatOrchestrationService`** (2 days)
7. **Migrate `MahoAiLlmClient`** SSE buffer to new accumulator (1 day)
8. **Delete legacy code** + verify ASAN/TSAN clean (1 day)

**Success criteria:**
- All Critical findings C2 + C5 closed
- H6 lifetime contract enforced in TYPE (not just docs) — `FfiCallbackHandle` constructor signature requires the contract
- H13 2MB cap inherited from `ConversationTask.bounded_accumulator`
- Browser tests: `ConversationTaskTest.*`, `FfiCallbackHandleTest.*`, `ToolExecutionGraphTest.*`
- ASAN clean after 100 chat turns with tool use
- No `delete this` patterns remain

**Commits:** ~12 atomic commits on branch `feat/ai-adapter-rewrite`.

### 4.2 — Sidebar binary protocol (C3)

**Scope:** New unified FFI returning binary buffer (or single JSON with base64 favicons) replacing the 3-call pattern.

1. Rust: `maho_core_get_sidebar_state_v2(core, space_id) -> *mut u8` returning a flat binary layout (header + 1 JSON for tree + base64-encoded favicons)
2. C++: rewrite `MahoSidebarStateAdapter` to consume v2
3. Deprecate v1 FFI calls (kept for safety; remove in follow-up after 1 week soak)

**Commits:** 3 commits on branch `feat/sidebar-binary-protocol-v2`.

### 4.3 — C1 settings-handler migration

Unchanged from plan agent's output. 3 batches: settings (~40 fns), boost (~18 fns), sync+other (~18 fns).

**Pre-requisite:** Design doc 1C.1 approved (I author + approve as czar).

**Commits:** 3 batched commits on branch `fix/c1-ui-thread-migration`.

## Commit summary (locked)

| Phase | Branch | Commits |
|-------|--------|---------|
| 0 | `fix/memory-perf-ci-lints` | 1 |
| 0.3 | `docs/asan-tsan-baseline` | 1 |
| 1A | `fix/ffi-safety-ub` | 2 |
| 1B | `fix/cross-thread-safety` | 2 |
| 1C | `docs/c1-threading-design` | 1 |
| 2A | `perf/rust-core-hotpath` | 4 |
| 2B | `perf/cpp-ui-thread` | 3 |
| 2C | `fix/quick-safety-wins` | 3 |
| 3A | `fix/cpp-resource-leaks` | 3 (was 4 — C5 moved) |
| 3B | `fix/rust-resource-leaks` | 2 (was 3 — H13 moved) |
| 4.1 | `feat/ai-adapter-rewrite` | ~12 (was patch-only) |
| 4.2 | `feat/sidebar-binary-protocol-v2` | 3 |
| 4.3 | `fix/c1-ui-thread-migration` | 3 |

**Total:** ~40 commits across 13 branches (was 29 / 10 in patch plan).

## Risks rebalanced after gate decisions

| Risk | Locked mitigation |
|------|-------------------|
| Burn-rewrite blast radius | Phase 4.1 starts AFTER Phase 1-3 stabilize; legacy code stays compiled until ASAN-clean verification |
| Sisyphus-as-czar bandwidth | Plan agent + sub-agent delegation; user approves design docs |
| ASAN baseline delays Phase 1 | Resolved — parallel execution |
| CI lint blocks Phase 1 development | Allowlist absorbs Phase 0.2 → Phase 3 cleanup; Phase 4 enforces all |

## Open items (none blocking — start Phase 0 now)

- Design doc 4.1 (AI adapter rewrite) writes during Phase 1
- Design doc 4.2 (sidebar v2 protocol) writes during Phase 2
- Design doc 1C.1 (C1 migration) writes during Phase 0

## Kickoff

Ready to start Phase 0.2 (CI scaffolding) + Phase 0.3 (ASAN build) on user "go".
