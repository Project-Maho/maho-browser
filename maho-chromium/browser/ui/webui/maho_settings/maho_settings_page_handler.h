// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_SETTINGS_MAHO_SETTINGS_PAGE_HANDLER_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_SETTINGS_MAHO_SETTINGS_PAGE_HANDLER_H_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "base/callback_list.h"
#include "base/files/file_path.h"
#include "base/functional/function_ref.h"
#include "base/json/json_reader.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/sequence_checker.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "components/os_crypt/async/common/encryptor.h"
#include "components/prefs/pref_change_registrar.h"
#include "content/public/browser/web_contents_observer.h"
#include "maho/browser/ui/webui/maho_ai_provider_oauth.h"
#include "maho/browser/extensions/maho_extension_state_bridge.h"
#include "chrome/browser/profiles/profile_manager_observer.h"
#include "chrome/browser/profiles/profile_observer.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/maho_space_profile_hydration.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/browser/mail_helper/maho_mail_service.h"
#include "maho/browser/ui/webui/maho_settings/maho_settings.mojom.h"
#include "maho/browser/updates/maho_update_manager.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "ui/shell_dialogs/select_file_dialog.h"

class PrefService;
class Profile;
class MahoVaultRevealDialog;

extern "C" {
struct MahoPasswordImportJob;
}

namespace maho::ai {
class MahoModelListFetcher;
class MahoManagedConnectionTest;
}  // namespace maho::ai

namespace content {
class WebContents;
}

namespace device_reauth {
class DeviceAuthenticator;
}

namespace views {
class Widget;
}

namespace maho {
struct DetectedBrowser;
}

namespace maho_settings::testing {

enum class ProfileObservablesDecisionKind { kSnapshot, kRetry, kNull };

struct ProfileObservablesDecision {
  ProfileObservablesDecision(
      ProfileObservablesDecisionKind kind,
      maho_settings::mojom::ProfileObservablesSnapshotPtr snapshot);
  ProfileObservablesDecision(ProfileObservablesDecision&&);
  ProfileObservablesDecision& operator=(ProfileObservablesDecision&&);
  ~ProfileObservablesDecision();

  ProfileObservablesDecisionKind kind;
  maho_settings::mojom::ProfileObservablesSnapshotPtr snapshot;
};

ProfileObservablesDecision DecideProfileObservablesSnapshotForTesting(
    const maho::ProfileCatalogResult& captured_catalog,
    uint64_t current_registry_revision,
    const base::FilePath& handler_profile_basename,
    const std::optional<std::string>& active_profile_id_json,
    uint32_t attempt);

}  // namespace maho_settings::testing

