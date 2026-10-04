// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/passwords/maho_password_save_update_view.h"

#include <utility>

#include "base/base_paths.h"
#include "base/check.h"
#include "base/files/file_util.h"
#include "base/path_service.h"
#include "chrome/browser/password_manager/factories/profile_password_store_factory.h"
#include "chrome/browser/ui/passwords/manage_passwords_ui_controller.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "base/test/scoped_feature_list.h"
#include "chrome/browser/autocomplete/autocomplete_classifier_factory.h"
#include "chrome/browser/autocomplete/chrome_autocomplete_provider_client.h"
#include "chrome/browser/extensions/extension_action_test_util.h"
#include "chrome/browser/media/router/media_router_feature.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/search_engines/template_url_service_test_util.h"
#include "chrome/browser/signin/chrome_signin_client_factory.h"
#include "chrome/browser/signin/chrome_signin_client_test_util.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/test/base/browser_with_test_window_test.h"
#include "components/omnibox/browser/autocomplete_classifier.h"
#include "components/omnibox/browser/autocomplete_controller.h"
#include "components/omnibox/browser/autocomplete_controller_config.h"
#include "components/omnibox/browser/test_scheme_classifier.h"
#include "components/signin/public/base/list_accounts_test_utils.h"
#include "extensions/browser/load_error_reporter.h"
#include "chrome/browser/ui/views/passwords/manage_passwords_view.h"
#include "chrome/browser/ui/views/passwords/password_bubble_view_test_base.h"
#include "chrome/grit/generated_resources.h"
#include "components/password_manager/core/browser/password_form.h"
#include "components/password_manager/core/browser/password_string.h"
#include "components/password_manager/core/browser/password_manager_test_utils.h"
#include "components/password_manager/core/browser/password_store/mock_password_store_interface.h"
#include "components/prefs/testing_pref_service.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/interaction/interaction_test_util_views.h"
#include "ui/views/test/views_drawing_test_utils.h"
#include "third_party/skia/include/core/SkBitmap.h"

namespace maho::passwords {

namespace {

using ::testing::Return;
using ::testing::ReturnRef;

base::FilePath PasswordScreenshotPath(const char* filename) {
  base::FilePath source_root;
  CHECK(base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &source_root));
  const base::FilePath maho_root =
      base::MakeAbsoluteFilePath(source_root.AppendASCII("maho"));
  CHECK(!maho_root.empty());
  return maho_root.DirName()
      .AppendASCII("docs")
      .AppendASCII("screenshots")
      .AppendASCII(filename);
}

void CapturePasswordBubbleScreenshot(MahoPasswordSaveUpdateView* view,
                                     const char* filename) {
  ASSERT_TRUE(view);
  views::Widget* widget = view->GetWidget();
  ASSERT_TRUE(widget);
  widget->LayoutRootViewIfNecessary();
  const gfx::Size size = view->GetPreferredSize();
  view->SetSize(size);
  widget->SetSize(size);
  widget->LayoutRootViewIfNecessary();

  SkBitmap bitmap = views::test::PaintViewToBitmap(widget->GetRootView());
  auto png_data = gfx::PNGCodec::EncodeBGRASkBitmap(
      bitmap, /*discard_transparency=*/false);
  ASSERT_TRUE(png_data.has_value());
  ASSERT_TRUE(base::WriteFile(PasswordScreenshotPath(filename), *png_data));
}

class TestManagePasswordsUIController : public ManagePasswordsUIController {
 public:
  TestManagePasswordsUIController(
      content::WebContents* web_contents,
      base::WeakPtr<PasswordsModelDelegate> model_delegate)
      : ManagePasswordsUIController(web_contents),
        model_delegate_(std::move(model_delegate)) {
    web_contents->SetUserData(UserDataKey(), base::WrapUnique(this));
  }

  base::WeakPtr<PasswordsModelDelegate> GetModelDelegateProxy() override {
    return model_delegate_;
  }

