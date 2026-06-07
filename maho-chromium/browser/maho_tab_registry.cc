// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/maho_tab_registry.h"

#include <string>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/memory/raw_ptr.h"
#include "base/values.h"
#include "base/uuid.h"
#include "chrome/browser/lifetime/browser_shutdown.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/tab_list/tab_removed_reason.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/browser/maho_tab_id_helper.h"
#include "maho/third_party/maho/maho_bridge.h"
#include "url/gurl.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_state_adapter.h"

namespace maho {

namespace {

ShellEventObserver* g_tab_registry_shell_event_observer_for_testing = nullptr;

bool IsRegistryEligibleBrowser(BrowserWindowInterface* browser) {
  if (!browser) {
    return false;
  }
  if (browser->GetType() != BrowserWindowInterface::Type::TYPE_NORMAL) {
    return false;
  }
  if (!browser->GetProfile() || browser->GetProfile()->IsOffTheRecord()) {
    return false;
  }
  return true;
}

bool IsCorePinnedTab(const std::string& tab_id) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return false;
  }
  char* json_str = maho_core_get_tab_view_models(core);
  if (!json_str) {
    return false;
  }
  std::string json(json_str);
  maho_string_free(json_str);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    return false;
  }

  for (const auto& item : parsed->GetList()) {
    const auto* dict = item.GetIfDict();
    if (!dict) {
      continue;
    }
    const std::string* id = dict->FindString("id");
    if (id && *id == tab_id) {
      std::optional<bool> is_pinned = dict->FindBool("isPinned");
      return is_pinned.value_or(false);
    }
  }
  return false;
}

void DispatchShellEventLocal(
    const std::string& kind,
    const std::vector<std::pair<std::string, std::string>>& fields) {
  base::DictValue event;
  event.Set("kind", kind);
  for (const auto& [key, value] : fields) {
    event.Set(key, value);
  }
  std::string json;
  base::JSONWriter::Write(event, &json);
  if (g_tab_registry_shell_event_observer_for_testing) {
    g_tab_registry_shell_event_observer_for_testing->OnShellEventDispatched(kind, json);
  }
  MahoCore* core = maho::GetCore();
  if (!core) {
    return;
  }
  maho::core::HandleEvent(core, json.c_str());
  if (auto* bridge = maho::MahoSpaceProfileBridge::GetInstance(); bridge) {
    bridge->NotifyChanged();
  }
}

void DispatchCreateTabForContents(BrowserWindowInterface* browser,
                                  content::WebContents* contents) {
  if (!browser || !contents) {
    return;
  }
  auto* helper = MahoTabIdHelper::FromWebContents(contents);
  if (!helper) {
    return;
  }
  if (helper->has_been_announced()) {
    return;
  }
  const std::string& tab_id = helper->stable_tab_id();
  if (tab_id.empty()) {
    return;
  }

  auto* bridge = MahoSpaceProfileBridge::GetInstance();
  const std::string space_id =
      bridge ? bridge->GetActiveSpaceId(browser) : std::string();
  if (space_id.empty()) {
    return;
  }

  const GURL& visible = contents->GetVisibleURL();
  const GURL& url =
      visible.is_empty() ? contents->GetLastCommittedURL() : visible;

  DispatchShellEventLocal("create_tab", {
      {"tab_id", tab_id},
      {"space_id", space_id},
      {"url", url.is_empty() ? std::string() : url.spec()},
  });
  helper->set_has_been_announced(true);
}

void DispatchCloseTabForContents(content::WebContents* contents) {
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
  DispatchShellEventLocal("close_tab", {{"tab_id", tab_id}});
}

void DispatchTabUrlUpdated(content::WebContents* contents,
                           const std::string& url_spec) {
  if (!contents) return;
  auto* helper = MahoTabIdHelper::FromWebContents(contents);
  if (!helper) return;
  const std::string& tab_id = helper->stable_tab_id();
  if (tab_id.empty()) return;
  DispatchShellEventLocal("tab_url_updated", {
      {"tab_id", tab_id},
      {"url", url_spec},
  });
}

