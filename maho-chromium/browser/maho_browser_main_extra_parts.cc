#include "maho/browser/maho_browser_main_extra_parts.h"

#include <string>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/logging.h"
#include "base/path_service.h"
#include "base/values.h"
#include "base/functional/bind.h"
#include "build/build_config.h"

#if BUILDFLAG(IS_WIN)
#include "maho/browser/mcp/maho_mcp_pipe_server_win.h"
#include "maho/browser/mcp/maho_mcp_session_token_win.h"
#else
#include "maho/browser/mcp/maho_mcp_socket_server.h"
#endif
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
#include "maho/browser/maho_tab_id_session_helper.h"
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

    maho::MahoTabIdSessionHelper::GetForProfile(profile);

    maho::MahoUpdateManager::GetInstance()->Initialize(profile);

    // If user hasn't explicitly set the value, override the default to LAST
    // so SessionService restore kicks in.
    PrefService* prefs = profile->GetPrefs();
    const PrefService::Preference* p = prefs->FindPreference(prefs::kRestoreOnStartup);
    if (p && p->IsDefaultValue()) {
      SessionStartupPref::SetStartupPref(profile,
          SessionStartupPref(SessionStartupPref::LAST));
    }

    // MCP server uses net::UnixDomainServerSocket (POSIX) or Windows overlapped
    // I/O via base::win::ObjectWatcher; both require an IO message pump, so
    // Start() must run on the IO thread. Construct on UI, bounce Start() to
    // IO. Destruction is also posted to IO in PostMainMessageLoopRun so every
    // access to the SEQUENCE_CHECKER-guarded state stays on that sequence.
#if BUILDFLAG(IS_WIN)
    mcp_session_token_ = maho::MahoMcpSessionToken::Create();
    if (mcp_session_token_) {
      base::FilePath token_path =
          maho::MahoMcpSessionToken::GetSessionTokenPath();
      if (!token_path.empty()) {
        mcp_session_token_->WriteEncryptedToDisk(token_path);
      }
    }
    mcp_pipe_server_ =
        std::make_unique<maho::MahoMcpPipeServer>(mcp_session_token_.get());
    content::GetIOThreadTaskRunner({})->PostTask(
        FROM_HERE,
        base::BindOnce(base::IgnoreResult(&maho::MahoMcpPipeServer::Start),
                       base::Unretained(mcp_pipe_server_.get())));
#else
    mcp_socket_server_ =
        std::make_unique<maho::MahoMcpSocketServer>(storage_path_);
    content::GetIOThreadTaskRunner({})->PostTask(
        FROM_HERE,
        base::BindOnce(base::IgnoreResult(&maho::MahoMcpSocketServer::Start),
                       base::Unretained(mcp_socket_server_.get())));
#endif
  }
}

void MahoBrowserMainExtraParts::PostMainMessageLoopRun() {
#if BUILDFLAG(IS_WIN)
  if (mcp_pipe_server_) {
    content::GetIOThreadTaskRunner({})->PostTask(
        FROM_HERE,
        base::BindOnce(
            [](std::unique_ptr<maho::MahoMcpPipeServer> server,
               std::unique_ptr<maho::MahoMcpSessionToken> token) {
              server.reset();
            },
            std::move(mcp_pipe_server_),
            std::move(mcp_session_token_)));
  } else {
    mcp_session_token_.reset();
  }
#else
  if (mcp_socket_server_) {
    content::GetIOThreadTaskRunner({})->DeleteSoon(
        FROM_HERE, std::move(mcp_socket_server_));
  }
#endif

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