 private:
  base::WeakPtr<PasswordsModelDelegate> model_delegate_;
};

class MahoPasswordSaveUpdateViewTest : public PasswordBubbleViewTestBase {
 public:
  MahoPasswordSaveUpdateViewTest() {
    pending_password_.url = GURL("https://login.example.test/sign-in");
    pending_password_.signon_realm = "https://login.example.test/";
    pending_password_.username_value = u"maho-user";
    pending_password_.password_value =
        password_manager::PasswordString(std::u16string(u"secret"));

    ON_CALL(*model_delegate_mock(), GetOrigin)
        .WillByDefault(Return(url::Origin::Create(pending_password_.url)));
    ON_CALL(*model_delegate_mock(), GetState)
        .WillByDefault(Return(password_manager::ui::PENDING_PASSWORD_STATE));
    ON_CALL(*model_delegate_mock(), GetPendingPassword)
        .WillByDefault(ReturnRef(pending_password_));
    ON_CALL(*model_delegate_mock(), GetCurrentForms)
        .WillByDefault(ReturnRef(current_forms_));

    ProfilePasswordStoreFactory::GetInstance()->SetTestingFactoryAndUse(
        profile(), base::BindRepeating(
                       &password_manager::BuildPasswordStoreInterface<
                           content::BrowserContext,
                           testing::NiceMock<
                               password_manager::MockPasswordStoreInterface>>));
  }

  ~MahoPasswordSaveUpdateViewTest() override = default;

  void SetUp() override {
    PasswordBubbleViewTestBase::SetUp();
    saved_core_ = maho::GetCore();
    saved_owner_profile_ = maho::GetCoreOwnerProfile();
    core_ = maho_core_new();
    ASSERT_TRUE(core_);
    maho::SetCoreForProfile(core_, profile());
    maho_core_set_installed_extensions_for_profile(core_, "default", "[]");
    maho_core_update_settings(
        core_,
        R"({"autofill":{"passwordsEnabled":true,"passwordProvider":"maho_native"}})");
  }

  void CreateViewAndShow() {
    CreateAnchorViewAndShow();
    view_ = new MahoPasswordSaveUpdateView(
        web_contents(), views::BubbleAnchor(anchor_view()),
        LocationBarBubbleDelegateView::AUTOMATIC);
    views::BubbleDialogDelegateView::CreateBubble(view_)->Show();
  }

  PasswordBubbleViewBase* CreateFactoryView() {
    CreateAnchorViewAndShow();
    return PasswordBubbleViewBase::CreateBubble(
        web_contents(), views::BubbleAnchor(anchor_view()),
        LocationBarBubbleDelegateView::AUTOMATIC);
  }

  void TearDown() override {
    PasswordBubbleViewBase* factory_view =
        PasswordBubbleViewBase::manage_password_bubble();
    if (factory_view && factory_view != view_) {
      PasswordBubbleViewBase::DestroyManagePasswordsBubbleForTesting();
    }
    if (view_ && view_->GetWidget()) {
      view_->GetWidget()->CloseNow();
    }
    view_ = nullptr;
    maho::SetCoreForProfile(saved_core_, saved_owner_profile_);
    maho_core_free(core_);
    core_ = nullptr;
    PasswordBubbleViewTestBase::TearDown();
  }

