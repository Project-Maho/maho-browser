#!/usr/bin/env python3
"""Automated Startup & Benchmark CI Gate for Maho Performance Regression Detection.

Runs Criterion microbenchmarks, parses generated JSON performance reports and
estimates, checks measured latencies against defined performance budgets, and exits
with code 0 on pass or code 1 on regression.
"""

from __future__ import annotations

import argparse
import dataclasses
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time
from typing import Any, Dict, List, Optional, Tuple


HERE = Path(__file__).resolve().parent
MAHO_DIR = HERE.parents[1] if (HERE.parents[1] / "Cargo.toml").exists() else HERE.parent
DEFAULT_MANIFEST_PATH = MAHO_DIR / "Cargo.toml"
DEFAULT_TARGET_DIR = MAHO_DIR / "target"
DEFAULT_CRITERION_DIR = DEFAULT_TARGET_DIR / "criterion"

DEFAULT_BENCHMARK_TARGETS = [
    "core_startup_bench",
    "persistence_bench",
    "memory_hnsw_bench",
    "sqlite_concurrency_bench",
    "tab_switch",
    "crdt_bench",
]

# Latency budgets specified in milliseconds (ms)
# Specific benchmark identifiers match exact IDs produced by Criterion.
DEFAULT_LATENCY_BUDGETS_MS: Dict[str, float] = {
    # Core Startup & Cold Load benchmarks (Contract: cold launch < 800ms, core load < 50ms)
    "cold_launch_to_interactive": 800.0,
    "core_startup_cold_load": 50.0,
    "core_startup_load_state": 50.0,
    "core_startup_hydrate_state": 50.0,
    "persistence_save_state": 100.0,
    "persistence_save_state_100_tabs": 100.0,
    "yrs_cold_load/100": 10.0,
    "yrs_cold_load/1000": 50.0,
    "automerge_cold_load/100": 20.0,
    "automerge_cold_load/1000": 100.0,
    "diamond_cold_load/100": 10.0,
    "diamond_cold_load/1000": 50.0,

    # Tab Switch benchmarks (< 5ms baseline budget, suspended-to-active contract < 300ms, gate < 5ms microbench)
    "tab_switch_active_to_active": 5.0,
    "tab_switch_active_to_active/switch_tab": 5.0,
    "tab_switch_suspended_to_active": 5.0,
    "tab_switch_suspended_to_active/activate_suspended": 5.0,
    "tab_switch_scale/50": 10.0,
    "tab_switch_scale/100": 20.0,
    "tab_switch_scale/500": 50.0,

    # LMDB Persistence benchmarks (< 10ms - 100ms budget)
    "lmdb_save_state_100_tabs": 100.0,
    "lmdb_handle_event_activate_tab_100_tabs": 5.0,
    "lmdb_handle_event_tab_url_updated_100_tabs": 5.0,
    "lmdb_handle_event_throttled_100_rapid_creates": 25.0,

    # Memory HNSW rebuild & search benchmarks
    "memory_hnsw_build_384d/100": 500.0,
    "memory_hnsw_build_384d/1000": 1500.0,
    "memory_hnsw_build_384d/5000": 8000.0,
    "memory_hnsw_build_1536d/100": 1500.0,
    "memory_hnsw_build_1536d/1000": 5000.0,
    "memory_hnsw_build_1536d/5000": 25000.0,
    "memory_hnsw_search_384d/100": 1.0,
    "memory_hnsw_search_384d/1000": 2.0,
    "memory_hnsw_search_384d/5000": 5.0,
    "memory_hnsw_search_1536d/100": 2.0,
    "memory_hnsw_search_1536d/1000": 5.0,
    "memory_hnsw_search_1536d/5000": 10.0,

    # SQLite Concurrency benchmarks
    "sqlite_unencrypted_concurrency/readers_with_writer/1": 10.0,
    "sqlite_unencrypted_concurrency/readers_with_writer/2": 20.0,
    "sqlite_unencrypted_concurrency/readers_with_writer/4": 40.0,
    "sqlite_unencrypted_concurrency/readers_with_writer/8": 80.0,
    "sqlite_encrypted_concurrency/readers_with_writer/1": 25.0,
    "sqlite_encrypted_concurrency/readers_with_writer/2": 50.0,
    "sqlite_encrypted_concurrency/readers_with_writer/4": 100.0,
    "sqlite_encrypted_concurrency/readers_with_writer/8": 200.0,
    "sqlite_idle_vs_active_writer/unencrypted_4_readers_idle": 30.0,
    "sqlite_idle_vs_active_writer/unencrypted_4_readers_with_writer": 45.0,
    "sqlite_idle_vs_active_writer/encrypted_4_readers_idle": 60.0,
    "sqlite_idle_vs_active_writer/encrypted_4_readers_with_writer": 90.0,
}

