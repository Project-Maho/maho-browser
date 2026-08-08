// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/downloads/maho_download_bridge_service.h"

#include <memory>
#include <optional>
#include <string>

#include "base/files/file_path.h"
#include "base/json/json_reader.h"
#include "base/memory/raw_ptr.h"
#include "base/values.h"
#include "chrome/test/base/testing_profile.h"
#include "components/download/public/common/download_item.h"
#include "components/download/public/common/mock_download_item.h"
#include "content/public/test/browser_task_environment.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace maho {
namespace {

using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnRefOfCopy;

// Swaps the process-global MahoCore for a test-owned instance for the test's
// lifetime, mirroring maho_extension_state_bridge_unittest.cc.
class ScopedCoreOverride {
 public:
  explicit ScopedCoreOverride(MahoCore* core) : saved_(GetCore()) { SetCore(core); }
  ~ScopedCoreOverride() { SetCore(saved_); }

 private:
  raw_ptr<MahoCore> saved_;
};

// Configures the MockDownloadItem getters that MahoDownloadBridgeService reads.
// ReturnRefOfCopy stores a stable internal copy, so there are no dangling refs.
void ConfigureItem(NiceMock<download::MockDownloadItem>& item,
                   const std::string& guid,
                   const std::string& url,
                   const std::string& target_path,
                   const std::string& report_name,
                   download::DownloadItem::DownloadState state,
                   int64_t total_bytes,
                   int64_t received_bytes) {
  ON_CALL(item, GetGuid()).WillByDefault(ReturnRefOfCopy(guid));
  ON_CALL(item, GetURL()).WillByDefault(ReturnRefOfCopy(GURL(url)));
  ON_CALL(item, GetTargetFilePath())
      .WillByDefault(ReturnRefOfCopy(base::FilePath::FromUTF8Unsafe(target_path)));
  ON_CALL(item, GetFileNameToReportUser())
      .WillByDefault(Return(base::FilePath::FromUTF8Unsafe(report_name)));
  ON_CALL(item, GetState()).WillByDefault(Return(state));
  ON_CALL(item, GetTotalBytes()).WillByDefault(Return(total_bytes));
  ON_CALL(item, GetReceivedBytes()).WillByDefault(Return(received_bytes));
  ON_CALL(item, GetMimeType()).WillByDefault(Return(std::string()));
  ON_CALL(item, CanResume()).WillByDefault(Return(false));
}

std::optional<base::DictValue> FindViewModelByFilename(MahoCore* core,
                                                       const std::string& filename) {
  char* raw = maho_core_get_download_view_models(core);
  if (!raw) return std::nullopt;
  std::string json(raw);
  maho_string_free(raw);
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) return std::nullopt;
  for (const auto& v : parsed->GetList()) {
    const auto* d = v.GetIfDict();
    if (!d) continue;
    const std::string* fn = d->FindString("filename");
    if (fn && *fn == filename) return d->Clone();
  }
  return std::nullopt;
}

bool FindByGuid(MahoCore* core, const std::string& guid) {
  char* raw = maho_core_find_download_by_chromium_guid(core, guid.c_str());
  if (!raw) return false;
  maho_string_free(raw);
  return true;
}

class MahoDownloadBridgeServiceTest : public testing::Test {
 protected:
  content::BrowserTaskEnvironment task_environment_;
  TestingProfile profile_;
};

// D2: a download already COMPLETE when first seen (history restore / fast
// completion) must be ingested with full metadata + persisted guid, not dropped.
TEST_F(MahoDownloadBridgeServiceTest, IngestCompleteDownloadPopulatesMetadataAndGuid) {
  MahoCore* core = maho_core_new();
  ScopedCoreOverride override_core(core);
  auto service = std::make_unique<MahoDownloadBridgeService>(&profile_);

  NiceMock<download::MockDownloadItem> item;
  ConfigureItem(item, "guid-complete-1", "https://example.com/a.avif",
                "/tmp/a.avif", "a.avif", download::DownloadItem::COMPLETE, 5000,
                5000);

  service->OnDownloadCreated(profile_.GetDownloadManager(), &item);

  auto vm = FindViewModelByFilename(core, "a.avif");
  ASSERT_TRUE(vm.has_value()) << "completed download must be ingested (D2)";
  const std::string* state = vm->FindString("state");
  ASSERT_TRUE(state);
  EXPECT_EQ(*state, "completed");
  EXPECT_EQ(vm->FindDouble("totalBytes").value_or(0.0), 5000.0);
  EXPECT_TRUE(FindByGuid(core, "guid-complete-1"))
      << "chromium_guid must be persisted for later re-mapping (D0)";

  maho_core_free(core);
}

// D3: an unmapped COMPLETE item arriving via OnDownloadUpdated must be delegated
// to the ingest path rather than dropped.
TEST_F(MahoDownloadBridgeServiceTest, UpdatedCompleteUnmappedItemIsIngested) {
  MahoCore* core = maho_core_new();
  ScopedCoreOverride override_core(core);
  auto service = std::make_unique<MahoDownloadBridgeService>(&profile_);

  NiceMock<download::MockDownloadItem> item;
  ConfigureItem(item, "guid-complete-2", "https://example.com/b.zip",
                "/tmp/b.zip", "b.zip", download::DownloadItem::COMPLETE, 1234,
                1234);

  service->OnDownloadUpdated(profile_.GetDownloadManager(), &item);

  EXPECT_TRUE(FindViewModelByFilename(core, "b.zip").has_value())
      << "unmapped COMPLETE item must be ingested via the fallback (D3)";

  maho_core_free(core);
}

// D1: a download created while IN_PROGRESS (target path unknown) is ingested as a
// placeholder, then its metadata must be finalized once the target path resolves.
TEST_F(MahoDownloadBridgeServiceTest, InProgressTargetResolutionFinalizesMetadata) {
  MahoCore* core = maho_core_new();
  ScopedCoreOverride override_core(core);
  auto service = std::make_unique<MahoDownloadBridgeService>(&profile_);
  content::DownloadManager* manager = profile_.GetDownloadManager();

  NiceMock<download::MockDownloadItem> item;
  // No target path / report name yet.
  ConfigureItem(item, "guid-inprogress-1", "https://example.com/c.bin", "", "",
                download::DownloadItem::IN_PROGRESS, 0, 0);

  service->OnDownloadCreated(manager, &item);
  EXPECT_FALSE(FindViewModelByFilename(core, "c.bin").has_value())
      << "no usable filename should exist before target resolution";

  // Target path resolves; the next update must finalize metadata (D1).
  ConfigureItem(item, "guid-inprogress-1", "https://example.com/c.bin",
                "/tmp/c.bin", "c.bin", download::DownloadItem::IN_PROGRESS, 100,
                10);
  service->OnDownloadUpdated(manager, &item);

  EXPECT_TRUE(FindViewModelByFilename(core, "c.bin").has_value())
      << "metadata must be finalized once the target path is known (D1)";

  maho_core_free(core);
}

}  // namespace
}  // namespace maho
