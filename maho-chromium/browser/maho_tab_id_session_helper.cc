// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/maho_tab_id_session_helper.h"

#include <set>
#include <string>
#include <vector>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/json/string_escape.h"
#include "base/logging.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/session_restore.h"
#include "chrome/browser/sessions/session_service.h"
#include "chrome/browser/sessions/session_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "components/sessions/content/session_tab_helper.h"
#include "content/public/browser/web_contents.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/browser/maho_tab_id_helper.h"
#include "maho/browser/maho_tab_registry.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_container_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_view.h"
#include "maho/browser/ui/views/split_view/maho_split_view_controller.h"
#include "maho/third_party/maho/maho_bridge.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "ui/views/view_utils.h"

namespace maho {

namespace {

const char kUserDataKey[] = "MahoTabIdSessionHelperUserDataKey";

struct MigrationCoreTab {
  std::string id;
  std::string url;
};

std::vector<MigrationCoreTab> GetCoreTabsForSpace(MahoCore* core, const std::string& space_id, bool pinned) {
  std::vector<MigrationCoreTab> results;
  const std::string space_id_json = base::GetQuotedJSONString(space_id);
  char* json_str = nullptr;
  if (pinned) {
    json_str = maho_core_get_pinned_tabs(core, space_id_json.c_str());
  } else {
    json_str = maho_core_get_space_tabs(core, space_id_json.c_str(), nullptr);
  }
  if (!json_str) {
    return results;
  }
  std::string json(json_str);
  maho_string_free(json_str);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    return results;
  }

  for (const auto& item : parsed->GetList()) {
    const auto* dict = item.GetIfDict();
    if (!dict) {
      continue;
    }
    const std::string* id = dict->FindString("id");
    const std::string* url = dict->FindString("url");
    if (id && url) {
      results.push_back({*id, *url});
    }
  }
  return results;
}

void MigrateTabIdentity(MahoCore* core,
                        content::WebContents* wc,
                        const std::string& fresh_id,
                        const std::string& old_id,
                        const std::string& space_id,
                        const std::string& url,
                        int64_t window_id) {
  // 1. Send close_tab event for fresh_id
  base::DictValue close_event;
  close_event.Set("kind", "close_tab");
  close_event.Set("tab_id", fresh_id);
  std::string close_json;
  base::JSONWriter::Write(close_event, &close_json);
  char* r1 = maho_core_dispatch_shell_event(core, close_json.c_str());
  if (r1) maho_string_free(r1);

  // 2. Change C++ helper's ID
  auto* helper = MahoTabIdHelper::FromWebContents(wc);
  if (helper) {
    helper->SetRestoredTabId(old_id);
  }

  // 3. Send create_tab event for old_id
  base::DictValue create_event;
  create_event.Set("kind", "create_tab");
  create_event.Set("space_id", space_id);
  create_event.Set("tab_id", old_id);
  if (!url.empty()) {
    create_event.Set("url", url);
  }
  create_event.Set("window_id", static_cast<int>(window_id));
  std::string create_json;
  base::JSONWriter::Write(create_event, &create_json);
  char* r2 = maho_core_dispatch_shell_event(core, create_json.c_str());
  if (r2) maho_string_free(r2);
}

}  // namespace

// static
void MahoTabIdSessionHelper::RegisterProfilePrefs(PrefRegistrySimple* registry) {
  registry->RegisterBooleanPref("maho.session_migration_done", false);
}

// static
MahoTabIdSessionHelper* MahoTabIdSessionHelper::GetForProfile(Profile* profile) {
  auto* helper = static_cast<MahoTabIdSessionHelper*>(profile->GetUserData(kUserDataKey));
  if (!helper) {
    auto new_helper = std::make_unique<MahoTabIdSessionHelper>(profile);
    helper = new_helper.get();
    profile->SetUserData(kUserDataKey, std::move(new_helper));
  }
  return helper;
}

