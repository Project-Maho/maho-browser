// Copyright 2026 Maho Browser. All rights reserved.

#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/run_loop.h"
#include "base/synchronization/waitable_event.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/test/base/testing_browser_process.h"
#include "chrome/test/base/testing_profile.h"
#include "chrome/test/base/testing_profile_manager.h"
#include "content/public/test/browser_task_environment.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/third_party/maho/maho_bridge.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho {
namespace {

struct FakeVaultBackendFfi {
  int session_close_calls = 0;
  int session_free_calls = 0;
  int session_execute_calls = 0;
  int result_free_calls = 0;
  int buffer_free_calls = 0;
  const char* payload = "{\"items\":[]}";
};
FakeVaultBackendFfi* g_fake_ffi = nullptr;
MahoVaultBackendSession* FakeSessionNew(MahoCore*) { return reinterpret_cast<MahoVaultBackendSession*>(0x1); }
void FakeSessionClose(MahoVaultBackendSession*) { ++g_fake_ffi->session_close_calls; }
void FakeSessionFree(MahoVaultBackendSession*) { ++g_fake_ffi->session_free_calls; }
MahoVaultBackendResult* FakeSessionExecute(MahoVaultBackendSession*, const char*) {
  ++g_fake_ffi->session_execute_calls;
  return reinterpret_cast<MahoVaultBackendResult*>(0x2);
}
MahoVaultBackendResult* FakeSessionExecuteCredentials(MahoVaultBackendSession*, const char*) {
  ++g_fake_ffi->session_execute_calls;
  return reinterpret_cast<MahoVaultBackendResult*>(0x2);
}
int FakeResultStatus(const MahoVaultBackendResult*) { return static_cast<int>(core::VaultBackendResultStatus::kSuccess); }
MahoVaultBackendBuffer* FakeResultConsume(MahoVaultBackendResult*) { return reinterpret_cast<MahoVaultBackendBuffer*>(0x3); }
void FakeResultFree(MahoVaultBackendResult*) { ++g_fake_ffi->result_free_calls; }
const uint8_t* FakeBufferData(const MahoVaultBackendBuffer*) { return reinterpret_cast<const uint8_t*>(g_fake_ffi->payload); }
size_t FakeBufferLen(const MahoVaultBackendBuffer*) { return std::char_traits<char>::length(g_fake_ffi->payload); }
void FakeBufferFree(MahoVaultBackendBuffer*) { ++g_fake_ffi->buffer_free_calls; }
const core::VaultBackendFfiForTesting kFakeFfi = {
    &FakeSessionNew, &FakeSessionClose, &FakeSessionFree, &FakeSessionExecute,
    &FakeSessionExecuteCredentials, &FakeResultStatus, &FakeResultConsume,
    &FakeResultFree, &FakeBufferData, &FakeBufferLen, &FakeBufferFree};

class MahoVaultRuntimeBridgeTest : public testing::Test {
 public:
  void SetUp() override {
    g_fake_ffi = &ffi_;
    core::SetVaultBackendFfiForTesting(&kFakeFfi);
    owner_profile_ = TestingProfile::Builder().Build();
    ASSERT_NE(owner_profile_, nullptr);
    // The core must be installed WITH its owning profile: CreateVaultBackendSession()
    // routes through IsPasswordManagerAllowedForProfile(GetCoreOwnerProfile()),
    // so a core set via SetCore() alone has a null owner and yields an invalid
    // session, which would make every positive assertion below unreachable.
    SetCoreForProfile(reinterpret_cast<MahoCore*>(0x1234),
                      owner_profile_.get());
  }
  void TearDown() override {
    SetCore(nullptr);
    core::SetVaultBackendFfiForTesting(nullptr);
    g_fake_ffi = nullptr;
    owner_profile_.reset();
  }

 protected:
  TestingProfile* owner_profile() { return owner_profile_.get(); }