void DispatchTabTitleUpdated(content::WebContents* contents,
                             const std::string& title) {
  if (!contents) return;
  auto* helper = MahoTabIdHelper::FromWebContents(contents);
  if (!helper) return;
  const std::string& tab_id = helper->stable_tab_id();
  if (tab_id.empty()) return;
  DispatchShellEventLocal("tab_title_updated", {
      {"tab_id", tab_id},
      {"title", title},
  });
}

}  // namespace

class MahoTabRegistry::NavObserver : public content::WebContentsObserver {
 public:
  NavObserver(content::WebContents* contents, MahoTabRegistry* registry)
      : content::WebContentsObserver(contents), registry_(registry) {
    DCHECK(registry_);
  }

  ~NavObserver() override = default;

  void DidFinishNavigation(
      content::NavigationHandle* navigation_handle) override {
    if (!navigation_handle) return;
    if (!navigation_handle->IsInPrimaryMainFrame()) return;
    if (!navigation_handle->HasCommitted()) return;
    if (navigation_handle->IsErrorPage()) return;
    if (navigation_handle->IsSameDocument()) return;
    MaybeDispatchHistory();
    MaybeDispatchTabUrl();
    MaybeDispatchTabTitle();
  }

  void TitleWasSet(content::NavigationEntry* /*entry*/) override {
    MaybeDispatchHistory();
    MaybeDispatchTabTitle();
  }

 private:
  void MaybeDispatchHistory() {
    auto* contents = web_contents();
    if (!contents) return;
    const GURL& url = contents->GetLastCommittedURL();
    if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS()) return;
    const std::string url_spec = url.spec();
    const std::string title = base::UTF16ToUTF8(contents->GetTitle());
    if (url_spec == last_url_ && title == last_title_) {
      return;
    }
    last_url_ = url_spec;
    last_title_ = title;
    MahoCore* core = maho::GetCore();
    if (!core) return;
    maho_core_add_history_entry(core, url_spec.c_str(), title.c_str());
    std::string visit_id = base::Uuid::GenerateRandomV4().AsLowercaseString();
    maho::core::InsertBrowseSession(core, visit_id, url_spec, title);
  }

  void MaybeDispatchTabUrl() {
    auto* contents = web_contents();
    if (!contents) return;
    const GURL& url = contents->GetLastCommittedURL();
    if (!url.is_valid()) return;
    auto* helper = MahoTabIdHelper::FromWebContents(contents);
    if (!helper) return;
    const std::string& tab_id = helper->stable_tab_id();
    if (tab_id.empty()) return;
    auto& cache = registry_->GetOrCreateDispatchCache(tab_id);
    const std::string url_spec = url.spec();
    if (url_spec == cache.url) return;
    cache.url = url_spec;
    DispatchTabUrlUpdated(contents, url_spec);
  }

  void MaybeDispatchTabTitle() {
    auto* contents = web_contents();
    if (!contents) return;
    auto* helper = MahoTabIdHelper::FromWebContents(contents);
    if (!helper) return;
    const std::string& tab_id = helper->stable_tab_id();
    if (tab_id.empty()) return;
    auto& cache = registry_->GetOrCreateDispatchCache(tab_id);
    const std::string title = base::UTF16ToUTF8(contents->GetTitle());
    if (title == cache.title) return;
    cache.title = title;
    DispatchTabTitleUpdated(contents, title);
  }

  std::string last_url_;
  std::string last_title_;
  raw_ptr<MahoTabRegistry> registry_;
};

class MahoTabRegistry::StripObserver : public TabStripModelObserver {
 public:
  StripObserver(MahoTabRegistry* registry, BrowserWindowInterface* browser, TabStripModel* model)
      : registry_(registry), browser_(browser), model_(model) {
    DCHECK(registry_);
    DCHECK(browser_);
    DCHECK(model_);
    model_->AddObserver(this);
    Bootstrap();
  }

