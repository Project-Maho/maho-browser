// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/passwords/maho_password_save_update_view.h"

#include <utility>

#include "chrome/browser/password_manager/factories/profile_password_store_factory.h"
#include "chrome/browser/ui/passwords/manage_passwords_ui_controller.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/test_with_browser_view.h"
#include "chrome/browser/ui/views/passwords/manage_passwords_view.h"
#include "chrome/browser/ui/views/passwords/password_bubble_view_test_base.h"
#include "chrome/browser/ui/views/passwords/password_save_update_view.h"
#include "chrome/grit/generated_resources.h"
#include "components/password_manager/core/browser/password_form.h"
#include "components/password_manager/core/browser/password_manager_test_utils.h"
#include "components/password_manager/core/browser/password_store/mock_password_store_interface.h"
#include "components/prefs/testing_pref_service.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/interaction/interaction_test_util_views.h"

namespace maho::passwords {

namespace {

using ::testing::Return;
using ::testing::ReturnRef;

class TestManagePasswordsUIController : public ManagePasswordsUIController {
 public:
  TestManagePasswordsUIController(
      content::WebContents* web_contents,
      base::WeakPtr<PasswordsModelDelegate> model_delegate)
      : ManagePasswordsUIController(web_contents),
        model_delegate_(std::move(model_delegate)) {
    EXPECT_FALSE(web_contents->GetUserData(UserDataKey()));
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
    pending_password_.password_value = u"secret";

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
  EXPECT_FALSE(ShouldUseMahoPasswordSaveUpdateView(
      EffectivePasswordProvider::kMahoNative, &local_state));

  local_state.SetBoolean(prefs::kMahoNativePasswordWriteEnabled, true);
  EXPECT_TRUE(ShouldUseMahoPasswordSaveUpdateView(
      EffectivePasswordProvider::kMahoNative, &local_state));
  EXPECT_FALSE(ShouldUseMahoPasswordSaveUpdateView(
      EffectivePasswordProvider::kBitwarden, &local_state));
  EXPECT_FALSE(ShouldUseMahoPasswordSaveUpdateView(
      EffectivePasswordProvider::kOnePassword, &local_state));
  EXPECT_FALSE(ShouldUseMahoPasswordSaveUpdateView(
      EffectivePasswordProvider::kDisabled, &local_state));
}

TEST_F(MahoPasswordSaveUpdateViewTest, FactoryUsesUpstreamViewWhenGatedOff) {
  TestingPrefServiceSimple local_state;
  RegisterLocalStatePrefs(local_state.registry());
  ScopedNativePasswordWritePrefsForTesting scoped_prefs(&local_state);

  PasswordBubbleViewBase* bubble = CreateFactoryView();

  ASSERT_TRUE(bubble);
  EXPECT_EQ(bubble->GetClassName(), PasswordSaveUpdateView::kViewClassName);
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
  EXPECT_EQ(maho_bubble->password_label_for_testing()->GetText(), u"••••••");
}

TEST_F(MahoPasswordSaveUpdateViewTest,
       FactoryUsesDistinctUpstreamFallbackForManageStateWhenAllGatesEnabled) {
  ON_CALL(*model_delegate_mock(), GetState)
      .WillByDefault(Return(password_manager::ui::MANAGE_STATE));

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

class MahoPasswordShowBubbleDispatchTest : public TestWithBrowserView {
 public:
  void SetUp() override {
    TestWithBrowserView::SetUp();
    AddTab(browser(), GURL("https://login.example.test/sign-in"));
    web_contents_ = browser()->tab_strip_model()->GetActiveWebContents();

    pending_password_.url = web_contents_->GetLastCommittedURL();
    pending_password_.signon_realm = "https://login.example.test/";
    pending_password_.username_value = u"maho-user";
    pending_password_.password_value = u"secret";

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

TEST_F(MahoPasswordSaveUpdateViewTest,
       FactoryUsesUpstreamViewForAvailableExternalProvider) {
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

  ASSERT_TRUE(bubble);
  EXPECT_EQ(bubble->GetClassName(), PasswordSaveUpdateView::kViewClassName);
}

TEST_F(MahoPasswordSaveUpdateViewTest,
       FactoryUsesUpstreamViewForUnauthorizedNativeProfile) {
  TestingPrefServiceSimple local_state;
  RegisterLocalStatePrefs(local_state.registry());
  local_state.SetBoolean(prefs::kMahoNativePasswordWriteEnabled, true);
  ScopedNativePasswordWritePrefsForTesting scoped_prefs(&local_state);

  maho::SetCore(core_);
  ASSERT_TRUE(IsNativePasswordProviderActive());
  ASSERT_FALSE(maho::IsPasswordManagerAllowedForProfile(profile()));

  PasswordBubbleViewBase* bubble = CreateFactoryView();

  ASSERT_TRUE(bubble);
  EXPECT_EQ(bubble->GetClassName(), PasswordSaveUpdateView::kViewClassName);
}

TEST_F(MahoPasswordSaveUpdateViewTest,
       BuildsWithMinimalMahoBrandingAndControls) {
  CreateViewAndShow();

  EXPECT_EQ(view_->GetSubtitle(), u"Maho Vault");
  ASSERT_TRUE(view_->GetOkButton());
  EXPECT_EQ(view_->GetOkButton()->GetText(), u"Save to Maho Vault");
  EXPECT_EQ(view_->GetCancelButton()->GetText(), u"Not now");
  ASSERT_TRUE(view_->never_button_for_testing());
  EXPECT_EQ(view_->never_button_for_testing()->GetText(),
            u"Never for this site");
  EXPECT_EQ(view_->origin_label_for_testing()->GetText(),
            u"login.example.test");
  EXPECT_EQ(view_->username_label_for_testing()->GetText(), u"maho-user");
  EXPECT_EQ(view_->password_label_for_testing()->GetText(), u"••••••");
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

  EXPECT_CALL(*model_delegate_mock(), SavePassword(std::u16string(u"maho-user"),
                                                   std::u16string(u"secret")));
  views::test::InteractionTestUtilSimulatorViews::PressButton(
      view_->GetOkButton());
}

TEST_F(MahoPasswordSaveUpdateViewTest, UpdateUsesChromiumCallback) {
  ON_CALL(*model_delegate_mock(), GetState)
      .WillByDefault(
          Return(password_manager::ui::PENDING_PASSWORD_UPDATE_STATE));
  CreateViewAndShow();

  EXPECT_CALL(*model_delegate_mock(), SavePassword(std::u16string(u"maho-user"),
                                                   std::u16string(u"secret")));
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

}  // namespace

}  // namespace maho::passwords
