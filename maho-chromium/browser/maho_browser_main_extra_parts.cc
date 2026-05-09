#include "maho/browser/maho_browser_main_extra_parts.h"

#include <string>

#include "base/json/json_reader.h"
#include "base/logging.h"
#include "base/values.h"
#include "base/functional/bind.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/net/system_network_context_manager.h"
#include "chrome/browser/profiles/profile.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "maho/browser/ai/maho_opencode_runtime_service.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/browser/ui/theme/maho_color_mixer.h"
#include "maho/browser/ui/theme/maho_space_theme_state.h"
#include "maho/browser/ui/webui/maho_ai_prefs.h"
#include "maho/third_party/maho/maho_bridge.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "ui/color/color_provider_manager.h"

MahoBrowserMainExtraParts::MahoBrowserMainExtraParts() = default;

MahoBrowserMainExtraParts::~MahoBrowserMainExtraParts() {
  if (core_) {
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

    base::FilePath profile_path =
        base::FilePath::FromASCII("Default");

    const std::string* profile_id = dict->FindString("profile_id");
    if (profile_id && !profile_id->empty() && *profile_id != "default") {
      profile_path =
          base::FilePath::FromASCII("MahoProfile_" + *profile_id);
    }

    bridge->RegisterSpace(*id, profile_path);
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
  core_ = maho::core::Create();
  if (core_) {
    maho::SetCore(core_);
    maho::core::InstallLogCallback();
    LOG(INFO) << "Maho core initialized, version: " << maho::core::Version();
    PopulateBridgeFromCore(core_);
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

  auto service = std::make_unique<MahoOpenCodeRuntimeService>();
  MahoOpenCodeRuntimeService::SetInstance(std::move(service));

  std::string custom_endpoint =
      profile->GetPrefs()->GetString(maho::ai_prefs::kRuntimeEndpoint);

  auto* system_network =
      g_browser_process->system_network_context_manager();
  scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory =
      system_network->GetSharedURLLoaderFactory();

  MahoOpenCodeRuntimeService::GetInstance()->Start(
      custom_endpoint, std::move(url_loader_factory));
}

void MahoBrowserMainExtraParts::PostMainMessageLoopRun() {
  MahoOpenCodeRuntimeService::ResetInstance();

  if (core_) {
    MahoSpaceThemeState::Clear();
    maho::SetCore(nullptr);
    maho::core::Destroy(core_);
    core_ = nullptr;
    LOG(INFO) << "Maho core destroyed";
  }
}
