#!/usr/bin/env python3
"""Validate machine-consumed desktop Mail contracts without a Chromium build."""

from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[3]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def fail(message: str) -> None:
    print(f"FAIL: {message}", file=sys.stderr)
    raise SystemExit(1)


def require(source: str, marker: str, contract: str) -> None:
    if marker not in source:
        fail(f"{contract} missing {marker!r}")


def quoted_tools(source: str, symbol: str) -> list[str]:
    match = re.search(
        rf"{symbol}[^=]*=\s*&?[\[\{{](.*?)[\]\}}];",
        source,
        re.S,
    )
    if not match:
        fail(f"cannot parse {symbol}")
    return re.findall(r'"(mail_[^"]+)"', match.group(1))


cpp_auth = read("maho-chromium/browser/ai/maho_mail_tool_authorization.h")
rust_auth = read("maho/crates/maho-agent/src/permission.rs")
for cpp_symbol, rust_symbol in (
    ("kMailReadTools", "MAIL_READ_TOOLS"),
    ("kMailWriteAccountTools", "MAIL_WRITE_ACCOUNT_TOOLS"),
):
    cpp_tools = quoted_tools(cpp_auth, cpp_symbol)
    rust_tools = quoted_tools(rust_auth, rust_symbol)
    if cpp_tools != rust_tools:
        fail(f"authorization catalog drift: {cpp_symbol} != {rust_symbol}")

reason_codes = {
    "mail_tool_unknown",
    "mail_feature_disabled",
    "mail_helper_starting",
    "mail_helper_unavailable",
    "mail_global_policy_denied",
    "mail_read_consent_required",
}
for reason in reason_codes:
    require(cpp_auth, f'"{reason}"', "C++ authorization")
    require(rust_auth, f'"{reason}"', "Rust authorization")
require(cpp_auth, '"mail_typed_approval_required"', "C++ authorization")
require(
    rust_auth,
    "MailToolClass::WriteAccount => MailAuthorizationAction::RequireApproval",
    "Rust typed Mail approval",
)

pgp = read("maho-chromium/browser/ui/webui/maho_mail/maho_mail_backend_boundary.cc")
require(pgp, 'command == "ExportPgpKey"', "PGP generic boundary")
require(pgp, '"code":"private_pgp_export_denied"', "PGP denial response")
require(pgp, "std::move(reply).Run(false, kPrivatePgpExportDenial);", "PGP denial callback")
require(pgp, "std::move(invoker).Run(key_id, false,", "public-only PGP export")

attachment = read("maho-chromium/browser/mail_helper/maho_mail_attachment_registry.cc")
for marker in (
    'constexpr char kInvalidToken[] = "invalid attachment capability";',
    'constexpr char kWrongProfile[] = "attachment capability profile mismatch";',
    'constexpr char kExpired[] = "attachment capability expired";',
    "Entry entry = std::move(it->second);",
    "entries_.erase(it);",
    "entry.profile_identity != requesting_profile_identity",
    "entry.expires_at <= now",
    "ReadFileIdentity(&staged_file, &actual_identity)",
    "CopyValidatedFile(&staged_file, canonical_launch_path",
):
    require(attachment, marker, "attachment capability boundary")

badge = read("maho-chromium/browser/mail_helper/maho_mail_badge.cc")
for marker in (
    'unread_count > 99',
    'u"99+"',
    'unread_count == 1 ? u" unread message"',
    'u" unread messages"',
):
    require(badge, marker, "native badge presentation")

notifications = read("maho-chromium/browser/mail_helper/maho_mail_notification_coordinator.cc")
for marker in (
    "permission != MailOsNotificationPermission::kGranted",
    "!behavior.desktop_notifications",
    "IsMuted(event.account_id, event.message_id, behavior)",
    "!IsReadyForGeneration(event.generation)",
    "event.cursor <= watermark->second.cursor",
    "pending_message_keys_.contains(message_key)",
):
    require(notifications, marker, "notification coordinator")

support = read("maho-chromium/browser/mail_helper/maho_mail_integration_test_support.h")
require(support, "class MailReplyBarrier", "service reply barrier")
if "MailEventDriver" in support or "GetFixedMailFixture" in support:
    fail("dead native Mail event/fixture model remains reachable")

print("PASS native Mail product contracts")
