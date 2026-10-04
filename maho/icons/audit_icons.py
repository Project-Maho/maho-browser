#!/usr/bin/env python3
import os
import re
import sys


# ---------------------------------------------------------------------------
# Registry parser
# ---------------------------------------------------------------------------

def parse_registry(toml_path):
    """Return (names, views_icons_list, apple_map) from registry.toml.

    apple_map: {icon_name: apple_symbol}  (empty-string entries included so
               we can distinguish "not present" from "intentionally blank").
    views_icons_list: list of non-empty views values (for the Views coverage check).
    """
    names = []
    views_icons = []
    apple_map = {}

    if not os.path.exists(toml_path):
        print(f"Error: Registry file not found at {toml_path}")
        sys.exit(1)

    with open(toml_path, "r", encoding="utf-8") as f:
        content = f.read()

    current_name = None
    for line in content.splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("[[icon]]"):
            current_name = None
            continue

        name_match = re.match(r'^name\s*=\s*"([^"]+)"', line)
        if name_match:
            current_name = name_match.group(1)
            names.append(current_name)

        views_match = re.match(r'^views\s*=\s*"([^"]*)"', line)
        if views_match and current_name is not None:
            val = views_match.group(1).strip()
            if val:
                views_icons.append(val)

        apple_match = re.match(r'^apple\s*=\s*"([^"]*)"', line)
        if apple_match and current_name is not None:
            apple_map[current_name] = apple_match.group(1).strip()

    return names, views_icons, apple_map


# ---------------------------------------------------------------------------
# Views C++ usage scanner (unchanged from original)
# ---------------------------------------------------------------------------

def find_views_icon_usages(views_dir):
    used_icons = set()
    pattern = re.compile(r'\b(vector_icons::k[A-Za-z0-9_]+Icon|kArchiveboxIcon)\b')

    for root, _, files in os.walk(views_dir):
        for file in files:
            if not (file.endswith(".cc") or file.endswith(".h")):
                continue
            path = os.path.join(root, file)
            try:
                with open(path, "r", encoding="utf-8") as f:
                    for line in f:
                        stripped = line.strip()
                        if stripped.startswith("//") or stripped.startswith("/*"):
                            continue
                        if ("static constexpr gfx::VectorIconRep" in line or
                                "constexpr gfx::VectorIcon" in line):
                            continue
                        for match in pattern.findall(line):
                            used_icons.add(match)
            except Exception as e:
                print(f"Warning: Failed to read {path}: {e}")

    return used_icons


# ---------------------------------------------------------------------------
# MahoIcon.swift parser
# ---------------------------------------------------------------------------

def parse_maho_icon_swift(swift_path):
    """Return swift_apple_map: {icon_name (semantic, snake_case): symbol_string}.

    Strategy:
      1. Collect enum cases: 'case navBack = "nav_back"' → {navBack: "nav_back"}
      2. Collect switch arms: 'case .navBack: return "chevron.backward"' → {navBack: "chevron.backward"}
      3. Join on enum-case name → {semantic_name: symbol_string}
    """
    if not os.path.exists(swift_path):
        print(f"Error: MahoIcon.swift not found at {swift_path}")
        sys.exit(1)

    with open(swift_path, "r", encoding="utf-8") as f:
        content = f.read()

    # Map: swift_case_name → semantic_name (raw value)
    case_to_semantic = {}
    for m in re.finditer(r'\bcase\s+([A-Za-z][A-Za-z0-9]*)\s*=\s*"([^"]+)"', content):
        swift_case = m.group(1)
        semantic = m.group(2)
        case_to_semantic[swift_case] = semantic

    # Map: swift_case_name → symbol string from the symbolName switch
    case_to_symbol = {}
    for m in re.finditer(r'case\s+\.([A-Za-z][A-Za-z0-9]*):\s*return\s*"([^"]+)"', content):
        swift_case = m.group(1)
        symbol = m.group(2)
        case_to_symbol[swift_case] = symbol

    # Join: semantic_name → symbol
    swift_apple_map = {}
    for swift_case, semantic in case_to_semantic.items():
        if swift_case in case_to_symbol:
            swift_apple_map[semantic] = case_to_symbol[swift_case]

    return swift_apple_map


# ---------------------------------------------------------------------------
# Raw NSImage(systemSymbolName:) bypass scanner
# ---------------------------------------------------------------------------

