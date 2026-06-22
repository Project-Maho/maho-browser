// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/importer/maho_space_ffi_client.h"

#include <memory>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/test/task_environment.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho {
namespace {

class ScopedCoreOverride {
 public:
  explicit ScopedCoreOverride(MahoCore* core) : saved_(GetCore()) {
    SetCore(core);
  }
  ~ScopedCoreOverride() { SetCore(saved_.get()); }

 private:
  raw_ptr<MahoCore> saved_;
};

class MahoSpaceFFIClientTest : public testing::Test {
 protected:
  void SetUp() override {
    core_ = maho_core_new();
    ASSERT_NE(core_, nullptr);
    scoped_core_override_ = std::make_unique<ScopedCoreOverride>(core_);
    client_ = std::make_unique<MahoSpaceFFIClient>();
  }

  void TearDown() override {
    client_.reset();
    scoped_core_override_.reset();
  }

  base::test::TaskEnvironment task_environment_;
  raw_ptr<MahoCore> core_ = nullptr;
  std::unique_ptr<ScopedCoreOverride> scoped_core_override_;
  std::unique_ptr<MahoSpaceFFIClient> client_;
};

TEST_F(MahoSpaceFFIClientTest, CreateSpaceAndTab) {
  std::string space_id = client_->CreateSpace("Test Space", "#ff0000", "home");
  EXPECT_FALSE(space_id.empty());

  std::string tab_id = client_->CreateTab(space_id, "https://example.com", "Example");
  EXPECT_FALSE(tab_id.empty());

  client_->PinTab(tab_id);
  client_->FavoriteTab(tab_id);
}

TEST_F(MahoSpaceFFIClientTest, FolderNestingDepthCap) {
  std::string space_id = client_->CreateSpace("Nesting Space", "#00ff00", "folder");
  ASSERT_FALSE(space_id.empty());

  client_->ResetFolderDepths();

  std::string f1 = client_->CreateFolder(space_id, "L1");
  EXPECT_FALSE(f1.empty());

  std::string f2 = client_->CreateFolder(space_id, "L2", f1);
  EXPECT_FALSE(f2.empty());
  EXPECT_NE(f1, f2);

  std::string f3 = client_->CreateFolder(space_id, "L3", f2);
  EXPECT_FALSE(f3.empty());
  EXPECT_NE(f2, f3);

  std::string f4 = client_->CreateFolder(space_id, "L4", f3);
  EXPECT_FALSE(f4.empty());
  EXPECT_NE(f3, f4);

  std::string f5 = client_->CreateFolder(space_id, "L5", f4);
  EXPECT_FALSE(f5.empty());
  EXPECT_NE(f4, f5);

  // 6th folder should be capped and return parent ID (f5)
  std::string f6 = client_->CreateFolder(space_id, "L6", f5);
  EXPECT_EQ(f6, f5);
}

TEST_F(MahoSpaceFFIClientTest, SetSpaceIcon) {
  std::string space_id = client_->CreateSpace("Icon Space", "#0000ff", "star");
  ASSERT_FALSE(space_id.empty());

  client_->SetSpaceIcon(space_id, "rocket");
  client_->SetSpaceIcon(space_id, "home");
}

TEST_F(MahoSpaceFFIClientTest, MoveTabToSpace) {
  std::string src_space_id = client_->CreateSpace("Source Space", "#ff0000", "box");
  ASSERT_FALSE(src_space_id.empty());

  std::string dst_space_id = client_->CreateSpace("Dest Space", "#00ff00", "box");
  ASSERT_FALSE(dst_space_id.empty());

  std::string tab_id = client_->CreateTab(src_space_id, "https://example.com", "Example");
  ASSERT_FALSE(tab_id.empty());

  client_->MoveTabToSpace(tab_id, dst_space_id);
}

}  // namespace
}  // namespace maho
