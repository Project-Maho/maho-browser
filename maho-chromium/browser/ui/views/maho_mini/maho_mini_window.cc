// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/maho_mini/maho_mini_window.h"

#include <utility>

#include <map>
#include <string>

#include "base/functional/callback_helpers.h"
#include "base/no_destructor.h"
#include "components/prefs/pref_service.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"

#if BUILDFLAG(IS_MAC)
#include <Carbon/Carbon.h>

#include "chrome/browser/ui/browser_window.h"
#else
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/accelerators/global_accelerator_listener/global_accelerator_listener.h"
#endif

#include "base/functional/callback.h"
#include "base/json/json_reader.h"
#include "base/scoped_observation.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/create_browser_window.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/browser_window/public/profile_browser_collection.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/browser_window/public/browser_collection_observer.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "ui/base/ui_base_types.h"
#include "ui/display/screen.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace {

constexpr int kMahoMiniWidth = 600;
constexpr int kMahoMiniHeight = 760;

maho::LinkDestinationResult ParseLinkDestination(const std::string& json_str) {
  maho::LinkDestinationResult result;
  std::optional<base::Value> val =
      base::JSONReader::Read(json_str, base::JSON_PARSE_RFC);
  if (!val || !val->is_dict()) {
    return result;
  }
  const auto* dict = val->GetIfDict();
  if (dict) {
    if (const std::string* type = dict->FindString("type")) {
      if (*type == "maho_mini") {
        result.type = maho::LinkDestinationType::kMahoMini;
      } else if (*type == "space") {
        if (const std::string* space_id = dict->FindString("space_id")) {
          result.type = maho::LinkDestinationType::kSpace;
          result.space_id = *space_id;
        }
      } else {
        result.type = maho::LinkDestinationType::kNormal;
      }
    }
  }
  return result;
}

}  // namespace

namespace maho {

namespace {

std::map<Profile*, base::WeakPtr<BrowserWindowInterface>>& GetActiveMahoMiniWindows() {
  static base::NoDestructor<std::map<Profile*, base::WeakPtr<BrowserWindowInterface>>>
      active_windows;
  return *active_windows;
}

std::map<Browser*, std::string>& GetOriginatingSpaceIds();

class MahoMiniBrowserDestructionObserver : public BrowserCollectionObserver {
 public:
  MahoMiniBrowserDestructionObserver() {
    if (auto* collection = GlobalBrowserCollection::GetInstance()) {
      observation_.Observe(collection);
    }
  }
  ~MahoMiniBrowserDestructionObserver() override = default;

  // BrowserCollectionObserver:
  void OnBrowserClosed(BrowserWindowInterface* browser) override {
    if (!browser) {
      return;
    }
    Browser* browser_ptr = static_cast<Browser*>(browser);
    if (browser_ptr) {
      GetOriginatingSpaceIds().erase(browser_ptr);
    }
  }

