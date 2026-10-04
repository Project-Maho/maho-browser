// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/updates/maho_config_manager.h"

#include <memory>
#include <string>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/memory/raw_ptr.h"
#include "chrome/test/base/testing_browser_process.h"
#include "chrome/test/base/testing_profile.h"
#include "components/prefs/testing_pref_service.h"
#include "components/sync_preferences/testing_pref_service_syncable.h"
#include "content/public/test/browser_task_environment.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/updates/maho_update_pref_names.h"
#include "maho/browser/updates/maho_update_prefs.h"
#include "maho/browser/updates/rollout_bucket.h"
#include "services/network/public/cpp/weak_wrapper_shared_url_loader_factory.h"
#include "services/network/test/test_url_loader_factory.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho {
namespace updates {
namespace {

constexpr char kEmptySkillsBlob[] =
    "{\"version\":7,\"min_brain_version\":1,\"skills\":[]}";
constexpr char kEmptySkillsBlobSha256[] =
    "5a00993062b1e1b357044b11d0d3268793a6353665b6f73cc0d9715b8c222417";

class ScopedCoreOverride {
 public:
  explicit ScopedCoreOverride(MahoCore* core) : saved_(maho::GetCore()) {
    maho::SetCore(core);
  }
  ~ScopedCoreOverride() { maho::SetCore(saved_); }

 private:
  raw_ptr<MahoCore> saved_ = nullptr;
};

}

class MahoConfigManagerTest : public testing::Test {
 protected:
  MahoConfigManagerTest()
      : task_environment_(content::BrowserTaskEnvironment::IO_MAINLOOP) {}

  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    
    auto* local_state = TestingBrowserProcess::GetGlobal()->GetTestingLocalState();
    if (!local_state->FindPreference(prefs::kMahoConfigEnabled)) {
      RegisterLocalStatePrefs(local_state->registry());
    }
    local_state->ClearPref(prefs::kMahoUpdateLastKnownGoodVersion);
    local_state->ClearPref(prefs::kMahoUpdateRollbackRequested);
    local_state->ClearPref(prefs::kMahoUpdateInstallId);
    local_state->ClearPref(prefs::kMahoConfigEnabled);

    TestingProfile::Builder builder;
    builder.SetPath(temp_dir_.GetPath().AppendASCII("TestingProfile"));
    profile_ = builder.Build();
    RegisterProfilePrefs(profile_->GetTestingPrefService()->registry());

    shared_url_loader_factory_ =
        base::MakeRefCounted<network::WeakWrapperSharedURLLoaderFactory>(
            &test_url_loader_factory_);
    TestingBrowserProcess::GetGlobal()->SetSharedURLLoaderFactory(
        shared_url_loader_factory_);

    MahoConfigManager::GetInstance()->ResetForTesting();
  }

  void TearDown() override {
    MahoConfigManager::GetInstance()->ResetForTesting();
    maho::SetCore(nullptr);
    TestingBrowserProcess::GetGlobal()->SetSharedURLLoaderFactory(nullptr);
  }

  base::FilePath WriteProfileConfig(const std::string& file_name,
                                    const std::string& contents) {
    base::FilePath config_dir = profile_->GetPath().AppendASCII("maho_config");
    EXPECT_TRUE(base::CreateDirectory(config_dir));
    base::FilePath path = config_dir.Append(base::FilePath::FromUTF8Unsafe(file_name));
    EXPECT_TRUE(base::CreateDirectory(path.DirName()));
    EXPECT_TRUE(base::WriteFile(path, contents));
    return path;
  }

  base::FilePath WriteTempBlob(const std::string& file_name,
                               const std::string& contents) {
    base::FilePath path = temp_dir_.GetPath().AppendASCII(file_name);
    EXPECT_TRUE(base::WriteFile(path, contents));
    return path;
  }

  content::BrowserTaskEnvironment task_environment_;
  base::ScopedTempDir temp_dir_;
  std::unique_ptr<TestingProfile> profile_;
  network::TestURLLoaderFactory test_url_loader_factory_;
  scoped_refptr<network::SharedURLLoaderFactory> shared_url_loader_factory_;
};

TEST_F(MahoConfigManagerTest, RolloutBucketCompute) {
  std::string id = "test-install-id-123";
  int bucket = RolloutBucket::Compute(id);
  EXPECT_GE(bucket, 0);
  EXPECT_LT(bucket, 100);
}

TEST_F(MahoConfigManagerTest, ConfigManagerInitialization) {
  MahoConfigManager* manager = MahoConfigManager::GetInstance();
  manager->Initialize(profile_.get());
  
  base::FilePath active_path = manager->GetActiveConfigPath();
  EXPECT_TRUE(active_path.empty());
}

