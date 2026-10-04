#!/usr/bin/env python3
import argparse
import os
import shutil
import sqlite3
import sys

def backup_sqlite(db_path, backup_path):
    if not os.path.exists(db_path):
        return False
    try:
        conn = sqlite3.connect(db_path)
        backup_conn = sqlite3.connect(backup_path)
        with backup_conn:
            conn.backup(backup_conn)
        backup_conn.close()
        conn.close()
        return True
    except Exception as e:
        print(f"Error backing up {db_path}: {e}", file=sys.stderr)
        return False

def main():
    parser = argparse.ArgumentParser(description="Snapshot browser storage state.")
    parser.add_argument("--phase", required=True, choices=["A", "B"], help="Phase of storage snap (A = baseline, B = post-run)")
    parser.add_argument("--raw-output", required=True, help="Directory to save raw database backups")
    args = parser.parse_args()

    workspace = "/Users/indo/code/project/maho-workspace"
    profile_dir = os.path.join(workspace, "chromium/src/out/Default/profile") # default profile dir in E2E

    os.makedirs(args.raw_output, exist_ok=True)

    dbs = {
        "maho.db": os.path.join(profile_dir, "MahoCore/maho.db"),
        "History": os.path.join(profile_dir, "Default/History"),
        "Cookies": os.path.join(profile_dir, "Default/Cookies")
    }

    for name, db_path in dbs.items():
        if os.path.exists(db_path):
            backup_dest = os.path.join(args.raw_output, f"{name}.{args.phase}.bak")
            if backup_sqlite(db_path, backup_dest):
                print(f"Backed up {name} to {backup_dest}")
            else:
                print(f"Failed to backup {name} (it might be empty or locked)")
        else:
            print(f"DB {name} not found at {db_path}")

    print(f"Storage snapshot phase {args.phase} completed.")
    return 0

if __name__ == "__main__":
    sys.exit(main())
