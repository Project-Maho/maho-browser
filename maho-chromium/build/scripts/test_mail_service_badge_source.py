#!/usr/bin/env python3
"""No-build contracts for Mail lifecycle and native badge wiring."""

from __future__ import annotations

import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[3]
SERVICE_HEADER = ROOT / "maho-chromium/browser/mail_helper/maho_mail_service.h"
SERVICE_SOURCE = ROOT / "maho-chromium/browser/mail_helper/maho_mail_service.cc"
DOCK_SOURCE = ROOT / "maho-chromium/browser/mail_helper/maho_mail_dock_badge_mac.mm"
SIDEBAR_SOURCE = ROOT / "maho-chromium/browser/ui/views/sidebar/maho_sidebar_top_bar_view.cc"
PAGE_HANDLER_SOURCE = (
    ROOT
    / "maho-chromium/browser/ui/webui/maho_mail/maho_mail_page_handler.cc"
)
BADGE_TEST = ROOT / "maho-chromium/browser/ui/views/sidebar/maho_sidebar_mail_badge_unittest.cc"
BROWSER_BUILD = ROOT / "maho-chromium/browser/BUILD.gn"
HELPER_BUILD = ROOT / "maho-chromium/browser/mail_helper/BUILD.gn"


class MailServiceBadgeSourceTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.header = SERVICE_HEADER.read_text(encoding="utf-8")
        cls.source = SERVICE_SOURCE.read_text(encoding="utf-8")
        cls.dock = DOCK_SOURCE.read_text(encoding="utf-8")
        cls.sidebar = SIDEBAR_SOURCE.read_text(encoding="utf-8")
        cls.page_handler = PAGE_HANDLER_SOURCE.read_text(encoding="utf-8")
        cls.badge_test = BADGE_TEST.read_text(encoding="utf-8")
        cls.browser_build = BROWSER_BUILD.read_text(encoding="utf-8")
        cls.helper_build = HELPER_BUILD.read_text(encoding="utf-8")

    def test_production_lifecycle_api_is_not_named_for_testing(self) -> None:
        self.assertIn("LifecycleState lifecycle_state() const", self.header)
        self.assertIn("uint64_t generation() const", self.header)
        production = "\n".join(
            path.read_text(encoding="utf-8")
            for path in (ROOT / "maho-chromium/browser").rglob("*.cc")
            if "unittest" not in path.name and "browsertest" not in path.name
        )
        self.assertNotIn("lifecycle_state_for_testing()", production)
        self.assertNotIn("generation_for_testing()", production)

    def test_badge_test_seam_binds_the_production_completion_path(self) -> None:
        seam = re.search(
            r"StartFolderBadgeRefreshForTesting\([^}]+?\n}", self.source, re.DOTALL
        )
        self.assertIsNotNone(seam)
        assert seam is not None
        self.assertIn("&MahoMailService::ApplyFolderBadgeReply", seam.group(0))
        self.assertNotIn("CompleteRefresh", seam.group(0))
        production_start = self.source.index("void MahoMailService::OnFoldersForBadge")
        production_end = self.source.index(
            "void MahoMailService::ApplyFolderBadgeReply", production_start
        )
        production = self.source[production_start:production_end]
        self.assertIn("ApplyFolderBadgeReply(", production)
        self.assertIn("UpdateUnreadCountPref();", self.source)

    def test_native_call_sites_use_the_shared_badge_resolver(self) -> None:
        self.assertIn("ResolveMailBadgePresentation(", self.dock)
        self.assertIn("ResolveMailBadgePresentation(", self.sidebar)
        self.assertNotIn("DockAndSidebarUseIdenticalFormattedValue", self.badge_test)

    def test_included_headers_are_declared_in_gn(self) -> None:
        self.assertIn('"ai/maho_mail_tool_authorization.h"', self.browser_build)
        self.assertGreaterEqual(
            self.helper_build.count('"maho_mail_helper_sandbox_linux.h"'), 2
        )

    def test_lifecycle_encoder_is_exhaustive_and_not_indexed(self) -> None:
        enum_match = re.search(
            r"enum class LifecycleState \{(.*?)\};",
            self.header,
            re.DOTALL,
        )
        self.assertIsNotNone(enum_match)
        assert enum_match is not None
        states = re.findall(r"\b(k[A-Z]\w*)\b", enum_match.group(1))
        encoder_match = re.search(
            r"const char\* EncodeMailLifecycleState\(.*?\n\}",
            self.page_handler,
            re.DOTALL,
        )
        self.assertIsNotNone(encoder_match)
        assert encoder_match is not None
        encoder = encoder_match.group(0)
        self.assertEqual(
            states,
            re.findall(r"case LifecycleState::(k[A-Z]\w*):", encoder),
        )
        self.assertNotIn("static_cast<size_t>", encoder)
        self.assertNotIn("default:", encoder)


if __name__ == "__main__":
    unittest.main()