# Fallback regex patterns for unlisted or newly added benchmarks
DEFAULT_REGEX_BUDGETS_MS: List[Tuple[re.Pattern, float, str]] = [
    (re.compile(r"^cold_launch.*"), 800.0, "Cold launch to interactive"),
    (re.compile(r".*(core_startup|cold_load|hydrate_state).*"), 50.0, "Core startup and cold load operations"),
    (re.compile(r".*persistence_save.*"), 100.0, "State persistence save operations"),
    (re.compile(r"^tab_switch.*"), 5.0, "Tab switch operations"),
    (re.compile(r"^lmdb_.*"), 50.0, "LMDB persistence operations"),
    (re.compile(r".*hnsw_search.*"), 5.0, "HNSW embedding search"),
    (re.compile(r".*hnsw_build.*"), 5000.0, "HNSW index rebuild"),
    (re.compile(r".*sqlite.*concurrency.*"), 100.0, "SQLite concurrent queries"),
    (re.compile(r".*command_search.*"), 10.0, "Command palette search"),
    (re.compile(r".*tab_snapshot.*"), 15.0, "Tab snapshot lookup"),
]


@dataclasses.dataclass
class BenchmarkEstimate:
    bench_id: str
    mean_ms: float
    median_ms: float
    std_dev_ms: float
    ci_lower_ms: float
    ci_upper_ms: float
    sample_count: Optional[int] = None
    group_id: Optional[str] = None
    function_id: Optional[str] = None
    value_str: Optional[str] = None


@dataclasses.dataclass
class BudgetEvaluation:
    bench_id: str
    measured_ms: float
    budget_ms: float
    passed: bool
    margin_percent: float
    rule_source: str
    median_ms: float
    ci_range_ms: Tuple[float, float]

    def to_dict(self) -> Dict[str, Any]:
        return {
            "bench_id": self.bench_id,
            "measured_ms": round(self.measured_ms, 4),
            "budget_ms": round(self.budget_ms, 4),
            "passed": self.passed,
            "margin_percent": round(self.margin_percent, 2),
            "rule_source": self.rule_source,
            "median_ms": round(self.median_ms, 4),
            "ci_lower_ms": round(self.ci_range_ms[0], 4),
            "ci_upper_ms": round(self.ci_range_ms[1], 4),
        }


def format_duration(ms: float) -> str:
    """Format milliseconds into human-readable string."""
    if ms < 0.001:
        return f"{ms * 1_000_000:.2f} ns"
    elif ms < 1.0:
        return f"{ms * 1_000:.2f} µs"
    elif ms < 1000.0:
        return f"{ms:.2f} ms"
    else:
        return f"{ms / 1000.0:.2f} s"


def resolve_budget(
    bench_id: str,
    explicit_budgets: Dict[str, float],
    regex_budgets: List[Tuple[re.Pattern, float, str]],
) -> Optional[Tuple[float, str]]:
    """Resolve budget for benchmark ID from exact match or regex rules."""
    if bench_id in explicit_budgets:
        return (explicit_budgets[bench_id], "exact_budget")

    for pattern, max_ms, desc in regex_budgets:
        if pattern.search(bench_id):
            return (max_ms, f"regex_budget({desc})")

    return None


