// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_SIDEBAR_MAHO_SIDEBAR_PAGE_HANDLER_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_SIDEBAR_MAHO_SIDEBAR_PAGE_HANDLER_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "maho/browser/ui/webui/maho_sidebar/maho_sidebar.mojom.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "services/network/public/cpp/simple_url_loader.h"

class Browser;

class MahoSidebarPageHandler : public maho_sidebar::mojom::PageHandler,
                               public TabStripModelObserver {
 public:
  MahoSidebarPageHandler(
      mojo::PendingReceiver<maho_sidebar::mojom::PageHandler> receiver,
      mojo::PendingRemote<maho_sidebar::mojom::Page> page,
      Browser* browser);

  MahoSidebarPageHandler(const MahoSidebarPageHandler&) = delete;
  MahoSidebarPageHandler& operator=(const MahoSidebarPageHandler&) = delete;

  ~MahoSidebarPageHandler() override;

  // maho_sidebar::mojom::PageHandler:
  void GetTabs(GetTabsCallback callback) override;
  void ActivateTab(int32_t tab_id) override;
  void CloseTab(int32_t tab_id) override;
  void GetSpaces(GetSpacesCallback callback) override;
  void ActivateSpace(const std::string& space_id) override;
  void CreateSpace(const std::string& name,
                   const std::string& color,
                   const std::string& icon,
                   const std::optional<std::string>& profile_id,
                   int32_t insert_after_index) override;
  void DeleteSpace(const std::string& space_id) override;
  void RenameSpace(const std::string& space_id,
                   const std::string& new_name) override;
  void RecolorSpace(const std::string& space_id,
                    const std::string& new_color) override;
  void UpdateSpaceTheme(const std::string& space_id,
                        const std::string& theme_json) override;
  void ChangeSpaceIcon(const std::string& space_id,
                       const std::string& new_icon) override;
  void ReorderSpace(const std::string& space_id,
                    int32_t new_index) override;
  void GetProfiles(GetProfilesCallback callback) override;
  void AssignProfileToSpace(const std::string& space_id,
                            const std::string& profile_id) override;
  void GetATCRules(GetATCRulesCallback callback) override;
  void AddATCRule(const std::string& url_pattern,
                  const std::string& target_space_id,
                  AddATCRuleCallback callback) override;
  void RemoveATCRule(const std::string& rule_id) override;
  void ToggleATCRule(const std::string& rule_id, bool enabled) override;
  void MoveTabToSpace(int32_t tab_id,
                      const std::string& target_space_id,
                      const std::string& section) override;
  void FetchThemeStore(FetchThemeStoreCallback callback) override;

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;

 private:
  std::vector<maho_sidebar::mojom::TabInfoPtr> BuildTabsList();
  void SendTabsUpdate();
  void SendSpacesUpdate();
  void SendProfilesUpdate();
  void SendATCRulesUpdate();

  // Runs on thread pool — no `this` access.
  static std::vector<maho_sidebar::mojom::SpaceInfoPtr> BuildSpacesOnPool();
  static void ActivateSpaceOnPool(const std::string& space_id);
  static std::string CreateSpaceOnPool(const std::string& name,
                                       const std::string& color,
                                       const std::string& icon,
                                       const std::string& profile_id,
                                       int32_t insert_after_index);
  static void DeleteSpaceOnPool(const std::string& space_id);
  static void RenameSpaceOnPool(const std::string& space_id,
                                const std::string& new_name);
  static void RecolorSpaceOnPool(const std::string& space_id,
                                 const std::string& new_color);
  static void UpdateSpaceThemeOnPool(const std::string& space_id,
                                     const std::string& theme_json);
  static void ChangeSpaceIconOnPool(const std::string& space_id,
                                    const std::string& new_icon);
  static void ReorderSpaceOnPool(const std::string& space_id,
                                 int32_t new_index);
  static std::vector<maho_sidebar::mojom::ProfileInfoPtr>
      BuildProfilesOnPool();
  static void AssignProfileOnPool(const std::string& space_id,
                                  const std::string& profile_id);
  static std::vector<maho_sidebar::mojom::ATCRulePtr> BuildATCRulesOnPool();
  static maho_sidebar::mojom::ATCRulePtr AddATCRuleOnPool(
      const std::string& url_pattern,
      const std::string& target_space_id);
  static void RemoveATCRuleOnPool(const std::string& rule_id);
  static void ToggleATCRuleOnPool(const std::string& rule_id, bool enabled);

  void OnSpacesResult(GetSpacesCallback callback,
                      std::vector<maho_sidebar::mojom::SpaceInfoPtr> spaces);
  void OnSpaceMutated();
  void OnSpaceCreated(const std::string& new_space_id);
  void OnSpaceActivated(const std::string& space_id);
  void OnProfilesResult(
      GetProfilesCallback callback,
      std::vector<maho_sidebar::mojom::ProfileInfoPtr> profiles);
  void OnATCRulesResult(
      GetATCRulesCallback callback,
      std::vector<maho_sidebar::mojom::ATCRulePtr> rules);
  void OnATCRuleAdded(
      AddATCRuleCallback callback,
      maho_sidebar::mojom::ATCRulePtr rule);
  void OnThemeStoreFetched(FetchThemeStoreCallback callback,
                           std::optional<std::string> body);

  mojo::Receiver<maho_sidebar::mojom::PageHandler> receiver_;
  mojo::Remote<maho_sidebar::mojom::Page> page_;
  raw_ptr<Browser> browser_;
  std::unique_ptr<network::SimpleURLLoader> theme_store_loader_;
  std::string cached_theme_store_json_;
  base::TimeTicks last_theme_store_fetch_;

  // Must be last member.
  base::WeakPtrFactory<MahoSidebarPageHandler> weak_factory_{this};
};

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_SIDEBAR_MAHO_SIDEBAR_PAGE_HANDLER_H_
