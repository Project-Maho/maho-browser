// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/extensions/api/maho_side_panel_api.h"
#include "maho/browser/extensions/api/maho_split_view_api.h"

#include <string>

#include "base/memory/scoped_refptr.h"
#include "chrome/test/base/testing_profile.h"
#include "content/public/test/browser_task_environment.h"
#include "extensions/browser/api_test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace extensions {
namespace {

// Exact error string emitted by MahoSidePanelSetLayoutFunction when the layout
// argument fails validation. Kept in sync with maho_side_panel_api.cc.
constexpr char kLayoutError[] = "layout must be 'left' or 'right'.";
// Exact error emitted by the split-view functions when no Browser is bound to
// the profile. Kept in sync with maho_split_view_api.cc.
constexpr char kNoBrowserError[] = "No active browser window for this profile.";

class MahoExtensionApiTest : public testing::Test {
 protected:
  content::BrowserTaskEnvironment task_environment_;
  TestingProfile profile_;
};

// --- maho.splitView.{create,close,query} ------------------------------------
//
// These functions require a live Browser for the active profile. In a unit-tier
// harness (TestingProfile with no Browser window) chrome::FindBrowserWithProfile
// returns null and each function must fail with the no-browser guard error.
// The success path (an actual split being created) needs a real Browser +
// BrowserView and is therefore only exercisable under browser_tests.

TEST_F(MahoExtensionApiTest, SplitViewCreateErrorsWithoutBrowser) {
  auto function = base::MakeRefCounted<MahoSplitViewCreateFunction>();
  const std::string error = api_test_utils::RunFunctionAndReturnError(
      function, "[]", &profile_);
  EXPECT_EQ(error, kNoBrowserError);
}

TEST_F(MahoExtensionApiTest, SplitViewCreateWithUrlErrorsWithoutBrowser) {
  auto function = base::MakeRefCounted<MahoSplitViewCreateFunction>();
  const std::string error = api_test_utils::RunFunctionAndReturnError(
      function, "[\"https://example.com/\"]", &profile_);
  EXPECT_EQ(error, kNoBrowserError);
}

TEST_F(MahoExtensionApiTest, SplitViewCloseErrorsWithoutBrowser) {
  auto function = base::MakeRefCounted<MahoSplitViewCloseFunction>();
  const std::string error = api_test_utils::RunFunctionAndReturnError(
      function, "[]", &profile_);
  EXPECT_EQ(error, kNoBrowserError);
}

TEST_F(MahoExtensionApiTest, SplitViewQueryErrorsWithoutBrowser) {
  auto function = base::MakeRefCounted<MahoSplitViewQueryFunction>();
  const std::string error = api_test_utils::RunFunctionAndReturnError(
      function, "[]", &profile_);
  EXPECT_EQ(error, kNoBrowserError);
}

// --- maho.sidePanel.setLayout -----------------------------------------------
//
// Layout-argument validation happens before any Browser/coordinator lookup, so
// it is fully unit-testable. Invalid arguments must produce the layout error;
// the accepted values "left"/"right" must pass validation (they then fail later
// with a *different* error because no calling extension / coordinator exists in
// this harness, which we assert to prove the value was accepted).

TEST_F(MahoExtensionApiTest, SidePanelSetLayoutRejectsMissingArg) {
  auto function = base::MakeRefCounted<MahoSidePanelSetLayoutFunction>();
  const std::string error = api_test_utils::RunFunctionAndReturnError(
      function, "[]", &profile_);
  EXPECT_EQ(error, kLayoutError);
}

TEST_F(MahoExtensionApiTest, SidePanelSetLayoutRejectsNonStringArg) {
  auto function = base::MakeRefCounted<MahoSidePanelSetLayoutFunction>();
  const std::string error = api_test_utils::RunFunctionAndReturnError(
      function, "[123]", &profile_);
  EXPECT_EQ(error, kLayoutError);
}

TEST_F(MahoExtensionApiTest, SidePanelSetLayoutRejectsUnknownValue) {
  auto function = base::MakeRefCounted<MahoSidePanelSetLayoutFunction>();
  const std::string error = api_test_utils::RunFunctionAndReturnError(
      function, "[\"middle\"]", &profile_);
  EXPECT_EQ(error, kLayoutError);
}

TEST_F(MahoExtensionApiTest, SidePanelSetLayoutAcceptsLeftPastValidation) {
  auto function = base::MakeRefCounted<MahoSidePanelSetLayoutFunction>();
  const std::string error = api_test_utils::RunFunctionAndReturnError(
      function, "[\"left\"]", &profile_);
  // "left" is a valid layout: it must NOT trip the layout validation error.
  // It fails later (no calling extension / coordinator), proving acceptance.
  EXPECT_NE(error, kLayoutError);
  EXPECT_FALSE(error.empty());
}

TEST_F(MahoExtensionApiTest, SidePanelSetLayoutAcceptsRightPastValidation) {
  auto function = base::MakeRefCounted<MahoSidePanelSetLayoutFunction>();
  const std::string error = api_test_utils::RunFunctionAndReturnError(
      function, "[\"right\"]", &profile_);
  EXPECT_NE(error, kLayoutError);
  EXPECT_FALSE(error.empty());
}

}  // namespace
}  // namespace extensions
