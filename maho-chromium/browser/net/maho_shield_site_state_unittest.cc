// Copyright 2026 Maho Browser. All rights reserved.

#include <memory>
#include <string>

#include "base/memory/raw_ptr.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/test/base/testing_profile.h"
#include "content/public/test/test_renderer_host.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/net/maho_shield_site_state.h"
#include "maho/browser/net/maho_shield_site_state_factory.h"
#include "maho/third_party/maho/maho_bridge.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho {
namespace {

struct MahoCoreDeleter {
  void operator()(MahoCore* core) const { maho::core::Destroy(core); }
};

using ScopedMahoCore = std::unique_ptr<MahoCore, MahoCoreDeleter>;

class ScopedCoreOverride {
 public:
  explicit ScopedCoreOverride(MahoCore* core) : saved_core_(GetCore()) {
    SetCore(core);
  }

  ScopedCoreOverride(const ScopedCoreOverride&) = delete;
  ScopedCoreOverride& operator=(const ScopedCoreOverride&) = delete;

  ~ScopedCoreOverride() { SetCore(saved_core_); }

 private:
  raw_ptr<MahoCore> saved_core_ = nullptr;
};

class MahoShieldSiteStateTest : public content::RenderViewHostTestHarness {
 protected:
  std::unique_ptr<content::BrowserContext> CreateBrowserContext() override {
    return TestingProfile::Builder().Build();
  }

  TestingProfile* testing_profile() {
    return static_cast<TestingProfile*>(browser_context());
  }
};

TEST_F(MahoShieldSiteStateTest, RegularProfileReadsWritesCore) {
  ScopedMahoCore core(maho::core::Create());
  ASSERT_TRUE(core);
  ScopedCoreOverride scoped_core(core.get());

  MahoShieldSiteState* state =
      MahoShieldSiteStateFactory::GetForProfile(testing_profile());
  ASSERT_NE(state, nullptr);

  const std::string origin = "https://example.com";
  EXPECT_EQ(state->GetEffectiveContentBlockingMode(),
            maho::core::GetContentBlockingMode(core.get()));
  EXPECT_FALSE(state->IsSiteExceptedForOrigin(origin));

  // RED intent: before the facade exists, regular callers either cannot compile
  // or keep bypassing the Profile-keyed boundary. The add must be observable in
  // the Rust core because regular profiles write through to global core state.
  state->AddSiteException(origin);
  EXPECT_TRUE(state->IsSiteExceptedForOrigin(origin));
  EXPECT_NE(maho::core::GetSiteExceptions(core.get()).find("example.com"),
            std::string::npos);

  // RED intent: removal must also write through; a facade that only updates an
  // in-memory copy for regular profiles leaves the Rust core dirty here.
  state->RemoveSiteException(origin);
  EXPECT_FALSE(state->IsSiteExceptedForOrigin(origin));
  EXPECT_EQ(maho::core::GetSiteExceptions(core.get()), "[]");
}

TEST_F(MahoShieldSiteStateTest,
       RegularProfileMatchesSubdomainWithParentException) {
  ScopedMahoCore core(maho::core::Create());
  ASSERT_TRUE(core);
  ScopedCoreOverride scoped_core(core.get());

  MahoShieldSiteState* state =
      MahoShieldSiteStateFactory::GetForProfile(testing_profile());
  ASSERT_NE(state, nullptr);

  // RED intent: the facade must rely on Rust's PSL-backed normalization, not a
  // local C++ host matcher that can drift from core storage semantics.
  state->AddSiteException("https://example.com");
  EXPECT_TRUE(state->IsSiteExceptedForOrigin("https://sub.example.com"));
}

TEST_F(MahoShieldSiteStateTest,
       OffTheRecordOverlayNormalizesSubdomainException) {
  ScopedMahoCore core(maho::core::Create());
  ASSERT_TRUE(core);
  ScopedCoreOverride scoped_core(core.get());

  MahoShieldSiteState* regular_state =
      MahoShieldSiteStateFactory::GetForProfile(testing_profile());
  ASSERT_NE(regular_state, nullptr);

  Profile* otr_profile =
      testing_profile()->GetPrimaryOTRProfile(/*create_if_needed=*/true);
  MahoShieldSiteState* otr_state =
      MahoShieldSiteStateFactory::GetForProfile(otr_profile);
  ASSERT_NE(otr_state, nullptr);

  // RED intent: raw-host overlay storage made this false for OTR because
  // "sub.example.com" did not become the Rust/core key "example.com".
  otr_state->AddSiteException("https://sub.example.com");
  EXPECT_TRUE(otr_state->IsSiteExceptedForOrigin("https://example.com"));
  EXPECT_TRUE(otr_state->IsSiteExceptedForOrigin("https://other.example.com"));
  EXPECT_FALSE(regular_state->IsSiteExceptedForOrigin("https://example.com"));
  EXPECT_EQ(maho::core::GetSiteExceptions(core.get()), "[]");
}

TEST_F(MahoShieldSiteStateTest, OffTheRecordStateIsSessionScoped) {
  ScopedMahoCore core(maho::core::Create());
  ASSERT_TRUE(core);
  ScopedCoreOverride scoped_core(core.get());
  ASSERT_TRUE(maho::core::SetContentBlockingMode(core.get(), 0));

  MahoShieldSiteState* regular_state =
      MahoShieldSiteStateFactory::GetForProfile(testing_profile());
  ASSERT_NE(regular_state, nullptr);

  Profile* otr_profile =
      testing_profile()->GetPrimaryOTRProfile(/*create_if_needed=*/true);
  MahoShieldSiteState* otr_state =
      MahoShieldSiteStateFactory::GetForProfile(otr_profile);
  ASSERT_NE(otr_state, nullptr);
  EXPECT_NE(otr_state, regular_state);
  EXPECT_EQ(otr_state->GetEffectiveContentBlockingMode(),
            regular_state->GetEffectiveContentBlockingMode());

  const std::string private_origin = "https://private.example";

  // RED intent: with direct maho::GetCore() mutation this leaks into the
  // regular Rust core. The OTR facade must mutate only its per-session overlay.
  otr_state->AddSiteException(private_origin);
  EXPECT_TRUE(otr_state->IsSiteExceptedForOrigin(private_origin));
  EXPECT_FALSE(regular_state->IsSiteExceptedForOrigin(private_origin));
  EXPECT_EQ(maho::core::GetSiteExceptions(core.get()), "[]");
  EXPECT_EQ(maho::core::GetContentBlockingMode(core.get()), 0);

  // RED intent: destroying the OTR profile tears down the keyed service and its
  // overlay. A freshly-created OTR service must copy regular state again and not
  // retain the private-only exception from the prior session.
  testing_profile()->DestroyOffTheRecordProfile(otr_profile);
  Profile* fresh_otr_profile =
      testing_profile()->GetPrimaryOTRProfile(/*create_if_needed=*/true);
  MahoShieldSiteState* fresh_otr_state =
      MahoShieldSiteStateFactory::GetForProfile(fresh_otr_profile);
  ASSERT_NE(fresh_otr_state, nullptr);
  EXPECT_FALSE(fresh_otr_state->IsSiteExceptedForOrigin(private_origin));
  EXPECT_EQ(fresh_otr_state->GetEffectiveContentBlockingMode(),
            regular_state->GetEffectiveContentBlockingMode());
}

}  // namespace
}  // namespace maho
