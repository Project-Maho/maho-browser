// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/maho_tab_registry.h"

#include <string>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/memory/raw_ptr.h"
#include "base/values.h"
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

namespace maho {

namespace {

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

}  // namespace

class MahoTabRegistry::NavObserver : public content::WebContentsObserver {
 public:
  explicit NavObserver(content::WebContents* contents)
      : content::WebContentsObserver(contents) {}

  ~NavObserver() override = default;

  void DidFinishNavigation(
      content::NavigationHandle* navigation_handle) override {
    if (!navigation_handle) return;
    if (!navigation_handle->IsInPrimaryMainFrame()) return;
    if (!navigation_handle->HasCommitted()) return;
    if (navigation_handle->IsErrorPage()) return;
    if (navigation_handle->IsSameDocument()) return;
    MaybeDispatch();
  }

  void TitleWasSet(content::NavigationEntry* /*entry*/) override {
    MaybeDispatch();
  }

 private:
  // Skip dispatch if the (url, title) pair is identical to the last one we
  // sent. Prevents flooding the Rust history Vec when SPAs (e.g. YouTube,
  // Twitter) change titles repeatedly within the same committed navigation.
  void MaybeDispatch() {
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
  }

  std::string last_url_;
  std::string last_title_;
};

class MahoTabRegistry::StripObserver : public TabStripModelObserver {
 public:
  StripObserver(BrowserWindowInterface* browser, TabStripModel* model)
      : browser_(browser), model_(model) {
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
      const auto* remove = change.GetRemove();
      if (!remove) {
        return;
      }
      for (const auto& removed : remove->contents) {
        nav_observers_.erase(removed.contents);
        if (!TabRemoveReasonUtils::WillDeleteTab(removed.remove_reason)) {
          continue;
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
        if (old_helper && new_helper &&
            !old_helper->stable_tab_id().empty() &&
            old_helper->stable_tab_id() != new_helper->stable_tab_id()) {
          new_helper->SetRestoredTabId(old_helper->stable_tab_id());
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
    nav_observers_.emplace(contents, std::make_unique<NavObserver>(contents));
  }

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
      browser, std::make_unique<StripObserver>(browser, model));
}

void MahoTabRegistry::DetachStripObserverFor(BrowserWindowInterface* browser) {
  if (!browser) {
    return;
  }
  strip_observers_.erase(browser);
}

}  // namespace maho
