// Copyright 2026 Maho Browser. All rights reserved.

#include <memory>
#include <string>

#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/run_loop.h"
#include "base/test/run_until.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/browser/download/download_prefs.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/download/public/common/download_item.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/download_manager.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/download_test_observer.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/ui/downloads/maho_download_bridge_service_factory.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/page_transition_types.h"
#include "url/gurl.h"

namespace maho {
namespace {

constexpr char kDownloadPath[] = "/maho-dl-test.bin";
constexpr char kDownloadName[] = "maho-dl-test.bin";

std::unique_ptr<net::test_server::HttpResponse> HandleDownloadRequest(
    const net::test_server::HttpRequest& request) {
  if (request.relative_url != kDownloadPath) {
    return nullptr;
  }
  auto response = std::make_unique<net::test_server::BasicHttpResponse>();
  response->set_code(net::HTTP_OK);
  response->set_content_type("application/octet-stream");
  response->AddCustomHeader(
      "Content-Disposition",
      std::string("attachment; filename=") + kDownloadName);
  response->set_content("maho download bridge integration payload");
  return response;
}

class MahoDownloadBridgeBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    embedded_test_server()->RegisterRequestHandler(
        base::BindRepeating(&HandleDownloadRequest));
    ASSERT_TRUE(embedded_test_server()->Start());

    ASSERT_TRUE(downloads_dir_.CreateUniqueTempDir());
    Profile* profile = browser()->GetProfile();
    profile->GetPrefs()->SetBoolean(prefs::kPromptForDownload, false);
    DownloadPrefs::FromBrowserContext(profile)->SetDownloadPath(
        downloads_dir_.GetPath());
  }

  // Returns true if `needle` appears anywhere in the Maho download view-models JSON.
  bool MahoStoreContains(const std::string& needle) {
    MahoCore* core = maho::GetCore();
    if (!core) return false;
    char* json = maho_core_get_download_view_models(core);
    if (!json) return false;
    std::string models(json);
    maho_string_free(json);
    return models.find(needle) != std::string::npos;
  }

  // Counts how many times `needle` appears in the Maho download view-models JSON.
  // Used as a B2-regression guard: a download ingested from OnDownloadCreated and
  // simultaneously seed-loaded on startup would produce 2 occurrences.
  int MahoStoreCountOccurrences(const std::string& needle) {
    MahoCore* core = maho::GetCore();
    if (!core) return 0;
    char* json = maho_core_get_download_view_models(core);
    if (!json) return 0;
    std::string models(json);
    maho_string_free(json);
    int count = 0;
    size_t pos = 0;
    while ((pos = models.find(needle, pos)) != std::string::npos) {
      ++count;
      pos += needle.size();
    }
    return count;
  }

  base::ScopedTempDir downloads_dir_;
};

IN_PROC_BROWSER_TEST_F(MahoDownloadBridgeBrowserTest,
                       RealDownloadIsIngestedIntoMahoStore) {
  ASSERT_FALSE(browser()->GetProfile()->IsOffTheRecord());
  ASSERT_NE(maho::MahoDownloadBridgeServiceFactory::GetForProfile(
                browser()->GetProfile()),
            nullptr);
  ASSERT_NE(maho::GetCore(), nullptr);

  content::DownloadManager* manager =
      browser()->GetProfile()->GetDownloadManager();
  content::DownloadTestObserverTerminal observer(
      manager, /*wait_count=*/1,
      content::DownloadTestObserver::ON_DANGEROUS_DOWNLOAD_FAIL);

  ui_test_utils::NavigateToURLWithDisposition(
      browser(), embedded_test_server()->GetURL(kDownloadPath),
      WindowOpenDisposition::CURRENT_TAB,
      ui_test_utils::BROWSER_TEST_NO_WAIT);

  observer.WaitForFinished();
  EXPECT_EQ(1u, observer.NumDownloadsSeenInState(
                    download::DownloadItem::COMPLETE));

  EXPECT_TRUE(base::test::RunUntil(
      [&]() { return MahoStoreContains(kDownloadName); }));
}

// R-16/B2 regression: a single download must appear exactly once in the Maho
// store. Before the B2 fix, an in-progress item would be seed-loaded from
// SQLite AND re-ingested via OnDownloadCreated, producing a duplicate entry.
IN_PROC_BROWSER_TEST_F(MahoDownloadBridgeBrowserTest,
                       DownloadAppearsExactlyOnceInMahoStore) {
  ASSERT_NE(maho::GetCore(), nullptr);

  content::DownloadManager* manager =
      browser()->GetProfile()->GetDownloadManager();
  content::DownloadTestObserverTerminal observer(
      manager, /*wait_count=*/1,
      content::DownloadTestObserver::ON_DANGEROUS_DOWNLOAD_FAIL);

  ui_test_utils::NavigateToURLWithDisposition(
      browser(), embedded_test_server()->GetURL(kDownloadPath),
      WindowOpenDisposition::CURRENT_TAB,
      ui_test_utils::BROWSER_TEST_NO_WAIT);

  observer.WaitForFinished();

  // Wait until the entry is visible, then confirm it appears exactly once.
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return MahoStoreContains(kDownloadName); }));

  EXPECT_EQ(1, MahoStoreCountOccurrences(kDownloadName));
}

// R-16/OTR privacy: downloads initiated from an incognito window must NOT
// be forwarded to the shared (regular-profile) Maho core store.
// MahoDownloadBridgeServiceFactory builds only for regular profiles, so the
// bridge service is null for OTR and OnDownloadCreated is never invoked.
IN_PROC_BROWSER_TEST_F(MahoDownloadBridgeBrowserTest,
                       IncognitoDownloadDoesNotAppearInMahoStore) {
  ASSERT_NE(maho::GetCore(), nullptr);

  // The bridge service must be absent for incognito (factory is regular-only).
  Browser* incognito = static_cast<Browser*>(CreateIncognitoBrowser());
  ASSERT_TRUE(incognito);
  ASSERT_TRUE(incognito->GetProfile()->IsOffTheRecord());
  EXPECT_EQ(maho::MahoDownloadBridgeServiceFactory::GetForProfileIfExists(
                incognito->GetProfile()),
            nullptr);

  // Configure the incognito profile to auto-accept the download.
  incognito->GetProfile()->GetPrefs()->SetBoolean(prefs::kPromptForDownload,
                                               false);
  DownloadPrefs::FromBrowserContext(incognito->GetProfile())
      ->SetDownloadPath(downloads_dir_.GetPath());

  content::DownloadManager* inc_manager =
      incognito->GetProfile()->GetDownloadManager();
  content::DownloadTestObserverTerminal observer(
      inc_manager, /*wait_count=*/1,
      content::DownloadTestObserver::ON_DANGEROUS_DOWNLOAD_FAIL);

  ui_test_utils::NavigateToURLWithDisposition(
      incognito, embedded_test_server()->GetURL(kDownloadPath),
      WindowOpenDisposition::CURRENT_TAB,
      ui_test_utils::BROWSER_TEST_NO_WAIT);

  observer.WaitForFinished();
  EXPECT_EQ(1u, observer.NumDownloadsSeenInState(
                    download::DownloadItem::COMPLETE));

  // Give any async observer callbacks a chance to fire.
  base::RunLoop().RunUntilIdle();

  // The Maho store (backed by the regular-profile core) must remain empty.
  EXPECT_FALSE(MahoStoreContains(kDownloadName));
}

}  // namespace
}  // namespace maho
