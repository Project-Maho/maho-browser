// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "maho/browser/ui/views/agent/maho_agent_help_bar.h"

#include <string>

#include "testing/gtest/include/gtest/gtest.h"
#include "ui/accessibility/ax_mode.h"
#include "ui/accessibility/platform/ax_platform.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/views/layout/layout_provider.h"

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

class FakeHelpDelegate : public MahoAgentHelpBar::Delegate {
 public:
  void OnHelpCompleted(const std::string& request_id) override {
    last_request_id = request_id;
    completed = true;
  }
  void OnHelpCancelled(const std::string& request_id) override {
    last_request_id = request_id;
    cancelled = true;
  }

  std::string last_request_id;
  bool completed = false;
  bool cancelled = false;
};

class MahoAgentHelpBarTest : public testing::Test {
 protected:
  void SetUp() override {
    if (!ui::ResourceBundle::HasSharedInstance()) {
      ui::ResourceBundle::InitSharedInstanceWithLocale(
          "en-US", nullptr, ui::ResourceBundle::LOAD_COMMON_RESOURCES);
      cleanup_resource_bundle_ = true;
    }
    delegate_ = std::make_unique<FakeHelpDelegate>();
    bar_ = std::make_unique<MahoAgentHelpBar>(delegate_.get());
  }
  void TearDown() override {
    bar_.reset();
    delegate_.reset();
    if (cleanup_resource_bundle_) {
      ui::ResourceBundle::CleanupSharedInstance();
      cleanup_resource_bundle_ = false;
    }
  }

  bool cleanup_resource_bundle_ = false;
  FakeAXPlatform fake_ax_platform_;
  views::LayoutProvider layout_provider_;
  std::unique_ptr<FakeHelpDelegate> delegate_;
  std::unique_ptr<MahoAgentHelpBar> bar_;
};

TEST_F(MahoAgentHelpBarTest, ShowRequestMakesVisibleAndStoresId) {
  EXPECT_FALSE(bar_->is_showing());
  bar_->ShowRequest("req-1", "Solve the CAPTCHA");
  EXPECT_TRUE(bar_->is_showing());
  EXPECT_EQ(bar_->active_request_id(), "req-1");
}

TEST_F(MahoAgentHelpBarTest, CompleteButtonInvokesDelegate) {
  bar_->ShowRequest("req-1", "Solve the CAPTCHA");
  bar_->Dismiss();
  bar_->ShowRequest("req-2", "2FA prompt");
  delegate_->OnHelpCompleted("req-2");
  EXPECT_TRUE(delegate_->completed);
  EXPECT_EQ(delegate_->last_request_id, "req-2");
}

TEST_F(MahoAgentHelpBarTest, CancelButtonInvokesDelegate) {
  bar_->ShowRequest("req-1", "Checkout");
  delegate_->OnHelpCancelled("req-1");
  EXPECT_TRUE(delegate_->cancelled);
  EXPECT_EQ(delegate_->last_request_id, "req-1");
}

TEST_F(MahoAgentHelpBarTest, DismissClearsState) {
  bar_->ShowRequest("req-1", "Checkout");
  EXPECT_TRUE(bar_->is_showing());
  bar_->Dismiss();
  EXPECT_FALSE(bar_->is_showing());
  EXPECT_EQ(bar_->active_request_id(), "");
}

TEST_F(MahoAgentHelpBarTest, RequestIdIsolatedBetweenRequests) {
  bar_->ShowRequest("req-1", "First");
  bar_->Dismiss();
  bar_->ShowRequest("req-2", "Second");
  EXPECT_EQ(bar_->active_request_id(), "req-2");
}

}  // namespace