 protected:
  password_manager::PasswordForm pending_password_;
  std::optional<password_manager::PasswordForm>
      single_credential_details_mode_credential_;
  std::vector<std::unique_ptr<password_manager::PasswordForm>> current_forms_;
  raw_ptr<MahoPasswordSaveUpdateView> view_ = nullptr;
  raw_ptr<MahoCore> core_ = nullptr;
  raw_ptr<MahoCore> saved_core_ = nullptr;
  raw_ptr<Profile> saved_owner_profile_ = nullptr;
};

TEST_F(MahoPasswordSaveUpdateViewTest,
       KillSwitchAndProviderGateMahoBubbleSelection) {
  TestingPrefServiceSimple unregistered_local_state;
  EXPECT_FALSE(ShouldUseMahoPasswordSaveUpdateView(
      EffectivePasswordProvider::kMahoNative, nullptr));
  EXPECT_FALSE(ShouldUseMahoPasswordSaveUpdateView(
      EffectivePasswordProvider::kMahoNative, &unregistered_local_state));

  TestingPrefServiceSimple local_state;
  RegisterLocalStatePrefs(local_state.registry());
  EXPECT_TRUE(ShouldUseMahoPasswordSaveUpdateView(
      EffectivePasswordProvider::kMahoNative, &local_state));

  local_state.SetBoolean(prefs::kMahoNativePasswordWriteEnabled, false);
  EXPECT_FALSE(ShouldUseMahoPasswordSaveUpdateView(
      EffectivePasswordProvider::kMahoNative, &local_state));

  local_state.SetBoolean(prefs::kMahoNativePasswordWriteEnabled, true);
  EXPECT_FALSE(ShouldUseMahoPasswordSaveUpdateView(
      EffectivePasswordProvider::kBitwarden, &local_state));
  EXPECT_FALSE(ShouldUseMahoPasswordSaveUpdateView(
      EffectivePasswordProvider::kOnePassword, &local_state));
  EXPECT_FALSE(ShouldUseMahoPasswordSaveUpdateView(
      EffectivePasswordProvider::kDisabled, &local_state));
}

TEST_F(MahoPasswordSaveUpdateViewTest, FactorySuppressesBubbleWhenGatedOff) {
  TestingPrefServiceSimple local_state;
  RegisterLocalStatePrefs(local_state.registry());
  local_state.SetBoolean(prefs::kMahoNativePasswordWriteEnabled, false);
  ScopedNativePasswordWritePrefsForTesting scoped_prefs(&local_state);

  PasswordBubbleViewBase* bubble = CreateFactoryView();

  // Kill switch off: no save prompt at all — never the upstream Chromium one.
  ASSERT_FALSE(bubble);
  EXPECT_FALSE(PasswordBubbleViewBase::manage_password_bubble());
}

TEST_F(MahoPasswordSaveUpdateViewTest, FactoryUsesMahoViewWhenGatedOn) {
  TestingPrefServiceSimple local_state;
  RegisterLocalStatePrefs(local_state.registry());
  local_state.SetBoolean(prefs::kMahoNativePasswordWriteEnabled, true);
  ScopedNativePasswordWritePrefsForTesting scoped_prefs(&local_state);

  PasswordBubbleViewBase* bubble = CreateFactoryView();

  ASSERT_TRUE(bubble);
  ASSERT_EQ(bubble->GetClassName(), MahoPasswordSaveUpdateView::kViewClassName);
  auto* maho_bubble = static_cast<MahoPasswordSaveUpdateView*>(bubble);
  EXPECT_EQ(maho_bubble->origin_label_for_testing()->GetText(),
            u"login.example.test");
  EXPECT_EQ(maho_bubble->username_label_for_testing()->GetText(), u"maho-user");
  EXPECT_EQ(maho_bubble->password_label_for_testing()->GetText(),
            u"••••••••");
}

TEST_F(MahoPasswordSaveUpdateViewTest,
       FactoryUsesDistinctUpstreamFallbackForManageStateWhenAllGatesEnabled) {
  ON_CALL(*model_delegate_mock(), GetState)
      .WillByDefault(Return(password_manager::ui::MANAGE_STATE));
  ON_CALL(*model_delegate_mock(),
          GetManagePasswordsSingleCredentialDetailsModeCredential)
      .WillByDefault(ReturnRef(single_credential_details_mode_credential_));

  TestingPrefServiceSimple local_state;
  RegisterLocalStatePrefs(local_state.registry());
  local_state.SetBoolean(prefs::kMahoNativePasswordWriteEnabled, true);
  ScopedNativePasswordWritePrefsForTesting scoped_prefs(&local_state);

  ASSERT_TRUE(IsNativePasswordProviderActive());
  ASSERT_TRUE(maho::IsPasswordManagerAllowedForProfile(profile()));
  ASSERT_TRUE(ShouldUseMahoPasswordSaveUpdateView(
      GetEffectivePasswordProviderForProfileKey("default"), &local_state));

  using Factory = decltype(&PasswordBubbleViewBase::CreateBubble);
  Factory original_factory = &PasswordBubbleViewBase::CreateBubble_ChromiumImpl;
  ASSERT_NE(original_factory, &PasswordBubbleViewBase::CreateBubble);

  PasswordBubbleViewBase* bubble = CreateFactoryView();

  ASSERT_TRUE(bubble);
  EXPECT_EQ(bubble->GetClassName(), ManagePasswordsView::kViewClassName);
}

std::unique_ptr<KeyedService> CreateAutocompleteClassifier(
    content::BrowserContext* context) {
  Profile* profile = Profile::FromBrowserContext(context);
  return std::make_unique<AutocompleteClassifier>(
      std::make_unique<AutocompleteController>(
          std::make_unique<ChromeAutocompleteProviderClient>(profile),
          AutocompleteControllerConfig{
              .provider_types =
                  AutocompleteClassifier::DefaultOmniboxProviders()}),
      std::make_unique<TestSchemeClassifier>());
}

// Canonical real-window test harness for unit tests requiring a real
// BrowserView and BrowserWidget. Replaces the obsolete upstream
// chrome/browser/ui/views/frame/test_with_browser_view.h (removed in Chromium
// commit 9f4cc6c49e001 under Project Bedrock) by overriding
// CreateBrowserWindow() to return nullptr, allowing Browser::Create() to
// instantiate a production BrowserView and BrowserWidget. Initializes the
// omnibox/profile test factories required for BrowserView, and ensures
// proper tab and window teardown.
class TestWithBrowserView : public BrowserWithTestWindowTest {
 public:
  TestWithBrowserView() {
    feature_list_.InitAndDisableFeature(media_router::kMediaRouter);
  }
  ~TestWithBrowserView() override = default;

