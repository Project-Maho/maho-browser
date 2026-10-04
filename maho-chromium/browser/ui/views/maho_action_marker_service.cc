// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/maho_action_marker_service.h"

#include <algorithm>
#include <cmath>
#include <memory>

#include "base/functional/bind.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_id.h"
#include "ui/gfx/animation/animation.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/canvas.h"
#include "ui/views/view.h"

namespace maho {
namespace {

constexpr int kMarkerDiameter = 40;
constexpr int kStaticMarkerDiameter = 28;
constexpr float kInnerRadius = 4.0f;
constexpr float kRingStrokeWidth = 2.0f;

class MahoActionMarkerView : public views::View {
  METADATA_HEADER(MahoActionMarkerView, views::View)

 public:
  MahoActionMarkerView(MahoActionMarkerService::Kind kind, bool sensitive)
      : kind_(kind), sensitive_(sensitive) {
    SetCanProcessEventsWithinSubtree(false);
    GetViewAccessibility().SetIsIgnored(true);
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
  }

  MahoActionMarkerView(const MahoActionMarkerView&) = delete;
  MahoActionMarkerView& operator=(const MahoActionMarkerView&) = delete;
  ~MahoActionMarkerView() override = default;

  void SetProgress(double progress, bool rich_motion) {
    progress_ = std::clamp(progress, 0.0, 1.0);
    rich_motion_ = rich_motion;
    SchedulePaint();
  }

  void OnPaint(gfx::Canvas* canvas) override {
    views::View::OnPaint(canvas);

    const auto* color_provider = GetColorProvider();
    const SkColor accent = color_provider
                               ? color_provider->GetColor(ui::kColorSysPrimary)
                               : SkColorSetRGB(0x36, 0x6C, 0xF4);
    const gfx::PointF center(GetLocalBounds().CenterPoint());

    // Reduced motion is a single static ring. Rich motion expands and fades a
    // bounded ring while retaining a stable center point.
    const double eased = 1.0 - std::pow(1.0 - progress_, 2.0);
    const float radius = rich_motion_
                             ? 8.0f + static_cast<float>(eased) * 10.0f
                             : 10.0f;
    const int alpha = rich_motion_
                          ? std::clamp(static_cast<int>(255.0 *
                                                       (1.0 - progress_)),
                                       0, 255)
                          : 220;

    cc::PaintFlags ring;
    ring.setAntiAlias(true);
    ring.setStyle(cc::PaintFlags::kStroke_Style);
    ring.setStrokeWidth(kRingStrokeWidth);
    ring.setColor(SkColorSetA(accent, alpha));
    canvas->DrawCircle(center, radius, ring);

    // Sensitive targets deliberately use position only: no kind glyph or text.
    if (sensitive_) {
      return;
    }

    cc::PaintFlags center_flags;
    center_flags.setAntiAlias(true);
    center_flags.setStyle(cc::PaintFlags::kFill_Style);
    center_flags.setColor(SkColorSetA(accent, std::min(alpha, 220)));
    canvas->DrawCircle(center, kInnerRadius, center_flags);

    if (kind_ == MahoActionMarkerService::Kind::kFocus) {
      cc::PaintFlags focus_ring = ring;
      focus_ring.setStrokeWidth(1.0f);
      focus_ring.setColor(SkColorSetA(accent, std::min(alpha, 150)));
      canvas->DrawCircle(center, radius - 4.0f, focus_ring);
    }
  }

