#!/usr/bin/env python3
# Copyright 2026 Maho Browser. All rights reserved.

"""Deterministic source contract checks for the browser-owned action marker."""

from __future__ import annotations

import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[3]
HEADER = ROOT / "maho-chromium/browser/ui/views/maho_action_marker_service.h"
SOURCE = ROOT / "maho-chromium/browser/ui/views/maho_action_marker_service.cc"
BUILD = ROOT / "maho-chromium/browser/BUILD.gn"


class ActionMarkerSourceContractTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.header = HEADER.read_text(encoding="utf-8")
        cls.source = SOURCE.read_text(encoding="utf-8")
        cls.build = BUILD.read_text(encoding="utf-8")
        cls.combined = cls.header + "\n" + cls.source

    def test_browser_owned_views_overlay_and_target_attribution(self) -> None:
        for token in (
            "content::WebContentsUserData<MahoActionMarkerService>",
            "contents_web_view()",
            "AddChildView",
            "primary_frame_id",
            "GetPrimaryMainFrame()",
            "GetActiveWebContents() == contents",
            "DidFinishNavigation",
            "OnVisibilityChanged",
            "WebContentsDestroyed",
        ):
            self.assertIn(token, self.combined)

    def test_non_dom_no_physical_pointer_and_screenshot_isolation(self) -> None:
        forbidden = (
            "ExecuteJavaScript",
            "ExecuteScript",
            "document.",
            "querySelector",
            "elementFromPoint",
            "SetCursorScreenPoint",
            "MoveCursorTo",
            "SendMouseMove",
            "CopyFromSurface",
            "CapturePage",
        )
        for token in forbidden:
            self.assertNotIn(token, self.combined, token)
        self.assertIn("Renderer/page screenshot capture therefore excludes it", self.header)

        synthesizer = (
            ROOT
            / "maho-chromium/browser/mcp/maho_mcp_input_synthesizer.cc"
        ).read_text(encoding="utf-8")
        self.assertIn("ForwardMouseEvent", synthesizer)
        self.assertIn("ShowForRevalidatedTarget", synthesizer)
        for token in (
            "SetCursorScreenPoint",
            "MoveCursorTo",
            "SendMouseMove",
        ):
            self.assertNotIn(token, synthesizer, token)

    def test_sensitive_a11y_and_reduced_motion_contract(self) -> None:
        for token in (
            "bool sensitive",
            "Sensitive targets deliberately use position only",
            "SetIsIgnored(true)",
            "SetCanProcessEventsWithinSubtree(false)",
            "Animation::PrefersReducedMotion()",
            "Animation::ShouldRenderRichAnimation()",
            "kMarkerLifetime = base::Milliseconds(700)",
        ):
            self.assertIn(token, self.combined)
        for secret_field in ("selector", "accessible_name", "value", "tool_arguments"):
            self.assertIsNone(
                re.search(rf"\bstd::string\s+{secret_field}\b", self.header),
                secret_field,
            )

    def test_reading_disclosure_has_no_spatial_marker(self) -> None:
        self.assertIn("OperationGetsSpatialMarker", self.combined)
        self.assertIn("return !is_reading && action_kind.has_value();", self.source)

    def test_narrow_source_and_test_targets_are_wired(self) -> None:
        self.assertIn('source_set("maho_action_marker_headers")', self.build)
        self.assertIn('test("maho_action_marker_test")', self.build)
        for filename in (
            "ui/views/maho_action_marker_service.cc",
            "ui/views/maho_action_marker_service.h",
            "ui/views/maho_action_marker_service_unittest.cc",
        ):
            self.assertIn(filename, self.build)


if __name__ == "__main__":
    unittest.main()
