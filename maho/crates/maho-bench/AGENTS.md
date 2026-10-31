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
| Browser CLI Harness | `src/browser_cli/`, `browser_cli/` | Workload definitions, fixtures W1-W6, result schema, driver harness |
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

# Rust micro-benchmarks (criterion): crdt_bench, tab_switch, command_search,
# tab_snapshot_lookup, persistence_bench, memory_hnsw_bench, sqlite_concurrency_bench
cargo bench -p maho-bench            # run all
cargo bench -p maho-bench --no-run   # compile-only check

# CI Performance Regression Gate
python3 maho/crates/maho-bench/run_perf_regression_gate.py --dry-run
python3 maho/crates/maho-bench/run_perf_regression_gate.py --parse-only
python3 maho/crates/maho-bench/run_perf_regression_gate.py
```

## AGENT TASK BENCHMARK (web/run_bench.py)

Mind2Web-style suite that runs each task in `web/tasks.json` through `maho agent run`
and grades the transcript with an LLM judge over the browser's configured endpoint.
Writes `web/results.json`.

Prerequisites (all asserted or required at runtime):
1. Debug CLI built: `cargo build -p maho-cli` (harness uses `target/debug/maho`).
2. Maho browser running with a live MCP socket at
   `~/Library/Application Support/Maho/maho.sock` (`require_live_browser()` aborts otherwise).
3. AI configured in `~/Library/Application Support/Maho/Default/Preferences`
   (`maho.ai.api_key` / `base_url` / `model`) — used for both the agent and the judge.

Run:
```bash
python3 crates/maho-bench/web/run_bench.py
# optional: BENCH_MCP_SOCKET=<path> BENCH_DB_PATH=<path> to override
```
Exit 0 only when every task passes; otherwise non-zero. Each task caps at 300s.
