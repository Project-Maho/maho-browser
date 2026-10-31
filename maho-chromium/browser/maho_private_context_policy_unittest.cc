#include "maho/browser/maho_private_context_policy.h"

#include <algorithm>
#include <memory>
#include <vector>

#include "chrome/browser/ui/browser_window/test/mock_browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/test/user_education/mock_browser_user_education_interface.h"
#include "chrome/browser/ui/browser_actions.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/test_tab_strip_model_delegate.h"
#include "chrome/test/base/chrome_render_view_host_test_harness.h"
#include "chrome/test/base/testing_profile.h"
#include "components/profile_metrics/browser_profile_type.h"
#include "content/public/test/web_contents_tester.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

class TestBrowserWindowInterface : public MockBrowserWindowInterface {
 public:
  explicit TestBrowserWindowInterface(Profile* profile)
      : profile_(profile), weak_factory_(this) {
    ON_CALL(*this, GetProfile()).WillByDefault(testing::Return(profile_));
    ON_CALL(*this, GetWeakPtr).WillByDefault([this]() {
      return weak_factory_.GetWeakPtr();
    });
    ON_CALL(testing::Const(*this), GetUnownedUserDataHost()).WillByDefault(testing::ReturnRef(user_data_host_));
    browser_actions_ = std::make_unique<BrowserActions>(this);
    user_education_ = std::make_unique<testing::NiceMock<MockBrowserUserEducationInterface>>(this);
    features_ = std::make_unique<BrowserWindowFeatures>();
  }
  ~TestBrowserWindowInterface() override = default;

  void SetTabStripModel(TabStripModel* tab_strip) {
    tab_strip_ = tab_strip;
    ON_CALL(*this, GetTabStripModel()).WillByDefault(testing::Return(tab_strip_));
  }

  BrowserWindowFeatures& GetFeatures() override {
    return *features_;
  }
  const BrowserWindowFeatures& GetFeatures() const override {
    return *features_;
  }

 private:
  raw_ptr<Profile> profile_;
  raw_ptr<TabStripModel> tab_strip_ = nullptr;
  ui::UnownedUserDataHost user_data_host_;
  std::unique_ptr<BrowserActions> browser_actions_;
  std::unique_ptr<MockBrowserUserEducationInterface> user_education_;
  std::unique_ptr<BrowserWindowFeatures> features_;
  base::WeakPtrFactory<TestBrowserWindowInterface> weak_factory_;
};

class MahoPrivateContextPolicyTest : public ChromeRenderViewHostTestHarness {
 protected:
  void ExpectCapabilities(MahoPrivateContextToken& token,
                          const std::vector<MahoPrivateCapability>& allowed) {
    std::vector<MahoPrivateCapability> all_caps = {
        MahoPrivateCapability::kWindowLocalTabs,
        MahoPrivateCapability::kPrivateVisuals,
        MahoPrivateCapability::kProcessGlobalWebUI,
        MahoPrivateCapability::kSavedReadMutate,
        MahoPrivateCapability::kPersistentSplit,
        MahoPrivateCapability::kMahoSearchReadMutate,
        MahoPrivateCapability::kPreview,
        MahoPrivateCapability::kAI,
        MahoPrivateCapability::kMCP,
        MahoPrivateCapability::kExtensionSnapshot,
        MahoPrivateCapability::kMahoDownloadMetadata,
    };

    for (auto cap : all_caps) {
      bool expected =
          std::find(allowed.begin(), allowed.end(), cap) != allowed.end();
      EXPECT_EQ(token.Revalidate(cap), expected);
    }
  }

