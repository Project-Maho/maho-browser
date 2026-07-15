// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/extensions/api/maho_side_panel_api.h"

#include <string>

#include "chrome/browser/profiles/profile.h"
#include "extensions/common/extension.h"
#include "maho/browser/ui/views/side_panel/maho_extension_side_panel_coordinator.h"

namespace extensions {

MahoSidePanelSetLayoutFunction::MahoSidePanelSetLayoutFunction() = default;
MahoSidePanelSetLayoutFunction::~MahoSidePanelSetLayoutFunction() = default;

ExtensionFunction::ResponseAction MahoSidePanelSetLayoutFunction::Run() {
  if (args().empty() || !args()[0].is_string()) {
    return RespondNow(Error("layout must be 'left' or 'right'."));
  }
  const std::string& layout = args()[0].GetString();
  if (layout != "left" && layout != "right") {
    return RespondNow(Error("layout must be 'left' or 'right'."));
  }

  if (!extension()) {
    return RespondNow(Error("No calling extension."));
  }

  Profile* profile = Profile::FromBrowserContext(browser_context());
  maho::MahoExtensionSidePanelCoordinator* coordinator =
      maho::MahoExtensionSidePanelCoordinator::GetForProfile(profile);
  if (!coordinator) {
    return RespondNow(Error("Side panel coordinator unavailable."));
  }

  coordinator->SetLayout(extension()->id(), layout);
  return RespondNow(NoArguments());
}

}  // namespace extensions