  StripObserver(const StripObserver&) = delete;
  StripObserver& operator=(const StripObserver&) = delete;

  ~StripObserver() override {
    if (model_) {
      model_->RemoveObserver(this);
    }
    nav_observers_.clear();
  }

  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override {
    if (change.type() == TabStripModelChange::kInserted) {
      const auto* insert = change.GetInsert();
      if (!insert) {
        return;
      }
      for (const auto& inserted : insert->contents) {
        DispatchCreateTabForContents(browser_, inserted.contents);
        AttachNavObserver(inserted.contents);
      }
    } else if (change.type() == TabStripModelChange::kRemoved) {
      // During app shutdown, Chromium destroys every WebContents which fires
      // kRemoved for every tab. We must NOT dispatch close_tab in that path —
      // doing so would erase every tab from the Rust tab_manager just before
      // SaveState runs, persisting an empty tab list to LMDB and silently
      // losing the entire session on next launch. Tabs are intentionally kept
      // in tab_manager so SaveState can serialize them for restoration.
      if (browser_shutdown::IsTryingToQuit()) {
        const auto* remove = change.GetRemove();
        if (remove) {
          for (const auto& removed : remove->contents) {
            nav_observers_.erase(removed.contents);
          }
        }
        return;
      }
      const auto* remove = change.GetRemove();
      if (!remove) {
        return;
      }
      for (const auto& removed : remove->contents) {
        nav_observers_.erase(removed.contents);
        if (!TabRemoveReasonUtils::WillDeleteTab(removed.remove_reason)) {
          continue;
        }
        // Erase dispatch cache before the tab is gone for good.
        if (auto* helper = MahoTabIdHelper::FromWebContents(removed.contents);
            helper && !helper->stable_tab_id().empty()) {
          const std::string& tab_id = helper->stable_tab_id();
          // If this tab is already known to Rust core as pinned, the sidebar/context-menu
          // close path has already dispatched close_tab (which suspended it). Suppress the
          // duplicate dispatch from kRemoved so it doesn't immediately escalate to 2nd-close.
          if (IsCorePinnedTab(tab_id)) {
            registry_->EraseDispatchCache(tab_id);
            continue;
          }
          registry_->EraseDispatchCache(tab_id);
        }
        DispatchCloseTabForContents(removed.contents);
      }
    } else if (change.type() == TabStripModelChange::kReplaced) {
      const auto* replace = change.GetReplace();
      if (!replace) {
        return;
      }
      // Tab discard / prerendering swap-in: TabStripModel keeps the same
      // logical tab but swaps the WebContents instance underneath. The new
      // WebContents has a fresh MahoTabIdHelper with a new UUID, which would
      // re-orphan the Rust core's record. Carry the old UUID over so identity
      // is preserved across the swap, then move the NavObserver to the new
      // contents.
      content::WebContents* old_contents = replace->old_contents;
      content::WebContents* new_contents = replace->new_contents;
      if (old_contents && new_contents && old_contents != new_contents) {
        auto* old_helper = MahoTabIdHelper::FromWebContents(old_contents);
        auto* new_helper = MahoTabIdHelper::FromWebContents(new_contents);
        std::string old_id;
        if (old_helper) {
          old_id = old_helper->stable_tab_id();
        }
        std::string new_id_pre_swap;
        if (new_helper) {
          new_id_pre_swap = new_helper->stable_tab_id();
        }
        if (old_helper && new_helper &&
            !old_helper->stable_tab_id().empty() &&
            old_helper->stable_tab_id() != new_helper->stable_tab_id()) {
          new_helper->SetRestoredTabId(old_helper->stable_tab_id());
          new_helper->set_has_been_announced(old_helper->has_been_announced());
        }
        if (!old_id.empty() && !new_id_pre_swap.empty() && old_id != new_id_pre_swap) {
          // Migrate the cache from the new helper's pre-swap UUID to the old (now-canonical) UUID.
          registry_->MigrateDispatchCache(new_id_pre_swap, old_id);
        }
        nav_observers_.erase(old_contents);
        AttachNavObserver(new_contents);
      }
    }
  }

