// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_MAHO_SPACE_PROFILE_BRIDGE_H_
#define MAHO_BROWSER_MAHO_SPACE_PROFILE_BRIDGE_H_

#include <cstddef>
#include <string>
#include <vector>

#include "base/containers/flat_map.h"
#include "base/files/file_path.h"
#include "base/no_destructor.h"
#include "base/sequence_checker.h"
#include "base/observer_list.h"

namespace maho {

class MahoSpaceProfileBridge {
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

  void SetActiveSpaceId(const std::string& space_id);
  void NotifyChanged();

  void SetLastActiveTab(const std::string& space_id, int tab_id);
  int GetLastActiveTab(const std::string& space_id) const;

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  const std::string& GetActiveSpaceId() const;
  size_t GetRegisteredSpaceCount() const;
  int GetActiveSpaceIndex() const;

  std::vector<std::string> GetSpaceIds() const;

 private:
  friend class base::NoDestructor<MahoSpaceProfileBridge>;
  MahoSpaceProfileBridge();
  ~MahoSpaceProfileBridge();

  base::flat_map<std::string, base::FilePath> space_to_profile_;
  base::flat_map<std::string, int> last_active_tab_per_space_;
  std::string active_space_id_;
  base::ObserverList<Observer, true> observers_;
  SEQUENCE_CHECKER(sequence_checker_);
};

}  // namespace maho

#endif  // MAHO_BROWSER_MAHO_SPACE_PROFILE_BRIDGE_H_