TEST_F(MahoConfigManagerTest, InitializeLoadsOfflineLastKnownGood) {
  MahoCore* fake_core = reinterpret_cast<MahoCore*>(0x1);
  ScopedCoreOverride scoped_core(fake_core);

  auto* local_state = TestingBrowserProcess::GetGlobal()->GetTestingLocalState();
  local_state->SetString(prefs::kMahoUpdateLastKnownGoodVersion, "42");
  base::FilePath lkg_path = WriteProfileConfig("config_v42.json", kEmptySkillsBlob);

  int apply_calls = 0;
  base::FilePath applied_path;

  MahoConfigManager* manager = MahoConfigManager::GetInstance();
  manager->SetApplyConfigCallbackForTesting(base::BindRepeating(
      [](int* apply_calls, base::FilePath* applied_path, MahoCore* core,
         const base::FilePath& path) {
        EXPECT_EQ(reinterpret_cast<MahoCore*>(0x1), core);
        ++*apply_calls;
        *applied_path = path;
        return true;
      },
      &apply_calls, &applied_path));
  manager->Initialize(profile_.get());

  EXPECT_EQ(1, apply_calls);
  EXPECT_EQ(lkg_path, applied_path);
  EXPECT_EQ(lkg_path, manager->GetActiveConfigPath());
}

TEST_F(MahoConfigManagerTest, RollbackWithNonNumericLastKnownGoodDoesNotCrash) {
  auto* local_state = TestingBrowserProcess::GetGlobal()->GetTestingLocalState();
  local_state->SetString(prefs::kMahoUpdateLastKnownGoodVersion, "not-a-number");
  local_state->SetBoolean(prefs::kMahoUpdateRollbackRequested, true);

  MahoConfigManager* manager = MahoConfigManager::GetInstance();
  manager->Initialize(profile_.get());

  EXPECT_TRUE(manager->GetActiveConfigPath().empty());
  EXPECT_FALSE(local_state->GetBoolean(prefs::kMahoUpdateRollbackRequested));
}

TEST_F(MahoConfigManagerTest, RollbackAppliesLastKnownGoodConfig) {
  MahoCore* fake_core = reinterpret_cast<MahoCore*>(0x1);
  ScopedCoreOverride scoped_core(fake_core);

  int apply_calls = 0;
  base::FilePath applied_path;

  MahoConfigManager* manager = MahoConfigManager::GetInstance();
  manager->SetApplyConfigCallbackForTesting(base::BindRepeating(
      [](int* apply_calls, base::FilePath* applied_path, MahoCore* core,
         const base::FilePath& path) {
        EXPECT_EQ(reinterpret_cast<MahoCore*>(0x1), core);
        ++*apply_calls;
        *applied_path = path;
        return true;
      },
      &apply_calls, &applied_path));
  manager->Initialize(profile_.get());

  auto* local_state = TestingBrowserProcess::GetGlobal()->GetTestingLocalState();
  local_state->SetString(prefs::kMahoUpdateLastKnownGoodVersion, "42");
  local_state->SetBoolean(prefs::kMahoUpdateRollbackRequested, true);
  base::FilePath lkg_path = WriteProfileConfig("config_v42.json", kEmptySkillsBlob);

  manager->HandleRollbackIfNeeded();

  EXPECT_EQ(1, apply_calls);
  EXPECT_EQ(lkg_path, applied_path);
  EXPECT_EQ(lkg_path, manager->GetActiveConfigPath());
  EXPECT_FALSE(local_state->GetBoolean(prefs::kMahoUpdateRollbackRequested));
}

TEST_F(MahoConfigManagerTest, VerifiedBlobCallsApplyConfigBridge) {
  MahoCore* fake_core = reinterpret_cast<MahoCore*>(0x1);
  ScopedCoreOverride scoped_core(fake_core);

  base::FilePath temp_blob = WriteTempBlob("config_temp.json", kEmptySkillsBlob);
  int apply_calls = 0;
  base::FilePath applied_path;

  MahoConfigManager* manager = MahoConfigManager::GetInstance();
  manager->Initialize(profile_.get());
  manager->SetApplyConfigCallbackForTesting(base::BindRepeating(
      [](int* apply_calls, base::FilePath* applied_path, MahoCore* core,
         const base::FilePath& path) {
        EXPECT_EQ(reinterpret_cast<MahoCore*>(0x1), core);
        ++*apply_calls;
        *applied_path = path;
        return true;
      },
      &apply_calls, &applied_path));

  manager->InstallDownloadedBlobForTesting(temp_blob, kEmptySkillsBlobSha256, 7);

  base::FilePath expected_path =
      profile_->GetPath().AppendASCII("maho_config").AppendASCII("config_v7.json");
  EXPECT_EQ(1, apply_calls);
  EXPECT_EQ(expected_path, applied_path);
  EXPECT_EQ(expected_path, manager->GetActiveConfigPath());
  EXPECT_TRUE(base::PathExists(expected_path));
  EXPECT_EQ("7", TestingBrowserProcess::GetGlobal()->GetTestingLocalState()->GetString(
                     prefs::kMahoUpdateLastKnownGoodVersion));
}

}  // namespace updates
}  // namespace maho
