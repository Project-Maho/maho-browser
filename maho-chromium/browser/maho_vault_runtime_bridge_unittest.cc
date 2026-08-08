// Copyright 2026 Maho Browser. All rights reserved.

#include <string>
#include <type_traits>
#include <utility>

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
    SetCore(reinterpret_cast<MahoCore*>(0x1234));
  }
  void TearDown() override {
    SetCore(nullptr);
    core::SetVaultBackendFfiForTesting(nullptr);
    g_fake_ffi = nullptr;
  }
 protected:
  FakeVaultBackendFfi ffi_;
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

}  // namespace
}  // namespace maho
