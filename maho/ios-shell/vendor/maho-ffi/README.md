# vendor/maho-ffi — pinned FFI snapshot for iOS

This directory holds a **pinned, vendored snapshot** of the `maho-ffi` FFI surface
that the iOS shell links against:

```
vendor/maho-ffi/
  include/maho_ffi.h      # cbindgen-generated C header (pinned)
  lib/ios/libmaho_ffi.a   # static lib, aarch64-apple-ios
  lib/sim/libmaho_ffi.a   # static lib, aarch64-apple-ios-sim
  maho_ffi.pin            # pin metadata (source commit + header sha256)
  README.md               # this file
```

> `include/maho_ffi.h` + `maho_ffi.pin` are produced by `scripts/pin-ffi.sh` and
> **are committed**. The `lib/` static libs are also produced by `pin-ffi.sh` but are
> **gitignored** (build artifacts, ~90 MB each) — mirroring
> `maho-chromium/third_party/maho/prebuilt/`, which is gitignored too. A fresh
> checkout runs `pin-ffi.sh` once to materialize `lib/`.
> None of these exist until you run it once from a consistent core state.

## Why this exists

This is a monorepo whose single working tree is edited by parallel sessions.
Ordinary iOS builds must **not** regenerate the FFI header/lib from the live
`maho-ffi` crate source: a concurrent core/FFI migration leaves the crate source,
the cbindgen header, and the Swift bridge temporarily inconsistent, which breaks
the iOS build with cryptic Swift argument-label errors.

By linking a **pinned snapshot** here (regenerated only on purpose), live core
churn never breaks a normal mobile build — so mobile and desktop can be developed
in parallel in the same folder. This mirrors how desktop already consumes a
prebuilt static lib under `maho-chromium/third_party/maho/`.

## Ownership boundary (IMPORTANT)

- `vendor/maho-ffi/**` and `ios-shell/Bridge/MahoBridge+*.swift` are **mobile-owned**.
- Core/desktop sessions change the FFI contract in `crates/maho-ffi` and publish it;
  they must **not** edit files under `vendor/maho-ffi/` or the Swift bridge.
- Mobile absorbs an FFI contract change **atomically** by running `pin-ffi.sh` and
  updating the Swift bridge in the **same** commit.

## Commands

Re-pin (only from a consistent, compiling core state):

```bash
ios-shell/scripts/pin-ffi.sh                # refuses if FFI source is git-dirty
ios-shell/scripts/pin-ffi.sh --allow-dirty  # override (you own the risk)
```

Pre-build guard (fails fast if the vendored header drifted):

```bash
ios-shell/scripts/check-ffi-pin.sh
```

## Wiring project.yml (do this at landing time, AFTER the first pin)

Once `pin-ffi.sh` has produced the snapshot, switch the search paths so ordinary
builds consume the vendored artifacts (and stop regenerating from live source):

```yaml
# settings:
HEADER_SEARCH_PATHS: ["$(SRCROOT)/vendor/maho-ffi/include"]
LIBRARY_SEARCH_PATHS: ["$(SRCROOT)/vendor/maho-ffi/lib/sim"]           # legacy fallback
"LIBRARY_SEARCH_PATHS[sdk=iphoneos*]": "$(SRCROOT)/vendor/maho-ffi/lib/ios"
"LIBRARY_SEARCH_PATHS[sdk=iphonesimulator*]": "$(SRCROOT)/vendor/maho-ffi/lib/sim"
# OTHER_LDFLAGS keeps "-lmaho_ffi"
```

Also add a pre-build Run Script phase that invokes
`"$SRCROOT/scripts/check-ffi-pin.sh"`, and drop the `Makefile` `build-app`
dependency on `generate-header` so normal builds never regenerate the header.

> These edits are intentionally **not** applied yet: `Makefile`/`cbindgen.toml`
> are currently being edited by an in-flight FFI migration in the shared tree.
> Apply them once that migration lands in a consistent, compiling state, then run
> `pin-ffi.sh` and commit the vendored header + `maho_ffi.pin` + the matching Swift
bridge together (the `lib/` static libs are gitignored and regenerated locally, so
they are never part of the commit).
