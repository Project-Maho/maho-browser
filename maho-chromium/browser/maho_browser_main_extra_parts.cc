#include "maho/browser/maho_browser_main_extra_parts.h"

#include <string>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/logging.h"
#include "base/path_service.h"
#include "base/values.h"
#include "base/functional/bind.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/chrome_paths.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/browser/maho_tab_registry.h"
#include "maho/browser/ui/theme/maho_color_mixer.h"
#include "maho/browser/ui/theme/maho_space_theme_state.h"
#include "maho/third_party/maho/maho_bridge.h"
#include "maho/browser/ui/theme/maho_color_mixer.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "ui/color/color_provider_manager.h"

MahoBrowserMainExtraParts::MahoBrowserMainExtraParts() = default;

MahoBrowserMainExtraParts::~MahoBrowserMainExtraParts() {
  if (core_) {
    maho::core::SaveState(core_);
    maho::SetCore(nullptr);
    maho::core::Destroy(core_);
    core_ = nullptr;
  }
}

namespace {

std::string DecodeActiveSpaceId(char* active_id_cstr) {
  if (!active_id_cstr) {
    return std::string();
  }

  std::string active_id = active_id_cstr;
  std::optional<base::Value> parsed =
      base::JSONReader::Read(active_id, base::JSON_PARSE_RFC);
  maho_string_free(active_id_cstr);

  if (parsed && parsed->is_string()) {
    return parsed->GetString();
  }

  return active_id;
}

void PopulateBridgeFromCore(MahoCore* core) {
  auto* bridge = maho::MahoSpaceProfileBridge::GetInstance();

  char* json_str = maho_core_get_space_view_models(core);
  if (!json_str) {
    return;
  }
  std::string json(json_str);
  maho_string_free(json_str);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    return;
  }

  for (const auto& item : parsed->GetList()) {
    const auto* dict = item.GetIfDict();
    if (!dict) {
      continue;
    }
    const std::string* id = dict->FindString("id");
    if (!id) {
      continue;
    }
    const std::string* profile_id = dict->FindString("profile_id");
    std::string pid_str = profile_id ? *profile_id : std::string();
    std::optional<base::FilePath> resolved_path =
        maho::MahoSpaceProfileBridge::ProfileBasenameForId(pid_str);
    if (!resolved_path) {
      LOG(WARNING) << "Maho: Skipping hydration for space id: " << *id
                   << " due to invalid profile_id: " << pid_str;
      continue;
    }

    bridge->RegisterSpace(*id, *resolved_path);
  }

  bridge->SetActiveSpaceId(
      DecodeActiveSpaceId(maho_core_get_active_space_id(core)));

  if (content::BrowserThread::CurrentlyOn(content::BrowserThread::UI)) {
    MahoSpaceThemeState::UpdateFromCore();
  } else {
    content::GetUIThreadTaskRunner({})->PostTask(
        FROM_HERE, base::BindOnce(&MahoSpaceThemeState::UpdateFromCore));
  }

  LOG(INFO) << "Maho bridge populated: "
            << bridge->GetRegisteredSpaceCount() << " spaces";
}

}  // namespace

void MahoBrowserMainExtraParts::PostEarlyInitialization() {
  base::FilePath user_data_dir;
  if (base::PathService::Get(chrome::DIR_USER_DATA, &user_data_dir)) {
    storage_path_ = user_data_dir.AppendASCII("MahoCore");
    base::CreateDirectory(storage_path_);
  }

  if (!storage_path_.empty()) {
    core_ = maho::core::CreateWithStorage(storage_path_.AsUTF8Unsafe());
  }
  if (!core_) {
    core_ = maho::core::Create();
  }

  if (core_) {
    maho::SetCore(core_);
    maho::core::InstallLogCallback();
    maho::core::LoadState(core_);
    LOG(INFO) << "Maho core initialized, version: " << maho::core::Version();
    PopulateBridgeFromCore(core_);
    maho::MahoTabRegistry::Get();
  } else {
    LOG(ERROR) << "Maho core initialization failed";
  }

  ui::ColorProviderManager::Get().AppendColorProviderInitializer(
      base::BindRepeating(&AddMahoColorMixers));
}

void MahoBrowserMainExtraParts::PostProfileInit(Profile* profile,
                                                 bool is_initial_profile) {
  if (!is_initial_profile) {
    return;
  }
}

void MahoBrowserMainExtraParts::PostMainMessageLoopRun() {
  if (core_) {
    MahoSpaceThemeState::Clear();
    maho::core::SaveState(core_);
    maho::SetCore(nullptr);
    maho::core::Destroy(core_);
    core_ = nullptr;
    LOG(INFO) << "Maho core destroyed";
  }
}