  content::BrowserTaskEnvironment task_environment_;
  FakeVaultBackendFfi ffi_;
  std::unique_ptr<TestingProfile> owner_profile_;
};

TEST(MahoVaultRuntimeBridgeTypesTest, WrappersAreMoveOnly) {
  static_assert(!std::is_copy_constructible_v<core::VaultBackendSession>);
  static_assert(!std::is_copy_constructible_v<core::VaultBackendResult>);
  static_assert(!std::is_copy_constructible_v<core::VaultBackendBuffer>);
  static_assert(std::is_move_constructible_v<core::VaultBackendSession>);
  static_assert(std::is_move_constructible_v<core::VaultBackendResult>);
  static_assert(std::is_move_constructible_v<core::VaultBackendBuffer>);
}

TEST_F(MahoVaultRuntimeBridgeTest, SessionIsFreedExactlyOnceAfterMove) {
  { core::VaultBackendSession session = CreateVaultBackendSession();
    ASSERT_TRUE(session.is_valid());
    core::VaultBackendSession moved_session = std::move(session);
    EXPECT_FALSE(session.is_valid());
    EXPECT_TRUE(moved_session.is_valid()); }
  EXPECT_EQ(ffi_.session_free_calls, 1);
}

TEST_F(MahoVaultRuntimeBridgeTest, ResultAndBufferUseDedicatedBackendFree) {
  { core::VaultBackendSession session = CreateVaultBackendSession();
    core::VaultBackendResult result = session.ExecuteBatch("{}");
    ASSERT_EQ(result.status(), core::VaultBackendResultStatus::kSuccess);
    EXPECT_EQ(result.ConsumeJson(), "{\"items\":[]}"); }
  EXPECT_EQ(ffi_.result_free_calls, 1);
  EXPECT_EQ(ffi_.buffer_free_calls, 1);
}

TEST_F(MahoVaultRuntimeBridgeTest, CoreReadyCallbackCanCreateVaultSession) {
  bool callback_ran = false;
  base::CallbackListSubscription subscription = AddCoreReadyCallback(
      base::BindRepeating([](TestingProfile* profile, bool* callback_ran) {
        *callback_ran =
            CreateVaultBackendSessionForProfile(profile).is_valid();
      },
      owner_profile(), &callback_ran));

  SetCore(nullptr);
  SetCoreForProfile(reinterpret_cast<MahoCore*>(0x1234), owner_profile());
  EXPECT_TRUE(callback_ran);
}

TEST_F(MahoVaultRuntimeBridgeTest,
       ClearedCoreSuppressesQueuedOffUiReadyCallback) {
  SetCore(nullptr);
  int callback_count = 0;
  base::CallbackListSubscription subscription = AddCoreReadyCallback(
      base::BindRepeating([](int* callback_count) { ++*callback_count; },
                          &callback_count));
  base::WaitableEvent core_set_on_worker(
      base::WaitableEvent::ResetPolicy::MANUAL,
      base::WaitableEvent::InitialState::NOT_SIGNALED);
  ASSERT_TRUE(base::ThreadPool::PostTask(
      FROM_HERE, {base::MayBlock()},
      base::BindOnce(
          [](TestingProfile* profile, base::WaitableEvent* core_set_on_worker) {
            SetCoreForProfile(reinterpret_cast<MahoCore*>(0x1234), profile);
            core_set_on_worker->Signal();
          },
          owner_profile(), &core_set_on_worker)));

  ASSERT_TRUE(core_set_on_worker.TimedWait(base::Seconds(5)));
  SetCore(nullptr);
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(callback_count, 0);
}

TEST_F(MahoVaultRuntimeBridgeTest, HolderQuiesceRejectsLateWorkerWithoutCoreAccess) {
  core::VaultBackendSession session = CreateVaultBackendSession();
  ASSERT_TRUE(session.is_valid());
  SetCore(nullptr);
  EXPECT_TRUE(IsVaultBackendSessionWorkQuiesced());
  EXPECT_EQ(ffi_.session_close_calls, 1);
  auto worker = [session = std::move(session)]() mutable { return session.ExecuteBatch("{}"); };
  EXPECT_FALSE(worker().is_valid());
  EXPECT_EQ(ffi_.session_execute_calls, 0);
}

TEST_F(MahoVaultRuntimeBridgeTest, SessionFailsSafelyAfterCoreIsCleared) {
  core::VaultBackendSession session = CreateVaultBackendSession();
  ASSERT_TRUE(session.is_valid());
  SetCore(nullptr);
  EXPECT_FALSE(session.ExecuteBatch("{}").is_valid());
  EXPECT_EQ(ffi_.session_execute_calls, 0);
}

// ── Password-manager profile ownership (remediation plan, decision 3) ──
//
// MahoCore is browser-global but the password manager must behave as if it were
// profile-scoped: only the ONE regular profile that created the core may reach
// credentials. Everything else — no core, no owner, incognito/OTR, guest,
// system, and any SECOND regular profile — fails closed.
//
// Failing-first rationale: before owner binding existed there was no
// GetCoreOwnerProfile() to compare against, so every profile that reached these
// entry points was admitted; each expectation below except the owner-ALLOW case
// asserts a denial that did not previously happen, and the owner-ALLOW case
// pins that the new check did not simply deny everyone.
class MahoPasswordProfileOwnershipTest : public testing::Test {
 public:
  MahoPasswordProfileOwnershipTest()
      : profile_manager_(TestingBrowserProcess::GetGlobal()) {}

  void SetUp() override {
    ASSERT_TRUE(profile_manager_.SetUp());
    owner_profile_ = profile_manager_.CreateTestingProfile("maho-owner");
    ASSERT_NE(owner_profile_, nullptr);
    SetCoreForProfile(reinterpret_cast<MahoCore*>(0x1234), owner_profile_);
  }

  void TearDown() override { SetCore(nullptr); }

