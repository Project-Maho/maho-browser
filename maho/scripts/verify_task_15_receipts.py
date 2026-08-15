#!/usr/bin/env python3
"""Source-only verifier for Todo 15 receipt projections."""

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]

REQUIRED = {
    "maho/crates/maho-types/src/tool.rs": [
        "BROWSER_RECEIPT_SCHEMA_VERSION",
        "BROWSER_RECEIPT_RESULT_VERSION",
        "BrowserReceiptProjection",
        '"arguments"',
        '"page_text"',
        '"screenshot"',
    ],
    "maho/crates/maho-browser-mcp/src/server.rs": [
        "project_external_result",
        '"mahoReceipt"',
        "CallToolResult::structured(value)",
        "with_meta",
    ],
    "maho/crates/maho-cli/src/browser.rs": [
        "project_cli_result",
        'get("_meta")',
        'get("structuredContent")',
        '"receipt": receipt',
    ],
    "maho-chromium/browser/resources/maho_ai/receipt-projection.ts": [
        "RECEIPT_SCHEMA_VERSION = 1",
        "RECEIPT_RESULT_VERSION = 1",
        "receiptFromToolOutput",
        "ReceiptOutcomeStatus",
        "PAGE_PAYLOAD_KEY_FRAGMENTS",
    ],
    "maho-chromium/browser/resources/maho_ai/views/conversation_thread.ts": [
        "receiptFromToolOutput",
        "receiptSummary(receipt)",
    ],
}

FORBIDDEN_EDITS = [
    "maho-chromium/browser/ai/maho_control_activity_service.cc",
    "maho-chromium/browser/ai/maho_control_activity_service.h",
    "maho-chromium/browser/ai/maho_unified_agent_adapter.cc",
    "maho-chromium/browser/ai/maho_unified_agent_adapter.h",
]


def fail(message: str) -> None:
    print(f"FAIL: {message}", file=sys.stderr)
    raise SystemExit(1)


for relative, needles in REQUIRED.items():
    path = ROOT / relative
    if not path.is_file():
        fail(f"missing {relative}")
    source = path.read_text(encoding="utf-8")
    for needle in needles:
        if needle not in source:
            fail(f"{relative} missing contract marker {needle!r}")

# Source-only scope: this task must not wire or alter Chromium activity/Agent
# producers. Git may contain pre-existing edits, so inspect this task's named
# implementation markers rather than asserting a globally clean worktree.
for relative in FORBIDDEN_EDITS:
    source = (ROOT / relative).read_text(encoding="utf-8")
    if "mahoReceipt" in source or "BrowserReceiptProjection" in source:
        fail(f"receipt consumer projection leaked into producer {relative}")

print("PASS task-15 receipt source contracts")
