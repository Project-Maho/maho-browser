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
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/sessions/exit_type_service.h"
#include "chrome/browser/prefs/session_startup_pref.h"
#include "chrome/common/pref_names.h"
#include "components/prefs/pref_service.h"
#include "chrome/common/chrome_paths.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "maho/browser/extensions/maho_extension_state_bridge.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/maho_routines_scheduler.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/browser/maho_tab_registry.h"
#include "maho/browser/updates/maho_update_manager.h"
#include "chrome/browser/sessions/session_restore.h"
#include "maho/browser/maho_tab_id_helper.h"
#include "maho/browser/ui/theme/maho_color_mixer.h"
#include "maho/browser/ui/theme/maho_space_theme_state.h"
#include "maho/third_party/maho/maho_bridge.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "ui/color/color_provider_manager.h"

MahoBrowserMainExtraParts::MahoBrowserMainExtraParts() = default;

MahoBrowserMainExtraParts::~MahoBrowserMainExtraParts() {
  if (core_) {
    routines_scheduler_.reset();
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

void OnSessionRestoreFinished(Profile* profile, int num_tabs_restored) {
  LOG(INFO) << "Session restore finished. Restored " << num_tabs_restored << " tabs.";
  
  MahoCore* core = maho::GetCore();
  if (!core) {
    return;
  }
  
  char* json_str = maho_core_get_tab_view_models(core);
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

  std::set<std::string> core_tab_ids;
  for (const auto& item : parsed->GetList()) {
    const auto* dict = item.GetIfDict();
    if (dict) {
      const std::string* id = dict->FindString("id");
      if (id) {
        core_tab_ids.insert(*id);
      }
    }
  }

  std::set<std::string> live_tab_ids =
      maho::MahoTabRegistry::GetLiveTabIdsForProfile(profile);
  int live_tab_count = live_tab_ids.size();

  int restored_count = 0;
  int suspended_count = 0;
  for (const auto& tab_id : core_tab_ids) {
    if (live_tab_ids.count(tab_id)) {
      restored_count++;
    } else {
      suspended_count++;
    }
  }

  LOG(INFO) << "Reconciliation: " << core_tab_ids.size()
            << " LMDB tabs, " << restored_count << " restored, "
            << suspended_count << " suspended (live count: " << live_tab_count << ")";
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

    routines_scheduler_ = std::make_unique<maho::MahoRoutinesScheduler>(core_);
    routines_scheduler_->Start();
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
  if (profile && !profile->IsOffTheRecord()) {
    extension_state_bridge_ =
        std::make_unique<maho::MahoExtensionStateBridge>(profile);

    maho::MahoUpdateManager::GetInstance()->Initialize(profile);

    // If user hasn't explicitly set the value, override the default to LAST
    // so SessionService restore kicks in.
    PrefService* prefs = profile->GetPrefs();
    const PrefService::Preference* p = prefs->FindPreference(prefs::kRestoreOnStartup);
    if (p && p->IsDefaultValue()) {
      SessionStartupPref::SetStartupPref(profile,
          SessionStartupPref(SessionStartupPref::LAST));
    }

    session_restored_subscription_ =
        SessionRestore::RegisterOnSessionRestoredCallback(
            base::BindRepeating(&OnSessionRestoreFinished));
  }
}

void MahoBrowserMainExtraParts::PostMainMessageLoopRun() {
  ProfileManager* manager = g_browser_process->profile_manager();
  if (manager) {
    for (Profile* profile : manager->GetLoadedProfiles()) {
      if (profile && profile->IsRegularProfile()) {
        // ExitTypeService::SetCurrentSessionExitType stashes (does not write
        // to prefs) when waiting_for_user_to_ack_crash_ is true — so a
        // perpetually-Crashed profile never auto-recovers. Write the prefs
        // value directly to break the loop, then mirror via the service so
        // any in-process state stays consistent. CommitPendingWrite forces
        // an immediate disk flush before the message loop tears down.
        profile->GetPrefs()->SetString(prefs::kSessionExitType, "Normal");
        profile->GetPrefs()->CommitPendingWrite();
        if (auto* svc = ExitTypeService::GetInstanceForProfile(profile)) {
          svc->SetCurrentSessionExitType(ExitType::kClean);
        }
      }
    }
  }

  if (core_) {
    MahoSpaceThemeState::Clear();
    routines_scheduler_.reset();
    maho::core::SaveState(core_);
    maho::SetCore(nullptr);
    maho::core::Destroy(core_);
    core_ = nullptr;
    LOG(INFO) << "Maho core destroyed";
  }
}
