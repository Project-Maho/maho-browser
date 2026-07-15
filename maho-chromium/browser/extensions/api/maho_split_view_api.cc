// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/extensions/api/maho_split_view_api.h"

#include <string>

#include "base/strings/string_number_conversions.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_finder.h"
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
             base::NumberToString(browser->session_id().id()));
  }
  return info;
}

}  // namespace

MahoSplitViewCreateFunction::MahoSplitViewCreateFunction() = default;
MahoSplitViewCreateFunction::~MahoSplitViewCreateFunction() = default;

ExtensionFunction::ResponseAction MahoSplitViewCreateFunction::Run() {
  Profile* profile = Profile::FromBrowserContext(browser_context());
  Browser* browser = chrome::FindBrowserWithProfile(profile);
  if (!browser) {
    return RespondNow(Error("No active browser window for this profile."));
  }

  GURL url("about:blank");
  if (!args().empty() && args()[0].is_string()) {
    GURL candidate(args()[0].GetString());
    if (candidate.is_valid()) {
      url = std::move(candidate);
    }
  }

  MahoSplitViewController controller(browser);
  controller.AddSplitWithURL(url);

  return RespondNow(WithArguments(
      BuildSplitViewInfo(browser, controller.IsSplitActive())));
}

MahoSplitViewCloseFunction::MahoSplitViewCloseFunction() = default;
MahoSplitViewCloseFunction::~MahoSplitViewCloseFunction() = default;

ExtensionFunction::ResponseAction MahoSplitViewCloseFunction::Run() {
  Profile* profile = Profile::FromBrowserContext(browser_context());
  Browser* browser = chrome::FindBrowserWithProfile(profile);
  if (!browser) {
    return RespondNow(Error("No active browser window for this profile."));
  }

  MahoSplitViewController controller(browser);
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
  Browser* browser = chrome::FindBrowserWithProfile(profile);
  if (!browser) {
    return RespondNow(Error("No active browser window for this profile."));
  }

  MahoSplitViewController controller(browser);
  return RespondNow(WithArguments(
      BuildSplitViewInfo(browser, controller.IsSplitActive())));
}

}  // namespace extensions
