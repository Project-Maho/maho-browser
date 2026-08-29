import tempfile
import unittest
from pathlib import Path
from unittest import mock

from apply_chromium_src_overrides import (
    _CHROME_BROWSER_UI_DEPS,
    _CHROME_BROWSER_UI_MAC_SOURCES,
    _CHROME_BROWSER_UI_SOURCES,
    _PINNED_CHROMIUM_REVISION,
    _PINNED_INCOMPATIBLE_TARGETS,
    REPLACEMENTS,
    apply_replacements,
    chromium_revision,
    is_revision_incompatible_target,
    normalize_chrome_browser_build,
    normalize_chrome_browser_ui_build,
)


class OverlayBuildNormalizersTest(unittest.TestCase):
    def test_ui_normalizer_is_complete_and_idempotent(self):
        source_anchor = '    "accelerator_utils.h",\n'
        dep_anchor = (
            '      "//chrome/browser/ui/side_panel:'
            'side_panel_views_dependent",\n'
        )
        original = source_anchor + dep_anchor

        normalized, first_count = normalize_chrome_browser_ui_build(original)
        repeated, second_count = normalize_chrome_browser_ui_build(normalized)

        self.assertGreater(first_count, 0)
        self.assertEqual(second_count, 0)
        self.assertEqual(repeated, normalized)
        for entry in (
            *_CHROME_BROWSER_UI_SOURCES,
            *_CHROME_BROWSER_UI_MAC_SOURCES,
            *_CHROME_BROWSER_UI_DEPS,
        ):
            self.assertEqual(normalized.count(f'"{entry}"'), 1, entry)

    def test_browser_normalizer_is_complete_and_idempotent(self):
        original = (
            '    "//components/password_manager/core/browser",\n'
            '    "//chrome/browser/ui/browser_window",\n'
            '    "chrome_browser_field_trials.cc",\n'
        )

        normalized, first_count = normalize_chrome_browser_build(original)
        repeated, second_count = normalize_chrome_browser_build(normalized)

        self.assertGreater(first_count, 0)
        self.assertEqual(second_count, 0)
        self.assertEqual(repeated, normalized)
        for entry in (
            "//maho/browser:maho_url_scheme",
            "//maho/browser:maho_control_activity_service",
            "//maho/browser/ui/views/boost:maho_boost_window",
            "//maho/browser/maho_tab_registry.cc",
            "//maho/browser/ui/views/side_panel:side_panel",
        ):
            self.assertEqual(normalized.count(f'"{entry}"'), 1, entry)

    def test_browser_replacements_are_idempotent(self):
        pristine = (
            '#include "chrome/browser/ui/browser.h"\n'
            '#include "maho/browser/maho_browser_main_extra_parts.h"\n'
            '#include "maho/browser/ui/views/peek/maho_peek_controller.h"\n'
        )
        replacements = [
            replacement
            for replacement in REPLACEMENTS["chrome/browser/ui/browser.cc"]
            if replacement.description == "Include Maho headers in browser.cc"
        ]
        self.assertEqual(len(replacements), 1)

        with tempfile.TemporaryDirectory() as temp_dir:
            target = Path(temp_dir) / "browser.cc"
            target.write_text(pristine)

            first = target.read_text()
            changed, _ = apply_replacements(
                target,
                replacements,
                relative_path="chrome/browser/ui/browser.cc",
            )

            self.assertFalse(changed)
            self.assertEqual(target.read_text(), first)

    def test_exact_pin_skips_only_incompatible_targets(self):
        incompatible = next(iter(_PINNED_INCOMPATIBLE_TARGETS))

        self.assertTrue(
            is_revision_incompatible_target(
                incompatible,
                _PINNED_CHROMIUM_REVISION,
            )
        )
        self.assertFalse(
            is_revision_incompatible_target(
                incompatible,
                "0123456789abcdef0123456789abcdef01234567",
            )
        )
        self.assertFalse(
            is_revision_incompatible_target(
                "chrome/browser/BUILD.gn",
                _PINNED_CHROMIUM_REVISION,
            )
        )

    def test_revision_lookup_fails_closed(self):
        with mock.patch(
            "apply_chromium_src_overrides.subprocess.run",
            side_effect=OSError("git unavailable"),
        ):
            with self.assertRaisesRegex(
                RuntimeError,
                "Unable to determine Chromium revision",
            ):
                chromium_revision(Path("/not-a-checkout"))

    def test_legacy_replacement_targets_remain_registered(self):
        self.assertEqual(len(_PINNED_INCOMPATIBLE_TARGETS), 47)
        for target in _PINNED_INCOMPATIBLE_TARGETS:
            self.assertIn(target, REPLACEMENTS)


if __name__ == "__main__":
    unittest.main()