  void SetUp() override {
    BrowserWithTestWindowTest::SetUp();
    browser_view_ = BrowserView::GetBrowserViewForBrowser(browser());
  }

  void TearDown() override {
    if (browser_view_ && browser_view_->browser()) {
      browser_view_->browser()->GetTabStripModel()->CloseAllTabs();
    }
    browser_view_ = nullptr;
    ASSERT_TRUE(release_browser());
    BrowserWithTestWindowTest::TearDown();
  }

  TestingProfile* CreateProfile(const std::string& profile_name) override {
    TestingProfile* profile =
        BrowserWithTestWindowTest::CreateProfile(profile_name);
    TemplateURLServiceFactory::GetInstance()->SetTestingFactory(
        profile,
        TemplateURLServiceTestUtil::GetTemplateURLServiceTestingFactory());
    AutocompleteClassifierFactory::GetInstance()->SetTestingFactory(
        profile, base::BindRepeating(&CreateAutocompleteClassifier));
    extensions::LoadErrorReporter::Init(/*enable_noisy_errors=*/false);
    extensions::extension_action_test_util::CreateToolbarModelForProfile(
        profile);
    signin::SetListAccountsResponseHttpNotFound(test_url_loader_factory());
    return profile;
  }

  std::unique_ptr<BrowserWindow> CreateBrowserWindow() override {
    return nullptr;
  }

  TestingProfile::TestingFactories GetTestingFactories() override {
    return {TestingProfile::TestingFactory{
        ChromeSigninClientFactory::GetInstance(),
        base::BindRepeating(&BuildChromeSigninClientWithURLLoader,
                            test_url_loader_factory())}};
  }

