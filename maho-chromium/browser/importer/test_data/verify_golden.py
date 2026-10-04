#!/usr/bin/env python3
import os
import sys
import argparse
import hashlib
import json

def compute_sha256(filepath):
    h = hashlib.sha256()
    try:
        with open(filepath, 'rb') as f:
            for chunk in iter(lambda: f.read(65536), b''):
                h.update(chunk)
        return h.hexdigest()
    except Exception as e:
        print(f"Error reading file {filepath}: {e}")
        return None

def main():
    parser = argparse.ArgumentParser(description="Verify integrity of synthetic test fixtures.")
    parser.add_argument('--stamp', help="Path to write the stamp file upon success.")
    parser.add_argument('--synthetic-dir', help="Override path to synthetic fixtures directory.")
    parser.add_argument('--hashes-file', help="Override path to golden_hashes.json.")
    args = parser.parse_args()

    script_dir = os.path.dirname(os.path.abspath(__file__))
    
    synthetic_dir = args.synthetic_dir or os.path.join(script_dir, 'synthetic')
    hashes_file = args.hashes_file or os.path.join(script_dir, 'golden_hashes.json')

    if not os.path.exists(hashes_file):
        print(f"Error: Hashes file not found at {hashes_file}", file=sys.stderr)
        return 1

    try:
        with open(hashes_file, 'r') as f:
            golden_hashes = json.load(f)
    except Exception as e:
        print(f"Error loading {hashes_file}: {e}", file=sys.stderr)
        return 1

    for filename, expected_hash in golden_hashes.items():
        filepath = os.path.join(synthetic_dir, filename)
        if not os.path.exists(filepath):
            print(f"Error: Required fixture missing: {filepath}", file=sys.stderr)
            return 1
        
        actual_hash = compute_sha256(filepath)
        if not actual_hash:
            return 1
            
        if actual_hash != expected_hash:
            print(f"Error: Hash mismatch for {filename}!", file=sys.stderr)
            print(f"  Expected: {expected_hash}", file=sys.stderr)
            print(f"  Actual:   {actual_hash}", file=sys.stderr)
            return 1

    print("All synthetic fixtures verified successfully.")

    if args.stamp:
        try:
            with open(args.stamp, 'w') as f:
                f.write('ok\n')
        except Exception as e:
            print(f"Error writing stamp file {args.stamp}: {e}", file=sys.stderr)
            return 1

    return 0

if __name__ == '__main__':
    sys.exit(main())
