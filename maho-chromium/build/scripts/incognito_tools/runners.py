import subprocess
import os

def run_gtest(binary_path, filter_arg, out_summary_path):
    cmd = [
        binary_path,
        f"--gtest_filter={filter_arg}",
        "--gtest_fail_if_no_test_selected",
        "--test-launcher-jobs=1",
        "--test-launcher-retry-limit=0",
        f"--test-launcher-summary-output={out_summary_path}"
    ]
    res = subprocess.run(cmd, capture_output=True, text=True)
    return res

def run_cargo_test(manifest_path, package, test_name, specific_case):
    cmd = [
        "cargo", "test",
        "--manifest-path", manifest_path,
        "-p", package,
        "--test", test_name,
        "--", specific_case
    ]
    res = subprocess.run(cmd, capture_output=True, text=True)
    return res
