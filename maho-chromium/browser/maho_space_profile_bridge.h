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
#include "base/scoped_observation.h"
#include "base/sequence_checker.h"
#include "chrome/browser/ui/browser_window/public/browser_collection_observer.h"

class GlobalBrowserCollection;

class Browser;
class BrowserWindowInterface;

namespace maho {

class MahoSpaceProfileBridge : public BrowserCollectionObserver {
 public:
  class Observer : public base::CheckedObserver {
   public:
    virtual void OnSpaceProfileBridgeChanged() = 0;

   protected:
    ~Observer() override = default;
  };

  static MahoSpaceProfileBridge* GetInstance();

  MahoSpaceProfileBridge(const MahoSpaceProfileBridge&) = delete;
  MahoSpaceProfileBridge& operator=(const MahoSpaceProfileBridge&) = delete;

  void RegisterSpace(const std::string& space_id,
                     const base::FilePath& profile_path);
  void UnregisterSpace(const std::string& space_id);

  bool SwitchToSpace(const std::string& space_id);
  bool SwitchToSpace(Browser* browser, const std::string& space_id);

  void SetActiveSpaceId(const std::string& space_id);
  void SetActiveSpaceId(Browser* browser, const std::string& space_id);
  void NotifyChanged();

  void SetLastActiveTab(const std::string& space_id,
                       const std::string& tab_id);
  std::string GetLastActiveTab(const std::string& space_id) const;

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  const std::string& GetActiveSpaceId() const;
  // The per-browser map is the canonical UI active-space state; the global
  // active_space_id_ value exists only as a seed and as a no-browser-context fallback.
  const std::string& GetActiveSpaceId(Browser* browser) const;
  const std::string& GetActiveSpaceId(BrowserWindowInterface* browser) const;
  size_t GetRegisteredSpaceCount() const;
  int GetActiveSpaceIndex() const;
  int GetActiveSpaceIndex(Browser* browser) const;

  void ClearBrowserActiveSpace(Browser* browser);

  std::vector<std::string> GetSpaceIds() const;
  base::FilePath GetProfilePathForTesting(const std::string& space_id) const;

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

  base::flat_map<std::string, base::FilePath> space_to_profile_;
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