  BrowserView* browser_view() { return browser_view_; }

 private:
  raw_ptr<BrowserView> browser_view_ = nullptr;
  base::test::ScopedFeatureList feature_list_;
};

class MahoPasswordShowBubbleDispatchTest : public TestWithBrowserView {
 public:
  void SetUp() override {
    TestWithBrowserView::SetUp();
    AddTab(browser(), GURL("https://login.example.test/sign-in"));
    web_contents_ = browser()->GetTabStripModel()->GetActiveWebContents();

    pending_password_.url = web_contents_->GetLastCommittedURL();
    pending_password_.signon_realm = "https://login.example.test/";
    pending_password_.username_value = u"maho-user";
    pending_password_.password_value =
        password_manager::PasswordString(std::u16string(u"secret"));

    ON_CALL(model_delegate_, GetWebContents)
        .WillByDefault(Return(web_contents_));
    ON_CALL(model_delegate_, GetOrigin)
        .WillByDefault(Return(url::Origin::Create(pending_password_.url)));
    ON_CALL(model_delegate_, GetState)
        .WillByDefault(Return(password_manager::ui::PENDING_PASSWORD_STATE));
    ON_CALL(model_delegate_, GetPendingPassword)
        .WillByDefault(ReturnRef(pending_password_));
    ON_CALL(model_delegate_, GetCurrentForms)
        .WillByDefault(ReturnRef(current_forms_));

    new TestManagePasswordsUIController(
        web_contents_, model_delegate_weak_ptr_factory_.GetWeakPtr());

    ProfilePasswordStoreFactory::GetInstance()->SetTestingFactoryAndUse(
        profile(), base::BindRepeating(
                       &password_manager::BuildPasswordStoreInterface<
                           content::BrowserContext,
                           testing::NiceMock<
                               password_manager::MockPasswordStoreInterface>>));

    saved_core_ = maho::GetCore();
    saved_owner_profile_ = maho::GetCoreOwnerProfile();
    core_ = maho_core_new();
    ASSERT_TRUE(core_);
    maho::SetCoreForProfile(core_, profile());
    maho_core_set_installed_extensions_for_profile(core_, "default", "[]");
    maho_core_update_settings(
        core_,
        R"({"autofill":{"passwordsEnabled":true,"passwordProvider":"maho_native"}})");
  }

  void TearDown() override {
    if (PasswordBubbleViewBase::manage_password_bubble()) {
      PasswordBubbleViewBase::manage_password_bubble()->GetWidget()->CloseNow();
    }
    maho::SetCoreForProfile(saved_core_, saved_owner_profile_);
    maho_core_free(core_);
    core_ = nullptr;
    TestWithBrowserView::TearDown();
  }