MahoTabIdSessionHelper::MahoTabIdSessionHelper(Profile* profile)
    : profile_(profile) {
  tracker_ = std::make_unique<BrowserTabStripTracker>(this, this);
  tracker_->Init();

  session_restored_subscription_ =
      SessionRestore::RegisterOnSessionRestoredCallback(
          base::BindRepeating(&MahoTabIdSessionHelper::OnSessionRestoreFinished,
                              base::Unretained(this)));
}

MahoTabIdSessionHelper::~MahoTabIdSessionHelper() = default;

void MahoTabIdSessionHelper::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  if (change.type() == TabStripModelChange::kInserted) {
    const auto* insert = change.GetInsert();
    if (insert) {
      for (const auto& inserted : insert->contents) {
        SaveTabIdToSession(inserted.contents);
      }
    }
  } else if (change.type() == TabStripModelChange::kReplaced) {
    const auto* replace = change.GetReplace();
    if (replace) {
      SaveTabIdToSession(replace->new_contents);
    }
  }
}

bool MahoTabIdSessionHelper::ShouldTrackBrowser(BrowserWindowInterface* browser) {
  return browser && browser->GetProfile() == profile_;
}

void MahoTabIdSessionHelper::SaveTabIdToSession(content::WebContents* contents) {
  if (!contents) {
    return;
  }
  auto* helper = MahoTabIdHelper::FromWebContents(contents);
  if (!helper) {
    return;
  }
  const std::string& tab_id = helper->stable_tab_id();
  if (tab_id.empty()) {
    return;
  }

  SessionService* session_service =
      SessionServiceFactory::GetForProfileIfExisting(profile_);
  if (!session_service) {
    return;
  }

  SessionID window_id = sessions::SessionTabHelper::IdForWindowContainingTab(contents);
  SessionID tab_id_session = sessions::SessionTabHelper::IdForTab(contents);

  if (window_id.is_valid() && tab_id_session.is_valid()) {
    session_service->AddTabExtraData(window_id, tab_id_session,
                                     MahoTabIdHelper::kExtraDataKey,
                                     tab_id);
  }
}

void MahoTabIdSessionHelper::OnSessionRestoreFinished(Profile* profile, int num_tabs_restored) {
  if (profile != profile_) {
    return;
  }
  LOG(INFO) << "Session restore finished. Restored " << num_tabs_restored << " tabs.";

  MahoCore* core = maho::GetCore();
  if (!core) {
    return;
  }

  PrefService* prefs = profile->GetPrefs();
  if (prefs && !prefs->GetBoolean("maho.session_migration_done")) {
    PerformSessionMigration(profile, core);
    prefs->SetBoolean("maho.session_migration_done", true);
  }

  DCHECK(MahoTabIdHelper::PopPendingRestoredTabIdForTesting().empty())
      << "Maho tab ID pending queue not fully drained after session restore";

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

  // Session restore has now applied Chromium's split-tab commands to the live
  // TabStripModel. The sidebar's initial render can run before those land,
  // leaving restored (loaded) split panes ungrouped until the next rebuild.
  // Force one grouping pass now so GroupSplitTabs sees the split data.
  // (Suspended panes absent from the model still need the core-persistence fix.)
  if (auto* collection = GlobalBrowserCollection::GetInstance()) {
    collection->ForEach([profile](BrowserWindowInterface* browser_interface) {
      Browser* browser = static_cast<Browser*>(browser_interface);
      if (!browser || browser->GetProfile() != profile) {
        return true;
      }
      BrowserView* browser_view =
          BrowserView::GetBrowserViewForBrowser(browser);
      if (!browser_view) {
        return true;
      }
      auto* container = views::AsViewClass<MahoSidebarContainerView>(
          browser_view->maho_sidebar_container());
      if (!container) {
        return true;
      }
      if (auto* sidebar_view =
              views::AsViewClass<MahoSidebarView>(container->sidebar_view())) {
        sidebar_view->ScheduleRefreshAll();
      }
      return true;
    });
  }
}

