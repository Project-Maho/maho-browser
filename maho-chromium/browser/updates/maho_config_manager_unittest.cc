// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/updates/maho_config_manager.h"

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "chrome/test/base/testing_browser_process.h"
#include "chrome/test/base/testing_profile.h"
#include "components/prefs/testing_pref_service.h"
#include "content/public/test/browser_task_environment.h"
#include "maho/browser/updates/maho_update_pref_names.h"
#include "maho/browser/updates/maho_update_prefs.h"
#include "maho/browser/updates/rollout_bucket.h"
#include "services/network/public/cpp/weak_wrapper_shared_url_loader_factory.h"
#include "services/network/test/test_url_loader_factory.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho {
namespace updates {

class MahoConfigManagerTest : public testing::Test {
 protected:
  MahoConfigManagerTest()
      : task_environment_(content::BrowserTaskEnvironment::IO_MAINLOOP) {}

  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    
    // Register local state prefs
    auto* local_state = TestingBrowserProcess::GetGlobal()->GetTestingLocalState();
    if (!local_state->FindPreference(prefs::kMahoConfigEnabled)) {
      RegisterLocalStatePrefs(local_state->registry());
    }

    // Create profile
    TestingProfile::Builder builder;
    builder.SetPath(temp_dir_.GetPath().AppendASCII("TestingProfile"));
    profile_ = builder.Build();

    // Set shared URL loader factory
    shared_url_loader_factory_ =
        base::MakeRefCounted<network::WeakWrapperSharedURLLoaderFactory>(
            &test_url_loader_factory_);
    TestingBrowserProcess::GetGlobal()->SetSharedURLLoaderFactory(
        shared_url_loader_factory_);
  }

  void TearDown() override {
    TestingBrowserProcess::GetGlobal()->SetSharedURLLoaderFactory(nullptr);
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

}  // namespace updates
}  // namespace maho
