// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/maho_ai_popup_lifetime_tracker.h"

#include "base/check.h"
#include "base/logging.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "ui/base/base_window.h"

namespace maho {

// static
MahoAiPopupLifetimeTracker* MahoAiPopupLifetimeTracker::Get() {
  static base::NoDestructor<MahoAiPopupLifetimeTracker> instance;
  return instance.get();
}

MahoAiPopupLifetimeTracker::MahoAiPopupLifetimeTracker() {
  browser_collection_observation_.Observe(
      GlobalBrowserCollection::GetInstance());
}

MahoAiPopupLifetimeTracker::~MahoAiPopupLifetimeTracker() = default;

void MahoAiPopupLifetimeTracker::TrackPopup(BrowserWindowInterface* opener,
                                             BrowserWindowInterface* popup) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(opener);
  DCHECK(popup);
  DCHECK_NE(opener, popup);

  auto it = opener_to_popup_.find(opener);
  if (it != opener_to_popup_.end()) {
    popup_to_opener_.erase(it->second);
  }

  opener_to_popup_[opener] = popup;
  popup_to_opener_[popup] = opener;
}

BrowserWindowInterface* MahoAiPopupLifetimeTracker::GetPopupForOpener(
    BrowserWindowInterface* opener) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto it = opener_to_popup_.find(opener);
  return it != opener_to_popup_.end() ? it->second : nullptr;
}

BrowserWindowInterface* MahoAiPopupLifetimeTracker::GetOpenerForPopup(
    BrowserWindowInterface* popup) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto it = popup_to_opener_.find(popup);
  return it != popup_to_opener_.end() ? it->second : nullptr;
}

void MahoAiPopupLifetimeTracker::OnBrowserCreated(
    BrowserWindowInterface* browser) {}

void MahoAiPopupLifetimeTracker::OnBrowserClosed(
    BrowserWindowInterface* browser) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!browser) {
    return;
  }

  auto opener_it = opener_to_popup_.find(browser);
  if (opener_it != opener_to_popup_.end()) {
    BrowserWindowInterface* popup = opener_it->second;
    popup_to_opener_.erase(popup);
    opener_to_popup_.erase(opener_it);
    if (popup && popup->GetWindow()) {
      popup->GetWindow()->Close();
    }
    return;
  }

  auto popup_it = popup_to_opener_.find(browser);
  if (popup_it != popup_to_opener_.end()) {
    opener_to_popup_.erase(popup_it->second);
    popup_to_opener_.erase(popup_it);
  }
}

}  // namespace maho
