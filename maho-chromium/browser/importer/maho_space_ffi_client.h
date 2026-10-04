// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_IMPORTER_MAHO_SPACE_FFI_CLIENT_H_
#define MAHO_BROWSER_IMPORTER_MAHO_SPACE_FFI_CLIENT_H_

#include <string>
#include <map>
#include <vector>
#include "base/sequence_checker.h"
#include "base/values.h"

namespace maho {

class MahoSpaceFFIClient {
 public:
  static MahoSpaceFFIClient* GetInstance();

  // Replaces the singleton with |instance| for the duration of a test.
  // Pass nullptr to restore the default singleton.
  // NOT thread-safe; must be called before any test that uses GetInstance().
  static void SetInstanceForTesting(MahoSpaceFFIClient* instance);

  MahoSpaceFFIClient();
  virtual ~MahoSpaceFFIClient();

  MahoSpaceFFIClient(const MahoSpaceFFIClient&) = delete;
  MahoSpaceFFIClient& operator=(const MahoSpaceFFIClient&) = delete;

  virtual std::string CreateSpace(const std::string& name,
                                  const std::string& theme_color_hex,
                                  const std::string& icon);

  virtual std::string CreateTab(const std::string& space_id,
                                const std::string& url,
                                const std::string& title = "",
                                const std::string& parent_folder_id = "");

  virtual void PinTab(const std::string& tab_id);
  virtual void FavoriteTab(const std::string& tab_id);

  virtual std::string CreateFolder(const std::string& space_id,
                                   const std::string& name,
                                   const std::string& parent_folder_id = "");

  virtual void MoveTabToFolder(const std::string& tab_id,
                               const std::string& folder_id);
  virtual void UpdateSpaceConfig(const std::string& space_id,
                                 const std::string& changes_json);
  virtual void ActivateSpace(const std::string& space_id);

  // Sets the icon for an existing space.
  // Dispatches `kind: "update_space_config"` with {spaceId, icon}; maho-core
  // canonicalizes the value (icon names become emoji).
  // MUST be called on UI thread.
  virtual void SetSpaceIcon(const std::string& space_id,
                            const std::string& icon);

  // Moves a tab to a target space (cross-space tab transfer).
  // Dispatches `kind: "move_tab_to_space"` event.
  // |section| is optional ("pinned" or "today"); empty means default placement.
  // MUST be called on UI thread.
  virtual void MoveTabToSpace(const std::string& tab_id,
                              const std::string& target_space_id,
                              const std::string& section = "");

  void ResetFolderDepths() { folder_depths_.clear(); }

  // Returns the recorded depths for all created folders, in creation order.
  // Only populated for the real implementation; fakes track their own.
  std::vector<int> GetRecordedFolderDepths() const;

 private:
  std::string DispatchEvent(const base::DictValue& event);

  std::map<std::string, int> folder_depths_;

  // maybe_unused: only touched by DCHECK_CALLED_ON_VALID_SEQUENCE (compiled out
  // when dcheck_always_on=false), else -Wunused-private-field fails release.
  [[maybe_unused]] base::SequenceChecker sequence_checker_;
};

}  // namespace maho

#endif  // MAHO_BROWSER_IMPORTER_MAHO_SPACE_FFI_CLIENT_H_