void MahoTabIdSessionHelper::PerformSessionMigration(Profile* profile, MahoCore* core) {
  char* spaces_json_str = maho_core_get_space_view_models(core);
  if (!spaces_json_str) {
    return;
  }
  std::string spaces_json(spaces_json_str);
  maho_string_free(spaces_json_str);

  std::optional<base::Value> parsed_spaces =
      base::JSONReader::Read(spaces_json, base::JSON_PARSE_RFC);
  if (!parsed_spaces || !parsed_spaces->is_list()) {
    return;
  }

  std::vector<std::string> space_ids;
  for (const auto& item : parsed_spaces->GetList()) {
    const auto* dict = item.GetIfDict();
    if (dict) {
      const std::string* id = dict->FindString("id");
      if (id) {
        space_ids.push_back(*id);
      }
    }
  }

  int migrated_count = 0;
  for (const std::string& space_id : space_ids) {
    std::vector<MigrationCoreTab> core_pinned = GetCoreTabsForSpace(core, space_id, true);
    std::vector<MigrationCoreTab> core_standard = GetCoreTabsForSpace(core, space_id, false);

    Browser* target_browser = nullptr;
    auto* collection = GlobalBrowserCollection::GetInstance();
    if (collection) {
      collection->ForEach([profile, space_id, &target_browser](BrowserWindowInterface* browser_interface) {
        Browser* browser = static_cast<Browser*>(browser_interface);
        if (browser && browser->GetProfile() == profile) {
          std::string browser_space_id =
              maho::MahoSpaceProfileBridge::GetInstance()
                  ? maho::MahoSpaceProfileBridge::GetInstance()->GetActiveSpaceId(browser)
                  : std::string();
          if (browser_space_id == space_id) {
            target_browser = browser;
            return false;
          }
        }
        return true;
      });
    }
    if (!target_browser) {
      continue;
    }

    TabStripModel* tab_strip_model = target_browser->GetTabStripModel();
    if (!tab_strip_model) {
      continue;
    }

    std::vector<content::WebContents*> chromium_pinned;
    std::vector<content::WebContents*> chromium_standard;
    for (int i = 0; i < tab_strip_model->count(); ++i) {
      if (content::WebContents* contents = tab_strip_model->GetWebContentsAt(i)) {
        if (tab_strip_model->IsTabPinned(i)) {
          chromium_pinned.push_back(contents);
        } else {
          chromium_standard.push_back(contents);
        }
      }
    }

    // Match pinned tabs
    for (size_t i = 0; i < chromium_pinned.size() && i < core_pinned.size(); ++i) {
      content::WebContents* wc = chromium_pinned[i];
      const auto& core_tab = core_pinned[i];
      auto* helper = MahoTabIdHelper::FromWebContents(wc);
      if (!helper) {
        continue;
      }
      const std::string& fresh_id = helper->stable_tab_id();
      const GURL& visible = wc->GetVisibleURL();
      const GURL& url = visible.is_empty() ? wc->GetLastCommittedURL() : visible;
      std::string wc_url = url.is_empty() ? std::string() : url.spec();

      if (wc_url == core_tab.url && fresh_id != core_tab.id) {
        MigrateTabIdentity(core, wc, fresh_id, core_tab.id, space_id, wc_url, target_browser->GetSessionID().id());
        migrated_count++;
      }
    }

    // Match standard tabs
    for (size_t i = 0; i < chromium_standard.size() && i < core_standard.size(); ++i) {
      content::WebContents* wc = chromium_standard[i];
      const auto& core_tab = core_standard[i];
      auto* helper = MahoTabIdHelper::FromWebContents(wc);
      if (!helper) {
        continue;
      }
      const std::string& fresh_id = helper->stable_tab_id();
      const GURL& visible = wc->GetVisibleURL();
      const GURL& url = visible.is_empty() ? wc->GetLastCommittedURL() : visible;
      std::string wc_url = url.is_empty() ? std::string() : url.spec();

      if (wc_url == core_tab.url && fresh_id != core_tab.id) {
        MigrateTabIdentity(core, wc, fresh_id, core_tab.id, space_id, wc_url, target_browser->GetSessionID().id());
        migrated_count++;
      }
    }

    // Restore split views for the browser window if any were saved.
    MahoSplitViewController::RestoreForWindow(target_browser);
  }
  LOG(INFO) << "One-shot session restore migration completed: migrated " << migrated_count << " tabs.";
}

}  // namespace maho
