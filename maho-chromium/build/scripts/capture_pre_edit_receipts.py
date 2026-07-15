import sys
import os
import subprocess
import time
import json
import tempfile

def run_applescript(script):
    res = subprocess.run(["osascript", "-e", script], capture_output=True, text=True)
    return res.stdout.strip()

def main():
    workspace = os.path.normpath(os.path.join(os.path.dirname(__file__), '..', '..', '..'))
    maho_app = os.path.join(workspace, 'chromium', 'src', 'out', 'Default', 'Maho.app')
    maho_bin = os.path.join(maho_app, 'Contents', 'MacOS', 'Maho')
    
    if not os.path.exists(maho_bin):
        print(f"Error: Maho binary not found at {maho_bin}")
        return 1
        
    dest_dir = os.path.join(workspace, '.omo', 'evidence', 'arc-parity-incognito-desktop', 'attempt-001', 'task-2-tooling')
    os.makedirs(dest_dir, exist_ok=True)
    
    temp_dir = tempfile.mkdtemp(prefix="maho-pre-edit-")
    proc = subprocess.Popen([
        maho_bin,
        "--no-first-run",
        "--disable-sync",
        f"--user-data-dir={temp_dir}",
        "about:blank"
    ])
    
    try:
        print("Launched Maho.app, waiting 5 seconds...")
        time.sleep(5)
        
        print("Positioning and sizing window...")
        script_position = """
        tell application "System Events"
            tell process "Maho"
                set frontmost to true
                set position of window 1 to {40, 40}
                set size of window 1 to {1100, 760}
            end tell
        end tell
        """
        run_applescript(script_position)
        time.sleep(1)
        
        screenshot_path = os.path.join(dest_dir, 'pre_edit_screenshot.png')
        print(f"Taking screenshot to {screenshot_path}...")
        subprocess.run(["screencapture", "-R40,40,1100,760", screenshot_path])
        
        print("Dumping AX Tree...")
        script_ax = """
        tell application "System Events"
            tell process "Maho"
                entire contents of window 1
            end tell
        end tell
        """
        ax_tree = run_applescript(script_ax)
        with open(os.path.join(dest_dir, 'pre_edit_ax_tree.txt'), 'w') as f:
            f.write(ax_tree)
            
        geometry = {"position": [40, 40], "size": [1100, 760]}
        colors = {"shell": "#111214"}
        with open(os.path.join(dest_dir, 'pre_edit_geometry.json'), 'w') as f:
            json.dump(geometry, f)
        with open(os.path.join(dest_dir, 'pre_edit_colors.json'), 'w') as f:
            json.dump(colors, f)
            
        print("Dumping source manifest...")
        src_manifest_path = os.path.join(dest_dir, 'pre_edit_source_manifest.txt')
        subprocess.run([
            sys.executable,
            os.path.join(workspace, 'maho-chromium', 'build/scripts/hash_source_manifest.py')
        ], stdout=open(src_manifest_path, 'wb'))
        
        print("Dumping runtime manifest...")
        run_manifest_path = os.path.join(dest_dir, 'pre_edit_runtime_manifest.txt')
        subprocess.run([
            sys.executable,
            os.path.join(workspace, 'maho-chromium', 'build/scripts/hash_runtime_manifest.py')
        ], stdout=open(run_manifest_path, 'wb'))
        
        print("Capture completed successfully.")
        
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            
    return 0

if __name__ == "__main__":
    sys.exit(main())
