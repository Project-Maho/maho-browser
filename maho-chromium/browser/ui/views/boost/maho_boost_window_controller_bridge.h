// Copyright 2026 Maho Browser. All rights reserved.

// Lightweight bridge header that breaks the circular include dependency
// between //maho/browser/ui/views/boost and //maho/browser/ui/webui/maho_boost.
//
// The webui side (maho_boost_ui.cc) includes this instead of the full
// maho_boost_window_controller.h, avoiding a views→webui→views include cycle.
// Definitions live in maho_boost_window_controller.cc.

#ifndef MAHO_BROWSER_UI_VIEWS_BOOST_MAHO_BOOST_WINDOW_CONTROLLER_BRIDGE_H_
#define MAHO_BROWSER_UI_VIEWS_BOOST_MAHO_BOOST_WINDOW_CONTROLLER_BRIDGE_H_

#include <optional>
#include <string>

#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "maho/browser/ui/webui/maho_boost/maho_boost.mojom.h"

namespace content {
class WebContents;
}

namespace maho {

class MahoBoostWindowController;

base::WeakPtr<MahoBoostWindowController>
ConsumePendingBoostControllerForUI();
base::WeakPtr<MahoBoostWindowController>
ConsumePendingBoostControllerForUI(const std::string& controller_token);

void SetBoostEditorKilledCallback(
    base::WeakPtr<MahoBoostWindowController> controller,
    base::RepeatingClosure callback);

void CloseBoostWindow(
    base::WeakPtr<MahoBoostWindowController> controller);

void CompleteBoostHostClose(
    base::WeakPtr<MahoBoostWindowController> controller);
void RequestBoostWindowClose(
    base::WeakPtr<MahoBoostWindowController> controller);
void SetBoostTemporaryId(
    base::WeakPtr<MahoBoostWindowController> controller,
    std::optional<std::string> boost_id);
void SetBoostHostCloseState(
    base::WeakPtr<MahoBoostWindowController> controller,
    std::optional<std::string> boost_id,
    bool dirty);

void SetBoostWindowMode(
    base::WeakPtr<MahoBoostWindowController> controller,
    maho_boost::mojom::WindowMode mode);

content::WebContents* GetTargetTabForBoost(
    base::WeakPtr<MahoBoostWindowController> controller);

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_BOOST_MAHO_BOOST_WINDOW_CONTROLLER_BRIDGE_H_
