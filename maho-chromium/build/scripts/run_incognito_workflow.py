#!/usr/bin/env python3
import argparse
import os
import sys
import subprocess

def main():
    parser = argparse.ArgumentParser(description="Run incognito desktop implementation workflow.")
    parser.add_argument("--check-all", action="store_true", help="Run all verification checks.")
    parser.add_argument("--audit", action="store_true", help="Run preimage audit only.")
    args = parser.parse_args()

    workspace = "/Users/indo/code/project/maho-workspace"
    script_dir = os.path.join(workspace, "maho-chromium/build/scripts")

    if args.audit or args.check_all:
        print("Running preimage audit...")
        res = subprocess.run([sys.executable, os.path.join(script_dir, "audit_incognito_preimage.py")], cwd=workspace)
        if res.returncode != 0:
            print("Preimage audit failed.")
            return 1
        print("Preimage audit passed.")

    if args.check_all:
        print("Running source manifest generation...")
        res = subprocess.run([sys.executable, os.path.join(script_dir, "hash_source_manifest.py")], cwd=workspace, capture_output=True)
        if res.returncode != 0:
            print("Source manifest generation failed.")
            return 1
        print(f"Source manifest generated: {len(res.stdout)} bytes.")

        print("Running python unit tests...")
        res = subprocess.run([sys.executable, os.path.join(script_dir, "test_incognito_tools.py")], cwd=workspace)
        if res.returncode != 0:
            print("Python unit tests failed.")
            return 1
        print("Python unit tests passed.")

    print("Incognito workflow completed successfully.")
    return 0

if __name__ == "__main__":
    sys.exit(main())