 private:
  void Bootstrap() {
    for (int i = 0; i < model_->count(); ++i) {
      content::WebContents* contents = model_->GetWebContentsAt(i);
      DispatchCreateTabForContents(browser_, contents);
      AttachNavObserver(contents);
    }
  }

  void AttachNavObserver(content::WebContents* contents) {
    if (!contents) return;
    if (nav_observers_.contains(contents)) return;
    nav_observers_.emplace(contents, std::make_unique<NavObserver>(contents, registry_));
  }

  raw_ptr<MahoTabRegistry> registry_;
  raw_ptr<BrowserWindowInterface> browser_;
  raw_ptr<TabStripModel> model_;
  base::flat_map<content::WebContents*, std::unique_ptr<NavObserver>> nav_observers_;
};

// static
MahoTabRegistry* MahoTabRegistry::Get() {
  static base::NoDestructor<MahoTabRegistry> instance;
  return instance.get();
}

MahoTabRegistry::MahoTabRegistry() {
  browser_collection_observation_.Observe(GlobalBrowserCollection::GetInstance());
}

MahoTabRegistry::~MahoTabRegistry() = default;

void MahoTabRegistry::OnBrowserCreated(BrowserWindowInterface* browser) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!browser) {
    return;
  }
  if (!IsRegistryEligibleBrowser(browser)) {
    return;
  }
  AttachStripObserverFor(browser);
}

void MahoTabRegistry::OnBrowserClosed(BrowserWindowInterface* browser) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!browser) {
    return;
  }
  DetachStripObserverFor(browser);
}

void MahoTabRegistry::AttachStripObserverFor(BrowserWindowInterface* browser) {
  if (!browser || strip_observers_.contains(browser)) {
    return;
  }
  TabStripModel* model = browser->GetTabStripModel();
  if (!model) {
    return;
  }
  strip_observers_.emplace(
      browser, std::make_unique<StripObserver>(this, browser, model));
}

void MahoTabRegistry::DetachStripObserverFor(BrowserWindowInterface* browser) {
  if (!browser) {
    return;
  }
  strip_observers_.erase(browser);
}

LastDispatchedTabState& MahoTabRegistry::GetOrCreateDispatchCache(const std::string& tab_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return dispatch_cache_[tab_id];
}

void MahoTabRegistry::EraseDispatchCache(const std::string& tab_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  dispatch_cache_.erase(tab_id);
}

void MahoTabRegistry::MigrateDispatchCache(const std::string& from_tab_id,
                                           const std::string& to_tab_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (from_tab_id == to_tab_id) {
    return;
  }
  auto it = dispatch_cache_.find(from_tab_id);
  if (it != dispatch_cache_.end()) {
    dispatch_cache_[to_tab_id] = std::move(it->second);
    dispatch_cache_.erase(it);
  }
}

// static
std::set<std::string> MahoTabRegistry::GetLiveTabIdsForProfile(Profile* profile) {
  std::set<std::string> live_tab_ids;
  auto* registry = Get();
  if (!registry) {
    return live_tab_ids;
  }
  DCHECK_CALLED_ON_VALID_SEQUENCE(registry->sequence_checker_);
  for (const auto& [browser, observer] : registry->strip_observers_) {
    if (browser->GetProfile() == profile) {
      TabStripModel* strip = browser->GetTabStripModel();
      if (strip) {
        for (int i = 0; i < strip->count(); ++i) {
          if (content::WebContents* wc = strip->GetWebContentsAt(i)) {
            if (auto* helper = MahoTabIdHelper::FromWebContents(wc)) {
              live_tab_ids.insert(helper->stable_tab_id());
            }
          }
        }
      }
    }
  }
  return live_tab_ids;
}

void SetTabRegistryShellEventObserverForTesting(ShellEventObserver* observer) {
  g_tab_registry_shell_event_observer_for_testing = observer;
}

}  // namespace maho
