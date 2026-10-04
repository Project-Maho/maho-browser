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

    def test_ui_normalizer_registers_maho_toolbar_button_provider(self):
        # todo 4c: on an ee4bd9e9-shaped chrome/browser/ui/BUILD.gn excerpt
        # (source anchor present, provider entry absent), the normalizer must
        # insert both the .cc and .h Maho-owned sources exactly once, next to
        # the maho_contents_header_view entries it already carries.
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
            "//maho/browser/ui/views/sidebar/maho_toolbar_button_provider.cc",
            "//maho/browser/ui/views/sidebar/maho_toolbar_button_provider.h",
        ):
            self.assertEqual(normalized.count(f'"{entry}"'), 1, entry)

    def test_ui_normalizer_strips_retired_search_pill_sources(self):
        # The sidebar search pill was deleted; a BUILD.gn that the normalizer
        # previously populated (4-space entries) must lose both entries.
        source_anchor = '    "accelerator_utils.h",\n'
        dep_anchor = (
            '      "//chrome/browser/ui/side_panel:'
            'side_panel_views_dependent",\n'
        )
        pill = (
            '    "//maho/browser/ui/views/sidebar/maho_sidebar_'
            'search_view.cc",\n'
            '    "//maho/browser/ui/views/sidebar/maho_sidebar_'
            'search_view.h",\n'
        )
        # 6-space entries sit inside a nested sources block on 72f18f12.
        nested_pill = pill.replace('    "', '      "')
        normalized, _ = normalize_chrome_browser_ui_build(
            source_anchor + pill + dep_anchor + nested_pill)
        repeated, second_count = normalize_chrome_browser_ui_build(normalized)

        self.assertNotIn("maho_sidebar_" "search_view", normalized)
        self.assertNotIn("\n  \n", normalized)
        self.assertEqual(second_count, 0)
        self.assertEqual(repeated, normalized)

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
        replacements = [
            replacement
            for replacement in REPLACEMENTS["chrome/browser/ui/browser.cc"]
            if replacement.description == "Include Maho headers in browser.cc"
        ]
        self.assertEqual(len(replacements), 1)
        # The applied shape is the payload's own new text; reapplying must be a
        # no-op that reports "already" and leaves the file byte-identical.
        pristine = replacements[0].new

        with tempfile.TemporaryDirectory() as temp_dir:
            target = Path(temp_dir) / "browser.cc"
            target.write_text(pristine)

            first = target.read_text()
            changed, applied = apply_replacements(
                target,
                replacements,
                relative_path="chrome/browser/ui/browser.cc",
            )

            self.assertFalse(changed)
            self.assertEqual(
                applied, ["already: Include Maho headers in browser.cc"]
            )
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
        self.assertEqual(len(_PINNED_INCOMPATIBLE_TARGETS), 27)
        for target in _PINNED_INCOMPATIBLE_TARGETS:
            self.assertIn(target, REPLACEMENTS)


if __name__ == "__main__":
    unittest.main()
