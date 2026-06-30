#!/usr/bin/env python3
import argparse, subprocess, sys, os

p = argparse.ArgumentParser()
p.add_argument("--input", required=True)
p.add_argument("--output", required=True)
p.add_argument("--cwd", required=False)  # Ignore, resolve dynamically
p.add_argument("--minify", action="store_true")
args = p.parse_args()

_SCRIPT_DIR = os.path.dirname(os.path.realpath(__file__))
_WORKSPACE_ROOT = os.path.normpath(os.path.join(_SCRIPT_DIR, '..', '..', '..'))
_WEBSITE_ROOT = os.path.join(_WORKSPACE_ROOT, 'website')

tailwind_bin = os.path.abspath(os.path.join(_WEBSITE_ROOT, "node_modules/.bin/tailwindcss"))
cmd = [
    "bun", tailwind_bin,
    "-i", os.path.abspath(args.input),
    "-o", os.path.abspath(args.output),
]
if args.minify:
    cmd.append("--minify")

sys.exit(subprocess.call(cmd, cwd=_WEBSITE_ROOT))
