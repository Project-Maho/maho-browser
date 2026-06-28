# Build Verification — 2026-06-28

## Summary

**Verdict: GREEN (full chrome target links cleanly after maho-core rebuild)**

The T3.1 (Tab Tidy) and T3.2 (Routines) Mojo IDL changes compile cleanly. Generated bindings are produced and contain all expected types. The `chrome` target now links successfully after rebuilding the maho-core prebuilt static library to include the `maho_account_get_tier` FFI symbol.

---

## PART 1 — Mojo IDL Syntax Verification

### maho_ai.mojom

| Element | Status | Location |
|---------|--------|----------|
| `struct TabInfo { tab_id, title, url }` | Valid | L67-74 |
| `struct TidyFolder { name, tab_ids }` | Valid | L77-82 |
| `RequestTabTidy(array<TabInfo>) => (array<TidyFolder>)` | Valid | L437 |
| `ApplyTabTidyFolders(array<TidyFolder>)` | Valid (one-way) | L442 |

No explicit ordinals used — implicit ordinals are fine since no methods were reordered (new methods appended at end of `PageHandler` interface).

### maho_routines.mojom

| Element | Status |
|---------|--------|
| `struct RoutineInfo { id, name, cron, description }` | Valid |
| `struct RoutineRunResult { id, ran_at, content, success }` | Valid |
| `PageHandlerFactory`, `PageHandler`, `Page` interfaces | Standard pattern |
| No imports needed (standalone) | Valid |

---

## PART 2 — Build Results

### Narrow mojo_bindings target (EXIT 0)

```bash
autoninja -C out/Default \
  "maho/browser/ui/webui/maho_ai:mojo_bindings" \
  "maho/browser/ui/webui/maho_routines:mojo_bindings"
# Result: "ninja: no work to do." (already built from prior incremental)
# Exit code: 0
```

GN labels confirmed from BUILD.gn:
- `//maho/browser/ui/webui/maho_ai:mojo_bindings` (sources: `maho_ai.mojom`)
- `//maho/browser/ui/webui/maho_routines:mojo_bindings` (sources: `maho_routines.mojom`)

### Full chrome target (EXIT 1 — linker only)

```bash
autoninja -C out/Default chrome
# Compiled 58/79 steps successfully including:
#   [41/79] maho_routines_page_handler.o — OK
#   [50/79] maho_ai_page_handler.o — OK
# FAILED at step [59/79] SOLINK libchrome_dll.dylib
```

**Error:**
```
Undefined symbols for architecture arm64:
  "_maho_account_get_tier", referenced from:
    MahoRoutinesPageHandler::RunRoutine(...) in maho_routines_page_handler.o
    MahoRoutinesPageHandler::GetUserTier(...) in maho_routines_page_handler.o
```

**Root cause:** The `maho_account_get_tier` FFI function is called from `maho_routines_page_handler.cc` but not yet exported from the Rust `libmaho_ffi.a` prebuilt. This is a Rust-side implementation gap (T3.2 pending FFI work), NOT a Mojo IDL issue.

**All C++ compilation steps passed.** The IDL-generated headers compiled without error.

---

## PART 3 — Generated Artifacts

### maho_ai (all present)

| File | Size | Modified |
|------|------|----------|
| `gen/maho/browser/ui/webui/maho_ai/maho_ai.mojom.h` | 120,570 B | 2026-06-28 09:33 |
| `gen/maho/browser/ui/webui/maho_ai/maho_ai.mojom-shared.h` | 66,750 B | 2026-06-28 09:33 |
| `gen/maho/browser/ui/webui/maho_ai/maho_ai.mojom-webui.ts` | present | 2026-06-28 |
| `gen/maho/browser/ui/webui/maho_ai/maho_ai.mojom-converters.ts` | present | 2026-06-28 |

**Content verified in `.mojom-webui.ts`:**
- `requestTabTidy(tabs: TabInfo[]): Promise<{ folders: TidyFolder[] }>`
- `applyTabTidyFolders(folders: TidyFolder[]): void`
- `TabInfo`, `TidyFolder` type interfaces generated

### maho_routines (all present)

| File | Status |
|------|--------|
| `gen/.../maho_routines.mojom.h` | Present — contains `RoutineInfo`, `RoutineRunResult`, `ListRoutines` |
| `gen/.../maho_routines.mojom-shared.h` | Present |
| `gen/.../maho_routines.mojom-forward.h` | Present |
| `gen/.../maho_routines.mojom-webui.ts` | Present — contains `RoutineInfo`, `RunRoutine`, `GetUserTier` |
| `gen/.../maho_routines.mojom-converters.ts` | Present |
| `gen/.../maho_routines.mojom-params-data.h` | Present |

