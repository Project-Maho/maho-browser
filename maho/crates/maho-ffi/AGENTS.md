# maho-ffi

## OVERVIEW

C FFI layer exposing MahoCore to both Chromium C++ (via `maho-chromium/third_party/maho/maho_bridge.cc`) and Swift shells. Single file, ~437 lines.

## STRUCTURE

`src/lib.rs` — all FFI exports in one file.

## WHERE TO LOOK

| Task | Location |
|------|----------|
| Add new FFI function | `lib.rs` — add `#[no_mangle] pub extern "C" fn` |
| Regenerate C header | Run `cbindgen` (config at `cbindgen.toml`) — **must re-run after any public FFI signature change** |
| Chromium C++ bridge | `maho-chromium/third_party/maho/maho_bridge.cc` + `maho_ffi.h` |
| Swift bridge header | `ios-shell/include/maho_ffi.h` (must match) |
| Complex type passing | JSON serialize → `*mut c_char` pattern |

## CONVENTIONS

- `#[no_mangle]` + `extern "C"` on all exports
- `*mut c_char` for string params/returns — JSON for complex types
- `null_ptr` on error, never panic across FFI boundary
- Safety comment (`// SAFETY: ...`) on every `unsafe` block
- Function naming: `maho_<verb>_<noun>` (e.g. `maho_create_tab`)
- Caller responsible for freeing returned strings via `maho_free_string()`

## ANTI-PATTERNS

- ❌ `unwrap()` / `expect()` in FFI — will abort the caller process
- ❌ Passing Rust types directly — always serialize to JSON/C types
- ❌ Business logic here — delegate to maho-core
- ❌ Editing generated C headers directly — re-run cbindgen instead
