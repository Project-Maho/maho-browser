// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_MAHO_SPACE_PROFILE_BRIDGE_H_
#define MAHO_BROWSER_MAHO_SPACE_PROFILE_BRIDGE_H_

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/containers/flat_map.h"
#include "base/files/file_path.h"
#include "base/no_destructor.h"
#include "base/observer_list.h"
#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "base/sequence_checker.h"
#include "chrome/browser/ui/browser_window/public/browser_collection_observer.h"
#include "maho/browser/maho_space_profile_hydration.h"

class GlobalBrowserCollection;

class Browser;
class BrowserWindowInterface;
class PrefRegistrySimple;
class PrefService;
class Profile;

namespace maho {

class MahoSpaceProfileBridge : public BrowserCollectionObserver {
 public:
  class Observer : public base::CheckedObserver {
   public:
    virtual void OnSpaceProfileBridgeChanged(bool is_structural);
    virtual void OnSpaceProfileBridgeChanged() = 0;

   protected:
    ~Observer() override = default;
  };

  static MahoSpaceProfileBridge* GetInstance();
  static void RegisterLocalStatePrefs(PrefRegistrySimple* registry);

  MahoSpaceProfileBridge(const MahoSpaceProfileBridge&) = delete;
  MahoSpaceProfileBridge& operator=(const MahoSpaceProfileBridge&) = delete;

  void RegisterSpace(const std::string& space_id,
                     const base::FilePath& profile_path);
  void UnregisterSpace(const std::string& space_id);

  bool SwitchToSpace(const std::string& space_id);
  bool SwitchToSpace(Browser* browser, const std::string& space_id);

  bool HydrateFromCore();
  bool HydrateFromSnapshot(ProfileCatalogResult catalog,
                           SpaceProfileHydrationState state);
  bool HydrateFromSerializedStateForTesting(
      std::string_view spaces_json,
      std::string_view active_space_id_json);

  void SetActiveSpaceId(const std::string& space_id);
  void SetActiveSpaceId(Browser* browser, const std::string& space_id);
  void NotifyChanged(bool is_structural = true);

  void SetLastActiveTab(const std::string& space_id, const std::string& tab_id);
  std::string GetLastActiveTab(const std::string& space_id) const;

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  const std::string& GetActiveSpaceId() const;
  // The per-browser map is the canonical UI active-space state. The global
  // active_space_id_ value is only a creation seed and no-browser fallback;
  // mapped lookup fails closed when a Browser has no explicit entry.
  const std::string& GetActiveSpaceId(Browser* browser) const;
  const std::string& GetActiveSpaceId(BrowserWindowInterface* browser) const;
  bool IsSpaceRegistered(std::string_view space_id) const;
  size_t GetRegisteredSpaceCount() const;
  int GetActiveSpaceIndex() const;
  int GetActiveSpaceIndex(Browser* browser) const;

  void ClearBrowserActiveSpace(Browser* browser);
  std::vector<Browser*> GetBrowsersForSpaces(
      const std::vector<std::string>& space_ids) const;
  void ClearBrowserActiveSpacesForTesting();
  void ReconcileExistingBrowsersActiveSpace();
  void EnsureBrowserCollectionObservationForTesting();
  void ResetBrowserCollectionObservationForTesting();
  // Drives the BrowserCollectionObserver path without a real
  // GlobalBrowserCollection notification. The override itself stays private so
  // the bridge keeps owning when browser lifecycle events are processed.
  void OnBrowserCreatedForTesting(BrowserWindowInterface* browser) {
    OnBrowserCreated(browser);
  }

  std::vector<std::string> GetSpaceIds() const;
  std::string GetSpaceIdForProfile(Profile* profile) const;
  base::FilePath GetProfilePathForSpace(const std::string& space_id) const;

  bool ReconcileProfileRegistry(std::string_view profiles_json,
                                std::string_view active_profile_id_json,
                                std::string_view spaces_json);
  bool ReconcileProfileRegistry(ProfileCatalogResult catalog);
  bool ReconcileProfileRegistryFromCore();
  void SetLocalStateForTesting(PrefService* local_state);
  const ProfileCatalogResult& GetProfileCatalog() const;
  uint64_t GetProfileRegistryRevision() const;
  std::optional<std::string> CanonicalizeProfileId(
      std::string_view profile_id) const;
  ProfileRegistryPathResult ResolveProfilePath(
      std::string_view profile_id,
      uint64_t expected_registry_revision) const;
  bool SetProfileLifecycleState(std::string_view profile_id,
                                ProfileLifecycleState state);
  bool UpdateProfileSettings(std::string_view profile_id,
                             const std::string* name,
                             const std::string* avatar_color,
                             const std::optional<int32_t>* archive_timeout_hours);

  static constexpr size_t kMahoProfileIdMaxLen = 64;
  static std::optional<base::FilePath> ProfileBasenameForId(
      std::string_view profile_id);

 private:
  friend class base::NoDestructor<MahoSpaceProfileBridge>;
  MahoSpaceProfileBridge();
  ~MahoSpaceProfileBridge() override;

  // BrowserCollectionObserver:
  void OnBrowserCreated(BrowserWindowInterface* browser) override;
  void OnBrowserClosed(BrowserWindowInterface* browser) override;

  void LoadProfileRegistryFromLocalState();
  void PersistProfileRegistryToLocalState();
  PrefService* GetLocalState() const;

  base::flat_map<std::string, base::FilePath> space_to_profile_;
  ProfileCatalogResult profile_catalog_;
  raw_ptr<PrefService> local_state_for_testing_ = nullptr;
  base::flat_map<std::string, std::string> last_active_tab_per_space_;
  std::string active_space_id_;
  base::flat_map<Browser*, std::string> browser_active_space_;
  base::ObserverList<Observer, true> observers_;
  base::ScopedObservation<GlobalBrowserCollection, BrowserCollectionObserver>
      browser_collection_observation_{this};
  SEQUENCE_CHECKER(sequence_checker_);
};

}  // namespace maho

#endif  // MAHO_BROWSER_MAHO_SPACE_PROFILE_BRIDGE_H_