 private:
  const MahoActionMarkerService::Kind kind_;
  const bool sensitive_;
  double progress_ = 0.0;
  bool rich_motion_ = false;
};

BEGIN_METADATA(MahoActionMarkerView)
END_METADATA

}  // namespace

WEB_CONTENTS_USER_DATA_KEY_IMPL(MahoActionMarkerService);

MahoActionMarkerService::MahoActionMarkerService(
    content::WebContents* web_contents)
    : content::WebContentsObserver(web_contents),
      content::WebContentsUserData<MahoActionMarkerService>(*web_contents) {}

MahoActionMarkerService::~MahoActionMarkerService() {
  Clear();
}

// static
bool MahoActionMarkerService::ShowForRevalidatedTarget(
    content::WebContents* web_contents,
    const Request& request) {
  if (!web_contents) {
    return false;
  }
  MahoActionMarkerService::CreateForWebContents(web_contents);
  MahoActionMarkerService* service =
      MahoActionMarkerService::FromWebContents(web_contents);
  return service && service->Show(request);
}

// static
bool MahoActionMarkerService::OperationGetsSpatialMarker(
    bool is_reading,
    std::optional<Kind> action_kind) {
  return !is_reading && action_kind.has_value();
}

// static
gfx::Rect MahoActionMarkerService::ComputeMarkerBoundsForTesting(
    const gfx::Size& contents_size,
    const gfx::PointF& viewport_point,
    bool rich_motion) {
  if (contents_size.IsEmpty()) {
    return gfx::Rect();
  }
  const int diameter = rich_motion ? kMarkerDiameter : kStaticMarkerDiameter;
  const gfx::Point center(std::lround(viewport_point.x()),
                          std::lround(viewport_point.y()));
  gfx::Rect bounds(center.x() - diameter / 2, center.y() - diameter / 2,
                   diameter, diameter);
  bounds.AdjustToFit(gfx::Rect(contents_size));
  return bounds;
}

bool MahoActionMarkerService::Show(const Request& request) {
  if (!IsTargetStillValid(request)) {
    Clear();
    return false;
  }

  Browser* browser = static_cast<Browser*>(
      GlobalBrowserCollection::GetInstance()->FindBrowserWithTab(
          web_contents()));
  BrowserView* browser_view =
      browser ? BrowserView::GetBrowserViewForBrowser(browser) : nullptr;
  views::View* contents_view =
      browser_view ? browser_view->contents_web_view() : nullptr;
  if (!contents_view || contents_view->bounds().IsEmpty()) {
    Clear();
    return false;
  }

  RemoveMarkerView();
  sensitive_ = request.sensitive;
  uses_rich_motion_ = !gfx::Animation::PrefersReducedMotion() &&
                      gfx::Animation::ShouldRenderRichAnimation();

  const gfx::Rect bounds = ComputeMarkerBoundsForTesting(
      contents_view->bounds().size(), request.viewport_point,
      uses_rich_motion_);

  auto marker =
      std::make_unique<MahoActionMarkerView>(request.kind, request.sensitive);
  marker->SetBoundsRect(bounds);
  MahoActionMarkerView* marker_ptr = marker.get();
  marker_view_.SetView(contents_view->AddChildView(std::move(marker)));

  if (uses_rich_motion_) {
    marker_ptr->SetProgress(0.0, true);
    pulse_animation_.Start();
  } else {
    marker_ptr->SetProgress(0.0, false);
  }

  dismiss_timer_.Start(
      FROM_HERE, kMarkerLifetime,
      base::BindOnce(&MahoActionMarkerService::ExpireForTesting,
                     weak_factory_.GetWeakPtr()));
  return true;
}

bool MahoActionMarkerService::IsTargetStillValid(const Request& request) const {
  content::WebContents* contents = web_contents();
  if (!contents || contents->GetVisibility() != content::Visibility::VISIBLE ||
      !request.primary_frame_id) {
    return false;
  }

  content::RenderFrameHost* primary = contents->GetPrimaryMainFrame();
  if (!primary || primary->GetGlobalId() != request.primary_frame_id) {
    return false;
  }

  Browser* browser = static_cast<Browser*>(
      GlobalBrowserCollection::GetInstance()->FindBrowserWithTab(contents));
  return browser && browser->GetTabStripModel() &&
         browser->GetTabStripModel()->GetActiveWebContents() == contents;
}

void MahoActionMarkerService::Clear() {
  dismiss_timer_.Stop();
  pulse_animation_.Stop();
  RemoveMarkerView();
  sensitive_ = false;
  uses_rich_motion_ = false;
}

void MahoActionMarkerService::RemoveMarkerView() {
  views::View* marker = marker_view_.view();
  marker_view_.SetView(nullptr);
  if (marker && marker->parent()) {
    marker->parent()->RemoveChildViewT(marker);
  }
}

bool MahoActionMarkerService::IsVisibleForTesting() const {
  return marker_view_.view() && marker_view_.view()->GetVisible();
}

gfx::Rect MahoActionMarkerService::MarkerBoundsInContentsForTesting() const {
  return marker_view_.view() ? marker_view_.view()->bounds() : gfx::Rect();
}

bool MahoActionMarkerService::IsSensitiveForTesting() const {
  return sensitive_;
}

bool MahoActionMarkerService::UsesRichMotionForTesting() const {
  return uses_rich_motion_;
}

void MahoActionMarkerService::FreezeForTesting() {
  dismiss_timer_.Stop();
  pulse_animation_.Stop();
}

void MahoActionMarkerService::ExpireForTesting() {
  Clear();
}

void MahoActionMarkerService::DidFinishNavigation(
    content::NavigationHandle* navigation_handle) {
  if (navigation_handle->IsInPrimaryMainFrame() &&
      navigation_handle->HasCommitted()) {
    Clear();
  }
}

void MahoActionMarkerService::OnVisibilityChanged(
    content::Visibility visibility) {
  if (visibility != content::Visibility::VISIBLE) {
    Clear();
  }
}

void MahoActionMarkerService::WebContentsDestroyed() {
  Clear();
}

void MahoActionMarkerService::AnimationProgressed(
    const gfx::Animation* animation) {
  if (animation != &pulse_animation_ || !marker_view_.view()) {
    return;
  }
  static_cast<MahoActionMarkerView*>(marker_view_.view())
      ->SetProgress(animation->GetCurrentValue(), true);
}

}  // namespace maho
