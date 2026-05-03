# maho-jni

## OVERVIEW
Android JNI bridge crate boundary.

## STRUCTURE
- `src/` — JNI bindings and Rust-side bridge code
- `Cargo.toml` — crate manifest

## WHERE TO LOOK
| Task | Location | Notes |
|------|----------|-------|
| JNI bridge implementation | `src/` | Rust ↔ Android boundary |
| Dependencies / features | `Cargo.toml` | Crate wiring |

## CONVENTIONS
- Keep this crate limited to JNI boundary concerns.
- Follow workspace Rust conventions and shared types contracts.

## ANTI-PATTERNS
- Do not place browser core logic here.
- Do not place Android UI logic here.

## COMMANDS
```bash
cargo test -p maho-jni
```