 protected:
  raw_ptr<content::WebContents> web_contents_ = nullptr;
  password_manager::PasswordForm pending_password_;
  std::vector<std::unique_ptr<password_manager::PasswordForm>> current_forms_;
  testing::NiceMock<PasswordsModelDelegateMock> model_delegate_;
  base::WeakPtrFactory<PasswordsModelDelegate> model_delegate_weak_ptr_factory_{
      &model_delegate_};
  raw_ptr<MahoCore> core_ = nullptr;
  raw_ptr<MahoCore> saved_core_ = nullptr;
  raw_ptr<Profile> saved_owner_profile_ = nullptr;
};

TEST_F(MahoPasswordShowBubbleDispatchTest,
       ShowBubbleDispatchesThroughPublicMahoFactoryWhenGatedOn) {
  TestingPrefServiceSimple local_state;
  RegisterLocalStatePrefs(local_state.registry());
  local_state.SetBoolean(prefs::kMahoNativePasswordWriteEnabled, true);
  ScopedNativePasswordWritePrefsForTesting scoped_prefs(&local_state);

  ASSERT_TRUE(IsNativePasswordProviderActive());
  ASSERT_TRUE(maho::IsPasswordManagerAllowedForProfile(profile()));
  ASSERT_TRUE(ShouldUseMahoPasswordSaveUpdateView(
      GetEffectivePasswordProviderForProfileKey("default"), &local_state));

  PasswordBubbleViewBase::ShowBubble(web_contents_,
                                     LocationBarBubbleDelegateView::AUTOMATIC);

  PasswordBubbleViewBase* bubble =
      PasswordBubbleViewBase::manage_password_bubble();
  ASSERT_TRUE(bubble);
  EXPECT_EQ(bubble->GetClassName(), MahoPasswordSaveUpdateView::kViewClassName);
}

TEST_F(MahoPasswordShowBubbleDispatchTest,
       ShowBubbleSuppressesPromptWhenNativeWritesDisabled) {
  TestingPrefServiceSimple local_state;
  RegisterLocalStatePrefs(local_state.registry());
  local_state.SetBoolean(prefs::kMahoNativePasswordWriteEnabled, false);
  ScopedNativePasswordWritePrefsForTesting scoped_prefs(&local_state);

  PasswordBubbleViewBase::ShowBubble(web_contents_,
                                     LocationBarBubbleDelegateView::AUTOMATIC);

  EXPECT_EQ(PasswordBubbleViewBase::manage_password_bubble(), nullptr);
}

TEST_F(MahoPasswordSaveUpdateViewTest,
       FactorySuppressesBubbleForAvailableExternalProvider) {
  maho_core_set_installed_extensions_for_profile(
      core_, "default",
      R"([{"id":"nngceckbapebfimnlniiiahkandclblb","name":"Bitwarden","version":"1.0","enabled":true}])");
  maho_core_update_settings(
      core_,
      R"({"autofill":{"passwordsEnabled":true,"passwordProvider":"bitwarden"}})");
  ASSERT_EQ(EffectivePasswordProvider::kBitwarden,
            GetEffectivePasswordProviderForProfileKey("default"));

  TestingPrefServiceSimple local_state;
  RegisterLocalStatePrefs(local_state.registry());
  local_state.SetBoolean(prefs::kMahoNativePasswordWriteEnabled, true);
  ScopedNativePasswordWritePrefsForTesting scoped_prefs(&local_state);

  PasswordBubbleViewBase* bubble = CreateFactoryView();

  // Bitwarden's own extension UI owns the save; the browser shows nothing.
  ASSERT_FALSE(bubble);
  EXPECT_FALSE(PasswordBubbleViewBase::manage_password_bubble());
}

TEST_F(MahoPasswordSaveUpdateViewTest,
       FactorySuppressesBubbleForUnauthorizedNativeProfile) {
  TestingPrefServiceSimple local_state;
  RegisterLocalStatePrefs(local_state.registry());
  local_state.SetBoolean(prefs::kMahoNativePasswordWriteEnabled, true);
  ScopedNativePasswordWritePrefsForTesting scoped_prefs(&local_state);

  maho::SetCore(core_);
  ASSERT_TRUE(IsNativePasswordProviderActive());
  ASSERT_FALSE(maho::IsPasswordManagerAllowedForProfile(profile()));

  PasswordBubbleViewBase* bubble = CreateFactoryView();

  // No authorized Maho surface and never the upstream Chromium prompt.
  ASSERT_FALSE(bubble);
  EXPECT_FALSE(PasswordBubbleViewBase::manage_password_bubble());
}

TEST_F(MahoPasswordSaveUpdateViewTest, BuildsModernCredentialCardAndControls) {
  CreateViewAndShow();

  EXPECT_EQ(view_->GetSubtitle(), u"Maho Vault");
  ASSERT_TRUE(view_->GetOkButton());
  EXPECT_EQ(view_->GetOkButton()->GetText(), u"Save password");
  EXPECT_GE(view_->GetOkButton()->GetMinSize().height(), 40);
  EXPECT_EQ(view_->GetCancelButton()->GetText(), u"Not now");
  EXPECT_GE(view_->GetCancelButton()->GetMinSize().height(), 40);
  ASSERT_TRUE(view_->credential_card_for_testing());
  EXPECT_TRUE(view_->credential_card_for_testing()->background());
  EXPECT_TRUE(view_->credential_card_for_testing()->GetBorder());
  ASSERT_TRUE(view_->password_reveal_button_for_testing());
  EXPECT_EQ(view_->password_reveal_button_for_testing()->GetText(), u"Show");
  EXPECT_EQ(view_->password_reveal_button_for_testing()
                ->GetViewAccessibility()
                .GetCachedName(),
            u"Show password");
  ASSERT_TRUE(view_->never_button_for_testing());
  EXPECT_EQ(view_->never_button_for_testing()->GetText(), u"Never on this site");
  EXPECT_EQ(view_->origin_label_for_testing()->GetText(),
            u"login.example.test");
  EXPECT_EQ(view_->username_label_for_testing()->GetText(), u"maho-user");
  EXPECT_EQ(view_->password_label_for_testing()->GetText(), u"••••••••");
  EXPECT_EQ(view_->username_label_for_testing()
                ->GetViewAccessibility()
                .GetCachedName(),
            u"Username");
  EXPECT_EQ(view_->password_label_for_testing()
                ->GetViewAccessibility()
                .GetCachedName(),
            u"Password, masked");
  EXPECT_TRUE(view_->HasMahoWindowIconForTesting());
}

TEST_F(MahoPasswordSaveUpdateViewTest, PasswordMaskDoesNotLeakSecretLength) {
  pending_password_.password_value = password_manager::PasswordString(
      std::u16string(u"this-is-a-much-longer-password"));
  CreateViewAndShow();

  EXPECT_EQ(view_->password_label_for_testing()->GetText(), u"••••••••");
}

TEST_F(MahoPasswordSaveUpdateViewTest,
       PasswordRevealRequiresExplicitActionAndCanBeHiddenAgain) {
  const std::u16string expected_password = u"s\u00e9cret\U0001f512\u4e2d";
  pending_password_.password_value =
      password_manager::PasswordString(std::u16string(expected_password));
  CreateViewAndShow();

  ASSERT_TRUE(view_->password_reveal_button_for_testing());
  EXPECT_FALSE(view_->password_revealed_for_testing());
  EXPECT_EQ(view_->password_label_for_testing()->GetText(), u"••••••••");

  views::test::InteractionTestUtilSimulatorViews::PressButton(
      view_->password_reveal_button_for_testing());

  EXPECT_TRUE(view_->password_revealed_for_testing());
  EXPECT_EQ(view_->password_label_for_testing()->GetText(), expected_password);
  EXPECT_EQ(view_->password_label_for_testing()->GetText().size(),
            expected_password.size());
  EXPECT_EQ(view_->password_reveal_button_for_testing()->GetText(), u"Hide");
  EXPECT_EQ(view_->password_label_for_testing()
                ->GetViewAccessibility()
                .GetCachedName(),
            u"Password, visible");

  views::test::InteractionTestUtilSimulatorViews::PressButton(
      view_->password_reveal_button_for_testing());

  EXPECT_FALSE(view_->password_revealed_for_testing());
  EXPECT_EQ(view_->password_label_for_testing()->GetText(), u"••••••••");
  EXPECT_EQ(view_->password_reveal_button_for_testing()->GetText(), u"Show");
  EXPECT_EQ(view_->password_label_for_testing()
                ->GetViewAccessibility()
                .GetCachedName(),
            u"Password, masked");
}

TEST_F(MahoPasswordSaveUpdateViewTest, EmptyPasswordHasNoRevealAction) {
  pending_password_.password_value.clear();
  CreateViewAndShow();

  EXPECT_EQ(view_->password_label_for_testing()->GetText(), u"No password");
  EXPECT_FALSE(view_->password_reveal_button_for_testing());
}

TEST_F(MahoPasswordSaveUpdateViewTest, PreservesUpstreamUpdateWording) {
  ON_CALL(*model_delegate_mock(), GetState)
      .WillByDefault(
          Return(password_manager::ui::PENDING_PASSWORD_UPDATE_STATE));
  CreateViewAndShow();

  EXPECT_EQ(view_->GetOkButton()->GetText(),
            l10n_util::GetStringUTF16(IDS_PASSWORD_MANAGER_UPDATE_BUTTON));
  EXPECT_FALSE(view_->never_button_for_testing());
}

TEST_F(MahoPasswordSaveUpdateViewTest, SaveUsesChromiumCallback) {
  CreateViewAndShow();

  EXPECT_CALL(*model_delegate_mock(),
              SavePassword(std::u16string(u"maho-user"),
                           password_manager::PasswordString(
                               std::u16string(u"secret"))));
  views::test::InteractionTestUtilSimulatorViews::PressButton(
      view_->GetOkButton());
}

TEST_F(MahoPasswordSaveUpdateViewTest, UpdateUsesChromiumCallback) {
  ON_CALL(*model_delegate_mock(), GetState)
      .WillByDefault(
          Return(password_manager::ui::PENDING_PASSWORD_UPDATE_STATE));
  CreateViewAndShow();

  EXPECT_CALL(*model_delegate_mock(),
              SavePassword(std::u16string(u"maho-user"),
                           password_manager::PasswordString(
                               std::u16string(u"secret"))));
  views::test::InteractionTestUtilSimulatorViews::PressButton(
      view_->GetOkButton());
}

TEST_F(MahoPasswordSaveUpdateViewTest, NeverUsesChromiumCallback) {
  CreateViewAndShow();

  EXPECT_CALL(*model_delegate_mock(), NeverSavePassword());
  views::test::InteractionTestUtilSimulatorViews::PressButton(
      view_->never_button_for_testing());
}

TEST_F(MahoPasswordSaveUpdateViewTest, EscapeRunsNotNowCallback) {
  CreateViewAndShow();

  EXPECT_CALL(*model_delegate_mock(), OnNopeUpdateClicked()).Times(0);
  EXPECT_CALL(*model_delegate_mock(), OnNotNowClicked());
  EXPECT_TRUE(view_->CancelForTesting());
}

TEST_F(MahoPasswordSaveUpdateViewTest, UpdateEscapeRunsNoThanksCallback) {
  ON_CALL(*model_delegate_mock(), GetState)
      .WillByDefault(
          Return(password_manager::ui::PENDING_PASSWORD_UPDATE_STATE));
  CreateViewAndShow();

  EXPECT_CALL(*model_delegate_mock(), OnNotNowClicked()).Times(0);
  EXPECT_CALL(*model_delegate_mock(), OnNopeUpdateClicked());
  EXPECT_TRUE(view_->CancelForTesting());
}

TEST_F(MahoPasswordSaveUpdateViewTest, CaptureSaveBubbleScreenshot) {
  CreateViewAndShow();
  CapturePasswordBubbleScreenshot(
      view_, "06_maho_password_save_ask_bubble.png");
}

TEST_F(MahoPasswordSaveUpdateViewTest, CaptureUpdateBubbleScreenshot) {
  ON_CALL(*model_delegate_mock(), GetState)
      .WillByDefault(
          Return(password_manager::ui::PENDING_PASSWORD_UPDATE_STATE));
  CreateViewAndShow();
  CapturePasswordBubbleScreenshot(
      view_, "07_maho_password_update_ask_bubble.png");
}

}  // namespace

}  // namespace maho::passwords