def parse_criterion_directory(criterion_dir: Path) -> List[BenchmarkEstimate]:
    """Discover and parse all Criterion estimates.json and benchmark.json files."""
    estimates: List[BenchmarkEstimate] = []
    if not criterion_dir.exists():
        return estimates

    # Criterion layout: criterion_dir/<group_or_bench>/.../[new|base]/estimates.json
    # Parse current measurements first so deduplication cannot select a baseline.
    for path in sorted(
        criterion_dir.rglob("estimates.json"),
        key=lambda path: (path.parent.name != "new", path),
    ):
        parent = path.parent
        if parent.name not in ("new", "base"):
            continue

        bench_dir = parent.parent
        bench_json_path = parent / "benchmark.json"
        if not bench_json_path.exists():
            bench_json_path = bench_dir / "benchmark.json"

        bench_id = ""
        group_id = None
        function_id = None
        value_str = None

        if bench_json_path.exists():
            try:
                meta = json.loads(bench_json_path.read_text(encoding="utf-8"))
                bench_id = meta.get("full_id") or meta.get("directory_name") or ""
                group_id = meta.get("group_id")
                function_id = meta.get("function_id")
                value_str = meta.get("value_str")
            except Exception:
                pass

        if not bench_id:
            try:
                bench_id = str(bench_dir.relative_to(criterion_dir)).replace("\\", "/")
            except Exception:
                bench_id = bench_dir.name

        try:
            data = json.loads(path.read_text(encoding="utf-8"))
            mean_data = data.get("mean") or {}
            median_data = data.get("median") or {}
            std_dev_data = data.get("std_dev") or {}

            mean_ns = float(mean_data.get("point_estimate", 0.0))
            median_ns = float(median_data.get("point_estimate", mean_ns))
            std_dev_ns = float(std_dev_data.get("point_estimate", 0.0))

            mean_ci = mean_data.get("confidence_interval") or {}
            ci_lower_ns = float(mean_ci.get("lower_bound", mean_ns))
            ci_upper_ns = float(mean_ci.get("upper_bound", mean_ns))

            estimates.append(
                BenchmarkEstimate(
                    bench_id=bench_id,
                    mean_ms=mean_ns / 1_000_000.0,
                    median_ms=median_ns / 1_000_000.0,
                    std_dev_ms=std_dev_ns / 1_000_000.0,
                    ci_lower_ms=ci_lower_ns / 1_000_000.0,
                    ci_upper_ms=ci_upper_ns / 1_000_000.0,
                    group_id=group_id,
                    function_id=function_id,
                    value_str=value_str,
                )
            )
        except Exception as e:
            sys.stderr.write(f"Warning: Failed to parse {path}: {e}\n")

    # Keep the current measurement, falling back to a baseline-only report.
    seen = {}
    for est in estimates:
        if est.bench_id not in seen:
            seen[est.bench_id] = est
    return list(seen.values())