**Note:** Chromium's newer Mojo pipeline generates `.mojom-webui.ts` (TypeScript), not `.mojom-webui.js`.

---

## PART 4 — TypeScript Binding Check

`maho-chromium/browser/resources/maho_ai/store.ts` imports:
```ts
import { TabInfo, TidyFolder, ... } from './maho_ai.mojom-webui.js';
```

And uses:
- `requestTabTidy(tabs: TabInfo[]): Promise<TidyFolder[]>` (L515)
- `applyTabTidyFolders(folders: TidyFolder[]): Promise<void>` (L520)

**Issue found:** The hand-maintained type stub at `maho_ai.mojom-webui.js.d.ts` does NOT include `TabInfo` or `TidyFolder` declarations. Local `tsc` via the React tsconfig would fail on these imports.

**Workaround:** At Chromium build time, the generated `.mojom-webui.ts` file resolves these types. The `.d.ts` stub is only needed for IDE/local typecheck. Needs update to add:
```ts
export interface TabInfo { tabId: string; title: string; url: string; }
export interface TidyFolder { name: string; tabIds: string[]; }
```

---

## PART 5 — C++ Implementation Sanity Check

### maho_ai_page_handler.h — override declarations

```
L87: RequestTabTidyCallback callback) override;
L89: std::vector<maho_ai::mojom::TidyFolderPtr> folders) override;
```

Both `RequestTabTidy` and `ApplyTabTidyFolders` declared with `override`.

### maho_routines_page_handler.h — override declarations

```
L21: void ListRoutines(ListRoutinesCallback callback) override;
L22: void RunRoutine(const std::string& id, RunRoutineCallback callback) override;
L23: void GetUserTier(GetUserTierCallback callback) override;
```

All interface methods implemented with `override`.

### WebUI Registration

```cpp
// chromium/src/chrome/browser/ui/webui/chrome_web_ui_configs.cc:282
map.AddWebUIConfig(std::make_unique<MahoRoutinesUIConfig>());
```

Registration confirmed.

---

## Blockers

| Blocker | Severity | Status |
|---------|----------|--------|
| ~~`_maho_account_get_tier` missing from `libmaho_ffi.a`~~ | ~~Link-time only~~ | **RESOLVED** — prebuilt lib rebuilt |
| `.d.ts` stub missing TabInfo/TidyFolder | Local typecheck only | Open — `maho_ai.mojom-webui.js.d.ts` needs update |

The linker blocker is resolved. Only the local TypeScript stub gap remains (does not affect Chromium build).

---

## Conclusion

- **Mojo IDL: GREEN** — both mojom files are syntactically valid, bindings generate correctly
- **C++ compilation: GREEN** — all page handler `.o` files compile without error
- **Linker: GREEN** — `chrome` target links successfully (`libchrome_dll.dylib` + `Maho` executable)
- **TypeScript: YELLOW** — local `.d.ts` stub needs update for Tab Tidy types
- **Overall: GREEN** — full chrome build passes; Maho.app bundle produced

---

## Resolution (2026-06-28)

The `_maho_account_get_tier` undefined symbol linker failure was resolved by rebuilding the maho-core prebuilt static library:

```bash
python3 maho-chromium/build/scripts/build_maho_core.py
# Exit code: 0
# Copied libmaho_ffi.a (82.2 MB) to maho-chromium/third_party/maho/prebuilt/arm64/
```

**Symbol verification:**
```
$ nm maho-chromium/third_party/maho/prebuilt/arm64/libmaho_ffi.a | grep maho_account_get_tier
000000000000a730 T _maho_account_get_tier
```

**Full chrome build:**
```bash
python3 maho-chromium/build/scripts/build_maho.py
# [1148/1148] COPY_BUNDLE_DATA 'Maho Framework.framework' 'Maho.app/Contents/Frameworks/Maho Framework.framework'
# Exit code: 0
```

**Root cause:** The Rust FFI function `maho_account_get_tier` was added to `maho/crates/maho-ffi/src/lib.rs` and declared in `maho-chromium/third_party/maho/maho_ffi.h`, but the prebuilt `.a` archive was stale. The lock-backed `build_maho_core.py` script recompiled the Rust workspace in release mode for `aarch64-apple-darwin` and copied the output, closing the symbol gap.

**App bundle confirmed:**
```
chromium/src/out/Default/Maho.app/Contents/ (8 entries)
```
