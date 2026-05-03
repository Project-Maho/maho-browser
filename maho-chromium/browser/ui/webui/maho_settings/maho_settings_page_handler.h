// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_SETTINGS_MAHO_SETTINGS_PAGE_HANDLER_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_SETTINGS_MAHO_SETTINGS_PAGE_HANDLER_H_

#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "components/prefs/pref_change_registrar.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/ui/webui/maho_settings/maho_settings.mojom.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"

class PrefService;
class Profile;

class MahoSettingsPageHandler : public maho_settings::mojom::PageHandler {
 public:
  MahoSettingsPageHandler(
      mojo::PendingReceiver<maho_settings::mojom::PageHandler> receiver,
      mojo::PendingRemote<maho_settings::mojom::Page> page,
      Profile* profile);
  MahoSettingsPageHandler(const MahoSettingsPageHandler&) = delete;
  MahoSettingsPageHandler& operator=(const MahoSettingsPageHandler&) = delete;
  ~MahoSettingsPageHandler() override;

  void GetSettings(GetSettingsCallback callback) override;
  void SetSetting(const std::string& key,
                  const std::string& value,
                  SetSettingCallback callback) override;

 private:
  // Returns the pref path for a Maho setting key, or nullptr if it is not
  // pref-backed.
  static const char* GetPrefPath(const std::string& maho_key);

  // Returns the current value for a setting key as a string.
  std::string ReadSetting(const std::string& maho_key);

  // Called when a watched Chrome pref changes.
  void OnPrefChanged(const std::string& pref_name);

  // Pushes all current settings to the WebUI page.
  void NotifySettingsChanged();

  // Returns true if the key is FFI-backed and was handled.
  bool HandleFfiSetting(const std::string& key, const std::string& value);

  // Appends FFI-backed appearance settings to the settings vector.
  void AppendFfiSettings(
      std::vector<maho_settings::mojom::SettingValuePtr>& settings);

  mojo::Receiver<maho_settings::mojom::PageHandler> receiver_;
  mojo::Remote<maho_settings::mojom::Page> page_;
  raw_ptr<Profile> profile_;
  raw_ptr<PrefService> prefs_;
  PrefChangeRegistrar pref_registrar_;
  raw_ptr<PrefService> local_state_;
  PrefChangeRegistrar local_state_pref_registrar_;

  // Must be last member.
  base::WeakPtrFactory<MahoSettingsPageHandler> weak_factory_{this};
};

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_SETTINGS_MAHO_SETTINGS_PAGE_HANDLER_H_
