#include "maho/browser/maho_tab_id_helper.h"

#include "base/uuid.h"
#include "chrome/browser/sessions/session_service_factory.h"
#include "chrome/browser/sessions/session_service.h"
#include "chrome/browser/sessions/session_service_test_helper.h"
#include "chrome/test/base/testing_browser_process.h"
#include "chrome/test/base/browser_with_test_window_test.h"
#include "components/sessions/content/session_tab_helper.h"
#include "content/public/browser/web_contents.h"
#include "maho/browser/maho_tab_id_session_helper.h"
#include "ui/base/resource/resource_bundle.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho {
namespace {

class MahoTabIdHelperTest : public BrowserWithTestWindowTest {
 protected:
  void SetUp() override {
    BrowserWithTestWindowTest::SetUp();
    // Clear queue before each test
    while (!MahoTabIdHelper::PopPendingRestoredTabIdForTesting().empty()) {}
  }

  void TearDown() override {
    BrowserWithTestWindowTest::TearDown();
  }
};

TEST_F(MahoTabIdHelperTest, RapidDualQueueing) {
  MahoTabIdHelper::SetPendingRestoredTabId("id-alpha");
  MahoTabIdHelper::SetPendingRestoredTabId("id-beta");

  EXPECT_EQ(MahoTabIdHelper::PopPendingRestoredTabIdForTesting(), "id-alpha");
  EXPECT_EQ(MahoTabIdHelper::PopPendingRestoredTabIdForTesting(), "id-beta");
  EXPECT_EQ(MahoTabIdHelper::PopPendingRestoredTabIdForTesting(), "");
}

TEST_F(MahoTabIdHelperTest, WebContentsConstructorConsumesQueue) {
  MahoTabIdHelper::SetPendingRestoredTabId("id-gamma");
  MahoTabIdHelper::SetPendingRestoredTabId("id-delta");

  AddTab(browser(), GURL("chrome://newtab"));
  content::WebContents* wc1 = browser()->GetTabStripModel()->GetWebContentsAt(0);
  MahoTabIdHelper::CreateForWebContents(wc1);
  EXPECT_EQ(MahoTabIdHelper::FromWebContents(wc1)->stable_tab_id(), "id-gamma");

  AddTab(browser(), GURL("chrome://newtab"));
  // AddTab inserts the new tab at index 0, so retrieve wc2 from index 0
  content::WebContents* wc2 = browser()->GetTabStripModel()->GetWebContentsAt(0);
  MahoTabIdHelper::CreateForWebContents(wc2);
  EXPECT_EQ(MahoTabIdHelper::FromWebContents(wc2)->stable_tab_id(), "id-delta");
}

TEST_F(MahoTabIdHelperTest, EmptyQueueGeneratesNewId) {
  AddTab(browser(), GURL("chrome://newtab"));
  content::WebContents* wc = browser()->GetTabStripModel()->GetWebContentsAt(0);
  MahoTabIdHelper::CreateForWebContents(wc);
  EXPECT_FALSE(MahoTabIdHelper::FromWebContents(wc)->stable_tab_id().empty());
  EXPECT_TRUE(base::Uuid::ParseLowercase(
      MahoTabIdHelper::FromWebContents(wc)->stable_tab_id()).is_valid());
}

TEST_F(MahoTabIdHelperTest, SessionHelperSavesTabId) {
  // Ensure SessionService is active for testing by forcing instantiation BEFORE adding any tabs
  auto service_ptr = std::make_unique<SessionService>(profile());
  SessionService* service = service_ptr.get();
  SessionServiceFactory::SetForTestProfile(profile(), std::move(service_ptr));

  // Manually notify SessionService that the browser is opened since the browser was created before the SessionService
  service->WindowOpened(browser());

  // Instantiate the helper
  auto session_helper = std::make_unique<MahoTabIdSessionHelper>(profile());

  // Add a tab to default browser and create its helper
  AddTab(browser(), GURL("chrome://newtab"));
  content::WebContents* wc = browser()->GetTabStripModel()->GetWebContentsAt(0);
  MahoTabIdHelper::CreateForWebContents(wc);
  auto* tab_id_helper = MahoTabIdHelper::FromWebContents(wc);
  std::string expected_id = tab_id_helper->stable_tab_id();

  // Flush commands to disk using helper
  ASSERT_TRUE(service);
  SessionServiceTestHelper test_helper(service);
  test_helper.SaveNow();

  // Destroy the writing SessionService to release the file lock and flush all commands
  SessionServiceFactory::SetForTestProfile(profile(), nullptr);
  task_environment()->RunUntilIdle();

  // Create a new SessionService to read the saved session file
  auto read_service = std::make_unique<SessionService>(profile());
  SessionServiceTestHelper read_helper(read_service.get());
  std::vector<std::unique_ptr<sessions::SessionWindow>> windows;
  read_helper.ReadWindows(&windows, nullptr, nullptr, nullptr);

  ASSERT_EQ(1U, windows.size());
  ASSERT_EQ(1U, windows[0]->tabs.size());
  
  sessions::SessionTab* tab = windows[0]->tabs[0].get();
  EXPECT_EQ(tab->extra_data[MahoTabIdHelper::kExtraDataKey], expected_id);

  // Clean up
  SessionServiceFactory::SetForTestProfile(profile(), nullptr);
}

}  // namespace
}  // namespace maho
