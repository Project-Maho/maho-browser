#!/usr/bin/env python3
"""Install or verify the CLI's browser-produced capability projection snapshot.

The input is the exact JSON result of the browser's ``tools/list`` method. This
script deliberately does not synthesize descriptors or schemas from the C++
catalog; the browser remains the sole projection owner.
"""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path
from typing import Any

CRATE_ROOT = Path(__file__).resolve().parents[1]
WORKSPACE_ROOT = CRATE_ROOT.parents[2]
DEFAULT_SNAPSHOT = CRATE_ROOT / "src" / "tool_catalog_snapshot.json"
CANONICAL_CATALOG = (
    WORKSPACE_ROOT
    / "maho-chromium"
    / "browser"
    / "ai"
    / "maho_browser_capability_catalog.def"
)
CATALOG_ROW = re.compile(
    r'^MAHO_BROWSER_(?:ACTION_)?CAPABILITY\([^,]+,\s*"([^"]+)"',
    re.MULTILINE,
)


def canonical_ids() -> set[str]:
    source = CANONICAL_CATALOG.read_text(encoding="utf-8")
    return set(CATALOG_ROW.findall(source))


def normalize_discovery(value: Any) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise ValueError("browser discovery must be a JSON object")
    if "catalogDiagnostics" not in value and isinstance(value.get("result"), dict):
        value = value["result"]
    tools = value.get("tools")
    if "catalogDiagnostics" not in value or not isinstance(tools, list):
        raise ValueError("browser discovery omitted catalogDiagnostics or tools")
    projection_fields = (
        "name",
        "description",
        "inputSchema",
        "capabilityId",
        "schemaVersion",
        "resultVersion",
        "policy",
    )
    return {
        "catalogDiagnostics": value["catalogDiagnostics"],
        "tools": [
            {field: tool[field] for field in projection_fields if field in tool}
            for tool in tools
        ],
    }


def validate_snapshot(snapshot: dict[str, Any]) -> None:
    diagnostics = snapshot.get("catalogDiagnostics")
    tools = snapshot.get("tools")
    if not isinstance(diagnostics, dict) or not isinstance(tools, list):
        raise ValueError("snapshot must contain catalogDiagnostics and tools")

    expected_canonical_ids = canonical_ids()
    if diagnostics.get("canonicalCount") != len(expected_canonical_ids):
        raise ValueError(
            "canonicalCount drift: "
            f"snapshot={diagnostics.get('canonicalCount')} "
            f"catalog={len(expected_canonical_ids)}"
        )

    surfaces = diagnostics.get("surfaces")
    if not isinstance(surfaces, dict):
        raise ValueError("catalog diagnostics omitted surfaces")
    projected_canonical_ids: set[str] = set()
    for name, projection in surfaces.items():
        if not isinstance(projection, dict) or not isinstance(projection.get("ids"), list):
            raise ValueError(f"surface {name} omitted ids")
        ids = projection["ids"]
        if projection.get("count") != len(ids):
            raise ValueError(f"surface {name} count does not match ids")
        if ids != sorted(ids) or len(ids) != len(set(ids)):
            raise ValueError(f"surface {name} ids must be sorted and unique")
        projected_canonical_ids.update(ids)
    if projected_canonical_ids != expected_canonical_ids:
        missing = sorted(expected_canonical_ids - projected_canonical_ids)
        extra = sorted(projected_canonical_ids - expected_canonical_ids)
        raise ValueError(f"canonical ID drift: missing={missing}, extra={extra}")

    tool_ids: list[str] = []
    for tool in tools:
        tool_id = tool.get("capabilityId") if isinstance(tool, dict) else None
        if not isinstance(tool_id, str):
            raise ValueError("every projected tool must have a capabilityId")
        tool_ids.append(tool_id)
    if len(tool_ids) != len(set(tool_ids)):
        raise ValueError("projected tool capabilityIds must be unique")
    public_projection = surfaces.get("publicMcp")
    if not isinstance(public_projection, dict):
        raise ValueError("catalog diagnostics omitted publicMcp")
    if sorted(tool_ids) != public_projection.get("ids"):
        raise ValueError("tools do not match the canonical publicMcp ID projection")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "discovery",
        nargs="?",
        type=Path,
        help="JSON file containing the browser tools/list result",
    )
    parser.add_argument("--output", type=Path, default=DEFAULT_SNAPSHOT)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()

    source = args.output if args.check else args.discovery
    if source is None:
        parser.error("discovery is required unless --check is used")
    snapshot = normalize_discovery(json.loads(source.read_text(encoding="utf-8")))
    validate_snapshot(snapshot)

    rendered = json.dumps(snapshot, indent=2, ensure_ascii=True) + "\n"
    if args.check:
        if source.read_text(encoding="utf-8") != rendered:
            raise ValueError("snapshot is not normalized; regenerate it")
        return 0
    args.output.write_text(rendered, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
