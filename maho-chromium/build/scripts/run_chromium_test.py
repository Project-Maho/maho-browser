#!/usr/bin/env python3
import argparse
import json
import os
import subprocess
import sys

def _workspace_root():
    _script_dir = os.path.dirname(os.path.abspath(__file__))
    return os.path.normpath(os.path.join(_script_dir, "..", "..", ".."))


def main():
    parser = argparse.ArgumentParser(description="Run Chromium tests for a specific inventory group.")
    parser.add_argument("--inventory", required=True, help="Path to incognito_required_inventory.json")
    parser.add_argument("--group", required=True, help="Inventory group name")
    parser.add_argument("--out-dir", required=True, help="Build output directory (e.g. out/Default)")
    parser.add_argument("--artifact-dir", required=True, help="Directory to save test artifacts/receipts")
    args = parser.parse_args()

    with open(args.inventory, "r") as f:
        inventory = json.load(f)

    if args.group not in inventory["groups"]:
        print(f"Error: Group {args.group} not found in inventory.", file=sys.stderr)
        return 1

    group_data = inventory["groups"][args.group]
    binary_name = group_data["binary"]
    expected_tests = group_data["tests"]
    expected_count = group_data["expected_count"]

    os.makedirs(args.artifact_dir, exist_ok=True)
    summary_path = os.path.join(args.artifact_dir, "test_summary.json")

    # Construct the binary path
    # Normally out-dir is relative to chromium/src
    workspace = _workspace_root()
    binary_path = os.path.join(workspace, "chromium", "src", args.out_dir, binary_name)
    if not os.path.exists(binary_path):
        # Fallback to absolute if it's absolute
        binary_path = os.path.join(args.out_dir, binary_name)
        if not os.path.exists(binary_path):
            print(f"Error: Test binary not found at {binary_path}", file=sys.stderr)
            return 1

    # Join tests with ':' for gtest_filter
    gtest_filter = ":".join(expected_tests)

    cmd = [
        binary_path,
        f"--gtest_filter={gtest_filter}",
        "--gtest_fail_if_no_test_selected",
        "--test-launcher-jobs=1",
        "--test-launcher-retry-limit=0",
        f"--test-launcher-summary-output={summary_path}"
    ]

    print(f"Running gtest: {' '.join(cmd)}")

    # Run the test binary
    # Chromium tests are run under depot_tools path
    env = os.environ.copy()
    depot_tools = os.path.join(workspace, "chromium", "src", "third_party", "depot_tools")
    if os.path.exists(depot_tools):
        env["PATH"] = depot_tools + os.pathsep + env.get("PATH", "")

    # gtest tests might run in background/foreground, let's run them synchronously
    res = subprocess.run(cmd, env=env, capture_output=True, text=True)

    # Write stdout/stderr to artifact dir
    with open(os.path.join(args.artifact_dir, "stdout.log"), "w") as f:
        f.write(res.stdout)
    with open(os.path.join(args.artifact_dir, "stderr.log"), "w") as f:
        f.write(res.stderr)

    print(res.stdout)
    if res.returncode != 0:
        print(f"Error: gtest execution failed with exit code {res.returncode}", file=sys.stderr)
        print(res.stderr, file=sys.stderr)
        return res.returncode

    # Validate test summary
    if not os.path.exists(summary_path):
        print(f"Error: test summary not generated at {summary_path}", file=sys.stderr)
        return 1

    with open(summary_path, "r") as f:
        summary = json.load(f)

    # Verify that expected count matches actual ran count and all succeeded using per_iteration_data
    per_iteration_data = summary.get("per_iteration_data", [])
    if not per_iteration_data:
        print("Error: per_iteration_data is empty or missing in test summary", file=sys.stderr)
        return 1

    iteration_results = per_iteration_data[0]
    actual_tests = set(iteration_results.keys())
    expected_tests_set = set(expected_tests)

    # 1. Enforce exact test set (no missing, no extra/renamed cases)
    if actual_tests != expected_tests_set:
        print(f"Error: test set mismatch. Expected: {expected_tests_set}, Got: {actual_tests}", file=sys.stderr)
        return 1

    passed_count = 0
    failures = []

    for test_name in expected_tests:
        runs = iteration_results.get(test_name, [])
        # 2. Reject zero runs or retries (more than 1 run)
        if len(runs) != 1:
            failures.append(f"{test_name} has invalid run count: {len(runs)} (expected exactly 1, no retries)")
            continue
        
        # 3. Reject non-SUCCESS status
        status = runs[0].get("status")
        if status == "SUCCESS":
            passed_count += 1
        else:
            failures.append(f"{test_name} failed with status: {status}")

    print(f"Passed tests in summary: {passed_count}/{expected_count}")

    if failures:
        print("Test validation failures:")
        for fail in failures:
            print(f"  {fail}")

    is_success = (passed_count == expected_count) and (len(failures) == 0)

    # Write a simple receipt file
    receipt_path = os.path.join(args.artifact_dir, "receipt.json")
    with open(receipt_path, "w") as f:
        json.dump({
            "group": args.group,
            "expected_count": expected_count,
            "passed_count": passed_count,
            "status": "SUCCESS" if is_success else "FAILED"
        }, f, indent=2)

    return 0 if is_success else 1


if __name__ == "__main__":
    sys.exit(main())
