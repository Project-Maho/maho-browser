# C1 Threading Migration Design

**Status:** Binding spec for Phase 4.3 batches (4.3.1 / 4.3.2 / 4.3.3)
**Date:** 2026-06-28
**Audit reference:** `docs/audits/fork-memory-perf-audit-2026-06-28.md` (finding C1)
**Plan reference:** `docs/plans/memory-perf-remediation-locked-2026-06-28.md` (Phase 4.3)

## Context

The WebUI page handlers currently dispatch ~76 `*OnPool()` helper functions across 8 files onto `base::ThreadPool` via `base::ThreadPool::PostTaskAndReplyWithResult`. Each of those helpers calls `maho::GetCore()` (defined in `maho-chromium/browser/maho_core_holder.cc:18-20`) and then drives FFI calls (`maho_core_*`) against the returned `MahoCore*` from a worker thread of the global pool.

The pointer hand-off is technically race-free, because `g_core` is a `std::atomic<MahoCore*>` with acquire/release ordering (`maho_core_holder.cc:15-24`). But the **referent** is not safe to touch from arbitrary threads. The Rust definition makes this explicit:

- `maho/crates/maho-core/src/maho_core.rs:1-2` imports `std::cell::RefCell` and `std::rc::Rc`.
- `maho/crates/maho-core/src/maho_core.rs:81` declares the manager fan-out as `type UpdateListeners = Rc<RefCell<Vec<Box<dyn Fn(CoreUpdate)>>>>`.

`Rc<T>` is `!Send + !Sync` by definition, and `RefCell<T>` is `!Sync`. The audit (C1) classified this as a hard invariant violation (INV-8: "All FFI access to `MahoCore` must be sequenced on the UI thread"). The current code is only accidentally working because the affected handlers are low-contention settings paths; under load (concurrent settings reads + a background notify + an in-flight write) the race window is real.

## Problem

Two concrete undefined-behaviour scenarios that the current architecture allows:

1. **`RefCell::borrow_mut()` race.** Thread A on the UI sequence enters `MahoCore::process_command` which calls `update_listeners.borrow_mut()`. Thread B on the ThreadPool, dispatched from `BuildProfilesOnPool`, calls `maho_core_list_profiles` which transitively reads `update_listeners.borrow()`. `RefCell`'s borrow counter is a non-atomic `Cell<isize>`; two threads racing on it can both observe a stale counter, both succeed, and produce aliased `&` / `&mut` references. The Rust standard library documents this as undefined behaviour, not a panic.
2. **`Rc::clone()` race.** Any FFI call that walks the manager graph eventually clones an `Rc` (e.g. listener registration, span deferred work). `Rc`'s strong/weak counters are plain `Cell<usize>`. Concurrent clones from two threads can torn-write the counter, leading to either a leak (over-count) or a use-after-free when the count drops to zero prematurely (under-count).

Both bugs are silent. There is no `debug_assertions` panic to catch them and TSan cannot see into the FFI boundary cleanly because the Rust side has no shadow memory annotations for `Rc`/`RefCell` interior state. The only durable fix is to enforce that all FFI calls observe `MahoCore` from a single sequence.

## Target architecture

Add a `PostCoreTask` helper to the `maho` namespace that owns the dedicated MahoCore sequence and posts task/reply pairs through it. The helper replaces every existing `base::ThreadPool::PostTaskAndReplyWithResult(... &FooOnPool ...)` call site.

**Header (`maho-chromium/browser/maho_core_holder.h`):**

```cpp
#ifndef MAHO_BROWSER_MAHO_CORE_HOLDER_H_
#define MAHO_BROWSER_MAHO_CORE_HOLDER_H_

#include "base/functional/callback.h"
#include "base/location.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"

struct MahoCore;

namespace maho {

MahoCore* GetCore();
void SetCore(MahoCore* core);

// Returns the SequencedTaskRunner that owns the MahoCore. All FFI calls
// against the pointer returned by GetCore() MUST be posted to this runner.
// The runner is created lazily on first call and lives for the process.
scoped_refptr<base::SequencedTaskRunner> GetCoreTaskRunner();

// Posts |core_work| to the MahoCore sequence, then hops the result back to
// the calling sequence and invokes |reply|. Modelled on
// base::ThreadPool::PostTaskAndReplyWithResult but routed through the
// MahoCore-affinity runner.
template <typename R>
void PostCoreTask(const base::Location& from_here,
                  base::OnceCallback<R()> core_work,
                  base::OnceCallback<void(R)> reply);

}  // namespace maho

#endif  // MAHO_BROWSER_MAHO_CORE_HOLDER_H_
```

**Implementation sketch (`maho_core_holder.cc`):**

```cpp
scoped_refptr<base::SequencedTaskRunner> GetCoreTaskRunner() {
  static base::NoDestructor<scoped_refptr<base::SequencedTaskRunner>> runner(
      base::ThreadPool::CreateSequencedTaskRunner(
          {base::TaskPriority::USER_VISIBLE, base::MayBlock()}));
  return *runner;
}

template <typename R>
void PostCoreTask(const base::Location& from_here,
                  base::OnceCallback<R()> core_work,
                  base::OnceCallback<void(R)> reply) {
  base::PostTaskAndReplyWithResult(GetCoreTaskRunner().get(), from_here,
                                   std::move(core_work), std::move(reply));
}
```

The runner is a single `SequencedTaskRunner`, so all `core_work` callbacks run one-at-a-time in submission order. That is the sequenced-affinity contract MahoCore needs.

## Migration pattern

Per call site: change one line of dispatch and let the helper preserve the reply.

**Before** (`maho_settings_page_handler.cc:1472-1482`):