def find_raw_symbol_bypasses(macos_shell_dir, wrapper_path):
    """Return list of (file_path, line_number, line_text) for every
    NSImage(systemSymbolName:) callsite found outside wrapper_path.

    Flags both string-literal and variable-argument callsites — any direct
    use of the AppKit initialiser outside the canonical MahoIcon wrapper is
    a bypass regardless of how the symbol name is supplied.

    Handles two patterns:
      1. Same-line:  NSImage(systemSymbolName: ...)
      2. Multi-line: NSImage(          <- line N
                        systemSymbolName: ...  <- line N+1

    Lines that are purely comments (// or /* leading content) are skipped
    to avoid false positives from documentation or commented-out code.
    For the multi-line case, comment-skipping is applied to the opening
    line (the line that contains NSImage(); the continuation line is not
    independently checked for comments because it can only match as a
    continuation after a non-comment opener).
    """
    wrapper_abspath = os.path.abspath(wrapper_path)
    single_line_pattern = re.compile(r'NSImage\s*\(\s*systemSymbolName\s*:')
    open_pattern = re.compile(r'NSImage\s*\(')
    label_pattern = re.compile(r'systemSymbolName\s*:')
    hits = []

    for root, _, files in os.walk(macos_shell_dir):
        for file in files:
            if not file.endswith(".swift"):
                continue
            path = os.path.join(root, file)
            if os.path.abspath(path) == wrapper_abspath:
                continue
            try:
                with open(path, "r", encoding="utf-8") as f:
                    lines = f.readlines()
                for lineno, line in enumerate(lines, start=1):
                    stripped = line.strip()
                    if stripped.startswith("//") or stripped.startswith("/*"):
                        continue
                    if single_line_pattern.search(line):
                        hits.append((path, lineno, line.rstrip()))
                        continue
                    if open_pattern.search(line):
                        next_index = lineno  # lineno is 1-based; lines[lineno] is the next line
                        if next_index < len(lines):
                            next_line = lines[next_index]
                            if label_pattern.search(next_line):
                                hits.append((path, lineno, line.rstrip()))
            except Exception as e:
                print(f"Warning: Failed to read {path}: {e}")

    return hits


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    workspace_root = os.path.abspath(os.path.join(script_dir, "..", ".."))

    toml_path = os.path.join(workspace_root, "maho", "icons", "registry.toml")
    views_dir = os.path.join(workspace_root, "maho-chromium", "browser", "ui", "views")
    swift_path = os.path.join(
        workspace_root,
        "maho", "macos-shell", "Sources", "Shared", "MahoIcon.swift",
    )
    macos_shell_dir = os.path.join(workspace_root, "maho", "macos-shell", "Sources")

    print("Running Maho Icon Registry Audit...")
    print()

    names, registry_views, registry_apple_map = parse_registry(toml_path)
    used_in_views = find_views_icon_usages(views_dir)
    swift_apple_map = parse_maho_icon_swift(swift_path)
    raw_bypasses = find_raw_symbol_bypasses(macos_shell_dir, swift_path)

    print(f"Registry entries parsed:         {len(names)}")
    print(f"Registry apple mappings:         {len(registry_apple_map)}")
    print(f"Registry views icons:            {len(registry_views)}")
    print(f"Swift wrapper entries:           {len(swift_apple_map)}")
    print(f"Unique icons used in Views code: {len(used_in_views)}")
    print(f"Raw NSImage bypasses:            {len(raw_bypasses)}")
    print()

    success = True

    # ------------------------------------------------------------------
    # Check 1 (original): Views code icons present in registry
    # ------------------------------------------------------------------
    registry_views_set = set(registry_views)
    missing_in_registry = used_in_views - registry_views_set
    unused_in_code = registry_views_set - used_in_views

    if missing_in_registry:
        print("[FAIL] Vector icons used in Views code but NOT in registry.toml:")
        for icon in sorted(missing_in_registry):
            print(f"  - {icon}")
        success = False
    else:
        print("[PASS] All Views vector icons are declared in registry.toml.")

    if unused_in_code:
        print("[INFO] Views icons registered but not referenced in Views code:")
        for icon in sorted(unused_in_code):
            print(f"  - {icon}")
    print()

    # ------------------------------------------------------------------
    # Check 2 (new): Registry <-> Swift wrapper drift
    # ------------------------------------------------------------------
    drift_errors = []
    missing_from_swift = []

    for icon_name, registry_symbol in registry_apple_map.items():
        if not registry_symbol:
            # Intentionally blank — no apple symbol defined; skip
            continue
        if icon_name not in swift_apple_map:
            missing_from_swift.append((icon_name, registry_symbol))
        elif swift_apple_map[icon_name] != registry_symbol:
            drift_errors.append((
                icon_name,
                registry_symbol,
                swift_apple_map[icon_name],
            ))

    if drift_errors:
        print("[FAIL] Registry <-> MahoIcon.swift symbol drift detected:")
        for icon_name, reg_sym, swift_sym in sorted(drift_errors):
            print(f"  {icon_name}: registry='{reg_sym}'  swift='{swift_sym}'")
        success = False
    else:
        print("[PASS] Registry apple symbols match MahoIcon.swift.")

    if missing_from_swift:
        print("[INFO] Registry icons with apple symbols missing from Swift wrapper:")
        for icon_name, reg_sym in sorted(missing_from_swift):
            print(f"  {icon_name}: registry='{reg_sym}'")
    print()

    # ------------------------------------------------------------------
    # Check 3 (new): Raw NSImage(systemSymbolName:) bypasses
    # ------------------------------------------------------------------
    if raw_bypasses:
        print("[FAIL] Raw NSImage(systemSymbolName:) callsites found outside MahoIcon.swift:")
        for path, lineno, text in sorted(raw_bypasses):
            rel = os.path.relpath(path, workspace_root)
            print(f"  {rel}:{lineno}: {text.strip()}")
        success = False
    else:
        print("[PASS] No raw NSImage(systemSymbolName:) bypasses outside MahoIcon.swift.")
    print()

    # ------------------------------------------------------------------
    # Final result
    # ------------------------------------------------------------------
    if not success:
        print("Audit FAILED — see above for details.")
        sys.exit(1)

    print("Audit completed successfully.")
    sys.exit(0)


if __name__ == "__main__":
    main()
