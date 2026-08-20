# Shortcut Registry Contract

**Status:** Active · **Owner:** `browser/ui/views/command/` + `maho-core` shortcut manager
**Enforcement:** `maho_shortcut_contract_unittest.cc` (C++) · `contract_tests` in `shortcut_manager.rs` (Rust)

## Why this document exists

On 2026-08-20 four keyboard shortcuts were found dead or misrouted (Ctrl+Tab MRU
switcher, Ctrl+Shift+\ swap split, Cmd+Plus zoom, Ctrl+N ghost binding). None were
new code. All four were **synchronization failures between three lists that no
test or type system connects**. This document is the contract those lists must
hold, and the tests that enforce it.

## The three lists

Every Maho keyboard shortcut exists in exactly three places. None can be derived
from another at compile time.

| # | List | Location | Role |
|---|------|----------|------|
| 1 | Rust registry | `maho/crates/maho-core/src/shortcut_manager.rs` (`register_default_bindings`) | Single source of truth: action id, label, category, key combo, enabled flag. Persisted user rebinding lands here too. |
| 2 | C++ dispatch chain | `maho-chromium/browser/ui/views/command/maho_command_action_handler.cc` (`ExecuteCommandAction` / `ExecutePrivateCommandAction`) | Maps an action id string to browser behavior. Ends in an "Unknown shortcut action" log-and-drop `else`. |
| 3 | C++ key codec | `maho-chromium/browser/ui/views/command/maho_shortcut_interceptor.cc` (`StringToVKey` / `VKeyToString`) | Translates registry key strings (`"tab"`, `"\\"`, `"+"`, `"f2"`, …) to `ui::KeyboardCode` and back. |

A fourth participant — `MahoShortcutInterceptor` (same file as list 3) — resolves
raw key events against list 1 via FFI (`maho_core_resolve_shortcut`) and hands the
resulting action id to list 2.

## Dispatch flow and the swallowing hazard

Maho shortcuts reach the interceptor through three BrowserView entry points
(pre-renderer key hook, Views key handler, FocusManager accelerator). The full
story is in the header comment of `maho_shortcut_interceptor.h`. The critical
property for this contract:

> Once the interceptor resolves a key event to a registry action, it dispatches
> `ExecuteCommandAction(action)` and reports the event **handled**. If list 2 has
> no branch for that action, the event dies right there — Chromium's native
> accelerator for the same physical key never runs.

That is why an unhandled registry action is worse than a no-op: it also
**suppresses the fallback**. This is exactly how Ctrl+Tab broke on 2026-07-23
(commit `841cf8489258`, "register all shortcuts as FocusManager accelerators"):
the Rust registry carried `mru_tab_switch_next` (added 07-05), the new
all-accelerators dispatch routed Ctrl+Tab into `ExecuteCommandAction`, and the
chain had no case for it — while the correct MRU path
(`IDC_SELECT_NEXT_TAB` → `BrowserView::MaybeHandleMruTabSwitch`) sat unreachable.

## The contract

1. **Every registry action must be handled.** For each binding in list 1 —
   enabled or not, since users can re-enable any binding in Settings — list 2
   must have a matching `action_id == "..."` branch (or an
   `action_id.starts_with("...")` prefix branch for generated ids such as
   `select_tab_7`).
2. **Every registry key must round-trip the codec.** For each binding key `k` in
   list 1: `StringToVKey(k) != VKEY_UNKNOWN` and `VKeyToString(StringToVKey(k)) == k`.
   A key that fails this can never become a FocusManager accelerator and can
   never be resolved from a key event.
3. **No duplicate normalized combos.** Two enabled bindings with the same
   normalized combo: the first in registration order wins; the other is
   unreachable. (Note `primary_modifier()` is Meta on macOS, Ctrl on Windows/Linux —
   check collisions on both platforms when adding bindings.)
4. **No ghost bindings.** A registry binding with no real feature behind it is
   removed, not shipped disabled. Ghosts swallow their key combo (Ctrl+N's ghost
   `new_note` did exactly that on macOS) and rot into future regressions. The
   2026-08-20 audit removed `new_note`; if a Notes feature ships, its binding
   returns together with its handler branch.

## Enforcement

| Test | Side | Catches |
|------|------|---------|
| `MahoShortcutContractTest.EveryRegistryKeyRoundTripsThroughCodec` | C++ (`unit_tests`) | Rule 2 against the live registry (reads it from a fresh `maho_core_new()` via FFI). This is the authoritative check — it sees exactly what the shipped binary sees. |
| `MahoShortcutContractTest.EveryRegistryActionIsHandled` | C++ (`unit_tests`) | Rule 1. Scans the dispatch chain source (through the `chromium/src/maho` symlink) for `action_id ==` / `starts_with(` literals and diffs against the live registry. Sentinels (`new_tab`, `mru_tab_switch_next`, `select_tab_` …) make a scanner-blind refactor fail loudly instead of passing vacuously. |
| `contract_tests::all_binding_keys_are_in_cpp_codec_alphabet` | Rust (`cargo test -p maho-core`) | Rule 2 at the fast feedback loop — fails before the Chromium build. The alphabet constant mirrors the C++ codec by hand; keep in sync when the codec grows. |
| `contract_tests::no_duplicate_normalized_combos` | Rust | Rule 3 on the host platform. |
| `contract_tests::binding_actions_are_unique_and_nonempty` | Rust | Registry self-consistency. |

Known blind spot: the C++ handler-coverage test reads the source file, so it
verifies what the chain *matches*, not what the matched branch *does*. Behavioral
correctness of each branch stays with the existing per-feature tests.

## Checklist: adding or changing a shortcut

1. **Registry** — add/rebind in `register_default_bindings()`. Pick a key inside
   the codec alphabet; check combo collisions on both macOS (Meta) and
   Win/Linux (Ctrl) spellings of `primary_modifier()`.
2. **Codec** — if the key spelling is new (e.g. a new OEM key), extend both
   `StringToVKey()` and `VKeyToString()` — they are documented inverses.
3. **Handler** — add the `action_id == "..."` branch (or prefix branch) in
   `ExecuteCommandAction()`. If the action must be blocked in primary
   incognito, also update `IsPrimaryIncognitoAllowedActionId`.
4. **Run the tests** — `cargo test -p maho-core contract_tests` (fast) and the
   C++ `MahoShortcutContractTest` suite (next `unit_tests` run).
5. **No feature, no binding** — if the feature is not shipping, do not register
   the shortcut (Rule 4).

## Case study: the 2026-08-20 audit

| Binding | Failure mode | Fix |
|---------|--------------|-----|
| `mru_tab_switch_next/prev` (Ctrl+Tab) | Rule 1: registry action with no dispatch case; interceptor swallowed the event, native fallback suppressed | Added branch routing to `BrowserView::MaybeHandleMruTabSwitch()` with positional fallback |
| `swap_split_view` (Ctrl+Shift+\\) | Rule 2: key `"\\"` missing from both codec directions — unregistrable and unresolvable | Added `VKEY_OEM_5 ↔ "\\"` |
| `zoom_in` (⌘+) | Rule 2: registry spells `"+"`, codec returned `"="` for `OEM_PLUS` — no round trip; zoom kept working only via Chromium's native accelerator, masking the dead Maho binding | Codec now emits `"+"` for `OEM_PLUS` and accepts both `"="` and `"+"` |
| `new_note` (Ctrl+N) | Rule 4: ghost binding from the 2026-05 monorepo init; Notes pipeline exists in core but no browser surface calls it; Ctrl+N was swallowed on macOS | Removed from the registry |
