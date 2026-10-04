// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MAHO_BROWSER_UI_VIEWS_AGENT_MAHO_AGENT_HELP_BAR_H_
#define MAHO_BROWSER_UI_VIEWS_AGENT_MAHO_AGENT_HELP_BAR_H_

#include <string>

#include "ui/views/view.h"

namespace views {
class Label;
class MdTextButton;
}  // namespace views

// Native banner shown while an agent run is paused waiting for the human to
// complete a step (CAPTCHA, 2FA, checkout). The adapter FSM owns timing and
// state; this view is presentation-only and resolves the request through its
// delegate when the human presses Complete or Cancel.
class MahoAgentHelpBar : public views::View {
 public:
  class Delegate {
   public:
    virtual void OnHelpCompleted(const std::string& request_id) = 0;
    virtual void OnHelpCancelled(const std::string& request_id) = 0;

   protected:
    virtual ~Delegate() = default;
  };

  static const char kViewClassName[];

  explicit MahoAgentHelpBar(Delegate* delegate);
  ~MahoAgentHelpBar() override;

  MahoAgentHelpBar(const MahoAgentHelpBar&) = delete;
  MahoAgentHelpBar& operator=(const MahoAgentHelpBar&) = delete;

  void ShowRequest(const std::string& request_id, const std::string& prompt);
  void Dismiss();
  const std::string& active_request_id() const { return active_request_id_; }
  bool is_showing() const { return !active_request_id_.empty(); }

  // views::View:
  std::string GetClassName() const;

 private:
  raw_ptr<Delegate> delegate_ = nullptr;
  std::string active_request_id_;
  raw_ptr<views::Label> prompt_label_ = nullptr;
  raw_ptr<views::MdTextButton> complete_button_ = nullptr;
  raw_ptr<views::MdTextButton> cancel_button_ = nullptr;
};

#endif  // MAHO_BROWSER_UI_VIEWS_AGENT_MAHO_AGENT_HELP_BAR_H_
