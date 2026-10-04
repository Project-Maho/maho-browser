#!/usr/bin/env python3
import os
import sys
import tarfile
import tempfile
import shutil
from incognito_tools.audit import verify_diff
from incognito_tools.manifests import parse_owned_product_paths

def _workspace_root():
    _script_dir = os.path.dirname(os.path.abspath(__file__))
    return os.path.normpath(os.path.join(_script_dir, "..", "..", ".."))


def get_active_attempt_dir():
    workspace = _workspace_root()
    evidence_root = os.path.join(workspace, '.omo', 'evidence', 'arc-parity-incognito-desktop')
    if os.path.exists(evidence_root):
        attempts = [d for d in os.listdir(evidence_root) if d.startswith('attempt-') and os.path.isdir(os.path.join(evidence_root, d))]
        if attempts:
            attempts.sort()
            return os.path.join(evidence_root, attempts[-1])
    return None

def main():
    workspace = _workspace_root()
    attempt_dir = get_active_attempt_dir()
    if not attempt_dir:
        print("Error: Active attempt directory not found.", file=sys.stderr)
        return 1

    preimage_tar = os.path.join(attempt_dir, "preimage", "owned-preimage.tar")
    tsv_path = os.path.join(attempt_dir, "preimage", "owned-product-paths.tsv")
    absent_txt = os.path.join(attempt_dir, "preimage", "owned-absent.txt")

    if not os.path.exists(preimage_tar) or not os.path.exists(tsv_path):
        print("Error: preimage tarball or tsv not found.", file=sys.stderr)
        return 1

    absent_set = set()
    if os.path.exists(absent_txt):
        with open(absent_txt, "r") as f:
            for line in f:
                p = line.strip()
                if p:
                    absent_set.add(p)

    owned_paths = parse_owned_product_paths(tsv_path)

    # Extract preimage tar to temp dir
    temp_dir = tempfile.mkdtemp()
    try:
        with tarfile.open(preimage_tar, "r") as tar:
            tar.extractall(path=temp_dir)

        all_errors = []
        for item in owned_paths:
            path = item["path"]
            mode = item["mode"]

            # Postimage path (in workspace)
            post_file = os.path.join(workspace, path)
            # Preimage file (in temp dir)
            pre_file = os.path.join(temp_dir, path)

            is_absent_at_setup = path in absent_set

            pre_content = ""
            if os.path.exists(pre_file):
                with open(pre_file, "r", encoding="utf-8", errors="ignore") as f:
                    pre_content = f.read()
            elif mode == "existing" and not is_absent_at_setup:
                # File was existing but not in tar, error
                all_errors.append(f"Preimage missing for existing file: {path}")
                continue

            post_content = ""
            if os.path.exists(post_file):
                with open(post_file, "r", encoding="utf-8", errors="ignore") as f:
                    post_content = f.read()
            elif mode == "new" or mode == "generated" or is_absent_at_setup:
                # File does not exist in workspace yet, that is fine
                continue
            else:
                all_errors.append(f"Postimage missing for: {path}")
                continue

            # Perform audit diff check
            errors, _ = verify_diff(pre_content, post_content)
            if errors:
                all_errors.extend(f"{path}: {err}" for err in errors)

        if all_errors:
            print(f"Preimage audit FAILED with {len(all_errors)} errors:", file=sys.stderr)
            for err in all_errors:
                print(f"  {err}", file=sys.stderr)
            return 1

        print("Preimage audit PASSED.")
        return 0
    finally:
        shutil.rmtree(temp_dir)

if __name__ == "__main__":
    sys.exit(main())
