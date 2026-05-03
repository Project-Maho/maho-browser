# maho-engine

## OVERVIEW
Rust workspace crate boundary for engine-level browser behavior.

## STRUCTURE
- `src/` — crate implementation
- `Cargo.toml` — crate manifest

## WHERE TO LOOK
| Task | Location | Notes |
|------|----------|-------|
| Crate implementation | `src/` | Engine-specific logic |
| Dependencies / features | `Cargo.toml` | Crate wiring |

## CONVENTIONS
- Keep engine concerns isolated from `maho-core` and shell code.
- Follow workspace-wide Rust conventions.

## ANTI-PATTERNS
- Do not leak shell/UI concerns into this crate.
- Do not invent shared contracts here; use `maho-types` if needed.

## COMMANDS
```bash
cargo test -p maho-engine
```