class MahoSettingsPageHandler : public maho_settings::mojom::PageHandler,
                                 public content::WebContentsObserver,
                                 public maho::MahoMailService::Observer,
                                 public maho::MahoUpdateObserver,
                                 public maho::MahoSpaceProfileBridge::Observer,
                                 public ProfileObserver,
                                 public ProfileManagerObserver,
                                 public ui::SelectFileDialog::Listener {
 public:
  MahoSettingsPageHandler(
      mojo::PendingReceiver<maho_settings::mojom::PageHandler> receiver,
      mojo::PendingRemote<maho_settings::mojom::Page> page,
      Profile* profile,
      content::WebContents* host_web_contents = nullptr);
  MahoSettingsPageHandler(const MahoSettingsPageHandler&) = delete;
  MahoSettingsPageHandler& operator=(const MahoSettingsPageHandler&) = delete;
  ~MahoSettingsPageHandler() override;

  // maho::MahoMailService::Observer:
  void OnAccountsChanged() override;

  // maho::MahoUpdateObserver:
  void OnUpdateStateChanged(maho::UpdateState state) override;
  void OnUpdateProgress(double percent) override;

  // maho::MahoSpaceProfileBridge::Observer:
  void OnSpaceProfileBridgeChanged() override;

  // ProfileObserver:
  void OnProfileWillBeDestroyed(Profile* profile) override;

  // ProfileManagerObserver:
  void OnProfileMarkedForPermanentDeletion(Profile* profile) override;
  void OnProfileManagerDestroying() override;

  // ui::SelectFileDialog::Listener:
  void FileSelected(const ui::SelectedFileInfo& file, int index) override;
  void FileSelectionCanceled() override;

  static MahoSettingsPageHandler* GetActiveInstance();
  static bool DisconnectAIProviderForTesting(
      PrefService* prefs,
      const std::string& provider_id,
      const std::optional<std::string>& replacement_default_provider_id,
      const std::optional<std::string>& replacement_default_model_id,
      std::vector<maho_settings::mojom::AITaskReplacementPtr> task_replacements,
      std::string* error_out = nullptr);
  static bool DisconnectAIProviderInternal(
      PrefService* prefs,
      const std::string& provider_id,
      const std::optional<std::string>& replacement_default_provider_id,
      const std::optional<std::string>& replacement_default_model_id,
      std::vector<maho_settings::mojom::AITaskReplacementPtr> task_replacements,
      std::vector<std::string>& affected_tasks,
      std::string& error_out);
  const os_crypt_async::Encryptor* encryptor() const {
    return encryptor_.get();
  }
  void NotifyShortcutRecorded(maho_settings::mojom::MojoKeyComboPtr key_combo);

  struct SelectedProfileRequestState {
    SelectedProfileRequestState();
    SelectedProfileRequestState(SelectedProfileRequestState&&);
    SelectedProfileRequestState& operator=(SelectedProfileRequestState&&);
    ~SelectedProfileRequestState();

    std::string selected_id;
    std::string target_token;
    std::string attached_id;
    std::string attached_target_token;
    uint64_t context_revision = 0;
    uint64_t profile_revision = 0;
    uint64_t load_generation = 0;
    bool attached = false;
  };

  static uint64_t BeginSelectedProfileRequestForTesting(
      SelectedProfileRequestState* state,
      const std::string& canonical_id,
      const std::string& target_token) {
    CHECK(state);
    state->selected_id = canonical_id;
    state->target_token = target_token;
    if (!state->attached) {
      state->attached_id.clear();
      state->attached_target_token.clear();
    }
    return ++state->load_generation;
  }
  static bool CompleteSelectedProfileRequestForTesting(
      SelectedProfileRequestState* state,
      uint64_t load_generation,
      const std::string& canonical_id,
      const std::string& target_token,
      uint64_t profile_revision) {
    CHECK(state);
    if (state->load_generation != load_generation ||
        state->selected_id != canonical_id ||
        state->target_token != target_token) {
      return false;
    }
    if (state->attached && state->attached_id == canonical_id &&
        state->attached_target_token == target_token) {
      state->profile_revision = profile_revision;
      return true;
    }
    state->attached = true;
    state->attached_id = canonical_id;
    state->attached_target_token = target_token;
    state->profile_revision = profile_revision;
    ++state->context_revision;
    return true;
  }
  static bool InvalidateSelectedProfileRequestForTesting(
      SelectedProfileRequestState* state,
      const std::string& canonical_id,
      const std::string& target_token) {
    CHECK(state);
    if (!state->attached || state->selected_id != canonical_id ||
        state->target_token != target_token) {
      return false;
    }
    state->selected_id.clear();
    state->target_token.clear();
    state->attached_id.clear();
    state->attached_target_token.clear();
    state->profile_revision = 0;
    state->attached = false;
    ++state->load_generation;
    ++state->context_revision;
    return true;
  }
  static maho_settings::mojom::SelectedProfileContextPtr
  BuildSelectedProfileContextForTesting(
      const SelectedProfileRequestState& state,
      bool is_host_profile,
      bool is_active_maho_profile,
      maho_settings::mojom::ProfileLifecycleState lifecycle_state) {
    auto context = maho_settings::mojom::SelectedProfileContext::New();
    context->profile_id = state.selected_id;
    context->target_token = state.target_token;
    context->context_revision = state.context_revision;
    context->profile_revision = state.profile_revision;
    context->lifecycle_state = lifecycle_state;
    context->is_host_profile = is_host_profile;
    context->is_active_maho_profile = is_active_maho_profile;
    return context;
  }
  static maho_settings::mojom::ProfileTargetErrorCode
  ValidateSelectedProfileIdentityForTesting(
      const SelectedProfileRequestState& state,
      const std::string& profile_id,
      const std::string& target_token,
      uint64_t expected_context_revision,
      uint64_t expected_profile_revision) {
    if (profile_id.empty()) {
      return maho_settings::mojom::ProfileTargetErrorCode::kInvalidProfileId;
    }
    if (!state.attached || profile_id != state.attached_id ||
        target_token != state.attached_target_token ||
        profile_id != state.selected_id || target_token != state.target_token) {
      return maho_settings::mojom::ProfileTargetErrorCode::kStaleContext;
    }
    if (expected_context_revision != state.context_revision) {
      return maho_settings::mojom::ProfileTargetErrorCode::kStaleContext;
    }
    if (expected_profile_revision != state.profile_revision) {
      return maho_settings::mojom::ProfileTargetErrorCode::kStaleProfileRevision;
    }
    return maho_settings::mojom::ProfileTargetErrorCode::kNone;
  }
  static bool ApplyProfileSettingsMutationForTesting(
      maho::ProfileRegistryRecord* record,
      uint64_t* catalog_revision,
      const std::string* name,
      const std::string* avatar_color,
      const std::optional<int32_t>* archive_timeout_hours) {
    if (!record || !catalog_revision) {
      return false;
    }
    if (name) {
      record->name = *name;
    }
    if (avatar_color) {
      record->avatar_color = *avatar_color;
    }
    if (archive_timeout_hours) {
      record->archive_timeout_hours = *archive_timeout_hours;
    }
    record->revision = ++*catalog_revision;
    return true;
  }
  static maho_settings::mojom::ProfileTargetErrorCode
  MapProfileUpdateErrorForTesting(const std::string& code) {
    if (code == "PROFILE_NAME_EMPTY" || code == "PROFILE_NAME_DUPLICATE" ||
        code == "PROFILE_AVATAR_COLOR_INVALID" ||
        code == "PROFILE_ARCHIVE_TIMEOUT_INVALID") {
      return maho_settings::mojom::ProfileTargetErrorCode::kInvalidArgument;
    }
    if (code == "PROFILE_NOT_FOUND") {
      return maho_settings::mojom::ProfileTargetErrorCode::kProfileNotFound;
    }
    return maho_settings::mojom::ProfileTargetErrorCode::kInternal;
  }
  // C5: `atc.open_external_links_in_maho_mini` lives in maho-core, which is the
  // only store ATCManager::decide_link_destination reads. The legacy Chromium
  // profile pref (sidebar_prefs::kOpenExternalLinksInMahoMini) is seeded into
  // core exactly once so existing users keep their choice, then never consulted
  // again — otherwise a later seed would clobber whatever the user set through
  // the settings surface. The core FFI getter returns a plain bool and cannot
  // distinguish "unset" from "false", so the marker is the sole record that the
  // seed already happened.
  //
  // This IS the production implementation of that sequence — read marker,
  // decide, seed core, persist marker. MigrateAtcExternalMiniSettingToCore()
  // is only a binding of it to local_state_/prefs_, and the unit tests drive
  // this exact function rather than restating its logic.
  //
  // The marker accessors MUST be backed by process-global storage (local
  // state), because `core` is a single process-global store shared by every
  // profile. `read_legacy_profile_pref` is the per-profile legacy value being
  // migrated. Returns true iff this call seeded core.
  static bool RunAtcExternalMiniMigration(
      MahoCore* core,
      base::FunctionRef<bool()> read_migrated_marker,
      base::FunctionRef<bool()> read_legacy_profile_pref,
      base::FunctionRef<void()> write_migrated_marker) {
    if (!core) {
      // Core is not up yet. Leave the marker unset so the seed is retried
      // later; recording a migration that never reached core would strand the
      // user's legacy value forever.
      return false;
    }
    if (read_migrated_marker()) {
      return false;
    }
    maho_core_set_open_external_links_in_maho_mini(
        core, read_legacy_profile_pref());
    write_migrated_marker();
    return true;
  }

  // Completes a successful Settings Vault lifecycle mutation by replacing its
  // status with a fresh canonical read and synchronously publishing the
  // resulting process-wide lock state. A missing/malformed canonical status is
  // treated as locked and turns the otherwise-successful reply into a failure.
  // Public so the narrow Settings test target can exercise the production
  // decision and publication sequence without constructing a full WebUI.
  static maho_settings::mojom::VaultOperationResultPtr
  FinalizeVaultLifecycleOperation(
      maho_settings::mojom::VaultOperationResultPtr operation,
      maho_settings::mojom::VaultOperationResultPtr canonical_status,
      base::FunctionRef<void(bool locked)> publish_lock_state) {
    if (!operation || !operation->success) {
      return operation;
    }
    if (!canonical_status || !canonical_status->success ||
        !canonical_status->status) {
      publish_lock_state(/*locked=*/true);
      operation->success = false;
      operation->status.reset();
      operation->error_code = "status_unavailable";
      operation->error_message = "Vault operation failed.";
      return operation;
    }

    const bool locked =
        canonical_status->status->lock_state !=
        maho_settings::mojom::VaultLockState::kUnlocked;
    operation->status = canonical_status->status.Clone();
    publish_lock_state(locked);
    return operation;
  }

  static bool CanDeleteProfileForTesting(bool is_default,
                                         bool is_final_ready_profile,
                                         bool is_host,
                                         bool has_space_references,
                                         maho::ProfileLifecycleState lifecycle) {
    return !is_default && !is_final_ready_profile && !is_host &&
           !has_space_references &&
           lifecycle == maho::ProfileLifecycleState::kReady;
  }
  static maho_settings::mojom::CurrentBrowserSpaceSnapshotPtr
  ParseCurrentBrowserSpaceSnapshotForTesting(
      int32_t focused_browser_session_id,
      const std::string& selected_space_id,
      const std::string& spaces_json,
      const std::string& tabs_json) {
    if (focused_browser_session_id <= 0 || selected_space_id.empty() ||
        spaces_json.empty() || tabs_json.empty()) {
      return nullptr;
    }
    std::optional<base::Value> parsed_spaces =
        base::JSONReader::Read(spaces_json, base::JSON_PARSE_RFC);
    std::optional<base::Value> parsed_tabs =
        base::JSONReader::Read(tabs_json, base::JSON_PARSE_RFC);
    if (!parsed_spaces || !parsed_spaces->is_list() || !parsed_tabs ||
        !parsed_tabs->is_list()) {
      return nullptr;
    }

    maho_settings::mojom::SpaceBasicInfoPtr selected_space;
    for (const base::Value& item : parsed_spaces->GetList()) {
      const base::DictValue* dict = item.GetIfDict();
      if (!dict) {
        continue;
      }
      const std::string* id = dict->FindString("id");
      if (!id || *id != selected_space_id) {
        continue;
      }
      const std::string* name = dict->FindString("name");
      if (!name) {
        return nullptr;
      }
      selected_space = maho_settings::mojom::SpaceBasicInfo::New();
      selected_space->id = *id;
      selected_space->name = *name;
      break;
    }
    if (!selected_space) {
      return nullptr;
    }

    std::vector<std::string> tab_ids;
    tab_ids.reserve(parsed_tabs->GetList().size());
    for (const base::Value& item : parsed_tabs->GetList()) {
      const base::DictValue* dict = item.GetIfDict();
      const std::string* id = dict ? dict->FindString("id") : nullptr;
      if (!id || id->empty()) {
        return nullptr;
      }
      tab_ids.push_back(*id);
    }

    auto snapshot =
        maho_settings::mojom::CurrentBrowserSpaceSnapshot::New();
    snapshot->focused_browser_session_id = focused_browser_session_id;
    snapshot->selected_space = std::move(selected_space);
    snapshot->assigned_tab_ids = std::move(tab_ids);
    snapshot->assigned_window_ids = {
        std::to_string(focused_browser_session_id)};
    return snapshot;
  }
  static bool IsCurrentBrowserSpaceSnapshotProfileAllowedForTesting(
      bool is_regular_profile,
      bool is_off_the_record,
      bool is_guest,
      bool is_system) {
    return is_regular_profile && !is_off_the_record && !is_guest && !is_system;
  }
  static maho_settings::mojom::ProfileTargetErrorPtr
  MakeLookupErrorForTesting(
      maho::ProfileRegistryLookupError error,
      uint64_t current_context_revision,
      std::optional<uint64_t> current_profile_revision) {
    auto target_error = maho_settings::mojom::ProfileTargetError::New();
    switch (error) {
      case maho::ProfileRegistryLookupError::kInvalidProfileId:
        target_error->code =
            maho_settings::mojom::ProfileTargetErrorCode::kInvalidProfileId;
        target_error->message = "Selected profile ID is invalid.";
        break;
      case maho::ProfileRegistryLookupError::kUnknownProfile:
        target_error->code =
            maho_settings::mojom::ProfileTargetErrorCode::kProfileNotFound;
        target_error->message = "Selected profile was not found.";
        break;
      case maho::ProfileRegistryLookupError::kProfileDeleting:
        target_error->code =
            maho_settings::mojom::ProfileTargetErrorCode::kProfileDeleting;
        target_error->message = "Selected profile is being deleted.";
        break;
      case maho::ProfileRegistryLookupError::kStaleRevision:
        target_error->code =
            maho_settings::mojom::ProfileTargetErrorCode::kStaleContext;
        target_error->message = "Selected profile context is stale.";
        break;
      case maho::ProfileRegistryLookupError::kCatalogUnavailable:
        target_error->code =
            maho_settings::mojom::ProfileTargetErrorCode::kProfileUnavailable;
        target_error->message =
            "Selected profile catalog is unavailable.";
        break;
      case maho::ProfileRegistryLookupError::kProfileProvisioning:
        target_error->code =
            maho_settings::mojom::ProfileTargetErrorCode::kProfileUnavailable;
        target_error->message =
            "Selected profile is unavailable while provisioning.";
        break;
      case maho::ProfileRegistryLookupError::kProfileRepairRequired:
        target_error->code =
            maho_settings::mojom::ProfileTargetErrorCode::kProfileUnavailable;
        target_error->message =
            "Selected profile is unavailable until repaired.";
        break;
      case maho::ProfileRegistryLookupError::kNone:
        target_error->code = maho_settings::mojom::ProfileTargetErrorCode::kNone;
        break;
    }
    target_error->current_context_revision = current_context_revision;
    target_error->current_profile_revision = current_profile_revision;
    return target_error;
  }

  void GetSettings(GetSettingsCallback callback) override;
  void SetSetting(const std::string& key,
                  const std::string& value,
                  SetSettingCallback callback) override;
  void GetSearchEngines(GetSearchEnginesCallback callback) override;
  void SetDefaultSearchEngine(
      const std::string& keyword,
      SetDefaultSearchEngineCallback callback) override;
  void GetGlobalSettingsSnapshot(
      GetGlobalSettingsSnapshotCallback callback) override;
  void GetSelectedProfileContext(
      maho_settings::mojom::ProfileTargetPtr target,
      GetSelectedProfileContextCallback callback) override;
  void GetSelectedProfileMetadata(
      maho_settings::mojom::ProfileTargetPtr target,
      GetSelectedProfileMetadataCallback callback) override;
  void UpdateSelectedProfileMetadata(
      maho_settings::mojom::ProfileTargetPtr target,
      uint64_t expected_context_revision,
      maho_settings::mojom::ProfileMetadataUpdatePtr update,
      UpdateSelectedProfileMetadataCallback callback) override;
  void GetSelectedProfileSearchSettings(
      maho_settings::mojom::ProfileTargetPtr target,
      GetSelectedProfileSearchSettingsCallback callback) override;
  void SetSelectedProfileDefaultSearchEngine(
      maho_settings::mojom::ProfileTargetPtr target,
      uint64_t expected_context_revision,
      const std::string& keyword,
      uint64_t expected_profile_revision,
      SetSelectedProfileDefaultSearchEngineCallback callback) override;
  void SetSelectedProfileSearchSuggestionsEnabled(
      maho_settings::mojom::ProfileTargetPtr target,
      uint64_t expected_context_revision,
      bool enabled,
      uint64_t expected_profile_revision,
      SetSelectedProfileSearchSuggestionsEnabledCallback callback) override;
  void GetSelectedProfileDownloadSettings(
      maho_settings::mojom::ProfileTargetPtr target,
      GetSelectedProfileDownloadSettingsCallback callback) override;
  void SetSelectedProfileDownloadPrompt(
      maho_settings::mojom::ProfileTargetPtr target,
      uint64_t expected_context_revision,
      bool prompt_for_download,
      uint64_t expected_profile_revision,
      SetSelectedProfileDownloadPromptCallback callback) override;
  void SelectSelectedProfileDownloadDirectory(
      maho_settings::mojom::ProfileTargetPtr target,
      uint64_t expected_context_revision,
      uint64_t expected_profile_revision,
      SelectSelectedProfileDownloadDirectoryCallback callback) override;
  void GetSelectedProfileArchiveSettings(
      maho_settings::mojom::ProfileTargetPtr target,
      GetSelectedProfileArchiveSettingsCallback callback) override;
  void SetSelectedProfileArchiveTimeout(
      maho_settings::mojom::ProfileTargetPtr target,
      uint64_t expected_context_revision,
      int32_t timeout_hours,
      uint64_t expected_profile_revision,
      SetSelectedProfileArchiveTimeoutCallback callback) override;

  void GetBrowserVersionInfo(GetBrowserVersionInfoCallback callback) override;
  void CheckForBrowserUpdates() override;
  void ApplyBrowserUpdateAndRestart() override;

  void GetProfiles(GetProfilesCallback callback) override;
  void GetProfileObservablesSnapshot(
      GetProfileObservablesSnapshotCallback callback) override;
  void CreateProfile(const std::string& name,
                     CreateProfileCallback callback) override;
  void DeleteProfile(const std::string& profile_id,
                     DeleteProfileCallback callback) override;
  void SwitchProfile(const std::string& profile_id,
                     SwitchProfileCallback callback) override;

  void GetATCRules(GetATCRulesCallback callback) override;
  void AddATCRule(const std::string& url_pattern,
                  const std::string& target_space_id,
                  AddATCRuleCallback callback) override;
  void RemoveATCRule(const std::string& rule_id) override;
  void ToggleATCRule(const std::string& rule_id, bool enabled) override;

  void GetAISettings(GetAISettingsCallback callback) override;
  void GetAIModelsSettings(GetAIModelsSettingsCallback callback) override;
  maho_settings::mojom::AIModelsSettingsPtr BuildAIModelsSettings();
  void OnAIModelsSettingsPrefsChanged();
  void SetDefaultAIModel(const std::string& provider_id,
                         const std::string& model_id,
                         SetDefaultAIModelCallback callback) override;
  void SetTaskAIModel(const std::string& task_id,
                      const std::optional<std::string>& provider_id,
                      const std::optional<std::string>& model_id,
                      SetTaskAIModelCallback callback) override;
  void SetAIProviderBaseUrl(const std::string& provider_id,
                            const std::string& base_url,
                            SetAIProviderBaseUrlCallback callback) override;
  void TestAIProvider(const std::string& provider_id,
                      TestAIProviderCallback callback) override;
  void DisconnectAIProvider(
      const std::string& provider_id,
      const std::optional<std::string>& replacement_default_provider_id,
      const std::optional<std::string>& replacement_default_model_id,
      std::vector<maho_settings::mojom::AITaskReplacementPtr> task_replacements,
      DisconnectAIProviderCallback callback) override;
  void SetAIProvider(const std::string& provider) override;
  void SetAIBaseUrl(const std::string& url) override;
  void SetAIApiKey(const std::string& key) override;
  void SetAIModel(const std::string& model) override;
  void SetAIApprovalPolicy(const std::string& policy) override;
  void SetAISessionPersistence(bool enabled) override;
  void SetAIMailReadAllowed(bool allowed,
                            SetAIMailReadAllowedCallback callback) override;
  void FetchProviderModels(const std::string& provider_id,
                           FetchProviderModelsCallback callback) override;
  void RefreshProviderModels(const std::string& provider_id,
                             RefreshProviderModelsCallback callback) override;
  void TestManagedConnection(TestManagedConnectionCallback callback) override;

  void GetShortcuts(GetShortcutsCallback callback) override;
  void SetShortcut(const std::string& action,
                   maho_settings::mojom::MojoKeyComboPtr key_combo,
                   SetShortcutCallback callback) override;
  void CheckShortcutConflict(maho_settings::mojom::MojoKeyComboPtr key_combo,
                             CheckShortcutConflictCallback callback) override;
  void ResetShortcut(const std::string& action) override;
  void ResetAllShortcuts() override;
  void ToggleShortcut(const std::string& action, bool enabled) override;
  void SetRecordingMode(bool enabled) override;
  void ExportShortcuts(ExportShortcutsCallback callback) override;
  void ImportShortcuts(const std::string& json_data,
                       ImportShortcutsCallback callback) override;

  void GetSyncStatus(GetSyncStatusCallback callback) override;
  void GetSyncDevices(GetSyncDevicesCallback callback) override;
  void GenerateSyncKey(GenerateSyncKeyCallback callback) override;
  void ConfigureSyncEncryption(
      const std::string& recovery_phrase,
      ConfigureSyncEncryptionCallback callback) override;
  void JoinSync(const std::string& recovery_phrase,
                JoinSyncCallback callback) override;
  void StopSync() override;
  void DisconnectSyncDevice(const std::string& device_id,
                            DisconnectSyncDeviceCallback callback) override;
  void RenameSyncDevice(const std::string& device_id,
                        const std::string& new_name,
                        RenameSyncDeviceCallback callback) override;

  void GetSpaces(GetSpacesCallback callback) override;
  void GetCurrentBrowserSpaceSnapshot(
      GetCurrentBrowserSpaceSnapshotCallback callback) override;

  void GetVaultStatus(GetVaultStatusCallback callback) override;
  void GetVaultPreflightState(
      GetVaultPreflightStateCallback callback) override;
  void GetVaultProviderStatus(GetVaultProviderStatusCallback callback) override;
  void InitializeVault(const std::string& master_passphrase,
                       const std::string& recovery_secret,
                       InitializeVaultCallback callback) override;
  void UnlockVault(const std::string& master_passphrase,
                   UnlockVaultCallback callback) override;
  void UnlockVaultWithRecovery(
      const std::string& recovery_secret,
      UnlockVaultWithRecoveryCallback callback) override;
  void LockVault(LockVaultCallback callback) override;
  void LockNow(LockNowCallback callback) override;
  void ListVaultItems(
      std::optional<maho_settings::mojom::PasswordProviderKind> provider,
      const std::vector<maho_settings::mojom::VaultItemKind>& kinds,
      const std::optional<std::string>& cursor,
      uint32_t limit,
      bool trash_only,
      bool favorites_only,
      ListVaultItemsCallback callback) override;
  void SearchVaultItems(
      const std::string& origin,
      std::optional<maho_settings::mojom::PasswordProviderKind> provider,
      const std::vector<maho_settings::mojom::VaultItemKind>& kinds,
      SearchVaultItemsCallback callback) override;
  void AddVaultLogin(const std::string& title,
                     const std::vector<std::string>& origins,
                     const std::string& username,
                     const std::string& password,
                     const std::optional<std::string>& notes,
                     AddVaultLoginCallback callback) override;
  void UpdateVaultLogin(const std::string& item_id,
                        uint64_t expected_revision,
                        const std::string& title,
                        const std::vector<std::string>& origins,
                        const std::string& username,
                        const std::optional<std::string>& password,
                        const std::optional<std::string>& notes,
                        UpdateVaultLoginCallback callback) override;
  void TrashVaultItem(const std::string& item_id, uint64_t expected_revision,
                      TrashVaultItemCallback callback) override;
  void RestoreVaultItem(const std::string& item_id, uint64_t expected_revision,
                        RestoreVaultItemCallback callback) override;
  void EmptyVaultTrash(EmptyVaultTrashCallback callback) override;
  void SetVaultItemFavorite(const std::string& item_id,
                            uint64_t expected_revision, bool favorite,
                            SetVaultItemFavoriteCallback callback) override;
  void GetVaultItemNotes(const std::string& item_id,
                         GetVaultItemNotesCallback callback) override;
  void AddVaultSecureNote(const std::string& title, const std::string& notes,
                          AddVaultSecureNoteCallback callback) override;
  void UpdateVaultSecureNote(const std::string& item_id,
                             uint64_t expected_revision,
                             const std::string& title, const std::string& notes,
                             UpdateVaultSecureNoteCallback callback) override;
  void SetVaultLoginTotp(const std::string& item_id, uint64_t expected_revision,
                         const std::string& secret,
                         SetVaultLoginTotpCallback callback) override;
  void GetVaultTotpCode(const std::string& item_id,
                        GetVaultTotpCodeCallback callback) override;
  void GeneratePassword(maho_settings::mojom::PasswordGeneratorOptionsPtr options,
                         GeneratePasswordCallback callback) override;
  void EstimatePasswordStrength(const std::string& password,
                                 EstimatePasswordStrengthCallback callback) override;
  void GetVaultHealthReport(GetVaultHealthReportCallback callback) override;
  void DeleteVaultItem(const std::string& item_id,
                       uint64_t expected_revision,
                       DeleteVaultItemCallback callback) override;
  void UseVaultSecret(const std::string& item_id,
                      uint64_t expected_revision,
                      maho_settings::mojom::SecretAction action,
                      UseVaultSecretCallback callback) override;
  void GetVaultPolicyStatus(GetVaultPolicyStatusCallback callback) override;
  void SetVaultPolicy(maho_settings::mojom::VaultAgentPolicy policy,
                      const std::optional<std::string>& item_id,
                      const std::optional<std::string>& origin,
                      const std::optional<std::string>& expires_at,
                      SetVaultPolicyCallback callback) override;
  void GetVaultAuditPage(const std::optional<std::string>& cursor,
                         uint32_t limit,
                         GetVaultAuditPageCallback callback) override;
  void SelectPasswordImportFile(
      maho_settings::mojom::PasswordImportSourceFormat source_format,
      SelectPasswordImportFileCallback callback) override;
  void PreviewPasswordImport(
      maho_settings::mojom::PasswordImportSourceFormat source_format,
      const std::string& file_path,
      PreviewPasswordImportCallback callback) override;
  void CancelPasswordImport(CancelPasswordImportCallback callback) override;
  void CommitPasswordImport(const std::string& preview_token,
                            CommitPasswordImportCallback callback) override;

  void GetPasswordProviderStatus(
      GetPasswordProviderStatusCallback callback) override;
  void GetPasswordProviderOptions(
      GetPasswordProviderOptionsCallback callback) override;
  void GetSavedPasswords(GetSavedPasswordsCallback callback) override;
  void SearchPasswords(const std::string& query,
                       SearchPasswordsCallback callback) override;
  void AddPassword(const std::string& domain,
                   const std::string& username,
                   const std::string& password,
                   AddPasswordCallback callback) override;
  void UpdatePasswordUsername(const std::string& password_id,
                              const std::string& username,
                              UpdatePasswordUsernameCallback callback) override;
  void DeletePassword(const std::string& password_id,
                      DeletePasswordCallback callback) override;

  void GetAutofillAddresses(GetAutofillAddressesCallback callback) override;
  void AddAutofillAddress(const std::string& id,
                          const std::string& name,
                          const std::string& address_line1,
                          const std::string& address_line2,
                          const std::string& city,
                          const std::string& state,
                          const std::string& postal_code,
                          const std::string& country,
                          const std::string& phone,
                          const std::string& email,
                          AddAutofillAddressCallback callback) override;
  void DeleteAutofillAddress(const std::string& id,
                             DeleteAutofillAddressCallback callback) override;
  void GetAutofillPayments(GetAutofillPaymentsCallback callback) override;
  void AddAutofillPayment(const std::string& id,
                          const std::string& card_network,
                          const std::string& last_four,
                          const std::string& expiration,
                          const std::string& cardholder_name,
                          AddAutofillPaymentCallback callback) override;
  void DeleteAutofillPayment(const std::string& id,
                             DeleteAutofillPaymentCallback callback) override;

  void GetFilterLists(GetFilterListsCallback callback) override;
  void AddFilterList(const std::string& id,
                     const std::string& name,
                     const std::string& url,
                     AddFilterListCallback callback) override;
  void ToggleFilterList(const std::string& id,
                        bool enabled,
                        ToggleFilterListCallback callback) override;
  void RemoveFilterList(const std::string& id,
                        RemoveFilterListCallback callback) override;
  void RebuildContentRules(RebuildContentRulesCallback callback) override;
  void GetContentBlockerStats(GetContentBlockerStatsCallback callback) override;
  void SetContentBlockingMode(maho_settings::mojom::ContentBlockingMode mode,
                              SetContentBlockingModeCallback callback) override;
  void TriggerFilterUpdate(const std::optional<std::string>& list_id,
                           TriggerFilterUpdateCallback callback) override;

  void OpenExtensionsPage() override;
  void OpenChromiumSettingsPage() override;
  void OpenMigrationDialog() override;

  void SetBYOKKey(const std::string& provider,
                  const std::string& key,
                  SetBYOKKeyCallback callback) override;
  void SetAIProviderOAuthClientId(
      const std::string& provider,
      const std::string& client_id,
      SetAIProviderOAuthClientIdCallback callback) override;
  void SignInToAIProvider(const std::string& provider,
                          SignInToAIProviderCallback callback) override;
  void SignOutOfAIProvider(const std::string& provider,
                           SignOutOfAIProviderCallback callback) override;
  void OnProviderOAuthCompleted(const std::string& provider,
                                SignInToAIProviderCallback callback,
                                bool ok,
                                maho::ai_oauth::ProviderTokens tokens,
                                const std::string& error_message);
  void ClearBYOKKey(const std::string& provider,
                    ClearBYOKKeyCallback callback) override;

  void Login(const std::string& email,
             const std::string& password,
             LoginCallback callback) override;
  void SignInWithGoogle(SignInWithGoogleCallback callback) override;
  void Logout(LogoutCallback callback) override;
  void GetAccountStatus(GetAccountStatusCallback callback) override;
  void GetBillingInfo(GetBillingInfoCallback callback) override;
  void GetInvoices(GetInvoicesCallback callback) override;
  void GetBillingPortalUrl(GetBillingPortalUrlCallback callback) override;
  void GetBuyCreditsUrl(int32_t pack_size_usd,
                        GetBuyCreditsUrlCallback callback) override;
  void GetSubscriptionCheckoutUrl(
      const std::string& tier,
      GetSubscriptionCheckoutUrlCallback callback) override;

  // Mail accounts — thin proxies to the profile-keyed MahoMailService.
  void MailListAccounts(MailListAccountsCallback callback) override;
  void MailAddAccount(const std::string& request_json,
                      MailAddAccountCallback callback) override;
  void MailTestConnection(const std::string& request_json,
                          MailTestConnectionCallback callback) override;
  void MailDeleteAccount(const std::string& account_id,
                         MailDeleteAccountCallback callback) override;
  void MailBeginOAuth(const std::string& provider,
                      MailBeginOAuthCallback callback) override;
  void MailOAuthComplete(const std::string& state,
                         const std::string& code,
                         MailOAuthCompleteCallback callback) override;
  void MailReconnectAccount(const std::string& account_id,
                            MailReconnectAccountCallback callback) override;
  void MailOAuthCancel(const std::string& state,
                       MailOAuthCancelCallback callback) override;

  // Signatures
  void MailListSignatures(const std::optional<std::string>& account_id,
                          MailListSignaturesCallback callback) override;
  void MailUpsertSignature(const std::string& request_json,
                           MailUpsertSignatureCallback callback) override;
  void MailDeleteSignature(const std::string& id,
                           MailDeleteSignatureCallback callback) override;

  // Templates
  void MailListTemplates(MailListTemplatesCallback callback) override;
  void MailUpsertTemplate(const std::string& request_json,
                          MailUpsertTemplateCallback callback) override;
  void MailDeleteTemplate(const std::string& id,
                          MailDeleteTemplateCallback callback) override;

  // Labels
  void MailListLabels(const std::string& account_id,
                      MailListLabelsCallback callback) override;
  void MailCreateLabel(const std::string& account_id,
                       const std::string& name,
                       const std::string& color,
                       MailCreateLabelCallback callback) override;
  void MailDeleteLabel(const std::string& id,
                       MailDeleteLabelCallback callback) override;

  // Rules
  void MailListRules(const std::string& account_id,
                     MailListRulesCallback callback) override;
  void MailUpsertRule(const std::string& request_json,
                      MailUpsertRuleCallback callback) override;
  void MailDeleteRule(const std::string& id,
                      MailDeleteRuleCallback callback) override;
  void MailReorderRules(const std::string& account_id,
                        const std::string& rule_ids_json,
                        MailReorderRulesCallback callback) override;

  // Calendar
  void MailGetCalendarPrefs(MailGetCalendarPrefsCallback callback) override;
  void MailSetCalendarPrefs(const std::string& prefs_json,
                            MailSetCalendarPrefsCallback callback) override;
  void MailListCalendarCategories(
      const std::string& account_id,
      MailListCalendarCategoriesCallback callback) override;
  void MailCreateCalendarCategory(
      const std::string& account_id,
      const std::string& name,
      const std::string& color,
      MailCreateCalendarCategoryCallback callback) override;
  void MailUpdateCalendarCategory(
      const std::string& id,
      const std::string& name,
      const std::string& color,
      MailUpdateCalendarCategoryCallback callback) override;
  void MailDeleteCalendarCategory(
      const std::string& id,
      MailDeleteCalendarCategoryCallback callback) override;
  void MailListAccountCalendars(
      const std::string& account_id,
      MailListAccountCalendarsCallback callback) override;
  void MailSetCalendarVisibility(
      const std::string& calendar_row_id,
      bool visible,
      MailSetCalendarVisibilityCallback callback) override;

  // Behavior
  void MailGetBehaviorPrefs(MailGetBehaviorPrefsCallback callback) override;
  void MailSetBehaviorPref(uint64_t expected_revision,
                           const std::string& key,
                           const std::string& value,
                           MailSetBehaviorPrefCallback callback) override;
  void MailRequestNotificationPermission(
      MailRequestNotificationPermissionCallback callback) override;

  // Security (PGP/S-MIME)
  void MailGeneratePgpKey(const std::string& account_id,
                          const std::string& email,
                          const std::string& passphrase,
                          MailGeneratePgpKeyCallback callback) override;
  void MailImportPgpKey(const std::string& account_id,
                        const std::string& key_block,
                        const std::optional<std::string>& passphrase,
                        MailImportPgpKeyCallback callback) override;
  void MailListPgpKeys(const std::string& account_id,
                       MailListPgpKeysCallback callback) override;
  void MailDeletePgpKey(const std::string& key_id,
                        MailDeletePgpKeyCallback callback) override;
  void MailSetDefaultPgpKey(const std::string& account_id,
                            const std::string& key_id,
                            MailSetDefaultPgpKeyCallback callback) override;
  void MailImportSmimeIdentity(
      const std::string& account_id,
      const std::string& p12_data_base64,
      const std::string& passphrase,
      MailImportSmimeIdentityCallback callback) override;
  void MailListSmimeIdentities(
      const std::string& account_id,
      MailListSmimeIdentitiesCallback callback) override;
  void MailDeleteSmimeIdentity(
      const std::string& id,
      MailDeleteSmimeIdentityCallback callback) override;
  void MailSetDefaultSmimeIdentity(
      const std::string& account_id,
      const std::string& identity_id,
      MailSetDefaultSmimeIdentityCallback callback) override;

  // MahoMailService::Observer overrides:
  void OnMutation(const std::string& account_id,
                  const std::string& payload) override;
  void OnCalendar(const std::string& account_id,
                  const std::string& payload) override;

 private:
  void StartProfileObservablesSnapshotRead(
      GetProfileObservablesSnapshotCallback callback,
      uint32_t attempt);
  void OnActiveMahoProfileIdRead(
      GetProfileObservablesSnapshotCallback callback,
      uint32_t attempt,
      maho::ProfileCatalogResult captured_catalog,
      base::FilePath handler_profile_basename,
      std::optional<std::string> active_profile_id_json);
  const maho::ProfileRegistryRecord* FindRegistryRecord(
      const std::string& canonical_id) const;
  maho_settings::mojom::SelectedProfileContextPtr BuildSelectedProfileContext()
      const;
  maho_settings::mojom::ProfileTargetErrorPtr MakeProfileTargetError(
      maho_settings::mojom::ProfileTargetErrorCode code,
      const std::string& message) const;
  void OnSelectedProfileLoaded(
      uint64_t load_generation,
      std::string canonical_id,
      std::string target_token,
      base::FilePath expected_path,
      uint64_t record_revision,
      GetSelectedProfileContextCallback callback,
      Profile* loaded_profile);
  void AttachSelectedProfile(const std::string& canonical_id,
                             const std::string& target_token,
                             uint64_t record_revision,
                             Profile* selected_profile);
  void InvalidateSelectedProfileContext();
  maho_settings::mojom::ProfileTargetErrorPtr ValidateSelectedProfileTarget(
      const maho_settings::mojom::ProfileTargetPtr& target,
      std::optional<uint64_t> expected_context_revision,
      std::optional<uint64_t> expected_profile_revision) const;
  void OnProfileMetadataUpdated(
      std::string profile_id,
      uint64_t context_revision,
      uint64_t profile_revision,
      UpdateSelectedProfileMetadataCallback callback,
      std::string json_result);
  void OnProfileArchiveSettingsLoaded(
      std::string profile_id,
      uint64_t context_revision,
      uint64_t profile_revision,
      GetSelectedProfileArchiveSettingsCallback callback,
      std::string json_result);
  void OnProfileArchiveTimeoutUpdated(
      std::string profile_id,
      uint64_t context_revision,
      uint64_t profile_revision,
      SetSelectedProfileArchiveTimeoutCallback callback,
      std::string json_result);
  void OnSelectedProfileDownloadDirectoryChosen(
      const base::FilePath& directory);

  // Seeds maho-core's canonical `open_external_links_in_maho_mini` value from
  // the legacy Chromium pref exactly once, guarded by the marker pref. No-op
  // once the marker is set, so later user writes to core are never clobbered.
  void MigrateAtcExternalMiniSettingToCore();

  static const char* GetPrefPath(const std::string& maho_key);
  std::string ReadSetting(const std::string& maho_key);
  void OnPrefChanged(const std::string& pref_name);

  // Builds the full settings snapshot. Used by both GetSettings() and the
  // coalesced notify path so there is a single construction site.
  std::vector<maho_settings::mojom::SettingValuePtr> BuildSettingsSnapshot();

  // Schedules a coalesced notify: if one is already pending the timer is
  // restarted, collapsing burst triggers into a single flush.
  void ScheduleNotify();

  // Fired by notify_timer_. Builds the snapshot, compares against the last
  // emitted value, and pushes to the page only if something changed.
  void FlushNotify();

  enum class FfiResult { kNotHandled, kOk, kInvalid };
  FfiResult HandleFfiSetting(const std::string& key, const std::string& value);
  void AppendFfiSettings(
      std::vector<maho_settings::mojom::SettingValuePtr>& settings);

  void OnRelayLoginCompleted(LoginCallback callback,
                             bool ok,
                             const std::string& error_message);
  void OnProfileCreatedInCore(
      CreateProfileCallback callback,
      maho_settings::mojom::ProfileInfoPtr profile);
  void OnChromiumProfileCreated(
      CreateProfileCallback callback,
      maho_settings::mojom::ProfileInfoPtr profile,
      Profile* chromium_profile);
  void OnSubscriptionResponse(GetBillingInfoCallback callback,
                              bool refreshed_once,
                              std::unique_ptr<network::SimpleURLLoader> loader,
                              std::optional<std::string> response_body);
  void BroadcastAccountStatus();

  void OnOpenMigrationDialogBrowsersDetected(
      std::vector<maho::DetectedBrowser> browsers);

  // Forwards a browser-process Vault lock-state transition (notably the
  // inactivity auto-lock evaluated by the periodic core tick) to the page so
  // transient secret UI is cleared and re-gated behind unlock.
  void OnVaultLockStateChanged(bool locked);
  void WebContentsDestroyed() override;
  void PrimaryPageChanged(content::Page& page) override;

  mojo::Receiver<maho_settings::mojom::PageHandler> receiver_;
  mojo::Remote<maho_settings::mojom::Page> page_;
  raw_ptr<Profile> profile_;
  raw_ptr<content::WebContents> host_web_contents_ = nullptr;
  raw_ptr<PrefService> prefs_;
  PrefChangeRegistrar pref_registrar_;
  raw_ptr<PrefService> local_state_;
  PrefChangeRegistrar local_state_pref_registrar_;
  base::CallbackListSubscription vault_lock_state_subscription_;
  std::unique_ptr<MahoVaultRevealDialog> vault_reveal_dialog_;
  std::unique_ptr<device_reauth::DeviceAuthenticator> vault_reveal_authenticator_;
  uint64_t vault_reveal_generation_ = 0;

  raw_ptr<Profile> selected_profile_ = nullptr;
  base::ScopedObservation<Profile, ProfileObserver>
      selected_profile_observation_{this};
  std::string selected_profile_id_;
  std::string selected_profile_target_token_;
  uint64_t selected_profile_context_revision_ = 0;
  uint64_t selected_profile_revision_ = 0;
  uint64_t selected_profile_load_generation_ = 0;
  bool selected_profile_attached_ = false;
  bool observing_profile_manager_ = false;
  base::ScopedObservation<maho::MahoSpaceProfileBridge,
                          maho::MahoSpaceProfileBridge::Observer>
      profile_bridge_observation_{this};

  base::OneShotTimer notify_timer_;
  std::string last_emitted_snapshot_;

  scoped_refptr<os_crypt_async::Encryptor> encryptor_;
  void OnOsCryptReady(scoped_refptr<os_crypt_async::Encryptor> encryptor);
  void OnProviderModelsRefreshed(const std::string& provider_id,
                                 const std::vector<std::string>& model_ids);
  void OnReauthAddComplete(
      const std::string& domain,
      const std::string& username,
      const std::string& password,
      std::unique_ptr<device_reauth::DeviceAuthenticator> authenticator,
      AddPasswordCallback callback,
      bool success);
  void OnReauthUpdateComplete(
      const std::string& password_id,
      const std::string& username,
      std::unique_ptr<device_reauth::DeviceAuthenticator> authenticator,
      UpdatePasswordUsernameCallback callback,
      bool success);
  void OnReauthDeleteComplete(
      const std::string& password_id,
      std::unique_ptr<device_reauth::DeviceAuthenticator> authenticator,
      DeletePasswordCallback callback,
      bool success);
  void OnReauthUseVaultSecretComplete(
      const std::string& item_id,
      uint64_t expected_revision,
      maho_settings::mojom::SecretAction action,
      bool fresh_os_auth,
      std::unique_ptr<device_reauth::DeviceAuthenticator> authenticator,
      UseVaultSecretCallback callback,
      bool success);
  void OnReauthSetVaultPolicyComplete(
      maho_settings::mojom::VaultAgentPolicy policy,
      const std::optional<std::string>& item_id,
      const std::optional<std::string>& origin,
      const std::optional<std::string>& expires_at,
      SetVaultPolicyCallback callback,
      bool success);

  struct PendingByokSave {
    PendingByokSave(std::string provider,
                    std::string key,
                    SetBYOKKeyCallback callback);
    ~PendingByokSave();
    PendingByokSave(PendingByokSave&&);
    PendingByokSave& operator=(PendingByokSave&&);
    std::string provider;
    std::string key;
    SetBYOKKeyCallback callback;
  };
  std::vector<PendingByokSave> pending_byok_saves_;

  std::unique_ptr<maho::ai::MahoModelListFetcher> model_list_fetcher_;
  std::unique_ptr<maho::ai::MahoManagedConnectionTest> managed_connection_test_;
  raw_ptr<MahoPasswordImportJob> password_import_job_ = nullptr;
  scoped_refptr<ui::SelectFileDialog> password_import_select_file_dialog_;
  SelectPasswordImportFileCallback password_import_file_callback_;
  scoped_refptr<ui::SelectFileDialog> download_directory_select_file_dialog_;
  SelectSelectedProfileDownloadDirectoryCallback
      download_directory_select_callback_;
  std::string download_directory_profile_id_;
  std::string download_directory_target_token_;
  uint64_t download_directory_context_revision_ = 0;
  uint64_t download_directory_profile_revision_ = 0;
  double last_known_credit_balance_ = -1.0;
  double last_known_tier_ceiling_ = -1.0;

  static MahoSettingsPageHandler* g_active_instance;

  SEQUENCE_CHECKER(sequence_checker_);

  base::WeakPtrFactory<MahoSettingsPageHandler> weak_factory_{this};
};

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_SETTINGS_MAHO_SETTINGS_PAGE_HANDLER_H_
