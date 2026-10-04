// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "maho/browser/ui/views/agent/maho_tab_borrow_chip.h"

#include <string>

#include "testing/gtest/include/gtest/gtest.h"
#include "ui/accessibility/ax_mode.h"
#include "ui/accessibility/platform/ax_platform.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/views/layout/layout_provider.h"

namespace maho {
namespace {

class FakeAXPlatform : public ui::AXPlatform::Delegate {
 public:
  FakeAXPlatform() = default;
  ~FakeAXPlatform() override = default;

  ui::AXMode GetAccessibilityMode() override { return ui::AXMode(); }
#if BUILDFLAG(IS_WIN)
  ui::AXPlatform::ProductStrings GetProductStrings() override { return {}; }
#endif

 private:
  ui::AXPlatform ax_platform_{*this};
};

class FakeBorrowDelegate : public MahoTabBorrowChip::Delegate {
 public:
  void OnBorrowApproved(const std::string& request_id) override {
    last_request_id = request_id;
    approved = true;
  }
  void OnBorrowDenied(const std::string& request_id) override {
    last_request_id = request_id;
    denied = true;
  }

  std::string last_request_id;
  bool approved = false;
  bool denied = false;
};

class MahoTabBorrowChipTest : public testing::Test {
 protected:
  void SetUp() override {
    if (!ui::ResourceBundle::HasSharedInstance()) {
      ui::ResourceBundle::InitSharedInstanceWithLocale(
          "en-US", nullptr, ui::ResourceBundle::LOAD_COMMON_RESOURCES);
      cleanup_resource_bundle_ = true;
    }
    delegate_ = std::make_unique<FakeBorrowDelegate>();
    chip_ = std::make_unique<MahoTabBorrowChip>(delegate_.get());
  }

  void TearDown() override {
    chip_.reset();
    delegate_.reset();
    if (cleanup_resource_bundle_) {
      ui::ResourceBundle::CleanupSharedInstance();
      cleanup_resource_bundle_ = false;
    }
  }

  bool cleanup_resource_bundle_ = false;
  FakeAXPlatform fake_ax_platform_;
  views::LayoutProvider layout_provider_;
  std::unique_ptr<FakeBorrowDelegate> delegate_;
  std::unique_ptr<MahoTabBorrowChip> chip_;
};

TEST_F(MahoTabBorrowChipTest, ShowStoresIdAndVisible) {
  EXPECT_FALSE(chip_->is_showing());
  chip_->ShowRequest("req-1", "Example Domain");
  EXPECT_TRUE(chip_->is_showing());
  EXPECT_EQ(chip_->active_request_id(), "req-1");
}

TEST_F(MahoTabBorrowChipTest, AllowInvokesDelegate) {
  chip_->ShowRequest("req-1", "Example Domain");
  delegate_->OnBorrowApproved("req-1");
  EXPECT_TRUE(delegate_->approved);
  EXPECT_EQ(delegate_->last_request_id, "req-1");
}

TEST_F(MahoTabBorrowChipTest, DenyInvokesDelegate) {
  chip_->ShowRequest("req-2", "Another Tab");
  delegate_->OnBorrowDenied("req-2");
  EXPECT_TRUE(delegate_->denied);
  EXPECT_EQ(delegate_->last_request_id, "req-2");
}

TEST_F(MahoTabBorrowChipTest, DismissClearsState) {
  chip_->ShowRequest("req-1", "Example Domain");
  EXPECT_TRUE(chip_->is_showing());
  chip_->Dismiss();
  EXPECT_FALSE(chip_->is_showing());
  EXPECT_TRUE(chip_->active_request_id().empty());
}

TEST_F(MahoTabBorrowChipTest, RequestIdIsolatedBetweenRequests) {
  chip_->ShowRequest("req-1", "Tab 1");
  EXPECT_EQ(chip_->active_request_id(), "req-1");
  chip_->Dismiss();
  EXPECT_TRUE(chip_->active_request_id().empty());

  chip_->ShowRequest("req-2", "Tab 2");
  EXPECT_EQ(chip_->active_request_id(), "req-2");
}

}  // namespace
}  // namespace maho
