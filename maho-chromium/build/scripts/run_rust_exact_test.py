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
    parser = argparse.ArgumentParser(description="Run exact Rust tests for a specific inventory group.")
    parser.add_argument("--inventory", required=True, help="Path to incognito_required_inventory.json")
    parser.add_argument("--group", required=True, help="Inventory group name")
    parser.add_argument("--out-dir", required=True, help="Build output directory (not directly used by cargo but required)")
    parser.add_argument("--artifact-dir", required=True, help="Directory to save test artifacts/receipts")
    args = parser.parse_args()

    with open(args.inventory, "r") as f:
        inventory = json.load(f)

    if args.group not in inventory["groups"]:
        print(f"Error: Group {args.group} not found in inventory.", file=sys.stderr)
        return 1

    group_data = inventory["groups"][args.group]
    manifest_path = group_data["manifest_path"]
    package = group_data["package"]
    test_target = group_data["test"]
    expected_tests = group_data["tests"]
    expected_count = group_data["expected_count"]

    os.makedirs(args.artifact_dir, exist_ok=True)

    workspace = _workspace_root()
    manifest_abs_path = os.path.join(workspace, manifest_path)

    passed_tests = []
    failed_tests = []

    # Determine integration or lib mode
    package_dir = os.path.join(workspace, "maho", "crates", package)
    test_file = os.path.join(package_dir, "tests", f"{test_target}.rs")
    is_integration = os.path.exists(test_file)

    # Run cargo test for each test case individually to ensure exact matching
    for test_case in expected_tests:
        cmd = [
            "cargo", "test",
            "--manifest-path", manifest_abs_path,
            "-p", package,
        ]
        if is_integration:
            cmd.extend(["--test", test_target])
        else:
            cmd.append("--lib")
        cmd.extend(["--", test_case, "--exact"])


        print(f"Running cargo test: {' '.join(cmd)}")
        res = subprocess.run(cmd, capture_output=True, text=True)

        # Write individual test log
        test_log_path = os.path.join(args.artifact_dir, f"test_{test_case}.log")
        with open(test_log_path, "w") as f:
            f.write(res.stdout)
            if res.stderr:
                f.write("\n--- STDERR ---\n")
                f.write(res.stderr)

        # Parse output for "1 passed; 0 failed" or equivalent success indicator
        # cargo test output format: "test result: ok. 1 passed; 0 failed; ..."
        if "1 passed; 0 failed" in res.stdout and "test result: ok" in res.stdout:
            passed_tests.append(test_case)
            print(f"Test {test_case}: PASSED")
        else:
            failed_tests.append(test_case)
            print(f"Test {test_case}: FAILED")
            print(res.stdout)

    print(f"Group {args.group} completed: {len(passed_tests)}/{expected_count} passed.")

    # Write receipt
    receipt_path = os.path.join(args.artifact_dir, "receipt.json")
    with open(receipt_path, "w") as f:
        json.dump({
            "group": args.group,
            "expected_count": expected_count,
            "passed_count": len(passed_tests),
            "failed": failed_tests,
            "status": "SUCCESS" if len(passed_tests) == expected_count else "FAILED"
        }, f, indent=2)

    if len(passed_tests) != expected_count:
        return 1
    return 0

if __name__ == "__main__":
    sys.exit(main())
