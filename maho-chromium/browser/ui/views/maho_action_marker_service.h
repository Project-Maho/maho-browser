// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_MAHO_ACTION_MARKER_SERVICE_H_
#define MAHO_BROWSER_UI_VIEWS_MAHO_ACTION_MARKER_SERVICE_H_

#include <optional>

#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "content/public/browser/global_routing_id.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"
#include "ui/gfx/animation/animation_delegate.h"
#include "ui/gfx/animation/linear_animation.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/view_tracker.h"

namespace content {
class NavigationHandle;
class WebContents;
}  // namespace content

namespace maho {

// Browser-owned, non-DOM visual disclosure for a revalidated action target.
//
// The marker is a Views child above the renderer-backed contents view. It never
// creates a DOM node, runs page JavaScript, or moves the physical OS pointer.
// Renderer/page screenshot capture therefore excludes it by construction.
class MahoActionMarkerService
    : public content::WebContentsObserver,
      public content::WebContentsUserData<MahoActionMarkerService>,
      public gfx::AnimationDelegate {
 public:
  enum class Kind {
    kClick,
    kHover,
    kFocus,
  };

  struct Request {
    // CSS viewport coordinates from the same target revalidation used for the
    // browser action. No selector, accessible name, value, or tool argument is
    // accepted by this API.
    gfx::PointF viewport_point;
    Kind kind = Kind::kClick;
    bool sensitive = false;
    content::GlobalRenderFrameHostId primary_frame_id;
  };

  static constexpr base::TimeDelta kMarkerLifetime = base::Milliseconds(700);

  ~MahoActionMarkerService() override;

  MahoActionMarkerService(const MahoActionMarkerService&) = delete;
  MahoActionMarkerService& operator=(const MahoActionMarkerService&) = delete;

  // Shows a marker only when `request` still names the current primary page,
  // the WebContents is visible, and it remains the active tab in its Browser.
  // This is deliberately separate from activity publication: reading
  // operations update persistent chrome but have no spatial marker.
  static bool ShowForRevalidatedTarget(content::WebContents* web_contents,
                                       const Request& request);

  static bool OperationGetsSpatialMarker(bool is_reading,
                                         std::optional<Kind> action_kind);
  static gfx::Rect ComputeMarkerBoundsForTesting(
      const gfx::Size& contents_size,
      const gfx::PointF& viewport_point,
      bool rich_motion);

  void Clear();
  bool IsVisibleForTesting() const;
  gfx::Rect MarkerBoundsInContentsForTesting() const;
  bool IsSensitiveForTesting() const;
  bool UsesRichMotionForTesting() const;
  void FreezeForTesting();
  void ExpireForTesting();

  // content::WebContentsObserver:
  void DidFinishNavigation(
      content::NavigationHandle* navigation_handle) override;
  void OnVisibilityChanged(content::Visibility visibility) override;
  void WebContentsDestroyed() override;

  // gfx::AnimationDelegate:
  void AnimationProgressed(const gfx::Animation* animation) override;

 private:
  friend class content::WebContentsUserData<MahoActionMarkerService>;

  explicit MahoActionMarkerService(content::WebContents* web_contents);

  bool Show(const Request& request);
  bool IsTargetStillValid(const Request& request) const;
  void RemoveMarkerView();

  views::ViewTracker marker_view_;
  gfx::LinearAnimation pulse_animation_{kMarkerLifetime, 60, this};
  base::OneShotTimer dismiss_timer_;
  bool sensitive_ = false;
  bool uses_rich_motion_ = false;
  base::WeakPtrFactory<MahoActionMarkerService> weak_factory_{this};

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_MAHO_ACTION_MARKER_SERVICE_H_