 private:
  base::ScopedObservation<GlobalBrowserCollection, BrowserCollectionObserver>
      observation_{this};
};

std::map<Browser*, std::string>& GetOriginatingSpaceIds() {
  static base::NoDestructor<MahoMiniBrowserDestructionObserver>
      destruction_observer;
  static base::NoDestructor<std::map<Browser*, std::string>> space_ids;
  return *space_ids;
}

std::string CaptureOriginatingSpace(Browser* active_browser) {
  auto* space_bridge = MahoSpaceProfileBridge::GetInstance();
  if (!space_bridge) {
    return std::string();
  }
  std::string originating_space =
      active_browser ? space_bridge->GetActiveSpaceId(active_browser)
                     : std::string();
  if (originating_space.empty()) {
    originating_space = space_bridge->GetActiveSpaceId();
  }
  return originating_space;
}

}  // namespace

std::string GetMahoMiniOriginatingSpaceId(Browser* popup_browser) {
  if (!popup_browser) {
    return std::string();
  }
  auto& originating_spaces = GetOriginatingSpaceIds();
  auto origin_it = originating_spaces.find(popup_browser);
  if (origin_it == originating_spaces.end()) {
    return std::string();
  }
  for (const auto& entry : GetActiveMahoMiniWindows()) {
    if (entry.second.get() == popup_browser) {
      return origin_it->second;
    }
  }
  originating_spaces.erase(origin_it);
  return std::string();
}

void LaunchMahoMini(content::BrowserContext* context, MahoMiniRequest request) {
  Profile* profile = nullptr;
  if (context) {
    profile = Profile::FromBrowserContext(context);
  } else {
    BrowserWindowInterface* active_interface =
        GlobalBrowserCollection::GetInstance()->GetLastActiveBrowser();
    if (active_interface) {
      profile = active_interface->GetProfile();
    }
  }
  if (!profile) {
    profile = ProfileManager::GetLastUsedProfileIfLoaded();
  }
  if (!profile) {
    return;
  }

  BrowserWindowInterface* active_interface =
      GlobalBrowserCollection::GetInstance()->GetLastActiveBrowser();
  Browser* active_browser = static_cast<Browser*>(active_interface);

  auto& active_windows = GetActiveMahoMiniWindows();
  auto active_window_it = active_windows.find(profile);
  if (active_window_it != active_windows.end() && active_window_it->second) {
    GURL target = request.url.is_empty() ? GURL("about:blank") : request.url;
    Browser* maho_mini_browser =
        static_cast<Browser*>(active_window_it->second.get());
    GetOriginatingSpaceIds()[maho_mini_browser] =
        CaptureOriginatingSpace(active_browser);
    maho_mini_browser->OpenURL(
        content::OpenURLParams(target, content::Referrer(),
                               WindowOpenDisposition::CURRENT_TAB,
                               ui::PAGE_TRANSITION_LINK, false),
        base::NullCallback());
    maho_mini_browser->GetWindow()->Activate();
    return;
  }
  if (active_window_it != active_windows.end()) {
    active_windows.erase(active_window_it);
  }

  // Get bounds to center the popup window
  gfx::Rect bounds;
  if (active_browser && active_browser->GetWindow()) {
    bounds = active_browser->GetWindow()->GetBounds();
  } else if (display::Screen::Get()) {
    bounds = display::Screen::Get()->GetPrimaryDisplay().bounds();
  } else {
    bounds = gfx::Rect(0, 0, 1024, 768);
  }

  int window_width = kMahoMiniWidth;
  int window_height = kMahoMiniHeight;
  PrefService* prefs = profile->GetPrefs();
  if (prefs) {
    const int persisted_width =
        prefs->GetInteger(sidebar_prefs::kMahoMiniWindowWidth);
    const int persisted_height =
        prefs->GetInteger(sidebar_prefs::kMahoMiniWindowHeight);
    if (persisted_width > 0 && persisted_height > 0) {
      window_width = persisted_width;
      window_height = persisted_height;
    }
  }

  int x = bounds.x() + (bounds.width() - window_width) / 2;
  int y = bounds.y() + (bounds.height() - window_height) / 2;

  std::string originating_space = CaptureOriginatingSpace(active_browser);

  BrowserWindowCreateParams params(Browser::TYPE_POPUP, profile,
                               /*user_gesture=*/true);
  params.is_maho_mini = true;
  params.initial_bounds = gfx::Rect(x, y, window_width, window_height);
  params.omit_from_session_restore = true;

  Browser* popup = static_cast<Browser*>(CreateBrowserWindow(std::move(params)));
  active_windows[profile] = popup->GetWeakPtr();
  GetOriginatingSpaceIds()[popup] = originating_space;

  GURL target = request.url.is_empty() ? GURL("about:blank") : request.url;
  chrome::AddTabAt(popup, target, /*index=*/-1, /*foreground=*/true);

  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(popup);
  if (browser_view) {
    views::Widget* widget = browser_view->GetWidget();
    if (widget) {
      widget->SetZOrderLevel(ui::ZOrderLevel::kNormal);
    }
  }

  popup->GetWindow()->Show();
}

void DecideLinkDestination(
    const GURL& url,
    bool is_external,
    base::OnceCallback<void(LinkDestinationResult)> callback) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    LinkDestinationResult default_result;
    std::move(callback).Run(default_result);
    return;
  }

  std::string url_spec = url.spec();
  maho::PostCoreTask<std::string>(
      FROM_HERE,
      base::BindOnce(
          [](std::string url_spec, bool is_external) -> std::string {
            MahoCore* core = maho::GetCore();
            if (!core) {
              return "";
            }
            char* result_json = maho_core_decide_link_destination(
                core, url_spec.c_str(), is_external, nullptr);
            if (!result_json) {
              return "";
            }
            std::string result(result_json);
            maho_string_free(result_json);
            return result;
          },
          url_spec, is_external),
      base::BindOnce(
          [](base::OnceCallback<void(LinkDestinationResult)> callback,
             std::string json_str) {
            std::move(callback).Run(ParseLinkDestination(json_str));
          },
          std::move(callback)));
}

#if BUILDFLAG(IS_MAC)
namespace {

OSStatus HotKeyHandler(EventHandlerCallRef nextHandler,
                       EventRef theEvent,
                       void* userData) {
  EventHotKeyID hotKeyID;
  if (GetEventParameter(theEvent, kEventParamDirectObject, typeEventHotKeyID,
                        nullptr, sizeof(hotKeyID), nullptr,
                        &hotKeyID) != noErr) {
    return eventNotHandledErr;
  }
  if (hotKeyID.id == 1) {
    // The Carbon hotkey is system-wide and consumes ⌥⌘N before it can reach
    // the in-app shortcut interceptor, so gating on an active Maho window
    // would make ⌥⌘N a no-op while Maho is focused. LaunchMahoMini() reuses
    // the single active window (GetActiveMahoMiniWindow), preventing dupes.
    Profile* profile = ProfileManager::GetLastUsedProfileIfLoaded();
    if (profile) {
      bool enabled = profile->GetPrefs()->GetBoolean(
          maho::sidebar_prefs::kMahoMiniGlobalShortcutEnabled);
      if (!enabled) {
        return eventNotHandledErr;
      }
      maho::LaunchMahoMini(profile, maho::MahoMiniRequest{GURL()});
      return noErr;
    }
  }
  return eventNotHandledErr;
}

}  // namespace

