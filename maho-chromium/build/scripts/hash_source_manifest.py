#!/usr/bin/env python3
import os
import sys
import hashlib
import subprocess

def _workspace_root():
    _script_dir = os.path.dirname(os.path.abspath(__file__))
    return os.path.normpath(os.path.join(_script_dir, "..", "..", ".."))


def sha256_bytes(b: bytes) -> str:
    return hashlib.sha256(b).hexdigest()

def sha256_file(path: str) -> str:
    h = hashlib.sha256()
    try:
        with open(path, "rb") as f:
            while chunk := f.read(65536):
                h.update(chunk)
    except OSError:
        return "error"
    return h.hexdigest()

def get_git_info(cwd):
    try:
        head = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=cwd, text=True).strip()
    except Exception:
        head = "unknown"
    try:
        index_dig = subprocess.check_output(["git", "write-tree"], cwd=cwd, text=True).strip()
    except Exception:
        index_dig = "unknown"
    try:
        diff_bytes = subprocess.check_output(["git", "diff", "HEAD"], cwd=cwd)
        diff_sha = sha256_bytes(diff_bytes)
    except Exception:
        diff_sha = "unknown"
    return head, index_dig, diff_sha

def main():
    workspace = _workspace_root()

    # Git ls-files
    try:
        files_raw = subprocess.check_output(["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard"], cwd=workspace)
        files = [f.decode('utf-8') for f in files_raw.split(b'\0') if f]
    except Exception:
        files = []

    records = []

    # Filter files: exclude evidence and nested roots
    for path in files:
        if path.startswith(".omo/evidence/") or path.startswith("chromium/") or path.startswith("mail/") or path.startswith("maho/.tmp-Arc_Palette"):
            continue

        abs_path = os.path.join(workspace, path)
        if not os.path.exists(abs_path):
            continue

        st = os.lstat(abs_path)
        mode = oct(st.st_mode)[-4:]
        size = str(st.st_size)

        if os.path.islink(abs_path):
            target = os.readlink(abs_path)
            sha = target
            p_type = "symlink"
        else:
            sha = sha256_file(abs_path)
            p_type = "file"

        records.append(f"superproject\0{path}\0{p_type}\0{mode}\0{size}\0{sha}\0")

    # Add identity records for nested repos
    nested_repos = [
        (".", "superproject"),
        ("chromium/src", "chromium_src"),
        ("mail", "mail"),
        ("maho/.tmp-Arc_Palette", "tmp_arc")
    ]

    for rel_path, domain in nested_repos:
        repo_abs = os.path.join(workspace, rel_path)
        if os.path.exists(repo_abs):
            head, index_dig, diff_sha = get_git_info(repo_abs)
            records.append(f"{domain}\0{rel_path}\0git_identity\00000\00\0{head}:{index_dig}:{diff_sha}\0")

    # Sort and print NUL-delimited records
    records.sort()
    sys.stdout.write("".join(records))

if __name__ == "__main__":
    main()
