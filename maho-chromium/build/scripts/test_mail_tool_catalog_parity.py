#!/usr/bin/env python3
"""Cross-language source contract for the Mail agent tool catalog."""

from __future__ import annotations

import re
import unittest
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
CPP_CATALOG = ROOT / "maho-chromium/browser/ai/maho_browser_capability_catalog.def"
CPP_AUTHORIZATION = ROOT / "maho-chromium/browser/ai/maho_mail_tool_authorization.h"
RUST_AUTHORIZATION = ROOT / "maho/crates/maho-agent/src/permission.rs"


@dataclass(frozen=True)
class CatalogEntry:
    canonical_id: str
    tool_name: str
    mutability: str
    surfaces: str


EXPECTED_DENIAL_REASONS = frozenset(
    {
        "mail_tool_unknown",
        "mail_feature_disabled",
        "mail_helper_starting",
        "mail_helper_unavailable",
        "mail_global_policy_denied",
        "mail_read_consent_required",
    }
)

EXPECTED_CANONICAL_IDS = {
    "mail_list_accounts": "mail.accounts.list",
    "mail_list_folders": "mail.folders.list",
    "mail_list_emails": "mail.emails.list",
    "mail_get_email": "mail.email.get",
    "mail_search_emails": "mail.emails.search",
    "mail_list_thread": "mail.thread.list",
    "mail_extract_otp": "mail.otp.extract",
    "mail_add_account": "mail.account.add",
    "mail_test_connection": "mail.connection.test",
    "mail_delete_account": "mail.account.delete",
    "mail_start_oauth": "mail.oauth.start",
    "mail_complete_oauth": "mail.oauth.complete",
    "mail_reconnect_account": "mail.account.reconnect",
    "mail_import_migration_archive": "mail.archive.import",
    "mail_send": "mail.send",
    "mail_save_draft": "mail.draft.save",
    "mail_update_draft": "mail.draft.update",
    "mail_queue_email": "mail.queue",
    "mail_flag": "mail.flag",
}

EXPECTED_SURFACES = {
    "mail_list_accounts": "DesktopAgentAndBrowserMcp",
    "mail_list_folders": "DesktopAgentAndBrowserMcp",
    "mail_list_emails": "DesktopAgentAndBrowserMcp",
    "mail_get_email": "DesktopAgentAndBrowserMcp",
    "mail_search_emails": "DesktopAgentAndBrowserMcp",
    "mail_list_thread": "DesktopAgentAndBrowserMcp",
    "mail_extract_otp": "BrowserMcp",
    "mail_add_account": "ControlPlane",
    "mail_test_connection": "ControlPlane",
    "mail_delete_account": "ControlPlane",
    "mail_start_oauth": "ControlPlane",
    "mail_complete_oauth": "ControlPlane",
    "mail_reconnect_account": "ControlPlane",
    "mail_import_migration_archive": "ControlPlane",
    "mail_send": "DesktopAgentAndBrowserMcp",
    "mail_save_draft": "DesktopAgentAndBrowserMcp",
    "mail_update_draft": "DesktopAgentAndBrowserMcp",
    "mail_queue_email": "BrowserMcp",
    "mail_flag": "BrowserMcp",
}


def _read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def _extract_block(source: str, start: str, end: str) -> str:
    start_offset = source.index(start)
    end_offset = source.index(end, start_offset)
    return source[start_offset:end_offset]


def _quoted_mail_values(source: str) -> set[str]:
    return set(re.findall(r'"(mail_[a-z0-9_]+)"', source))


def _cpp_tool_classes(source: str) -> dict[str, str]:
    read_block = _extract_block(source, "kMailReadTools =", "kMailWriteAccountTools")
    write_block = _extract_block(source, "kMailWriteAccountTools =", "enum class MailGlobalPolicy")
    return {
        **{tool: "read" for tool in _quoted_mail_values(read_block)},
        **{tool: "write_account" for tool in _quoted_mail_values(write_block)},
    }