void RegisterGlobalShortcut(Profile* profile) {
  static bool registered = false;
  if (registered || !profile ||
      !profile->GetPrefs()->GetBoolean(
          maho::sidebar_prefs::kMahoMiniGlobalShortcutEnabled)) {
    return;
  }

  EventHotKeyRef hotkey_ref;
  EventHotKeyID hotKeyID;
  hotKeyID.signature = 'maho';
  hotKeyID.id = 1;

  UInt32 modifiers = cmdKey | optionKey;
  if (RegisterEventHotKey(45, modifiers, hotKeyID, GetApplicationEventTarget(),
                          0, &hotkey_ref) != noErr) {
    return;
  }

  EventTypeSpec eventType;
  eventType.eventClass = kEventClassKeyboard;
  eventType.eventKind = kEventHotKeyPressed;
  if (InstallApplicationEventHandler(NewEventHandlerUPP(HotKeyHandler), 1,
                                     &eventType, nullptr, nullptr) == noErr) {
    registered = true;
  }
}
#else  // !IS_MAC: portable global shortcut via ui::GlobalAcceleratorListener.

namespace {

// Portable equivalent of the macOS \u2325\u2318N global hotkey.
class MahoMiniShortcutObserver
    : public ui::GlobalAcceleratorListener::Observer {
 public:
  void OnKeyPressed(const ui::Accelerator& accelerator) override;

  void ExecuteCommand(const std::string& accelerator_group_id,
                      const std::string& command_id) override {}
};

void MahoMiniShortcutObserver::OnKeyPressed(
    const ui::Accelerator& accelerator) {
  Profile* profile = ProfileManager::GetLastUsedProfileIfLoaded();
  if (!profile) {
    return;
  }
  if (!profile->GetPrefs()->GetBoolean(
          maho::sidebar_prefs::kMahoMiniGlobalShortcutEnabled)) {
    return;
  }
  maho::LaunchMahoMini(profile, maho::MahoMiniRequest{GURL()});
}

MahoMiniShortcutObserver* ShortcutObserver() {
  static MahoMiniShortcutObserver observer;
  return &observer;
}

}  // namespace

void RegisterGlobalShortcut(Profile* profile) {
  static bool registered = false;
  if (registered || !profile ||
      !profile->GetPrefs()->GetBoolean(
          maho::sidebar_prefs::kMahoMiniGlobalShortcutEnabled)) {
    return;
  }

  auto* listener = ui::GlobalAcceleratorListener::GetInstance();
  if (!listener) {
    return;
  }
  const ui::Accelerator accelerator(ui::VKEY_N,
                                    ui::EF_CONTROL_DOWN | ui::EF_ALT_DOWN);
  if (listener->RegisterAccelerator(accelerator, ShortcutObserver())) {
    registered = true;
  }
}
#endif

}  // namespace maho

// static
void MahoMiniWindow::Open(Browser* parent_browser, const GURL& url) {
  maho::MahoMiniRequest request;
  request.url = url;
  maho::LaunchMahoMini(parent_browser ? parent_browser->GetProfile() : nullptr,
                       request);
}

// static
void MahoMiniWindow::Open(Profile* profile, const GURL& url) {
  maho::MahoMiniRequest request;
  request.url = url;
  maho::LaunchMahoMini(profile, request);
}

// static
void MahoMiniWindow::PromoteToTab(Browser* popup_browser) {
  if (!popup_browser) {
    return;
  }

  TabStripModel* popup_tab_strip = popup_browser->GetTabStripModel();
  if (!popup_tab_strip || popup_tab_strip->empty()) {
    return;
  }

  // Find a tabbed (TYPE_NORMAL) browser with the same profile, skipping
  // popups. This ensures the promoted tab lands in a real browser window.
  ProfileBrowserCollection* collection =
      ProfileBrowserCollection::GetForProfile(popup_browser->GetProfile());
  BrowserWindowInterface* target_bwi =
      collection ? collection->FindTabbedBrowser(/*match_original_profiles=*/false)
                 : nullptr;
  Browser* target_browser = static_cast<Browser*>(target_bwi);
  if (!target_browser || target_browser == popup_browser) {
    target_browser =
        static_cast<Browser*>(CreateBrowserWindow(BrowserWindowCreateParams(popup_browser->GetProfile(), true)));
  }

  int active_index = popup_tab_strip->active_index();
  std::unique_ptr<content::WebContents> contents =
      popup_tab_strip->DetachWebContentsAtForInsertion(active_index);
  if (contents) {
    target_browser->GetTabStripModel()->InsertWebContentsAt(
        /*index=*/-1, std::move(contents),
        AddTabTypes::ADD_ACTIVE | AddTabTypes::ADD_INHERIT_OPENER);
    target_browser->GetWindow()->Show();
    target_browser->GetWindow()->Activate();
  }

  popup_browser->GetWindow()->Close();
}