  void ExpectAccessorCapabilities(
      Profile* profile,
      const std::vector<MahoPrivateCapability>& allowed) {
    std::vector<MahoPrivateCapability> all_caps = {
        MahoPrivateCapability::kWindowLocalTabs,
        MahoPrivateCapability::kPrivateVisuals,
        MahoPrivateCapability::kProcessGlobalWebUI,
        MahoPrivateCapability::kSavedReadMutate,
        MahoPrivateCapability::kPersistentSplit,
        MahoPrivateCapability::kMahoSearchReadMutate,
        MahoPrivateCapability::kPreview,
        MahoPrivateCapability::kAI,
        MahoPrivateCapability::kMCP,
        MahoPrivateCapability::kExtensionSnapshot,
        MahoPrivateCapability::kMahoDownloadMetadata,
    };

    for (auto cap : all_caps) {
      bool expected =
          std::find(allowed.begin(), allowed.end(), cap) != allowed.end();
      EXPECT_EQ(MahoIsCapabilityAllowed(profile, cap), expected);
    }
  }
};

TEST_F(MahoPrivateContextPolicyTest, NullFailsClosed) {
  MahoPrivateContextToken token(nullptr, nullptr);
  EXPECT_EQ(token.context_class(), MahoPrivateContextClass::kNull);
  ExpectCapabilities(token, {});
}

TEST_F(MahoPrivateContextPolicyTest, RegularCapabilities) {
  MahoPrivateContextToken token(nullptr, web_contents());
  EXPECT_EQ(token.context_class(), MahoPrivateContextClass::kRegular);
  ExpectCapabilities(token, {
                                MahoPrivateCapability::kWindowLocalTabs,
                                MahoPrivateCapability::kProcessGlobalWebUI,
                                MahoPrivateCapability::kSavedReadMutate,
                                MahoPrivateCapability::kPersistentSplit,
                                MahoPrivateCapability::kMahoSearchReadMutate,
                                MahoPrivateCapability::kPreview,
                                MahoPrivateCapability::kAI,
                                MahoPrivateCapability::kMCP,
                                MahoPrivateCapability::kExtensionSnapshot,
                                MahoPrivateCapability::kMahoDownloadMetadata,
                            });
}

TEST_F(MahoPrivateContextPolicyTest, PrimaryIncognitoCapabilities) {
  TestingProfile* incognito_profile =
      TestingProfile::Builder().BuildIncognito(profile());
  std::unique_ptr<content::WebContents> incognito_contents =
      content::WebContentsTester::CreateTestWebContents(incognito_profile,
                                                        nullptr);
  MahoPrivateContextToken token(nullptr, incognito_contents.get());
  EXPECT_EQ(token.context_class(), MahoPrivateContextClass::kPrimaryIncognito);
  ExpectCapabilities(token, {
                                MahoPrivateCapability::kWindowLocalTabs,
                                MahoPrivateCapability::kPrivateVisuals,
                            });
}

TEST_F(MahoPrivateContextPolicyTest, GuestRegularFailsClosed) {
  std::unique_ptr<TestingProfile> guest_profile =
      TestingProfile::Builder().SetGuestSession().Build();
  EXPECT_EQ(MahoClassifyProfile(guest_profile.get()),
            MahoPrivateContextClass::kGuest);
  std::unique_ptr<content::WebContents> guest_contents =
      content::WebContentsTester::CreateTestWebContents(guest_profile.get(),
                                                        nullptr);
  MahoPrivateContextToken token(nullptr, guest_contents.get());
  EXPECT_EQ(token.context_class(), MahoPrivateContextClass::kGuest);
  ExpectCapabilities(token, {});
}

TEST_F(MahoPrivateContextPolicyTest, GuestPrimaryOtrFailsClosed) {
  std::unique_ptr<TestingProfile> guest_profile =
      TestingProfile::Builder().SetGuestSession().Build();
  Profile* guest_otr = guest_profile->GetPrimaryOTRProfile(true);
  EXPECT_EQ(MahoClassifyProfile(guest_otr),
            MahoPrivateContextClass::kGuest);
  std::unique_ptr<content::WebContents> guest_otr_contents =
      content::WebContentsTester::CreateTestWebContents(guest_otr, nullptr);
  MahoPrivateContextToken token(nullptr, guest_otr_contents.get());
  EXPECT_EQ(token.context_class(), MahoPrivateContextClass::kGuest);
  ExpectCapabilities(token, {});
}

