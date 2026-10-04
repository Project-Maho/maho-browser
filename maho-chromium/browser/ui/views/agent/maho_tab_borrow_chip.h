// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MAHO_BROWSER_UI_VIEWS_AGENT_MAHO_TAB_BORROW_CHIP_H_
#define MAHO_BROWSER_UI_VIEWS_AGENT_MAHO_TAB_BORROW_CHIP_H_

#include <string>

#include "base/memory/raw_ptr.h"
#include "ui/views/view.h"

namespace views {
class Label;
class MdTextButton;
}  // namespace views

namespace maho {

class MahoTabBorrowChip : public views::View {
 public:
  static const char kViewClassName[];

  class Delegate {
   public:
    virtual ~Delegate() = default;
    virtual void OnBorrowApproved(const std::string& request_id) = 0;
    virtual void OnBorrowDenied(const std::string& request_id) = 0;
  };

  explicit MahoTabBorrowChip(Delegate* delegate);
  MahoTabBorrowChip(const MahoTabBorrowChip&) = delete;
  MahoTabBorrowChip& operator=(const MahoTabBorrowChip&) = delete;
  ~MahoTabBorrowChip() override;

  void ShowRequest(const std::string& request_id, const std::string& tab_title);
  void Dismiss();
  const std::string& active_request_id() const { return active_request_id_; }
  bool is_showing() const { return !active_request_id_.empty(); }

  std::string GetClassName() const;

 private:
  raw_ptr<Delegate> delegate_ = nullptr;
  std::string active_request_id_;
  raw_ptr<views::Label> label_ = nullptr;
  raw_ptr<views::MdTextButton> allow_button_ = nullptr;
  raw_ptr<views::MdTextButton> deny_button_ = nullptr;
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_AGENT_MAHO_TAB_BORROW_CHIP_H_
