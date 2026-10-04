// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/maho_tab_registry.h"
#include "maho/browser/maho_private_context_policy.h"

#include <string>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/timer/elapsed_timer.h"
#include "base/memory/raw_ptr.h"
#include "base/values.h"
#include "chrome/browser/lifetime/browser_shutdown.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/tab_list/tab_removed_reason.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "base/strings/utf_string_conversions.h"
#include "base/strings/stringprintf.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/browser/maho_tab_id_helper.h"
#include "maho/third_party/maho/maho_bridge.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "url/gurl.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_state_adapter.h"  // nogncheck
#include "base/base64.h"
#include "components/favicon/content/content_favicon_driver.h"
#include "components/favicon/core/favicon_driver.h"
#include "ui/gfx/image/image.h"

namespace maho {

namespace {

ShellEventObserver* g_tab_registry_shell_event_observer_for_testing = nullptr;

// Active wake identity (see ScopedMahoTabWakeId in the header). UI-thread only.
std::string& GetActiveWakeTabId() {
  static base::NoDestructor<std::string> id;
  return *id;
}

bool IsRegistryEligibleBrowser(BrowserWindowInterface* browser) {
  if (!browser) {
    return false;
  }
  if (browser->GetType() != BrowserWindowInterface::Type::TYPE_NORMAL) {
    return false;
  }
  Profile* profile = browser->GetProfile();
  if (!profile) {
    return false;
  }
  MahoPrivateContextClass cls = MahoClassifyProfile(profile);
  return cls == MahoPrivateContextClass::kRegular;
}

// Fires the on_many_tabs routine event when a strip's live tab count first
// reaches the threshold, and re-arms once it drops back below. Debounced to a
// single fire per crossing so session restore's burst of inserts (and normal
// churn around the boundary) cannot repeatedly re-trigger it.
constexpr int kManyTabsThreshold = 20;

namespace {

std::map<TabStripModel*, bool>& GetArmedMap() {
  static base::NoDestructor<std::map<TabStripModel*, bool>> armed_map;
  return *armed_map;
}

} // namespace

void MaybeFireManyTabsEvent(TabStripModel* model, int tab_count) {
  auto& armed_map = GetArmedMap();
  auto it = armed_map.find(model);
  bool armed = (it == armed_map.end()) ? true : it->second;

  if (tab_count >= kManyTabsThreshold) {
    if (!armed) {
      return;
    }
    armed_map[model] = false;
    if (maho::GetCore()) {
      const uint64_t generation = maho::GetCoreGeneration();
      maho::PostCoreClosure(FROM_HERE, base::BindOnce(
          [](uint64_t generation, int count) {
            if (generation == maho::GetCoreGeneration()) {
              if (MahoCore* current = maho::GetCore()) {
                maho_routines_fire_event(
                    current, base::StringPrintf(
                        "{\"kind\":\"on_many_tabs\",\"count\":%d}", count).c_str());
              }
            }
          }, generation, tab_count));
    }
  } else {
    armed_map[model] = true;
  }
}

void CleanUpManyTabsEventState(TabStripModel* model) {
  GetArmedMap().erase(model);
}

void DispatchShellEventLocalWithCore(MahoCore* core,
                                     const std::string& kind,
                                     base::DictValue event) {
  event.Set("kind", kind);
  std::string json;
  base::JSONWriter::Write(event, &json);
  if (g_tab_registry_shell_event_observer_for_testing) {
    g_tab_registry_shell_event_observer_for_testing->OnShellEventDispatched(kind, json);
  }
  if (!core) {
    return;
  }
  const bool is_structural = !(kind == "tab_title_updated" ||
                                kind == "tab_url_updated" ||
                                kind == "tab_favicon_updated" ||
                                kind == "tab_loading_state_changed" ||
                                kind == "update_tab_scroll_position" ||
                                kind == "update_tab_preview");
  maho::DispatchCoreEvent(
      std::move(json),
      base::BindOnce([](bool is_structural, maho::CoreEventResult result) {
        if (result.status != maho::CoreEventStatus::kApplied) {
          LOG(WARNING) << "Tab core event did not apply: "
                       << static_cast<int>(result.status);
          return;
        }
        InvalidateSidebarCoreCacheForUpdatesJson(result.updates_json);
        maho::MahoSpaceProfileBridge::GetInstance()->NotifyChanged(is_structural);
      }, is_structural));
}

void DispatchShellEventLocal(const std::string& kind, base::DictValue event) {
  DispatchShellEventLocalWithCore(maho::GetCore(), kind, std::move(event));
}

void DispatchShellEventLocal(
    const std::string& kind,
    const std::vector<std::pair<std::string, std::string>>& fields) {
  base::DictValue event;
  for (const auto& [key, value] : fields) {
    event.Set(key, value);
  }
  DispatchShellEventLocal(kind, std::move(event));
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
  // Wake-scoped identity adoption: this kInserted announcement fires
  // synchronously inside WakeSuspendedTab's Navigate(), while the helper still
  // carries the fresh UUID minted by TabHelpers. Rebinding here — before the
  // create_tab dispatch — makes maho-core resolve the EXISTING suspended tab
  // (create_tab_with_id no-op + restore_to_active) instead of minting a new
  // Normal tab under the throwaway UUID. Consumed at most once per scope.
  {
    std::string& wake_id = GetActiveWakeTabId();
    if (!wake_id.empty()) {
      if (helper->stable_tab_id() != wake_id) {
        helper->SetRestoredTabId(wake_id);
      }
      wake_id.clear();
    }
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
  if (space_id.empty() || !bridge->IsSpaceRegistered(space_id)) {
    return;
  }
  if (!maho::GetCore()) {
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

ScopedMahoTabWakeId::ScopedMahoTabWakeId(const std::string& tab_id) {
  GetActiveWakeTabId() = tab_id;
}

ScopedMahoTabWakeId::~ScopedMahoTabWakeId() {
  GetActiveWakeTabId().clear();
}

bool IsCorePinnedTab(const std::string& tab_id) {
  const auto facts = maho::GetCachedCoreTabFacts(tab_id);
  return facts && facts->pinned;
}

bool IsCoreCloseProtectedTab(const std::string& tab_id) {
  const auto facts = maho::GetCachedCoreTabFacts(tab_id);
  return facts && facts->close_protected;
}

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
    MaybeDispatchTabFavicon();
  }

  void TitleWasSet(content::NavigationEntry* /*entry*/) override {
    MaybeDispatchHistory();
    MaybeDispatchTabTitle();
  }

  void DidUpdateFaviconURL(
      content::RenderFrameHost* render_frame_host,
      const std::vector<blink::mojom::FaviconURLPtr>& candidates,
      blink::mojom::FaviconUpdateReason reason) override {
    MaybeDispatchTabFavicon();
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
    const uint64_t generation = maho::GetCoreGeneration();
    maho::PostCoreClosure(FROM_HERE, base::BindOnce(
        [](uint64_t generation, std::string url, std::string title) {
          if (generation == maho::GetCoreGeneration()) {
            if (MahoCore* current = maho::GetCore()) {
              maho_core_add_history_entry(current, url.c_str(), title.c_str());
            }
          }
        }, generation, url_spec, title));
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

  void MaybeDispatchTabFavicon() {
    auto* contents = web_contents();
    if (!contents) return;
    auto* helper = MahoTabIdHelper::FromWebContents(contents);
    if (!helper) return;
    const std::string& tab_id = helper->stable_tab_id();
    if (tab_id.empty()) return;

    favicon::FaviconDriver* favicon_driver =
        favicon::ContentFaviconDriver::FromWebContents(contents);
    if (!favicon_driver) return;

    gfx::Image favicon_image = favicon_driver->GetFavicon();
    if (favicon_image.IsEmpty()) return;

    scoped_refptr<base::RefCountedMemory> favicon_bytes =
        favicon_image.As1xPNGBytes();
    if (!favicon_bytes || favicon_bytes->size() == 0u) return;

    std::string base64_data = base::Base64Encode(base::span(*favicon_bytes));

    auto& cache = registry_->GetOrCreateDispatchCache(tab_id);
    if (base64_data == cache.favicon_b64) return;
    cache.favicon_b64 = base64_data;

    base::DictValue favicon_obj;
    favicon_obj.Set("data", base64_data);
    favicon_obj.Set("width", favicon_image.Width());
    favicon_obj.Set("height", favicon_image.Height());
    favicon_obj.Set("format", "png");

    base::DictValue event;
    event.Set("tab_id", tab_id);
    event.Set("favicon", std::move(favicon_obj));

    // Route through the local dispatch path so test observers and
    // NotifyChanged fire; the Dict overload carries the nested favicon object.
    DispatchShellEventLocal("tab_favicon_updated", std::move(event));
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
    if (model_ && !model_destroyed_) {
      model_->RemoveObserver(this);
      CleanUpManyTabsEventState(model_);
    }
    nav_observers_.clear();
  }

  // TabStripModelObserver:
  // Fired from ~TabStripModel, after TabStripModelObserver::ModelDestroyed has
  // already detached this observer from the model.
  //
  // The registry keys strip observers by browser and dereferences the browser's
  // TabStripModel on every ReannounceUnannouncedTabs(). A model can be destroyed
  // while its browser is still recorded here, and the browser's own
  // OnBrowserClosed notification is not guaranteed to arrive first (it is one of
  // several observers of GlobalBrowserCollection). A later reannounce -- every
  // MahoSpaceProfileBridge::SetActiveSpaceId() triggers one -- then walks the
  // freed TabStripModel and crashes on model->count(). Drop the entry here, at
  // the only point where the model's death is observable.
  void OnTabStripModelDestroyed(TabStripModel* tab_strip_model) override {
    model_destroyed_ = true;
    CleanUpManyTabsEventState(model_);
    // Erasing the registry entry destroys `this`; touch no members afterwards.
    if (registry_) {
      registry_->DetachStripObserverForDestroyedModel(browser_);
    }
  }

  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override {
    base::ElapsedTimer maho_perf_t;
    const char* ch = "?";
    switch (change.type()) {
      case TabStripModelChange::kSelectionOnly: ch = "SelectionOnly"; break;
      case TabStripModelChange::kInserted: ch = "Inserted"; break;
      case TabStripModelChange::kRemoved: ch = "Removed"; break;
      case TabStripModelChange::kMoved: ch = "Moved"; break;
      case TabStripModelChange::kReplaced: ch = "Replaced"; break;
    }
    DVLOG(1) << "[MAHO_PERF] TabRegistry::OnTabStripModelChanged type=" << ch;
    if (change.type() == TabStripModelChange::kInserted) {
      const auto* insert = change.GetInsert();
      if (!insert) {
        return;
      }
      for (const auto& inserted : insert->contents) {
        DispatchCreateTabForContents(browser_, inserted.contents);
        AttachNavObserver(inserted.contents);
      }
      MaybeFireManyTabsEvent(tab_strip_model, tab_strip_model->count());
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
          if (registry_->ConsumeArchiveRemovalSuppression(tab_id)) {
            DispatchShellEventLocal("archive_tab_by_id", {{"tab_id", tab_id}});
            registry_->EraseDispatchCache(tab_id);
            continue;
          }
          registry_->EraseDispatchCache(tab_id);
        }
        DispatchCloseTabForContents(removed.contents);
      }
      MaybeFireManyTabsEvent(tab_strip_model, tab_strip_model->count());
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
    DVLOG(1) << "[MAHO_PERF] TabRegistry::OnTabStripModelChanged END "
             << maho_perf_t.Elapsed().InMillisecondsF() << "ms";
  }

  void OnTabCloseCancelled(const tabs::TabInterface* tab) override {
    if (!tab) {
      return;
    }
    content::WebContents* contents = tab->GetContents();
    if (!contents) {
      return;
    }
    if (auto* helper = MahoTabIdHelper::FromWebContents(contents);
        helper && !helper->stable_tab_id().empty()) {
      registry_->EraseArchiveRemovalSuppression(helper->stable_tab_id());
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
  // Set from OnTabStripModelDestroyed(), which runs while the model is being
  // torn down. The destructor must then skip both RemoveObserver() and any
  // CleanUpManyTabsEventState() work, because the base class already detached
  // this observer and the model may not be touched again.
  bool model_destroyed_ = false;
  base::flat_map<content::WebContents*, std::unique_ptr<NavObserver>> nav_observers_;
};

// static
MahoTabRegistry* MahoTabRegistry::Get() {
  static base::NoDestructor<MahoTabRegistry> instance;
  return instance.get();
}

MahoTabRegistry::MahoTabRegistry() {
  browser_collection_observation_.Observe(GlobalBrowserCollection::GetInstance());
  core_ready_subscription_ = AddCoreReadyCallback(base::BindRepeating(
      [](MahoTabRegistry* registry) {
        registry->ReannounceUnannouncedTabs();
      },
      base::Unretained(this)));
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

void MahoTabRegistry::DetachStripObserverForDestroyedModel(
    BrowserWindowInterface* browser) {
  if (!browser) {
    return;
  }
  // The browser's TabStripModel is already being destroyed, so this must not
  // walk it the way DetachStripObserverFor() does.
  strip_observers_.erase(browser);
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
  // Drop dispatch-cache entries for this window's tabs before the observer
  // goes away, otherwise a window close that does not fire per-tab kRemoved
  // (e.g. shutdown-suppressed path) leaves the entries orphaned forever.
  if (TabStripModel* model = browser->GetTabStripModel()) {
    for (int i = 0; i < model->count(); ++i) {
      if (content::WebContents* wc = model->GetWebContentsAt(i)) {
        if (auto* helper = MahoTabIdHelper::FromWebContents(wc);
            helper && !helper->stable_tab_id().empty()) {
          dispatch_cache_.erase(helper->stable_tab_id());
        }
      }
    }
  }
  strip_observers_.erase(browser);
}

void MahoTabRegistry::ReannounceUnannouncedTabs() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (auto* collection = GlobalBrowserCollection::GetInstance()) {
    collection->ForEach([this](BrowserWindowInterface* browser) {
      if (browser && IsRegistryEligibleBrowser(browser) &&
          !strip_observers_.contains(browser)) {
        AttachStripObserverFor(browser);
      }
      return true;
    });
  }
  for (const auto& [browser, observer] : strip_observers_) {
    if (!browser) {
      continue;
    }
    TabStripModel* model = browser->GetTabStripModel();
    if (!model) {
      continue;
    }
    for (int i = 0; i < model->count(); ++i) {
      content::WebContents* contents = model->GetWebContentsAt(i);
      if (!contents) {
        continue;
      }
      auto* helper = MahoTabIdHelper::FromWebContents(contents);
      if (!helper || helper->has_been_announced()) {
        continue;
      }
      DispatchCreateTabForContents(browser, contents);
    }
  }
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
  if (it == dispatch_cache_.end()) {
    return;
  }
  // If the target already holds a (canonical) entry, migrating would clobber
  // it with the throwaway pre-swap source; skip the move and drop the source.
  if (dispatch_cache_.count(to_tab_id)) {
    dispatch_cache_.erase(it);
    return;
  }
  // `dispatch_cache_` is an unordered_map: inserting the (absent) destination
  // key can rehash and invalidate every iterator, including `it`. Extract the
  // node first so the source entry is removed before the insert runs.
  auto node = dispatch_cache_.extract(it);
  node.key() = to_tab_id;
  dispatch_cache_.insert(std::move(node));
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

void MahoTabRegistry::ArchiveTabsAndRemoveFromStrip(
    Browser* browser,
    const std::vector<std::string>& tab_ids) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (tab_ids.empty() || !browser) {
    return;
  }

  std::vector<std::string> unique_ids;
  std::set<std::string> seen;
  for (const auto& id : tab_ids) {
    if (seen.insert(id).second) {
      unique_ids.push_back(id);
    }
  }

  TabStripModel* tab_strip_model = browser->GetTabStripModel();
  std::vector<std::pair<int, std::string>> index_id_pairs;
  for (int i = 0; i < tab_strip_model->count(); ++i) {
    content::WebContents* contents = tab_strip_model->GetWebContentsAt(i);
    if (!contents) {
      continue;
    }
    auto* helper = MahoTabIdHelper::FromWebContents(contents);
    if (helper) {
      std::string tab_id = helper->stable_tab_id();
      if (seen.count(tab_id)) {
        index_id_pairs.push_back({i, tab_id});
      }
    }
  }

  std::sort(index_id_pairs.begin(), index_id_pairs.end(),
            [](const auto& a, const auto& b) { return a.first > b.first; });

  for (const auto& pair : index_id_pairs) {
    const std::string& tab_id = pair.second;
    int index = pair.first;

    archive_removal_suppressions_.insert(tab_id);
    tab_strip_model->CloseWebContentsAt(index, TabCloseTypes::CLOSE_USER_GESTURE);
  }
}

bool MahoTabRegistry::ConsumeArchiveRemovalSuppression(const std::string& tab_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto it = archive_removal_suppressions_.find(tab_id);
  if (it != archive_removal_suppressions_.end()) {
    archive_removal_suppressions_.erase(it);
    return true;
  }
  return false;
}

void MahoTabRegistry::EraseArchiveRemovalSuppression(const std::string& tab_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  archive_removal_suppressions_.erase(tab_id);
}

void SetTabRegistryShellEventObserverForTesting(ShellEventObserver* observer) {
  g_tab_registry_shell_event_observer_for_testing = observer;
}

void DispatchTabRegistryShellEventForTesting(MahoCore* core,
                                             const std::string& kind,
                                             base::DictValue event) {
  // Apply synchronously against the caller's core. DispatchCoreEvent routes
  // through the global core holder, which unit tests driving a standalone
  // maho_core_new() instance never populate, so the event would be dropped.
  event.Set("kind", kind);
  std::string json;
  base::JSONWriter::Write(event, &json);
  if (g_tab_registry_shell_event_observer_for_testing) {
    g_tab_registry_shell_event_observer_for_testing->OnShellEventDispatched(
        kind, json);
  }
  if (!core) {
    return;
  }
  InvalidateSidebarCoreCacheForUpdatesJson(
      maho::core::HandleEvent(core, json.c_str()));
}

}  // namespace maho
