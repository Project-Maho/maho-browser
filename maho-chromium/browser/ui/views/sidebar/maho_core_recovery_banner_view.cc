// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_core_recovery_banner_view.h"

#include "ui/views/controls/label.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/metadata/metadata_impl_macros.h"
#include "base/functional/bind.h"
#include "chrome/browser/lifetime/application_lifetime.h"

namespace maho {

BEGIN_METADATA(MahoCoreRecoveryBannerView)
END_METADATA

MahoCoreRecoveryBannerView::MahoCoreRecoveryBannerView() {
  SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal));
  AddChildView(std::make_unique<views::Label>());
  AddChildView(std::make_unique<views::MdTextButton>(
      base::BindRepeating(
          [](MahoCoreRecoveryBannerView* view) { view->SetDismissed(true); },
          base::Unretained(this)),
      u"Dismiss"));
  AddChildView(std::make_unique<views::MdTextButton>(
      base::BindRepeating([] { chrome::AttemptRestart(); }), u"Restart Maho"));
  state_subscription_ = AddCoreRecoveryStateCallback(base::BindRepeating(
      &MahoCoreRecoveryBannerView::SetState, base::Unretained(this)));
  SetState(CoreRecoveryBannerState::kHidden);
}

MahoCoreRecoveryBannerView::~MahoCoreRecoveryBannerView() = default;

void MahoCoreRecoveryBannerView::SetState(CoreRecoveryBannerState state) {
  state_ = state;
  auto* label = static_cast<views::Label*>(children().front());
  auto* dismiss = static_cast<views::MdTextButton*>(children()[1]);
  auto* restart = static_cast<views::MdTextButton*>(children()[2]);
  switch (ApplyCoreRecoveryBannerDismissal(state, dismissed_)) {
    case CoreRecoveryBannerState::kHidden:
      SetVisible(false);
      break;
    case CoreRecoveryBannerState::kDegraded:
      label->SetText(u"Some Maho features are temporarily unavailable.");
      restart->SetVisible(false);
      SetVisible(true);
      break;
    case CoreRecoveryBannerState::kFailedWithRetry:
      label->SetText(u"Maho core startup failed. Restart Maho to restore features.");
      restart->SetVisible(true);
      SetVisible(true);
      break;
  }
  dismiss->SetVisible(ApplyCoreRecoveryBannerDismissal(state, dismissed_) !=
                      CoreRecoveryBannerState::kHidden);
  restart->SetVisible(CoreRecoveryBannerHasRestartAction(state) &&
                      !dismissed_);
}

void MahoCoreRecoveryBannerView::SetDismissed(bool dismissed) {
  dismissed_ = dismissed;
  SetVisible(!dismissed_ && state_ != CoreRecoveryBannerState::kHidden);
}

}  // namespace maho