 protected:
  content::BrowserTaskEnvironment task_environment_;
  TestingProfileManager profile_manager_;
  raw_ptr<TestingProfile> owner_profile_ = nullptr;
};

TEST_F(MahoPasswordProfileOwnershipTest, NullProfileIsDenied) {
  EXPECT_FALSE(IsPasswordManagerAllowedForProfile(nullptr));
  EXPECT_FALSE(CreateVaultBackendSessionForProfile(nullptr).is_valid());
}

TEST_F(MahoPasswordProfileOwnershipTest, OwnerRegularProfileIsAllowed) {
  ASSERT_EQ(owner_profile_, GetCoreOwnerProfile());
  EXPECT_TRUE(IsPasswordManagerAllowedForProfile(owner_profile_));
}

TEST_F(MahoPasswordProfileOwnershipTest, SecondRegularProfileIsDenied) {
  // A second regular profile is a legitimate Chromium profile in every other
  // respect; it is denied purely because it does not own the core. This is the
  // case that makes the browser-global core safe to keep.
  TestingProfile* second_profile =
      profile_manager_.CreateTestingProfile("maho-second");
  ASSERT_NE(second_profile, nullptr);
  ASSERT_NE(second_profile, owner_profile_);
  ASSERT_TRUE(second_profile->IsRegularProfile());

  EXPECT_FALSE(IsPasswordManagerAllowedForProfile(second_profile));
  EXPECT_FALSE(CreateVaultBackendSessionForProfile(second_profile).is_valid());
}

TEST_F(MahoPasswordProfileOwnershipTest, IncognitoAndOtherOtrProfilesAreDenied) {
  // The primary OTR profile of the OWNER itself: same underlying profile, so it
  // would pass a naive pointer/identity check, yet it must still be denied.
  Profile* primary_otr = owner_profile_->GetPrimaryOTRProfile(
      /*create_if_needed=*/true);
  ASSERT_NE(primary_otr, nullptr);
  ASSERT_TRUE(primary_otr->IsOffTheRecord());
  EXPECT_FALSE(IsPasswordManagerAllowedForProfile(primary_otr));
  EXPECT_FALSE(CreateVaultBackendSessionForProfile(primary_otr).is_valid());

  const Profile::OTRProfileID other_otr_id =
      Profile::OTRProfileID::CreateUniqueForTesting();
  Profile* other_otr = owner_profile_->GetOffTheRecordProfile(
      other_otr_id, /*create_if_needed=*/true);
  ASSERT_NE(other_otr, nullptr);
  ASSERT_TRUE(other_otr->IsOffTheRecord());
  EXPECT_FALSE(IsPasswordManagerAllowedForProfile(other_otr));
  EXPECT_FALSE(CreateVaultBackendSessionForProfile(other_otr).is_valid());
}

TEST_F(MahoPasswordProfileOwnershipTest, GuestProfileIsDenied) {
  TestingProfile* guest_profile = profile_manager_.CreateGuestProfile();
  ASSERT_NE(guest_profile, nullptr);
  ASSERT_FALSE(guest_profile->IsRegularProfile());

  EXPECT_FALSE(IsPasswordManagerAllowedForProfile(guest_profile));
  EXPECT_FALSE(CreateVaultBackendSessionForProfile(guest_profile).is_valid());
}

#if !BUILDFLAG(IS_CHROMEOS) && !BUILDFLAG(IS_ANDROID)
TEST_F(MahoPasswordProfileOwnershipTest, SystemProfileIsDenied) {
  TestingProfile* system_profile = profile_manager_.CreateSystemProfile();
  ASSERT_NE(system_profile, nullptr);
  ASSERT_FALSE(system_profile->IsRegularProfile());

  EXPECT_FALSE(IsPasswordManagerAllowedForProfile(system_profile));
  EXPECT_FALSE(CreateVaultBackendSessionForProfile(system_profile).is_valid());
}
#endif

TEST_F(MahoPasswordProfileOwnershipTest, OwnerIsDeniedOnceCoreIsGone) {
  // Ownership alone is not sufficient: with no core there is nothing to
  // authorize against, so even the owner must fail closed rather than be
  // admitted on identity.
  SetCore(nullptr);
  ASSERT_EQ(nullptr, GetCore());

  EXPECT_FALSE(IsPasswordManagerAllowedForProfile(owner_profile_));
  EXPECT_FALSE(CreateVaultBackendSessionForProfile(owner_profile_).is_valid());
}

TEST_F(MahoPasswordProfileOwnershipTest, UnownedCoreDeniesEveryProfile) {
  // SetCore() (no owner) is the legacy/test-only entry point. It leaves the
  // owner null, which must deny everyone rather than fall back to "any regular
  // profile".
  SetCore(reinterpret_cast<MahoCore*>(0x1234));
  ASSERT_EQ(nullptr, GetCoreOwnerProfile());

  EXPECT_FALSE(IsPasswordManagerAllowedForProfile(owner_profile_));
  EXPECT_FALSE(CreateVaultBackendSession().is_valid());
}

}  // namespace
}  // namespace maho
