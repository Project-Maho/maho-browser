import tempfile
import unittest
from pathlib import Path

from apply_chromium_src_overrides import REPLACEMENTS, apply_replacements


_BROWSER_BUILD_PATH = "chrome/browser/BUILD.gn"
_APP_CONTROLLER_HEADER_PATH = "chrome/browser/app_controller_mac.h"
_APP_CONTROLLER_MM_PATH = "chrome/browser/app_controller_mac.mm"
_BROWSER_VIEW_CC_PATH = "chrome/browser/ui/views/frame/browser_view.cc"
_TOOLBAR_VIEW_CC_PATH = "chrome/browser/ui/views/toolbar/toolbar_view.cc"
_SIDE_PANEL_CC_PATH = "chrome/browser/ui/views/side_panel/side_panel.cc"
_CA_LAYER_TREE_COORDINATOR_PATH = "ui/accelerated_widget_mac/ca_layer_tree_coordinator.mm"


class ApplyChromiumSrcOverridesTest(unittest.TestCase):
    def test_toolbar_minimum_size_tolerates_preinit_overflow_button(self):
        source = (
            "  if (media_button) {\n"
            "    media_button_ = AddChildView(std::move(media_button));\n"
            "  }\n"
            "      if (browser_->is_type_normal() && !overflow_button_->GetVisible()) {\n"
            "        size.Enlarge(1, 0);\n"
            "      }\n"
        )

        with tempfile.TemporaryDirectory() as tmp_dir:
            target = Path(tmp_dir) / "toolbar_view.cc"
            target.write_text(source)

            changed, _ = apply_replacements(
                target,
                REPLACEMENTS[_TOOLBAR_VIEW_CC_PATH],
                relative_path=_TOOLBAR_VIEW_CC_PATH,
            )

            self.assertTrue(changed)
            self.assertIn(
                "browser_->is_type_normal() && overflow_button_ &&",
                target.read_text(),
            )

    def test_macos_remote_compositor_preserves_transparent_web_contents_alpha(self):
        source = (
            "  if (allow_remote_layers_) {\n"
            "    root_ca_layer_ = [[CALayer alloc] init];\n"
            "    root_ca_layer_.opaque = YES;\n"
            "  }\n"
        )

        with tempfile.TemporaryDirectory() as tmp_dir:
            target = Path(tmp_dir) / "ca_layer_tree_coordinator.mm"
            target.write_text(source)

            changed, _ = apply_replacements(
                target,
                REPLACEMENTS[_CA_LAYER_TREE_COORDINATOR_PATH],
                relative_path=_CA_LAYER_TREE_COORDINATOR_PATH,
            )

            self.assertTrue(changed)
            output = target.read_text()
            self.assertIn("root_ca_layer_.opaque = NO;", output)
            self.assertNotIn("root_ca_layer_.opaque = YES;", output)

    def test_macos_reopen_activates_welcome_before_creating_browser(self):
        replacements = REPLACEMENTS[_APP_CONTROLLER_MM_PATH]

        include_patch = next(
            item
            for item in replacements
            if item.description
            == "Include the Maho welcome window for native URL login-gate presentation"
        )
        self.assertIn(
            '#include "maho/browser/ui/views/welcome/maho_welcome_window.h"',
            include_patch.new,
        )

        reopen_patch = next(
            item
            for item in replacements
            if item.description
            == "Activate the Maho welcome window before macOS creates a browser"
        )
        self.assertIn("MahoWelcomeWindow::ActivateIfOpen()", reopen_patch.new)
        self.assertLess(
            reopen_patch.new.index("MahoWelcomeWindow::ActivateIfOpen()"),
            reopen_patch.new.index("GetStartupProfilePathMac()"),
        )

    def test_shared_glass_patch_uses_blank_page_state_not_tab_count(self):
        replacements = REPLACEMENTS[_BROWSER_VIEW_CC_PATH]
        replacement = next(
            item for item in replacements
            if item.description.startswith(
                "Maho: define BrowserView content-surface palette projection")
        )

        self.assertIn("MahoShouldRevealSharedGlass", replacement.new)
        self.assertIn("url.is_empty()", replacement.new)
        self.assertIn('url.SchemeIs("about") && url.path() == "blank"',
                      replacement.new)
        self.assertIn("chrome::kChromeUINewTabHost", replacement.new)
        self.assertNotIn("tab_strip_model()->count() == 0", replacement.new)
        self.assertIn("SetMahoBackgroundOverride(std::nullopt)", replacement.new)
        self.assertIn("SetPageBaseBackgroundColor(SK_ColorTRANSPARENT)",
                      replacement.new)

    def test_shared_glass_view_is_browser_owned_and_spans_all_three_panes(self):
        replacements = REPLACEMENTS[_BROWSER_VIEW_CC_PATH]
        rendered = "\n".join(item.new for item in replacements)

        self.assertIn(
            "maho_content_gradient_view_ = AddChildView(\n"
            "        std::make_unique<maho::MahoContentGradientView>());",
            rendered,
        )
        self.assertNotIn(
            "maho_content_gradient_view_ = contents_container->AddChildView(",
            rendered,
        )
        self.assertIn("SetSharedBrowserGlassActive(true)", rendered)

        layout_replacements = REPLACEMENTS[
            "chrome/browser/ui/views/frame/layout/browser_view_tabbed_layout_impl.cc"
        ]
        layout_rendered = "\n".join(item.new for item in layout_replacements)
        self.assertIn("shared_glass_bounds.Union(*panel_bounds)", layout_rendered)
        self.assertIn("shared_glass_bounds.Union(contents_bounds)", layout_rendered)
        self.assertIn("layout.AddChild(views().maho_content_gradient_view",
                      layout_rendered)

    def test_maho_ai_content_parent_is_transparent(self):
        rendered = "\n".join(
            item.new for item in REPLACEMENTS[_SIDE_PANEL_CC_PATH]
        )
        self.assertIn("MahoAiSidePanelWebView", rendered)
        self.assertIn("return;", rendered)

    def test_url_alias_dep_uses_context_menus_when_password_provider_is_absent(self):
        source = (
            'static_library("browser") {\n'
            '  public_deps = [\n'
            '      "//maho/browser:maho_browser_main_extra_parts",\n'
            '      "//maho/browser:maho_context_menus",\n'
            '      "//maho/browser:maho_os_crypt_key_provider",\n'
            '  ]\n'
            '}\n'
        )

        with tempfile.TemporaryDirectory() as tmp_dir:
            target = Path(tmp_dir) / "BUILD.gn"
            target.write_text(source)

            changed, _ = apply_replacements(
                target,
                REPLACEMENTS[_BROWSER_BUILD_PATH],
                relative_path=_BROWSER_BUILD_PATH,
            )
            output = target.read_text()

            self.assertTrue(changed)
            self.assertIn(
                '      "//maho/browser:maho_context_menus",\n'
                '      "//maho/browser:maho_url_scheme",\n',
                output,
            )

            changed_again, _ = apply_replacements(
                target,
                REPLACEMENTS[_BROWSER_BUILD_PATH],
                relative_path=_BROWSER_BUILD_PATH,
            )
            self.assertFalse(changed_again)
            self.assertEqual(target.read_text(), output)

    def test_url_alias_dep_recognizes_existing_password_provider_output(self):
        source = (
            'static_library("browser") {\n'
            '  public_deps = [\n'
            '      "//maho/browser:maho_context_menus",\n'
            '      "//maho/browser:maho_password_provider",\n'
            '       "//maho/browser:maho_url_scheme",\n'
            '      "//maho/browser:maho_os_crypt_key_provider",\n'
            '  ]\n'
            '}\n'
        )

        with tempfile.TemporaryDirectory() as tmp_dir:
            target = Path(tmp_dir) / "BUILD.gn"
            target.write_text(source)

            changed, applied = apply_replacements(
                target,
                REPLACEMENTS[_BROWSER_BUILD_PATH],
                relative_path=_BROWSER_BUILD_PATH,
            )

            self.assertFalse(changed)
            self.assertEqual(
                applied,
                [
                    "already: Add Maho URL alias resolver directly to "
                    "chrome/browser:browser deps"
                ],
            )
            self.assertEqual(target.read_text(), source)

    def test_url_alias_dep_rejects_unknown_browser_dep_shape(self):
        source = (
            'static_library("browser") {\n'
            '  public_deps = [\n'
            '      "//maho/browser:maho_browser_main_extra_parts",\n'
            '      "//maho/browser:maho_os_crypt_key_provider",\n'
            '  ]\n'
            '}\n'
        )

        with tempfile.TemporaryDirectory() as tmp_dir:
            target = Path(tmp_dir) / "BUILD.gn"
            target.write_text(source)

            with self.assertRaisesRegex(
                RuntimeError,
                "Missing expected anchor for Add Maho URL alias resolver",
            ):
                apply_replacements(
                    target,
                    REPLACEMENTS[_BROWSER_BUILD_PATH],
                    relative_path=_BROWSER_BUILD_PATH,
                )

            self.assertEqual(target.read_text(), source)

    def test_app_controller_header_queue_anchors_apply(self):
        source = (
            '#include "components/sessions/core/tab_restore_service_observer.h"\n'
            '\n'
            '#if defined(__OBJC__)\n'
            'void AllowApplicationToTerminate();\n'
        )

        with tempfile.TemporaryDirectory() as tmp_dir:
            target = Path(tmp_dir) / "app_controller_mac.h"
            target.write_text(source)

            changed, _ = apply_replacements(
                target,
                REPLACEMENTS[_APP_CONTROLLER_HEADER_PATH],
                relative_path=_APP_CONTROLLER_HEADER_PATH,
            )

            self.assertTrue(changed)
            output = target.read_text()
            self.assertIn("#include <vector>\n", output)
            self.assertIn("class GURL;\n", output)
            self.assertIn(
                "std::vector<GURL> GetQueuedNativeUrlsForTesting(Profile* profile);\n",
                output,
            )

    def test_app_controller_header_queue_anchors_reject_missing_declaration_anchor(self):
        source = (
            '#include "components/sessions/core/tab_restore_service_observer.h"\n'
            '\n'
            '#if defined(__OBJC__)\n'
        )

        with tempfile.TemporaryDirectory() as tmp_dir:
            target = Path(tmp_dir) / "app_controller_mac.h"
            target.write_text(source)

            with self.assertRaisesRegex(
                RuntimeError,
                "Missing expected anchor for Expose the macOS deferred native URL queue API",
            ):
                apply_replacements(
                    target,
                    REPLACEMENTS[_APP_CONTROLLER_HEADER_PATH],
                    relative_path=_APP_CONTROLLER_HEADER_PATH,
                )

    def test_app_controller_mm_queue_anchors_apply_and_reject_misplacement(self):
        descriptions = {
            "Include map for AppController's deferred native URL queues",
            "Store deferred native URLs on AppController by original regular profile path",
            "Include the Maho welcome login-gate pref declaration",
            "Include the Maho welcome window for native URL login-gate presentation",
            "Route external single URL links to ATC Space or Maho Mini if configured, with global linkage",
            "Add AppController-owned deferred native URL queue operations",
            "Gate native URL dispatch while Maho login gate is active",
            "Expose ordered AppController native URL queue entries for tests",
            "Bridge deferred native URL queue operations through AppController",
            "Bridge ordered native URL queue entries through AppController for tests",
        }
        replacements = [
            replacement
            for replacement in REPLACEMENTS[_APP_CONTROLLER_MM_PATH]
            if replacement.description in descriptions
        ]
        source = "\n".join(replacement.old for replacement in replacements)

        with tempfile.TemporaryDirectory() as tmp_dir:
            target = Path(tmp_dir) / "app_controller_mac.mm"
            target.write_text(source)

            changed, _ = apply_replacements(
                target,
                replacements,
                relative_path=_APP_CONTROLLER_MM_PATH,
            )
            self.assertTrue(changed)
            self.assertIn(
                "queuedNativeUrlsForTestingForProfile",
                target.read_text(),
            )

            store_replacement = next(
                replacement
                for replacement in replacements
                if replacement.description
                == "Store deferred native URLs on AppController by original regular profile path"
            )
            misplaced_target = Path(tmp_dir) / "misplaced_app_controller_mac.mm"
            misplaced_target.write_text(
                "  BOOL _startupComplete;\n"
            )
            with self.assertRaisesRegex(
                RuntimeError,
                "Missing expected anchor for Store deferred native URLs",
            ):
                apply_replacements(
                    misplaced_target,
                    [store_replacement],
                    relative_path=_APP_CONTROLLER_MM_PATH,
                )

    def test_app_controller_mm_native_url_gate_precedes_both_create_paths(self):
        source = (
            '#import "chrome/browser/app_controller_mac.h"\n'
            '#include "maho/browser/maho_space_profile_bridge.h"\n'
            'void OpenUrlsInBrowserWithProfileNormal(const std::vector<GURL>& urls,\n'
            '                                        Profile* profile);\n'
            '\n'
            '}  // namespace\n'
            '\n'
            'void OpenUrlsInBrowserWithProfile(const std::vector<GURL>& urls,\n'
            '                                  Profile* profile) {\n'
            '  if (!profile)\n'
            '    return;  // No suitable profile to open the URLs, do nothing.\n'
            '\n'
            '  if (urls.size() == 1) {\n'
            '    GURL url = urls[0];\n'
            '    base::WeakPtr<Profile> weak_profile = profile->GetWeakPtr();\n'
            '    maho::DecideLinkDestination(\n'
            '        url, /*is_external=*/true,\n'
            '        base::BindOnce(\n'
            '            [](base::WeakPtr<Profile> profile, GURL url, maho::LinkDestinationResult result) {\n'
            '              if (!profile) return;\n'
            '              if (result.type == maho::LinkDestinationType::kSpace && !result.space_id.empty()) {\n'
            '                Browser* browser = chrome::FindTabbedBrowser(profile.get(), false);\n'
            '                if (!browser) {\n'
            '                  browser = Browser::Create(Browser::CreateParams(profile.get(), true));\n'
            '                }\n'
            '              } else {\n'
            '                std::vector<GURL> urls_fallback = {url};\n'
            '                OpenUrlsInBrowserWithProfileNormal(urls_fallback, profile.get());\n'
            '              }\n'
            '            },\n'
            '            weak_profile, url));\n'
            '    return;\n'
            '  }\n'
            '  OpenUrlsInBrowserWithProfileNormal(urls, profile);\n'
            '}\n'
            '\n'
            'namespace {\n'
            '\n'
            'void OpenUrlsInBrowserWithProfileNormal(const std::vector<GURL>& urls,\n'
            '                                        Profile* profile) {\n'
            '  Browser::Create(Browser::CreateParams(profile, true));\n'
            '}\n'
            '\n'
            '- (void)drainQueuedNativeUrlsAfterMahoWelcomeForProfile:(Profile*)profile {\n'
            '  if (!profile) {\n'
            '    return;\n'
            '  }\n'
            '  Profile* regular_profile = profile->GetOriginalProfile();\n'
            '  if (regular_profile->GetPrefs()->GetBoolean(\n'
            '          maho::welcome::kLoginGateActive)) {\n'
            '    return;\n'
            '  }\n'
            '  // Todo 3 takes this profile\'s FIFO batch and dispatches it through the\n'
            '  // existing native URL path after successful completion.\n'
            '}\n'
            '\n'
            '@end  // @implementation AppController\n'
            '\n'
            '//---------------------------------------------------------------------------\n'
        )
        descriptions = {
            "Include the Maho welcome login-gate pref declaration",
            "Include the Maho welcome window for native URL login-gate presentation",
            "Gate native URL dispatch while Maho login gate is active",
            "Drain deferred native URLs through the existing dispatcher after the login gate releases",
        }
        replacements = [
            replacement
            for replacement in REPLACEMENTS[_APP_CONTROLLER_MM_PATH]
            if replacement.description in descriptions
        ]

        with tempfile.TemporaryDirectory() as tmp_dir:
            target = Path(tmp_dir) / "app_controller_mac.mm"
            target.write_text(source)

            changed, _ = apply_replacements(
                target,
                replacements,
                relative_path=_APP_CONTROLLER_MM_PATH,
            )

            self.assertTrue(changed)
            output = target.read_text()
            guard_index = output.index("QueueNativeUrlsWhileMahoLoginGated")
            destination_create_index = output.index("Browser::Create(Browser::CreateParams(profile.get(), true))")
            fallback_create_index = output.index("Browser::Create(Browser::CreateParams(profile, true))")
            self.assertLess(guard_index, destination_create_index)
            self.assertLess(guard_index, fallback_create_index)
            self.assertLess(
                guard_index,
                output.index("maho::DecideLinkDestination"),
            )
            self.assertIn("MahoWelcomeWindow::Show(profile)", output)
            self.assertIn("_mahoNativeUrlsQueuedWhileLoginGated.erase(it)", output)
            self.assertIn("OpenUrlsInBrowserWithProfile(urls, profile)", output)

    def test_app_controller_mm_native_url_gate_rejects_missing_or_misplaced_anchor(self):
        replacement = next(
            replacement
            for replacement in REPLACEMENTS[_APP_CONTROLLER_MM_PATH]
            if replacement.description
            == "Gate native URL dispatch while Maho login gate is active"
        )

        with tempfile.TemporaryDirectory() as tmp_dir:
            target = Path(tmp_dir) / "app_controller_mac.mm"
            target.write_text(
                "void OpenUrlsInBrowserWithProfile(const std::vector<GURL>& urls,\n"
                "                                  Profile* profile) {\n"
                "  if (urls.size() == 1) {\n"
                "    return;\n"
                "  }\n"
                "}\n"
            )

            with self.assertRaisesRegex(
                RuntimeError,
                "Missing expected anchor for Gate native URL dispatch",
            ):
                apply_replacements(
                    target,
                    [replacement],
                    relative_path=_APP_CONTROLLER_MM_PATH,
                )


if __name__ == "__main__":
    unittest.main()