```cpp
void MahoSettingsPageHandler::GetProfiles(GetProfilesCallback callback) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(&BuildProfilesOnPool),
      base::BindOnce(
          [](GetProfilesCallback cb,
             std::vector<maho_settings::mojom::ProfileInfoPtr> profiles) {
            std::move(cb).Run(std::move(profiles));
          },
          std::move(callback)));
}
```

**After:**

```cpp
void MahoSettingsPageHandler::GetProfiles(GetProfilesCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::PostCoreTask<std::vector<maho_settings::mojom::ProfileInfoPtr>>(
      FROM_HERE,
      base::BindOnce(&BuildProfilesOnCoreSequence),
      base::BindOnce(
          [](GetProfilesCallback cb,
             std::vector<maho_settings::mojom::ProfileInfoPtr> profiles) {
            std::move(cb).Run(std::move(profiles));
          },
          std::move(callback)));
}
```

The free-function helpers keep their bodies unchanged; rename `*OnPool` to `*OnCoreSequence` so the name reflects the new affinity. No FFI body changes.

## Batch plan

Batches run **serial within a file** (one-at-a-time inside each batch) but **parallel across files** (different translation units; no merge conflicts). The executor agents own one batch each.

- **Batch 1 — Settings.** `maho-chromium/browser/ui/webui/maho_settings/maho_settings_page_handler.cc`. ~40 functions covering profiles, ATC rules, shortcuts, sync, password provider, autofill, filter lists, BYOK, extension toggles. Largest single-file batch.
- **Batch 2 — Boost.** `maho-chromium/browser/ui/webui/maho_boost/maho_boost_page_handler.cc`. ~18 functions covering boost CRUD, scope toggles, install/uninstall, CSS-mod plumbing.
- **Batch 3 — Sync and the long tail.** `maho-chromium/browser/ui/webui/maho_sync/maho_sync_page_handler.cc` plus the remaining handlers flagged by the audit (notes, easel, space-manage, live-folders as audit dictates). ~18 functions.

Total ~76 call sites. Each batch:

1. Rename `*OnPool` helpers to `*OnCoreSequence`.
2. Replace each `base::ThreadPool::PostTaskAndReplyWithResult` dispatch with `maho::PostCoreTask`.
3. Add `DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_)` at the entry of every Mojo interface method.
4. Add `SEQUENCE_CHECKER(sequence_checker_)` member to the class if missing.
5. Run `python3 maho-chromium/build/scripts/build_maho.py --skip-rust --ninja-target unit_tests` and the matching browser_tests target.

## Verification

Per migrated function:

- The first line of every Mojo override is `DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);`.
- The owning class declares `SEQUENCE_CHECKER(sequence_checker_);` as the final member (per Chromium style).
- The reply callback is bound via `base::BindOnce` with `weak_factory_.GetWeakPtr()` whenever the reply touches `this`. The settings handler already has `base::WeakPtrFactory<MahoSettingsPageHandler> weak_factory_{this};` (`maho_settings_page_handler.h:219`). For free-function replies (the dominant pattern), no weak ptr is needed.
- Mojo callbacks hop back to the receiver's sequence automatically because they are `OnceCallback` bound on the UI thread; the `PostTaskAndReplyWithResult` reply runs on the sequence that posted, which is the UI thread for every Mojo entry point.
- Add a focused regression test under `chrome/test/data/webui/maho_settings/` that fires `GetProfiles` while a `process_command` is in flight and asserts no `DCHECK` trips.

## Risks

1. **Reply latency.** Each call adds one extra task-loop iteration relative to the old ThreadPool dispatch (UI → core sequence → UI). For settings pages that hit the FFI on user click, this is below human-perceptible latency (sub-millisecond on local task posting).
2. **Mojo reply ordering.** Mojo guarantees reply callbacks run on the receiver's sequence. The `PostTaskAndReplyWithResult` reply target is the calling sequence, which is the UI sequence for Mojo dispatch. No extra hop is required.
3. **Sequence saturation.** A single sequenced runner serialises all FFI work. The audit confirms the affected handlers are low-volume (settings, sync, boost). If a future hot path needs concurrency it must redesign MahoCore for `Sync`, not bypass this runner.

## Alternatives considered

- **(a) `base::Lock` around `maho::GetCore()`.** REJECTED. A lock would serialise C++ access, but `RefCell::borrow_mut` and `Rc::clone` execute *inside* the FFI call on the Rust side. The Rust types are `!Send`; carrying them across threads is UB regardless of whether the C++ caller holds a mutex. A lock papers over the symptom while keeping the language-level UB.
- **(b) Make `MahoCore` `Sync`.** REJECTED. Wrapping the whole core in `Arc<Mutex<MahoCore>>` would force every manager to be `Send` (currently most hold `Rc<RefCell<...>>` for cheap intra-manager fan-out) and would block the event_dispatcher hot path on a global mutex. The migration cost is ~3000 lines across `maho-core`; the threading payoff is identical to the sequenced runner.
- **(c) Inline FFI on the UI thread directly (no PostTask).** REJECTED. Some `*OnPool` helpers call `maho_core_*` functions that touch LMDB (`MayBlock`). Running them on the UI thread blocks input. The sequenced runner with `base::MayBlock()` is the correct trade.

## Sequencing constraint

Phase 4.3 executes the three batches in parallel because each batch owns a distinct translation unit. Inside a batch, the migrations must be performed and verified one function at a time, so a half-migrated build never lands. The executor agents must:

- Open exactly one batch.
- Convert one Mojo method per commit (or per atomic edit unit) and rebuild after each.
- Not advance to the next function until the current build is green and the regression test passes.

Cross-batch coordination happens at the plan level: batch owners report `done` to the plan agent, which closes Phase 4.3 only when all three are landed and verified.
