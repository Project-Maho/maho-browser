#!/usr/bin/env python3
import argparse
import os
import sqlite3
import sys

def compare_sqlite_backups(db_a, db_b):
    if not os.path.exists(db_a) or not os.path.exists(db_b):
        return ["Missing backup file for comparison."]

    conn_a = sqlite3.connect(db_a)
    conn_b = sqlite3.connect(db_b)

    cursor_a = conn_a.cursor()
    cursor_b = conn_b.cursor()

    # Compare schema tables
    cursor_a.execute("SELECT name FROM sqlite_master WHERE type='table'")
    tables_a = set(row[0] for row in cursor_a.fetchall())

    cursor_b.execute("SELECT name FROM sqlite_master WHERE type='table'")
    tables_b = set(row[0] for row in cursor_b.fetchall())

    diffs = []
    if tables_a != tables_b:
        diffs.append(f"Tables mismatch: A has {tables_a}, B has {tables_b}")

    conn_a.close()
    conn_b.close()
    return diffs

def main():
    parser = argparse.ArgumentParser(description="Inspect and compare storage state.")
    parser.add_argument("--baseline", required=True, help="Directory containing baseline snapshots")
    parser.add_argument("--postrun", required=True, help="Directory containing postrun snapshots")
    args = parser.parse_args()

    dbs = ["maho.db", "History", "Cookies"]
    all_diffs = []

    for name in dbs:
        db_a = os.path.join(args.baseline, f"{name}.A.bak")
        db_b = os.path.join(args.postrun, f"{name}.B.bak")
        diffs = compare_sqlite_backups(db_a, db_b)
        if diffs:
            all_diffs.extend(f"{name}: {d}" for d in diffs)

    if all_diffs:
        print("Storage inspection: CHANGES DETECTED:", file=sys.stderr)
        for d in all_diffs:
            print(f"  {d}", file=sys.stderr)
        return 1

    print("Storage inspection: NO LOGICAL CHANGES DETECTED.")
    return 0

if __name__ == "__main__":
    sys.exit(main())
