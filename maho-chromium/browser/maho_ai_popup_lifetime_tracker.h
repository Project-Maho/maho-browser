// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_MAHO_AI_POPUP_LIFETIME_TRACKER_H_
#define MAHO_BROWSER_MAHO_AI_POPUP_LIFETIME_TRACKER_H_

#include "base/containers/flat_map.h"
#include "base/no_destructor.h"
#include "base/scoped_observation.h"
#include "base/sequence_checker.h"
#include "chrome/browser/ui/browser_window/public/browser_collection_observer.h"

class BrowserWindowInterface;
class GlobalBrowserCollection;

namespace maho {

// Tracks opener→popup relationships for AI floating popups so that closing
// the originating normal browser automatically cascades to the popup.
//
// Lifecycle:
//   1. MahoAIPageHandler::SetViewMode(kFloating) calls TrackPopup() after
//      creating the popup Browser.
//   2. If the opener browser closes first, OnBrowserClosed() closes the popup.
//   3. If the popup closes first, OnBrowserClosed() removes the mapping.
//
// Singleton, same pattern as MahoTabRegistry / MahoSpaceProfileBridge.
class MahoAiPopupLifetimeTracker : public BrowserCollectionObserver {
 public:
  static MahoAiPopupLifetimeTracker* Get();

  MahoAiPopupLifetimeTracker(const MahoAiPopupLifetimeTracker&) = delete;
  MahoAiPopupLifetimeTracker& operator=(const MahoAiPopupLifetimeTracker&) =
      delete;

  // Register an opener→popup relationship. Both must be non-null.
  void TrackPopup(BrowserWindowInterface* opener, BrowserWindowInterface* popup);

  // Returns the popup tracked for |opener|, or nullptr.
  BrowserWindowInterface* GetPopupForOpener(
      BrowserWindowInterface* opener) const;

  // Returns the opener tracked for |popup|, or nullptr.
  BrowserWindowInterface* GetOpenerForPopup(
      BrowserWindowInterface* popup) const;

  // BrowserCollectionObserver:
  void OnBrowserCreated(BrowserWindowInterface* browser) override;
  void OnBrowserClosed(BrowserWindowInterface* browser) override;

 private:
  friend class base::NoDestructor<MahoAiPopupLifetimeTracker>;
  MahoAiPopupLifetimeTracker();
  ~MahoAiPopupLifetimeTracker() override;

  // opener → popup
  base::flat_map<BrowserWindowInterface*, BrowserWindowInterface*>
      opener_to_popup_;
  // popup → opener (reverse index for fast cleanup on popup close)
  base::flat_map<BrowserWindowInterface*, BrowserWindowInterface*>
      popup_to_opener_;

  base::ScopedObservation<GlobalBrowserCollection, BrowserCollectionObserver>
      browser_collection_observation_{this};

  SEQUENCE_CHECKER(sequence_checker_);
};

}  // namespace maho

#endif  // MAHO_BROWSER_MAHO_AI_POPUP_LIFETIME_TRACKER_H_