def generate_mock_estimates() -> List[BenchmarkEstimate]:
    """Generate representative mock estimates for dry-run verification."""
    return [
        BenchmarkEstimate(
            bench_id="cold_launch_to_interactive",
            mean_ms=138.45,
            median_ms=137.90,
            std_dev_ms=4.12,
            ci_lower_ms=135.20,
            ci_upper_ms=141.80,
            group_id="cold_launch_to_interactive",
        ),
        BenchmarkEstimate(
            bench_id="core_startup_cold_load",
            mean_ms=12.65,
            median_ms=12.50,
            std_dev_ms=0.45,
            ci_lower_ms=12.30,
            ci_upper_ms=13.00,
            group_id="core_startup_cold_load",
        ),
        BenchmarkEstimate(
            bench_id="persistence_save_state",
            mean_ms=28.40,
            median_ms=28.10,
            std_dev_ms=1.10,
            ci_lower_ms=27.50,
            ci_upper_ms=29.30,
            group_id="persistence_save_state",
        ),
        BenchmarkEstimate(
            bench_id="tab_switch_active_to_active/switch_tab",
            mean_ms=0.00264,
            median_ms=0.00263,
            std_dev_ms=0.00003,
            ci_lower_ms=0.00263,
            ci_upper_ms=0.00265,
            group_id="tab_switch_active_to_active",
            function_id="switch_tab",
        ),
        BenchmarkEstimate(
            bench_id="tab_switch_suspended_to_active/activate_suspended",
            mean_ms=0.00312,
            median_ms=0.00310,
            std_dev_ms=0.00004,
            ci_lower_ms=0.00308,
            ci_upper_ms=0.00315,
            group_id="tab_switch_suspended_to_active",
            function_id="activate_suspended",
        ),
        BenchmarkEstimate(
            bench_id="tab_switch_scale/50",
            mean_ms=0.0152,
            median_ms=0.0151,
            std_dev_ms=0.0002,
            ci_lower_ms=0.0150,
            ci_upper_ms=0.0154,
            group_id="tab_switch_scale",
            value_str="50",
        ),
        BenchmarkEstimate(
            bench_id="lmdb_save_state_100_tabs",
            mean_ms=31.20,
            median_ms=30.98,
            std_dev_ms=2.09,
            ci_lower_ms=30.79,
            ci_upper_ms=31.60,
            group_id="lmdb_save_state_100_tabs",
        ),
        BenchmarkEstimate(
            bench_id="lmdb_handle_event_activate_tab_100_tabs",
            mean_ms=0.0218,
            median_ms=0.0217,
            std_dev_ms=0.0005,
            ci_lower_ms=0.0216,
            ci_upper_ms=0.0219,
            group_id="lmdb_handle_event_activate_tab_100_tabs",
        ),
        BenchmarkEstimate(
            bench_id="memory_hnsw_build_384d/100",
            mean_ms=240.78,
            median_ms=240.50,
            std_dev_ms=5.10,
            ci_lower_ms=238.84,
            ci_upper_ms=242.91,
            group_id="memory_hnsw_build_384d",
            value_str="100",
        ),
        BenchmarkEstimate(
            bench_id="memory_hnsw_build_384d/1000",
            mean_ms=668.83,
            median_ms=665.20,
            std_dev_ms=12.40,
            ci_lower_ms=657.65,
            ci_upper_ms=680.98,
            group_id="memory_hnsw_build_384d",
            value_str="1000",
        ),
        BenchmarkEstimate(
            bench_id="memory_hnsw_search_384d/1000",
            mean_ms=0.1205,
            median_ms=0.1201,
            std_dev_ms=0.0020,
            ci_lower_ms=0.1195,
            ci_upper_ms=0.1214,
            group_id="memory_hnsw_search_384d",
            value_str="1000",
        ),
        BenchmarkEstimate(
            bench_id="sqlite_unencrypted_concurrency/readers_with_writer/4",
            mean_ms=6.85,
            median_ms=6.80,
            std_dev_ms=0.15,
            ci_lower_ms=6.75,
            ci_upper_ms=6.95,
            group_id="sqlite_unencrypted_concurrency",
            function_id="readers_with_writer",
            value_str="4",
        ),
        BenchmarkEstimate(
            bench_id="sqlite_encrypted_concurrency/readers_with_writer/4",
            mean_ms=14.30,
            median_ms=14.20,
            std_dev_ms=0.45,
            ci_lower_ms=14.05,
            ci_upper_ms=14.55,
            group_id="sqlite_encrypted_concurrency",
            function_id="readers_with_writer",
            value_str="4",
        ),
    ]


def run_cargo_benchmark(
    manifest_path: Path,
    bench_target: str,
    target_dir: Path,
    filter_expr: Optional[str] = None,
    verbose: bool = False,
) -> int:
    """Execute a specific benchmark target via cargo bench."""
    cmd = [
        "cargo",
        "bench",
        "--manifest-path",
        str(manifest_path),
        "-p",
        "maho-bench",
        "--bench",
        bench_target,
        "--target-dir",
        str(target_dir.resolve()),
    ]
    if filter_expr:
        cmd.extend(["--", filter_expr])

    print(f"\n🚀 Running benchmark suite: {bench_target}...")
    if verbose:
        print(f"   Command: {' '.join(cmd)}")

    start_t = time.time()
    res = subprocess.run(cmd)
    elapsed = time.time() - start_t
    print(f"⏱️  Suite '{bench_target}' completed in {elapsed:.2f}s (exit {res.returncode})")
    return res.returncode


def evaluate_estimates(
    estimates: List[BenchmarkEstimate],
    explicit_budgets: Dict[str, float],
    regex_budgets: List[Tuple[re.Pattern, float, str]],
    target_filter: Optional[str] = None,
) -> Tuple[List[BudgetEvaluation], List[BenchmarkEstimate]]:
    """Compare benchmark estimates against latency budgets."""
    evaluations: List[BudgetEvaluation] = []
    unbudgeted: List[BenchmarkEstimate] = []

    for est in estimates:
        if target_filter and not (re.search(target_filter, est.bench_id)):
            continue

        resolved = resolve_budget(est.bench_id, explicit_budgets, regex_budgets)
        if resolved is None:
            unbudgeted.append(est)
            continue

        budget_ms, source = resolved
        passed = est.mean_ms <= budget_ms
        margin = ((budget_ms - est.mean_ms) / budget_ms) * 100.0

        evaluations.append(
            BudgetEvaluation(
                bench_id=est.bench_id,
                measured_ms=est.mean_ms,
                budget_ms=budget_ms,
                passed=passed,
                margin_percent=margin,
                rule_source=source,
                median_ms=est.median_ms,
                ci_range_ms=(est.ci_lower_ms, est.ci_upper_ms),
            )
        )

    # Sort so regressions appear at top, followed by bench_id alphabetical
    evaluations.sort(key=lambda x: (x.passed, x.bench_id))
    return evaluations, unbudgeted