TEST_F(MahoPrivateContextPolicyTest, SystemRegularAndOtrFailClosed) {
  std::unique_ptr<TestingProfile> system_profile =
      TestingProfile::Builder().Build();
  profile_metrics::SetBrowserProfileType(
      system_profile.get(), profile_metrics::BrowserProfileType::kSystem);
  EXPECT_EQ(MahoClassifyProfile(system_profile.get()),
            MahoPrivateContextClass::kSystem);

  std::unique_ptr<content::WebContents> system_contents =
      content::WebContentsTester::CreateTestWebContents(system_profile.get(),
                                                        nullptr);
  MahoPrivateContextToken token1(nullptr, system_contents.get());
  EXPECT_EQ(token1.context_class(), MahoPrivateContextClass::kSystem);
  ExpectCapabilities(token1, {});

  Profile* system_otr = system_profile->GetPrimaryOTRProfile(true);
  std::unique_ptr<content::WebContents> system_otr_contents =
      content::WebContentsTester::CreateTestWebContents(system_otr, nullptr);
  MahoPrivateContextToken token2(nullptr, system_otr_contents.get());
  EXPECT_EQ(token2.context_class(), MahoPrivateContextClass::kSystem);
  ExpectCapabilities(token2, {});

  // Reset profile types before destruction to avoid Service Manager mismatch crashes
  profile_metrics::SetBrowserProfileType(
      system_profile.get(), profile_metrics::BrowserProfileType::kRegular);
  profile_metrics::SetBrowserProfileType(
      system_otr, profile_metrics::BrowserProfileType::kIncognito);
}

TEST_F(MahoPrivateContextPolicyTest, DevToolsOtrFailsClosed) {
  Profile::OTRProfileID devtools_otr_id =
      Profile::OTRProfileID::CreateUniqueForDevTools();
  TestingProfile* devtools_otr_profile =
      TestingProfile::Builder().BuildOffTheRecord(profile(), devtools_otr_id);
  EXPECT_EQ(MahoClassifyProfile(devtools_otr_profile),
            MahoPrivateContextClass::kDevToolsOtr);

  std::unique_ptr<content::WebContents> devtools_contents =
      content::WebContentsTester::CreateTestWebContents(devtools_otr_profile,
                                                        nullptr);
  MahoPrivateContextToken token(nullptr, devtools_contents.get());
  EXPECT_EQ(token.context_class(), MahoPrivateContextClass::kDevToolsOtr);
  ExpectCapabilities(token, {});
}

TEST_F(MahoPrivateContextPolicyTest, NonPrimaryOtrFailsClosed) {
  Profile::OTRProfileID other_otr_id =
      Profile::OTRProfileID::CreateUniqueForTesting();
  TestingProfile* other_otr_profile =
      TestingProfile::Builder().BuildOffTheRecord(profile(), other_otr_id);
  EXPECT_EQ(MahoClassifyProfile(other_otr_profile),
            MahoPrivateContextClass::kOtherOtr);

  std::unique_ptr<content::WebContents> other_contents =
      content::WebContentsTester::CreateTestWebContents(other_otr_profile,
                                                        nullptr);
  MahoPrivateContextToken token(nullptr, other_contents.get());
  EXPECT_EQ(token.context_class(), MahoPrivateContextClass::kOtherOtr);
  ExpectCapabilities(token, {});
}

