import tempfile
import unittest
from pathlib import Path

from apply_chromium_src_overrides import REPLACEMENTS, apply_replacements


BROWSER_WIDGET_PATH = "chrome/browser/ui/views/frame/browser_widget.cc"


class SidebarThemeColorProviderTest(unittest.TestCase):
    def test_browser_widget_keeps_theme_service_mode_and_adds_only_otr_sentinel(self):
        source = (
            '#include "chrome/browser/ui/views/frame/browser_view.h"\n'
            "\n"
            "  const auto browser_color_scheme =\n"
            "      theme_service->GetBrowserColorScheme();\n"
            "  if (browser_color_scheme != ThemeService::BrowserColorScheme::kSystem) {\n"
            "    key.color_mode =\n"
            "        browser_color_scheme == ThemeService::BrowserColorScheme::kLight\n"
            "            ? ui::ColorProviderKey::ColorMode::kLight\n"
            "            : ui::ColorProviderKey::ColorMode::kDark;\n"
            "  }\n"
            "\n"
            "  // user_color.\n"
        )

        with tempfile.TemporaryDirectory() as tmp_dir:
            target = Path(tmp_dir) / "browser_widget.cc"
            target.write_text(source)

            changed, _ = apply_replacements(
                target,
                REPLACEMENTS[BROWSER_WIDGET_PATH],
                relative_path=BROWSER_WIDGET_PATH,
            )
            output = target.read_text()

            self.assertTrue(changed)
            self.assertIn("theme_service->GetBrowserColorScheme()", output)
            self.assertIn("key.app_controller = GetMahoOtrSentinel();", output)
            self.assertNotIn("ResolveSidebarDarkMode", output)
            self.assertNotIn("maho_space_theme_state.h", output)
            self.assertNotIn("ui/native_theme/native_theme.h", output)

            changed_again, _ = apply_replacements(
                target,
                REPLACEMENTS[BROWSER_WIDGET_PATH],
                relative_path=BROWSER_WIDGET_PATH,
            )
            self.assertFalse(changed_again)
            self.assertEqual(target.read_text(), output)

    def test_browser_widget_mismatched_anchor_fails_without_partial_write(self):
        source = (
            '#include "chrome/browser/ui/views/frame/browser_view.h"\n'
            '#include "maho/browser/ui/theme/maho_space_theme_state.h"\n'
            '#include "maho/browser/ui/theme/maho_color_mixer.h"\n'
            '#include "ui/native_theme/native_theme.h"\n'
            "\n"
            "  // Maho: drifted legacy Space override.\n"
            "  key.color_mode = MahoSpaceThemeState::ResolveSidebarDarkMode(os_dark)\n"
            "                       ? ui::ColorProviderKey::ColorMode::kDark\n"
            "                       : ui::ColorProviderKey::ColorMode::kLight;\n"
            "\n"
            "  // user_color.\n"
        )

        with tempfile.TemporaryDirectory() as tmp_dir:
            target = Path(tmp_dir) / "browser_widget.cc"
            target.write_text(source)

            with self.assertRaisesRegex(
                RuntimeError,
                "BrowserWidget Space-theme override drift",
            ):
                apply_replacements(
                    target,
                    REPLACEMENTS[BROWSER_WIDGET_PATH],
                    relative_path=BROWSER_WIDGET_PATH,
                )

            self.assertEqual(target.read_text(), source)

    def test_color_mixer_has_no_process_global_space_theme_recipe(self):
        mixer = (
            Path(__file__).resolve().parents[2]
            / "browser"
            / "ui"
            / "theme"
            / "maho_color_mixer.cc"
        ).read_text()

        self.assertNotIn("MahoSpaceThemeState::GetThemeData()", mixer)
        self.assertNotIn("Phase 3: Space theme tinting", mixer)
        self.assertNotIn("maho_space_theme_state.h", mixer)
        self.assertIn(
            "key.color_mode == ui::ColorProviderKey::ColorMode::kDark",
            mixer,
        )
        self.assertIn("AddChromeSurfaceColors(mixer, browser_theme_dark)", mixer)
        self.assertIn("AddPrivateColors(mixer)", mixer)


if __name__ == "__main__":
    unittest.main()
