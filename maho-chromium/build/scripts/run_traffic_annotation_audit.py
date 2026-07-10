#!/usr/bin/env python3
import os
import sys
import subprocess

def main():
    if sys.platform == "darwin":
        print("Traffic annotation auditor is not supported on macOS (only Linux/Windows/Android/ChromeOS). Skipping check.")
        return 0
        
    script_dir = os.path.dirname(os.path.abspath(__file__))
    repo_root = os.path.abspath(os.path.join(script_dir, "..", "..", ".."))
    
    src_dir = os.path.join(repo_root, "chromium", "src")
    build_path = os.path.join(src_dir, "out", "Default")
    auditor_script = os.path.join(src_dir, "tools", "traffic_annotation", "scripts", "auditor", "auditor.py")
    
    print(f"Repo root: {repo_root}")
    print(f"Build path: {build_path}")
    print(f"Auditor script: {auditor_script}")
    
    # Configure PATH to include depot_tools
    env = os.environ.copy()
    depot_tools_paths = [
        os.path.join(src_dir, "third_party", "depot_tools"),
        os.path.expanduser("~/depot_tools"),
        "/Users/indo/depot_tools"
    ]
    current_path = env.get("PATH", "")
    valid_paths = [p for p in depot_tools_paths if os.path.exists(p)]
    if valid_paths:
        env["PATH"] = os.path.pathsep.join(valid_paths) + os.path.pathsep + current_path
        print(f"Added depot_tools to PATH: {valid_paths}")
    
    # Run the auditor script directly (since chrome target is already built)
    # Run with --test-only so it doesn't modify summary/annotations.xml
    cmd = [
        sys.executable,
        auditor_script,
        "--build-path", build_path,
        "--test-only",
        "maho/"
    ]
    print(f"Running: {' '.join(cmd)}")
    res = subprocess.run(cmd, cwd=src_dir, env=env)
    return res.returncode

if __name__ == "__main__":
    sys.exit(main())