def _rust_tool_classes(source: str) -> dict[str, str]:
    read_block = _extract_block(source, "MAIL_READ_TOOLS:", "MAIL_WRITE_ACCOUNT_TOOLS:")
    write_block = _extract_block(source, "MAIL_WRITE_ACCOUNT_TOOLS:", "pub struct MailAuthorizationState")
    return {
        **{tool: "read" for tool in _quoted_mail_values(read_block)},
        **{tool: "write_account" for tool in _quoted_mail_values(write_block)},
    }


def _catalog_entries(source: str) -> dict[str, CatalogEntry]:
    pattern = re.compile(
        r'^MAHO_BROWSER_CAPABILITY\('
        r'[^,]+,\s*"(?P<canonical_id>[^"]+)",\s*'
        r'"(?P<tool_name>mail_[a-z0-9_]+)",\s*Mail,\s*'
        r'(?P<mutability>ReadOnly|Mutable),\s*[^,]+,\s*[^,]+,\s*[^,]+,\s*'
        r'[^,]+,\s*MailBeta,\s*(?P<surfaces>[^,]+),',
        re.MULTILINE,
    )
    entries = {}
    for match in pattern.finditer(source):
        entry = CatalogEntry(
            canonical_id=match.group("canonical_id"),
            tool_name=match.group("tool_name"),
            mutability=match.group("mutability"),
            surfaces=match.group("surfaces").strip(),
        )
        if entry.tool_name in entries:
            raise AssertionError(f"duplicate Mail capability: {entry.tool_name}")
        entries[entry.tool_name] = entry
    return entries


def _authorization_reason_codes(
    source: str, start: str, end: str, non_denial: set[str] | None = None
) -> set[str]:
    block = _extract_block(source, start, end)
    reasons = set(re.findall(r'"(mail_[a-z0-9_]+)"', block))
    return reasons - (non_denial or set())


class MailToolCatalogParityTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.cpp_catalog = _catalog_entries(_read(CPP_CATALOG))
        cls.cpp_source = _read(CPP_AUTHORIZATION)
        cls.rust_source = _read(RUST_AUTHORIZATION)
        cls.cpp_classes = _cpp_tool_classes(cls.cpp_source)
        cls.rust_classes = _rust_tool_classes(cls.rust_source)

    def test_cpp_and_rust_authorization_catalogs_have_exact_class_parity(self) -> None:
        self.assertEqual(self.cpp_classes, self.rust_classes)
        self.assertEqual(set(self.cpp_classes), set(self.cpp_catalog))

    def test_canonical_names_and_routes_are_stable(self) -> None:
        self.assertEqual(
            {name: entry.canonical_id for name, entry in self.cpp_catalog.items()},
            EXPECTED_CANONICAL_IDS,
        )
        self.assertEqual(
            {name: entry.surfaces for name, entry in self.cpp_catalog.items()},
            EXPECTED_SURFACES,
        )
        self.assertEqual(len(set(EXPECTED_CANONICAL_IDS.values())), len(EXPECTED_CANONICAL_IDS))

    def test_catalog_mutability_does_not_underclassify_authorized_reads(self) -> None:
        # Account connection testing is intentionally approval-gated despite
        # being non-mutating; all other ReadOnly capabilities must be reads.
        exceptions = {"mail_test_connection"}
        read_only = {
            name for name, entry in self.cpp_catalog.items() if entry.mutability == "ReadOnly"
        }
        self.assertEqual(read_only - exceptions, {
            name for name, tool_class in self.cpp_classes.items() if tool_class == "read"
        })
        self.assertEqual(
            {name for name in exceptions if self.cpp_classes.get(name) == "write_account"},
            exceptions,
        )

    def test_cross_language_denial_reason_codes_are_stable(self) -> None:
        cpp_reasons = _authorization_reason_codes(
            self.cpp_source,
            "AuthorizeMailTool(",
            "}  // namespace maho::ai",
            {"mail_read_allowed", "mail_typed_approval_required"},
        )
        rust_reasons = _authorization_reason_codes(
            self.rust_source, "pub fn authorize_mail_tool(", "pub struct PermissionRequest"
        )
        self.assertEqual(cpp_reasons, EXPECTED_DENIAL_REASONS)
        self.assertEqual(rust_reasons, EXPECTED_DENIAL_REASONS)


if __name__ == "__main__":
    unittest.main()
