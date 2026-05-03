# maho-bench

## OVERVIEW
Benchmark crate boundary for workspace performance and comparison harnesses.

## STRUCTURE
- `src/` — benchmark code
- `Cargo.toml` — crate manifest

## WHERE TO LOOK
| Task | Location | Notes |
|------|----------|-------|
| Benchmarks | `src/` | Perf/comparison harnesses |
| Dependencies / features | `Cargo.toml` | Crate wiring |

## CONVENTIONS
- Keep benchmark code isolated from product logic.
- Use the workspace Rust toolchain and shared dependencies.

## ANTI-PATTERNS
- Do not move production browser logic here.
- Do not couple benchmarks to shell-specific code.

## COMMANDS
```bash
cargo test -p maho-bench
```
