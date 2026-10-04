// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "maho/browser/ui/views/agent/maho_tab_borrow_chip.h"

#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"

namespace maho {

const char MahoTabBorrowChip::kViewClassName[] = "MahoTabBorrowChip";

MahoTabBorrowChip::MahoTabBorrowChip(Delegate* delegate) : delegate_(delegate) {
  DCHECK(delegate_);
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(6, 10), 6));

  label_ = AddChildView(
      std::make_unique<views::Label>(u"Agent wants to borrow this tab"));
  label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  layout->SetFlexForView(label_, 1);

  allow_button_ = AddChildView(std::make_unique<views::MdTextButton>(
      base::BindRepeating(
          [](MahoTabBorrowChip* chip) {
            chip->delegate_->OnBorrowApproved(chip->active_request_id_);
            chip->Dismiss();
          },
          base::Unretained(this)),
      u"Allow"));

  deny_button_ = AddChildView(std::make_unique<views::MdTextButton>(
      base::BindRepeating(
          [](MahoTabBorrowChip* chip) {
            chip->delegate_->OnBorrowDenied(chip->active_request_id_);
            chip->Dismiss();
          },
          base::Unretained(this)),
      u"Deny"));

  SetVisible(false);
}

MahoTabBorrowChip::~MahoTabBorrowChip() = default;

void MahoTabBorrowChip::ShowRequest(const std::string& request_id,
                                    const std::string& tab_title) {
  active_request_id_ = request_id;
  std::u16string text = u"Agent wants to borrow: " + base::UTF8ToUTF16(tab_title);
  label_->SetText(text);
  SetVisible(true);
  InvalidateLayout();
  PreferredSizeChanged();
}

void MahoTabBorrowChip::Dismiss() {
  active_request_id_.clear();
  SetVisible(false);
  InvalidateLayout();
  PreferredSizeChanged();
}

std::string MahoTabBorrowChip::GetClassName() const {
  return kViewClassName;
}

}  // namespace maho
