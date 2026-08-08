#!/usr/bin/env python3
import argparse, subprocess, sys, os

p = argparse.ArgumentParser()
p.add_argument("--input", required=True)
p.add_argument("--output", required=True)
p.add_argument("--cwd", required=False)  # Ignore, resolve dynamically
p.add_argument("--minify", action="store_true")
p.add_argument("--depfile", required=False)
args = p.parse_args()

_SCRIPT_DIR = os.path.dirname(os.path.realpath(__file__))
_WORKSPACE_ROOT = os.path.normpath(os.path.join(_SCRIPT_DIR, '..', '..', '..'))
_WEBSITE_ROOT = os.path.join(_WORKSPACE_ROOT, 'website')

if args.depfile:
    input_dir = os.path.dirname(os.path.abspath(args.input))
    react_dir = os.path.abspath(os.path.join(input_dir, '..'))
    maho_common_dir = os.path.abspath(os.path.join(react_dir, '..', '..', 'maho_common', 'react'))
    
    files = []
    for root, _, filenames in os.walk(react_dir):
        if '__tests__' in root or 'node_modules' in root:
            continue
        for f in filenames:
            if f.endswith(('.ts', '.tsx', '.css')):
                files.append(os.path.join(root, f))
                
    if os.path.exists(maho_common_dir):
        for root, _, filenames in os.walk(maho_common_dir):
            if '__tests__' in root or 'node_modules' in root:
                continue
            for f in filenames:
                if f.endswith(('.ts', '.tsx', '.css')):
                    files.append(os.path.join(root, f))
                    
    dep_content = f"{args.output}:"
    for f in sorted(files):
        dep_content += f" {f}"
    dep_content += "\n"
    
    with open(args.depfile, 'w') as df:
        df.write(dep_content)

tailwind_bin = os.path.abspath(os.path.join(_WEBSITE_ROOT, "node_modules/.bin/tailwindcss"))
cmd = [
    "bun", tailwind_bin,
    "-i", os.path.abspath(args.input),
    "-o", os.path.abspath(args.output),
]
if args.minify:
    cmd.append("--minify")

env = os.environ.copy()
workspace_node_modules = os.path.join(_WORKSPACE_ROOT, "node_modules")
env["NODE_PATH"] = os.pathsep.join(
    filter(None, [workspace_node_modules, env.get("NODE_PATH")]))

sys.exit(subprocess.call(cmd, cwd=_WEBSITE_ROOT, env=env))
