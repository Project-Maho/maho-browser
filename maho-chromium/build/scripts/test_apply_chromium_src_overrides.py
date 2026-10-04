import subprocess
import tempfile
import unittest
from pathlib import Path

from apply_chromium_src_overrides import (
    _CHROMIUM_BRANDING_TARGETS,
    _MAHO_FOCUS_ACCELERATOR_REGISTRATION,
    _MAHO_SIDEBAR_RAIL_LAYOUT,
    _MAHO_SIDEBAR_RAIL_LAYOUT_V1,
    REPLACEMENTS,
    apply_base_patch,
    apply_replacements,
    base_patch_targets,
    normalize_peek_open_url_routing,
    normalize_startup_browser_creator_session_restore,
)


_BROWSER_BUILD_PATH = "chrome/browser/BUILD.gn"
_BROWSER_COMMAND_CONTROLLER_PATH = "chrome/browser/ui/browser_command_controller.cc"
_CHROME_PAGES_CC_PATH = "chrome/browser/ui/chrome_pages.cc"

_UPSTREAM_ADDED_TO_WIDGET = (
    "  views::ClientView::AddedToWidget();\n"
    "\n"
    "  widget_observation_.Observe(GetWidget());\n"
)

_UPSTREAM_SELECT_NUMBERED_TAB_CASE = (
    "    case IDC_SELECT_TAB_0:\n"
    "    case IDC_SELECT_TAB_1:\n"
    "    case IDC_SELECT_TAB_2:\n"
    "    case IDC_SELECT_TAB_3:\n"
    "    case IDC_SELECT_TAB_4:\n"
    "    case IDC_SELECT_TAB_5:\n"
    "    case IDC_SELECT_TAB_6:\n"
    "    case IDC_SELECT_TAB_7:\n"
    '      base::RecordAction(base::UserMetricsAction("Accel_SelectNumberedTab"));\n'
    "      SelectNumberedTab(\n"
    "          browser_, id - IDC_SELECT_TAB_0,\n"
    "          TabStripUserGestureDetails(\n"
    "              TabStripUserGestureDetails::GestureType::kKeyboard, time_stamp));\n"
    "      break;\n"
)

_UPSTREAM_SHOW_SETTINGS = (
    '#include "chrome/browser/ui/browser_navigator_params.h"\n'
    "\n"
    "void ShowSettings(BrowserWindowInterface* browser) {\n"
    "  ShowSettingsSubPage(browser, std::string());\n"
    "}\n"
)
_APP_CONTROLLER_HEADER_PATH = "chrome/browser/app_controller_mac.h"
_APP_CONTROLLER_MM_PATH = "chrome/browser/app_controller_mac.mm"
_BROWSER_VIEW_CC_PATH = "chrome/browser/ui/views/frame/browser_view.cc"
_BROWSER_UI_BUILD_PATH = "chrome/browser/ui/BUILD.gn"
_TOOLBAR_VIEW_CC_PATH = "chrome/browser/ui/views/toolbar/toolbar_view.cc"
_SIDE_PANEL_CC_PATH = "chrome/browser/ui/views/side_panel/side_panel.cc"
_CA_LAYER_TREE_COORDINATOR_PATH = "ui/accelerated_widget_mac/ca_layer_tree_coordinator.mm"
_PASSWORD_BACKEND_FACTORY_PATH = (
    "chrome/browser/password_manager/factories/password_store_backend_factory.cc"
)
_PASSWORD_BACKEND_FACTORY_BUILD_PATH = (
    "chrome/browser/password_manager/factories/BUILD.gn"
)
_STARTUP_BROWSER_CREATOR_IMPL_PATH = (
    "chrome/browser/ui/startup/startup_browser_creator_impl.cc"
)
_BROWSER_CC_PATH = "chrome/browser/ui/browser.cc"
_MAIN_MENU_BUILDER_PATH = "chrome/browser/ui/cocoa/main_menu_builder.mm"
_PERMISSION_PROMPT_FACTORY_PATH = (
    "chrome/browser/ui/views/permissions/permission_prompt_factory.cc"
)
_BUBBLE_ANCHOR_UTIL_PATH = "chrome/browser/ui/views/bubble_anchor_util_views.cc"

_UPSTREAM_IGNORE_EMPTY_OMNIBOX = (
    "  // Suppress permission prompts if the omnibox is being edited or is empty.\n"
    "  LocationBar* location_bar = GetLocationBar(browser);\n"
    "  bool can_display_prompt = !(location_bar && location_bar->IsEditingOrEmpty());\n"
)
_UPSTREAM_PAGE_INFO_ANCHOR_RECT = (
    "  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser);\n"
    "  // Get position in view (taking RTL UI into account).\n"
    "  int x_within_browser_view = browser_view->GetMirroredXInView(\n"
    "      bubble_anchor_util::kNoToolbarLeftOffset);\n"
)

_UPSTREAM_RUN_FILE_CHOOSER = (
    "void Browser::RunFileChooser(\n"
    "    content::RenderFrameHost* render_frame_host,\n"
    "    scoped_refptr<content::FileSelectListener> listener,\n"
    "    const blink::mojom::FileChooserParams& params) {\n"
    "  FileSelectHelper::RunFileChooser(render_frame_host, std::move(listener),\n"
    "                                   params);\n"
    "}\n"
)


