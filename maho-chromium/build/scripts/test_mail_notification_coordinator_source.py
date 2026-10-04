#!/usr/bin/env python3
"""No-build contract for Mail notification lifecycle and display ordering."""

from __future__ import annotations

import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[3]
HEADER = ROOT / "maho-chromium/browser/mail_helper/maho_mail_notification_coordinator.h"
SOURCE = ROOT / "maho-chromium/browser/mail_helper/maho_mail_notification_coordinator.cc"
PLATFORM = ROOT / "maho-chromium/browser/maho_browser_main_extra_parts.cc"
PERMISSION_HEADER = ROOT / "maho-chromium/browser/mail_helper/maho_mail_service.h"
PERMISSION_SOURCE = ROOT / "maho-chromium/browser/mail_helper/maho_mail_service.cc"
MAC_PERMISSION = ROOT / "maho-chromium/browser/mail_helper/maho_mail_notification_permission_mac.mm"
LINUX_PERMISSION = ROOT / "maho-chromium/browser/mail_helper/maho_mail_notification_permission.cc"
TEST = ROOT / "maho-chromium/browser/mail_helper/maho_mail_notification_coordinator_unittest.cc"
SETTINGS = ROOT / "maho-chromium/browser/ui/webui/maho_settings/maho_settings_page_handler.cc"


class MailNotificationCoordinatorSourceTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.header = HEADER.read_text(encoding="utf-8")
        cls.source = SOURCE.read_text(encoding="utf-8")
        cls.platform = PLATFORM.read_text(encoding="utf-8")
        cls.permission_header = PERMISSION_HEADER.read_text(encoding="utf-8")
        cls.permission_source = PERMISSION_SOURCE.read_text(encoding="utf-8")
        cls.mac_permission = MAC_PERMISSION.read_text(encoding="utf-8")
        cls.linux_permission = LINUX_PERMISSION.read_text(encoding="utf-8")
        cls.test = TEST.read_text(encoding="utf-8")
        cls.settings = SETTINGS.read_text(encoding="utf-8")

    def test_lifecycle_readiness_gates_display_and_click(self) -> None:
        self.assertIn("void OnLifecycleChanged(", self.header)
        self.assertGreaterEqual(self.source.count("IsReadyForGeneration("), 3)
        self.assertIn("state != MahoMailService::LifecycleState::kReady", self.source)
        self.assertIn("WithdrawAll();", self.source)

    def test_display_completion_precedes_durable_commit(self) -> None:
        self.assertIn("base::OnceCallback<void(bool)>", self.header)
        self.assertIn("pending_", self.header)
        self.assertIn("OnDisplayComplete", self.source)
        callback = self.source.index("display_callback_.Run")
        commit = self.source.index("ordering_watermarks_[", callback)
        self.assertLess(callback, commit)
        self.assertIn("notification_service->GetDisplayed", self.platform)
        self.assertIn("displayed.contains(notification_id)", self.platform)
        self.assertNotIn("std::move(completion).Run(true)", self.platform)

    def test_permission_admission_is_invalidated_before_reply(self) -> None:
        self.assertIn("uint64_t lifecycle_revision_", self.header)
        self.assertIn("uint64_t gate_revision_", self.header)
        self.assertIn("uint64_t gate_revision_", self.header)
        self.assertIn("account_revisions_", self.header)
        self.assertIn("++lifecycle_revision_;", self.source)
        self.assertIn("++gate_revision_;", self.source)
        self.assertIn("++gate_revision_;", self.source)
        self.assertIn("++account_revisions_[account_id];", self.source)
        self.assertIn(
            "lifecycle_revision != lifecycle_revision_",
            self.source,
        )
        self.assertIn("gate_revision != gate_revision_", self.source)
        self.assertIn(
            "PermissionReplyAfterDisableReenableCannotDisplayStaleMail",
            self.test,
        )
        self.assertIn(
            "PermissionReplyAfterAccountRemovalCannotDisplayStaleMail",
            self.test,
        )
        self.assertIn("gate_revision != gate_revision_", self.source)
        self.assertIn(
            "account_revision != account_revisions_[event.account_id]",
            self.source,
        )

    def test_deterministic_regressions_cover_required_races(self) -> None:
        for name in (
            "DisplayFailureDoesNotConsumeDurableOrdering",
            "PendingDisplayReservesEventUntilCallback",
            "SameGenerationLifecycleExitWithdrawsAndRegates",
            "StaleGenerationAndLateDisplaySuccessCannotReactivate",
        ):
            self.assertIn(name, self.test)
        self.assertNotIn("Sleep(", self.test)

    def test_mac_permission_queries_are_fresh_and_completion_based(self) -> None:
        self.assertIn(
            "base::OnceCallback<void(MailOsNotificationPermission)>",
            self.permission_header,
        )
        self.assertIn(
            "void GetMahoMailNotificationPermission(", self.permission_header
        )
        self.assertNotIn(
            "MailOsNotificationPermission GetMahoMailNotificationPermission()",
            self.permission_header,
        )
        self.assertIn("getNotificationSettingsWithCompletionHandler", self.mac_permission)
        self.assertIn("pending_status_callbacks", self.mac_permission)
        self.assertIn("settings_query_pending", self.mac_permission)
        self.assertNotIn("std::atomic<MailOsNotificationPermission>", self.mac_permission)
        self.assertNotIn("CurrentPermission()", self.mac_permission)
        self.assertIn(
            "GetMahoMailNotificationPermission(base::BindOnce(", self.settings
        )

    def test_linux_permission_provider_grants_notifications(self) -> None:
        self.assertIn(
            "MailOsNotificationPermission::kGranted",
            self.linux_permission,
        )
        self.assertNotIn(
            "MailOsNotificationPermission::kUnsupported",
            self.linux_permission,
        )

    def test_permission_change_regressions_and_prompt_serialization(self) -> None:
        for name in (
            "DeniedThenAuthorizedUsesFreshPermission",
            "AuthorizedThenDeniedUsesFreshPermission",
        ):
            self.assertIn(name, self.test)
        self.assertIn("pending_request_callbacks", self.mac_permission)
        self.assertIn("request_pending", self.mac_permission)
        self.assertIn("RefreshSettings", self.mac_permission)


if __name__ == "__main__":
    unittest.main()
