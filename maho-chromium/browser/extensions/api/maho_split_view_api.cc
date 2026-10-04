// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/extensions/api/maho_split_view_api.h"

#include <optional>
#include <string>

#include "base/strings/string_number_conversions.h"
#include "base/values.h"
#include "chrome/browser/extensions/api/tabs/windows_util.h"
#include "chrome/browser/extensions/extension_tab_util.h"
#include "chrome/browser/extensions/window_controller.h"
#include "chrome/browser/profiles/profile.h"
#include "maho/browser/maho_private_context_policy.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/profile_browser_collection.h"
#include "components/sessions/core/session_id.h"
#include "maho/browser/ui/views/split_view/maho_split_view_controller.h"
#include "url/gurl.h"

namespace extensions {

namespace {

// Builds the SplitViewInfo dictionary for the given browser window.
base::DictValue BuildSplitViewInfo(Browser* browser, bool active) {
  base::DictValue info;
  info.Set("active", active);
  if (active) {
    info.Set("splitViewId",
             base::NumberToString(browser->GetSessionID().id()));
  }
  return info;
}

// Privacy boundary (VerificationContract 4): Incognito split membership must
// never be mirrored to MahoCore, so OTR profiles use kPrivateLocal. Only the
// kPersistentSplit-capable (regular) profile persists.
MahoSplitPersistence SplitPersistenceForProfile(Profile* profile) {
  return MahoIsCapabilityAllowed(profile,
                                 MahoPrivateCapability::kPersistentSplit)
             ? MahoSplitPersistence::kPersistent
              : MahoSplitPersistence::kPrivateLocal;
}

std::optional<int> WindowIdFromArgs(const base::ListValue& args) {
  for (const base::Value& arg : args) {
    if (arg.is_int()) {
      return arg.GetInt();
    }
  }
  return std::nullopt;
}

// L25: resolve the target window. A supplied windowId must belong to the calling
// profile — a security boundary so an extension can never target another
// profile's (e.g. non-incognito) window.
Browser* ResolveTargetBrowser(ExtensionFunction* function,
                              Profile* profile,
                              std::optional<int> window_id,
                              std::string* error) {
  if (window_id) {
    WindowController* controller = nullptr;
    if (!windows_util::GetControllerFromWindowID(
            function, *window_id, WindowController::GetAllWindowFilter(),
            &controller, error)) {
      return nullptr;
    }
    BrowserWindowInterface* browser_interface =
        controller ? controller->GetBrowser() : nullptr;
    if (!browser_interface || browser_interface->GetProfile() != profile) {
      *error = "windowId does not match a window for this profile.";
      return nullptr;
    }
    return static_cast<Browser*>(browser_interface);
  }
  ProfileBrowserCollection* collection =
      ProfileBrowserCollection::GetForProfile(profile);
  BrowserWindowInterface* browser_interface =
      collection ? collection->GetLastActiveBrowser() : nullptr;
  return static_cast<Browser*>(browser_interface);
}

}  // namespace

MahoSplitViewCreateFunction::MahoSplitViewCreateFunction() = default;
MahoSplitViewCreateFunction::~MahoSplitViewCreateFunction() = default;

ExtensionFunction::ResponseAction MahoSplitViewCreateFunction::Run() {
  Profile* profile = Profile::FromBrowserContext(browser_context());
  std::string window_error;
  Browser* browser = ResolveTargetBrowser(
      this, profile, WindowIdFromArgs(args()), &window_error);
  if (!browser) {
    return RespondNow(Error(window_error.empty()
                                ? "No active browser window for this profile."
                                : window_error));
  }

  GURL url("about:blank");
  if (!args().empty() && args()[0].is_string()) {
    base::expected<GURL, std::string> prepared =
        ExtensionTabUtil::PrepareURLForNavigation(
            args()[0].GetString(), extension(), browser_context());
    if (!prepared.has_value()) {
      return RespondNow(Error(prepared.error()));
    }
    url = std::move(prepared.value());
  }

  MahoSplitViewController controller(browser, SplitPersistenceForProfile(profile));
  controller.AddSplitWithURL(url);

  return RespondNow(WithArguments(
      BuildSplitViewInfo(browser, controller.IsSplitActive())));
}

MahoSplitViewCloseFunction::MahoSplitViewCloseFunction() = default;
MahoSplitViewCloseFunction::~MahoSplitViewCloseFunction() = default;

ExtensionFunction::ResponseAction MahoSplitViewCloseFunction::Run() {
  Profile* profile = Profile::FromBrowserContext(browser_context());
  std::string window_error;
  Browser* browser = ResolveTargetBrowser(
      this, profile, WindowIdFromArgs(args()), &window_error);
  if (!browser) {
    return RespondNow(Error(window_error.empty()
                                ? "No active browser window for this profile."
                                : window_error));
  }

  MahoSplitViewController controller(browser, SplitPersistenceForProfile(profile));
  if (controller.IsSplitActive()) {
    controller.RemoveSplit();
  }

  return RespondNow(WithArguments(
      BuildSplitViewInfo(browser, controller.IsSplitActive())));
}

MahoSplitViewQueryFunction::MahoSplitViewQueryFunction() = default;
MahoSplitViewQueryFunction::~MahoSplitViewQueryFunction() = default;

ExtensionFunction::ResponseAction MahoSplitViewQueryFunction::Run() {
  Profile* profile = Profile::FromBrowserContext(browser_context());
  std::string window_error;
  Browser* browser = ResolveTargetBrowser(
      this, profile, WindowIdFromArgs(args()), &window_error);
  if (!browser) {
    return RespondNow(Error(window_error.empty()
                                ? "No active browser window for this profile."
                                : window_error));
  }

  MahoSplitViewController controller(browser);
  return RespondNow(WithArguments(
      BuildSplitViewInfo(browser, controller.IsSplitActive())));
}

}  // namespace extensions