class ApplyChromiumSrcOverridesTest(unittest.TestCase):
    def test_macos_app_declares_bluetooth_for_hybrid_passkeys(self):
        import plistlib

        source = Path(
            Path(__file__).resolve().parents[3],
            "chromium/src/chrome/app/app-Info.plist",
        ).read_text(encoding="utf-8")
        replacements = REPLACEMENTS["chrome/app/app-Info.plist"]
        for replacement in replacements:
            if replacement.old in source:
                source = source.replace(replacement.old, replacement.new, 1)
            elif replacement.new not in source and not replacement.idempotent:
                self.fail(f"Missing app Info.plist anchor: {replacement.description}")

        info = plistlib.loads(source.encode("utf-8"))
        self.assertTrue(info.get("NSBluetoothAlwaysUsageDescription"))

    def test_password_fill_browsertest_waits_for_renderer_data(self):
        source = Path(
            Path(__file__).resolve().parents[2],
            "browser/passwords/maho_password_fill_browsertest.cc",
        ).read_text(encoding="utf-8")

        self.assertIn("HaveFormManagersReceivedData(&driver)", source)
        self.assertIn("ASSERT_TRUE(PrepareRendererFill(*driver))", source)
        self.assertIn("driver.GetPasswordManager()->GetClient()->UpdateFormManagers()", source)
        self.assertNotIn("f.focus(); f.click();", source)

    def test_password_fill_browsertest_stops_on_dispatch_failure(self):
        source = Path(
            Path(__file__).resolve().parents[2],
            "browser/passwords/maho_password_fill_browsertest.cc",
        ).read_text(encoding="utf-8")

        self.assertIn("ASSERT_TRUE(ResolveAndDispatch", source)
        self.assertNotIn("EXPECT_TRUE(ResolveAndDispatch", source)

        helper_start = source.index("  void ExpectMainFrameFilled(")
        helper_end = source.index("  void ExpectMainFrameEmpty(", helper_start)
        filled_helper = source[helper_start:helper_end]
        self.assertNotIn("WaitForElementValue", filled_helper)
    def test_cold_start_helper_migrates_legacy_sidebar_gate(self):
        replacement = next(
            replacement
            for replacement in REPLACEMENTS[_STARTUP_BROWSER_CREATOR_IMPL_PATH]
            if replacement.description
            == "Maho cold-start: migrate the legacy sidebar-gated retry helper to "
            "BrowserView's widget-ready pending-overlay handoff"
        )
        legacy_helper = (
            "void ShowMahoColdStartPaletteWhenReady(base::WeakPtr<Browser> browser,\n"
            "                                       int remaining_attempts) {\n"
            "  if (auto* browser_view =\n"
            "          BrowserView::GetBrowserViewForBrowser(browser.get())) {\n"
            "    if (browser_view->maho_sidebar_container()) {\n"
            "      browser_view->ShowMahoCommandOverlayForNewTab();\n"
            "      return;\n"
            "    }\n"
            "  }\n"
            "}\n"
        )
        with tempfile.TemporaryDirectory() as tmp_dir:
            target = Path(tmp_dir) / "startup_browser_creator_impl.cc"
            target.write_text(legacy_helper)

            changed, _ = apply_replacements(
                target,
                [replacement],
                relative_path=_STARTUP_BROWSER_CREATOR_IMPL_PATH,
            )

            self.assertTrue(changed)
            output = target.read_text()
            self.assertIn("browser_view->ShowMahoCommandOverlayForNewTab();", output)
            self.assertIn(
                "Do not wait for sidebar construction here: in slow", output
            )
            self.assertNotIn("maho_sidebar_container()", output)

            changed_again, _ = apply_replacements(
                target,
                [replacement],
                relative_path=_STARTUP_BROWSER_CREATOR_IMPL_PATH,
            )
            self.assertFalse(changed_again)

    def test_startup_browser_creator_session_restore_palette_reverted(self):
        legacy_restore_code = (
            "    browser = SessionRestore::RestoreSession(profile_, nullptr, restore_options,\n"
            "                                             tabs);\n"
            "    if (browser) {\n"
            "      static std::atomic<bool> g_maho_restore_palette_shown{false};\n"
            "      if (!g_maho_restore_palette_shown.exchange(true)) {\n"
            "        ShowMahoColdStartPaletteWhenReady(browser->AsWeakPtr(), 200);\n"
            "      }\n"
            "      return browser;\n"
            "    }\n"
        )
        normalized, count = normalize_startup_browser_creator_session_restore(
            legacy_restore_code
        )
        self.assertEqual(count, 1)
        self.assertNotIn("g_maho_restore_palette_shown", normalized)
        self.assertIn("return browser;", normalized)

        # Verify idempotence on already-clean text
        clean_code = (
            "    browser = SessionRestore::RestoreSession(profile_, nullptr, restore_options,\n"
            "                                             tabs);\n"
            "    if (browser) {\n"
            "      return browser;\n"
            "    }\n"
        )
        normalized_clean, count_clean = (
            normalize_startup_browser_creator_session_restore(clean_code)
        )
        self.assertEqual(count_clean, 0)
        self.assertEqual(normalized_clean, clean_code)

        # Verify apply_replacements on target file normalizes and does not re-add
        with tempfile.TemporaryDirectory() as tmp_dir:
            target = Path(tmp_dir) / "startup_browser_creator_impl.cc"
            target.write_text(legacy_restore_code)

            changed, applied = apply_replacements(
                target,
                [],
                relative_path=_STARTUP_BROWSER_CREATOR_IMPL_PATH,
            )
            self.assertTrue(changed)
            output = target.read_text()
            self.assertNotIn("g_maho_restore_palette_shown", output)
            self.assertIn("return browser;", output)

            # Re-running must be a clean no-op
            changed_again, _ = apply_replacements(
                target,
                [],
                relative_path=_STARTUP_BROWSER_CREATOR_IMPL_PATH,
            )
            self.assertFalse(changed_again)

    def test_password_fill_contract_transformations_are_guarded_and_idempotent(self):
        fixtures = {
            "components/password_manager/core/browser/password_form_digest.h": (
                '#include <string>\n\n'
                '#include "components/password_manager/core/browser/password_form.h"\n'
                'namespace password_manager {\n\n// Represents a subset'
            ),
            "components/password_manager/core/browser/password_store/password_store_interface.h": (
                '#include <vector>\n'
                '  virtual void GetLogins(const PasswordFormDigest& form,\n'
                '                         base::WeakPtr<PasswordStoreConsumer> consumer) = 0;\n'
            ),
            "components/password_manager/core/browser/password_store/password_store_backend.h": (
                '#include <optional>\n'
                '  virtual void GetGroupedMatchingLoginsAsync(\n'
                '      const PasswordFormDigest& form_digest,\n'
                '      LoginsOrErrorReply callback) = 0;\n'
            ),
            "components/password_manager/core/browser/password_store/password_store.h": (
                '  void GetLogins(const PasswordFormDigest& form,\n'
                '                 base::WeakPtr<PasswordStoreConsumer> consumer) override;\n'
            ),
            "components/password_manager/core/browser/password_store/password_store.cc": (
                '    post_init_callback_ = std::move(post_init_callback_)\n'
                '                              .Then(base::BindOnce(&PasswordStore::GetLogins,\n'
                '                                                   this, form, consumer));\n'
                'void PasswordStore::GetAutofillableLogins(\n'
                'void PasswordStore::GetLogins(\n'
                '    const PasswordFormDigest& form,\n'
                '    const PasswordFillRequestContext& context,\n'
                '    base::WeakPtr<PasswordStoreConsumer> consumer) {\n'
            ),
            "components/password_manager/core/browser/form_fetcher.h": (
                '#include "components/password_manager/core/browser/password_form.h"\n'
                '  virtual void Fetch() = 0;\n'
            ),
            "components/password_manager/core/browser/form_fetcher_impl.h": (
                '  void Fetch() override;\n'
                '  const PasswordFormDigest form_digest_;\n'
            ),
            "components/password_manager/core/browser/form_fetcher_impl.cc": (
                'void FormFetcherImpl::Fetch() {\n'
                '  profile_password_store->GetLogins(form_digest_,\n'
                '                                    weak_ptr_factory_.GetWeakPtr());\n'
                '  if (account_password_store) {\n'
                '    account_password_store->GetLogins(form_digest_,\n'
                '                                      weak_ptr_factory_.GetWeakPtr());\n'
                '  }\n'
                '  return result;\n}\n\nstd::unique_ptr<FormFetcher> FormFetcherImpl::Clone() {\n'
                '  auto result = std::make_unique<FormFetcherImpl>(form_digest_, client_, false);\n'
            ),
            "components/password_manager/core/browser/password_manager_driver.h": (
                '#include <string>\n'
                '#include "components/autofill/core/common/unique_ids.h"\n'
                '  virtual int GetId() const = 0;\n'
                '  virtual void FillSuggestion(\n'
                '      const std::u16string& username,\n'
                '      const std::u16string& password,\n'
                '      base::OnceCallback<void(bool)> success_callback) = 0;\n'
                '  virtual base::WeakPtr<PasswordManagerDriver> AsWeakPtr() = 0;\n'
            ),
            "components/password_manager/content/browser/content_password_manager_driver.cc": (
                'void ContentPasswordManagerDriver::DidNavigate() {\n'
            ),
            "components/password_manager/core/browser/password_store/password_store_consumer.cc": (
                'void PasswordStoreConsumer::OnGetPasswordStoreResults(\n'
            ),
            "components/password_manager/core/browser/password_store/password_store_backend_metrics_recorder.cc": (
                '#include "components/password_manager/core/browser/password_store/'
                'password_store_backend_metrics_recorder.h"\n'
                'namespace password_manager {\n'
            ),
            "components/password_manager/core/browser/password_form_digest.cc": (
                '#include "components/password_manager/core/browser/password_form_digest.h"\n'
                "PasswordFormDigest& PasswordFormDigest::operator=(PasswordFormDigest&& other) =\n    default;\n"
            ),
            "components/password_manager/core/browser/stub_password_manager_driver.cc": (
                'void PasswordManagerDriver::FillSuggestion(\n'
                '    const PasswordFillRequestContext& context,\n'
                '    const std::u16string& username,\n'
                '    const std::u16string& password,\n'
                '    base::OnceCallback<void(bool)> success_callback) {\n'
                '  if (!IsPasswordFillRequestContextCurrent(context)) {\n'
                '    std::move(success_callback).Run(false);\n'
                '    return;\n'
                '  }\n'
                '  FillSuggestion(username, password, std::move(success_callback));\n'
                '}\n\n'
                'base::WeakPtr<PasswordManagerDriver> StubPasswordManagerDriver::AsWeakPtr() {\n'
            ),
            "components/password_manager/core/browser/password_form_manager.cc": (
                '  if (driver_) {\n'
                '    driver_id_ = driver->GetId();\n'
                '    cached_driver_frame_id_ = driver->GetFrameId();\n'
                '  }\n\n'
                '  metrics_recorder_->RecordFormSignature('
            ),
        }
        expected = {
            "PasswordFillRequestContext",
            "CreatePasswordFillResolver",
            "ResolvePasswordFill",
            "fill_request_context_",
            "GetPasswordFillRequestContext",
            "RotatePasswordManagerDocumentToken",
        }
        rendered = []
        with tempfile.TemporaryDirectory() as tmp_dir:
            for relative_path, source in fixtures.items():
                target = Path(tmp_dir) / Path(relative_path).name
                target.write_text(source)
                changed, _ = apply_replacements(
                    target, REPLACEMENTS[relative_path], relative_path=relative_path
                )
                self.assertTrue(changed, relative_path)
                output = target.read_text()
                rendered.append(output)
                changed_again, _ = apply_replacements(
                    target, REPLACEMENTS[relative_path], relative_path=relative_path
                )
                self.assertFalse(changed_again, relative_path)
                self.assertEqual(output, target.read_text())

        combined = "\n".join(rendered)
        contract = Path(
            Path(__file__).resolve().parents[2],
            "chromium_src/components/password_manager/core/browser/"
            "password_fill_request.h",
        ).read_text()
        combined = contract + "\n" + combined
        for token in expected:
            self.assertIn(token, combined)
        self.assertIn("GetLogins(form, std::move(consumer));", combined)
        self.assertIn("GetGroupedMatchingLoginsAsync(form_digest, std::move(callback));", combined)
        self.assertIn("std::move(resolver).Run(std::nullopt);", combined)
        self.assertIn("void Zeroize();", contract)
        transformed_contract_source = next(
            output
            for (path, _), output in zip(fixtures.items(), rendered)
            if "password_form_digest.cc" in path
        )
        self.assertIn(
            "std::fill(secret_.begin(), secret_.end(), u'\\0');",
            transformed_contract_source,
        )
        self.assertIn("!driver->IsPasswordFillRequestContextCurrent(context)", combined)
        self.assertIn("driver_->GetPasswordFillRequestContext()", combined)
        self.assertNotIn("OnceCallback<void(bool)>;", combined)
        self.assertNotIn("std::move(resolver).Run(false);", combined)
        self.assertNotIn("void password_manager::FormFetcher::SetPasswordFillRequestContext", combined)
        self.assertNotIn("consumer) {\n    GetLogins", combined)
        backend_header = next(
            output
            for output in rendered
            if "virtual void GetGroupedMatchingLoginsAsync" in output
        )
        self.assertNotIn("BackendLoginsOrErrorReply callback) {", backend_header)
        driver_header = next(
            output for output in rendered if "CreatePasswordFillResolver" in output
        )
        # driver_header contains default inline implementation of FillSuggestion(context, ...)
        self.assertIn("void PasswordStoreInterface::ResolvePasswordFill(", combined)
        self.assertIn("void PasswordStoreBackend::ResolvePasswordFill(", combined)
        production_driver = next(
            output
            for output in rendered
            if "void ContentPasswordManagerDriver::DidNavigate()" in output
        )
        stub_driver = next(
            output
            for output in rendered
            if "StubPasswordManagerDriver::AsWeakPtr()" in output
        )
        # The context-aware FillSuggestion default moved to a new
        # password_manager_driver.cc (the chromium-style plugin forbids inline
        # virtual bodies in official builds); neither the production driver nor
        # the stub test support file may reintroduce an out-of-line
        # PasswordManagerDriver::FillSuggestion.
        self.assertIn("RotatePasswordManagerDocumentToken();", production_driver)
        self.assertNotIn(
            "void PasswordManagerDriver::FillSuggestion(", production_driver
        )
        self.assertNotIn("void PasswordManagerDriver::FillSuggestion(", stub_driver)
        self.assertIn(
            "chromium-style plugin does not flag an inline virtual body",
            driver_header,
        )
        self.assertIn(
            "base::OnceCallback<void(bool)> success_callback);", driver_header
        )
        self.assertIn(
            '"password_manager_driver.cc"',
            REPLACEMENTS[
                "components/password_manager/core/browser/BUILD.gn"
            ][0].new,
        )
        self.assertIn(
            "PasswordFillResolver::PasswordFillResolver(PasswordFillResolver&&) noexcept =\n    default;",
            transformed_contract_source,
        )
        self.assertIn(
            "PasswordFillResolver& PasswordFillResolver::operator=(\n"
            "    PasswordFillResolver&&) noexcept = default;",
            transformed_contract_source,
        )
        self.assertIn(
            "PasswordFillResolver::~PasswordFillResolver() = default;", transformed_contract_source
        )
        self.assertIn(
            "PasswordFillResolver::PasswordFillResolver(\n"
            "    base::OnceCallback<void(std::optional<PasswordFillResolution>)>",
            transformed_contract_source,
        )
        self.assertNotIn(
            "PasswordFillResolver(PasswordFillResolver&&) noexcept = default;",
            contract,
        )
        self.assertNotIn("~PasswordFillResolver() = default;", contract)
        self.assertNotIn("callback_(std::move(callback))", contract)
        self.assertIn("PasswordFillResolver(PasswordFillResolver&&) noexcept;", contract)
        self.assertIn("~PasswordFillResolver();", contract)

    def test_password_fill_resolver_is_driver_bound_and_mismatch_fails_before_dispatch(self):
        driver_source = (
            '#include <string>\n'
            '#include "components/autofill/core/common/unique_ids.h"\n'
            '  virtual int GetId() const = 0;\n'
            '  virtual void FillSuggestion(\n'
            '      const std::u16string& username,\n'
            '      const std::u16string& password,\n'
            '      base::OnceCallback<void(bool)> success_callback) = 0;\n'
            '  virtual base::WeakPtr<PasswordManagerDriver> AsWeakPtr() = 0;\n'
        )
        with tempfile.TemporaryDirectory() as tmp_dir:
            target = Path(tmp_dir) / "password_manager_driver.h"
            target.write_text(driver_source)
            relative_path = (
                "components/password_manager/core/browser/password_manager_driver.h"
            )
            changed, _ = apply_replacements(
                target, REPLACEMENTS[relative_path], relative_path=relative_path
            )
            self.assertTrue(changed)
            output = target.read_text()

        mismatch = """if (!driver || !resolution ||
              !driver->IsPasswordFillRequestContextCurrent(context)) {
            std::move(success_callback).Run(false);
            return;
          }"""
        dispatch = "driver->FillSuggestion(context, username, resolution->secret(),"
        self.assertIn(mismatch, output)
        self.assertIn(dispatch, output)
        self.assertLess(output.index(mismatch), output.index(dispatch))
        contract = Path(
            Path(__file__).resolve().parents[2],
            "chromium_src/components/password_manager/core/browser/"
            "password_fill_request.h",
        ).read_text()
        self.assertIn("friend class PasswordManagerDriver;", contract)

    def test_password_fill_contract_rejects_missing_anchor(self):
        relative_path = (
            "components/password_manager/core/browser/password_store/"
            "password_store_backend.h"
        )
        with tempfile.TemporaryDirectory() as tmp_dir:
            target = Path(tmp_dir) / "password_store_backend.h"
            target.write_text("class UnexpectedBackend {};\n")
            with self.assertRaisesRegex(
                RuntimeError,
                "Missing expected anchor for Maho: include utility for password backend default adapters",
            ):
                apply_replacements(
                    target, REPLACEMENTS[relative_path], relative_path=relative_path
                )

    def test_password_bubble_override_is_injected_into_browser_ui_target(self):
        source = (
            'static_library("ui") {\n'
            '  sources = [\n'
            '      "views/passwords/password_bubble_view_base.cc",\n'
            '      "views/passwords/password_bubble_view_base.h",\n'
            '  ]\n'
            '\n'
            '  deps = [\n'
            '    "//build/config/linux/dbus:buildflags",\n'
            '    "//maho/browser/net",\n'
            '  ]\n'
            '\n'
            '  if (is_mac) {\n'
            '    include_dirs = [ "$target_gen_dir" ]\n'
            '  }\n'
            '}\n'
        )
        descriptions = {
            "Maho: remove target-wide chromium_src include shadowing from chrome/browser/ui:ui",
            "Maho: expose the explicit Chromium password bubble fallback to chrome/browser/ui:ui",
            "Maho: preserve password bubble overlay include roots when macOS adds target_gen_dir to chrome/browser/ui:ui",
            "Maho: compile the chromium_src password bubble factory override in chrome/browser/ui:ui",
            "Maho: link the native password save/update view into chrome/browser/ui:ui",
        }
        replacements = [
            replacement
            for replacement in REPLACEMENTS[_BROWSER_UI_BUILD_PATH]
            if replacement.description in descriptions
        ]

        with tempfile.TemporaryDirectory() as tmp_dir:
            target = Path(tmp_dir) / "BUILD.gn"
            target.write_text(source)

            changed, _ = apply_replacements(
                target,
                replacements,
                relative_path=_BROWSER_UI_BUILD_PATH,
            )

            self.assertTrue(changed)
            output = target.read_text()
            include_contract = (
                '  # The password bubble wrapper includes its overlay explicitly.\n'
                '  # This parent root is only for its src/... Chromium fallback.\n'
                '  include_dirs = [ "../../../.." ]'
            )
            self.assertIn(include_contract, output)
            self.assertNotIn('"//maho/chromium_src"', output)
            self.assertIn(
                '  if (is_mac) {\n'
                '    include_dirs += [ "$target_gen_dir" ]\n'
                '  }',
                output,
            )
            self.assertNotIn(
                'include_dirs = [ "$target_gen_dir" ]', output
            )
            self.assertIn(
                '"//maho/chromium_src/chrome/browser/ui/views/passwords/'
                'password_bubble_view_base.cc"',
                output,
            )
            self.assertIn(
                '"//maho/chromium_src/chrome/browser/ui/views/passwords/'
                'password_bubble_view_base.h"',
                output,
            )
            self.assertNotIn(
                '      "views/passwords/password_bubble_view_base.cc",', output
            )
            self.assertNotIn(
                '      "views/passwords/password_bubble_view_base.h",', output
            )
            self.assertIn(
                '"//maho/browser:maho_password_save_update_view"', output
            )

            changed_again, _ = apply_replacements(
                target,
                replacements,
                relative_path=_BROWSER_UI_BUILD_PATH,
            )
            self.assertFalse(changed_again)
            self.assertEqual(target.read_text(), output)

    def test_password_bubble_wrapper_selects_only_its_explicit_overlay(self):
        repository = Path(__file__).resolve().parents[2]
        workspace = repository.parent
        chromium_src = workspace / "chromium" / "src"
        overlay_root = repository / "chromium_src"
        wrapper = overlay_root / (
            "chrome/browser/ui/views/passwords/password_bubble_view_base.cc"
        )
        overlay_include = (
            "maho/chromium_src/chrome/browser/ui/views/passwords/"
            "password_bubble_view_base.h"
        )
        upstream_fallback = (
            "../src/chrome/browser/ui/views/passwords/password_bubble_view_base.h"
        )
        canonical_password_manager_header = Path(
            "components/password_manager/core/browser/password_manager_driver.h"
        )

        wrapper_text = wrapper.read_text(encoding="utf-8")
        self.assertIn(f'#include "{overlay_include}"', wrapper_text)
        self.assertNotIn(
            '#include "chrome/browser/ui/views/passwords/'
            'password_bubble_view_base.h"',
            wrapper_text,
        )
        overlay_text = (chromium_src / overlay_include).read_text(encoding="utf-8")
        self.assertIn("CreateBubble_ChromiumImpl", overlay_text)
        self.assertIn(f'#include "{upstream_fallback}"', overlay_text)
        self.assertTrue((chromium_src / upstream_fallback).is_file())
        for relative_path in (
            "chrome/browser/ui/views/passwords/password_bubble_view_base.h",
            "chrome/browser/ui/views/passwords/password_bubble_view_base.cc",
            "chrome/browser/ui/views/autofill/popup/popup_row_view.h",
            "chrome/browser/ui/views/autofill/popup/popup_row_view.cc",
        ):
            with self.subTest(wrapper=relative_path):
                text = (overlay_root / relative_path).read_text(encoding="utf-8")
                fallback = f"../src/{relative_path}"
                self.assertIn(f'#include "{fallback}"', text)
                self.assertTrue((chromium_src / fallback).is_file())
                self.assertEqual((chromium_src / fallback).resolve(),
                                 (chromium_src / relative_path).resolve())

        # With no //maho/chromium_src target include root, ordinary canonical
        # password-manager includes select the transformed upstream checkout.
        self.assertTrue((chromium_src / canonical_password_manager_header).is_file())
        self.assertNotEqual(
            chromium_src / canonical_password_manager_header,
            overlay_root / canonical_password_manager_header,
        )

    def test_password_bubble_override_rejects_missing_production_anchors(self):
        descriptions = {
            "Maho: remove target-wide chromium_src include shadowing from chrome/browser/ui:ui",
            "Maho: expose the explicit Chromium password bubble fallback to chrome/browser/ui:ui",
            "Maho: preserve password bubble overlay include roots when macOS adds target_gen_dir to chrome/browser/ui:ui",
            "Maho: compile the chromium_src password bubble factory override in chrome/browser/ui:ui",
            "Maho: link the native password save/update view into chrome/browser/ui:ui",
        }
        replacements = [
            replacement
            for replacement in REPLACEMENTS[_BROWSER_UI_BUILD_PATH]
            if replacement.description in descriptions
        ]

        with tempfile.TemporaryDirectory() as tmp_dir:
            target = Path(tmp_dir) / "BUILD.gn"
            target.write_text(
                'static_library("ui") {\n'
                '  sources = [\n'
                '      "views/passwords/unexpected_factory.cc",\n'
                '  ]\n'
                '\n'
                '  deps = [\n'
                '    "//build/config/linux/dbus:buildflags",\n'
                '    "//maho/browser/net",\n'
                '  ]\n'
                '\n'
                '  if (is_mac) {\n'
                '    include_dirs = [ "$target_gen_dir" ]\n'
                '  }\n'
                '}\n'
            )

            with self.assertRaisesRegex(
                RuntimeError,
                "Missing expected anchor for Maho: compile the chromium_src "
                "password bubble factory override",
            ):
                apply_replacements(
                    target,
                    replacements,
                    relative_path=_BROWSER_UI_BUILD_PATH,
                )

    def test_password_backend_factory_binds_constructed_profile_identity(self):
        source = (
            '#include "maho/browser/passwords/maho_password_store_backend.h"\n'
            "#else   //  BUILDFLAG(IS_ANDROID)\n"
            "  std::unique_ptr<password_manager::LoginDatabase> login_db(\n"
            "      password_manager::CreateLoginDatabase(is_account_store,\n"
            "                                            login_db_directory, prefs));\n"
            "  SetIsUserDataDirPolicySet(login_db.get());\n"
            "  // Maho: Route CreatePasswordStoreBackend through Maho's password store backend\n"
            "  // and disable account store in Maho Native.\n"
            "  if (is_account_store) {\n"
            "    return std::make_unique<maho::passwords::MahoPasswordStoreBackend>(false);\n"
            "  }\n"
            "  return std::make_unique<maho::passwords::MahoPasswordStoreBackend>();\n"
            "#endif  // BUILDFLAG(IS_ANDROID)\n"
        )

        with tempfile.TemporaryDirectory() as tmp_dir:
            target = Path(tmp_dir) / "password_store_backend_factory.cc"
            target.write_text(source)
            replacements = [
                item
                for item in REPLACEMENTS[_PASSWORD_BACKEND_FACTORY_PATH]
                if item.description
                in {
                    "Maho: include profile identity helper beside an already-applied password backend factory override",
                    "Maho: bind an already-routed password backend factory to its construction-time profile identity",
                }
            ]
            changed, _ = apply_replacements(
                target,
                replacements,
                relative_path=_PASSWORD_BACKEND_FACTORY_PATH,
            )

            self.assertTrue(changed)
            output = target.read_text()
            self.assertIn('#include "maho/browser/maho_core_holder.h"', output)
            self.assertIn(
                "const std::string profile_key = maho::GetProfileIdentityKey(\n"
                "      login_db_directory, prefs);",
                output,
            )
            self.assertIn("/*enabled=*/false, profile_key", output)
            self.assertIn("/*enabled=*/true, profile_key", output)
            self.assertNotIn("MahoPasswordStoreBackend>(false);", output)
            self.assertNotIn("MahoPasswordStoreBackend>();", output)

    def test_password_backend_factory_build_adds_identity_helper_dependency(self):
        source = (
            'source_set("backend_factory") {\n'
            "  deps = [\n"
            '    "//maho/browser:maho_password_store_backend",\n'
            '    "//components/password_manager/core/browser",\n'
            '    "//components/password_manager/core/browser:password_manager_buildflags",\n'
            "  ]\n"
            "}\n"
        )

        with tempfile.TemporaryDirectory() as tmp_dir:
            target = Path(tmp_dir) / "BUILD.gn"
            target.write_text(source)
            replacement = next(
                item
                for item in REPLACEMENTS[_PASSWORD_BACKEND_FACTORY_BUILD_PATH]
                if item.description.startswith(
                    "Maho: add profile identity helper dependency"
                )
            )
            changed, _ = apply_replacements(
                target,
                [replacement],
                relative_path=_PASSWORD_BACKEND_FACTORY_BUILD_PATH,
            )

            self.assertTrue(changed)
            output = target.read_text()
            self.assertIn('"//maho/browser:maho_core_holder"', output)
            self.assertEqual(output.count('"//maho/browser:maho_core_holder"'), 1)

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
            from apply_chromium_src_overrides import SPLIT_VIEW_REPLACEMENTS

            # The excerpt lacks the ctor/include anchors of the pinned-tree
            # split hunks, which are covered by MahoToolbarViewArcRegistrationTest.
            split = SPLIT_VIEW_REPLACEMENTS.get(_TOOLBAR_VIEW_CC_PATH, [])
            changed, _ = apply_replacements(
                target,
                [r for r in REPLACEMENTS[_TOOLBAR_VIEW_CC_PATH]
                 if r not in split],
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

    def test_macos_main_menu_drops_the_bookmarks_menu(self):
        upstream = REPLACEMENTS[_MAIN_MENU_BUILDER_PATH][0].old

        with tempfile.TemporaryDirectory() as tmp_dir:
            target = Path(tmp_dir) / "main_menu_builder.mm"
            target.write_text(upstream)

            changed, _ = apply_replacements(
                target,
                REPLACEMENTS[_MAIN_MENU_BUILDER_PATH],
                relative_path=_MAIN_MENU_BUILDER_PATH,
            )

            self.assertTrue(changed)
            output = target.read_text()
            self.assertIn("return nil;", output)
            self.assertNotIn("IDS_BOOKMARKS_MENU", output)
            self.assertNotIn("IDC_SHOW_BOOKMARK_MANAGER", output)
            self.assertNotIn("IDC_BOOKMARK_THIS_TAB", output)
            self.assertIn("NSMenuItem* BuildBookmarksMenu(", output)

            unchanged, _ = apply_replacements(
                target,
                REPLACEMENTS[_MAIN_MENU_BUILDER_PATH],
                relative_path=_MAIN_MENU_BUILDER_PATH,
            )
            self.assertFalse(unchanged)

    def test_macos_linker_driver_aligns_stripped_string_pool(self):
        path = "build/toolchain/apple/linker_driver.py"
        replacements = REPLACEMENTS[path]

        upstream = (
            "class LinkerDriver(object):\n"
            "    def __init__(self, args):\n"
            "        self._args = args\n"
            "\n"
            "    def run_strip(self, strip_args_string):\n"
            "        strip_command = list(self._strip_cmd)\n"
            "        if len(strip_args_string) > 0:\n"
            "            strip_command += strip_args_string.split(',')\n"
            "        strip_command.append(self._get_linker_output())\n"
            "        subprocess.check_call(strip_command)\n"
            "        return []\n"
        )

        with tempfile.TemporaryDirectory() as tmp_dir:
            target = Path(tmp_dir) / "linker_driver.py"
            target.write_text(upstream)

            changed, _ = apply_replacements(
                target,
                replacements,
                relative_path=path,
            )
            self.assertTrue(changed)
            output = target.read_text()
            self.assertIn("def _ensure_symtab_string_pool_aligned", output)
            self.assertIn(
                "_ensure_symtab_string_pool_aligned(self._get_linker_output())",
                output,
            )
            self.assertIn('"--remove-signature"', output)
            self.assertIn('"-f", "-s", "-"', output)

            # The generated module must stay syntactically valid Python.
            compile(output, "linker_driver.py", "exec")

            unchanged, _ = apply_replacements(
                target,
                replacements,
                relative_path=path,
            )
            self.assertFalse(unchanged)

    def test_macos_bookmark_bridge_is_skipped_when_the_menu_is_absent(self):
        replacement = next(
            replacement
            for replacement in REPLACEMENTS[_APP_CONTROLLER_MM_PATH]
            if replacement.description.startswith(
                "Maho: skip the macOS BookmarkMenuBridge setup"
            )
        )

        with tempfile.TemporaryDirectory() as tmp_dir:
            target = Path(tmp_dir) / "app_controller_mac.mm"
            target.write_text(replacement.old)

            changed, _ = apply_replacements(
                target,
                [replacement],
                relative_path=_APP_CONTROLLER_MM_PATH,
            )

            self.assertTrue(changed)
            output = target.read_text()
            self.assertIn("if (bookmarkItem) {", output)
            self.assertIn("CHECK(menu_root_)", output)
            self.assertIn("bookmarkItem.hidden = hidden;\n  }\n", output)
            self.assertNotIn(
                "  _bookmarkMenuBridge = entry.get();\n"
                "\n"
                "  // No need to |BuildMenu| here.",
                output,
            )

            unchanged, _ = apply_replacements(
                target,
                [replacement],
                relative_path=_APP_CONTROLLER_MM_PATH,
            )
            self.assertFalse(unchanged)

            misplaced_target = Path(tmp_dir) / "misplaced_app_controller_mac.mm"
            misplaced_target.write_text(
                "  auto& entry = _profileBookmarkMenuBridgeMap[profile->GetPath()];\n"
                "  _bookmarkMenuBridge = entry.get();\n"
            )
            with self.assertRaisesRegex(
                RuntimeError,
                "Missing expected anchor for Maho: skip the macOS BookmarkMenuBridge",
            ):
                apply_replacements(
                    misplaced_target,
                    [replacement],
                    relative_path=_APP_CONTROLLER_MM_PATH,
                )

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
        self.assertNotIn("SK_ColorTRANSPARENT", replacement.new)

    def test_shared_glass_patch_keeps_space_palette_out_of_external_web_contents(self):
        replacements = REPLACEMENTS[_BROWSER_VIEW_CC_PATH]
        replacement = next(
            item for item in replacements
            if item.description.startswith(
                "Maho: define BrowserView content-surface palette projection")
        )

        self.assertIn("view->SetMahoBackgroundOverride(std::nullopt);",
                      replacement.new)
        self.assertIn(
            "wc->SetPageBaseBackgroundColor(std::nullopt);",
            replacement.new,
        )
        self.assertNotIn("view->SetMahoBackgroundOverride(solid);",
                         replacement.new)
        self.assertNotIn("wc->SetPageBaseBackgroundColor(solid);",
                         replacement.new)

    def test_active_navigation_refreshes_shared_glass_state(self):
        rendered = "\n".join(
            item.new for item in REPLACEMENTS[_BROWSER_VIEW_CC_PATH]
        )

        self.assertIn(
            "if (change_type == TabChangeType::kLoadingOnly &&\n"
            "      contents == GetActiveWebContents()) {\n"
            "    MaybeRefreshMahoContentSurfacesFromSidebar();\n"
            "  }\n",
            rendered,
        )

    def test_shared_glass_scopes_transparency_to_each_visible_web_contents(self):
        replacement = next(
            item for item in REPLACEMENTS[_BROWSER_VIEW_CC_PATH]
            if item.description.startswith(
                "Maho: define BrowserView content-surface palette projection"
            )
        )

        self.assertIn(
            "MahoShouldRevealSharedGlass(view->web_contents())",
            replacement.new,
        )
        self.assertIn(
            "view->SetVisible(!view_shows_native_ntp);",
            replacement.new,
        )
        self.assertIn("view->SetBackgroundVisible(true);", replacement.new)
        self.assertNotIn("SK_ColorTRANSPARENT", replacement.new)

    def test_content_surface_palette_projection_not_gated_on_linux(self):
        replacement = next(
            item for item in REPLACEMENTS[_BROWSER_VIEW_CC_PATH]
            if item.description.startswith(
                "Maho: define BrowserView content-surface palette projection"
            )
        )
        self.assertNotIn("#if BUILDFLAG(IS_LINUX)\n  return;", replacement.new)
        self.assertIn(
            "maho_content_gradient_view_->SetVisible(has_native_ntp_surface);",
            replacement.new,
        )

    def test_browser_view_added_to_widget_and_theme_changed_refresh_content_surfaces(self):
        replacements = REPLACEMENTS[_BROWSER_VIEW_CC_PATH]
        added_to_widget = next(
            item for item in replacements
            if item.description.startswith(
                "Flush pending Maho new-tab command palette request"
            )
        )
        self.assertIn(
            "initialized_ = true;\n  MaybeRefreshMahoContentSurfacesFromSidebar();",
            added_to_widget.new,
        )
        theme_changed = next(
            item for item in replacements
            if item.description == "Maho: refresh content surfaces on theme change"
        )
        self.assertIn(
            "MaybeRefreshMahoContentSurfacesFromSidebar();",
            theme_changed.new,
        )

    def test_existing_space_tinted_web_contents_patch_migrates_to_neutral_base(self):
        replacement = next(
            item
            for item in REPLACEMENTS[_BROWSER_VIEW_CC_PATH]
            if item.description
            == "Maho: isolate loaded WebContents from the active Space palette"
        )

        with tempfile.TemporaryDirectory() as tmp_dir:
            target = Path(tmp_dir) / "browser_view.cc"
            target.write_text(replacement.old)

            changed, _ = apply_replacements(
                target,
                [replacement],
                relative_path=_BROWSER_VIEW_CC_PATH,
            )

            self.assertTrue(changed)
            output = target.read_text()
            self.assertIn(
                "GetColorProvider()->GetColor(kColorWebContentsBackground)",
                output,
            )
            self.assertIn("SetMahoBackgroundOverride(std::nullopt)", output)
            self.assertIn(
                "SetPageBaseBackgroundColor(maho_web_content_base)",
                output,
            )
            self.assertNotIn("SetPageBaseBackgroundColor(solid)", output)

    def test_legacy_tab_count_gradient_refresh_is_removed(self):
        replacement = next(
            item
            for item in REPLACEMENTS[_BROWSER_VIEW_CC_PATH]
            if item.description
            == "Maho: remove legacy tab-count shared-glass refresh"
        )

        with tempfile.TemporaryDirectory() as tmp_dir:
            target = Path(tmp_dir) / "browser_view.cc"
            target.write_text(replacement.old)

            changed, _ = apply_replacements(
                target,
                [replacement],
                relative_path=_BROWSER_VIEW_CC_PATH,
            )

            self.assertTrue(changed)
            output = target.read_text()
            self.assertNotIn("tab_strip_model->count() == 0", output)
            self.assertEqual(
                output.count("MaybeRefreshMahoContentSurfacesFromSidebar();"),
                1,
            )

    def test_translate_bubble_suppression_is_applied_to_browser_view(self):
        replacement = next(
            item
            for item in REPLACEMENTS[_BROWSER_VIEW_CC_PATH]
            if item.description
            == (
                "Maho: suppress upstream TranslateBubbleView in BrowserView::ShowTranslateBubble "
                "so the native Google Translate bubble never overlays the sidebar search pill"
            )
        )

        with tempfile.TemporaryDirectory() as tmp_dir:
            target = Path(tmp_dir) / "browser_view.cc"
            target.write_text(replacement.old)

            changed, _ = apply_replacements(
                target,
                [replacement],
                relative_path=_BROWSER_VIEW_CC_PATH,
            )

            self.assertTrue(changed)
            output = target.read_text()
            self.assertNotIn("TranslateBubbleController::From", output)
            self.assertIn("ShowTranslateBubbleResult::kSuccess", output)

    def test_translate_bubble_comment_retargets_from_removed_pill(self):
        # A tree patched while the sidebar search pill existed still names it
        # in the suppression comment; the follow-up entry rewrites it once.
        legacy = (
            "  // Maho: in Maho's Arc-style layout, translation affordances are surfaced\n"
            "  // via the sidebar search pill (MahoSidebar" "SearchView) and on-device\n"
            "  // translation engine. The upstream TranslateBubbleView popup is suppressed\n"
            "  // so it does not duplicate or overlay the sidebar interface.\n"
            "  return ShowTranslateBubbleResult::kSuccess;\n"
        )
        replacements = [
            item
            for item in REPLACEMENTS[_BROWSER_VIEW_CC_PATH]
            if "translate bubble suppression comment" in item.description
            or "suppress upstream TranslateBubbleView" in item.description
        ]
        self.assertEqual(len(replacements), 2)
        with tempfile.TemporaryDirectory() as tmp_dir:
            target = Path(tmp_dir) / "browser_view.cc"
            target.write_text(legacy)
            changed, _ = apply_replacements(
                target, replacements, relative_path=_BROWSER_VIEW_CC_PATH)
            self.assertTrue(changed)
            output = target.read_text()
            self.assertNotIn("SearchView", output)
            self.assertIn("MahoContentsHeaderView", output)
            changed_again, applied = apply_replacements(
                target, replacements, relative_path=_BROWSER_VIEW_CC_PATH)
            self.assertFalse(changed_again)
            self.assertTrue(all(line.startswith("already: ") for line in applied))

    def test_ui_build_strips_retired_search_pill_sources(self):
        pill = (
            '      "//maho/browser/ui/views/sidebar/maho_sidebar_'
            'search_view.cc",\n'
            '      "//maho/browser/ui/views/sidebar/maho_sidebar_'
            'search_view.h",\n'
        )
        popover = (
            '      "//maho/browser/ui/views/sidebar/maho_translate_popover_view.cc",\n'
        )
        replacement = next(
            item
            for item in REPLACEMENTS["chrome/browser/ui/BUILD.gn"]
            if "retired sidebar search pill" in item.description
        )
        with tempfile.TemporaryDirectory() as tmp_dir:
            target = Path(tmp_dir) / "BUILD.gn"
            target.write_text("sources = [\n" + pill + popover + "]\n")
            changed, _ = apply_replacements(target, [replacement])
            self.assertTrue(changed)
            output = target.read_text()
            self.assertNotIn("search_view", output)
            self.assertIn(popover, output)
            changed_again, applied = apply_replacements(target, [replacement])
            self.assertFalse(changed_again)
            self.assertEqual(applied, [f"already: {replacement.description}"])

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

    def test_sidebar_container_non_client_hit_test_applied_to_pristine_browser_view(self):
        source = (
            "  gfx::Point point_in_browser_view_coords(point);\n"
            "  views::View::ConvertPointToTarget(parent(), this,\n"
            "                                    &point_in_browser_view_coords);\n"
            "\n"
            "  if (web_app_frame_toolbar_) {\n"
            "\n"
            "    // The vertical tabstrip is not part of the overlay in immersive mode and\n"
            "    // must be tested separately.\n"
            "    if (vertical_tab_strip_region_view_ &&\n"
            "        vertical_tab_strip_region_view_->GetVisible()) {\n"
        )
        replacements = [
            item
            for item in REPLACEMENTS[_BROWSER_VIEW_CC_PATH]
            if "NonClientHitTest" in item.description
            or "immersive mode on macOS" in item.description
        ]
        with tempfile.TemporaryDirectory() as tmp_dir:
            target = Path(tmp_dir) / "browser_view.cc"
            target.write_text(source)

            changed, applied = apply_replacements(
                target,
                replacements,
                relative_path=_BROWSER_VIEW_CC_PATH,
            )
            self.assertTrue(changed)
            output = target.read_text()
            self.assertIn("maho_sidebar_container_", output)
            self.assertEqual(output.count("static_cast<maho::MahoSidebarContainerView*>"), 2)
            self.assertEqual(output.count("static_cast<maho::MahoMiniTopBarView*>"), 1)
            self.assertEqual(output.count("->IsPositionInWindowCaption(test_point)"), 3)
            self.assertIn("return HTCAPTION;", output)
            self.assertIn("return HTCLIENT;", output)
            self.assertIn("maho_mini_top_bar_", output)

            changed_again, applied_again = apply_replacements(
                target,
                replacements,
                relative_path=_BROWSER_VIEW_CC_PATH,
            )
            self.assertFalse(changed_again)
            self.assertEqual(target.read_text(), output)
            self.assertTrue(all(item.startswith("already:") for item in applied_again))

    def test_sidebar_container_non_client_hit_test_migrates_already_patched_browser_view(self):
        source = (
            "  gfx::Point point_in_browser_view_coords(point);\n"
            "  views::View::ConvertPointToTarget(parent(), this,\n"
            "                                    &point_in_browser_view_coords);\n"
            "\n"
            "  if (maho_mini_top_bar_ && maho_mini_top_bar_->GetVisible()) {\n"
            "    gfx::Point test_point(point);\n"
            "    if (ConvertedHitTest(parent(), maho_mini_top_bar_, &test_point)) {\n"
            "      if (static_cast<maho::MahoMiniTopBarView*>(\n"
            "              maho_mini_top_bar_)\n"
            "              ->IsPositionInWindowCaption(test_point)) {\n"
            "        return HTCAPTION;\n"
            "      }\n"
            "      return HTCLIENT;\n"
            "    }\n"
            "  }\n"
            "\n"
            "    // Maho: the vertical sidebar is not part of the overlay in immersive mode\n"
            "    // and must be tested separately.\n"
            "    if (maho_sidebar_container_ &&\n"
            "        maho_sidebar_container_->GetVisible()) {\n"
            "      gfx::Point test_point(point);\n"
            "      if (ConvertedHitTest(parent(), maho_sidebar_container_,\n"
            "                           &test_point)) {\n"
            "        if (static_cast<maho::MahoSidebarContainerView*>(\n"
            "                maho_sidebar_container_)\n"
            "                ->IsPositionInWindowCaption(test_point)) {\n"
            "          return HTCAPTION;\n"
            "        }\n"
            "        return HTCLIENT;\n"
            "      }\n"
            "    }\n"
            "\n"
            "    // The vertical tabstrip is not part of the overlay in immersive mode and\n"
            "    // must be tested separately.\n"
            "    if (vertical_tab_strip_region_view_ &&\n"
            "        vertical_tab_strip_region_view_->GetVisible()) {\n"
        )
        replacements = [
            item
            for item in REPLACEMENTS[_BROWSER_VIEW_CC_PATH]
            if "NonClientHitTest" in item.description
            or "immersive mode on macOS" in item.description
        ]
        with tempfile.TemporaryDirectory() as tmp_dir:
            target = Path(tmp_dir) / "browser_view.cc"
            target.write_text(source)

            changed, applied = apply_replacements(
                target,
                replacements,
                relative_path=_BROWSER_VIEW_CC_PATH,
            )
            self.assertTrue(changed)
            self.assertIn(
                "patched: Maho: Append sidebar container caption hit-test to already-patched NonClientHitTest",
                applied,
            )
            output = target.read_text()
            self.assertIn("maho_sidebar_container_", output)
            self.assertIn("// Maho: the Maho sidebar sits inside the full-size window frame.", output)
            self.assertEqual(output.count("static_cast<maho::MahoSidebarContainerView*>"), 2)
            self.assertEqual(output.count("static_cast<maho::MahoMiniTopBarView*>"), 1)
            self.assertEqual(output.count("->IsPositionInWindowCaption(test_point)"), 3)

            changed_again, applied_again = apply_replacements(
                target,
                replacements,
                relative_path=_BROWSER_VIEW_CC_PATH,
            )
            self.assertFalse(changed_again)
            self.assertEqual(target.read_text(), output)
            self.assertTrue(all(item.startswith("already:") for item in applied_again))

    def test_sidebar_container_non_client_hit_test_applied_to_immersive_mode_mac(self):
        source = (
            "    // The vertical tabstrip is not part of the overlay in immersive mode and\n"
            "    // must be tested separately.\n"
            "    if (vertical_tab_strip_region_view_ &&\n"
            "        vertical_tab_strip_region_view_->GetVisible()) {\n"
        )
        replacements = [
            item
            for item in REPLACEMENTS[_BROWSER_VIEW_CC_PATH]
            if "immersive mode on macOS" in item.description
        ]
        with tempfile.TemporaryDirectory() as tmp_dir:
            target = Path(tmp_dir) / "browser_view.cc"
            target.write_text(source)

            changed, _ = apply_replacements(
                target,
                replacements,
                relative_path=_BROWSER_VIEW_CC_PATH,
            )
            self.assertTrue(changed)
            output = target.read_text()
            self.assertIn("maho_sidebar_container_", output)
            self.assertIn("->IsPositionInWindowCaption(test_point)", output)

            changed_again, _ = apply_replacements(
                target,
                replacements,
                relative_path=_BROWSER_VIEW_CC_PATH,
            )
            self.assertFalse(changed_again)
            self.assertEqual(target.read_text(), output)

    def test_sidebar_container_non_client_hit_test_fails_closed_on_missing_anchor(self):
        drifted_source = (
            "    // Upstream changed anchor comment\n"
            "    if (vertical_tab_strip_region_view_ &&\n"
            "        vertical_tab_strip_region_view_->GetVisible()) {\n"
        )
        replacements = [
            item
            for item in REPLACEMENTS[_BROWSER_VIEW_CC_PATH]
            if "immersive mode on macOS" in item.description
        ]
        with tempfile.TemporaryDirectory() as tmp_dir:
            target = Path(tmp_dir) / "browser_view.cc"
            target.write_text(drifted_source)

            with self.assertRaisesRegex(
                RuntimeError,
                "Missing expected anchor for Maho: Consult sidebar container caption hit-test in immersive mode on macOS",
            ):
                apply_replacements(
                    target,
                    replacements,
                    relative_path=_BROWSER_VIEW_CC_PATH,
                )

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

    def test_url_alias_dep_tolerates_unknown_browser_dep_shape(self):
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

    def test_interactive_tests_deps_self_heal_from_partial_revert(self):
        replacements = [
            replacement
            for replacement in REPLACEMENTS["chrome/test/BUILD.gn"]
            if "interactive_ui_tests deps" in replacement.description
        ]
        # A partially reverted chrome/test/BUILD.gn kept sidebar/shields/boost
        # but lost spaces_overlay/peek/side_panel/command. One apply pass must
        # restore every missing dep exactly once.
        source = (
            '      "//maho/browser/ui/views/sidebar:interactive_tests",\n'
            '      "//maho/browser/ui/views/shields:interactive_tests",\n'
            '      "//maho/browser/ui/views/boost:interactive_tests",\n'
        )

        with tempfile.TemporaryDirectory() as tmp_dir:
            target = Path(tmp_dir) / "BUILD.gn"
            target.write_text(source, encoding="utf-8")

            changed, _ = apply_replacements(
                target,
                replacements,
                relative_path="chrome/test/BUILD.gn",
            )

            self.assertTrue(changed)
            output = target.read_text(encoding="utf-8")
            for dep in (
                "spaces_overlay",
                "peek",
                "command",
            ):
                self.assertEqual(
                    output.count(
                        f'"//maho/browser/ui/views/{dep}:interactive_tests"'
                    ),
                    1,
                    dep,
                )
            self.assertEqual(
                output.count(
                    '"//chrome/browser/ui/views/side_panel:interactive_tests"'
                ),
                1,
            )
            self.assertEqual(
                output.count('"//maho/browser/ui/views/sidebar:interactive_tests"'),
                1,
            )

            changed_again, _ = apply_replacements(
                target,
                replacements,
                relative_path="chrome/test/BUILD.gn",
            )
            self.assertFalse(changed_again)
            self.assertEqual(
                target.read_text(encoding="utf-8"), output
            )

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
    def test_browser_cc_run_file_chooser_interception_applies_cleanly(self):
        replacement = next(
            replacement
            for replacement in REPLACEMENTS[_BROWSER_CC_PATH]
            if replacement.description.startswith(
                "Maho: intercept RunFileChooser under an automation lease"
            )
        )

        with tempfile.TemporaryDirectory() as tmp_dir:
            target = Path(tmp_dir) / "browser.cc"
            target.write_text(_UPSTREAM_RUN_FILE_CHOOSER)

            changed, _ = apply_replacements(
                target,
                [replacement],
                relative_path=_BROWSER_CC_PATH,
            )

            self.assertTrue(changed)
            output = target.read_text()
            self.assertIn("namespace maho {", output)
            self.assertIn("MahoBeginAutomationFileChooser(", output)
            self.assertIn(
                "if (maho::MahoBeginAutomationFileChooser(",
                output,
            )
            self.assertIn(
                "FileSelectHelper::RunFileChooser(render_frame_host, std::move(listener),",
                output,
            )
            self.assertEqual(output.count("void Browser::RunFileChooser("), 1)
            self.assertEqual(
                output.count("bool MahoBeginAutomationFileChooser("), 1
            )

    def test_browser_cc_run_file_chooser_interception_is_idempotent(self):
        replacement = next(
            replacement
            for replacement in REPLACEMENTS[_BROWSER_CC_PATH]
            if replacement.description.startswith(
                "Maho: intercept RunFileChooser under an automation lease"
            )
        )

        with tempfile.TemporaryDirectory() as tmp_dir:
            target = Path(tmp_dir) / "browser.cc"
            target.write_text(_UPSTREAM_RUN_FILE_CHOOSER)

            changed, _ = apply_replacements(
                target,
                [replacement],
                relative_path=_BROWSER_CC_PATH,
            )
            self.assertTrue(changed)

            changed_again, _ = apply_replacements(
                target,
                [replacement],
                relative_path=_BROWSER_CC_PATH,
            )
            self.assertFalse(changed_again)
            output = target.read_text()
            self.assertEqual(output.count("void Browser::RunFileChooser("), 1)
            self.assertEqual(
                output.count("bool MahoBeginAutomationFileChooser("), 1
            )

    def test_browser_cc_run_file_chooser_interception_rejects_drifted_anchor(self):
        replacement = next(
            replacement
            for replacement in REPLACEMENTS[_BROWSER_CC_PATH]
            if replacement.description.startswith(
                "Maho: intercept RunFileChooser under an automation lease"
            )
        )

        with tempfile.TemporaryDirectory() as tmp_dir:
            target = Path(tmp_dir) / "browser.cc"
            target.write_text(
                "void Browser::RunFileChooser(\n"
                "    content::WebContents* web_contents) {\n"
                "  // Upstream signature drifted: the hook no longer matches.\n"
                "}\n"
            )

            with self.assertRaisesRegex(
                RuntimeError,
                "Missing expected anchor for Maho: intercept RunFileChooser",
            ):
                apply_replacements(
                    target,
                    [replacement],
                    relative_path=_BROWSER_CC_PATH,
                )


    def test_current_tab_prefill_migrates_intermediate_display_policy_form(self):
        replacements = [
            item
            for item in REPLACEMENTS[_BROWSER_VIEW_CC_PATH]
            if item.guard
            == "visible.is_empty() ? contents->GetLastCommittedURL() : visible;"
        ]
        self.assertEqual(len(replacements), 2)
        intermediate = replacements[1].old
        self.assertIn("MahoDisplayPolicy::IsInternalBrowserPage(url)", intermediate)
        with tempfile.TemporaryDirectory() as tmp_dir:
            target = Path(tmp_dir) / "browser_view.cc"
            target.write_text("// prefix\n" + intermediate + "\n// suffix\n")

            changed, _ = apply_replacements(
                target, replacements, relative_path=_BROWSER_VIEW_CC_PATH
            )

            self.assertTrue(changed)
            output = target.read_text()
            self.assertIn("/*select_initial_text=*/true);", output)
            self.assertNotIn("IsInternalBrowserPage", output)

            changed_again, _ = apply_replacements(
                target, replacements, relative_path=_BROWSER_VIEW_CC_PATH
            )
            self.assertFalse(changed_again)


class MahoDesktopDefectRegressionTest(unittest.TestCase):
    """Regressions for the 2026-09-14 macOS desktop defect report.

    D1 Settings entry points opened Chromium's chrome://settings.
    D3 Cmd+Shift+C / Cmd+L / Cmd+E never reached Maho's shortcut registry
       because AddedToWidget only ever registered Ctrl+1..9 with the
       FocusManager, and macOS dispatches FocusManager accelerators (in
       ChromeCommandDispatcherDelegate::prePerformKeyEquivalent) BEFORE
       CommandForKeyEvent -- but only at kHighPriority.
    D4 Cmd+2/3/4 fell through to the Chromium tab strip when the requested
       favorite did not exist.
    """

    def _apply(self, relative_path, filename, source, replacements):
        with tempfile.TemporaryDirectory() as tmp_dir:
            target = Path(tmp_dir) / filename
            target.write_text(source)
            changed, applied = apply_replacements(
                target,
                replacements,
                relative_path=relative_path,
            )
            output = target.read_text()

            changed_again, applied_again = apply_replacements(
                target,
                replacements,
                relative_path=relative_path,
            )
            self.assertFalse(
                changed_again, "override must be idempotent on a patched tree"
            )
            self.assertEqual(target.read_text(), output)
            return changed, applied, output

    def test_added_to_widget_registers_every_maho_shortcut_at_high_priority(self):
        replacements = [
            item
            for item in REPLACEMENTS["chrome/browser/ui/views/frame/browser_view.cc"]
            if item.old == _UPSTREAM_ADDED_TO_WIDGET
        ]
        self.assertEqual(len(replacements), 1)

        changed, _applied, output = self._apply(
            "chrome/browser/ui/views/frame/browser_view.cc",
            "browser_view.cc",
            _UPSTREAM_ADDED_TO_WIDGET,
            replacements,
        )
        self.assertTrue(changed)

        # Every enabled Maho binding must be registered, not just Ctrl+1..9:
        # AcceleratorPressed only ever fires for combos the FocusManager knows.
        self.assertIn(
            "maho::MahoShortcutInterceptor::GetRegisteredAccelerators()", output
        )
        self.assertNotIn("ui::VKEY_1 + i", output)

        # kHighPriority is what makes prePerformKeyEquivalent consult the
        # FocusManager BEFORE CommandForKeyEvent, which is the only way
        # Cmd+Shift+C stops resolving to IDC_DEV_TOOLS_INSPECT.
        self.assertIn("ui::AcceleratorManager::kHighPriority", output)
        self.assertNotIn("ui::AcceleratorManager::kNormalPriority", output)

    def test_accelerator_migrations_consume_the_comment_they_reemit(self):
        # Each accelerator migration rewrites an ALREADY-patched block, and its
        # replacement text carries the explanatory comment. So its "old" must
        # start at that block's existing comment, otherwise the previous copy is
        # left behind and a fresh one is prepended on every run -- which is how
        # browser_view.cc ended up with the comment three times. Starting from
        # pristine upstream does NOT catch this: the insert fires and every
        # migration reports "already", so assert the invariant on the rules.
        replacements = [
            item
            for item in REPLACEMENTS["chrome/browser/ui/views/frame/browser_view.cc"]
            if _MAHO_FOCUS_ACCELERATOR_REGISTRATION in item.new
        ]
        migrations = [
            item for item in replacements if item.old != _UPSTREAM_ADDED_TO_WIDGET
        ]
        self.assertGreaterEqual(len(migrations), 3)

        for migration in migrations:
            self.assertTrue(
                migration.old.lstrip().startswith("//"),
                f"migration {migration.description!r} re-emits the Maho comment "
                f"but its old text starts with {migration.old.lstrip()[:60]!r}, "
                "so the existing comment is orphaned instead of replaced",
            )

    def test_accelerator_registration_is_idempotent_and_mac_scoped(self):
        replacements = [
            item
            for item in REPLACEMENTS["chrome/browser/ui/views/frame/browser_view.cc"]
            if _MAHO_FOCUS_ACCELERATOR_REGISTRATION in item.new
        ]
        _changed, _applied, output = self._apply(
            "chrome/browser/ui/views/frame/browser_view.cc",
            "browser_view.cc",
            _UPSTREAM_ADDED_TO_WIDGET,
            replacements,
        )

        # AcceleratorManager DCHECKs !Contains(target) and, at high priority,
        # !has_priority_handler_, so the immediate call and the core-ready
        # retry must not both register.
        self.assertIn("self->maho_accelerators_registered_", output)
        self.assertIn("self->maho_accelerators_registered_ = true;", output)

        # An empty list means the core is still cold, not that registration
        # succeeded; treating it as success is what stranded the window with
        # zero accelerators for its lifetime.
        self.assertIn("maho_accelerators.empty()", output)

        # LoadAccelerators() runs right after this block and on Windows/Linux
        # registers Ctrl+L on the same target that command_bar resolves to.
        gate = output.index("#if BUILDFLAG(IS_MAC)")
        end = output.index("#endif  // BUILDFLAG(IS_MAC)")
        self.assertLess(gate, output.index("maho_register_accelerators = "))
        self.assertLess(output.index("AddCoreReadyCallback"), end)

    def test_numbered_tab_shortcut_never_falls_back_to_the_tab_strip(self):
        replacements = [
            item
            for item in REPLACEMENTS[_BROWSER_COMMAND_CONTROLLER_PATH]
            if item.old.startswith("    case IDC_SELECT_TAB_0:")
        ]
        self.assertEqual(len(replacements), 1)

        changed, _applied, output = self._apply(
            _BROWSER_COMMAND_CONTROLLER_PATH,
            "browser_command_controller.cc",
            _UPSTREAM_SELECT_NUMBERED_TAB_CASE,
            replacements,
        )
        self.assertTrue(changed)
        self.assertIn("TryActivateFavoriteByIndex", output)

        # In a Maho window Cmd+1..8 addresses favorites only. Asking for a
        # favorite that does not exist must be a no-op, never a jump to an
        # unrelated tab, so the tab-strip call must be unreachable once the
        # sidebar container is present.
        favorite_call = output.index("TryActivateFavoriteByIndex")
        tab_strip_call = output.index("SelectNumberedTab(")
        self.assertLess(favorite_call, tab_strip_call)
        between = output[favorite_call:tab_strip_call]
        self.assertIn("break;", between)
        self.assertNotIn("if (!activated)", output)
        self.assertRegex(
            output,
            r"if \(maho_sidebar_container\) \{",
        )

    def test_user_visible_chromium_product_strings_are_rebranded_to_maho(self):
        source = (
            '      <message name="IDS_PROFILES_CUSTOMIZE_PROFILE" desc="Chromium profile customization">\n'
            "        Customize your Chromium profile\n"
            "      </message>\n"
            '      <message name="IDS_PROFILE_CUSTOMIZATION_TITLE" desc="Title text.">\n'
            "        This is your Chromium\n"
            "      </message>\n"
            '      <message name="IDS_SYNC_ERROR" desc="Shown when Chromium cannot sync.">\n'
            "        Some of your Chromium data hasn't been saved yet.\n"
            "      </message>\n"
            '      <message name="IDS_OS_ONLY" desc="ChromeOS string.">\n'
            "        Chromium OS is up to date\n"
            "      </message>\n"
        )

        # The per-message Replacements cover a handful of anchors; the sweep
        # that makes the product name consistent is a normalizer, so exercise
        # it on its own with an empty replacement list.
        changed, _applied, output = self._apply(
            "chrome/app/chromium_strings.grd",
            "chromium_strings.grd",
            source,
            [],
        )
        self.assertTrue(changed)

        # Message BODIES are the user-visible surface and must say Maho.
        self.assertIn("Customize your Maho profile", output)
        self.assertIn("This is your Maho", output)
        self.assertIn("Some of your Maho data hasn't been saved yet.", output)

        # Translator notes and grit message ids must keep referring to upstream.
        self.assertIn('desc="Chromium profile customization"', output)
        self.assertIn('desc="Shown when Chromium cannot sync."', output)

        # ChromeOS is a different product and is never compiled into Maho.
        self.assertIn("Chromium OS is up to date", output)

        # settings_chromium_strings.grdp holds "Customize your Chromium profile"
        # and friends. It carries no per-message Replacements, so it only gets
        # swept if it is registered as a REPLACEMENTS key.
        self.assertIn("chrome/app/settings_chromium_strings.grdp", REPLACEMENTS)

    def test_show_settings_opens_maho_settings_not_chrome_settings(self):
        replacements = REPLACEMENTS.get(_CHROME_PAGES_CC_PATH, [])
        self.assertTrue(
            replacements,
            "chrome_pages.cc needs an override so every settings entry point "
            "(macOS app menu, Cmd+comma, IDC_OPTIONS) opens Maho settings",
        )

        changed, _applied, output = self._apply(
            _CHROME_PAGES_CC_PATH,
            "chrome_pages.cc",
            _UPSTREAM_SHOW_SETTINGS,
            replacements,
        )
        self.assertTrue(changed)
        self.assertIn('#include "maho/browser/ui/maho_settings_navigation.h"', output)
        self.assertIn("maho::OpenMahoSettingsPane(", output)

        body_start = output.index("void ShowSettings(BrowserWindowInterface* browser) {")
        body_end = output.index("\n}\n", body_start)
        body = output[body_start:body_end]
        self.assertIn("maho::OpenMahoSettingsPane(", body)

    def test_hidden_toolbar_still_shows_site_permission_prompt(self):
        ignore = next(
            item
            for item in REPLACEMENTS[_PERMISSION_PROMPT_FACTORY_PATH]
            if item.old == _UPSTREAM_IGNORE_EMPTY_OMNIBOX
        )
        anchor = next(
            item
            for item in REPLACEMENTS[_BUBBLE_ANCHOR_UTIL_PATH]
            if item.old == _UPSTREAM_PAGE_INFO_ANCHOR_RECT
        )

        _changed, _applied, ignore_output = self._apply(
            _PERMISSION_PROMPT_FACTORY_PATH,
            "permission_prompt_factory.cc",
            _UPSTREAM_IGNORE_EMPTY_OMNIBOX,
            [ignore],
        )
        self.assertIn("location_bar->IsDrawn()", ignore_output)
        self.assertNotIn(
            "!(location_bar && location_bar->IsEditingOrEmpty())",
            ignore_output,
        )

        _changed, _applied, anchor_output = self._apply(
            _BUBBLE_ANCHOR_UTIL_PATH,
            "bubble_anchor_util_views.cc",
            _UPSTREAM_PAGE_INFO_ANCHOR_RECT,
            [anchor],
        )
        self.assertIn("contents_container()", anchor_output)
        self.assertIn("contents->GetBoundsInScreen()", anchor_output)
        self.assertIn(
            "bubble_anchor_util::kNoToolbarLeftOffset",
            anchor_output,
        )

    def test_browser_open_url_from_tab_bypasses_context_menu_and_preserves_fallthrough(self):
        sample = """  struct AtcBypassState {
    // dummy
  };

  if (!AtcBypassState::ShouldBypass(this)) {
    // old logic
  }

  // If the source is already split
"""
        normalized, count = normalize_peek_open_url_routing(sample)
        self.assertEqual(count, 1)
        self.assertIn("!params.started_from_context_menu", normalized)
        self.assertIn("return result;", normalized)
        # Verify kFallThrough does not unconditional return nullptr
        self.assertNotIn("std::move(route_after_atc).Run();\n    return nullptr;", normalized)

    def test_render_view_context_menu_open_link_in_split_view_guards_null_and_notab(self):
        repl = next(
            (
                r
                for r in REPLACEMENTS.get(
                    "chrome/browser/renderer_context_menu/render_view_context_menu.cc",
                    [],
                )
                if "OpenLinkInSplitView" in r.description
            ),
            None,
        )
        self.assertIsNotNone(repl)
        assert repl is not None
        self.assertIn("if (!new_web_contents)", repl.new)
        self.assertIn("if (new_tab_index == TabStripModel::kNoTab)", repl.new)
        self.assertIn("const int post_split_index =", repl.new)
        self.assertIn("tab_strip_model->ActivateTabAt(post_split_index);", repl.new)
        # Ensure we never activate with the stale pre-split index
        self.assertNotIn("tab_strip_model->ActivateTabAt(new_tab_index);", repl.new)

        # Verify replacement applies to upstream and is idempotent
        with tempfile.TemporaryDirectory() as tmp_dir:
            target = Path(tmp_dir) / "render_view_context_menu.cc"
            target.write_text(repl.old)

            changed, _ = apply_replacements(
                target,
                [repl],
                relative_path="chrome/browser/renderer_context_menu/render_view_context_menu.cc",
            )
            self.assertTrue(changed)
            output = target.read_text()
            self.assertIn("if (!new_web_contents)", output)
            self.assertIn("post_split_index", output)

            # Second application must be idempotent (no change)
            changed_again, _ = apply_replacements(
                target,
                [repl],
                relative_path="chrome/browser/renderer_context_menu/render_view_context_menu.cc",
            )
            self.assertFalse(changed_again)


class MahoSidebarFloatingLayoutTest(unittest.TestCase):
    """An auto-hide sidebar reserves no width but must stay laid out as a
    floating hover strip, or nothing on screen can receive the hover that
    reveals it again."""

    _PATH = "chrome/browser/ui/views/frame/layout/browser_view_tabbed_layout_impl.cc"

    def _apply_twice(self, source):
        replacements = [
            item
            for item in REPLACEMENTS[self._PATH]
            if item.new == _MAHO_SIDEBAR_RAIL_LAYOUT
            or item.old == '#include "ui/views/view_utils.h"\n'
        ]
        self.assertEqual(len(replacements), 3)
        with tempfile.TemporaryDirectory() as tmp_dir:
            target = Path(tmp_dir) / "browser_view_tabbed_layout_impl.cc"
            target.write_text(source)
            apply_replacements(target, replacements, relative_path=self._PATH)
            output = target.read_text()
            changed_again, _ = apply_replacements(
                target, replacements, relative_path=self._PATH)
            self.assertFalse(changed_again)
            return output

    def _assert_floating_layout(self, output):
        self.assertEqual(output.count("Maho: reserve the leading rail"), 1)
        self.assertIn("GetFloatingOverlayWidth()", output)
        self.assertIn(
            '#include "maho/browser/ui/views/sidebar/maho_sidebar_container_view.h"',
            output)

    def test_pristine_tree_gets_floating_layout(self):
        output = self._apply_twice(
            '#include "ui/views/view_utils.h"\n'
            "void F() {\n"
            "  bool needs_exclusion = true;\n"
            "}\n")
        self._assert_floating_layout(output)

    def test_reserved_only_tree_migrates_to_floating_layout(self):
        output = self._apply_twice(
            '#include "ui/views/view_utils.h"\n'
            "void F() {\n" + _MAHO_SIDEBAR_RAIL_LAYOUT_V1 + "}\n")
        self._assert_floating_layout(output)
        self.assertIn(_MAHO_SIDEBAR_RAIL_LAYOUT, output)


class MahoContentsHeaderSlotTest(unittest.TestCase):
    """contents_container_view.{h,cc} are pinned-incompatible, so only
    SPLIT_VIEW_REPLACEMENTS patch them; the pinned pristine blobs must gain the
    existing Maho hunks plus the per-pane contents header slot."""

    _REVISION = "ee4bd9e95294a95c855ef51dfa26f0576f192e69"
    _HEADER = "chrome/browser/ui/views/frame/contents_container_view.h"
    _SOURCE = "chrome/browser/ui/views/frame/contents_container_view.cc"

    def _pristine(self, relative_path, revision=None):
        import os
        import subprocess

        revision = revision or self._REVISION
        # MAHO_PRISTINE_FIXTURE_DIR/<revision>/<relative_path> holds blobs
        # fetched from a builder when the local checkout is unavailable.
        fixture_root = os.environ.get("MAHO_PRISTINE_FIXTURE_DIR")
        if fixture_root:
            fixture = Path(fixture_root) / revision / relative_path
            if fixture.is_file():
                return fixture.read_text()
        chromium_src = Path(__file__).resolve().parents[3] / "chromium" / "src"
        result = subprocess.run(
            ["git", "-C", str(chromium_src), "show",
             f"{revision}:{relative_path}"],
            capture_output=True, text=True, check=False)
        if result.returncode != 0:
            self.skipTest(f"pinned Chromium blob unavailable: {relative_path}")
        return result.stdout

    def _apply_twice(self, relative_path, revision=None):
        from apply_chromium_src_overrides import (
            SPLIT_VIEW_REPLACEMENTS,
            is_revision_incompatible_target,
        )

        revision = revision or self._REVISION
        self.assertTrue(
            is_revision_incompatible_target(relative_path, revision))
        replacements = SPLIT_VIEW_REPLACEMENTS.get(relative_path, [])
        self.assertTrue(replacements, f"no split replacements for {relative_path}")
        with tempfile.TemporaryDirectory() as tmp_dir:
            target = Path(tmp_dir) / relative_path
            target.parent.mkdir(parents=True)
            target.write_text(self._pristine(relative_path, revision))
            changed, _ = apply_replacements(
                target, replacements, relative_path=relative_path)
            self.assertTrue(changed)
            output = target.read_text()
            changed_again, applied_again = apply_replacements(
                target, replacements, relative_path=relative_path)
            self.assertFalse(changed_again)
            self.assertTrue(applied_again)
            self.assertTrue(
                all(line.startswith("already: ") for line in applied_again),
                applied_again)
            return output

    def test_pinned_header_declares_contents_header_slot(self):
        output = self._apply_twice(self._HEADER)
        self.assertIn("class MahoContentsHeaderView;", output)
        self.assertIn(
            "raw_ptr<maho::MahoContentsHeaderView> maho_contents_header_ = nullptr;",
            output)
        self.assertIn(
            "maho::MahoContentsHeaderView* maho_contents_header() {", output)
        self.assertIn("void SetMahoAiPanelAdjacent(bool adjacent);", output)
        self.assertIn("bool maho_ai_panel_adjacent_ = false;", output)

    def test_pinned_source_lays_out_contents_header_slot(self):
        output = self._apply_twice(self._SOURCE)
        self.assertIn(
            '#include "maho/browser/ui/views/frame/maho_contents_header_view.h"',
            output)
        self.assertIn("maho_contents_header_ = AddChildView(", output)
        self.assertLess(
            output.index("mini_toolbar_ = AddChildView("),
            output.index("maho_contents_header_ = AddChildView("))
        self.assertIn("maho::kMahoContentsHeaderHeightDp", output)
        layout_start = output.index(
            "  gfx::Rect full_contents_bounds = GetContentsBounds();\n")
        inset_at = output.index(
            "full_contents_bounds.Inset(gfx::Insets().set_top(", layout_start)
        self.assertLess(inset_at, output.index(
            "ApplyDevToolsContentsResizingStrategy(", layout_start))
        # Existing Maho hunks for this file stay applied.
        self.assertIn("constexpr float kContentCornerRadius = 0;", output)
        self.assertIn("constexpr int kSplitViewContentPadding = 0;", output)
        self.assertIn("    container_outline_->SetVisible(false);\n", output)
        self.assertIn("    mini_toolbar_->SetVisible(false);\n", output)
        self.assertNotIn("UpdateState(is_active, is_highlighted)", output)
        self.assertIn(
            "void ContentsContainerView::SetMahoAiPanelAdjacent(bool adjacent) {",
            output)

    _SET_ACTIVE = (
        "    if (maho_contents_header_) {\n"
        "      maho_contents_header_->SetActive(!is_in_split || is_active);\n"
        "    }\n")

    def _assert_set_active_in_both_branches(self, output):
        start = output.index("void ContentsContainerView::UpdateBorderAndOverlay(")
        body = output[start:output.index("\n}\n", start)]
        self.assertEqual(body.count(self._SET_ACTIVE), 2, body)
        non_split = body.index("  if (!is_in_split) {\n")
        split = body.index("  } else {\n", non_split)
        self.assertLess(non_split, body.index(self._SET_ACTIVE))
        self.assertLess(body.index(self._SET_ACTIVE), split)
        self.assertGreater(body.rindex(self._SET_ACTIVE),
                           body.index("    mini_toolbar_->SetVisible(false);\n"
                                      "    if (maho_contents_header_)"))

    def test_pinned_source_wires_header_active_state(self):
        output = self._apply_twice(self._SOURCE)
        self._assert_set_active_in_both_branches(output)

    def test_second_pinned_source_wires_header_active_state(self):
        from apply_chromium_src_overrides import _PINNED_CHROMIUM_REVISIONS

        output = self._apply_twice(self._SOURCE, _PINNED_CHROMIUM_REVISIONS[1])
        self._assert_set_active_in_both_branches(output)
        self.assertIn("maho_contents_header_ = AddChildView(", output)

    def _apply_split_hunks_once(self, relative_path, text):
        """Apply this file's SPLIT_VIEW_REPLACEMENTS hunks once to `text` and
        return the resulting text (no pristine fetch, no revision check)."""
        from apply_chromium_src_overrides import SPLIT_VIEW_REPLACEMENTS

        replacements = SPLIT_VIEW_REPLACEMENTS.get(relative_path, [])
        self.assertTrue(replacements, f"no split replacements for {relative_path}")
        with tempfile.TemporaryDirectory() as tmp_dir:
            target = Path(tmp_dir) / relative_path
            target.parent.mkdir(parents=True)
            target.write_text(text)
            apply_replacements(target, replacements, relative_path=relative_path)
            return target.read_text()

    def test_split_hunks_do_not_duplicate_already_present_set_maho_ai_panel_adjacent_in_source(self):
        # Regression for the a remote builder task-9 build failure: a tree that already
        # carries ContentsContainerView::SetMahoAiPanelAdjacent at a DIFFERENT
        # location than this hunk's anchor (an older REPLACEMENTS entry put it
        # at the end of the file) must not gain a second copy. The definition
        # is deliberately not adjacent to the anchor, so the hunk's `new` text
        # is absent and the generic "already applied" check cannot fire; only
        # the _SPLIT_VIEW_HUNK_GUARDS entry prevents the duplicate. (F2 N22:
        # the previous variant placed it at the anchor and passed without the
        # guard.)
        pristine = self._pristine(self._SOURCE)
        definition = (
            "void ContentsContainerView::SetMahoAiPanelAdjacent(bool adjacent) {\n"
            "  if (maho_ai_panel_adjacent_ == adjacent) {\n"
            "    return;\n"
            "  }\n"
            "  maho_ai_panel_adjacent_ = adjacent;\n"
            "}\n"
            "\n")
        already_present = pristine.replace(
            "BEGIN_METADATA(ContentsContainerView)\n",
            definition + "BEGIN_METADATA(ContentsContainerView)\n",
            1,
        )
        signature = "void ContentsContainerView::SetMahoAiPanelAdjacent(bool adjacent) {"
        self.assertEqual(already_present.count(signature), 1)
        hunk = self._split_hunk(self._SOURCE, signature + "\n")
        self.assertIn(hunk.old, already_present)
        self.assertNotIn(hunk.new, already_present)

        output = self._apply_split_hunks_once(self._SOURCE, already_present)
        self.assertEqual(output.count(signature), 1, output)

    def test_split_hunks_do_not_duplicate_already_present_set_maho_ai_panel_adjacent_in_header(self):
        # Same shape for the declaration: present after a different member
        # than the hunk's anchor, so only the guard prevents a duplicate.
        pristine = self._pristine(self._HEADER)
        declaration = "  void SetMahoAiPanelAdjacent(bool adjacent);\n"
        already_present = pristine.replace(
            "  void HideCaptureContentsBorder();\n",
            "  void HideCaptureContentsBorder();\n" + declaration,
            1,
        )
        self.assertEqual(already_present.count(declaration), 1)
        hunk = self._split_hunk(self._HEADER, declaration)
        self.assertIn(hunk.old, already_present)
        self.assertNotIn(hunk.new, already_present)

        output = self._apply_split_hunks_once(self._HEADER, already_present)
        self.assertEqual(output.count(declaration), 1, output)

    def _split_hunk(self, relative_path, guard):
        """The SPLIT_VIEW_REPLACEMENTS hunk for `relative_path` guarded by
        `guard`."""
        from apply_chromium_src_overrides import SPLIT_VIEW_REPLACEMENTS

        matches = [r for r in SPLIT_VIEW_REPLACEMENTS[relative_path]
                   if r.guard == guard]
        self.assertEqual(len(matches), 1, [r.description for r in matches])
        return matches[0]

    def test_pristine_source_still_gets_exactly_one_set_maho_ai_panel_adjacent(self):
        output = self._apply_twice(self._SOURCE)
        self.assertEqual(
            output.count(
                "void ContentsContainerView::SetMahoAiPanelAdjacent(bool adjacent) {"),
            1, output)

    def test_pristine_header_still_gets_exactly_one_set_maho_ai_panel_adjacent(self):
        output = self._apply_twice(self._HEADER)
        self.assertEqual(
            output.count("void SetMahoAiPanelAdjacent(bool adjacent);"), 1, output)


# Pinned 72f18f12 excerpts (fetched read-only from a remote builder
# ~/maho-workspace/chromium/src) and the legacy search_view construction block
# still present in that builder's patched browser_view.cc.
_BV72_INCLUDE = (
    "#include \"maho/browser/ui/views/command/maho_command_overlay_controller.h\"\n"
)
_BV72_ACCESSOR = (
    "ToolbarButtonProvider* BrowserView::toolbar_button_provider() {\n"
    "  return ToolbarButtonProvider::From(browser_);\n"
    "}\n"
)
_BV72_INIT = (
    "  EnsureFocusOrder();\n"
    "\n"
    "  // At this point a ToolbarButtonProvider must have been set. It is set only\n"
    "  // once per browser instance.\n"
    "  auto* toolbar_button_provider = ToolbarButtonProvider::From(browser_);\n"
    "  CHECK(toolbar_button_provider);\n"
)
_BV72_DTOR = (
    "  autofill_bubble_handler_.reset();\n"
    "\n"
    "  // These are raw pointers to child views, so they need to be set to null\n"
)
_BV_LEGACY_SEARCH_VIEW_INIT = (
    "  EnsureFocusOrder();\n"
    "\n"
    "  if (!maho_toolbar_button_provider_ &&\n"
    "      !ToolbarButtonProvider::From(browser_.get()) &&\n"
    "      IsMahoArcLayoutActive()) {\n"
    "    // Resolve search_view opportunistically; MahoToolbarButtonProvider\n"
    "    // null-checks it internally and falls back to LastResortAnchor(), so\n"
    "    // construction must not be gated on a non-null search_view.\n"
    "    maho::MahoSidebar" "SearchView* search_view = nullptr;\n"
    "    if (auto* container = static_cast<maho::MahoSidebarContainerView*>(\n"
    "            maho_sidebar_container_)) {\n"
    "      if (auto* sidebar =\n"
    "              static_cast<maho::MahoSidebarView*>(container->sidebar_view())) {\n"
    "        search_view = sidebar->search_view_" "for_testing();\n"
    "      }\n"
    "    }\n"
    "    maho_toolbar_button_provider_ =\n"
    "        std::make_unique<maho::MahoToolbarButtonProvider>(\n"
    "            browser_.get(), this, search_view);\n"
    "  }\n"
    "\n"
    "  // At this point a ToolbarButtonProvider must have been set. It is set only\n"
    "  // once per browser instance.\n"
    "  auto* toolbar_button_provider = ToolbarButtonProvider::From(browser_);\n"
    "  CHECK(toolbar_button_provider);\n"
)
_BVH72_FORWARD = (
    "namespace maho {\n"
    "class MahoCommandOverlayController;\n"
    "class MahoSpacesOverlayController;\n"
    "}  // namespace maho\n"
)
_BVH72_MEMBER = (
    "  raw_ptr<views::View> maho_create_space_blank_view_ = nullptr;\n"
    "\n"
    "  std::unique_ptr<maho::MahoCommandOverlayController>\n"
)


class MahoToolbarButtonProviderRegistrationTest(unittest.TestCase):
    """BrowserView owns the Maho ToolbarButtonProvider in the Arc layout and
    exposes it through toolbar_button_provider(); upstream ToolbarView keeps
    the browser's user-data registration."""

    _CC = "chrome/browser/ui/views/frame/browser_view.cc"
    _H = "chrome/browser/ui/views/frame/browser_view.h"

    def _provider_replacements(self, relative_path):
        return [
            r for r in REPLACEMENTS[relative_path]
            if any(marker in text
                   for marker in ("maho_toolbar_button_provider",
                                  "MahoToolbarButtonProvider")
                   for text in (r.old, r.new))
        ]

    def _apply_twice(self, relative_path, source):
        replacements = self._provider_replacements(relative_path)
        self.assertTrue(replacements, relative_path)
        with tempfile.TemporaryDirectory() as tmp_dir:
            target = Path(tmp_dir) / Path(relative_path).name
            target.write_text(source)
            changed, _ = apply_replacements(
                target, replacements, relative_path=relative_path)
            self.assertTrue(changed)
            output = target.read_text()
            changed_again, applied_again = apply_replacements(
                target, replacements, relative_path=relative_path)
            self.assertFalse(changed_again)
            self.assertTrue(
                all(line.startswith("already: ") for line in applied_again),
                applied_again)
            return output

    def _assert_source(self, output):
        self.assertIn(
            '#include "maho/browser/ui/views/sidebar/'
            'maho_toolbar_button_provider.h"', output)
        self.assertEqual(1, output.count(
            "        std::make_unique<maho::MahoToolbarButtonProvider>(\n"
            "            browser_.get(), this);\n"))
        self.assertIn(
            "  if (!maho_toolbar_button_provider_ && IsMahoArcLayoutActive()) {",
            output)
        self.assertNotIn("search_view", output)
        construct_at = output.index("maho_toolbar_button_provider_ =")
        self.assertLess(construct_at, output.index(
            "  // At this point a ToolbarButtonProvider must have been set."))
        self.assertIn(
            "  auto* toolbar_button_provider =\n"
            "      BrowserView::toolbar_button_provider();\n", output)
        self.assertIn(
            "  if (maho_toolbar_button_provider_) {\n"
            "    return maho_toolbar_button_provider_.get();\n"
            "  }\n"
            "  return ToolbarButtonProvider::From(browser_);\n", output)
        reset_at = output.index("  maho_toolbar_button_provider_.reset();\n")
        self.assertLess(
            output.index("  autofill_bubble_handler_.reset();\n"), reset_at)
        self.assertLess(reset_at, output.index(
            "  // These are raw pointers to child views"))

    def test_pinned_source_constructs_and_exposes_maho_provider(self):
        self._assert_source(self._apply_twice(
            self._CC,
            _BV72_INCLUDE + "\n" + _BV72_DTOR + "\n" + _BV72_ACCESSOR
            + "\n" + _BV72_INIT))

    def test_legacy_search_view_block_migrates(self):
        self._assert_source(self._apply_twice(
            self._CC,
            _BV72_INCLUDE
            + '#include "maho/browser/ui/views/sidebar/'
            'maho_toolbar_button_provider.h"\n\n'
            + _BV72_DTOR + "\n" + _BV72_ACCESSOR + "\n"
            + _BV_LEGACY_SEARCH_VIEW_INIT))

    def test_pinned_header_owns_maho_provider(self):
        output = self._apply_twice(self._H, _BVH72_FORWARD + _BVH72_MEMBER)
        self.assertEqual(1, output.count("class MahoToolbarButtonProvider;"))
        self.assertLess(output.index("class MahoToolbarButtonProvider;"),
                        output.index("}  // namespace maho"))
        self.assertEqual(1, output.count(
            "  std::unique_ptr<maho::MahoToolbarButtonProvider>\n"
            "      maho_toolbar_button_provider_;\n"))


class MahoBasePatchLayerTest(unittest.TestCase):
    """Whole-file base patches rebuild a pristine checkout, are idempotent,
    and reject a file that matches neither pristine nor patched text."""

    _REL = "chrome/browser/example.cc"

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        root = Path(self._tmp.name)
        self.src = root / "src"
        target = self.src / self._REL
        target.parent.mkdir(parents=True)
        subprocess.run(["git", "init", "-q", str(self.src)], check=True)
        target.write_text("int a = 1;\nint b = 2;\n")
        subprocess.run(["git", "-C", str(self.src), "add", "."], check=True)
        target.write_text("int a = 1;\n// Maho\nint b = 2;\n")
        diff = subprocess.run(
            ["git", "-C", str(self.src), "diff", "--no-color", "--full-index"],
            check=True, capture_output=True, text=True).stdout
        subprocess.run(["git", "-C", str(self.src), "checkout", "-q", "--", "."],
                       check=True)
        patches = root / "patches"
        (patches / self._REL).parent.mkdir(parents=True)
        (patches / f"{self._REL}.patch").write_text(diff)
        self.patches = patches

    def tearDown(self):
        self._tmp.cleanup()

    def test_applies_once_then_reports_already(self):
        targets = base_patch_targets(self.patches)
        self.assertEqual(list(targets), [self._REL])
        patch = targets[self._REL]
        self.assertEqual(
            apply_base_patch(self.src, self._REL, patch, dry_run=False), "patched")
        self.assertIn("// Maho", (self.src / self._REL).read_text())
        self.assertEqual(
            apply_base_patch(self.src, self._REL, patch, dry_run=False), "already")

    def test_drifted_file_is_rejected(self):
        (self.src / self._REL).write_text("unrelated\n")
        patch = base_patch_targets(self.patches)[self._REL]
        with self.assertRaises(RuntimeError):
            apply_base_patch(self.src, self._REL, patch, dry_run=True)

    def test_main_base_layer_does_not_apply_legacy_anchors(self):
        import contextlib
        import io
        import sys
        from unittest.mock import patch
        import apply_chromium_src_overrides as module

        with contextlib.ExitStack() as stack:
            stack.enter_context(patch.object(
                sys, "argv", ["overrides", "--chromium-src", str(self.src)]))
            stack.enter_context(patch.object(
                module, "chromium_revision", return_value="fixture"))
            stack.enter_context(patch.object(
                module, "base_patch_dir", return_value=self.patches))
            stack.enter_context(patch.object(
                module, "REPLACEMENTS", {"retired/missing.cc": []}))
            stack.enter_context(patch.object(module, "MAHO_SCHEMA_FILE_COPIES", {}))
            stack.enter_context(contextlib.redirect_stdout(io.StringIO()))
            self.assertEqual(module.main(), 0)
            self.assertIn("// Maho", (self.src / self._REL).read_text())
            self.assertEqual(module.main(), 0)


class MahoContentsHeaderAccessiblePaneTest(unittest.TestCase):
    """Todo 7 (keyboard/accessibility for the header): the per-pane header
    must be a keyboard pane, so ContentsContainerView::GetAccessiblePanes()
    (pinned-incompatible, patched only via SPLIT_VIEW_REPLACEMENTS) has to
    list it ahead of the WebContents pane when it is visible."""

    _SOURCE = "chrome/browser/ui/views/frame/contents_container_view.cc"

    def _pristine(self, relative_path, revision):
        import os
        import subprocess

        fixture_root = os.environ.get("MAHO_PRISTINE_FIXTURE_DIR")
        if fixture_root:
            fixture = Path(fixture_root) / revision / relative_path
            if fixture.is_file():
                return fixture.read_text()
        chromium_src = Path(__file__).resolve().parents[3] / "chromium" / "src"
        result = subprocess.run(
            ["git", "-C", str(chromium_src), "show",
             f"{revision}:{relative_path}"],
            capture_output=True, text=True, check=False)
        if result.returncode != 0:
            self.skipTest(f"pinned Chromium blob unavailable: {relative_path}")
        return result.stdout

    def _apply_twice(self, revision):
        from apply_chromium_src_overrides import (
            SPLIT_VIEW_REPLACEMENTS,
            is_revision_incompatible_target,
        )

        self.assertTrue(
            is_revision_incompatible_target(self._SOURCE, revision))
        replacements = SPLIT_VIEW_REPLACEMENTS.get(self._SOURCE, [])
        self.assertTrue(replacements, f"no split replacements for {self._SOURCE}")
        with tempfile.TemporaryDirectory() as tmp_dir:
            target = Path(tmp_dir) / self._SOURCE
            target.parent.mkdir(parents=True)
            target.write_text(self._pristine(self._SOURCE, revision))
            changed, _ = apply_replacements(
                target, replacements, relative_path=self._SOURCE)
            self.assertTrue(changed)
            output = target.read_text()
            changed_again, applied_again = apply_replacements(
                target, replacements, relative_path=self._SOURCE)
            self.assertFalse(changed_again)
            self.assertTrue(applied_again)
            self.assertTrue(
                all(line.startswith("already: ") for line in applied_again),
                applied_again)
            return output

    def _assert_accessible_pane_wired(self, output):
        # Machine-relevant structure only: the function signature, the
        # visibility-guarded push, and push ordering. The explanatory
        # comment's wording is not pinned here (prose is free to change).
        self.assertIn(
            "std::vector<views::View*> ContentsContainerView::"
            "GetAccessiblePanes() {\n"
            "  std::vector<views::View*> accessible_panes;\n",
            output)
        self.assertIn(
            "  if (maho_contents_header_ && maho_contents_header_->GetVisible()) {\n"
            "    accessible_panes.push_back(maho_contents_header_);\n"
            "  }\n",
            output)
        # The header pane must be pushed before the WebContents/devtools panes
        # so F6 reaches header controls ahead of the page.
        header_push = output.index(
            "accessible_panes.push_back(maho_contents_header_);")
        contents_push = output.index(
            "accessible_panes.push_back(contents_view_);")
        self.assertLess(header_push, contents_push)

    def test_pinned_source_wires_accessible_pane(self):
        from apply_chromium_src_overrides import _PINNED_CHROMIUM_REVISIONS

        output = self._apply_twice(_PINNED_CHROMIUM_REVISIONS[0])
        self._assert_accessible_pane_wired(output)

    def test_second_pinned_source_wires_accessible_pane(self):
        from apply_chromium_src_overrides import _PINNED_CHROMIUM_REVISIONS

        output = self._apply_twice(_PINNED_CHROMIUM_REVISIONS[1])
        self._assert_accessible_pane_wired(output)


# ee4bd9e9 excerpts (local Mac / maho-mac pin; `git show ee4bd9e9:...`). This
# revision predates ToolbarButtonProvider::From(): BrowserView keeps a raw
# toolbar_button_provider_ set through SetToolbarButtonProvider() and the
# accessor is inline in browser_view.h.
_BVEE_DTOR = (
    "  autofill_bubble_handler_.reset();\n"
    "\n"
    "  // These are raw pointers to child views, so they need to be set to null\n"
    "  // before `RemoveAllChildViews()` is called to avoid dangling.\n"
    "  toolbar_ = nullptr;\n"
    "  contents_height_side_panel_ = nullptr;\n"
    "  toolbar_button_provider_ = nullptr;\n"
    "\n"
    "  RemoveAllChildViews();\n"
)
_BVEE_INIT = (
    "  EnsureFocusOrder();\n"
    "\n"
    "  // This browser view may already have a custom button provider set (e.g the\n"
    "  // hosted app frame).\n"
    "  if (!toolbar_button_provider_) {\n"
    "    SetToolbarButtonProvider(toolbar_);\n"
    "  }\n"
)
_BVEE_ACCESSOR_H = (
    "  ToolbarButtonProvider* toolbar_button_provider() {\n"
    "    return toolbar_button_provider_;\n"
    "  }\n"
)


class MahoToolbarButtonProviderEe4bd9e9Test(unittest.TestCase):
    """On ee4bd9e9 the Maho provider is installed through
    SetToolbarButtonProvider() in the `if (!toolbar_button_provider_)` path,
    and the 72f18f12-only accessor/CHECK replacements skip instead of
    raising."""

    _REVISION = "ee4bd9e95294a95c855ef51dfa26f0576f192e69"
    _CC = "chrome/browser/ui/views/frame/browser_view.cc"
    _H = "chrome/browser/ui/views/frame/browser_view.h"

    def _pristine(self, relative_path):
        import os
        import subprocess

        fixture_root = os.environ.get("MAHO_PRISTINE_FIXTURE_DIR")
        if fixture_root:
            fixture = Path(fixture_root) / self._REVISION / relative_path
            if fixture.is_file():
                return fixture.read_text()
        chromium_src = Path(__file__).resolve().parents[3] / "chromium" / "src"
        result = subprocess.run(
            ["git", "-C", str(chromium_src), "show",
             f"{self._REVISION}:{relative_path}"],
            capture_output=True, text=True, check=False)
        if result.returncode != 0:
            self.skipTest(f"pinned Chromium blob unavailable: {relative_path}")
        return result.stdout

    def _apply_twice(self, relative_path, source):
        replacements = [
            r for r in REPLACEMENTS[relative_path]
            if any(marker in text
                   for marker in ("maho_toolbar_button_provider",
                                  "MahoToolbarButtonProvider",
                                  "maho_sidebar_prefs.h")
                   for text in (r.old, r.new))
        ]
        self.assertTrue(replacements, relative_path)
        with tempfile.TemporaryDirectory() as tmp_dir:
            target = Path(tmp_dir) / Path(relative_path).name
            target.write_text(source)
            changed, _ = apply_replacements(
                target, replacements, relative_path=relative_path)
            self.assertTrue(changed)
            output = target.read_text()
            changed_again, applied_again = apply_replacements(
                target, replacements, relative_path=relative_path)
            self.assertFalse(changed_again)
            self.assertTrue(
                all(line.startswith("already: ") for line in applied_again),
                applied_again)
            return output

    def _assert_ee_source(self, output):
        self.assertIn(
            '#include "maho/browser/ui/views/sidebar/'
            'maho_toolbar_button_provider.h"\n', output)
        self.assertIn(
            '#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"\n',
            output)
        self.assertEqual(1, output.count(
            "          std::make_unique<maho::MahoToolbarButtonProvider>(\n"
            "              browser_.get(), this);\n"))
        self.assertEqual(1, output.count(
            "  if (!toolbar_button_provider_) {\n"
            "    // Maho: in the Arc layout page bubbles anchor to the active pane\n"
            "    // header, so BrowserView owns the Maho provider and installs it in\n"
            "    // place of toolbar_.\n"
            "    if (browser_->is_type_normal() &&\n"
            "        maho::sidebar_prefs::IsSidebarLayoutEnabled(\n"
            "            browser_->profile()->GetPrefs())) {\n"))
        self.assertIn(
            "      SetToolbarButtonProvider(maho_toolbar_button_provider_.get());\n"
            "    } else {\n"
            "      SetToolbarButtonProvider(toolbar_);\n"
            "    }\n"
            "  }\n", output)
        # No 72f18f12-only shapes leak into this revision.
        self.assertNotIn("ToolbarButtonProvider::From(", output)
        self.assertNotIn("BrowserView::toolbar_button_provider() {", output)
        # Teardown: the raw toolbar_button_provider_ is cleared before the
        # owning unique_ptr dies, both after the autofill handler and before
        # RemoveAllChildViews() destroys toolbar_.
        self.assertEqual(
            1, output.count("  maho_toolbar_button_provider_.reset();\n"))
        reset_at = output.index("  maho_toolbar_button_provider_.reset();\n")
        self.assertLess(
            output.index("  autofill_bubble_handler_.reset();\n"), reset_at)
        self.assertLess(
            output.index("  toolbar_button_provider_ = nullptr;\n"), reset_at)
        self.assertLess(reset_at, output.index("  RemoveAllChildViews();\n"))

    def test_ee4bd9e9_excerpt_installs_maho_provider_via_setter(self):
        self._assert_ee_source(self._apply_twice(
            self._CC, _BV72_INCLUDE + "\n" + _BVEE_DTOR + "\n" + _BVEE_INIT))

    def test_ee4bd9e9_pristine_blob_installs_maho_provider(self):
        source = self._pristine(self._CC)
        self.assertEqual(1, source.count(_BVEE_INIT.split("\n\n", 1)[1]))
        self.assertNotIn("ToolbarButtonProvider::From(browser_)", source)
        # The pristine blob lacks the Maho include anchor (added by earlier
        # overrides), so seed it the way the full REPLACEMENTS run does.
        seeded = _BV72_INCLUDE + source
        self._assert_ee_source(self._apply_twice(self._CC, seeded))

    def test_ee4bd9e9_header_keeps_inline_accessor_and_owns_provider(self):
        header = self._pristine(self._H)
        self.assertIn(_BVEE_ACCESSOR_H, header)
        output = self._apply_twice(self._H, _BVH72_FORWARD + _BVH72_MEMBER)
        self.assertEqual(1, output.count(
            "  std::unique_ptr<maho::MahoToolbarButtonProvider>\n"
            "      maho_toolbar_button_provider_;\n"))


class MahoToolbarViewArcRegistrationTest(unittest.TestCase):
    """In the Arc layout MahoToolbarButtonProvider owns the browser's
    ToolbarButtonProvider user-data slot (72f18f12), so ToolbarView must drop
    its ctor registration under the exact BrowserView Arc predicate. The split
    hunk anchors on text present on both pins; the registration drop is
    compiled only where ToolbarView has scoped_unowned_user_data_."""

    _CC = "chrome/browser/ui/views/toolbar/toolbar_view.cc"
    _PROVIDER_H = (Path(__file__).resolve().parents[2]
                   / "browser/ui/views/sidebar/maho_toolbar_button_provider.h")
    _VERSION_GUARD = "#if CHROME_VERSION_BUILD > 7795\n"
    _RESET = "    scoped_unowned_user_data_.reset();\n"

    def _pristine(self, relative_path, revision):
        import os

        fixture_root = os.environ.get("MAHO_PRISTINE_FIXTURE_DIR")
        if fixture_root:
            fixture = Path(fixture_root) / revision / relative_path
            if fixture.is_file():
                return fixture.read_text()
        chromium_src = Path(__file__).resolve().parents[3] / "chromium" / "src"
        result = subprocess.run(
            ["git", "-C", str(chromium_src), "show",
             f"{revision}:{relative_path}"],
            capture_output=True, text=True, check=False)
        if result.returncode != 0:
            self.skipTest(f"pinned Chromium blob unavailable: {relative_path}")
        return result.stdout

    def _apply_twice(self, revision):
        from apply_chromium_src_overrides import (
            SPLIT_VIEW_REPLACEMENTS,
            is_revision_incompatible_target,
        )

        self.assertTrue(is_revision_incompatible_target(self._CC, revision))
        replacements = SPLIT_VIEW_REPLACEMENTS.get(self._CC, [])
        self.assertTrue(replacements, self._CC)
        pristine = self._pristine(self._CC, revision)
        with tempfile.TemporaryDirectory() as tmp_dir:
            target = Path(tmp_dir) / Path(self._CC).name
            target.write_text(pristine)
            changed, _ = apply_replacements(
                target, replacements, relative_path=self._CC)
            self.assertTrue(changed)
            output = target.read_text()
            changed_again, applied_again = apply_replacements(
                target, replacements, relative_path=self._CC)
            self.assertFalse(changed_again)
            self.assertTrue(
                all(line.startswith("already: ") for line in applied_again),
                applied_again)
            return pristine, output

    @staticmethod
    def _tokens(text):
        import re

        return re.findall(r"\w+|->|::|&&|\S", text)

    def _arc_condition(self, output):
        block = output[output.index(self._VERSION_GUARD):]
        block = block[:block.index("#endif")]
        condition = block[block.index("  if (") + len("  if ("):
                          block.index(") {\n")]
        return block, condition

    def test_72f18f12_drops_ctor_registration_in_arc_layout(self):
        from apply_chromium_src_overrides import _PINNED_CHROMIUM_REVISIONS

        pristine, output = self._apply_twice(_PINNED_CHROMIUM_REVISIONS[1])
        self.assertIn("      scoped_unowned_user_data_;\n", self._pristine(
            "chrome/browser/ui/views/toolbar/toolbar_view.h",
            _PINNED_CHROMIUM_REVISIONS[1]))
        self.assertEqual(1, output.count(self._VERSION_GUARD))
        self.assertEqual(1, output.count(self._RESET))
        self.assertEqual(1, output.count(
            '#include "chrome/common/chrome_version.h"\n'))
        self.assertEqual(1, output.count(
            '#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"\n'))
        # Upstream registration stays; the drop runs right after it and
        # before any other ctor work.
        emplace_at = output.index(
            "    scoped_unowned_user_data_.emplace(browser_->GetUnownedUserDataHost(),\n")
        self.assertLess(emplace_at, output.index(self._RESET))
        self.assertLess(output.index(self._RESET),
                        output.index("  SetID(VIEW_ID_TOOLBAR);\n"))
        self.assertEqual(1, pristine.count("  SetID(VIEW_ID_TOOLBAR);\n"))

    def test_ee4bd9e9_hunk_applies_but_compiles_out(self):
        from apply_chromium_src_overrides import _PINNED_CHROMIUM_REVISIONS

        pristine, output = self._apply_twice(_PINNED_CHROMIUM_REVISIONS[0])
        self.assertNotIn("scoped_unowned_user_data_", pristine)
        self.assertNotIn("scoped_unowned_user_data_", self._pristine(
            "chrome/browser/ui/views/toolbar/toolbar_view.h",
            _PINNED_CHROMIUM_REVISIONS[0]))
        # The only reference to the 72f18f12-only member sits inside the
        # version guard, so ee4bd9e9 (7795) compiles none of it.
        block, _ = self._arc_condition(output)
        self.assertEqual(1, output.count("scoped_unowned_user_data_"))
        self.assertIn(self._RESET, block)

    def test_version_threshold_matches_provider_header(self):
        header = self._PROVIDER_H.read_text()
        self.assertIn(
            "#define MAHO_TOOLBAR_BUTTON_PROVIDER_OWNS_USER_DATA \\\n"
            "  (CHROME_VERSION_BUILD > 7795)\n", header)

    def test_arc_predicate_matches_browser_view_construction(self):
        from apply_chromium_src_overrides import (
            _BROWSER_VIEW_EE4BD9E9_PROVIDER_INIT,
            _PINNED_CHROMIUM_REVISIONS,
            REPLACEMENTS,
        )

        _, output = self._apply_twice(_PINNED_CHROMIUM_REVISIONS[1])
        _, condition = self._arc_condition(output)
        # BrowserView's Arc gate (the ee4bd9e9 install branch spells out the
        # same body IsMahoArcLayoutActive() returns on 72f18f12).
        install = next(
            r.new for r in REPLACEMENTS["chrome/browser/ui/views/frame/browser_view.cc"]
            if r.old == _BROWSER_VIEW_EE4BD9E9_PROVIDER_INIT)
        gate = install[install.index("    if (") + len("    if ("):
                       install.index(") {\n      maho_toolbar_button_provider_")]
        self.assertEqual(self._tokens(gate), self._tokens(condition))


if __name__ == "__main__":
    unittest.main()
