import tempfile
import unittest
from pathlib import Path

from apply_chromium_src_overrides import REPLACEMENTS, apply_replacements


_BROWSER_BUILD_PATH = "chrome/browser/BUILD.gn"
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


class ApplyChromiumSrcOverridesTest(unittest.TestCase):
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
                '      BackendLoginsOrErrorReply callback) = 0;\n'
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
        self.assertIn("void PasswordManagerDriver::FillSuggestion(", production_driver)
        self.assertNotIn("void PasswordManagerDriver::FillSuggestion(", stub_driver)
        self.assertIn(
            "!IsPasswordFillRequestContextCurrent(context)", production_driver
        )
        self.assertLess(
            production_driver.index("!IsPasswordFillRequestContextCurrent(context)"),
            production_driver.index(
                "FillSuggestion(username, password, std::move(success_callback));"
            ),
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
        chromium_root = chromium_src.parent
        overlay_root = repository / "chromium_src"
        wrapper = overlay_root / (
            "chrome/browser/ui/views/passwords/password_bubble_view_base.cc"
        )
        overlay_include = (
            "maho/chromium_src/chrome/browser/ui/views/passwords/"
            "password_bubble_view_base.h"
        )
        upstream_fallback = (
            "src/chrome/browser/ui/views/passwords/password_bubble_view_base.h"
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
        self.assertTrue((chromium_root / upstream_fallback).is_file())

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