def render_report_table(
    evaluations: List[BudgetEvaluation],
    unbudgeted: List[BenchmarkEstimate],
    dry_run: bool = False,
    no_color: bool = False,
) -> None:
    """Print ASCII/ANSI summary table."""
    c_green = "" if no_color else "\033[92m"
    c_red = "" if no_color else "\033[91m"
    c_yellow = "" if no_color else "\033[93m"
    c_cyan = "" if no_color else "\033[96m"
    c_bold = "" if no_color else "\033[1m"
    c_reset = "" if no_color else "\033[0m"

    title = "PERFORMANCE REGRESSION GATE REPORT"
    if dry_run:
        title += " (DRY RUN SIMULATION)"

    print("\n" + "=" * 100)
    print(f"{c_bold}{c_cyan}{title.center(100)}{c_reset}")
    print("=" * 100)

    header = f"{'BENCHMARK':<55} | {'MEASURED':<12} | {'BUDGET':<10} | {'MARGIN':<9} | {'STATUS'}"
    print(header)
    print("-" * 100)

    for item in evaluations:
        status_str = f"{c_green}PASS{c_reset}" if item.passed else f"{c_red}FAIL (REGRESSION){c_reset}"
        measured_str = format_duration(item.measured_ms)
        budget_str = format_duration(item.budget_ms)
        margin_sign = "+" if item.margin_percent >= 0 else ""
        margin_str = f"{margin_sign}{item.margin_percent:.1f}%"

        print(
            f"{item.bench_id:<55} | {measured_str:<12} | {budget_str:<10} | {margin_str:<9} | {status_str}"
        )

    if unbudgeted:
        print("-" * 100)
        print(f"{c_yellow}Unbudgeted benchmarks ({len(unbudgeted)} items):{c_reset}")
        for u in unbudgeted:
            print(f"  • {u.bench_id:<53} = {format_duration(u.mean_ms)}")

    print("=" * 100)

    total = len(evaluations)
    passed_count = sum(1 for e in evaluations if e.passed)
    failed_count = total - passed_count

    if failed_count > 0:
        print(
            f"{c_bold}{c_red}🚨 GATE RESULT: FAILED ({failed_count}/{total} regressions detected){c_reset}\n"
        )
    else:
        print(
            f"{c_bold}{c_green}✅ GATE RESULT: PASSED ({passed_count}/{total} benchmarks within budget){c_reset}\n"
        )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Maho Performance Regression CI Gate - Criterion Benchmark Assertions"
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Simulate execution without running cargo bench or modifying criterion files",
    )
    parser.add_argument(
        "--parse-only",
        action="store_true",
        help="Parse existing target/criterion reports without running cargo bench",
    )
    parser.add_argument(
        "--bench",
        action="append",
        dest="benches",
        help="Specific benchmark suite(s) to execute (e.g. tab_switch, persistence_bench)",
    )
    parser.add_argument(
        "--filter",
        type=str,
        default=None,
        help="Filter specific benchmark tests matching substring or regex",
    )
    parser.add_argument(
        "--target-dir",
        type=Path,
        default=DEFAULT_TARGET_DIR,
        help=f"Target directory (default: {DEFAULT_TARGET_DIR})",
    )
    parser.add_argument(
        "--criterion-dir",
        type=Path,
        default=None,
        help="Explicit path to target/criterion directory",
    )
    parser.add_argument(
        "--manifest-path",
        type=Path,
        default=DEFAULT_MANIFEST_PATH,
        help=f"Path to workspace Cargo.toml (default: {DEFAULT_MANIFEST_PATH})",
    )
    parser.add_argument(
        "--budget-file",
        type=Path,
        default=None,
        help="JSON file defining custom/override latency budgets in ms",
    )
    parser.add_argument(
        "--budget",
        action="append",
        metavar="BENCH_ID=MAX_MS",
        help="Override a single budget on CLI, e.g. --budget lmdb_save_state_100_tabs=50.0",
    )
    parser.add_argument(
        "--json-output",
        type=Path,
        default=None,
        help="Write structured gate results to JSON file",
    )
    parser.add_argument(
        "--no-color",
        action="store_true",
        help="Disable ANSI color codes in console output",
    )
    parser.add_argument(
        "--verbose",
        "-v",
        action="store_true",
        help="Enable verbose output",
    )
    parser.add_argument(
        "--list-budgets",
        action="store_true",
        help="Print all configured latency budgets and exit",
    )

    args = parser.parse_args()

    # Merge budgets
    budgets = dict(DEFAULT_LATENCY_BUDGETS_MS)
    if args.budget_file and args.budget_file.exists():
        try:
            custom = json.loads(args.budget_file.read_text(encoding="utf-8"))
            budgets.update(custom)
        except Exception as e:
            sys.stderr.write(f"Error loading budget file {args.budget_file}: {e}\n")
            return 1

    if args.budget:
        for b in args.budget:
            if "=" in b:
                k, v = b.split("=", 1)
                try:
                    budgets[k.strip()] = float(v.strip())
                except ValueError:
                    sys.stderr.write(f"Invalid budget format: {b}\n")
                    return 1

    if args.list_budgets:
        print("Configured Latency Budgets:")
        for k, v in sorted(budgets.items()):
            print(f"  {k:<55}: {format_duration(v)}")
        return 0

    criterion_dir = args.criterion_dir or (args.target_dir / "criterion")
    target_benches = args.benches or DEFAULT_BENCHMARK_TARGETS

    print(f"🔍 Maho Perf Regression CI Gate starting...")
    print(f"   Manifest: {args.manifest_path}")
    print(f"   Criterion dir: {criterion_dir}")
    print(f"   Target suites: {', '.join(target_benches)}")
    if args.filter:
        print(f"   Filter: {args.filter}")
    if args.dry_run:
        print(f"   Mode: DRY RUN (Simulation)")
    elif args.parse_only:
        print(f"   Mode: PARSE ONLY (Existing Reports)")

    if args.dry_run:
        estimates = generate_mock_estimates()
    elif args.parse_only:
        estimates = parse_criterion_directory(criterion_dir)
    else:
        # Check cargo availability
        if not shutil.which("cargo"):
            sys.stderr.write("Error: 'cargo' executable not found in PATH.\n")
            return 1

        # Run benchmarks
        for suite in target_benches:
            exit_code = run_cargo_benchmark(
                manifest_path=args.manifest_path,
                bench_target=suite,
                target_dir=args.target_dir,
                filter_expr=args.filter,
                verbose=args.verbose,
            )
            if exit_code != 0:
                sys.stderr.write(f"\n❌ Benchmark suite '{suite}' failed during execution.\n")
                return 1

        estimates = parse_criterion_directory(criterion_dir)

    evaluations, unbudgeted = evaluate_estimates(
        estimates=estimates,
        explicit_budgets=budgets,
        regex_budgets=DEFAULT_REGEX_BUDGETS_MS,
        target_filter=args.filter,
    )

    if not evaluations and not unbudgeted:
        sys.stderr.write(
            f"Warning: No benchmark estimates found in {criterion_dir}.\n"
        )
        return 1

    render_report_table(
        evaluations=evaluations,
        unbudgeted=unbudgeted,
        dry_run=args.dry_run,
        no_color=args.no_color,
    )

    if args.json_output:
        result_payload = {
            "timestamp": time.time(),
            "dry_run": args.dry_run,
            "total": len(evaluations),
            "passed": sum(1 for e in evaluations if e.passed),
            "failed": sum(1 for e in evaluations if not e.passed),
            "all_passed": all(e.passed for e in evaluations),
            "evaluations": [e.to_dict() for e in evaluations],
            "unbudgeted_count": len(unbudgeted),
        }
        args.json_output.parent.mkdir(parents=True, exist_ok=True)
        args.json_output.write_text(
            json.dumps(result_payload, indent=2), encoding="utf-8"
        )
        print(f"📄 Summary saved to {args.json_output}")

    # Return 0 if all budgeted benchmarks passed, 1 otherwise
    all_passed = all(e.passed for e in evaluations)
    return 0 if all_passed else 1


if __name__ == "__main__":
    sys.exit(main())
