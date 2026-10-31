// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MAHO_BROWSER_UI_VIEWS_AGENT_MAHO_AGENT_HELP_OVERLAY_H_
#define MAHO_BROWSER_UI_VIEWS_AGENT_MAHO_AGENT_HELP_OVERLAY_H_

#include <cstdint>
#include <memory>
#include <string>

#include "base/memory/raw_ptr.h"
#include "chrome/browser/ui/browser.h"
#include "maho/browser/ui/browser_user_data.h"
#include "maho/browser/ui/views/agent/maho_agent_help_bar.h"
#include "ui/views/widget/widget.h"

namespace maho {

// Floats a MahoAgentHelpBar over the browser contents while an agent waits
// for the human (browser_request_help). Complete returns the task space to
// the agent; Cancel ends it with an error so the agent stops instead of
// retrying the blocked step.
class MahoAgentHelpOverlay : public BrowserUserData<MahoAgentHelpOverlay>,
                             public MahoAgentHelpBar::Delegate {
 public:
  explicit MahoAgentHelpOverlay(Browser* browser);
  ~MahoAgentHelpOverlay() override;
  MahoAgentHelpOverlay(const MahoAgentHelpOverlay&) = delete;
  MahoAgentHelpOverlay& operator=(const MahoAgentHelpOverlay&) = delete;

  void ShowRequest(int64_t tab_id,
                   const std::string& session_id,
                   const std::string& request_id,
                   const std::string& prompt);
  void DismissRequest(const std::string& request_id);

  // MahoAgentHelpBar::Delegate:
  void OnHelpCompleted(const std::string& request_id) override;
  void OnHelpCancelled(const std::string& request_id) override;

  MahoAgentHelpBar* help_bar_for_testing() const { return help_bar_; }

 private:
  void EnsureWidget();
  gfx::Rect ComputeTargetBounds() const;
  void Hide();

  const raw_ptr<Browser> browser_;
  std::unique_ptr<views::Widget> widget_;
  raw_ptr<MahoAgentHelpBar> help_bar_ = nullptr;
  int64_t tab_id_ = 0;
  std::string session_id_;
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_AGENT_MAHO_AGENT_HELP_OVERLAY_H_