TEST_F(MahoPrivateContextPolicyTest, BrowserWebContentsAndTokenAgree) {
  std::unique_ptr<TestBrowserWindowInterface> browser_window =
      std::make_unique<TestBrowserWindowInterface>(profile());
  std::unique_ptr<TestTabStripModelDelegate> tab_strip_delegate =
      std::make_unique<TestTabStripModelDelegate>();
  tab_strip_delegate->SetBrowserWindowInterface(browser_window.get());
  std::unique_ptr<TabStripModel> tab_strip_model =
      std::make_unique<TabStripModel>(tab_strip_delegate.get(), profile());
  browser_window->SetTabStripModel(tab_strip_model.get());

  std::unique_ptr<content::WebContents> test_contents =
      content::WebContentsTester::CreateTestWebContents(profile(), nullptr);
  content::WebContents* contents_ptr = test_contents.get();

  auto tab_model = std::make_unique<tabs::TabModel>(std::move(test_contents),
                                                    tab_strip_model.get());
  tab_strip_model->AddTab(std::move(tab_model), 0, ui::PAGE_TRANSITION_LINK,
                          AddTabTypes::ADD_ACTIVE);

  MahoPrivateContextToken token(browser_window.get(), contents_ptr);
  EXPECT_EQ(token.context_class(), MahoPrivateContextClass::kRegular);

  EXPECT_TRUE(token.Revalidate(MahoPrivateCapability::kWindowLocalTabs));

  std::unique_ptr<tabs::TabModel> detached =
      tab_strip_model->DetachTabAtForInsertion(0);
  EXPECT_FALSE(token.Revalidate(MahoPrivateCapability::kWindowLocalTabs));

  tab_strip_model->AddTab(std::move(detached), 0, ui::PAGE_TRANSITION_LINK,
                          AddTabTypes::ADD_ACTIVE);
  EXPECT_TRUE(token.Revalidate(MahoPrivateCapability::kWindowLocalTabs));

  browser_window.reset();
  EXPECT_FALSE(token.Revalidate(MahoPrivateCapability::kWindowLocalTabs));
}

TEST_F(MahoPrivateContextPolicyTest, AccessorNullFailsClosed) {
  ExpectAccessorCapabilities(nullptr, {});
}

TEST_F(MahoPrivateContextPolicyTest, AccessorRegularCapabilities) {
  ExpectAccessorCapabilities(profile(), {
                                            MahoPrivateCapability::kWindowLocalTabs,
                                            MahoPrivateCapability::kProcessGlobalWebUI,
                                            MahoPrivateCapability::kSavedReadMutate,
                                            MahoPrivateCapability::kPersistentSplit,
                                            MahoPrivateCapability::kMahoSearchReadMutate,
                                            MahoPrivateCapability::kPreview,
                                            MahoPrivateCapability::kAI,
                                            MahoPrivateCapability::kMCP,
                                            MahoPrivateCapability::kExtensionSnapshot,
                                            MahoPrivateCapability::kMahoDownloadMetadata,
                                        });
}

TEST_F(MahoPrivateContextPolicyTest, AccessorPrimaryIncognitoCapabilities) {
  TestingProfile* incognito_profile =
      TestingProfile::Builder().BuildIncognito(profile());
  EXPECT_EQ(MahoClassifyProfile(incognito_profile),
            MahoPrivateContextClass::kPrimaryIncognito);
  ExpectAccessorCapabilities(incognito_profile, {
                                                    MahoPrivateCapability::kWindowLocalTabs,
                                                    MahoPrivateCapability::kPrivateVisuals,
                                                });
}

TEST_F(MahoPrivateContextPolicyTest, AccessorGuestFailsClosed) {
  std::unique_ptr<TestingProfile> guest_profile =
      TestingProfile::Builder().SetGuestSession().Build();
  ExpectAccessorCapabilities(guest_profile.get(), {});
  ExpectAccessorCapabilities(guest_profile->GetPrimaryOTRProfile(true), {});
}

TEST_F(MahoPrivateContextPolicyTest, AccessorDevToolsOtrFailsClosed) {
  Profile::OTRProfileID devtools_otr_id =
      Profile::OTRProfileID::CreateUniqueForDevTools();
  TestingProfile* devtools_otr_profile =
      TestingProfile::Builder().BuildOffTheRecord(profile(), devtools_otr_id);
  ExpectAccessorCapabilities(devtools_otr_profile, {});
}

TEST_F(MahoPrivateContextPolicyTest, AccessorNonPrimaryOtrFailsClosed) {
  Profile::OTRProfileID other_otr_id =
      Profile::OTRProfileID::CreateUniqueForTesting();
  TestingProfile* other_otr_profile =
      TestingProfile::Builder().BuildOffTheRecord(profile(), other_otr_id);
  ExpectAccessorCapabilities(other_otr_profile, {});
}

}  // namespace
