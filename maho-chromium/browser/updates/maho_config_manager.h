// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UPDATES_MAHO_CONFIG_MANAGER_H_
#define MAHO_BROWSER_UPDATES_MAHO_CONFIG_MANAGER_H_

#include <memory>
#include <optional>
#include <string>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/singleton.h"
#include "base/sequence_checker.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "services/network/public/cpp/simple_url_loader.h"

class Profile;
struct MahoCore;

namespace maho {
namespace updates {

class MahoConfigManager {
 public:
  static MahoConfigManager* GetInstance();

  void Initialize(::Profile* profile);
  void CheckForConfigUpdate();

  // Rollback to LKG configuration if requested.
  void HandleRollbackIfNeeded();

  // Returns the path to the currently active configuration file.
  base::FilePath GetActiveConfigPath() const;

  // Signal reload of configuration.
  void SignalReload(const base::FilePath& path, int version);

  using ApplyConfigCallbackForTesting =
      base::RepeatingCallback<bool(::MahoCore*, const base::FilePath&)>;
  void SetApplyConfigCallbackForTesting(ApplyConfigCallbackForTesting callback);
  void InstallDownloadedBlobForTesting(const base::FilePath& temp_path,
                                       const std::string& sha256,
                                       int version);
  void ResetForTesting();

 private:
  MahoConfigManager();
  ~MahoConfigManager();
  friend struct base::DefaultSingletonTraits<MahoConfigManager>;

  void LoadLastKnownGoodConfig();
  void OnManifestFetched(std::optional<std::string> body);
  void OnBlobDownloaded(base::FilePath temp_path);
  void InstallDownloadedBlob(base::FilePath temp_path);
  bool ApplyConfigToCore(const base::FilePath& path);

  SEQUENCE_CHECKER(sequence_checker_);
  bool initialized_ = false;
  raw_ptr<::Profile> profile_ = nullptr;
  base::FilePath active_config_path_;
  int active_version_ = 0;

  std::unique_ptr<network::SimpleURLLoader> manifest_loader_;
  std::unique_ptr<network::SimpleURLLoader> blob_loader_;
  base::RepeatingTimer update_timer_;

  std::string downloading_url_;
  std::string downloading_sha256_;
  int downloading_version_ = 0;
  ApplyConfigCallbackForTesting apply_config_callback_for_testing_;
};

}  // namespace updates
}  // namespace maho

#endif  // MAHO_BROWSER_UPDATES_MAHO_CONFIG_MANAGER_H_
