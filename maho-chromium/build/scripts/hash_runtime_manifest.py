#!/usr/bin/env python3
import os
import sys
import hashlib
import subprocess

def sha256_file(path: str) -> str:
    h = hashlib.sha256()
    try:
        with open(path, "rb") as f:
            while chunk := f.read(65536):
                h.update(chunk)
    except OSError:
        return "error"
    return h.hexdigest()

def get_active_attempt_dir():
    workspace = "/Users/indo/code/project/maho-workspace"
    evidence_root = os.path.join(workspace, '.omo', 'evidence', 'arc-parity-incognito-desktop')
    if os.path.exists(evidence_root):
        attempts = [d for d in os.listdir(evidence_root) if d.startswith('attempt-') and os.path.isdir(os.path.join(evidence_root, d))]
        if attempts:
            attempts.sort()
            return os.path.join(evidence_root, attempts[-1])
    return None

def main():
    workspace = "/Users/indo/code/project/maho-workspace"
    attempt_dir = get_active_attempt_dir()

    records = []

    # 1. Walk Maho.app if it exists
    out_dir = os.path.join(workspace, "chromium/src/out/Default")
    app_path = os.path.join(out_dir, "Maho.app")
    if os.path.exists(app_path):
        for root, dirs, files in os.walk(app_path):
            for filename in files:
                filepath = os.path.join(root, filename)
                st = os.lstat(filepath)
                rel = os.path.relpath(filepath, workspace)
                mode = oct(st.st_mode)[-4:]
                size = str(st.st_size)

                if os.path.islink(filepath):
                    target = os.readlink(filepath)
                    sha = target
                    p_type = "symlink"
                else:
                    sha = sha256_file(filepath)
                    p_type = "file"
                records.append(f"runtime\0{rel}\0{p_type}\0{mode}\0{size}\0{sha}\0")

    # 2. Add build files: args.gn, build.ninja
    for f in ["args.gn", "build.ninja"]:
        fpath = os.path.join(out_dir, f)
        if os.path.exists(fpath):
            st = os.lstat(fpath)
            mode = oct(st.st_mode)[-4:]
            records.append(f"build_file\0{f}\0file\0{mode}\0{st.st_size}\0{sha256_file(fpath)}\0")

    # 3. Add Tooling Info
    try:
        sdk_path = subprocess.check_output(["xcrun", "--show-sdk-path"], text=True).strip()
    except Exception:
        sdk_path = "unknown"
    records.append(f"tooling\0sdk_path\0info\00000\00\0{sdk_path}\0")

    try:
        clang_path = subprocess.check_output(["which", "clang++"], text=True).strip()
    except Exception:
        clang_path = "unknown"
    records.append(f"tooling\0clang_path\0info\00000\00\0{clang_path}\0")

    # Sort and print NUL-delimited records
    records.sort()
    sys.stdout.write("".join(records))

if __name__ == "__main__":
    main()
