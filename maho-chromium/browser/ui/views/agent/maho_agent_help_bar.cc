// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "maho/browser/ui/views/agent/maho_agent_help_bar.h"

#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"

const char MahoAgentHelpBar::kViewClassName[] = "MahoAgentHelpBar";

MahoAgentHelpBar::MahoAgentHelpBar(Delegate* delegate) : delegate_(delegate) {
  DCHECK(delegate_);
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(8, 12), 8));

  prompt_label_ = AddChildView(
      std::make_unique<views::Label>(u"Agent is waiting for your help"));
  prompt_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  prompt_label_->SetMultiLine(true);
  layout->SetFlexForView(prompt_label_, 1);

  complete_button_ = AddChildView(std::make_unique<views::MdTextButton>(
      base::BindRepeating(
          [](MahoAgentHelpBar* bar) {
            bar->delegate_->OnHelpCompleted(bar->active_request_id_);
            bar->Dismiss();
          },
          base::Unretained(this)),
      u"Complete"));

  cancel_button_ = AddChildView(std::make_unique<views::MdTextButton>(
      base::BindRepeating(
          [](MahoAgentHelpBar* bar) {
            bar->delegate_->OnHelpCancelled(bar->active_request_id_);
            bar->Dismiss();
          },
          base::Unretained(this)),
      u"Cancel"));

  SetVisible(false);
}

MahoAgentHelpBar::~MahoAgentHelpBar() = default;

void MahoAgentHelpBar::ShowRequest(const std::string& request_id,
                                   const std::string& prompt) {
  active_request_id_ = request_id;
  prompt_label_->SetText(base::UTF8ToUTF16(prompt));
  SetVisible(true);
  InvalidateLayout();
  PreferredSizeChanged();
}

void MahoAgentHelpBar::Dismiss() {
  active_request_id_.clear();
  SetVisible(false);
  InvalidateLayout();
  PreferredSizeChanged();
}

std::string MahoAgentHelpBar::GetClassName() const {
  return kViewClassName;
}
