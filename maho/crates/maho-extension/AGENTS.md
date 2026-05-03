# EXTENSION SYSTEM

Browser extension manifest parsing, validation, and loading.

## STRUCTURE

| File | Role |
|------|------|
| `src/manifest.rs` | Manifest parsing from JSON/TOML |
| `src/loader.rs` | Filesystem discovery and loading |
| `src/error.rs` | `thiserror`-derived error types |
| `src/lib.rs` | Public exports |

## WHERE TO LOOK

| Task | Location |
|------|----------|
| Parse extension manifest | `manifest.rs` |
| Load unpacked extension | `loader.rs` |
| Error definitions | `error.rs` |

## CONVENTIONS

- `glob` crate for file matching
- `thiserror` for all error types
- Manifest validates required fields strictly
