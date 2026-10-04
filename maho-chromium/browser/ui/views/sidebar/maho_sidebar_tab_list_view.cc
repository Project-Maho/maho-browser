// Copyright 2026 Maho Browser. All rights reserved.

#include "maho_sidebar_tab_list_view.h"
#include "ui/views/controls/button/image_button.h"

#include "maho/browser/ui/views/sidebar/maho_sidebar_view.h"
#include "maho/browser/maho_tab_registry.h"

#include <functional>
#include <algorithm>
#include <memory>
#include <set>
#include <unordered_set>
#include <utility>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/check_op.h"
#include "base/auto_reset.h"
#include "base/timer/elapsed_timer.h"
#include "base/trace_event/trace_event.h"
#include "base/functional/callback_helpers.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/json/string_escape.h"
#include "base/pickle.h"
#include "base/strings/string_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "base/values.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_navigator.h"
#include "chrome/browser/ui/browser_navigator_params.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/side_panel/side_panel_enums.h"
#include "chrome/browser/ui/side_panel/side_panel_entry_id.h"
#include "chrome/browser/ui/side_panel/side_panel_entry_key.h"
#include "chrome/browser/ui/side_panel/side_panel_ui.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/location_bar/location_bar_view.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/common/url_constants.h"
#include "chrome/browser/favicon/favicon_service_factory.h"
#include "components/favicon/core/favicon_service.h"
#include "components/favicon_base/favicon_types.h"
#include "components/keyed_service/core/service_access_type.h"
#include "maho/browser/ui/theme/maho_color_id.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_container_view.h"
#include "maho/browser/ui/notifications/maho_notification_overlay.h"
#include "content/public/browser/page_navigator.h"
#include "content/public/common/referrer.h"
#include "chrome/browser/ui/tab_ui_helper.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_muted_utils.h"
#include "chrome/browser/ui/tabs/split_tab_metrics.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/profiles/profile.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "components/tabs/public/tab_interface.h"
#include "maho/browser/ui/tab_preview/maho_tab_preview_controller.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_folder_hover_controller.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "components/tabs/public/split_tab_data.h"
#include "maho/browser/ui/context_menu/maho_folder_context_menu.h"
#include "maho/browser/ui/context_menu/maho_space_context_menu.h"
#include "maho/browser/ui/context_menu/maho_tab_context_menu.h"
#include "maho/browser/ai/maho_tab_tidy_orchestrator.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/maho_private_context_policy.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/browser/maho_tab_id_helper.h"
#include "maho/browser/ui/views/split_view/maho_split_view_controller.h"
#include "maho/browser/maho_tab_preview_capture.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_dnd_events.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_state_adapter.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/base/clipboard/clipboard_format_type.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/accessibility/ax_virtual_view.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/dragdrop/mojom/drag_drop_types.mojom.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/base/models/image_model.h"
#include "ui/accessibility/platform/ax_platform.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/compositor.h"
#include "ui/compositor/layer_tree_owner.h"
#include "ui/compositor/scoped_layer_animation_settings.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/display/screen.h"
#include "ui/base/window_open_disposition.h"
#include "ui/base/dragdrop/drop_target_event.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/geometry/point.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/events/event.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font_list.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/image_button_factory.h"
#include "maho/browser/ui/views/maho_lucide_icons/vector_icons.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/box_layout_view.h"
#include "ui/views/layout/layout_manager_base.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_visibility_manager.h"
#include "ui/views/style/typography.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/view_utils.h"
#include "ui/views/vector_icons.h"
#include "url/gurl.h"
#include "base/timer/timer.h"
#include "cc/paint/paint_flags.h"
#include "ui/views/controls/scroll_view.h"

#include "third_party/skia/include/core/SkColor.h"

#include "maho/browser/ui/views/sidebar/maho_sidebar_drag_util.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_drop_planner.h"
#include "maho/components/constants/webui_url_constants.h"

namespace maho {
void ForwardDragToAutoScroller(views::View* child,
                               const ui::DropTargetEvent& event);
void ForwardDragEnded(views::View* child);
}  // namespace maho

namespace {

using namespace maho;  // NOLINT(build/namespaces)

bool ShouldBuildSidebarVirtualAccessibilityTree() {
  return ui::AXPlatform::GetInstance().IsScreenReaderActive();
}

constexpr int kRowSpacingDp = 0;
constexpr int kRowHeightDp = 36;
constexpr int kRowCornerRadiusDp = 6;
constexpr int kRowIconSizeDp = 17;
constexpr int kCloseButtonSizeDp = 24;
constexpr int kCloseIconSizeDp = 15;
constexpr int kRowIconTextSpacingDp = 12;
constexpr int kRowTrailingSpacingDp = 0;
constexpr int kFolderIndentDp = 15;
constexpr int kFolderRowHeightDp = 36;
constexpr int kSectionSeparatorHeightDp = 14;
constexpr int kSectionSeparatorLineHeightDp = 1;
constexpr int kFolderGlyphSizeDp = 14;
constexpr int kAutoScrollEdgeDp = 24;
constexpr int kAutoScrollStepDp = 4;
constexpr int kAutoScrollHz = 30;
constexpr int kFolderGlyphContainerSizeDp = 18;
constexpr int kFolderRowLeadingInsetDp = 8;
constexpr int kDragStartThresholdDp = 5;
constexpr int kDropIndicatorThicknessDp = 2;
constexpr int kDropIndicatorOutlineThicknessDp = 1;
constexpr int kSplitPreviewAnimationMs = 170;
constexpr int kControlledTabPulseAnimationMs = 650;
constexpr int kSplitPreviewDividerInsetDp = 4;
constexpr int kSplitPreviewDividerWidthDp = 2;
constexpr int kInlineActionIconSizeDp = 17;
constexpr int kInlineActionsSpacingDp = 6;
[[maybe_unused]] constexpr int kAudioIndicatorSizeDp = 14;

const gfx::Insets kRootInsets = gfx::Insets::TLBR(0, 0, 0, 0);
const gfx::Insets kInlineRowInsets = gfx::Insets::TLBR(0, 10, 0, 8);
const gfx::Insets kNewTabRowMargins = gfx::Insets::TLBR(0, 0, 0, 0);
const gfx::Insets kSectionInsets = gfx::Insets::TLBR(0, 0, 0, 0);
const gfx::Insets kSectionSeparatorInsets = gfx::Insets::TLBR(2, 8, 2, 8);

SkColor ResolveSidebarPaletteColor(const MahoSidebarPalette& palette,
                                   const views::View* view,
                                   SkColor palette_color,
                                   ui::ColorId fallback_color_id) {
  if (palette.primary_text != SK_ColorTRANSPARENT) {
    return palette_color;
  }
  const ui::ColorProvider* color_provider = view->GetColorProvider();
  return color_provider ? color_provider->GetColor(fallback_color_id)
                        : SK_ColorTRANSPARENT;
}

class SidebarSplitDropTargetBackground : public views::Background {
 public:
  explicit SidebarSplitDropTargetBackground(
      base::RepeatingCallback<double()> progress_callback,
      base::RepeatingCallback<MahoSplitDropSide()> preview_side_callback,
      SkColor active_color,
      SkColor divider_color,
      SkColor focus_color)
      : progress_callback_(std::move(progress_callback)),
        preview_side_callback_(std::move(preview_side_callback)),
        active_color_(active_color),
        divider_color_(divider_color),
        focus_color_(focus_color) {}

  void Paint(gfx::Canvas* canvas, views::View* view) const override {
    const gfx::Rect bounds = view->GetLocalBounds();
    if (bounds.IsEmpty()) {
      return;
    }

    const double progress =
        std::clamp(progress_callback_.Run(), 0.0, 1.0);
    if (progress <= 0.0) {
      return;
    }

    const bool incoming_first =
        preview_side_callback_.Run() == MahoSplitDropSide::kLeft;
    const float half_width =
        (bounds.width() - kSplitPreviewDividerWidthDp) / 2.0f;
    const float split_x = bounds.x() + half_width;
    const gfx::RectF incoming_bounds(
        incoming_first ? static_cast<float>(bounds.x())
                       : split_x + kSplitPreviewDividerWidthDp,
        static_cast<float>(bounds.y()), half_width,
        static_cast<float>(bounds.height()));
    const gfx::RectF target_bounds(
        incoming_first ? split_x + kSplitPreviewDividerWidthDp
                       : static_cast<float>(bounds.x()),
        static_cast<float>(bounds.y()), half_width,
        static_cast<float>(bounds.height()));

    cc::PaintFlags target_flags;
    target_flags.setAntiAlias(true);
    target_flags.setStyle(cc::PaintFlags::kFill_Style);
    target_flags.setColor(ApplyOpacity(active_color_, progress));
    canvas->DrawRoundRect(target_bounds, kRowCornerRadiusDp, target_flags);

    cc::PaintFlags incoming_flags;
    incoming_flags.setAntiAlias(true);
    incoming_flags.setStyle(cc::PaintFlags::kFill_Style);
    incoming_flags.setColor(ApplyOpacity(active_color_, progress));
    canvas->DrawRoundRect(incoming_bounds, kRowCornerRadiusDp, incoming_flags);

    const float full_divider_height =
        bounds.height() - (kSplitPreviewDividerInsetDp * 2.0f);
    if (full_divider_height > 0.0f) {
      const float divider_height =
          full_divider_height * static_cast<float>(progress);
      cc::PaintFlags divider_flags;
      divider_flags.setAntiAlias(true);
      divider_flags.setStyle(cc::PaintFlags::kFill_Style);
      divider_flags.setColor(ApplyOpacity(divider_color_, progress));
      const float divider_x =
          bounds.x() + ((bounds.width() - kSplitPreviewDividerWidthDp) / 2.0f);
      const float divider_y =
          bounds.y() + (bounds.height() - divider_height) / 2.0f;
      canvas->DrawRoundRect(
          gfx::RectF(divider_x, divider_y, kSplitPreviewDividerWidthDp,
                     divider_height),
          kSplitPreviewDividerWidthDp / 2.0f, divider_flags);
    }

    cc::PaintFlags border_flags;
    border_flags.setAntiAlias(true);
    border_flags.setStyle(cc::PaintFlags::kStroke_Style);
    border_flags.setStrokeWidth(kDropIndicatorOutlineThicknessDp);
    border_flags.setColor(ApplyOpacity(focus_color_, progress));
    gfx::RectF border_bounds(bounds);
    border_bounds.Inset(kDropIndicatorOutlineThicknessDp / 2.0f);
    canvas->DrawRoundRect(border_bounds, kRowCornerRadiusDp, border_flags);
  }

  void OnViewThemeChanged(views::View* view) override { view->SchedulePaint(); }

 private:
  static SkColor ApplyOpacity(SkColor color, double opacity) {
    const int alpha = std::clamp(
        static_cast<int>(SkColorGetA(color) * opacity + 0.5), 0, 255);
    return SkColorSetA(color, alpha);
  }

  base::RepeatingCallback<double()> progress_callback_;
  base::RepeatingCallback<MahoSplitDropSide()> preview_side_callback_;
  SkColor active_color_;
  SkColor divider_color_;
  SkColor focus_color_;
};

ui::DropTargetEvent ForwardDropTargetEventToView(const ui::DropTargetEvent& event,
                                                 views::View* source,
                                                 views::View* target) {
  gfx::Point target_location = event.location();
  views::View::ConvertPointToTarget(source, target, &target_location);
  return ui::DropTargetEvent(event.data(), gfx::PointF(target_location),
                             event.root_location_f(),
                             event.source_operations());
}

void NotifySidebarDragEnded(views::View* start_view) {
  for (views::View* v = start_view; v; v = v->parent()) {
    if (auto* sidebar_view = views::AsViewClass<MahoSidebarView>(v)) {
      sidebar_view->OnDragEnded();
      break;
    }
  }
}

void NotifySidebarDragStarted(views::View* start_view,
                               const std::string& tab_id) {
  for (views::View* v = start_view; v; v = v->parent()) {
    if (auto* sidebar_view = views::AsViewClass<MahoSidebarView>(v)) {
      sidebar_view->OnDragStarted(tab_id);
      break;
    }
  }
}

void NotifySidebarDragGhostImage(views::View* start_view,
                                 const gfx::ImageSkia& image,
                                 const gfx::Vector2d& offset) {
  for (views::View* v = start_view; v; v = v->parent()) {
    if (auto* sidebar_view = views::AsViewClass<MahoSidebarView>(v)) {
      sidebar_view->SetActiveDragGhostImage(image, offset);
      break;
    }
  }
}

// Virtual layout manager for tab_rows_ that culls off-screen children based
// on scroll position, reducing the active view working set.
class SidebarVirtualLayoutDelegate : public views::LayoutManagerBase {
 public:
  SidebarVirtualLayoutDelegate(SidebarVisibilityManager* mgr, int row_height)
      : visibility_mgr_(mgr), row_height_(row_height) {}

  bool OnViewAdded(views::View* host, views::View* view) override {
    cache_valid_ = false;
    return views::LayoutManagerBase::OnViewAdded(host, view);
  }

  bool OnViewRemoved(views::View* host, views::View* view) override {
    cache_valid_ = false;
    return views::LayoutManagerBase::OnViewRemoved(host, view);
  }

  bool OnViewVisibilitySet(views::View* host,
                           views::View* view,
                           bool visible) override {
    cache_valid_ = false;
    return views::LayoutManagerBase::OnViewVisibilitySet(host, view, visible);
  }

  // A child's preferred height can change without any child being added,
  // removed or re-shown (a section's window spacers resize, a folder expands).
  // That arrives here as a layout invalidation; the per-child heights cached
  // below are stale from that point on.
  void OnLayoutChanged() override {
    cache_valid_ = false;
    views::LayoutManagerBase::OnLayoutChanged();
  }

  // True when a child's current visibility no longer matches the cull decision
  // for the scroll viewport as it is NOW. The cull runs inside layout, but
  // ScrollView::Layout() lays out its contents before clamping the offset
  // (ConstrainScrollToBounds), and the clamp neither scrolls nor invalidates
  // this host. A rebuild that shrinks the list can therefore cull the action
  // row and the normal section against the pre-clamp offset and leave them
  // hidden in the settled viewport until an unrelated scroll or rebuild.
  bool HasStaleCulling() const {
    const views::View* host = host_view();
    if (!host || !cache_valid_) {
      return false;
    }
    const auto& children = host->children();
    if (cached_heights_.size() != children.size()) {
      return false;
    }
    const gfx::Rect visible_rect = GetViewportRect(host);
    const gfx::Rect expanded_rect = ExpandViewportRect(visible_rect);
    const int width = host->width();
    int y = 0;
    for (size_t i = 0; i < children.size(); ++i) {
      const bool visible =
          IsChildLaidOutVisible(children[i], gfx::Rect(0, y, width,
                                                       cached_heights_[i]),
                                visible_rect, expanded_rect);
      if (children[i]->GetVisible() != visible) {
        return true;
      }
      y += cached_heights_[i];
    }
    return false;
  }

  views::ProposedLayout CalculateProposedLayout(
      const views::SizeBounds& size_bounds) const override {
    views::ProposedLayout layout;
    const views::View* host = host_view();
    const auto& children = host->children();
    const int total = static_cast<int>(children.size());
    const int width = size_bounds.width().value_or(0);

    if (width != last_width_) {
      last_width_ = width;
      cache_valid_ = false;
    }

    const gfx::Rect visible_rect = GetViewportRect(host);

    if (!cache_valid_ || cached_heights_.size() != static_cast<size_t>(total)) {
      cached_heights_.clear();
      cached_heights_.reserve(total);
      total_height_ = 0;
      for (int i = 0; i < total; ++i) {
        // CanBeVisible() tracks visibility requested by product code, unlike
        // GetVisible(), which is also false when this layout virtualizes an
        // offscreen row. Externally hidden lanes must occupy 0dp, while
        // virtualized rows must retain their geometry and scroll extent.
        const int h = CanBeVisible(children[i])
                          ? children[i]
                                ->GetPreferredSize(views::SizeBounds(
                                    width, views::SizeBound()))
                                .height()
                          : 0;
        cached_heights_.push_back(h);
        total_height_ += h;
      }
      cache_valid_ = true;
    }

    layout.host_size = gfx::Size(width, total_height_);

    const gfx::Rect expanded_rect = ExpandViewportRect(visible_rect);

    int y = 0;
    layout.child_layouts.reserve(total);
    for (int i = 0; i < total; ++i) {
      views::ChildLayout child_layout;
      child_layout.child_view = children[i];
      child_layout.bounds = gfx::Rect(0, y, width, cached_heights_[i]);
      child_layout.visible = IsChildLaidOutVisible(
          children[i], child_layout.bounds, visible_rect, expanded_rect);
      layout.child_layouts.push_back(child_layout);
      y += cached_heights_[i];
    }

    return layout;
  }

  ~SidebarVirtualLayoutDelegate() override {
    visibility_mgr_ = nullptr;
  }

 private:
  static gfx::Rect GetViewportRect(const views::View* host) {
    if (auto* scroll = views::ScrollView::GetScrollViewForContents(
            const_cast<views::View*>(host->parent()))) {
      return scroll->GetVisibleRect();
    }
    return gfx::Rect();
  }

  // Expand the visible rect by the overscan buffer.
  gfx::Rect ExpandViewportRect(const gfx::Rect& visible_rect) const {
    const int buffer_dp = visibility_mgr_->is_drag_active()
        ? visibility_mgr_->config().drag_overscan_rows * row_height_
        : visibility_mgr_->config().buffer_rows_above * row_height_;
    gfx::Rect expanded_rect = visible_rect;
    if (!visible_rect.IsEmpty()) {
      expanded_rect.Inset(gfx::Insets::TLBR(-buffer_dp, 0, -buffer_dp, 0));
    }
    return expanded_rect;
  }

  bool IsChildLaidOutVisible(const views::View* child,
                             const gfx::Rect& bounds,
                             const gfx::Rect& visible_rect,
                             const gfx::Rect& expanded_rect) const {
    bool is_selected = false;
    if (auto* row = views::AsViewClass<SidebarTabRowView>(child)) {
      is_selected = row->is_selected();
    }
    return CanBeVisible(child) &&
           (is_selected || visible_rect.IsEmpty() ||
            expanded_rect.Intersects(bounds));
  }

  raw_ptr<SidebarVisibilityManager> visibility_mgr_ = nullptr;
  int row_height_;

  mutable std::vector<int> cached_heights_;
  mutable int total_height_ = 0;
  mutable int last_width_ = 0;
  mutable bool cache_valid_ = false;
};

class MahoInlineActionButton : public views::LabelButton {
  METADATA_HEADER(MahoInlineActionButton, views::LabelButton)

 public:
  MahoInlineActionButton(PressedCallback callback,
                         std::u16string text,
                         int corner_radius)
      : views::LabelButton(std::move(callback), std::move(text)),
        corner_radius_(corner_radius) {}

  MahoInlineActionButton(const MahoInlineActionButton&) = delete;
  MahoInlineActionButton& operator=(const MahoInlineActionButton&) = delete;
  ~MahoInlineActionButton() override = default;

  void StateChanged(ButtonState old_state) override {
    views::LabelButton::StateChanged(old_state);
    UpdateHoverBackground();
  }

  void OnThemeChanged() override {
    views::LabelButton::OnThemeChanged();
    UpdateHoverBackground();
  }

  void SetEngaged(bool engaged) {
    if (engaged_ == engaged) {
      return;
    }
    engaged_ = engaged;
    UpdateHoverBackground();
  }

  void SetSidebarPalette(const MahoSidebarPalette& palette) {
    palette_ = palette;
    UpdateHoverBackground();
  }

 private:
  void UpdateHoverBackground() {
    const ButtonState state = GetState();
    if (engaged_ || state == STATE_PRESSED) {
      SetBackground(views::CreateRoundedRectBackground(
          palette_.row_active, corner_radius_));
    } else if (state == STATE_HOVERED) {
      SetBackground(views::CreateRoundedRectBackground(
          palette_.row_hover, corner_radius_));
    } else {
      SetBackground(nullptr);
    }
  }

  int corner_radius_;
  bool engaged_ = false;
  MahoSidebarPalette palette_;
};

BEGIN_METADATA(MahoInlineActionButton)
END_METADATA

class SidebarDropForwardingLabelButton : public views::LabelButton {
 public:
  SidebarDropForwardingLabelButton(PressedCallback callback,
                                   std::u16string text,
                                   raw_ptr<views::View> drop_target)
      : views::LabelButton(std::move(callback), std::move(text)),
        drop_target_(drop_target) {}

  SidebarDropForwardingLabelButton(const SidebarDropForwardingLabelButton&) =
      delete;
  SidebarDropForwardingLabelButton& operator=(
      const SidebarDropForwardingLabelButton&) = delete;
  ~SidebarDropForwardingLabelButton() override = default;

  bool GetDropFormats(int* formats,
                      std::set<ui::ClipboardFormatType>* format_types) override {
    return drop_target_ && drop_target_->GetDropFormats(formats, format_types);
  }

  bool CanDrop(const ui::OSExchangeData& data) override {
    return drop_target_ && drop_target_->CanDrop(data);
  }

  int OnDragUpdated(const ui::DropTargetEvent& event) override {
    if (!drop_target_) {
      return static_cast<int>(ui::mojom::DragOperation::kNone);
    }
    return drop_target_->OnDragUpdated(
        ForwardDropTargetEventToView(event, this, drop_target_));
  }

  DropCallback GetDropCallback(const ui::DropTargetEvent& event) override {
    if (!drop_target_) {
      return views::LabelButton::GetDropCallback(event);
    }
    return drop_target_->GetDropCallback(
        ForwardDropTargetEventToView(event, this, drop_target_));
  }

  void OnDragExited() override {
    if (drop_target_) {
      drop_target_->OnDragExited();
    }
  }

  void OnDragDone() override {
    views::LabelButton::OnDragDone();
    NotifySidebarDragEnded(this);
  }

 private:
  raw_ptr<views::View> drop_target_ = nullptr;
};

// A focusable button must expose either a non-empty accessible name or be
// explicitly marked empty (ui/views RunAccessibilityPaintChecks DCHECK). A tab
// row's title button derives its name from its label text, which is
// transiently empty while a row is being torn down (e.g. the last child leaves
// an empty folder). Mark those transient states explicitly-empty so a paint
// during the transition does not trip the check.
void SyncButtonAccessibleName(views::LabelButton* button,
                              const std::u16string& text) {
  if (!button) {
    return;
  }
  if (text.empty()) {
    button->GetViewAccessibility().SetName(
        std::u16string(), ax::mojom::NameFrom::kAttributeExplicitlyEmpty);
  } else {
    button->GetViewAccessibility().SetName(text);
  }
}

class SidebarDropForwardingView : public views::View {
 public:
  explicit SidebarDropForwardingView(raw_ptr<views::View> drop_target)
      : drop_target_(drop_target) {}

  SidebarDropForwardingView(const SidebarDropForwardingView&) = delete;
  SidebarDropForwardingView& operator=(const SidebarDropForwardingView&) =
      delete;
  ~SidebarDropForwardingView() override = default;

  bool GetDropFormats(int* formats,
                      std::set<ui::ClipboardFormatType>* format_types) override {
    return drop_target_ && drop_target_->GetDropFormats(formats, format_types);
  }

  bool CanDrop(const ui::OSExchangeData& data) override {
    return drop_target_ && drop_target_->CanDrop(data);
  }

  int OnDragUpdated(const ui::DropTargetEvent& event) override {
    if (!drop_target_) {
      return static_cast<int>(ui::mojom::DragOperation::kNone);
    }
    return drop_target_->OnDragUpdated(
        ForwardDropTargetEventToView(event, this, drop_target_));
  }

  DropCallback GetDropCallback(const ui::DropTargetEvent& event) override {
    if (!drop_target_) {
      return views::View::GetDropCallback(event);
    }
    return drop_target_->GetDropCallback(
        ForwardDropTargetEventToView(event, this, drop_target_));
  }

  void OnDragExited() override {
    if (drop_target_) {
      drop_target_->OnDragExited();
    }
  }

 private:
  raw_ptr<views::View> drop_target_ = nullptr;
};

class SidebarSeparatorRow : public views::View {
  METADATA_HEADER(SidebarSeparatorRow, views::View)

 public:
  SidebarSeparatorRow() {
    SetPreferredSize(gfx::Size(1, kSectionSeparatorHeightDp));
    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, kSectionSeparatorInsets,
        0));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);

    auto divider = std::make_unique<views::View>();
    divider->SetPreferredSize(gfx::Size(0, kSectionSeparatorLineHeightDp));
    divider_ = AddChildView(std::move(divider));
  }

  SidebarSeparatorRow(const SidebarSeparatorRow&) = delete;
  SidebarSeparatorRow& operator=(const SidebarSeparatorRow&) = delete;
  ~SidebarSeparatorRow() override = default;

  void SetSidebarPalette(const MahoSidebarPalette& palette) {
    palette_ = palette;
    UpdateDivider();
  }

  void OnThemeChanged() override {
    views::View::OnThemeChanged();
    UpdateDivider();
  }

 private:
  void UpdateDivider() {
    divider_->SetBackground(views::CreateSolidBackground(
        ResolveSidebarPaletteColor(palette_, this, palette_.outline,
                                   ui::kColorSysNeutralOutline)));
  }

  raw_ptr<views::View> divider_ = nullptr;
  MahoSidebarPalette palette_;
};

class MahoSidebarActionRowView : public views::View {
  METADATA_HEADER(MahoSidebarActionRowView, views::View)

 public:
  MahoSidebarActionRowView() {
    SetLayoutManager(std::make_unique<views::FillLayout>());
  }
  MahoSidebarActionRowView(const MahoSidebarActionRowView&) = delete;
  MahoSidebarActionRowView& operator=(const MahoSidebarActionRowView&) = delete;
  ~MahoSidebarActionRowView() override = default;

  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(0, 42);
  }
};

// Leading/trailing window spacer that PAINTS a faint row-rhythm skeleton across
// its region. It absorbs the culled extent (its height is set by
// ReconcileSectionWindow) and, if a fling frame ever briefly exposes the
// spacer region before realization catches up, shows neutral placeholder rows
// instead of blank/torn content. Paint-only: no layer, no child views, so it
// does not affect the realized-layer ceiling.
class SidebarSkeletonSpacerView : public views::View {
  METADATA_HEADER(SidebarSkeletonSpacerView, views::View)

 public:
  SidebarSkeletonSpacerView() = default;
  SidebarSkeletonSpacerView(const SidebarSkeletonSpacerView&) = delete;
  SidebarSkeletonSpacerView& operator=(const SidebarSkeletonSpacerView&) =
      delete;
  ~SidebarSkeletonSpacerView() override = default;

  // Barely-visible separators on the 36dp row rhythm, tinted from the
  // palette outline role.
  void SetSidebarPalette(const MahoSidebarPalette& palette) {
    palette_ = palette;
    SchedulePaint();
  }

  void OnPaint(gfx::Canvas* canvas) override {
    views::View::OnPaint(canvas);
    const int h = height();
    const int w = width();
    const SkColor line = SkColorSetA(
        ResolveSidebarPaletteColor(palette_, this, palette_.outline,
                                   ui::kColorSysNeutralOutline),
        0x14);
    if (SkColorGetA(line) == 0 || h <= 0 || w <= 0) {
      return;
    }
    // This spacer absorbs the full culled section extent, which can be tens of
    // thousands of dp. Clip the paint loop to the viewport clip so scroll
    // painting records O(visible rows), not O(total tabs), draw ops.
    gfx::Rect clip;
    if (!canvas->GetClipBounds(&clip) || clip.IsEmpty()) {
      return;
    }

    const int first =
        std::max(kRowHeightDp, (clip.y() / kRowHeightDp) * kRowHeightDp);
    const int limit = std::min(h, clip.bottom() + 1);
    for (int y = first; y < limit; y += kRowHeightDp) {
      canvas->FillRect(gfx::Rect(0, y, w, 1), line);
    }
  }

 private:
  MahoSidebarPalette palette_;
};

BEGIN_METADATA(SidebarSkeletonSpacerView)
END_METADATA

}  // namespace

namespace maho {

base::DictValue MakeRootItem(SidebarNodeKind kind, const std::string& id);
base::DictValue MakeInsertionPointBefore(SidebarNodeKind target_kind,
                                         const std::string& target_id);
base::DictValue MakeInsertionPointAppend();
void DispatchReorderRootItem(const std::string& space_id,
                             SidebarNodeKind item_kind,
                             const std::string& item_id,
                             base::DictValue insertion_point);

// Defined in namespace maho (not the anonymous namespace) so it is the same
// type as the `friend class SidebarSectionDropTarget;` declaration in
// MahoSidebarTabListView. Under clang-cl's MSVC compatibility mode that friend
// declaration is visible to ordinary name lookup, so an anonymous-namespace
// definition would leave the maho:: name incomplete at the static_cast sites
// below and fail to compile on Windows.
class SidebarSectionDropTarget : public views::View {
 public:
  explicit SidebarSectionDropTarget(
      MahoSidebarTabSection section,
      std::string active_space_id,
      base::WeakPtr<MahoSidebarTabListView> tab_list_view)
      : section_(section),
        active_space_id_(std::move(active_space_id)),
        tab_list_view_(tab_list_view) {
    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, kSectionInsets,
        kRowSpacingDp));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);
    SetBackground(nullptr);
    SetBorder(nullptr);
  }

  void SetFavoritesDropAcceptedCallback(
      base::RepeatingCallback<void(const std::string&)> cb) {
    on_favorites_drop_accepted_ = std::move(cb);
  }

  bool GetDropFormats(int* formats,
                      std::set<ui::ClipboardFormatType>* format_types) override {
    format_types->insert(GetMahoDragFormatType());
    return true;
  }

  bool CanDrop(const ui::OSExchangeData& data) override {
    SidebarDragPayload payload;
    return ReadMahoDragData(data, payload);
  }

  int OnDragUpdated(const ui::DropTargetEvent& event) override {
    ForwardDragToAutoScroller(this, event);
    drag_over_drop_target_ = true;
    UpdateAppearance();
    return static_cast<int>(ui::mojom::DragOperation::kMove);
  }

  DropCallback GetDropCallback(const ui::DropTargetEvent& event) override {
    ForwardDragEnded(this);
    return base::BindOnce(
        [](base::WeakPtr<MahoSidebarTabListView> tab_list_view,
           MahoSidebarTabSection section,
           std::string active_space_id,
           base::RepeatingCallback<void(const std::string&)>
               on_favorites_drop_accepted,
           const ui::DropTargetEvent& event,
           ui::mojom::DragOperation& output_drag_op,
           std::unique_ptr<ui::LayerTreeOwner> drag_image_layer_owner) {
          SidebarDragPayload payload;
          if (!ReadMahoDragData(event.data(), payload)) {
            output_drag_op = ui::mojom::DragOperation::kNone;
            return;
          }
          output_drag_op = ui::mojom::DragOperation::kMove;

          const SidebarDropPlan plan = PlanSectionDrop(section, payload);
          if (!plan.is_valid) {
            output_drag_op = ui::mojom::DragOperation::kNone;
            return;
          }

          // This view is only a section's background fallback; child rows and
          // their 2dp lanes own precise hit targets. Folder-child tab/folder
          // escapes already append via move_*_to_root, so suppress this generic
          // append to avoid duplicate root_order and stale parent/root entries.
          const bool tab_escaped_to_root =
              payload.node_kind == SidebarNodeKind::kTab &&
              !payload.source_parent_folder_id.empty();
          const bool folder_escaped_to_root =
              payload.node_kind == SidebarNodeKind::kFolder &&
              !payload.source_parent_folder_id.empty();
          if (folder_escaped_to_root) {
            DispatchShellEvent(maho::sidebar::kMoveFolderToRoot,
                               {{"space_id", active_space_id},
                                {"folder_id", payload.node_id}});
          }

          // Apply the native TabStripModel role before dispatching the core
          // event. DispatchShellEvent is synchronous and may rebuild the
          // sidebar, invalidating the source row/index while this drop callback
          // is still running. Hover feedback alone is not drop completion.
          if (tab_list_view) {
            tab_list_view->HandlePostDropTransition(payload, plan);
          }
          ExecuteDropPlan(plan);

          if (plan.favorite_transition.kind ==
                  SidebarDropPlan::FavoriteTransition::Kind::kUnfavorite &&
              on_favorites_drop_accepted) {
            on_favorites_drop_accepted.Run(payload.node_id);
          }

          if (payload.node_kind == SidebarNodeKind::kTab &&
              !payload.node_id.empty() && !tab_escaped_to_root) {
            DispatchReorderRootItem(active_space_id, SidebarNodeKind::kTab,
                                    payload.node_id,
                                    MakeInsertionPointAppend());
          } else if (payload.node_kind == SidebarNodeKind::kFolder &&
                     !payload.node_id.empty() && !folder_escaped_to_root) {
            DispatchReorderRootItem(active_space_id, SidebarNodeKind::kFolder,
                                    payload.node_id,
                                    MakeInsertionPointAppend());
          }
        },
        tab_list_view_, section_, active_space_id_,
        on_favorites_drop_accepted_);
  }

  void OnDragExited() override {
    ForwardDragEnded(this);
    drag_over_drop_target_ = false;
    UpdateAppearance();
  }

  void SetMinDropTargetHeight(int height_dp) {
    SetPreferredSize(gfx::Size(0, height_dp));
  }

  bool is_drop_indicator_visible_for_testing() const {
    return drag_over_drop_target_;
  }

  MahoSidebarTabSection section_for_testing() const { return section_; }

 private:
  void UpdateAppearance() {
    if (!drag_over_drop_target_ || !tab_list_view_) {
      SetBackground(nullptr);
      SetBorder(nullptr);
      return;
    }
    const MahoSidebarPalette& palette = tab_list_view_->palette_;
    SetBackground(views::CreateRoundedRectBackground(
        ResolveSidebarPaletteColor(palette, this, palette.row_active,
                                   ui::kColorSysSurface4),
        kRowCornerRadiusDp));
    SetBorder(views::CreateRoundedRectBorder(
        kDropIndicatorOutlineThicknessDp, kRowCornerRadiusDp,
        ResolveSidebarPaletteColor(palette, this, palette.focus_ring,
                                   ui::kColorSysPrimary)));
  }

  const MahoSidebarTabSection section_;
  const std::string active_space_id_;
  base::WeakPtr<MahoSidebarTabListView> tab_list_view_;
  bool drag_over_drop_target_ = false;
  base::RepeatingCallback<void(const std::string&)> on_favorites_drop_accepted_;
};

}  // namespace maho

namespace {

bool IsSidebarEligibleUrl(const GURL& url) {
  if (!url.is_valid() || url.is_empty()) {
    return false;
  }
  if (url.SchemeIs("about")) {
    return false;
  }
  if (url.SchemeIs("chrome")) {
    // Maho Mail is reached through the dedicated sidebar control button, so its
    // tab is kept out of the list for the same reason as downloads/history.
    if (url.host() == "newtab" || url.host() == "downloads" ||
        url.host() == "history" || url.host() == maho::kMahoMailHost) {
      return false;
    }
  }
  return true;
}

void FilterSidebarIneligibleTabs(std::vector<SidebarTreeNode>* nodes) {
  CHECK(nodes);
  std::erase_if(*nodes, [](SidebarTreeNode& node) {
    if (node.kind == SidebarNodeKind::kTab) {
      return !node.url.empty() && !IsSidebarEligibleUrl(GURL(node.url));
    }
    FilterSidebarIneligibleTabs(&node.children);
    return node.kind == SidebarNodeKind::kSplitGroup && node.children.empty();
  });
}

const GURL& GetVisibleOrCommittedSidebarUrl(content::WebContents* contents) {
  CHECK(contents);
  return contents->GetVisibleURL().is_empty() ? contents->GetLastCommittedURL()
                                              : contents->GetVisibleURL();
}

MahoSidebarTabListModel BuildLiveStripFallbackTabListModel(
    Browser* browser,
    const std::string& active_space_id) {
  MahoSidebarTabListModel fallback_model;
  fallback_model.active_space_id = active_space_id;

  TabStripModel* strip = browser ? browser->GetTabStripModel() : nullptr;
  if (!strip) {
    return fallback_model;
  }

  fallback_model.active_tab.index = strip->active_index();
  if (strip->ContainsIndex(fallback_model.active_tab.index)) {
    if (content::WebContents* active_contents =
            strip->GetWebContentsAt(fallback_model.active_tab.index)) {
      fallback_model.active_tab.title = active_contents->GetTitle();
      fallback_model.active_tab.host = base::UTF8ToUTF16(
          GetVisibleOrCommittedSidebarUrl(active_contents).host());
      if (auto* helper = MahoTabIdHelper::FromWebContents(active_contents);
          helper && !helper->stable_tab_id().empty()) {
        fallback_model.active_tab.tab_id = helper->stable_tab_id();
      }
    }
  }

  // This fallback model intentionally flattens live eligible tabs into pinned
  // and normal root rows. It is only used to preserve the scaffold during the
  // brief window where the core-driven sidebar tree is transiently empty.
  for (int i = 0; i < strip->count(); ++i) {
    content::WebContents* contents = strip->GetWebContentsAt(i);
    if (!contents) {
      continue;
    }

    const GURL& url = GetVisibleOrCommittedSidebarUrl(contents);
    if (!IsSidebarEligibleUrl(url)) {
      continue;
    }

    auto* helper = MahoTabIdHelper::FromWebContents(contents);
    if (!helper || helper->stable_tab_id().empty()) {
      continue;
    }

    SidebarTreeNode node;
    node.kind = SidebarNodeKind::kTab;
    node.tab_id = helper->stable_tab_id();
    node.tab_strip_index = i;
    node.is_active = strip->active_index() == i;
    node.is_pinned = strip->IsTabPinned(i);
    node.title = contents->GetTitle();
    node.host = base::UTF8ToUTF16(url.host());
    node.is_loading = contents->IsLoading();
    node.is_audible = contents->IsCurrentlyAudible();
    node.is_muted = contents->IsAudioMuted();

    if (node.is_pinned) {
      fallback_model.pinned_tree.push_back(std::move(node));
    } else {
      fallback_model.normal_tree.push_back(std::move(node));
    }
  }

  maho::GroupSplitTabs(fallback_model.pinned_tree, strip);
  maho::GroupSplitTabs(fallback_model.normal_tree, strip);

  return fallback_model;
}

views::View* AddSectionContainer(
    views::View* parent,
    MahoSidebarTabSection section,
    const std::string& active_space_id,
    base::WeakPtr<MahoSidebarTabListView> tab_list_view) {
  auto container = std::make_unique<SidebarSectionDropTarget>(
      section, active_space_id, tab_list_view);
  auto* raw = container.get();
  parent->AddChildView(std::unique_ptr<views::View>(container.release()));
  return raw;
}

bool HasTabStripContext(Browser* browser) {
  return browser && browser->GetTabStripModel();
}

BEGIN_METADATA(SidebarSeparatorRow)
END_METADATA

BEGIN_METADATA(MahoSidebarActionRowView)
END_METADATA

}  // namespace

namespace maho {

class SplitGroupContainerView : public views::BoxLayoutView {
  METADATA_HEADER(SplitGroupContainerView, views::BoxLayoutView)

 public:
  SplitGroupContainerView() {
    SetNotifyEnterExitOnChild(true);
  }

  void SetActiveState(bool active) {
    if (active_ == active) {
      return;
    }
    active_ = active;
    UpdateBackground();
  }

  void SetSidebarPalette(const MahoSidebarPalette& palette) {
    palette_ = palette;
    UpdateBackground();
  }

  // Pane dividers are recolored with the palette and the active state.
  void AddDivider(std::unique_ptr<views::View> divider) {
    dividers_.push_back(AddChildView(std::move(divider)));
    UpdateBackground();
  }

  void OnThemeChanged() override {
    views::BoxLayoutView::OnThemeChanged();
    UpdateBackground();
  }

  void OnMouseEntered(const ui::MouseEvent& event) override {
    hovered_ = true;
    UpdateBackground();
  }

  void OnMouseExited(const ui::MouseEvent& event) override {
    hovered_ = false;
    UpdateBackground();
  }

  // The panes carry main-axis flex (the split ratio). Measured against a
  // bounded height, BoxLayoutView lets flex children fill it, so the section's
  // vertical BoxLayout -- which measures each row against the height still
  // remaining -- would get back "all remaining height" for this row and
  // collapse every row below the split to 0dp. Report the content height.
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return views::BoxLayoutView::CalculatePreferredSize(
        views::SizeBounds(available_size.width(), views::SizeBound()));
  }

 private:
  void UpdateBackground() {
    if (active_ || hovered_) {
      SetBackground(views::CreateRoundedRectBackground(
          hovered_ ? palette_.row_hover : palette_.row_active,
          kRowCornerRadiusDp));
    } else {
      SetBackground(nullptr);
    }
    // The active split background lightens the row, washing out a subtle
    // divider; use the stronger glyph role while active so it stays visible.
    const SkColor divider_color =
        active_ ? ResolveSidebarPaletteColor(palette_, this,
                                             palette_.neutral_glyph,
                                             ui::kColorSysOnSurfaceSubtle)
                : ResolveSidebarPaletteColor(palette_, this, palette_.outline,
                                             ui::kColorSysDivider);
    for (views::View* divider : dividers_) {
      divider->SetBackground(views::CreateSolidBackground(divider_color));
    }
  }

  bool active_ = false;
  bool hovered_ = false;
  MahoSidebarPalette palette_;
  std::vector<raw_ptr<views::View>> dividers_;
};

BEGIN_METADATA(SplitGroupContainerView)
END_METADATA

class SidebarInsertionLaneView : public views::View {
   METADATA_HEADER(SidebarInsertionLaneView, views::View)

 public:
  SidebarInsertionLaneView(std::string space_id,
                           std::string item_id,
                           SidebarNodeKind target_kind,
                           std::string target_id,
                           bool append = false,
                           std::string target_parent_folder_id = std::string(),
                           int target_folder_child_index = -1,
                           MahoSidebarTabSection section =
                               MahoSidebarTabSection::kNormal,
                           base::WeakPtr<MahoSidebarTabListView> tab_list_view = nullptr)
      : space_id_(std::move(space_id)),
        item_id_(std::move(item_id)),
        target_kind_(target_kind),
        target_id_(std::move(target_id)),
        append_(append),
        target_parent_folder_id_(std::move(target_parent_folder_id)),
        target_folder_child_index_(target_folder_child_index),
        section_(section),
        tab_list_view_(tab_list_view) {
    SetPreferredSize(gfx::Size(0, kDropIndicatorThicknessDp));
  }

  void SetFavoritesDropAcceptedCallback(
      base::RepeatingCallback<void(const std::string&)> cb) {
    on_favorites_drop_accepted_ = std::move(cb);
  }

  void SetRevealEmptyPinnedDropLaneCallback(base::RepeatingClosure cb) {
    reveal_empty_pinned_drop_lane_callback_ = std::move(cb);
  }

  SidebarInsertionLaneView(const SidebarInsertionLaneView&) = delete;
  SidebarInsertionLaneView& operator=(const SidebarInsertionLaneView&) = delete;
  ~SidebarInsertionLaneView() override = default;

  const std::string& target_id_for_testing() const { return target_id_; }
  SidebarNodeKind target_kind_for_testing() const { return target_kind_; }
  bool append_for_testing() const { return append_; }
  const std::string& target_parent_folder_id() const { return target_parent_folder_id_; }

  bool GetDropFormats(int* formats,
                      std::set<ui::ClipboardFormatType>* format_types) override {
    format_types->insert(GetMahoDragFormatType());
    return true;
  }

  bool CanDrop(const ui::OSExchangeData& data) override {
    SidebarDragPayload payload;
    return ReadMahoDragData(data, payload) &&
           (payload.node_kind == SidebarNodeKind::kTab ||
            payload.node_kind == SidebarNodeKind::kFolder);
  }

  int OnDragUpdated(const ui::DropTargetEvent& event) override {
    ForwardDragToAutoScroller(this, event);
    SidebarDragPayload payload;
    if (!ReadMahoDragData(event.data(), payload) || payload.node_id == item_id_) {
      drag_over_drop_target_ = false;
      UpdateAppearance();
      return static_cast<int>(ui::mojom::DragOperation::kNone);
    }
    if (section_ == MahoSidebarTabSection::kNormal &&
        reveal_empty_pinned_drop_lane_callback_ &&
        (payload.origin == SidebarDragOrigin::kPinnedSection ||
         payload.origin == SidebarDragOrigin::kNormalSection ||
         payload.origin == SidebarDragOrigin::kFavorites)) {
      reveal_empty_pinned_drop_lane_callback_.Run();
    }
    drag_over_drop_target_ = true;
    UpdateAppearance();
    return static_cast<int>(ui::mojom::DragOperation::kMove);
  }

  DropCallback GetDropCallback(const ui::DropTargetEvent& event) override {
    ForwardDragEnded(this);
    return base::BindOnce(
        [](base::WeakPtr<MahoSidebarTabListView> tab_list_view,
           std::string space_id, std::string item_id,
           SidebarNodeKind target_kind, std::string target_id, bool append,
           std::string target_parent_folder_id, int target_folder_child_index,
           MahoSidebarTabSection section,
           base::RepeatingCallback<void(const std::string&)>
               on_favorites_drop_accepted,
           const ui::DropTargetEvent& event,
           ui::mojom::DragOperation& output_drag_op,
           std::unique_ptr<ui::LayerTreeOwner> drag_image_layer_owner) {
          SidebarDragPayload payload;
          if (!ReadMahoDragData(event.data(), payload) || payload.node_id == item_id) {
            output_drag_op = ui::mojom::DragOperation::kNone;
            return;
          }
          output_drag_op = ui::mojom::DragOperation::kNone;
          if (payload.node_kind == SidebarNodeKind::kTab) {
            if (target_kind == SidebarNodeKind::kTab) {
              if (!target_parent_folder_id.empty() &&
                  payload.source_parent_folder_id == target_parent_folder_id) {
                output_drag_op = ui::mojom::DragOperation::kMove;
                DispatchShellEventEx("reorder_tab_in_folder", {
                    ShellEventField("space_id", space_id),
                    ShellEventField("folder_id", target_parent_folder_id),
                    ShellEventField("tab_id", payload.node_id),
                    ShellEventField("to", target_folder_child_index)});
                return;
              }
              const SidebarDropPlan plan =
                  ResolveDropPlan(SectionKindFromTabSection(section), payload);
              if (!plan.is_valid) {
                output_drag_op = ui::mojom::DragOperation::kNone;
                return;
              }
              if (plan.favorite_transition.kind ==
                  SidebarDropPlan::FavoriteTransition::Kind::kUnfavorite) {
                if (on_favorites_drop_accepted) {
                  on_favorites_drop_accepted.Run(payload.node_id);
                }
              }
              if (tab_list_view) {
                tab_list_view->HandlePostDropTransition(payload, plan);
              }
              ExecuteDropPlan(plan);
              output_drag_op = ui::mojom::DragOperation::kMove;
              DispatchReorderRootItem(space_id, SidebarNodeKind::kTab,
                                     payload.node_id,
                                     append ? MakeInsertionPointAppend()
                                            : MakeInsertionPointBefore(
                                                  SidebarNodeKind::kTab,
                                                  target_id));
              return;
            }
            if (target_kind == SidebarNodeKind::kFolder) {
              const SidebarDropPlan plan =
                  ResolveDropPlan(SectionKindFromTabSection(section), payload);
              if (!plan.is_valid) {
                output_drag_op = ui::mojom::DragOperation::kNone;
                return;
              }
              if (plan.favorite_transition.kind ==
                  SidebarDropPlan::FavoriteTransition::Kind::kUnfavorite) {
                if (on_favorites_drop_accepted) {
                  on_favorites_drop_accepted.Run(payload.node_id);
                }
              }
              if (tab_list_view) {
                tab_list_view->HandlePostDropTransition(payload, plan);
              }
              ExecuteDropPlan(plan);
              output_drag_op = ui::mojom::DragOperation::kMove;
              DispatchReorderRootItem(space_id, SidebarNodeKind::kTab,
                                     payload.node_id,
                                     append ? MakeInsertionPointAppend()
                                            : MakeInsertionPointBefore(
                                                  SidebarNodeKind::kFolder,
                                                  target_id));
              return;
            }
          } else if (payload.node_kind == SidebarNodeKind::kFolder) {
            const SidebarDropPlan plan =
                ResolveDropPlan(SectionKindFromTabSection(section), payload);
            if (!plan.is_valid) {
              output_drag_op = ui::mojom::DragOperation::kNone;
              return;
            }
            if (tab_list_view) {
              tab_list_view->HandlePostDropTransition(payload, plan);
            }
            ExecuteDropPlan(plan);
            output_drag_op = ui::mojom::DragOperation::kMove;
            if (target_kind == SidebarNodeKind::kTab) {
              DispatchReorderRootItem(space_id, SidebarNodeKind::kFolder,
                                     payload.node_id,
                                     append ? MakeInsertionPointAppend()
                                            : MakeInsertionPointBefore(
                                                  SidebarNodeKind::kTab,
                                                  target_id));
              return;
            }
            if (target_kind == SidebarNodeKind::kFolder) {
              if (append) {
                std::vector<std::pair<std::string, std::string>> fields = {
                    {"space_id", space_id},
                    {"folder_id", payload.node_id}};
                if (!target_parent_folder_id.empty()) {
                  fields.push_back({"parent_folder_id",
                                    target_parent_folder_id});
                }
                DispatchShellEvent("reorder_folder", fields);
              } else {
                if (!target_parent_folder_id.empty()) {
                  DispatchShellEvent("reorder_folder", {
                      {"space_id", space_id},
                      {"folder_id", payload.node_id},
                      {"parent_folder_id", target_parent_folder_id},
                      {"before_folder_id", target_id}});
                } else {
                  DispatchShellEvent("reorder_folder", {
                      {"space_id", space_id},
                      {"folder_id", payload.node_id},
                      {"before_folder_id", target_id}});
                }
              }
            }
          }
        },
        tab_list_view_, space_id_, item_id_, target_kind_, target_id_, append_,
        target_parent_folder_id_, target_folder_child_index_, section_,
        on_favorites_drop_accepted_);
  }

  void OnDragExited() override {
    ForwardDragEnded(this);
    drag_over_drop_target_ = false;
    UpdateAppearance();
  }

 private:
  void UpdateAppearance() {
    SetBackground(drag_over_drop_target_ && tab_list_view_
                      ? views::CreateSolidBackground(ResolveSidebarPaletteColor(
                            tab_list_view_->palette_, this,
                            tab_list_view_->palette_.focus_ring,
                            ui::kColorSysPrimary))
                      : nullptr);
    // The lane stays a thin marker fixed at the insertion gap. It must never
    // grow into the adjacent tab row on hover: doing so shifts layout under the
    // cursor, traps the drag on the enlarged lane, and hides the tab row's
    // split drop zone (its lower two-thirds).
    SetPreferredSize(gfx::Size(0, kDropIndicatorThicknessDp));
    if (parent()) {
      parent()->InvalidateLayout();
    }
  }

  const std::string space_id_;
  const std::string item_id_;
  const SidebarNodeKind target_kind_;
  const std::string target_id_;
  const bool append_;
  const std::string target_parent_folder_id_;
  const int target_folder_child_index_;
  const MahoSidebarTabSection section_;
  base::WeakPtr<MahoSidebarTabListView> tab_list_view_;
  bool drag_over_drop_target_ = false;
  base::RepeatingCallback<void(const std::string&)> on_favorites_drop_accepted_;
  base::RepeatingClosure reveal_empty_pinned_drop_lane_callback_;
};

std::u16string NormalizeRenamedTitle(const std::u16string& text) {
  std::u16string trimmed = text;
  base::TrimWhitespace(trimmed, base::TRIM_ALL, &trimmed);
  return trimmed;
}

BEGIN_METADATA(SidebarInsertionLaneView)
END_METADATA

BEGIN_METADATA(MahoSidebarTabListView)
END_METADATA
BEGIN_METADATA(SidebarTabRowView)
END_METADATA
BEGIN_METADATA(SidebarFolderRowView)
END_METADATA

views::View::DropCallback GetSectionDropCallbackForTesting(
    MahoSidebarTabSection section,
    const ui::DropTargetEvent& event) {
  SidebarSectionDropTarget target(
      section, "space-1", base::WeakPtr<MahoSidebarTabListView>());
  return target.GetDropCallback(event);
}

SidebarSectionDropIndicatorStateForTesting
GetSectionDropIndicatorStateForTesting(MahoSidebarTabSection section,
                                       const ui::DropTargetEvent& event) {
  SidebarSectionDropTarget target(
      section, "space-1", base::WeakPtr<MahoSidebarTabListView>());
  SidebarSectionDropIndicatorStateForTesting state;
  target.OnDragUpdated(event);
  state.visible_after_update = target.is_drop_indicator_visible_for_testing();
  target.OnDragExited();
  state.visible_after_exit = target.is_drop_indicator_visible_for_testing();
  return state;
}

namespace {

template <typename ViewType>
ViewType* RecursiveFindView(views::View* root,
                            std::function<bool(ViewType*)> predicate) {
  if (!root) return nullptr;
  if (auto* typed = views::AsViewClass<ViewType>(root)) {
    if (predicate(typed)) return typed;
  }
  for (views::View* child : root->children()) {
    if (auto* found = RecursiveFindView<ViewType>(child, predicate)) {
      return found;
    }
  }
  return nullptr;
}

}  // namespace

views::View* MahoSidebarTabListView::FindSectionDropTargetForTesting(
    MahoSidebarTabSection section) {
  if (section == MahoSidebarTabSection::kPinned) return pinned_section_target_;
  if (section == MahoSidebarTabSection::kNormal) return normal_section_target_;
  return nullptr;
}

SidebarTabRowView* MahoSidebarTabListView::FindTabRowByIdForTesting(
    const std::string& tab_id) {
  return RecursiveFindView<SidebarTabRowView>(
      tab_rows_,
      [&tab_id](SidebarTabRowView* row) {
        return row->tab_id_for_testing() == tab_id;
      });
}

SidebarTabRowView* MahoSidebarTabListView::FindTabRowByTabIndexForTesting(
    int tab_index) {
  return RecursiveFindView<SidebarTabRowView>(
      tab_rows_, [tab_index](SidebarTabRowView* row) {
        return row->live_tab_index_for_testing() == tab_index;
      });
}

SidebarTabRowView* MahoSidebarTabListView::FindActiveTabRowForTesting() {
  TabStripModel* strip = browser_ ? browser_->GetTabStripModel() : nullptr;
  if (strip) {
    const int active_index = strip->active_index();
    if (auto* row = RecursiveFindView<SidebarTabRowView>(
            tab_rows_, [active_index](SidebarTabRowView* r) {
              return r->live_tab_index_for_testing() == active_index;
            })) {
      return row;
    }
  }
  return RecursiveFindView<SidebarTabRowView>(
      tab_rows_, [](SidebarTabRowView* row) {
        return row->is_active_for_testing();
      });
}

SidebarTabRowView* MahoSidebarTabListView::FindFirstInactiveTabRowForTesting() {
  TabStripModel* strip = browser_ ? browser_->GetTabStripModel() : nullptr;
  if (!strip) {
    return nullptr;
  }
  const int active_index = strip->active_index();
  return RecursiveFindView<SidebarTabRowView>(
      tab_rows_, [active_index](SidebarTabRowView* row) {
        const int live_index = row->live_tab_index_for_testing();
        return live_index >= 0 && live_index != active_index;
      });
}

SidebarFolderRowView* MahoSidebarTabListView::FindFolderRowByIdForTesting(
    const std::string& folder_id) {
  return RecursiveFindView<SidebarFolderRowView>(
      tab_rows_,
      [&folder_id](SidebarFolderRowView* row) {
        return row->folder_id_for_testing() == folder_id;
      });
}

views::View* MahoSidebarTabListView::FindInsertionLaneBeforeTabByIdForTesting(
    const std::string& tab_id) {
  return RecursiveFindView<SidebarInsertionLaneView>(
      tab_rows_, [&tab_id](SidebarInsertionLaneView* lane) {
        return lane && lane->GetVisible() &&
               lane->target_kind_for_testing() == SidebarNodeKind::kTab &&
               lane->target_id_for_testing() == tab_id &&
               !lane->append_for_testing();
      });
}

views::View* MahoSidebarTabListView::FindInsertionLaneBeforeFolderByIdForTesting(
    const std::string& folder_id) {
  return RecursiveFindView<SidebarInsertionLaneView>(
      tab_rows_, [&folder_id](SidebarInsertionLaneView* lane) {
        return lane && lane->GetVisible() &&
               lane->target_kind_for_testing() == SidebarNodeKind::kFolder &&
               lane->target_id_for_testing() == folder_id &&
               !lane->append_for_testing();
      });
}

void MahoSidebarTabListView::ShowTabRowContextMenu(
    views::View* source,
    int live_index,
    const gfx::Point& point,
    ui::mojom::MenuSourceType source_type,
    MahoTabContextMenu::Delegate* delegate) {
  SidebarTabRowView* row = views::AsViewClass<SidebarTabRowView>(source);
  std::string clicked_tab_id = row ? row->tab_id() : std::string();

  if (!clicked_tab_id.empty() && selected_tab_ids_.count(clicked_tab_id) == 0) {
    ClearSelection();
  }

  std::vector<std::string> selected_ids;
  if (row && selected_tab_ids_.count(row->tab_id()) > 0) {
    selected_ids.assign(selected_tab_ids_.begin(), selected_tab_ids_.end());
  }

  active_context_menu_ =
      std::make_unique<MahoTabContextMenu>(browser_, live_index, delegate,
                                           row ? row->tab_id() : std::string(),
                                           selected_ids);
  active_menu_model_ = active_context_menu_->BuildMenuModel();
  if (!browser_) {
    return;
  }
  active_menu_runner_ = std::make_unique<views::MenuRunner>(
      active_menu_model_.get(),
      views::MenuRunner::CONTEXT_MENU);
  active_menu_runner_->RunMenuAt(source->GetWidget(), nullptr,
                                 gfx::Rect(point, gfx::Size()),
                                 views::MenuAnchorPosition::kTopLeft,
                                 source_type);
}

void MahoSidebarTabListView::ShowContextMenuForViewImpl(
    views::View* source,
    const gfx::Point& point,
    ui::mojom::MenuSourceType source_type) {
  if (!browser_) {
    return;
  }
  MahoSpaceContextMenu context_menu(browser_, "sidebar-spaces");
  auto menu_model = context_menu.BuildMenuModel();
  views::MenuRunner menu_runner(menu_model.get(),
                                views::MenuRunner::CONTEXT_MENU);
  menu_runner.RunMenuAt(source->GetWidget(), nullptr,
                        gfx::Rect(point, gfx::Size()),
                        views::MenuAnchorPosition::kTopLeft, source_type);
}

// --- Custom clipboard format for Maho sidebar drag ---

const ui::ClipboardFormatType& GetMahoDragFormatType() {
  // Must be CustomPlatformType, not Deserialize: on Windows Deserialize parses
  // a numeric CLIPFORMAT (StringToInt CHECK) and crashes on a MIME-style name.
  static const base::NoDestructor<ui::ClipboardFormatType> kFormat(
      ui::ClipboardFormatType::CustomPlatformType(
          "application/x-maho-sidebar-drag"));
  return *kFormat;
}

std::string SerializeDragPayload(const SidebarDragPayload& payload) {
  base::ListValue selected_list;
  for (const auto& id : payload.selected_tab_ids) {
    selected_list.Append(id);
  }
  base::ListValue split_member_list;
  for (const auto& id : payload.split_member_tab_ids) {
    split_member_list.Append(id);
  }
  base::DictValue dict = base::DictValue()
      .Set("node_kind", payload.node_kind == SidebarNodeKind::kTab ? "tab" : "folder")
      .Set("node_id", payload.node_id)
      .Set("origin", static_cast<int>(payload.origin))
      .Set("space_id", payload.space_id)
      .Set("source_parent_folder_id", payload.source_parent_folder_id)
      .Set("source_folder_child_index", payload.source_folder_child_index)
      .Set("selected_tab_ids", std::move(selected_list))
      .Set("split_member_tab_ids", std::move(split_member_list));
  std::string json;
  base::JSONWriter::Write(dict, &json);
  return json;
}

bool DeserializeDragPayload(const std::string& data, SidebarDragPayload& out) {
  auto parsed = base::JSONReader::Read(data, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return false;
  }
  const auto& dict = parsed->GetDict();
  const std::string* kind_str = dict.FindString("node_kind");
  if (!kind_str) {
    return false;
  }
  out.node_kind = (*kind_str == "folder") ? SidebarNodeKind::kFolder : SidebarNodeKind::kTab;
  const std::string* nid = dict.FindString("node_id");
  if (nid) out.node_id = *nid;
  std::optional<int> origin = dict.FindInt("origin");
  if (origin.has_value()) out.origin = static_cast<SidebarDragOrigin>(*origin);
  const std::string* sid = dict.FindString("space_id");
  if (sid) out.space_id = *sid;
  const std::string* source_parent_folder_id =
      dict.FindString("source_parent_folder_id");
  if (source_parent_folder_id) {
    out.source_parent_folder_id = *source_parent_folder_id;
  }
  std::optional<int> source_folder_child_index =
      dict.FindInt("source_folder_child_index");
  if (source_folder_child_index.has_value()) {
    out.source_folder_child_index = *source_folder_child_index;
  }
  const base::ListValue* selected_list = dict.FindList("selected_tab_ids");
  if (selected_list) {
    for (size_t i = 0; i < selected_list->size(); ++i) {
      const base::Value& value = (*selected_list)[i];
      if (value.is_string()) {
        out.selected_tab_ids.push_back(value.GetString());
      }
    }
  }
  const base::ListValue* split_member_list =
      dict.FindList("split_member_tab_ids");
  if (split_member_list) {
    for (size_t i = 0; i < split_member_list->size(); ++i) {
      const base::Value& value = (*split_member_list)[i];
      if (value.is_string()) {
        out.split_member_tab_ids.push_back(value.GetString());
      }
    }
  }
  return true;
}

void WriteMahoDragData(const SidebarDragPayload& payload,
                       ui::OSExchangeData* data) {
  std::string serialized = SerializeDragPayload(payload);
  base::Pickle pickle;
  pickle.WriteString(serialized);
  data->SetPickledData(GetMahoDragFormatType(), pickle);
}

bool ReadMahoDragData(const ui::OSExchangeData& data,
                      SidebarDragPayload& out) {
  std::optional<base::Pickle> pickle = data.GetPickledData(GetMahoDragFormatType());
  if (!pickle.has_value()) {
    return false;
  }
  base::PickleIterator iter(pickle.value());
  std::string serialized;
  if (!iter.ReadString(&serialized)) {
    return false;
  }
  return DeserializeDragPayload(serialized, out);
}

base::DictValue MakeRootItem(SidebarNodeKind kind, const std::string& id) {
  base::DictValue item;
  item.Set("kind", kind == SidebarNodeKind::kTab ? "tab" : "folder");
  item.Set("id", id);
  return item;
}

base::DictValue MakeInsertionPointBefore(SidebarNodeKind target_kind,
                                         const std::string& target_id) {
  base::DictValue point;
  point.Set("kind", "before");
  point.Set("target", MakeRootItem(target_kind, target_id));
  return point;
}

base::DictValue MakeInsertionPointAppend() {
  base::DictValue point;
  point.Set("kind", "append");
  return point;
}

void DispatchReorderRootItem(const std::string& space_id,
                             SidebarNodeKind item_kind,
                             const std::string& item_id,
                             base::DictValue insertion_point) {
  base::DictValue dict;
  dict.Set("space_id", space_id);
  dict.Set("item", MakeRootItem(item_kind, item_id));
  dict.Set("insertion_point", std::move(insertion_point));
  DispatchShellEventDict("reorder_root_item", std::move(dict));
}

// --- SidebarTabRowView ---

SidebarTabRowView::SidebarTabRowView(const SidebarTreeNode& node,
                                     ui::ImageModel favicon,
                                     Browser* browser,
                                     MahoSidebarTabSection section,
                                     std::string active_space_id,
                                     std::u16string display_title,
                                     views::Button::PressedCallback activate_callback,
                                     views::Button::PressedCallback close_callback,
                                     views::Button::PressedCallback mute_callback,
                                     base::RepeatingCallback<void(const std::string&, const std::u16string&)>
                                         rename_callback,
                                     base::RepeatingCallback<int(const std::string&)>
                                         resolve_tab_index,
                                     base::RepeatingCallback<void(views::View*, int)>
                                         preview_show_callback,
                                     base::RepeatingClosure preview_hide_callback)
    : active_(node.is_active),
      pinned_(node.is_pinned),
      audible_(node.is_audible),
      muted_(node.is_muted),
      tab_id_(node.tab_id),
      section_(section),
      space_id_(std::move(active_space_id)),
      source_parent_folder_id_(node.parent_folder_id),
      folder_child_index_(node.folder_child_index),
      browser_(browser),
      tab_index_(node.tab_strip_index),
      committed_title_(std::move(display_title)),
      rename_callback_(std::move(rename_callback)),
      resolve_tab_index_(std::move(resolve_tab_index)),
      preview_show_callback_(std::move(preview_show_callback)),
      preview_hide_callback_(std::move(preview_hide_callback)),
      activate_callback_(std::move(activate_callback)),
      mute_callback_(std::move(mute_callback)) {
  split_preview_animation_.SetSlideDuration(
      base::Milliseconds(kSplitPreviewAnimationMs));
  split_preview_animation_.SetTweenType(gfx::Tween::EASE_OUT);
  control_pulse_animation_.SetSlideDuration(
      base::Milliseconds(kControlledTabPulseAnimationMs));
  control_pulse_animation_.SetTweenType(gfx::Tween::EASE_IN_OUT);
  SetNotifyEnterExitOnChild(true);
  const int indent = node.depth * kFolderIndentDp;
  auto row_insets = gfx::Insets::TLBR(0, 9 + indent, 0, 7);

  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, row_insets,
      kRowIconTextSpacingDp));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  favicon_view_ = AddChildView(std::make_unique<views::ImageView>());
  favicon_view_->SetImage(std::move(favicon));
  favicon_view_->SetImageSize(gfx::Size(kRowIconSizeDp, kRowIconSizeDp));

  std::u16string display_text = committed_title_.empty() ? node.host : committed_title_;
  title_button_ = static_cast<views::LabelButton*>(AddChildView(
      std::unique_ptr<views::View>(
          std::make_unique<SidebarDropForwardingLabelButton>(
              base::BindRepeating(&SidebarTabRowView::ActivateTabFromEvent,
                                  base::Unretained(this)),
              display_text, this)
              .release())));
  title_button_->SetBorder(nullptr);
  title_button_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  title_button_->SetMinSize(gfx::Size(0, kRowHeightDp));
  title_button_->SetMaxSize(
      gfx::Size(sidebar_layout::kRuntimeRailWidthMaxDp, kRowHeightDp));
  title_button_->SetFocusRingCornerRadius(kRowCornerRadiusDp);
  title_button_->SetRequestFocusOnPress(false);
  title_button_->SetImageLabelSpacing(0);
  // All four title states (including STATE_DISABLED, which macOS paints for
  // inactive windows) are bound to the palette in UpdateAppearance().
  title_button_->SetTextSubpixelRenderingEnabled(false);
  title_button_->SetLabelStyle(views::style::STYLE_BODY_2);
  layout->SetFlexForView(title_button_, 1);
  SyncButtonAccessibleName(title_button_, display_text);

  auto title_field = std::make_unique<views::Textfield>();
  title_field->SetVisible(false);
  title_field->SetBorder(nullptr);
  title_field->SetBackgroundColor(SK_ColorTRANSPARENT);
  title_field->SetController(this);
  title_field->SetAccessibleName(u"Rename tab");
  title_field_ = AddChildView(std::move(title_field));
  layout->SetFlexForView(title_field_, 1);

  audio_button_ = AddChildView(std::make_unique<views::ImageButton>(
      base::BindRepeating(&SidebarTabRowView::OnAudioButtonPressed,
                          base::Unretained(this))));
  audio_button_->SetBorder(nullptr);
  views::InstallCircleHighlightPathGenerator(audio_button_);
  UpdateAudioState(audible_, muted_);

  close_button_ =
      AddChildView(views::CreateVectorImageButton(std::move(close_callback)));
  // Close glyph images for every state are bound to the palette in
  // UpdateAppearance().
  close_button_->SetBorder(nullptr);
  close_button_->SetPreferredSize(
      gfx::Size(kCloseButtonSizeDp, kCloseButtonSizeDp));
  close_button_->SetImageHorizontalAlignment(views::ImageButton::ALIGN_CENTER);
  close_button_->SetImageVerticalAlignment(views::ImageButton::ALIGN_MIDDLE);
  close_button_->SetTooltipText(u"Close tab");
  close_button_->SetAccessibleName(u"Close tab");
  close_button_->SetProperty(
      views::kMarginsKey,
      gfx::Insets::TLBR(0, kRowTrailingSpacingDp, 0, 0));
  views::InstallCircleHighlightPathGenerator(close_button_);

  close_button_->SetPaintToLayer();
  close_button_->layer()->SetFillsBoundsOpaquely(false);
  close_button_->layer()->SetOpacity(0.0f);
  close_button_->SetCanProcessEventsWithinSubtree(false);
  audio_button_->SetPaintToLayer();
  audio_button_->layer()->SetFillsBoundsOpaquely(false);

  title_button_->set_context_menu_controller(this);
  title_button_->set_drag_controller(this);

  set_context_menu_controller(this);
  set_drag_controller(this);

  GetViewAccessibility().SetRole(ax::mojom::Role::kTab);
  GetViewAccessibility().SetIsSelected(is_selected_);

  UpdateAppearance();
}

SidebarTabRowView::~SidebarTabRowView() = default;

double SidebarTabRowView::GetSplitPreviewProgress() const {
  return split_preview_animation_.GetCurrentValue();
}

MahoSplitDropSide SidebarTabRowView::GetSplitPreviewSide() const {
  return split_preview_visual_side_;
}

void SidebarTabRowView::ShowSplitPreview(MahoSplitDropSide side) {
  const bool side_changed = split_preview_side_ != side ||
                            split_preview_visual_side_ != side;
  split_preview_side_ = side;
  split_preview_visual_side_ = side;
  if (side_changed) {
    RefreshSplitPreviewLayout();
  }
  UpdateSplitPreviewAnimation(true);
}

void SidebarTabRowView::ClearSplitPreview() {
  split_preview_side_ = MahoSplitDropSide::kRight;
  split_preview_visual_side_ = MahoSplitDropSide::kRight;
  split_preview_target_visible_ = false;
  split_preview_animation_.Reset(0.0);
  RefreshSplitPreviewLayout();
}

void SidebarTabRowView::UpdateSplitPreviewAnimation(bool visible) {
  if (split_preview_target_visible_ == visible) {
    return;
  }
  split_preview_target_visible_ = visible;
  if (visible) {
    split_preview_animation_.Show();
  } else {
    split_preview_animation_.Hide();
  }
  RefreshSplitPreviewLayout();
}

void SidebarTabRowView::AnimationProgressed(const gfx::Animation* animation) {
  if (animation == &split_preview_animation_) {
    RefreshSplitPreviewLayout();
  } else if (animation == &control_pulse_animation_) {
    SchedulePaint();
  }
}

void SidebarTabRowView::AnimationEnded(const gfx::Animation* animation) {
  if (animation == &control_pulse_animation_) {
    if (controlled_ && gfx::Animation::ShouldRenderRichAnimation()) {
      control_pulse_animation_.Reset(0.0);
      control_pulse_animation_.Show();
    } else {
      SchedulePaint();
    }
    return;
  }
  if (animation != &split_preview_animation_) {
    return;
  }
  RefreshSplitPreviewLayout();
  if (!split_preview_target_visible_ &&
      split_preview_animation_.GetCurrentValue() == 0.0) {
    split_preview_visual_side_ = MahoSplitDropSide::kRight;
    UpdateAppearance();
  }
}

void SidebarTabRowView::OnPaint(gfx::Canvas* canvas) {
  views::View::OnPaint(canvas);
  if (!controlled_) {
    return;
  }

  const double progress = control_pulse_animation_.is_animating()
                              ? control_pulse_animation_.GetCurrentValue()
                              : 0.5;
  const double pulse = 4.0 * progress * (1.0 - progress);

  constexpr SkColor kAgentViolet = SkColorSetRGB(168, 85, 247);
  constexpr SkColor kAgentGlow = SkColorSetRGB(192, 132, 252);

  gfx::RectF bounds(GetLocalBounds());
  bounds.Inset(1.0f);

  cc::PaintFlags bg_tint;
  bg_tint.setAntiAlias(true);
  bg_tint.setStyle(cc::PaintFlags::kFill_Style);
  bg_tint.setColor(SkColorSetA(kAgentViolet, static_cast<int>(35 + 25 * pulse)));
  canvas->DrawRoundRect(bounds, kRowCornerRadiusDp, bg_tint);

  cc::PaintFlags glow_outline;
  glow_outline.setAntiAlias(true);
  glow_outline.setStyle(cc::PaintFlags::kStroke_Style);
  glow_outline.setStrokeWidth(4.5f);
  glow_outline.setColor(SkColorSetA(kAgentGlow, static_cast<int>(80 + 100 * pulse)));
  canvas->DrawRoundRect(bounds, kRowCornerRadiusDp, glow_outline);

  cc::PaintFlags neon_outline;
  neon_outline.setAntiAlias(true);
  neon_outline.setStyle(cc::PaintFlags::kStroke_Style);
  neon_outline.setStrokeWidth(2.5f);
  neon_outline.setColor(SkColorSetA(kAgentViolet, static_cast<int>(210 + 45 * pulse)));
  canvas->DrawRoundRect(bounds, kRowCornerRadiusDp, neon_outline);

  cc::PaintFlags dot_halo;
  dot_halo.setAntiAlias(true);
  dot_halo.setStyle(cc::PaintFlags::kFill_Style);
  dot_halo.setColor(SkColorSetA(kAgentGlow, static_cast<int>(100 + 80 * pulse)));
  const gfx::PointF dot_center(bounds.right() - 12.0f, bounds.CenterPoint().y());
  canvas->DrawCircle(dot_center, 6.0f, dot_halo);

  cc::PaintFlags dot_core;
  dot_core.setAntiAlias(true);
  dot_core.setStyle(cc::PaintFlags::kFill_Style);
  dot_core.setColor(kAgentViolet);
  canvas->DrawCircle(dot_center, 3.5f, dot_core);
}

void SidebarTabRowView::SetSplitCompactMode(bool compact) {
  split_compact_ = compact;
  if (audio_button_) {
    audio_button_->SetVisible(!compact && (audible_ || muted_));
  }
  if (close_button_) {
    close_button_->SetVisible(!compact);
  }
  last_visual_state_ = AppearanceVisualState::kUninitialized;
  UpdateAppearance();
  InvalidateLayout();
}

void SidebarTabRowView::UpdateAudioState(bool audible, bool muted) {
  audible_ = audible;
  muted_ = muted;

  if (!audio_button_) {
    return;
  }

  const SkColor neutral_glyph = ResolveSidebarPaletteColor(
      palette_, this, palette_.neutral_glyph, ui::kColorSysOnSurfaceSubtle);
  const SkColor primary_text = ResolveSidebarPaletteColor(
      palette_, this, palette_.primary_text, ui::kColorSysOnSurface);
  const auto set_audio_image = [this, neutral_glyph, primary_text](
                                   const gfx::VectorIcon& icon) {
    audio_button_->SetImageModel(
        views::Button::STATE_NORMAL,
        ui::ImageModel::FromVectorIcon(icon, neutral_glyph, 14));
    audio_button_->SetImageModel(
        views::Button::STATE_HOVERED,
        ui::ImageModel::FromVectorIcon(icon, primary_text, 14));
    audio_button_->SetImageModel(
        views::Button::STATE_PRESSED,
        ui::ImageModel::FromVectorIcon(icon, primary_text, 14));
  };

  if (muted_) {
    set_audio_image(maho_lucide_icons::kVolumeXIcon);
    audio_button_->SetTooltipText(u"Tab muted");
    audio_button_->SetVisible(true);
  } else if (audible_) {
    set_audio_image(maho_lucide_icons::kVolume2Icon);
    audio_button_->SetTooltipText(u"Tab playing audio");
    audio_button_->SetVisible(true);
  } else {
    audio_button_->SetVisible(false);
  }
  InvalidateLayout();
}

void SidebarTabRowView::OnAudioButtonPressed(const ui::Event& event) {
  if (mute_callback_) {
    mute_callback_.Run(event);
  }
}

// P3: Constant preferred size avoids BoxLayout re-measuring 870+ rows during
// layout invalidation triggered by hover state changes.
gfx::Size SidebarTabRowView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  return gfx::Size(0, kRowHeightDp);
}

void SidebarTabRowView::Layout(PassKey) {
  LayoutSuperclass<views::View>(this);
  if (!split_compact_) {
    ApplySplitPreviewContentLayout(GetSplitPreviewProgress());
  }
}

void SidebarTabRowView::ApplySplitPreviewContentLayout(double progress) {
  progress = std::clamp(progress, 0.0, 1.0);
  if (progress <= 0.0 || !favicon_view_ || !title_button_ || !close_button_) {
    return;
  }

  const gfx::Rect full_bounds = GetContentsBounds();
  if (full_bounds.IsEmpty() ||
      full_bounds.width() <= kSplitPreviewDividerWidthDp) {
    return;
  }

  const int pane_width =
      std::max(0, (full_bounds.width() - kSplitPreviewDividerWidthDp) / 2);
  const int pane_x = split_preview_side_ == MahoSplitDropSide::kLeft
                         ? full_bounds.x() + pane_width +
                               kSplitPreviewDividerWidthDp
                         : full_bounds.x();
  const gfx::Rect pane_bounds(pane_x, full_bounds.y(), pane_width,
                              full_bounds.height());
  const gfx::Rect favicon_bounds = favicon_view_->bounds();
  const gfx::Rect title_bounds = title_button_->bounds();
  const gfx::Rect title_field_bounds = title_field_ ? title_field_->bounds()
                                                    : title_bounds;
  const gfx::Rect audio_bounds = audio_button_ ? audio_button_->bounds()
                                               : gfx::Rect();
  const gfx::Rect close_bounds = close_button_->bounds();
  const int leading_inset =
      std::max(0, favicon_bounds.x() - full_bounds.x());
  const int trailing_inset =
      std::max(0, full_bounds.right() - close_bounds.right());

  gfx::Rect close_target = close_bounds;
  close_target.set_x(std::max(
      pane_bounds.x(), pane_bounds.right() - trailing_inset -
                           close_bounds.width()));
  int trailing_x = close_target.x();

  gfx::Rect audio_target = audio_bounds;
  if (audio_button_ && audio_button_->GetVisible()) {
    audio_target.set_x(std::max(
        pane_bounds.x(), trailing_x - kRowIconTextSpacingDp -
                             audio_bounds.width()));
    trailing_x = audio_target.x();
  }

  gfx::Rect favicon_target = favicon_bounds;
  favicon_target.set_x(std::min(
      pane_bounds.right() - favicon_bounds.width(),
      pane_bounds.x() + std::min(leading_inset, pane_width)));
  favicon_target.set_x(std::max(pane_bounds.x(), favicon_target.x()));

  const int title_x = favicon_target.right() + kRowIconTextSpacingDp;
  const int title_right = std::max(title_x, trailing_x - kRowIconTextSpacingDp);
  gfx::Rect title_target(title_x, title_bounds.y(),
                         std::max(0, title_right - title_x),
                         title_bounds.height());
  gfx::Rect title_field_target(title_x, title_field_bounds.y(),
                               title_target.width(),
                               title_field_bounds.height());

  favicon_view_->SetBoundsRect(
      gfx::Tween::RectValueBetween(progress, favicon_bounds, favicon_target));
  title_button_->SetBoundsRect(
      gfx::Tween::RectValueBetween(progress, title_bounds, title_target));
  if (title_field_) {
    title_field_->SetBoundsRect(gfx::Tween::RectValueBetween(
        progress, title_field_bounds, title_field_target));
  }
  if (audio_button_ && audio_button_->GetVisible()) {
    audio_button_->SetBoundsRect(
        gfx::Tween::RectValueBetween(progress, audio_bounds, audio_target));
  }
  close_button_->SetBoundsRect(
      gfx::Tween::RectValueBetween(progress, close_bounds, close_target));
}

void SidebarTabRowView::RefreshSplitPreviewLayout() {
  InvalidateLayout();
  DeprecatedLayoutImmediately();
  SchedulePaint();
}

void SidebarTabRowView::SetFavicon(ui::ImageModel image) {
  if (!favicon_view_) {
    return;
  }
  favicon_view_->SetImage(std::move(image));
  favicon_view_->SetImageSize(gfx::Size(kRowIconSizeDp, kRowIconSizeDp));
}

ui::ImageModel SidebarTabRowView::favicon_for_testing() const {
  return favicon_view_ ? favicon_view_->GetImageModel() : ui::ImageModel();
}

void SidebarTabRowView::SetSuspended(bool suspended) {
  if (is_suspended_ == suspended) {
    return;
  }
  is_suspended_ = suspended;
  UpdateAppearance();
}

int SidebarTabRowView::live_tab_index_for_testing() const {
  if (resolve_tab_index_) {
    const int resolved_index = resolve_tab_index_.Run(tab_id_);
    if (resolved_index >= 0) {
      return resolved_index;
    }
  }
  return tab_index_;
}

void SidebarTabRowView::RenameTab(int /*tab_index*/) {
  BeginTitleEditing();
}

void SidebarTabRowView::OnThemeChanged() {
  views::View::OnThemeChanged();
  // The pre-palette fallback reads the ColorProvider, which only exists once
  // the row is in a widget; re-render the close glyphs with it.
  close_button_images_valid_ = false;
  if (title_field_) {
    title_field_->SetMahoResolvedTextColor(ResolveSidebarPaletteColor(
        palette_, this, palette_.primary_text, ui::kColorSysOnSurface));
  }
  UpdateAppearance();
}

void SidebarTabRowView::SetSidebarPalette(const MahoSidebarPalette& palette) {
  palette_ = palette;
  last_visual_state_ = AppearanceVisualState::kUninitialized;
  last_title_color_ = SK_ColorTRANSPARENT;
  close_button_images_valid_ = false;
  if (title_field_) {
    title_field_->SetMahoResolvedTextColor(ResolveSidebarPaletteColor(
        palette_, this, palette_.primary_text, ui::kColorSysOnSurface));
  }
  UpdateAudioState(audible_, muted_);
  UpdateAppearance();
}

void SidebarTabRowView::OnFocus() {
  views::View::OnFocus();
  ScrollViewToVisible();
}

bool SidebarTabRowView::OnMousePressed(const ui::MouseEvent& event) {
  if (!event.IsOnlyLeftMouseButton() || is_editing_title_) {
    return views::View::OnMousePressed(event);
  }
  if ((event.flags() & ui::EF_IS_DOUBLE_CLICK) && !is_editing_title_) {
    activate_pending_ = false;
    BeginTitleEditing();
    return true;
  }

  MahoSidebarTabListView* tab_list_view = GetTabListView();
  if (tab_list_view) {
    const bool is_cmd_ctrl = (event.flags() & (ui::EF_COMMAND_DOWN | ui::EF_CONTROL_DOWN)) != 0;
    const bool is_shift = (event.flags() & ui::EF_SHIFT_DOWN) != 0;

    if (is_cmd_ctrl) {
      tab_list_view->ToggleTabSelected(tab_id_);
      activate_pending_ = false;
      return true;
    } else if (is_shift) {
      tab_list_view->SelectRangeTo(tab_id_);
      activate_pending_ = false;
      return true;
    } else {
      if (!tab_list_view->selected_tab_ids().empty()) {
        tab_list_view->ClearSelection();
      }
      activate_pending_ = true;
      activation_handled_for_current_press_ = false;
    }
  } else {
    activate_pending_ = true;
    activation_handled_for_current_press_ = false;
  }

  return views::View::OnMousePressed(event);
}

void SidebarTabRowView::OnMouseReleased(const ui::MouseEvent& event) {
  const bool should_activate = activate_pending_;
  activate_pending_ = false;
  views::View::OnMouseReleased(event);
  if (should_activate) {
    ActivateFromRowClick(event);
  }
}

void SidebarTabRowView::OnMouseEntered(const ui::MouseEvent& event) {
  views::View::OnMouseEntered(event);
  int live_index = tab_index_;
  if (resolve_tab_index_) {
    const int resolved_index = resolve_tab_index_.Run(tab_id_);
    if (resolved_index >= 0) {
      live_index = resolved_index;
    }
  }
  if (!active_ && live_index >= 0 && preview_show_callback_) {
    preview_show_callback_.Run(this, live_index);
  }
  UpdateAppearance();
}

void SidebarTabRowView::OnMouseExited(const ui::MouseEvent& event) {
  views::View::OnMouseExited(event);
  if (preview_hide_callback_) {
    preview_hide_callback_.Run();
  }
  UpdateAppearance();
}

bool SidebarTabRowView::HandleKeyEvent(views::Textfield* sender,
                                       const ui::KeyEvent& event) {
  if (event.type() != ui::EventType::kKeyPressed || sender != title_field_) {
    return false;
  }

  switch (event.key_code()) {
    case ui::VKEY_RETURN:
      EndTitleEditing(true);
      return true;
    case ui::VKEY_ESCAPE:
      EndTitleEditing(false);
      return true;
    default:
      return false;
  }
}

void SidebarTabRowView::ShowContextMenuForViewImpl(
    views::View* source,
    const gfx::Point& point,
    ui::mojom::MenuSourceType source_type) {
  if (is_editing_title_) {
    EndTitleEditing(false);
  }
  TabStripModel* tab_strip_model = browser_ ? browser_->GetTabStripModel() : nullptr;
  const int live_index = resolve_tab_index_ ? resolve_tab_index_.Run(tab_id_) : tab_index_;
  // Suspended tabs have live_index == -1 but must still open the menu; do not
  // gate on ContainsIndex. Single-tab ops self-guard on a valid index.
  if (!tab_strip_model) {
    return;
  }

  views::View* v = parent();
  MahoSidebarTabListView* list_view = nullptr;
  while (v) {
    list_view = views::AsViewClass<MahoSidebarTabListView>(v);
    if (list_view) {
      break;
    }
    v = v->parent();
  }
  if (!list_view) {
    return;
  }
  list_view->ShowTabRowContextMenu(this, live_index, point, source_type, this);
}

bool SidebarTabRowView::GetDropFormats(
    int* formats,
    std::set<ui::ClipboardFormatType>* format_types) {
  *formats = 0;
  format_types->insert(GetMahoDragFormatType());
  return true;
}

bool SidebarTabRowView::CanDrop(const ui::OSExchangeData& data) {
  SidebarDragPayload payload;
  return ReadMahoDragData(data, payload);
}

int SidebarTabRowView::OnDragUpdated(const ui::DropTargetEvent& event) {
  ForwardDragToAutoScroller(this, event);
  SidebarDragPayload payload;
  if (!ReadMahoDragData(event.data(), payload)) {
    active_drop_zone_ = SidebarTabDropZone::kNone;
    drag_over_drop_target_ = false;
    ClearSplitPreview();
    UpdateAppearance();
    return static_cast<int>(ui::mojom::DragOperation::kNone);
  }
  active_drop_zone_ = ResolveDropZone(event, payload);
  if (active_drop_zone_ == SidebarTabDropZone::kNone) {
    drag_over_drop_target_ = false;
    ClearSplitPreview();
    UpdateAppearance();
    return static_cast<int>(ui::mojom::DragOperation::kNone);
  }
  if (active_drop_zone_ == SidebarTabDropZone::kSplit) {
    ShowSplitPreview(MahoSplitViewController::ResolveDropSide(
        event.location().x(), width()));
  } else {
    ClearSplitPreview();
  }
  if (section_ == MahoSidebarTabSection::kNormal &&
      reveal_empty_pinned_drop_lane_callback_ &&
      (payload.origin == SidebarDragOrigin::kPinnedSection ||
       payload.origin == SidebarDragOrigin::kNormalSection ||
       payload.origin == SidebarDragOrigin::kFavorites)) {
    reveal_empty_pinned_drop_lane_callback_.Run();
  }
  drag_over_drop_target_ = true;
  UpdateAppearance();
  return static_cast<int>(ui::mojom::DragOperation::kMove);
}

views::View::DropCallback SidebarTabRowView::GetDropCallback(
    const ui::DropTargetEvent& event) {
  ForwardDragEnded(this);
  SidebarDragPayload drag_payload;
  const bool has_drag_payload = ReadMahoDragData(event.data(), drag_payload);
  const SidebarTabDropZone drop_zone =
      has_drag_payload ? ResolveDropZone(event, drag_payload)
                       : SidebarTabDropZone::kNone;
  const MahoSplitDropSide split_drop_side =
      MahoSplitViewController::ResolveDropSide(event.location().x(), width());
  active_drop_zone_ = SidebarTabDropZone::kNone;
  drag_over_drop_target_ = false;
  ClearSplitPreview();
  UpdateAppearance();
  MahoSidebarTabListView* list_view = GetTabListView();

  return base::BindOnce(
      [](Browser* browser, std::string tab_id,
          MahoSidebarTabSection section, std::string space_id,
          std::string source_parent_folder_id,
          base::RepeatingCallback<int(const std::string&)> resolve_tab_index,
          base::RepeatingCallback<void(const std::string&)>
              favorites_drop_accepted,
          SidebarTabDropZone drop_zone,
          MahoSplitDropSide split_drop_side,
          MahoSidebarTabListView* list_view,
          const ui::DropTargetEvent& event,
          ui::mojom::DragOperation& output_drag_op,
          std::unique_ptr<ui::LayerTreeOwner> drag_image_layer_owner) {
        SidebarDragPayload payload;
        if (!ReadMahoDragData(event.data(), payload)) {
          output_drag_op = ui::mojom::DragOperation::kNone;
          return;
        }
        output_drag_op = ui::mojom::DragOperation::kNone;
        if (drop_zone == SidebarTabDropZone::kNone) {
          return;
        }

        if (payload.node_kind == SidebarNodeKind::kTab &&
            !tab_id.empty()) {

          std::vector<std::string> ids_to_move;
          if (!payload.selected_tab_ids.empty()) {
            ids_to_move = payload.selected_tab_ids;
          } else if (!payload.node_id.empty()) {
            ids_to_move.push_back(payload.node_id);
          }

          std::unique_ptr<RebuildRowsBatchScoper> scoper;
          if (list_view) {
            scoper = std::make_unique<RebuildRowsBatchScoper>(list_view);
          }

          for (const auto& move_id : ids_to_move) {
            if (move_id.empty() || move_id == tab_id) {
              continue;
            }
            SidebarDragPayload single_payload = payload;
            single_payload.node_id = move_id;
            // Prefer the strip index captured when the drag began: re-resolving
            // by id here is racy (the id->index cache can be stale mid-drag and
            // the fallback scan misses tabs whose helper isn't attached yet).
            single_payload.tab_strip_index =
                (move_id == payload.node_id && payload.tab_strip_index >= 0)
                    ? payload.tab_strip_index
                    : (resolve_tab_index ? resolve_tab_index.Run(move_id) : -1);

            const SidebarDropPlan plan =
                ResolveDropPlan(SectionKindFromTabSection(section), single_payload);
            if (!plan.is_valid) {
              continue;
            }

            // A cross-section drop (pin/favorite transition) is a section
            // change, not a split: fall through to the transition path so it
            // dispatches unpin_tab/change_tab_role and reports kMove without a
            // live tab strip. A same-section folder-escape has steps but no
            // transition, so it still enters the split branch below.
            const bool is_cross_section_transition =
                plan.pin_transition.kind !=
                    SidebarDropPlan::PinTransition::Kind::kNone ||
                plan.favorite_transition.kind !=
                    SidebarDropPlan::FavoriteTransition::Kind::kNone;

            if (drop_zone == SidebarTabDropZone::kSplit &&
                !is_cross_section_transition) {
              // The source tab may live inside a folder; run the drop plan
              // first so it exits the folder to root, then create the split.
              if (!plan.steps.empty()) {
                if (list_view) {
                  list_view->HandlePostDropTransition(single_payload, plan);
                }
                ExecuteDropPlan(plan);
              }
              const bool source_before_target =
                  split_drop_side == MahoSplitDropSide::kLeft;
              if (!list_view || single_payload.selected_tab_ids.size() > 1 ||
                  !list_view->CreateSidebarTwoPaneSplit(
                      single_payload.node_id, tab_id, source_before_target)) {
                continue;
              }
              output_drag_op = ui::mojom::DragOperation::kMove;

              // The target pane may have lived inside a folder. GroupSplitTabs
              // only groups split members that are siblings at the same tree
              // depth, so pull the target out to root after the model split is
              // confirmed. The source was already rooted by the drop plan.
              if (!source_parent_folder_id.empty()) {
                DispatchShellEvent("move_tab_to_root",
                                   {{"space_id", space_id},
                                    {"folder_id", source_parent_folder_id},
                                    {"tab_id", tab_id}});
              }
              continue;
            }

            // Drag out: dragging a split pane to a non-split position dissolves
            // its split so the tab separates into an independent tab.
            if (browser) {
              TabStripModel* model = browser->GetTabStripModel();
              const int src_idx = single_payload.tab_strip_index;
              if (model && model->ContainsIndex(src_idx) &&
                  model->GetSplitForTab(src_idx).has_value()) {
                MahoSplitViewController(browser).RemoveSplitForTab(src_idx);
              }
            }

            if (plan.favorite_transition.kind ==
                SidebarDropPlan::FavoriteTransition::Kind::kUnfavorite) {
              if (favorites_drop_accepted) {
                favorites_drop_accepted.Run(single_payload.node_id);
              }
            }
            // For a folder child, escape to root first, apply pin/unpin or
            // favorite transitions second, then reorder before the target.
            // Pinning moves root tabs to the end of pinned, overwriting any
            // before-target reorder performed earlier.
            if (drop_zone == SidebarTabDropZone::kBefore &&
                !single_payload.source_parent_folder_id.empty()) {
              DispatchShellEvent("move_tab_to_root", {
                  {"space_id", space_id},
                  {"folder_id", single_payload.source_parent_folder_id},
                  {"tab_id", single_payload.node_id}});
            }

            if (list_view) {
              list_view->HandlePostDropTransition(single_payload, plan);
            }
            ExecuteDropPlanTransitionsOnly(plan);
            output_drag_op = ui::mojom::DragOperation::kMove;

            // Root reorder only makes sense when the target row itself sits at
            // root; a folder-child target has no root position to insert
            // before, so the escape-to-root above is the whole move.
            if (drop_zone == SidebarTabDropZone::kBefore &&
                source_parent_folder_id.empty()) {
              DispatchReorderRootItem(space_id, SidebarNodeKind::kTab,
                                      single_payload.node_id,
                                      MakeInsertionPointBefore(
                                          SidebarNodeKind::kTab, tab_id));
            }
          }
          return;
        }
        if (payload.node_kind == SidebarNodeKind::kFolder &&
            !payload.node_id.empty() && !tab_id.empty()) {
          const SidebarDropPlan plan =
              ResolveDropPlan(SectionKindFromTabSection(section), payload);
          if (!plan.is_valid) {
            output_drag_op = ui::mojom::DragOperation::kNone;
            return;
          }
          if (list_view) {
            list_view->HandlePostDropTransition(payload, plan);
          }
          ExecuteDropPlanTransitionsOnly(plan);
          if (drop_zone == SidebarTabDropZone::kBefore) {
            output_drag_op = ui::mojom::DragOperation::kMove;
            if (!payload.source_parent_folder_id.empty()) {
              DispatchShellEvent(maho::sidebar::kMoveFolderToRoot,
                                 {{"space_id", space_id},
                                  {"folder_id", payload.node_id}});
            }
            DispatchReorderRootItem(space_id, SidebarNodeKind::kFolder,
                                    payload.node_id,
                                    MakeInsertionPointBefore(
                                        SidebarNodeKind::kTab, tab_id));
          }
        }
      },
      browser_, tab_id_, section_, space_id_,
      source_parent_folder_id_, resolve_tab_index_,
      favorites_drop_accepted_callback_, drop_zone, split_drop_side,
      list_view);
}

void SidebarTabRowView::OnDragExited() {
  ForwardDragEnded(this);
  active_drop_zone_ = SidebarTabDropZone::kNone;
  drag_over_drop_target_ = false;
  ClearSplitPreview();
  UpdateAppearance();
}

void SidebarTabRowView::OnDragDone() {
  views::View::OnDragDone();
  if (layer()) {
    layer()->SetOpacity(1.f);
    DestroyLayer();
  }
  active_drop_zone_ = SidebarTabDropZone::kNone;
  drag_over_drop_target_ = false;
  ClearSplitPreview();
  UpdateAppearance();
  NotifySidebarDragEnded(this);
  if (owning_tab_list_view_) {
    owning_tab_list_view_->OnDragSourceFinished();
  }
}

void SidebarTabRowView::BeginTitleEditing() {
  if (tab_id_.empty() || is_editing_title_ || !title_field_) {
    return;
  }

  is_editing_title_ = true;
  title_button_->SetVisible(false);
  title_field_->SetVisible(true);
  title_field_->SetText(committed_title_);
  title_field_->RequestFocus();
  title_field_->SelectAll(false);
  UpdateAppearance();
}

void SidebarTabRowView::EndTitleEditing(bool commit) {
  if (!is_editing_title_ || !title_field_) {
    return;
  }

  std::u16string next_title = committed_title_;
  if (commit) {
    next_title =
        NormalizeRenamedTitle(std::u16string(title_field_->GetText()));
    committed_title_ = next_title;
    ApplyDisplayedTitle(committed_title_);
    if (rename_callback_) {
      rename_callback_.Run(tab_id_, committed_title_);
    }
  } else {
    title_field_->SetText(committed_title_);
    ApplyDisplayedTitle(committed_title_);
  }

  is_editing_title_ = false;
  title_field_->SetVisible(false);
  title_button_->SetVisible(true);
  RequestFocus();
  UpdateAppearance();
}

void SidebarTabRowView::ApplyDisplayedTitle(const std::u16string& title) {
  if (title_button_) {
    title_button_->SetText(title);
    SyncButtonAccessibleName(title_button_, title);
  }
}

void SidebarTabRowView::ActivateFromRowClick(const ui::MouseEvent& event) {
  if (is_editing_title_ ||
      activation_handled_for_current_press_ ||
      !(event.changed_button_flags() & ui::EF_LEFT_MOUSE_BUTTON)) {
    return;
  }
  if (!HitTestPoint(event.location())) {
    return;
  }
  if (close_button_ && close_button_->GetVisible()) {
    gfx::Point close_button_point = event.location();
    views::View::ConvertPointToTarget(this, close_button_, &close_button_point);
    if (close_button_->HitTestPoint(close_button_point)) {
      return;
    }
  }
  const int live_index =
      resolve_tab_index_ ? resolve_tab_index_.Run(tab_id_) : tab_index_;
  if (live_index < 0) {
    if (activate_callback_) {
      activation_handled_for_current_press_ = true;
      activate_callback_.Run(event);
    }
    return;
  }
  if (!browser_ || !browser_->GetTabStripModel() ||
      !browser_->GetTabStripModel()->ContainsIndex(live_index)) {
    return;
  }
  activation_handled_for_current_press_ = true;
  browser_->GetTabStripModel()->ActivateTabAt(live_index);
}

void SidebarTabRowView::ActivateTabFromEvent(const ui::Event& event) {
  if (MahoSidebarTabListView* list_view = GetTabListView()) {
    const bool is_cmd_ctrl =
        (event.flags() & (ui::EF_COMMAND_DOWN | ui::EF_CONTROL_DOWN)) != 0;
    const bool is_shift = (event.flags() & ui::EF_SHIFT_DOWN) != 0;
    if (is_cmd_ctrl) {
      list_view->ToggleTabSelected(tab_id_);
      return;
    }
    if (is_shift) {
      list_view->SelectRangeTo(tab_id_);
      return;
    }
    if (!list_view->selected_tab_ids().empty()) {
      list_view->ClearSelection();
    }
  }
  const int live_index =
      resolve_tab_index_ ? resolve_tab_index_.Run(tab_id_) : tab_index_;

  if (live_index < 0) {
    if (activate_callback_) {
      activation_handled_for_current_press_ = true;
      activate_callback_.Run(event);
    }
    return;
  }
  if (!browser_ || !browser_->GetTabStripModel() ||
      !browser_->GetTabStripModel()->ContainsIndex(live_index)) {
    return;
  }
  activation_handled_for_current_press_ = true;
  browser_->GetTabStripModel()->ActivateTabAt(live_index);
}

void SidebarTabRowView::WriteDragDataForView(views::View* sender,
                                             const gfx::Point& press_pt,
                                             ui::OSExchangeData* data) {
  SidebarDragPayload payload;
  payload.node_kind = SidebarNodeKind::kTab;
  payload.node_id = tab_id_;
  payload.origin = pinned_ ? SidebarDragOrigin::kPinnedSection
                           : SidebarDragOrigin::kNormalSection;
  payload.space_id = space_id_;
  payload.tab_strip_index = resolve_tab_index_ ? resolve_tab_index_.Run(tab_id_) : tab_index_;
  payload.source_parent_folder_id = source_parent_folder_id_;
  payload.source_folder_child_index = folder_child_index_;

  MahoSidebarTabListView* list_view = GetTabListView();
  const size_t count = list_view && list_view->IsTabSelected(tab_id_)
                           ? list_view->selected_tab_ids().size()
                           : 1;
  if (list_view && list_view->IsTabSelected(tab_id_)) {
    for (const auto& id : list_view->selected_tab_ids()) {
      payload.selected_tab_ids.push_back(id);
    }
  }

  if (auto* split_container =
          views::AsViewClass<SplitGroupContainerView>(parent())) {
    for (views::View* sibling : split_container->children()) {
      if (auto* row = views::AsViewClass<SidebarTabRowView>(sibling)) {
        payload.split_member_tab_ids.push_back(row->tab_id());
      }
    }
  }

  WriteMahoDragData(payload, data);
  NotifySidebarDragStarted(this, tab_id_);
  if (list_view) {
    list_view->SetDragSourceTabId(tab_id_);
  }

  // Hide layer-backed children before Paint() to avoid missing them in the snapshot.
  const bool audio_was_visible = audio_button_->GetVisible();
  const bool close_was_visible = close_button_->GetVisible();
  audio_button_->SetVisible(false);
  close_button_->SetVisible(false);

  // Temporarily move to origin so PaintInfo::CreateRootPaintInfo offset == 0.
  // Without this, Paint() DCHECKs: context.PaintOffset().y() == offset.y().
  const gfx::Rect og_bounds = bounds();
  gfx::Rect adjusted_bounds = og_bounds;
  adjusted_bounds.Offset(-og_bounds.OffsetFromOrigin());
  const gfx::Rect unmirrored_bounds =
      parent() ? parent()->GetMirroredRect(adjusted_bounds) : adjusted_bounds;
  SetBoundsRect(unmirrored_bounds);

  const SkColor base_color = palette_.surface_stops.empty()
                                 ? palette_.row_active
                                 : palette_.surface_stops.back();
  const SkColor overlay_color = palette_.row_active;
  auto drag_image = CreateSidebarDragImageWithBackground(
      this, base_color, overlay_color, static_cast<float>(kRowCornerRadiusDp));

  SetBoundsRect(og_bounds);
  audio_button_->SetVisible(audio_was_visible);
  close_button_->SetVisible(close_was_visible);

  if (count > 1) {
    float scale = (GetWidget() && GetWidget()->GetCompositor())
                      ? GetWidget()->GetCompositor()->device_scale_factor()
                      : 1.0f;
    gfx::Canvas canvas(drag_image.size(), scale, /*is_opaque=*/false);
    canvas.DrawImageInt(drag_image, 0, 0);

    const int badge_radius = 9;
    const int badge_x = drag_image.size().width() - badge_radius - 4;
    const int badge_y = badge_radius + 4;

    cc::PaintFlags paint;
    paint.setAntiAlias(true);
    paint.setStyle(cc::PaintFlags::kFill_Style);
    paint.setColor(SkColorSetRGB(0x1a, 0x73, 0xe8));
    canvas.DrawCircle(gfx::PointF(badge_x, badge_y), badge_radius, paint);

    paint.setStyle(cc::PaintFlags::kStroke_Style);
    paint.setStrokeWidth(1.5f);
    paint.setColor(SK_ColorWHITE);
    canvas.DrawCircle(gfx::PointF(badge_x, badge_y), badge_radius, paint);

    std::u16string text = base::UTF8ToUTF16(std::to_string(count));
    gfx::FontList font_list({"Helvetica", "Arial", "sans-serif"}, gfx::Font::NORMAL, 10, gfx::Font::Weight::BOLD);
    canvas.DrawStringRectWithFlags(
        text, font_list, SK_ColorWHITE,
        gfx::Rect(badge_x - badge_radius, badge_y - badge_radius, badge_radius * 2, badge_radius * 2),
        gfx::Canvas::TEXT_ALIGN_CENTER | gfx::Canvas::NO_SUBPIXEL_RENDERING);

    drag_image = gfx::ImageSkia::CreateFromBitmap(canvas.GetBitmap(), scale);
  }

  data->provider().SetDragImage(drag_image, gfx::Vector2d());
  NotifySidebarDragGhostImage(this, drag_image, gfx::Vector2d());

  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);
  layer()->SetOpacity(0.35f);
}

int SidebarTabRowView::GetDragOperationsForView(views::View* sender,
                                                const gfx::Point& p) {
  return static_cast<int>(ui::mojom::DragOperation::kMove);
}

bool SidebarTabRowView::CanStartDragForView(views::View* sender,
                                            const gfx::Point& press_pt,
                                            const gfx::Point& current_pt) {
  return (press_pt - current_pt).Length() >= kDragStartThresholdDp;
}

void SidebarTabRowView::SetActive(bool active) {
  if (active_ == active) {
    return;
  }
  active_ = active;
  UpdateAppearance();
}

void SidebarTabRowView::SetControlled(bool controlled) {
  if (controlled_ == controlled) {
    return;
  }
  controlled_ = controlled;
  GetViewAccessibility().SetDescription(controlled ? u"Controlled by Maho"
                                                   : std::u16string());
  if (controlled && gfx::Animation::ShouldRenderRichAnimation()) {
    control_pulse_animation_.Reset(0.0);
    control_pulse_animation_.Show();
  } else {
    control_pulse_animation_.Reset(0.0);
  }
  SchedulePaint();
}

void SidebarTabRowView::UpdateAppearance() {
  const bool hovered = IsMouseHovered();
  const bool show_close_button = hovered && !is_editing_title_;

  // FIX 3: Determine intended visual state and skip SetBackground/SetBorder
  // when unchanged. P3: Eliminates redundant background/border allocations;
  // UpdateAppearance() invoked per-row on every hover enter/exit (~870×).
  AppearanceVisualState current_state;
  if (drag_over_drop_target_) {
    current_state = AppearanceVisualState::kDropTarget;
  } else if (active_) {
    current_state = AppearanceVisualState::kActive;
  } else if (is_selected_) {
    current_state = AppearanceVisualState::kSelected;
  } else if (hovered) {
    current_state = AppearanceVisualState::kHovered;
  } else {
    current_state = AppearanceVisualState::kIdle;
  }

  const bool wants_split_preview =
      !split_compact_ && drag_over_drop_target_ &&
      active_drop_zone_ == SidebarTabDropZone::kSplit;
  UpdateSplitPreviewAnimation(wants_split_preview);
  const bool keep_split_preview_paint =
      !split_compact_ &&
      (wants_split_preview || split_preview_animation_.is_animating() ||
       split_preview_animation_.GetCurrentValue() > 0.0);
  const bool split_preview_paint_changed =
      keep_split_preview_paint != split_preview_paint_installed_;

  if (current_state != last_visual_state_ || split_preview_paint_changed) {
    last_visual_state_ = current_state;
    split_preview_paint_installed_ = keep_split_preview_paint;
    if (split_compact_) {
      // Split panes render transparent; the unified split container paints the
      // shared active/hover background so both halves highlight together.
      SetBackground(nullptr);
      SetBorder(nullptr);
    } else if (keep_split_preview_paint) {
      SetBackground(std::make_unique<SidebarSplitDropTargetBackground>(
          base::BindRepeating(&SidebarTabRowView::GetSplitPreviewProgress,
                              base::Unretained(this)),
          base::BindRepeating(&SidebarTabRowView::GetSplitPreviewSide,
                              base::Unretained(this)),
          palette_.row_active,
          ResolveSidebarPaletteColor(palette_, this, palette_.outline,
                                     ui::kColorSysDivider),
          ResolveSidebarPaletteColor(palette_, this, palette_.focus_ring,
                                     ui::kColorSysPrimary)));
      SetBorder(views::CreateEmptyBorder(
          gfx::Insets(kDropIndicatorOutlineThicknessDp)));
    } else {
      switch (current_state) {
        case AppearanceVisualState::kDropTarget:
          if (active_drop_zone_ == SidebarTabDropZone::kSplit) {
            SetBackground(std::make_unique<SidebarSplitDropTargetBackground>(
                base::BindRepeating(
                    &SidebarTabRowView::GetSplitPreviewProgress,
                    base::Unretained(this)),
                base::BindRepeating(&SidebarTabRowView::GetSplitPreviewSide,
                                     base::Unretained(this)),
                palette_.row_active,
                ResolveSidebarPaletteColor(palette_, this, palette_.outline,
                                           ui::kColorSysDivider),
                ResolveSidebarPaletteColor(palette_, this, palette_.focus_ring,
                                           ui::kColorSysPrimary)));
            SetBorder(views::CreateEmptyBorder(
                gfx::Insets(kDropIndicatorOutlineThicknessDp)));
          } else {
            SetBackground(views::CreateRoundedRectBackground(
                ResolveSidebarPaletteColor(palette_, this, palette_.row_active,
                                           ui::kColorSysSurface4),
                kRowCornerRadiusDp));
            SetBorder(views::CreateRoundedRectBorder(
                kDropIndicatorOutlineThicknessDp, kRowCornerRadiusDp,
                ResolveSidebarPaletteColor(palette_, this, palette_.focus_ring,
                                           ui::kColorSysPrimary)));
          }
          break;
        case AppearanceVisualState::kActive:
          SetBackground(views::CreateRoundedRectBackground(
              hovered ? palette_.row_hover : palette_.row_active,
              kRowCornerRadiusDp));
          SetBorder(nullptr);
          break;
        case AppearanceVisualState::kSelected:
          SetBackground(views::CreateRoundedRectBackground(
              hovered ? palette_.row_hover : palette_.row_selected,
              kRowCornerRadiusDp));
          SetBorder(nullptr);
          break;
        case AppearanceVisualState::kHovered:
          SetBackground(views::CreateRoundedRectBackground(
              palette_.row_hover, kRowCornerRadiusDp));
          SetBorder(nullptr);
          break;
        case AppearanceVisualState::kIdle:
        case AppearanceVisualState::kUninitialized:
          SetBackground(nullptr);
          SetBorder(nullptr);
          break;
      }
    }
  }

  const SkColor desired_title_color = ResolveSidebarPaletteColor(
      palette_, this, palette_.primary_text, ui::kColorSysOnSurface);
  if (desired_title_color != last_title_color_) {
    last_title_color_ = desired_title_color;
    title_button_->SetTextColor(views::Button::STATE_NORMAL, desired_title_color);
    title_button_->SetTextColor(views::Button::STATE_HOVERED, desired_title_color);
    title_button_->SetTextColor(views::Button::STATE_PRESSED, desired_title_color);
    // macOS renders controls in an inactive (non-key) window as STATE_DISABLED
    // (kInactiveWidgetControlsAppearDisabled), regardless of the button's real
    // enabled state. Bind the disabled color to the same resolved palette text
    // color so the tab title stays readable and its color is invariant to
    // window focus, instead of the default disabled color that is not tied
    // to the space theme.
    title_button_->SetTextColor(views::Button::STATE_DISABLED, desired_title_color);
  }
  if (favicon_view_ && favicon_view_->layer()) {
    favicon_view_->DestroyLayer();
  }
  const bool show_minus =
      (section_ == MahoSidebarTabSection::kPinned) && !is_suspended_;
  // FIX 2: Skip vector icon re-render when close button icon state unchanged.
  if (!close_button_images_valid_ ||
      show_minus != last_close_button_show_minus_) {
    close_button_images_valid_ = true;
    last_close_button_show_minus_ = show_minus;
    const gfx::VectorIcon& close_icon =
        show_minus ? maho_lucide_icons::kMinusIcon
                   : maho_lucide_icons::kXIcon;
    const SkColor neutral_glyph = ResolveSidebarPaletteColor(
        palette_, this, palette_.neutral_glyph, ui::kColorSysOnSurfaceSubtle);
    close_button_->SetImageModel(
        views::Button::STATE_NORMAL,
        ui::ImageModel::FromVectorIcon(close_icon, neutral_glyph,
                                       kCloseIconSizeDp));
    close_button_->SetImageModel(
        views::Button::STATE_HOVERED,
        ui::ImageModel::FromVectorIcon(close_icon, desired_title_color,
                                       kCloseIconSizeDp));
    close_button_->SetImageModel(
        views::Button::STATE_PRESSED,
        ui::ImageModel::FromVectorIcon(close_icon, desired_title_color,
                                       kCloseIconSizeDp));
    close_button_->SetImageModel(
        views::Button::STATE_DISABLED,
        ui::ImageModel::FromVectorIcon(close_icon, neutral_glyph,
                                       kCloseIconSizeDp));
    const std::u16string close_tooltip =
        show_minus ? u"Unload tab" : u"Close tab";
    close_button_->SetTooltipText(close_tooltip);
    close_button_->SetAccessibleName(close_tooltip);
  }
  if (title_field_) {
    title_field_->SetVisible(is_editing_title_);
  }
  title_button_->SetVisible(!is_editing_title_);
  // P3: Use opacity instead of SetVisible to avoid propagating
  // InvalidateLayout up to BrowserView, which would trigger a full BoxLayout
  // re-measure pass across all ~1740 sidebar children on every hover enter/exit.
  if (split_compact_) {
    // Split panes hide close by default (preserve title width); reveal on hover
    // so closing one pane dissolves the split.
    if (close_button_->GetVisible() != show_close_button) {
      close_button_->SetVisible(show_close_button);
    }
    close_button_->layer()->SetOpacity(show_close_button ? 1.0f : 0.0f);
    close_button_->SetCanProcessEventsWithinSubtree(show_close_button);
  } else {
    const float close_opacity = show_close_button ? 1.0f : 0.0f;
    close_button_->layer()->SetOpacity(close_opacity);
    close_button_->SetCanProcessEventsWithinSubtree(show_close_button);
  }
}

bool SidebarTabRowView::IsSplitDropEligible(
    const SidebarDragPayload& payload) const {
  if (payload.node_kind != SidebarNodeKind::kTab || !browser_) {
    return false;
  }

  TabStripModel* model = browser_->GetTabStripModel();
  if (!model) {
    return false;
  }

  const int source_index =
      model->ContainsIndex(payload.tab_strip_index)
          ? payload.tab_strip_index
          : (resolve_tab_index_ ? resolve_tab_index_.Run(payload.node_id)
                                : -1);
  const int target_index =
      resolve_tab_index_ ? resolve_tab_index_.Run(tab_id_) : tab_index_;
  const bool source_is_target =
      payload.node_id == tab_id_ ||
      (model->ContainsIndex(source_index) && model->ContainsIndex(target_index) &&
       model->GetWebContentsAt(source_index) ==
           model->GetWebContentsAt(target_index));
  const bool source_already_split =
      model->ContainsIndex(source_index) &&
      model->GetSplitForTab(source_index).has_value();
  const bool target_already_split =
      model->ContainsIndex(target_index) &&
      model->GetSplitForTab(target_index).has_value();
  return MahoSplitViewController::IsSplitDropEligible(
      payload.selected_tab_ids.size() > 1, source_is_target,
      source_already_split, target_already_split);
}

SidebarTabRowView::SidebarTabDropZone SidebarTabRowView::ResolveDropZone(
    const ui::DropTargetEvent& event,
    const SidebarDragPayload& payload) const {
  if (payload.node_id.empty() || payload.node_id == tab_id_) {
    return SidebarTabDropZone::kNone;
  }

  if (payload.node_kind == SidebarNodeKind::kTab &&
      !source_parent_folder_id_.empty() &&
      !payload.source_parent_folder_id.empty() &&
      payload.source_parent_folder_id == source_parent_folder_id_) {
    const int src = payload.source_folder_child_index;
    const int tgt = folder_child_index_;
    if (src == tgt - 1 || src == tgt + 1) {
      return SidebarTabDropZone::kNone;
    }
  }

  // Cross-section tab bodies are role transitions, never split previews.
  // Only same-section tab bodies split; insertion lanes remain the precise
  // reorder surface.
  if (payload.node_kind == SidebarNodeKind::kTab) {
    const bool is_cross_section_drop =
        payload.origin == SidebarDragOrigin::kFavorites ||
        (section_ == MahoSidebarTabSection::kPinned &&
         payload.origin == SidebarDragOrigin::kNormalSection) ||
        (section_ == MahoSidebarTabSection::kNormal &&
         payload.origin == SidebarDragOrigin::kPinnedSection);
    if (is_cross_section_drop) {
      return SidebarTabDropZone::kBefore;
    }
    if (browser_) {
      return IsSplitDropEligible(payload) ? SidebarTabDropZone::kSplit
                                          : SidebarTabDropZone::kNone;
    }
    const int row_height = std::max(height(), 1);
    const int y = event.location().y();
    if (y < row_height / 3) {
      return SidebarTabDropZone::kBefore;
    }
    return SidebarTabDropZone::kSplit;
  }

  // Non-tab (folder) payloads keep the top-third reorder-before behavior.
  const int row_height = std::max(height(), 1);
  const int y = event.location().y();
  if (y < row_height / 3) {
    return SidebarTabDropZone::kBefore;
  }
  return SidebarTabDropZone::kNone;
}

// --- SidebarFolderRowView ---

SidebarFolderRowView::SidebarFolderRowView(
    const SidebarTreeNode& node,
    MahoSidebarTabSection section,
    std::string active_space_id,
    Browser* browser,
    base::RepeatingClosure toggle_callback,
    base::RepeatingCallback<void(const std::string&, const std::u16string&)>
        rename_callback,
    std::string next_sibling_folder_id,
    std::string parent_folder_id,
    bool has_children)
    : folder_id_(node.folder_id),
      space_id_(std::move(active_space_id)),
      folder_is_pinned_(node.folder_is_pinned),
      next_sibling_folder_id_(std::move(next_sibling_folder_id)),
      parent_folder_id_(std::move(parent_folder_id)),
      has_children_(has_children),
      is_expanded_(node.is_expanded),
      section_(section),
      browser_(browser),
      rename_callback_(std::move(rename_callback)),
      committed_name_(node.folder_name.empty() ? u"Folder" : node.folder_name) {
  SetNotifyEnterExitOnChild(true);
  toggle_callback_ = toggle_callback;
  const int indent = node.depth * kFolderIndentDp;
  auto row_insets = gfx::Insets::TLBR(0, kFolderRowLeadingInsetDp + indent,
                                      0, 7);

  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, row_insets,
      kRowIconTextSpacingDp));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  folder_glyph_container_ = AddChildView(std::make_unique<views::View>());
  folder_glyph_container_->SetPreferredSize(
      gfx::Size(kFolderGlyphContainerSizeDp, kFolderGlyphContainerSizeDp));
  auto* folder_glyph_layout = folder_glyph_container_->SetLayoutManager(
      std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kHorizontal,
                                         gfx::Insets(), 0));
  folder_glyph_layout->set_main_axis_alignment(
      views::BoxLayout::MainAxisAlignment::kCenter);
  folder_glyph_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  folder_icon_ = folder_glyph_container_->AddChildView(
      std::make_unique<views::ImageView>());
  folder_icon_->SetImageSize(gfx::Size(kFolderGlyphSizeDp, kFolderGlyphSizeDp));

  label_button_ = static_cast<views::LabelButton*>(AddChildView(
      std::unique_ptr<views::View>(
          std::make_unique<SidebarDropForwardingLabelButton>(
              base::BindRepeating(&SidebarFolderRowView::ToggleFromLabelButton,
                                  base::Unretained(this)),
              committed_name_, this)
              .release())));
  label_button_->SetBorder(nullptr);
  label_button_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label_button_->SetMinSize(gfx::Size(0, kFolderRowHeightDp));
  label_button_->SetMaxSize(
      gfx::Size(sidebar_layout::kRuntimeRailWidthMaxDp, kFolderRowHeightDp));
  label_button_->SetFocusRingCornerRadius(kRowCornerRadiusDp);
  label_button_->SetRequestFocusOnPress(false);
  // Label colors for every state come from the palette in UpdateAppearance().
  label_button_->SetTextSubpixelRenderingEnabled(false);
  label_button_->SetLabelStyle(views::style::STYLE_BODY_2_MEDIUM);
  layout->SetFlexForView(label_button_, 1);

  auto name_field = std::make_unique<views::Textfield>();
  name_field->SetVisible(false);
  name_field->SetBorder(nullptr);
  // Match the tab rename field: no opaque OS-theme box over the Space row.
  name_field->SetBackgroundColor(SK_ColorTRANSPARENT);
  name_field->SetController(this);
  name_field->SetAccessibleName(u"Rename folder");
  name_field_ = AddChildView(std::move(name_field));
  layout->SetFlexForView(name_field_, 1);

  label_button_->set_context_menu_controller(this);
  label_button_->set_drag_controller(this);

  set_context_menu_controller(this);
  set_drag_controller(this);

  UpdateAppearance();
}

SidebarFolderRowView::~SidebarFolderRowView() = default;

void SidebarFolderRowView::ShowContextMenuForViewImpl(
    views::View* source,
    const gfx::Point& point,
    ui::mojom::MenuSourceType source_type) {
  if (is_editing_name_) {
    EndFolderEditing(false);
  }
  active_folder_context_menu_ = std::make_unique<MahoFolderContextMenu>(
      browser_, space_id_, folder_id_, parent_folder_id_,
      base::BindOnce(&SidebarFolderRowView::BeginFolderEditing,
                     weak_factory_.GetWeakPtr()));
  active_folder_menu_model_ = active_folder_context_menu_->BuildMenuModel();
  active_folder_menu_runner_ = std::make_unique<views::MenuRunner>(
      active_folder_menu_model_.get(),
      views::MenuRunner::CONTEXT_MENU);
  active_folder_menu_runner_->RunMenuAt(
      GetWidget(), nullptr, gfx::Rect(point, gfx::Size()),
      views::MenuAnchorPosition::kTopLeft, source_type);
}

bool SidebarFolderRowView::HandleKeyEvent(views::Textfield* sender,
                                           const ui::KeyEvent& event) {
  if (event.type() != ui::EventType::kKeyPressed || sender != name_field_) {
    return false;
  }
  switch (event.key_code()) {
    case ui::VKEY_RETURN:
      EndFolderEditing(true);
      return true;
    case ui::VKEY_ESCAPE:
      EndFolderEditing(false);
      return true;
    default:
      return false;
  }
}

void SidebarFolderRowView::BeginFolderEditing() {
  if (folder_id_.empty() || is_editing_name_ || !name_field_) {
    return;
  }
  is_editing_name_ = true;
  label_button_->SetVisible(false);
  name_field_->SetVisible(true);
  name_field_->SetText(committed_name_);
  InvalidateLayout();
  DeprecatedLayoutImmediately();
  name_field_->RequestFocus();
  name_field_->SelectAll(false);
  UpdateAppearance();
}

void SidebarFolderRowView::EndFolderEditing(bool commit) {
  if (!is_editing_name_ || !name_field_) {
    return;
  }
  if (commit) {
    std::u16string next_name =
        NormalizeRenamedTitle(std::u16string(name_field_->GetText()));
    if (next_name.empty()) {
      next_name = committed_name_;
    }
    committed_name_ = next_name;
    if (label_button_) {
      label_button_->SetText(committed_name_);
    }
    if (rename_callback_) {
      rename_callback_.Run(folder_id_, committed_name_);
    }
  } else {
    name_field_->SetText(committed_name_);
    if (label_button_) {
      label_button_->SetText(committed_name_);
    }
  }
  is_editing_name_ = false;
  name_field_->SetVisible(false);
  label_button_->SetVisible(true);
  RequestFocus();
  UpdateAppearance();
}

void SidebarFolderRowView::OnThemeChanged() {
  views::View::OnThemeChanged();
  if (name_field_) {
    name_field_->SetMahoResolvedTextColor(ResolveSidebarPaletteColor(
        palette_, this, palette_.primary_text, ui::kColorSysOnSurface));
  }
  UpdateAppearance();
}

void SidebarFolderRowView::SetSidebarPalette(
    const MahoSidebarPalette& palette) {
  palette_ = palette;
  icon_cache_valid_ = false;
  last_folder_visual_state_ = FolderVisualState::kIdle;
  if (name_field_) {
    name_field_->SetMahoResolvedTextColor(ResolveSidebarPaletteColor(
        palette_, this, palette_.primary_text, ui::kColorSysOnSurface));
  }
  UpdateAppearance();
}

void SidebarFolderRowView::OnMouseEntered(const ui::MouseEvent& event) {
  views::View::OnMouseEntered(event);
  UpdateAppearance();
  if (show_popup_callback_ && has_children_) {
    show_popup_callback_.Run();
  }
}

void SidebarFolderRowView::OnMouseExited(const ui::MouseEvent& event) {
  views::View::OnMouseExited(event);
  UpdateAppearance();
  if (hide_popup_callback_) {
    hide_popup_callback_.Run();
  }
}

bool SidebarFolderRowView::OnMousePressed(const ui::MouseEvent& event) {
  toggle_pending_ = event.IsOnlyLeftMouseButton() && !is_editing_name_;
  toggle_handled_for_current_press_ = false;
  if (toggle_pending_) {
    return true;
  }
  return views::View::OnMousePressed(event);
}

void SidebarFolderRowView::OnMouseReleased(const ui::MouseEvent& event) {
  const bool should_toggle = toggle_pending_;
  toggle_pending_ = false;
  views::View::OnMouseReleased(event);
  if (!should_toggle || toggle_handled_for_current_press_) {
    return;
  }
  if (!(event.changed_button_flags() & ui::EF_LEFT_MOUSE_BUTTON)) {
    return;
  }
  if (!HitTestPoint(event.location())) {
    return;
  }
  RunToggleDeferred();
}

void SidebarFolderRowView::ToggleFromLabelButton(const ui::Event& event) {
  if (is_editing_name_ || toggle_handled_for_current_press_) {
    return;
  }
  toggle_handled_for_current_press_ = true;
  RunToggleDeferred();
}

void SidebarFolderRowView::RunToggleDeferred() {
  // The toggle callback rebuilds the sidebar (RebuildRows), which destroys
  // this view and its child label button. Running it synchronously inside
  // a mouse event handler leaves the views::Widget mouse-capture state
  // referencing a freed view, which manifests as a UI freeze on subsequent
  // clicks. Defer the toggle to the next message-loop iteration so the
  // current event finishes unwinding and the widget releases capture before
  // we tear the row down.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](base::WeakPtr<SidebarFolderRowView> weak_self) {
            if (!weak_self) {
              return;
            }
            weak_self->toggle_handled_for_current_press_ = false;
            if (weak_self->toggle_callback_) {
              weak_self->toggle_callback_.Run();
            }
          },
          weak_factory_.GetWeakPtr()));
}

bool SidebarFolderRowView::GetDropFormats(
    int* formats,
    std::set<ui::ClipboardFormatType>* format_types) {
  *formats = 0;
  format_types->insert(GetMahoDragFormatType());
  return true;
}

bool SidebarFolderRowView::CanDrop(const ui::OSExchangeData& data) {
  SidebarDragPayload payload;
  return ReadMahoDragData(data, payload);
}

int SidebarFolderRowView::OnDragUpdated(const ui::DropTargetEvent& event) {
  ForwardDragToAutoScroller(this, event);
  SidebarDragPayload payload;
  if (!ReadMahoDragData(event.data(), payload)) {
    active_drop_zone_ = SidebarFolderDropZone::kNone;
    drag_over_drop_target_ = false;
    UpdateAppearance();
    return static_cast<int>(ui::mojom::DragOperation::kNone);
  }
  active_drop_zone_ = ResolveDropZone(event, payload);
  if (active_drop_zone_ == SidebarFolderDropZone::kNone) {
    drag_over_drop_target_ = false;
    UpdateAppearance();
    return static_cast<int>(ui::mojom::DragOperation::kNone);
  }
  if (reveal_empty_pinned_drop_lane_callback_ &&
      (payload.origin == SidebarDragOrigin::kPinnedSection ||
       payload.origin == SidebarDragOrigin::kNormalSection ||
       payload.origin == SidebarDragOrigin::kFavorites)) {
    reveal_empty_pinned_drop_lane_callback_.Run();
  }
  drag_over_drop_target_ = true;
  UpdateAppearance();

  if (active_drop_zone_ == SidebarFolderDropZone::kInto && !is_expanded_ &&
      spring_expand_callback_ && !spring_load_timer_.IsRunning()) {
    spring_load_timer_.Start(
        FROM_HERE, base::Milliseconds(700),
        base::BindOnce(
            [](base::WeakPtr<SidebarFolderRowView> self) {
              if (self && self->spring_expand_callback_) {
                self->spring_expand_callback_.Run();
              }
            },
            weak_factory_.GetWeakPtr()));
  } else if (active_drop_zone_ != SidebarFolderDropZone::kInto) {
    spring_load_timer_.Stop();
  }

  return static_cast<int>(ui::mojom::DragOperation::kMove);
}

views::View::DropCallback SidebarFolderRowView::GetDropCallback(
    const ui::DropTargetEvent& event) {
  ForwardDragEnded(this);
  SidebarDragPayload payload;
  SidebarFolderDropZone drop_zone = SidebarFolderDropZone::kNone;
  if (ReadMahoDragData(event.data(), payload)) {
    drop_zone = ResolveDropZone(event, payload);
  }

  MahoSidebarTabListView* list_view = GetTabListView();

  return base::BindOnce(
      [](std::string folder_id, std::string space_id, bool folder_is_pinned,
         SidebarFolderDropZone drop_zone, std::string next_sibling_folder_id,
         std::string parent_folder_id,
         base::RepeatingCallback<void(const std::string&)>
             favorites_drop_accepted,
         MahoSidebarTabListView* list_view,
         const ui::DropTargetEvent& event,
         ui::mojom::DragOperation& output_drag_op,
         std::unique_ptr<ui::LayerTreeOwner> drag_image_layer_owner) {
        SidebarDragPayload payload;
        if (!ReadMahoDragData(event.data(), payload)) {
          output_drag_op = ui::mojom::DragOperation::kNone;
          return;
        }
        if (payload.node_id == folder_id) {
          output_drag_op = ui::mojom::DragOperation::kNone;
          return;
        }
        const SectionKind section_kind =
            folder_is_pinned ? SectionKind::kPinned : SectionKind::kNormal;
        const SidebarDropPlan plan = ResolveDropPlan(section_kind, payload);
        if (!plan.is_valid) {
          output_drag_op = ui::mojom::DragOperation::kNone;
          return;
        }
        if (drop_zone == SidebarFolderDropZone::kNone ||
            (drop_zone == SidebarFolderDropZone::kAfter &&
             payload.node_kind == SidebarNodeKind::kFolder &&
             payload.node_id == next_sibling_folder_id)) {
          output_drag_op = ui::mojom::DragOperation::kNone;
          return;
        }
        if (plan.favorite_transition.kind ==
            SidebarDropPlan::FavoriteTransition::Kind::kUnfavorite) {
          if (favorites_drop_accepted) {
            favorites_drop_accepted.Run(payload.node_id);
          }
        }
        if (list_view) {
          list_view->HandlePostDropTransition(payload, plan);
        }
        if (payload.node_kind == SidebarNodeKind::kTab &&
            drop_zone == SidebarFolderDropZone::kInto) {
          ExecuteDropPlanTransitionsOnly(plan);
        } else {
          ExecuteDropPlan(plan);
        }
        output_drag_op = ui::mojom::DragOperation::kMove;
        if (payload.node_kind == SidebarNodeKind::kTab) {
          if (drop_zone == SidebarFolderDropZone::kBefore) {
            DispatchReorderRootItem(space_id, SidebarNodeKind::kTab,
                                    payload.node_id,
                                    MakeInsertionPointBefore(
                                        SidebarNodeKind::kFolder, folder_id));
            return;
          }
          if (drop_zone == SidebarFolderDropZone::kInto) {
            DispatchShellEvent("move_tab_to_folder", {
                {"space_id", space_id},
                {"folder_id", folder_id},
                {"tab_id", payload.node_id}});
            return;
          }
          DispatchReorderRootItem(
              space_id, SidebarNodeKind::kTab, payload.node_id,
              next_sibling_folder_id.empty()
                  ? MakeInsertionPointAppend()
                  : MakeInsertionPointBefore(SidebarNodeKind::kFolder,
                                             next_sibling_folder_id));
          return;
        }

        if (payload.node_kind == SidebarNodeKind::kFolder) {
          if (drop_zone == SidebarFolderDropZone::kBefore) {
            if (!parent_folder_id.empty()) {
              DispatchShellEvent("reorder_folder", {
                  {"space_id", space_id},
                  {"folder_id", payload.node_id},
                  {"parent_folder_id", parent_folder_id},
                  {"before_folder_id", folder_id}});
            } else {
              DispatchShellEvent("reorder_folder", {
                  {"space_id", space_id},
                  {"folder_id", payload.node_id},
                  {"before_folder_id", folder_id}});
            }
            return;
          }
          if (drop_zone == SidebarFolderDropZone::kInto) {
            DispatchShellEvent("move_folder_into_folder", {
                {"space_id", space_id},
                {"folder_id", payload.node_id},
                {"target_folder_id", folder_id}});
            return;
          }
          if (!parent_folder_id.empty()) {
            std::vector<std::pair<std::string, std::string>> fields = {
                {"space_id", space_id},
                {"folder_id", payload.node_id},
                {"parent_folder_id", parent_folder_id}};
            if (!next_sibling_folder_id.empty()) {
              fields.push_back({"before_folder_id", next_sibling_folder_id});
            }
            DispatchShellEvent("reorder_folder", fields);
            return;
          }
          if (!next_sibling_folder_id.empty()) {
            DispatchShellEvent("reorder_folder", {
                {"space_id", space_id},
                {"folder_id", payload.node_id},
                {"before_folder_id", next_sibling_folder_id}});
            return;
          }
          DispatchShellEvent("reorder_folder", {
              {"space_id", space_id},
              {"folder_id", payload.node_id}});
        }
       },
       folder_id_, space_id_, folder_is_pinned_, drop_zone,
       next_sibling_folder_id_, parent_folder_id_,
       favorites_drop_accepted_callback_, list_view);
}

MahoSidebarTabListView* SidebarFolderRowView::GetTabListView() {
  views::View* v = parent();
  while (v) {
    if (auto* list_view = views::AsViewClass<MahoSidebarTabListView>(v)) {
      return list_view;
    }
    v = v->parent();
  }
  return nullptr;
}

void SidebarFolderRowView::OnDragExited() {
  spring_load_timer_.Stop();
  ForwardDragEnded(this);
  active_drop_zone_ = SidebarFolderDropZone::kNone;
  drag_over_drop_target_ = false;
  UpdateAppearance();
}

void SidebarFolderRowView::OnDragDone() {
  spring_load_timer_.Stop();
  views::View::OnDragDone();
  NotifySidebarDragEnded(this);
}

void SidebarFolderRowView::WriteDragDataForView(views::View* sender,
                                                const gfx::Point& press_pt,
                                                ui::OSExchangeData* data) {
  SidebarDragPayload payload;
  payload.node_kind = SidebarNodeKind::kFolder;
  payload.node_id = folder_id_;
  payload.origin = folder_is_pinned_ ? SidebarDragOrigin::kPinnedSection
                                     : SidebarDragOrigin::kNormalSection;
  payload.space_id = space_id_;
  payload.source_parent_folder_id = parent_folder_id_;
  WriteMahoDragData(payload, data);
  data->provider().SetDragImage(CreateSidebarDragImage(sender),
                                gfx::Vector2d(0, 0));
}

int SidebarFolderRowView::GetDragOperationsForView(views::View* sender,
                                                   const gfx::Point& p) {
  return static_cast<int>(ui::mojom::DragOperation::kMove);
}

bool SidebarFolderRowView::CanStartDragForView(views::View* sender,
                                               const gfx::Point& press_pt,
                                               const gfx::Point& current_pt) {
  return (press_pt - current_pt).Length() >= kDragStartThresholdDp;
}

void SidebarFolderRowView::SetExpanded(bool expanded) {
  is_expanded_ = expanded;
  UpdateAppearance();
}

void SidebarFolderRowView::UpdateAppearance() {
  const bool show_drop_target = drag_over_drop_target_;
  const bool hovered = !show_drop_target && IsMouseHovered();
  const bool expanded = is_expanded_;
  if (folder_glyph_container_) {
    folder_glyph_container_->SetBackground(nullptr);
    folder_glyph_container_->SetBorder(nullptr);
  }
  const SkColor icon_color = show_drop_target
                                 ? ResolveSidebarPaletteColor(
                                       palette_, this, palette_.focus_ring,
                                       ui::kColorSysPrimary)
                                 : ResolveSidebarPaletteColor(
                                       palette_, this, palette_.neutral_glyph,
                                       ui::kColorSysOnSurface);
  if (!icon_cache_valid_ || cached_icon_expanded_ != expanded ||
      cached_icon_color_ != icon_color) {
    cached_icon_expanded_ = expanded;
    cached_icon_color_ = icon_color;
    icon_cache_valid_ = true;
    if (folder_icon_) {
      folder_icon_->SetImage(ui::ImageModel::FromVectorIcon(
          expanded ? maho_lucide_icons::kFolderOpenIcon
                   : maho_lucide_icons::kFolderIcon,
           icon_color,
          kFolderGlyphSizeDp));
    }
  }
  if (label_button_) {
    label_button_->SetVisible(!is_editing_name_);
    label_button_->SetTextColor(views::Button::STATE_NORMAL,
                                show_drop_target
                                    ? ResolveSidebarPaletteColor(
                                          palette_, this, palette_.focus_ring,
                                          ui::kColorSysPrimary)
                                    : ResolveSidebarPaletteColor(
                                          palette_, this, palette_.primary_text,
                                          ui::kColorSysOnSurface));
    label_button_->SetTextColor(views::Button::STATE_HOVERED,
                                show_drop_target
                                    ? ResolveSidebarPaletteColor(
                                          palette_, this, palette_.focus_ring,
                                          ui::kColorSysPrimary)
                                    : ResolveSidebarPaletteColor(
                                          palette_, this, palette_.primary_text,
                                          ui::kColorSysOnSurface));
    label_button_->SetTextColor(views::Button::STATE_PRESSED,
                                show_drop_target
                                    ? ResolveSidebarPaletteColor(
                                          palette_, this, palette_.focus_ring,
                                          ui::kColorSysPrimary)
                                    : ResolveSidebarPaletteColor(
                                          palette_, this, palette_.primary_text,
                                          ui::kColorSysOnSurface));
    // Inactive (non-key) macOS windows render controls as STATE_DISABLED; bind
    // the disabled color to the resolved palette so folder labels stay readable
    // and focus-invariant rather than reverting to the default disabled color.
    label_button_->SetTextColor(views::Button::STATE_DISABLED,
                                show_drop_target
                                    ? ResolveSidebarPaletteColor(
                                          palette_, this, palette_.focus_ring,
                                          ui::kColorSysPrimary)
                                    : ResolveSidebarPaletteColor(
                                          palette_, this, palette_.primary_text,
                                          ui::kColorSysOnSurface));
  }
  if (name_field_) {
    name_field_->SetVisible(is_editing_name_);
  }
  FolderVisualState current_state;
  if (show_drop_target) {
    current_state = FolderVisualState::kDropTarget;
  } else if (hovered) {
    current_state = FolderVisualState::kHovered;
  } else {
    current_state = FolderVisualState::kIdle;
  }
  if (current_state != last_folder_visual_state_) {
    last_folder_visual_state_ = current_state;
    switch (current_state) {
        case FolderVisualState::kDropTarget:
        SetBackground(views::CreateRoundedRectBackground(
            ResolveSidebarPaletteColor(palette_, this, palette_.row_active,
                                       ui::kColorSysSurface4),
            kRowCornerRadiusDp));
        SetBorder(views::CreateRoundedRectBorder(
            kDropIndicatorOutlineThicknessDp, kRowCornerRadiusDp,
            ResolveSidebarPaletteColor(palette_, this, palette_.focus_ring,
                                       ui::kColorSysPrimary)));
        break;
      case FolderVisualState::kHovered:
        SetBackground(views::CreateRoundedRectBackground(
            palette_.row_hover, kRowCornerRadiusDp));
        SetBorder(nullptr);
        break;
      case FolderVisualState::kIdle:
        SetBackground(nullptr);
        SetBorder(nullptr);
        break;
    }
  }
}

SidebarFolderRowView::SidebarFolderDropZone SidebarFolderRowView::ResolveDropZone(
    const ui::DropTargetEvent& event,
    const SidebarDragPayload& payload) const {
  if (payload.node_id.empty() || payload.node_id == folder_id_) {
    return SidebarFolderDropZone::kNone;
  }

  if (payload.node_kind == SidebarNodeKind::kTab ||
      payload.node_kind == SidebarNodeKind::kFolder) {
    const int row_height = std::max(height(), 1);
    const int y = event.location().y();
    const int third = row_height / 3;

    SidebarFolderDropZone zone = SidebarFolderDropZone::kAfter;
    if (y < third) {
      zone = SidebarFolderDropZone::kBefore;
    } else if (y >= third && y < row_height - third) {
      zone = SidebarFolderDropZone::kInto;
    }

    if (zone == SidebarFolderDropZone::kAfter &&
        payload.node_kind == SidebarNodeKind::kFolder &&
        payload.node_id == next_sibling_folder_id_) {
      return SidebarFolderDropZone::kNone;
    }

    return zone;
  }

  return SidebarFolderDropZone::kNone;
}

class SidebarSpaceHeaderRow : public views::View {
  METADATA_HEADER(SidebarSpaceHeaderRow, views::View)

 public:
  SidebarSpaceHeaderRow() {
    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal,
        gfx::Insets::TLBR(4, 12, 4, 12),
        8));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);

    // R-12: vector identity icon slot (used by ConfigurePrivate for the
    // Incognito Glasses glyph). Hidden in the normal space-header path, which
    // uses the emoji text glyph in |icon_label_| instead.
    icon_image_ = AddChildView(std::make_unique<views::ImageView>());
    icon_image_->SetVisible(false);

    icon_label_ = AddChildView(std::make_unique<views::Label>());
    icon_label_->SetFontList(
        views::Label::GetDefaultFontList().Derive(4, gfx::Font::NORMAL, gfx::Font::Weight::NORMAL));
    icon_label_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
    icon_label_->SetVerticalAlignment(gfx::ALIGN_MIDDLE);
    icon_label_->SetVisible(false);
    // Palette colors are pre-resolved for contrast; Label's readability
    // re-blend would test them against the default (non-sidebar) background.
    icon_label_->SetAutoColorReadabilityEnabled(false);

    name_label_ = AddChildView(std::make_unique<views::Label>());
    name_label_->SetTextStyle(views::style::STYLE_BODY_3_MEDIUM);
    name_label_->SetAutoColorReadabilityEnabled(false);
    // Color is applied by ApplyPalette() (primary role, same as tab titles).
    name_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    name_label_->SetVerticalAlignment(gfx::ALIGN_MIDDLE);

    SetBackground(nullptr);
    SetBorder(nullptr);
  }

  SidebarSpaceHeaderRow(const SidebarSpaceHeaderRow&) = delete;
  SidebarSpaceHeaderRow& operator=(const SidebarSpaceHeaderRow&) = delete;
  ~SidebarSpaceHeaderRow() override = default;

  void Configure(const std::string& icon, const std::u16string& name) {
    icon_image_->SetVisible(false);
    icon_label_->SetText(base::UTF8ToUTF16(icon));
    icon_label_->SetVisible(!icon.empty());
    name_label_->SetText(name);
    private_text_color_.reset();
    ApplyPalette();
    // Invariant: name is non-empty when an active space exists.
    // Empty name == no active space (transitional/zero-context) → hide row.
    SetVisible(!name.empty());
    InvalidateLayout();
  }

  void ConfigurePrivate(const ui::ImageModel& icon,
                        const std::u16string& name,
                        ui::ColorId text_color) {
    icon_label_->SetVisible(false);
    icon_image_->SetImage(icon);
    icon_image_->SetVisible(!icon.IsEmpty());
    name_label_->SetText(name);
    name_label_->SetEnabledColor(text_color);
    private_text_color_ = text_color;
    SetVisible(true);
    InvalidateLayout();
  }

  void SetSidebarPalette(const MahoSidebarPalette& palette) {
    palette_ = palette;
    ApplyPalette();
  }

  MahoSidebarPalette palette_for_testing() const { return palette_; }

  void SetTabListView(
      base::WeakPtr<MahoSidebarTabListView> tab_list_view) {
    tab_list_view_ = std::move(tab_list_view);
  }

  // --- Drop target overrides ---
  bool GetDropFormats(int* formats,
                      std::set<ui::ClipboardFormatType>* format_types) override {
    format_types->insert(GetMahoDragFormatType());
    return true;
  }

  bool AreDropTypesRequired() override { return true; }

  bool CanDrop(const ui::OSExchangeData& data) override {
    SidebarDragPayload payload;
    if (!ReadMahoDragData(data, payload)) {
      return false;
    }
    return payload.origin == SidebarDragOrigin::kNormalSection ||
           payload.origin == SidebarDragOrigin::kPinnedSection;
  }

  int OnDragUpdated(const ui::DropTargetEvent& event) override {
    if (!drag_hover_active_) {
      drag_hover_active_ = true;
      SetBackground(
          views::CreateRoundedRectBackground(
              ResolveSidebarPaletteColor(palette_, this, palette_.row_hover,
                                         ui::kColorSysSurface4),
              kRowCornerRadiusDp));
    }
    return static_cast<int>(ui::mojom::DragOperation::kMove);
  }

  void OnDragExited() override {
    drag_hover_active_ = false;
    SetBackground(nullptr);
  }

  DropCallback GetDropCallback(const ui::DropTargetEvent& event) override {
    drag_hover_active_ = false;
    SetBackground(nullptr);
    return base::BindOnce(
        [](base::WeakPtr<MahoSidebarTabListView> tab_list_view,
           const ui::DropTargetEvent& event,
           ui::mojom::DragOperation& output_drag_op,
           std::unique_ptr<ui::LayerTreeOwner> drag_image_layer_owner) {
          SidebarDragPayload payload;
          if (!ReadMahoDragData(event.data(), payload)) {
            output_drag_op = ui::mojom::DragOperation::kNone;
            return;
          }
          output_drag_op = ui::mojom::DragOperation::kMove;
          const SidebarDropPlan plan =
              PlanSectionDrop(MahoSidebarTabSection::kPinned, payload);
          if (!plan.is_valid) {
            output_drag_op = ui::mojom::DragOperation::kNone;
            return;
          }
          // This row paints the active Space name, but its accepted drop means
          // "move into Pinned". It must complete the same native transition as
          // SidebarSectionDropTarget before the synchronous core event can
          // refresh the hierarchy. A hover highlight is never proof of a
          // successful mutation.
          if (tab_list_view) {
            tab_list_view->HandlePostDropTransition(payload, plan);
          }
          ExecuteDropPlan(plan);
        },
        tab_list_view_);
  }

 private:
  void ApplyPalette() {
    if (private_text_color_) {
      name_label_->SetEnabledColor(*private_text_color_);
      return;
    }
    // DESIGN.md: the Space name and a tab title resolve to the identical
    // primary role for the same palette.
    name_label_->SetEnabledColor(ResolveSidebarPaletteColor(
        palette_, this, palette_.primary_text, ui::kColorSysOnSurface));
  }

  raw_ptr<views::ImageView> icon_image_ = nullptr;
  raw_ptr<views::Label> icon_label_ = nullptr;
  raw_ptr<views::Label> name_label_ = nullptr;
  bool drag_hover_active_ = false;
  base::WeakPtr<MahoSidebarTabListView> tab_list_view_;
  std::optional<ui::ColorId> private_text_color_;
  MahoSidebarPalette palette_;
};

BEGIN_METADATA(SidebarSpaceHeaderRow)
END_METADATA

void ForwardDragToAutoScroller(views::View* child, const ui::DropTargetEvent& event) {
  for (views::View* v = child; v; v = v->parent()) {
    if (auto* list = views::AsViewClass<MahoSidebarTabListView>(v)) {
      gfx::Point screen_point = event.location();
      views::View::ConvertPointToScreen(child, &screen_point);
      list->OnAutoScrollDragOver(screen_point);
      return;
    }
  }
}

void ForwardDragEnded(views::View* child) {
  for (views::View* v = child; v; v = v->parent()) {
    if (auto* list = views::AsViewClass<MahoSidebarTabListView>(v)) {
      list->OnAutoScrollDragEnded();
      return;
    }
  }
}

// --- public SidebarSpaceHeaderRow APIs ---
std::unique_ptr<views::View> CreateSidebarSpaceHeaderRow() {
  return std::make_unique<SidebarSpaceHeaderRow>();
}

void SetSidebarSpaceHeaderRowTabListView(
    views::View* row,
    base::WeakPtr<MahoSidebarTabListView> tab_list_view) {
  if (!row) {
    return;
  }
  auto* header = views::AsViewClass<SidebarSpaceHeaderRow>(row);
  CHECK(header)
      << "SetSidebarSpaceHeaderRowTabListView received non-header row";
  header->SetTabListView(std::move(tab_list_view));
}

void UpdateSidebarSpaceHeaderRow(views::View* row,
                                 const std::string& icon,
                                 const std::u16string& name) {
  if (!row) {
    return;
  }
  auto* header = views::AsViewClass<SidebarSpaceHeaderRow>(row);
  CHECK(header) << "UpdateSidebarSpaceHeaderRow received non-SidebarSpaceHeaderRow";
  header->Configure(icon, name);
}

void ConfigureSidebarSpaceHeaderRowPrivate(views::View* row,
                                           const ui::ImageModel& icon,
                                           const std::u16string& name,
                                           ui::ColorId text_color) {
  if (!row) {
    return;
  }
  auto* header = views::AsViewClass<SidebarSpaceHeaderRow>(row);
  CHECK(header) << "ConfigureSidebarSpaceHeaderRowPrivate received non-SidebarSpaceHeaderRow";
  header->ConfigurePrivate(icon, name, text_color);
}

void SetSidebarSpaceHeaderRowPalette(views::View* row,
                                      const MahoSidebarPalette& palette) {
  if (!row) {
    return;
  }
  auto* header = views::AsViewClass<SidebarSpaceHeaderRow>(row);
  CHECK(header) << "SetSidebarSpaceHeaderRowPalette received non-SidebarSpaceHeaderRow";
  header->SetSidebarPalette(palette);
}

MahoSidebarPalette GetSidebarSpaceHeaderRowPaletteForTesting(
    views::View* row) {
  CHECK(row);
  auto* header = views::AsViewClass<SidebarSpaceHeaderRow>(row);
  CHECK(header)
      << "GetSidebarSpaceHeaderRowPaletteForTesting received non-header row";
  return header->palette_for_testing();
}

// --- MahoSidebarTabListView::SidebarAutoScroller implementation ---
class MahoSidebarTabListView::SidebarAutoScroller {
 public:
  explicit SidebarAutoScroller(views::ScrollView* scroll_view)
      : scroll_view_(scroll_view) {}

  SidebarAutoScroller(const SidebarAutoScroller&) = delete;
  SidebarAutoScroller& operator=(const SidebarAutoScroller&) = delete;
  ~SidebarAutoScroller() {
    timer_.Stop();
  }

  void OnDragOver(const gfx::Point& screen_point) {
    if (!scroll_view_) {
      return;
    }
    gfx::Point local_point = screen_point;
    views::View::ConvertPointFromScreen(scroll_view_, &local_point);

    const int height = scroll_view_->height();

    if (local_point.y() >= 0 && local_point.y() < kAutoScrollEdgeDp) {
      direction_ = -1;
      if (!timer_.IsRunning()) {
        timer_.Start(FROM_HERE, base::Hertz(kAutoScrollHz), this, &SidebarAutoScroller::ScrollTick);
      }
    } else if (local_point.y() > height - kAutoScrollEdgeDp && local_point.y() <= height) {
      direction_ = 1;
      if (!timer_.IsRunning()) {
        timer_.Start(FROM_HERE, base::Hertz(kAutoScrollHz), this, &SidebarAutoScroller::ScrollTick);
      }
    } else {
      timer_.Stop();
    }
  }

  void OnDragEnded() {
    timer_.Stop();
  }

 private:
  void ScrollTick() {
    if (!scroll_view_) {
      return;
    }
    scroll_view_->ScrollByOffset(gfx::PointF(0, kAutoScrollStepDp * direction_));
  }

  const raw_ptr<views::ScrollView> scroll_view_;
  base::RepeatingTimer timer_;
  int direction_ = 0;
};

void MahoSidebarTabListView::SetAutoScrollScrollView(
    views::ScrollView* scroll_view) {
  scroll_view_ = scroll_view;
  last_visible_row_ = -1;
  if (scroll_view) {
    auto_scroller_ = std::make_unique<SidebarAutoScroller>(scroll_view);
  } else {
    auto_scroller_.reset();
  }
}

void MahoSidebarTabListView::OnAutoScrollDragOver(const gfx::Point& screen_point) {
  if (auto_scroller_) {
    auto_scroller_->OnDragOver(screen_point);
  }
}

void MahoSidebarTabListView::OnAutoScrollDragEnded() {
  if (auto_scroller_) {
    auto_scroller_->OnDragEnded();
  }
}

void MahoSidebarTabListView::OnScrollChanged() {
  if (!scroll_view_ || !tab_rows_) {
    return;
  }
  if (scroll_presenting_) {
    return;
  }

  const int visible_y = std::max(0, scroll_view_->GetVisibleRect().y());
  const int visible_row = visible_y / kRowHeightDp;
  if (visible_row == last_visible_row_) {
    return;
  }
  base::AutoReset<bool> presenting(&scroll_presenting_, true);

  // Velocity-aware directional overscan (bounded). The compositor scrolls the
  // layer-backed ScrollView on its own thread; realization + presentation run
  // here on the main thread. These are distinct pipelines, so callback/frame
  // scheduling jitter can still briefly outrun realization. To absorb that
  // jitter we pre-realize AHEAD of the compositor: grow the window in the
  // scroll direction so entering rows are realized off-screen in the overscan
  // buffer before they scroll into view. Hard-capped at kMaxVelocityOverscanRows
  // so realization never scales with the tab count. This only expands
  // realization; it never limits scrolling.
  if (visibility_manager_) {
    const int base_rows = visibility_manager_->config().buffer_rows_below;
    int leading_rows = base_rows;
    const base::TimeTicks now = base::TimeTicks::Now();
    const int dy = visible_y - last_scroll_velocity_y_;
    if (!last_scroll_velocity_time_.is_null() && dy != 0) {
      const base::TimeDelta dt = now - last_scroll_velocity_time_;
      if (dt.is_positive()) {
        const int abs_dy = dy < 0 ? -dy : dy;
        const double rows_per_sec =
            (abs_dy / static_cast<double>(kRowHeightDp)) / dt.InSecondsF();
        // Lead the compositor by ~0.1s of travel: at 60fps that is ~6 frames,
        // keeping several already-realized rows ahead of the viewport to cover
        // scheduling jitter between the compositor and main-thread realization.
        // Capped at kMaxVelocityOverscanRows; beyond that the paint-only
        // skeleton spacer is the residual fallback.
        const int extra = static_cast<int>(rows_per_sec * 0.1);
        leading_rows = std::clamp(
            base_rows + extra, base_rows,
            SidebarVisibilityManager::kMaxVelocityOverscanRows);
      }
    }
    const bool scrolling_down = dy >= 0;
    if (scrolling_down) {
      visibility_manager_->SetProjectionOverscanRows(base_rows, leading_rows);
    } else {
      visibility_manager_->SetProjectionOverscanRows(leading_rows, base_rows);
    }
    velocity_leading_overscan_rows_for_testing_ = leading_rows;
    last_scroll_velocity_y_ = visible_y;
    last_scroll_velocity_time_ = now;
  }

  last_visible_row_ = visible_row;
  ++scroll_layout_invalidation_count_for_testing_;
  ReconcileSectionWindow(MahoSidebarTabSection::kPinned, browser_);
  ReconcileSectionWindow(MahoSidebarTabSection::kNormal, browser_);
  // Full-root/ScrollView layout is forbidden: it can resize/clamp and re-fire
  // scrolling. Lay out only this existing contents subtree at current bounds;
  // outer invalidation remains for ancestor extent/scrollbar adoption.
  tab_rows_->InvalidateLayout();
  DeprecatedLayoutImmediately();
}

void MahoSidebarTabListView::SchedulePostLayoutSync() {
  if (post_layout_sync_pending_) {
    return;
  }
  post_layout_sync_pending_ = true;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](base::WeakPtr<MahoSidebarTabListView> self) {
                       if (!self) {
                         return;
                       }
                       self->post_layout_sync_pending_ = false;
                       self->SyncRealizedWindowAfterLayout();
                     },
                     weak_factory_.GetWeakPtr()));
}

void MahoSidebarTabListView::SyncRealizedWindowAfterLayout() {
  if (post_layout_syncing_ || rebuilding_rows_ || reconciling_) {
    return;
  }
  if (!scroll_view_ || !tab_rows_) {
    return;
  }
  // Nothing projected yet (pre-first-rebuild, or a genuinely empty space).
  if (pinned_projection_.empty() && normal_projection_.empty()) {
    return;
  }
  base::AutoReset<bool> syncing(&post_layout_syncing_, true);

  const RealizedWindow pinned_before = pinned_window_;
  const RealizedWindow normal_before = normal_window_;
  ReconcileSectionWindow(MahoSidebarTabSection::kPinned, browser_);
  ReconcileSectionWindow(MahoSidebarTabSection::kNormal, browser_);

  // Keep OnScrollChanged()'s early-out (visible_row == last_visible_row_) in
  // step with the post-clamp offset so it cannot swallow the next real tick.
  last_visible_row_ =
      std::max(0, scroll_view_->GetVisibleRect().y()) / kRowHeightDp;

  // This runs after EVERY ScrollView layout. Invalidating unconditionally would
  // schedule another layout that re-enters here: a layout per frame, forever.
  // Only a changed window needs its new rows placed now; the child/spacer
  // mutations already invalidated upward, and the follow-up pass is stable.
  // The one exception is a stale cull: the layout that just ran decided the
  // top-level scaffold visibility (action row, sections) against the
  // pre-clamp offset. Re-laying out settles it against the current viewport,
  // after which HasStaleCulling() is false, so this cannot loop either.
  const auto* virtual_layout =
      static_cast<const SidebarVirtualLayoutDelegate*>(
          tab_rows_->GetLayoutManager());
  const bool culling_stale =
      virtual_layout && virtual_layout->HasStaleCulling();
  if (!culling_stale &&
      pinned_window_.first == pinned_before.first &&
      pinned_window_.last == pinned_before.last &&
      normal_window_.first == normal_before.first &&
      normal_window_.last == normal_before.last) {
    return;
  }
  // The window change resized spacers and added rows inside each section's
  // rows container. Invalidate those containers themselves: laying out only
  // this view leaves their children at pre-reconcile bounds, so the new rows
  // stay pushed below the section and are clipped to zero height.
  for (views::View* spacer : {pinned_top_spacer_.get(), normal_top_spacer_.get()}) {
    if (spacer && spacer->parent()) {
      spacer->parent()->InvalidateLayout();
    }
  }
  tab_rows_->InvalidateLayout();
  DeprecatedLayoutImmediately();
}

void MahoSidebarTabListView::ResetScrollLayoutInvalidationStateForTesting() {
  last_visible_row_ = -1;
  scroll_layout_invalidation_count_for_testing_ = 0;
}

uint64_t MahoSidebarTabListView::row_favicon_generation_for_testing(
    const std::string& tab_id) const {
  const auto it = row_favicon_requests_.find(tab_id);
  return it == row_favicon_requests_.end() ? 0 : it->second.generation;
}

bool MahoSidebarTabListView::ReentrantReconcileIsBlockedForTesting() {
  // Simulate an outer reconcile in progress, then attempt a nested reconcile;
  // the guard must make it a no-op, leaving the window untouched.
  base::AutoReset<bool> outer(&reconciling_, true);
  const RealizedWindow before = normal_window_;
  ReconcileSectionWindow(MahoSidebarTabSection::kNormal, browser_);
  return normal_window_.first == before.first &&
         normal_window_.last == before.last;
}

bool MahoSidebarTabListView::OnKeyPressed(const ui::KeyEvent& event) {
  if (event.IsShiftDown() &&
      (event.key_code() == ui::VKEY_UP || event.key_code() == ui::VKEY_DOWN)) {
    views::View* focused = GetFocusManager() ? GetFocusManager()->GetFocusedView() : nullptr;
    if (focused) {
      SidebarTabRowView* focused_row = nullptr;
      for (views::View* v = focused; v; v = v->parent()) {
        if (auto* row = views::AsViewClass<SidebarTabRowView>(v)) {
          focused_row = row;
          break;
        }
      }
      if (focused_row) {
        // Navigate in projection order (every tab), so a shift+arrow can cross
        // the realized window boundary; reveal + focus the target row.
        const std::vector<std::string> order = GetTabVisualOrderIds();
        const std::string focused_id = focused_row->tab_id();
        auto it = std::find(order.begin(), order.end(), focused_id);
        if (it != order.end()) {
          size_t index = std::distance(order.begin(), it);
          size_t target_index = index;
          if (event.key_code() == ui::VKEY_UP && index > 0) {
            target_index = index - 1;
          } else if (event.key_code() == ui::VKEY_DOWN &&
                     index + 1 < order.size()) {
            target_index = index + 1;
          }
          if (target_index != index) {
            const std::string target_id = order[target_index];
            SelectRangeTo(target_id);
            RevealTabById(target_id);
            auto row_it = tab_id_to_row_view_.find(target_id);
            if (row_it != tab_id_to_row_view_.end() && row_it->second) {
              row_it->second->RequestFocus();
            }
            return true;
          }
        }
      }
    }
  }
  return views::View::OnKeyPressed(event);
}

// --- MahoSidebarTabListView ---

MahoSidebarTabListView::MahoSidebarTabListView(Browser* browser)
    : browser_(browser),
      tab_preview_controller_(std::make_unique<MahoTabPreviewController>(
          browser)),
      folder_hover_controller_(std::make_unique<MahoSidebarFolderHoverController>(
          browser)) {
  auto* root_layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, kRootInsets, kRowSpacingDp));
  root_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  auto rows = std::make_unique<views::View>();
  visibility_manager_ = std::make_unique<SidebarVisibilityManager>(
      SidebarVisibilityManager::Config{
          .row_height_dp = kRowHeightDp,
          .buffer_rows_above = 30,
          .buffer_rows_below = 30,
          .drag_overscan_rows = 50});
  rows->SetLayoutManager(std::make_unique<SidebarVirtualLayoutDelegate>(
      visibility_manager_.get(), kRowHeightDp));

  tab_rows_ = rows.get();
  tab_rows_->set_context_menu_controller(this);
  AddChildView(std::move(rows));
  set_context_menu_controller(this);

  GetViewAccessibility().SetRole(ax::mojom::Role::kTabList);
  GetViewAccessibility().SetIsMultiselectable(true);
}

MahoSidebarTabListView::~MahoSidebarTabListView() {
  weak_factory_.InvalidateWeakPtrs();
  new_tab_button_ = nullptr;
  tidy_button_ = nullptr;
  clear_button_ = nullptr;
  action_dividers_.clear();
  pinned_section_target_ = nullptr;
  normal_section_target_ = nullptr;
  pinned_separator_ = nullptr;
  action_row_ = nullptr;
  action_buttons_ = nullptr;
  action_processing_view_ = nullptr;
  trigger_button_ = nullptr;
  folder_rows_by_id_.clear();
  tab_id_to_row_view_.clear();
  split_id_to_container_.clear();
  pinned_top_spacer_ = nullptr;
  pinned_bottom_spacer_ = nullptr;
  normal_top_spacer_ = nullptr;
  normal_bottom_spacer_ = nullptr;
  drag_preserve_holder_ = nullptr;
  if (tab_rows_) {
    tab_rows_->SetLayoutManager(nullptr);
    tab_rows_->RemoveAllChildViews();
  }
  GetViewAccessibility().RemoveAllVirtualChildViews();
}

void MahoSidebarTabListView::SetControlledTabId(std::string stable_tab_id) {
  if (controlled_tab_id_ == stable_tab_id) {
    return;
  }
  controlled_tab_id_ = std::move(stable_tab_id);
  ApplyControlledTabState();
}

bool MahoSidebarTabListView::IsControlledTabRow(
    const SidebarTabRowView& row) const {
  return !controlled_tab_id_.empty() &&
         row.tab_id() == controlled_tab_id_;
}

void MahoSidebarTabListView::ApplyControlledTabState() {
  for (const auto& [tab_id, row] : tab_id_to_row_view_) {
    if (row) {
      row->SetControlled(IsControlledTabRow(*row));
    }
  }
}

void MahoSidebarTabListView::OnSidebarPaletteChanged(
    const MahoSidebarPalette& palette) {
  palette_ = palette;
  if (action_processing_view_) {
    action_processing_view_->SetSidebarPalette(palette_);
  }
  for (const auto& entry : tab_id_to_row_view_) {
    if (entry.second) {
      entry.second->SetSidebarPalette(palette_);
    }
  }
  for (const auto& entry : folder_rows_by_id_) {
    if (entry.second) {
      entry.second->SetSidebarPalette(palette_);
    }
  }
  for (const auto& entry : split_id_to_container_) {
    if (auto* split_container =
            views::AsViewClass<SplitGroupContainerView>(entry.second.get())) {
      split_container->SetSidebarPalette(palette_);
    }
  }
  if (auto* separator =
          views::AsViewClass<SidebarSeparatorRow>(pinned_separator_.get())) {
    separator->SetSidebarPalette(palette_);
  }
  for (views::View* spacer :
       {pinned_top_spacer_.get(), pinned_bottom_spacer_.get(),
        normal_top_spacer_.get(), normal_bottom_spacer_.get()}) {
    if (auto* skeleton = views::AsViewClass<SidebarSkeletonSpacerView>(spacer)) {
      skeleton->SetSidebarPalette(palette_);
    }
  }
  ApplyActionRowPalette();
}

void MahoSidebarTabListView::SetFavoritesDropAcceptedCallback(
    base::RepeatingCallback<void(const std::string&)> callback) {
  favorites_drop_accepted_callback_ = callback;

  auto propagate = [&callback](auto& self, views::View* root) -> void {
    if (!root) return;
    if (auto* lane = views::AsViewClass<SidebarInsertionLaneView>(root)) {
      lane->SetFavoritesDropAcceptedCallback(callback);
    } else if (auto* folder = views::AsViewClass<SidebarFolderRowView>(root)) {
      folder->SetFavoritesDropAcceptedCallback(callback);
    } else if (auto* tab = views::AsViewClass<SidebarTabRowView>(root)) {
      tab->SetFavoritesDropAcceptedCallback(callback);
    }
    for (views::View* child : root->children()) {
      self(self, child);
    }
  };
  propagate(propagate, this);
}

void MahoSidebarTabListView::ToggleFolderExpandedForTesting(
    const std::string& folder_id) {
  ToggleFolderExpanded(folder_id);
}

bool MahoSidebarTabListView::IsTabPreviewShowingForTesting() const {
  return tab_preview_controller_ && tab_preview_controller_->IsShowingForTesting();
}

views::Widget* MahoSidebarTabListView::tab_preview_widget_for_testing() const {
  return tab_preview_controller_
             ? tab_preview_controller_->preview_widget_for_testing()
             : nullptr;
}

const std::u16string& MahoSidebarTabListView::tab_preview_title_for_testing()
    const {
  static const std::u16string kEmpty;
  return tab_preview_controller_
             ? tab_preview_controller_->preview_title_for_testing()
             : kEmpty;
}

const std::u16string& MahoSidebarTabListView::tab_preview_url_for_testing()
    const {
  static const std::u16string kEmpty;
  return tab_preview_controller_
             ? tab_preview_controller_->preview_url_for_testing()
             : kEmpty;
}

void MahoSidebarTabListView::SetTabPreviewShowDelayForTesting(
    base::TimeDelta delay) {
  if (tab_preview_controller_) {
    tab_preview_controller_->SetShowDelayForTesting(delay);
  }
}

void MahoSidebarTabListView::Update(MahoSidebarTabListModel model,
                                    Browser* browser) {
  base::ElapsedTimer maho_perf_t;
  DVLOG(1) << "[MAHO_PERF] Update START";
  DCHECK(!rebuilding_rows_);
  last_visible_row_ = -1;
  browser_ = browser;
  // A model whose tree the core could not produce (null v2 payload) is not
  // data: rendering it would wipe the sidebar for the space already on screen
  // until the next successful push. Keep the last known-good rows. A different
  // space, or a genuinely EMPTY tree (tree_unavailable == false), still renders.
  if (model.tree_unavailable && has_last_model_ &&
      active_space_id_ == model.active_space_id) {
    DVLOG(1) << "[MAHO_PERF] Update END(tree_unavailable)";
    return;
  }
  if (active_space_id_ != model.active_space_id) {
    ClearSelection();
    active_space_id_ = model.active_space_id;
  }

  const TabStripModel* strip = browser ? browser->GetTabStripModel() : nullptr;
  const bool live_tab_strip_is_empty = !strip || strip->empty();

  auto model_has_tab_ids = [](const MahoSidebarTabListModel& candidate) {
    auto has_tree_tab_id = [&](const auto& self,
                               const std::vector<SidebarTreeNode>& nodes) -> bool {
      for (const auto& node : nodes) {
        if (node.kind == SidebarNodeKind::kTab && !node.tab_id.empty()) {
          return true;
        }
        if (!node.children.empty() && self(self, node.children)) {
          return true;
        }
      }
      return false;
    };
    return has_tree_tab_id(has_tree_tab_id, candidate.pinned_tree) ||
           has_tree_tab_id(has_tree_tab_id, candidate.normal_tree);
  };

  const bool adopt = live_tab_strip_is_empty || model_has_tab_ids(model);

  if (batch_depth_ > 0) {
    if (adopt) {
      last_model_ = std::move(model);
      has_last_model_ = true;
      RefreshRowFaviconRequestsFromModel(last_model_);
    }
    batch_rebuild_needed_ = true;
    return;
  }

  if (GetWidget() && GetWidget()->HasCapture()) {
    if (adopt) {
      // The same adoption guard used by the immediate and batch paths must
      // protect the deferred slot too. A transient empty core push can arrive
      // while a drag owns capture; queueing it would clear every rendered row
      // on release, move the drop target under the cursor, and restart the
      // enter/exit cycle that users observe as pinned-area flipping. Do not
      // clear an already queued valid model when rejecting that transient push.
      rebuild_deferred_ = true;
      deferred_model_ = model;
      deferred_browser_ = browser;
      last_model_ = std::move(model);
      has_last_model_ = true;
      RefreshRowFaviconRequestsFromModel(last_model_);
    }
    return;
  }

  if (adopt) {
    last_model_ = std::move(model);
    has_last_model_ = true;
  }
  const MahoSidebarTabListModel& effective = adopt ? last_model_ : model;
  RefreshRowFaviconRequestsFromModel(effective);

  // Hiding the pinned drop-lane collapses a row's worth of height. Deferred
  // until here (past the capture early-return) so it never shifts layout
  // mid-drag; the flush after capture releases performs the hide.
  HideEmptyPinnedDropLane();

  // Single-source-of-truth for folder toggle: when we do our own optimistic
  // local rebuild in ToggleFolderExpanded(), we suppress the single matching
  // state-push rebuild that fires asynchronously from
  // DispatchShellEvent → NotifyChanged → ScheduleRefreshAll → ThreadPool
  // → RefreshAll → Update(). Gating on the fingerprint (rather than a bare
  // flag) ensures an unrelated update arriving first — e.g. a tab created
  // elsewhere — is NOT swallowed and still rebuilds. Otherwise every folder
  // click pays for two full RebuildRows() passes on the entire sidebar (the
  // user has 870+ tabs — that is the freeze).
  if (!suppress_push_fingerprint_.empty()) {
    std::string fingerprint = ComputeModelFingerprint(effective);
    if (fingerprint == suppress_push_fingerprint_) {
      suppress_push_fingerprint_.clear();
      if (!adopt) {
        last_model_ = effective;
        has_last_model_ = true;
      }
      last_model_fingerprint_ = std::move(fingerprint);
      // A suppressed push can still carry an active-tab change. Applying it
      // via the highlight fast path (which also reconciles last_model_) keeps
      // the guard in UpdateActiveTabHighlightOnly from blocking a later
      // correction; a bare last_active_tab_id_ assignment would drop it.
      if (effective.active_tab.tab_id != last_active_tab_id_) {
        UpdateActiveTabHighlightOnly(effective.active_tab.tab_id);
      }
      RebuildTabIndexCache();
      return;
    }
    // Fingerprint mismatch: this is not the toggle push we armed for. The arm
    // is single-shot — disarm now so a stale fingerprint can never swallow a
    // later legitimate push that happens to match it (e.g. an unrelated change
    // being undone). Then process this update normally below.
    suppress_push_fingerprint_.clear();
  }

  // Fingerprint-based dedupe of background state pushes. Without this, every
  // tab strip or space-profile observer notification (which fires many times
  // per second under normal use — tab loads, hover, focus changes, etc.)
  // re-enters Update() with a structurally identical model and forces a full
  // ~2s RebuildRows() pass on the 870+ row sidebar. The fingerprint covers
  // every field that affects rendered row output (kind, ids, expansion,
  // titles, loading state). If the fingerprint matches the last rendered
  // pass, the view tree is already correct and the rebuild is wasted work.
  std::string fingerprint = ComputeModelFingerprint(effective);
  if (!last_model_fingerprint_.empty() && fingerprint == last_model_fingerprint_) {
    if (effective.active_tab.tab_id != last_active_tab_id_) {
      DVLOG(1) << "[MAHO_PERF]   fp_match active_only";
      UpdateActiveTabHighlightOnly(effective.active_tab.tab_id);
      last_active_tab_id_ = effective.active_tab.tab_id;
      if (favorites_sync_callback_) {
        favorites_sync_callback_.Run();
      }
    } else {
      DVLOG(1) << "[MAHO_PERF]   fp_match noop";
    }
    // Index-only strip mutations don't change the fingerprint, so refresh the
    // cache even on this fast path to keep it aligned with the live strip.
    RebuildTabIndexCache();
    DVLOG(1) << "[MAHO_PERF] Update END(fp_match) "
             << maho_perf_t.Elapsed().InMillisecondsF() << "ms";
    return;
  }
  last_model_fingerprint_ = std::move(fingerprint);
  last_active_tab_id_ = effective.active_tab.tab_id;
  PruneLocalTitleCache(effective);

  base::ElapsedTimer rebuild_t;
  RebuildRows(effective, browser);
  DVLOG(1) << "[MAHO_PERF]   RebuildRows="
           << rebuild_t.Elapsed().InMillisecondsF() << "ms rows=" << (tab_rows_ ? tab_rows_->children().size() : 0);

  RebuildTabIndexCache();
  DVLOG(1) << "[MAHO_PERF] Update END(rebuilt) "
           << maho_perf_t.Elapsed().InMillisecondsF() << "ms";
}

std::string MahoSidebarTabListView::ComputeModelFingerprint(
    const MahoSidebarTabListModel& model) {
  std::string out;
  out.reserve(8192);
  out.append(model.active_space_id);
  out.push_back('|');
  // Fingerprint intentionally omits model.active_tab.tab_id: the active-tab
  // highlight is applied by UpdateActiveTabHighlightOnly() without a full
  // ~2s RebuildRows() pass. Including it here would defeat that fast path.
  std::function<void(const std::vector<SidebarTreeNode>&)> walk;
  walk = [&](const std::vector<SidebarTreeNode>& nodes) {
    for (const auto& n : nodes) {
      out.push_back(static_cast<char>('A' + static_cast<int>(n.kind)));
      out.push_back(':');
      out.append(n.kind == SidebarNodeKind::kFolder ? n.folder_id : n.tab_id);
      out.push_back(':');
      if (n.kind == SidebarNodeKind::kFolder) {
        out.push_back(n.is_expanded ? 'E' : 'C');
        out.push_back(':');
        out.append(base::UTF16ToUTF8(n.folder_name));
      } else {
        // is_audible is intentionally excluded: an audio start/stop is a
        // cosmetic change routed through ApplyCosmeticRefresh()
        // (UpdateAudioState), so folding it into the fingerprint here would
        // force a full RebuildRows on every audio flicker.
        out.push_back(n.is_suspended ? 'S' : 's');
        out.push_back(n.is_muted ? 'M' : 'm');
      }
      out.push_back(';');
      if (!n.children.empty()) {
        out.push_back('[');
        walk(n.children);
        out.push_back(']');
      }
    }
  };
  walk(model.pinned_tree);
  out.push_back('|');
  walk(model.normal_tree);
  return out;
}

bool MahoSidebarTabListView::HasTabInLastModel(
    const std::string& tab_id) const {
  if (!has_last_model_ || tab_id.empty()) {
    return false;
  }
  std::function<bool(const std::vector<SidebarTreeNode>&)> search;
  search = [&](const std::vector<SidebarTreeNode>& nodes) -> bool {
    for (const auto& n : nodes) {
      if (n.kind == SidebarNodeKind::kTab && n.tab_id == tab_id) {
        return true;
      }
      if (!n.children.empty() && search(n.children)) {
        return true;
      }
    }
    return false;
  };
  return search(last_model_.pinned_tree) || search(last_model_.normal_tree);
}

bool MahoSidebarTabListView::CoreSuspendedForTab(
    const std::string& tab_id) const {
  if (!has_last_model_ || tab_id.empty()) {
    return false;
  }
  bool found_suspended = false;
  std::function<bool(const std::vector<SidebarTreeNode>&)> search;
  search = [&](const std::vector<SidebarTreeNode>& nodes) -> bool {
    for (const auto& n : nodes) {
      if (n.kind == SidebarNodeKind::kTab && n.tab_id == tab_id) {
        found_suspended = n.is_suspended;
        return true;
      }
      if (!n.children.empty() && search(n.children)) {
        return true;
      }
    }
    return false;
  };
  if (!search(last_model_.pinned_tree)) {
    search(last_model_.normal_tree);
  }
  return found_suspended;
}

const std::vector<SidebarTreeNode>* MahoSidebarTabListView::FindFolderChildren(
    const std::string& folder_id) const {
  if (!has_last_model_ || folder_id.empty()) {
    return nullptr;
  }

  std::function<const std::vector<SidebarTreeNode>*(
      const std::vector<SidebarTreeNode>&)>
      search;
  search = [&](const std::vector<SidebarTreeNode>& nodes)
      -> const std::vector<SidebarTreeNode>* {
    for (const SidebarTreeNode& node : nodes) {
      if (node.kind == SidebarNodeKind::kFolder &&
          node.folder_id == folder_id) {
        return &node.children;
      }
      if (!node.children.empty()) {
        if (const std::vector<SidebarTreeNode>* found = search(node.children)) {
          return found;
        }
      }
    }
    return nullptr;
  };

  if (const std::vector<SidebarTreeNode>* found =
          search(last_model_.pinned_tree)) {
    return found;
  }
  return search(last_model_.normal_tree);
}

void MahoSidebarTabListView::ScheduleFolderHoverPopup(
    views::View* anchor,
    std::string folder_id,
    std::u16string folder_name) {
  if (!folder_hover_controller_) {
    return;
  }
  const std::vector<SidebarTreeNode>* children = FindFolderChildren(folder_id);
  if (!children || children->empty()) {
    return;
  }
  folder_hover_controller_->ScheduleShow(anchor, std::move(folder_id),
                                         std::move(folder_name), *children);
}

void MahoSidebarTabListView::UpdateActiveTabHighlightOnly(
    const std::string& new_active_tab_id) {
  if (last_active_tab_id_ == new_active_tab_id) {
    return;
  }

  // An empty incoming id while the strip still has an active tab means the
  // stable id was not captured yet (transient/startup race). Clearing here
  // would drop a valid highlight no later push may restore, so skip — mirrors
  // the MahoSidebarView fast-path guard. A genuine "no active tab" clear still
  // proceeds because active_index() is then kNoTab.
  if (new_active_tab_id.empty() && browser_ && browser_->GetTabStripModel() &&
      browser_->GetTabStripModel()->active_index() != TabStripModel::kNoTab) {
    return;
  }

  // A split renders both panes active when either is active (GroupSplitTabs).
  // Resolve each active tab to the full set of tab_ids in its split so the
  // highlight toggles both panes as one unit; a single-pane toggle would
  // desync last_model_ and, in stacked splits (rows paint their own
  // background), leave a stale half-highlight on the next re-render.
  std::map<std::string, std::vector<std::string>> tab_to_split_unit;
  if (has_last_model_) {
    std::function<void(const std::vector<SidebarTreeNode>&)> collect_units;
    collect_units = [&](const std::vector<SidebarTreeNode>& nodes) {
      for (const auto& node : nodes) {
        if (node.kind == SidebarNodeKind::kSplitGroup) {
          std::vector<std::string> members;
          for (const auto& child : node.children) {
            if (child.kind == SidebarNodeKind::kTab && !child.tab_id.empty()) {
              members.push_back(child.tab_id);
            }
          }
          for (const auto& member : members) {
            tab_to_split_unit[member] = members;
          }
        }
        if (!node.children.empty()) {
          collect_units(node.children);
        }
      }
    };
    collect_units(last_model_.pinned_tree);
    collect_units(last_model_.normal_tree);
  }

  auto unit_for = [&](const std::string& tab_id) -> std::vector<std::string> {
    if (tab_id.empty()) {
      return {};
    }
    auto it = tab_to_split_unit.find(tab_id);
    if (it != tab_to_split_unit.end()) {
      return it->second;
    }
    return {tab_id};
  };

  const std::vector<std::string> new_unit = unit_for(new_active_tab_id);
  const std::vector<std::string> old_unit = unit_for(last_active_tab_id_);
  std::set<std::string> new_set(new_unit.begin(), new_unit.end());

  // The observer fast paths reach here without an Update() pass, so
  // last_model_ still carries the previous is_active flags. Everything that
  // re-renders from last_model_ would resurrect the old highlight — or drop
  // the new one for a tab inside a collapsed folder — and the
  // last_active_tab_id_ guard above then blocks any later correction.
  if (has_last_model_) {
    std::function<void(std::vector<SidebarTreeNode>&)> sync_active;
    sync_active = [&](std::vector<SidebarTreeNode>& nodes) {
      for (auto& node : nodes) {
        if (node.kind == SidebarNodeKind::kTab) {
          node.is_active = new_set.count(node.tab_id) > 0;
        }
        if (!node.children.empty()) {
          sync_active(node.children);
        }
      }
    };
    sync_active(last_model_.pinned_tree);
    sync_active(last_model_.normal_tree);
    last_model_.active_tab.tab_id = new_active_tab_id;
  }

  if (!tab_rows_) {
    last_active_tab_id_ = new_active_tab_id;
    return;
  }

  // Sticky-row maintenance: a collapsed folder keeps its active descendant
  // visible as a sticky row. The outgoing tab's sticky row (if any) is torn
  // down, and when the incoming tab lives inside a collapsed folder — so it
  // has no ordinary row — a sticky row is materialized for it.
  if (!last_active_tab_id_.empty()) {
    auto old_it = tab_id_to_row_view_.find(last_active_tab_id_);
    if (old_it != tab_id_to_row_view_.end() && old_it->second &&
        !old_it->second->collapsed_sticky_folder_id().empty()) {
      SidebarTabRowView* old_sticky = old_it->second;
      views::View* sticky_parent = old_sticky->parent();
      tab_id_to_row_view_.erase(old_it);
      if (sticky_parent) {
        std::unique_ptr<views::View> owned_sticky =
            sticky_parent->RemoveChildViewT(
                static_cast<views::View*>(old_sticky));
        sticky_parent->InvalidateLayout();
      }
    }
  }
  if (!new_active_tab_id.empty() &&
      tab_id_to_row_view_.find(new_active_tab_id) ==
          tab_id_to_row_view_.end()) {
    MaterializeStickyRowForTab(new_active_tab_id);
  }

  auto toggle_active = [&](const std::string& tab_id, bool active) {
    if (tab_id.empty()) {
      return;
    }
    auto it = tab_id_to_row_view_.find(tab_id);
    if (it == tab_id_to_row_view_.end() || !it->second) {
      return;
    }
    SidebarTabRowView* row = it->second;
    // Split panes paint transparent, so SetActive is state-only there; the
    // container paints the shared highlight for both halves. Keeping the
    // row's own flag accurate matters for is_active_for_testing() and any
    // later reparenting out of the split.
    row->SetActive(active);
    if (auto* container =
            views::AsViewClass<SplitGroupContainerView>(row->parent())) {
      container->SetActiveState(active);
    }
  };

  // Toggle whole split units, not single rows: deactivate every outgoing pane
  // not shared by the incoming unit, then activate every incoming pane. Both
  // panes of a split share one container and SetActive/SetActiveState early-out
  // on an unchanged value, so redundant calls here are idempotent.
  for (const auto& id : old_unit) {
    if (new_set.count(id) == 0) {
      toggle_active(id, false);
    }
  }
  for (const auto& id : new_unit) {
    toggle_active(id, true);
  }

  // The active tab may live outside the visible viewport or the realized
  // window; reveal it so its highlight is actually materialized and scrolled
  // into view.
  if (!new_active_tab_id.empty()) {
    RevealTabById(new_active_tab_id);
  }

  last_active_tab_id_ = new_active_tab_id;
  ScheduleAccessibilityTreeRebuild();
}

bool MahoSidebarTabListView::TrySuspendFastPath(
    const std::string& suspended_tab_id,
    const std::string& new_active_tab_id) {
  if (!has_last_model_ || suspended_tab_id.empty()) {
    return false;
  }
  // Only close-protected removals stay in the model as a suspend; a real close
  // must fall through so the row is actually removed by a full rebuild.
  if (!maho::IsCoreCloseProtectedTab(suspended_tab_id)) {
    return false;
  }
  // A moved active tab must already have a row in the last model; otherwise a
  // full rebuild is needed to materialize the successor's row.
  if (!new_active_tab_id.empty() && !HasTabInLastModel(new_active_tab_id)) {
    return false;
  }
  auto row_it = tab_id_to_row_view_.find(suspended_tab_id);
  if (row_it == tab_id_to_row_view_.end() || !row_it->second) {
    return false;
  }
  // Mirror is_suspended into last_model_ so the node can't diverge: the
  // fingerprint encodes is_suspended and any later rebuild reads last_model_.
  bool node_found = false;
  std::function<void(std::vector<SidebarTreeNode>&)> flip;
  flip = [&](std::vector<SidebarTreeNode>& nodes) {
    for (auto& n : nodes) {
      if (n.kind == SidebarNodeKind::kTab && n.tab_id == suspended_tab_id) {
        n.is_suspended = true;
        node_found = true;
      }
      if (!n.children.empty()) {
        flip(n.children);
      }
    }
  };
  flip(last_model_.pinned_tree);
  flip(last_model_.normal_tree);
  if (!node_found) {
    return false;
  }
  // Flip the row before moving the highlight: if the suspended tab is the
  // active sticky row of a collapsed folder, UpdateActiveTabHighlightOnly()
  // tears that row down, so it must be updated first.
  row_it->second->SetSuspended(true);
  if (!new_active_tab_id.empty()) {
    UpdateActiveTabHighlightOnly(new_active_tab_id);
  }
  // The async core push reflecting this suspend fingerprints identically
  // (is_suspended is in the fingerprint, active tab is not), so arm one-shot
  // suppression to swallow it instead of a full RebuildRows.
  suppress_push_fingerprint_ = ComputeModelFingerprint(last_model_);
  return true;
}

bool MahoSidebarTabListView::TryWakeFastPath() {
  if (!has_last_model_ || waking_tab_id_.empty()) {
    return false;
  }
  const std::string tab_id = waking_tab_id_;
  auto row_it = tab_id_to_row_view_.find(tab_id);
  if (row_it == tab_id_to_row_view_.end() || !row_it->second) {
    return false;
  }
  // Only handle a genuine suspended->live transition; flip is_suspended in
  // last_model_ so the node matches the fingerprint we arm below.
  bool node_found = false;
  bool was_suspended = false;
  std::function<void(std::vector<SidebarTreeNode>&)> flip;
  flip = [&](std::vector<SidebarTreeNode>& nodes) {
    for (auto& n : nodes) {
      if (n.kind == SidebarNodeKind::kTab && n.tab_id == tab_id) {
        was_suspended = n.is_suspended;
        n.is_suspended = false;
        node_found = true;
      }
      if (!n.children.empty()) {
        flip(n.children);
      }
    }
  };
  flip(last_model_.pinned_tree);
  flip(last_model_.normal_tree);
  if (!node_found || !was_suspended) {
    return false;
  }
  row_it->second->SetSuspended(false);
  UpdateActiveTabHighlightOnly(tab_id);
  suppress_push_fingerprint_ = ComputeModelFingerprint(last_model_);
  return true;
}

bool MahoSidebarTabListView::ReconcileInsertedRowLiveness(
    content::WebContents* inserted_contents) {
  if (!has_last_model_ || !inserted_contents) {
    return false;
  }
  const std::string tab_id = FindCoreTabIdByWebContents(inserted_contents);
  if (tab_id.empty()) {
    return false;
  }
  auto it = tab_id_to_row_view_.find(tab_id);
  if (it == tab_id_to_row_view_.end() || !it->second) {
    return false;
  }
  SidebarTabRowView* row = it->second;
  if (!row->is_suspended()) {
    return false;
  }
  // Only un-X a row whose core model says it is live; a genuinely suspended
  // tab (core is_suspended=true) must stay X.
  bool node_found = false;
  bool core_suspended = false;
  std::function<void(const std::vector<SidebarTreeNode>&)> walk;
  walk = [&](const std::vector<SidebarTreeNode>& nodes) {
    for (const auto& n : nodes) {
      if (n.kind == SidebarNodeKind::kTab && n.tab_id == tab_id) {
        core_suspended = n.is_suspended;
        node_found = true;
        return;
      }
      if (!n.children.empty()) {
        walk(n.children);
      }
    }
  };
  walk(last_model_.pinned_tree);
  if (!node_found) {
    walk(last_model_.normal_tree);
  }
  if (!node_found || core_suspended) {
    return false;
  }
  row->SetSuspended(false);
  return true;
}

void MahoSidebarTabListView::SetFavoritesSyncCallback(
    base::RepeatingClosure callback) {
  favorites_sync_callback_ = std::move(callback);
}

void MahoSidebarTabListView::ApplyActionPlaceholderButtonState() {
  auto set_engaged = [](views::View* v, bool engaged) {
    if (auto* button = views::AsViewClass<MahoInlineActionButton>(v)) {
      button->SetEngaged(engaged);
    }
  };
  const bool active = action_placeholder_active_;
  set_engaged(tidy_button_, active && (trigger_button_ == tidy_button_));
  set_engaged(clear_button_, active && (trigger_button_ == clear_button_));
}

void MahoSidebarTabListView::OnActionPlaceholderHidden() {
  action_placeholder_active_ = false;
  trigger_button_ = nullptr;
  ApplyActionPlaceholderButtonState();
  if (action_buttons_) {
    action_buttons_->SetCanProcessEventsWithinSubtree(true);
    action_buttons_->GetViewAccessibility().SetIsIgnored(false);
  }
  views::View* focus_view = focus_tracker_.view();
  if (focus_view && focus_view->GetFocusManager()) {
    focus_view->RequestFocus();
  }
  focus_tracker_.SetView(nullptr);
}

void MahoSidebarTabListView::SetPrivateMode(bool private_mode) {
  if (private_mode_ == private_mode) {
    return;
  }
  DCHECK(!action_placeholder_active_);
  private_mode_ = private_mode;

  // Force a full rebuild (not a SetVisible toggle): RebuildRows reuses the
  // existing action row, so drop it first to get a private-aware one and let
  // the pinned separator / empty pinned drop-target gating re-apply.
  if (has_last_model_ && browser_) {
    if (action_row_ && action_row_->parent() == tab_rows_) {
      tab_rows_->RemoveChildViewT(action_row_.get());
    }
    action_row_ = nullptr;
    action_buttons_ = nullptr;
    action_processing_view_ = nullptr;
    RebuildRows(last_model_, browser_);
  }
}

void MahoSidebarTabListView::StartActionProcessing(
    MahoSidebarProcessingPlaceholderView::Mode mode,
    views::View* trigger_button) {
  if (action_placeholder_active_) {
    return;
  }
  if (!action_processing_view_) {
    return;
  }
  action_placeholder_active_ = true;
  trigger_button_ = trigger_button;
  ApplyActionPlaceholderButtonState();

  if (trigger_button && trigger_button->GetFocusManager() &&
      trigger_button->GetFocusManager()->GetFocusedView() == trigger_button) {
    focus_tracker_.SetView(trigger_button);
    trigger_button->GetFocusManager()->ClearFocus();
  } else {
    focus_tracker_.SetView(nullptr);
  }

  if (action_buttons_) {
    action_buttons_->SetCanProcessEventsWithinSubtree(false);
    action_buttons_->GetViewAccessibility().SetIsIgnored(true);
  }

  action_processing_view_->Start(
      mode, base::BindOnce(&MahoSidebarTabListView::OnActionPlaceholderHidden,
                           weak_factory_.GetWeakPtr()));
}

void MahoSidebarTabListView::ResolveActionPlaceholderDeferred(bool success) {
  if (action_processing_view_) {
    if (success) {
      action_processing_view_->ResolveSuccess();
    } else {
      action_processing_view_->ResolveFailure();
    }
  }
}

bool MahoSidebarTabListView::GetDropFormats(
    int* formats,
    std::set<ui::ClipboardFormatType>* format_types) {
  *formats = ui::OSExchangeData::URL;
  format_types->insert(GetMahoDragFormatType());
  return true;
}

bool MahoSidebarTabListView::CanDrop(const ui::OSExchangeData& data) {
  SidebarDragPayload payload;
  if (ReadMahoDragData(data, payload)) {
    return true;
  }
  const auto urls = data.GetURLs(ui::FilenameToURLPolicy::CONVERT_FILENAMES);
  return !urls.empty() && urls.front().url.is_valid();
}

int MahoSidebarTabListView::OnDragUpdated(const ui::DropTargetEvent& event) {
  if (visibility_manager_ && !visibility_manager_->is_drag_active()) {
    visibility_manager_->SetDragActive(true);
    if (tab_rows_) {
      tab_rows_->InvalidateLayout();
    }
  }
  ForwardDragToAutoScroller(this, event);
  SidebarDragPayload payload;
  if (ReadMahoDragData(event.data(), payload)) {
    if (payload.origin == SidebarDragOrigin::kPinnedSection ||
        payload.origin == SidebarDragOrigin::kNormalSection ||
        payload.origin == SidebarDragOrigin::kFavorites) {
      RevealEmptyPinnedDropLane();
    }
    return static_cast<int>(ui::mojom::DragOperation::kMove);
  }
  const auto urls =
      event.data().GetURLs(ui::FilenameToURLPolicy::CONVERT_FILENAMES);
  return !urls.empty() && urls.front().url.is_valid()
             ? static_cast<int>(ui::mojom::DragOperation::kCopy)
             : static_cast<int>(ui::mojom::DragOperation::kNone);
}

views::View::DropCallback MahoSidebarTabListView::GetDropCallback(
    const ui::DropTargetEvent& event) {
  ForwardDragEnded(this);
  return base::BindOnce(
      [](base::WeakPtr<MahoSidebarTabListView> tab_list_view,
         Browser* browser, std::string space_id,
         base::RepeatingCallback<void(const std::string&)>
             favorites_drop_accepted,
         const ui::DropTargetEvent& event,
         ui::mojom::DragOperation& output_drag_op,
         std::unique_ptr<ui::LayerTreeOwner> drag_image_layer_owner) {
        SidebarDragPayload payload;
        if (ReadMahoDragData(event.data(), payload)) {
          const SidebarDropPlan plan =
              ResolveDropPlan(SectionKind::kNormal, payload);
          if (!plan.is_valid) {
            output_drag_op = ui::mojom::DragOperation::kNone;
            return;
          }
          if (plan.favorite_transition.kind ==
              SidebarDropPlan::FavoriteTransition::Kind::kUnfavorite) {
            if (favorites_drop_accepted) {
              favorites_drop_accepted.Run(payload.node_id);
            }
          }
          if (tab_list_view) {
            tab_list_view->HandlePostDropTransition(payload, plan);
          }
          ExecuteDropPlan(plan);
          output_drag_op = ui::mojom::DragOperation::kMove;
          if (payload.node_kind == SidebarNodeKind::kTab &&
              !payload.node_id.empty()) {
            DispatchReorderRootItem(space_id, SidebarNodeKind::kTab,
                                   payload.node_id,
                                   MakeInsertionPointAppend());
          } else if (payload.node_kind == SidebarNodeKind::kFolder &&
                     !payload.node_id.empty()) {
            DispatchReorderRootItem(space_id, SidebarNodeKind::kFolder,
                                   payload.node_id,
                                   MakeInsertionPointAppend());
          }
          return;
        }
        const auto urls = event.data().GetURLs(
            ui::FilenameToURLPolicy::CONVERT_FILENAMES);
        if (!browser || urls.empty() || !urls.front().url.is_valid()) {
          output_drag_op = ui::mojom::DragOperation::kNone;
          return;
        }
        output_drag_op = ui::mojom::DragOperation::kCopy;
        browser->OpenURL(content::OpenURLParams(
                             urls.front().url, content::Referrer(),
                             WindowOpenDisposition::NEW_FOREGROUND_TAB,
                             ui::PAGE_TRANSITION_LINK, false),
                         base::NullCallback());
      },
      weak_factory_.GetWeakPtr(), browser_, active_space_id_,
      favorites_drop_accepted_callback_);
}

void MahoSidebarTabListView::OnDragExited() {
  if (visibility_manager_) {
    visibility_manager_->SetDragActive(false);
    if (tab_rows_) {
      tab_rows_->InvalidateLayout();
    }
  }
  ForwardDragEnded(this);
  // A parent receives OnDragExited when DropHelper hands the drag to the
  // newly revealed pinned child. Hiding that child here makes the next hit
  // test fall back to the parent, producing an enter/exit visibility loop.
  // External drags have no retained source; internal drags keep the lane until
  // the source's authoritative OnDragDone -> OnDragSourceFinished callback.
  if (drag_source_tab_id_.empty()) {
    HideEmptyPinnedDropLane();
  }
}

void MahoSidebarTabListView::RevealEmptyPinnedDropLane() {
  if (pinned_drop_lane_revealed_) {
    return;
  }
  if (private_mode_ || !has_last_model_) {
    return;
  }
  const bool pinned_already_has_content =
      !last_model_.pinned_tree.empty();
  if (pinned_already_has_content) {
    return;
  }
  pinned_drop_lane_revealed_ = true;
  UpdatePinnedDropLaneVisibility(true);
}

void MahoSidebarTabListView::HideEmptyPinnedDropLane() {
  if (!pinned_drop_lane_revealed_) {
    return;
  }
  pinned_drop_lane_revealed_ = false;
  UpdatePinnedDropLaneVisibility(false);
}

void MahoSidebarTabListView::UpdatePinnedDropLaneVisibility(bool visible) {
  const bool pinned_has_content =
      has_last_model_ && !last_model_.pinned_tree.empty();
  const bool pinned_target_visible =
      !private_mode_ && (visible || pinned_has_content);
  if (pinned_section_target_) {
    auto* drop_target =
        static_cast<SidebarSectionDropTarget*>(pinned_section_target_);
    if (!pinned_has_content) {
      // Keep the empty lane intrinsically tab-height so revealing it produces
      // a stable target. SidebarVirtualLayoutDelegate removes that height only
      // while product code externally hides the lane.
      drop_target->SetMinDropTargetHeight(private_mode_ ? 0 : kRowHeightDp);
    }
    pinned_section_target_->SetVisible(pinned_target_visible);
  }
  if (pinned_separator_) {
    // The divider separates pinned from normal semantics in normal mode; it
    // is not part of the transient empty-lane hit target and must not flicker.
    pinned_separator_->SetVisible(!private_mode_);
  }
}

namespace {

std::string ProjectionSectionId(MahoSidebarTabSection section) {
  return section == MahoSidebarTabSection::kPinned ? "pinned" : "normal";
}

const SidebarTreeNode* ProjectionActiveDescendant(
    const std::vector<SidebarTreeNode>& nodes) {
  for (const auto& n : nodes) {
    if (n.kind == SidebarNodeKind::kTab && n.is_active) {
      return &n;
    }
    if (!n.children.empty()) {
      if (const SidebarTreeNode* found =
              ProjectionActiveDescendant(n.children)) {
        return found;
      }
    }
  }
  return nullptr;
}

int CountProjectionModelTabs(const std::vector<SidebarTreeNode>& nodes) {
  int count = 0;
  for (const auto& n : nodes) {
    if (n.kind == SidebarNodeKind::kTab) {
      ++count;
    }
    count += CountProjectionModelTabs(n.children);
  }
  return count;
}

SidebarTreeNode CopyNodeWithoutChildren(const SidebarTreeNode& node) {
  SidebarTreeNode copy = node;
  copy.children.clear();
  return copy;
}

SidebarVisualRow MakeProjectionLaneRow(MahoSidebarTabSection section,
                                       SidebarNodeKind target_kind,
                                       const std::string& target_id,
                                       bool append,
                                       const std::string& parent_folder_id,
                                       int target_child_index) {
  SidebarVisualRow row;
  row.kind = SidebarVisualRowKind::kInsertionLane;
  row.section = section;
  row.stable_id = "lane:" + ProjectionSectionId(section) + ":" +
                  (append ? "after:" : "before:") + target_id;
  row.lane_target_kind = target_kind;
  row.lane_target_id = target_id;
  row.lane_append = append;
  row.lane_parent_folder_id = parent_folder_id;
  row.lane_target_child_index = target_child_index;
  return row;
}

// Mirrors RebuildTreeNodes() exactly: before-lane, tab/folder row, expanded
// children flattened as siblings, collapsed sticky row, append-lane, split
// group. Splits are emitted as a single kSplitGroup row (their internal panes
// live inside one container view in the eager tree, so they occupy one slot in
// the parent's child order).
void AppendProjectionRows(const std::vector<SidebarTreeNode>& nodes,
                          MahoSidebarTabSection section,
                          const std::string& parent_folder_id,
                          std::vector<SidebarVisualRow>* out,
                          bool force_split_stacked) {
  std::unordered_map<std::string, std::string> sibling_last_seen_folder_id;
  std::vector<std::string> next_sibling_folder_ids(nodes.size());
  for (int i = static_cast<int>(nodes.size()) - 1; i >= 0; --i) {
    const auto& n = nodes[i];
    if (n.kind == SidebarNodeKind::kFolder) {
      const std::string& parent_id = n.parent_of_folder_id;
      next_sibling_folder_ids[i] = sibling_last_seen_folder_id[parent_id];
      if (!n.folder_id.empty()) {
        sibling_last_seen_folder_id[parent_id] = n.folder_id;
      }
    }
  }

  for (size_t index = 0; index < nodes.size(); ++index) {
    const auto& node = nodes[index];
    const bool is_last = (index + 1 == nodes.size());
    if (node.kind == SidebarNodeKind::kFolder) {
      const std::string sibling_parent_folder_id = node.parent_of_folder_id;
      out->push_back(MakeProjectionLaneRow(section, SidebarNodeKind::kFolder,
                                           node.folder_id, /*append=*/false,
                                           sibling_parent_folder_id,
                                           /*target_child_index=*/-1));

      SidebarVisualRow folder_row;
      folder_row.kind = SidebarVisualRowKind::kFolder;
      folder_row.section = section;
      folder_row.stable_id = node.folder_id;
      folder_row.indent_depth = node.depth;
      folder_row.node = CopyNodeWithoutChildren(node);
      folder_row.parent_folder_id = parent_folder_id;
      folder_row.folder_next_sibling_id = next_sibling_folder_ids[index];
      folder_row.folder_has_children = !node.children.empty();
      out->push_back(std::move(folder_row));

      if (node.is_expanded && !node.children.empty()) {
        AppendProjectionRows(node.children, section, node.folder_id, out,
                             force_split_stacked);
      } else if (!node.children.empty()) {
        if (const SidebarTreeNode* active =
                ProjectionActiveDescendant(node.children)) {
          SidebarVisualRow sticky;
          sticky.kind = SidebarVisualRowKind::kCollapsedStickyTab;
          sticky.section = section;
          sticky.stable_id = "sticky:" + node.folder_id + ":" + active->tab_id;
          sticky.indent_depth = node.depth + 1;
          sticky.node = CopyNodeWithoutChildren(*active);
          sticky.parent_folder_id = node.folder_id;
          out->push_back(std::move(sticky));
        }
      }

      if (is_last) {
        const std::string append_target = next_sibling_folder_ids[index].empty()
                                              ? node.folder_id
                                              : next_sibling_folder_ids[index];
        out->push_back(MakeProjectionLaneRow(section, SidebarNodeKind::kFolder,
                                             append_target, /*append=*/true,
                                             sibling_parent_folder_id,
                                             /*target_child_index=*/-1));
      }
    } else if (node.kind == SidebarNodeKind::kSplitGroup) {
      SidebarVisualRow split_row;
      split_row.kind = SidebarVisualRowKind::kSplitGroup;
      split_row.section = section;
      split_row.stable_id = node.split_id;
      split_row.indent_depth = node.depth;
      split_row.node = node;
      split_row.parent_folder_id = parent_folder_id;
      split_row.split_force_stacked = force_split_stacked;
      out->push_back(std::move(split_row));
    } else {
      out->push_back(MakeProjectionLaneRow(section, SidebarNodeKind::kTab,
                                           node.tab_id, /*append=*/false,
                                           parent_folder_id,
                                           static_cast<int>(index)));

      SidebarVisualRow tab_row;
      tab_row.kind = SidebarVisualRowKind::kTab;
      tab_row.section = section;
      tab_row.stable_id = node.tab_id;
      tab_row.indent_depth = node.depth;
      tab_row.node = CopyNodeWithoutChildren(node);
      tab_row.parent_folder_id = parent_folder_id;
      out->push_back(std::move(tab_row));

      if (is_last) {
        out->push_back(MakeProjectionLaneRow(section, SidebarNodeKind::kTab,
                                             node.tab_id, /*append=*/true,
                                             parent_folder_id,
                                             static_cast<int>(index + 1)));
      }
    }
  }
}

int CountProjectionTabRows(const std::vector<SidebarVisualRow>& projection) {
  int count = 0;
  for (const auto& row : projection) {
    switch (row.kind) {
      case SidebarVisualRowKind::kTab:
      case SidebarVisualRowKind::kCollapsedStickyTab:
        ++count;
        break;
      case SidebarVisualRowKind::kSplitGroup:
        count += CountProjectionModelTabs(row.node.children);
        break;
      case SidebarVisualRowKind::kInsertionLane:
      case SidebarVisualRowKind::kFolder:
        break;
    }
  }
  return count;
}

}  // namespace

SidebarVisualRow::SidebarVisualRow() = default;
SidebarVisualRow::SidebarVisualRow(const SidebarVisualRow&) = default;
SidebarVisualRow::SidebarVisualRow(SidebarVisualRow&&) = default;
SidebarVisualRow& SidebarVisualRow::operator=(const SidebarVisualRow&) = default;
SidebarVisualRow& SidebarVisualRow::operator=(SidebarVisualRow&&) = default;
SidebarVisualRow::~SidebarVisualRow() = default;

int MeasureVisualRowHeight(const SidebarVisualRow& row) {
  switch (row.kind) {
    case SidebarVisualRowKind::kInsertionLane:
      return kDropIndicatorThicknessDp;
    case SidebarVisualRowKind::kTab:
    case SidebarVisualRowKind::kFolder:
    case SidebarVisualRowKind::kCollapsedStickyTab:
      return kRowHeightDp;
    case SidebarVisualRowKind::kSplitGroup: {
      // Matches RebuildSplitGroupContainer(): split_orientation == "vertical"
      // lays panes side-by-side (one row tall); any other value stacks them.
      // A stacked pane realizes before+append insertion lanes (kDropIndicator
      // ThicknessDp each) around its kRowHeightDp row, and adjacent panes are
      // separated by a 1dp divider — so the stacked height matches the eager
      // container exactly, keeping the section extent stable.
      const bool side_by_side =
          row.node.split_orientation == "vertical" && !row.split_force_stacked;
      if (side_by_side) {
        return kRowHeightDp;
      }
      const int child_count =
          std::max(static_cast<int>(row.node.children.size()), 1);
      constexpr int kSplitStackedDividerDp = 1;
      const int pane_height = kRowHeightDp + 2 * kDropIndicatorThicknessDp;
      return child_count * pane_height +
             (child_count - 1) * kSplitStackedDividerDp;
    }
  }
  return kRowHeightDp;
}

void BuildSectionProjection(const std::vector<SidebarTreeNode>& nodes,
                            MahoSidebarTabSection section,
                            std::vector<SidebarVisualRow>* out,
                            bool force_split_stacked) {
  out->clear();
  AppendProjectionRows(nodes, section, std::string(), out,
                       force_split_stacked);
  int cumulative_top = 0;
  for (SidebarVisualRow& row : *out) {
    row.height_dp = MeasureVisualRowHeight(row);
    row.cumulative_top_dp = cumulative_top;
    cumulative_top += row.height_dp;
  }
}

void MahoSidebarTabListView::RebuildRows(const MahoSidebarTabListModel& model,
                                              Browser* browser) {
  base::ElapsedTimer maho_rr_t;
  DCHECK(!rebuilding_rows_) << "RebuildRows reentered";
  base::AutoReset<bool> rebuild_guard(&rebuilding_rows_, true);
  ++rebuild_rows_count_for_testing_;
  last_visible_row_ = -1;
  pinned_projection_.clear();
  normal_projection_.clear();
  HandleTabRowHoverEnd();

  favicon_task_tracker_.TryCancelAll();
  folder_rows_by_id_.clear();
  active_menu_runner_.reset();
  active_menu_model_.reset();
  active_context_menu_.reset();

  tab_rows_->SetLayoutManager(nullptr);

  // Reset split tracking before destroying rows: split_id_to_container_ holds
  // raw_ptrs to the containers RemoveAllChildViews is about to delete, and
  // RebuildSplitGroupContainer repopulates both while building, so clearing
  // after the tree walk would wipe the freshly-collected ids. splits_awaiting_
  // anim_ is intentionally NOT cleared here: it survives the rebuild burst.
  current_rebuild_split_ids_.clear();
  split_id_to_container_.clear();

  // Spring-load safety: while a drag is in flight, move the drag-source row
  // into a hidden holder instead of letting RemoveAllChildViews() free it. The
  // drag's nested run loop runs on that row's own stack frame (macOS), so
  // freeing it mid-drag is a use-after-free. The holder is a child of this view
  // so the row keeps a live parent chain (drag-end notifications still walk up).
  // OnDragSourceFinished() releases it after the drag stack unwinds.
  if (!drag_source_tab_id_.empty()) {
    auto it = tab_id_to_row_view_.find(drag_source_tab_id_);
    if (it != tab_id_to_row_view_.end() && it->second) {
      SidebarTabRowView* row = it->second;
      if (views::View* row_parent = row->parent();
          row_parent && row_parent != drag_preserve_holder_) {
        if (!drag_preserve_holder_) {
          auto holder = std::make_unique<views::View>();
          holder->SetVisible(false);
          drag_preserve_holder_ = AddChildView(std::move(holder));
        }
        drag_preserve_holder_->AddChildView(
            row_parent->RemoveChildViewT(row));
      }
    }
    tab_id_to_row_view_.erase(drag_source_tab_id_);
  }

  std::unique_ptr<views::View> action_row_holder;
  if (action_row_ && action_row_->parent() == tab_rows_) {
    // Virtualization culls this scaffold with SetVisible(false). The layout
    // manager installed at the end of this rebuild snapshots GetVisible() into
    // CanBeVisible(), which would pin the row at 0dp for every later space.
    action_row_->SetVisible(true);
    action_row_holder = tab_rows_->RemoveChildViewT(action_row_);
  } else {
    if (action_row_ && !action_row_->parent()) {
      delete action_row_.get();
    }
    action_row_ = nullptr;
    action_buttons_ = nullptr;
    action_processing_view_ = nullptr;
    tidy_button_ = nullptr;
    clear_button_ = nullptr;
    new_tab_button_ = nullptr;
    action_dividers_.clear();
    action_row_holder = CreateActionRow();
  }

  pinned_section_target_ = nullptr;
  normal_section_target_ = nullptr;
  pinned_separator_ = nullptr;
  tab_id_to_row_view_.clear();
  pinned_top_spacer_ = nullptr;
  pinned_bottom_spacer_ = nullptr;
  normal_top_spacer_ = nullptr;
  normal_bottom_spacer_ = nullptr;

  base::ElapsedTimer rr_remove_t;
  tab_rows_->RemoveAllChildViews();
  DVLOG(1) << "[MAHO_PERF]     RR.RemoveAllChildViews="
            << rr_remove_t.Elapsed().InMillisecondsF() << "ms";

  const bool has_tree = !model.pinned_tree.empty() ||
                        !model.normal_tree.empty();
  if (!HasTabStripContext(browser) && !has_tree) {
    if (action_row_holder) {
      tab_rows_->AddChildView(std::move(action_row_holder));
    }
    DVLOG(1) << "[MAHO_PERF]     RR.zero_context TOTAL="
              << maho_rr_t.Elapsed().InMillisecondsF() << "ms";
    return;
  }

  const MahoSidebarTabListModel* effective_model = &model;
  MahoSidebarTabListModel fallback_model;
  if (!has_tree && active_space_id_.empty()) {
    fallback_model =
        BuildLiveStripFallbackTabListModel(browser, active_space_id_);
    effective_model = &fallback_model;
  }

  MahoSidebarTabListModel filtered_model = *effective_model;
  FilterSidebarIneligibleTabs(&filtered_model.pinned_tree);
  FilterSidebarIneligibleTabs(&filtered_model.normal_tree);
  effective_model = &filtered_model;

  MahoSidebarTabListModel private_model;
  if (private_mode_) {
    private_model = *effective_model;
    private_model.pinned_tree.clear();
    effective_model = &private_model;
  }

  base::ElapsedTimer rr_pinned_t;
  RebuildTreeSection(tab_rows_, effective_model->pinned_tree,
                     MahoSidebarTabSection::kPinned, browser);
  DVLOG(1) << "[MAHO_PERF]     RR.RebuildPinned="
            << rr_pinned_t.Elapsed().InMillisecondsF()
            << "ms nodes=" << effective_model->pinned_tree.size();

  auto separator = std::make_unique<SidebarSeparatorRow>();
  separator->SetSidebarPalette(palette_);
  pinned_separator_ = tab_rows_->AddChildView(std::move(separator));
  const bool pinned_has_content = !effective_model->pinned_tree.empty();
  const bool pinned_visible = pinned_has_content || pinned_drop_lane_revealed_;
  auto* pinned_drop_target =
      static_cast<SidebarSectionDropTarget*>(pinned_section_target_);
  if (!pinned_has_content) {
    pinned_drop_target->SetMinDropTargetHeight(private_mode_ ? 0 : kRowHeightDp);
  }
  pinned_section_target_->SetVisible(!private_mode_ && pinned_visible);
  pinned_separator_->SetVisible(!private_mode_);
  if (action_row_holder) {
    tab_rows_->AddChildView(std::move(action_row_holder));
  }

  base::ElapsedTimer rr_normal_t;
  RebuildTreeSection(tab_rows_, effective_model->normal_tree,
                     MahoSidebarTabSection::kNormal, browser);
  DVLOG(1) << "[MAHO_PERF]     RR.RebuildNormal="
            << rr_normal_t.Elapsed().InMillisecondsF()
            << "ms nodes=" << effective_model->normal_tree.size();

  bool force_split_stacked = false;
  if (browser && browser->GetProfile()) {
    force_split_stacked = !maho::sidebar_prefs::IsSidebarPanelExpanded(
        browser->GetProfile()->GetPrefs());
  }
  BuildSectionProjection(effective_model->pinned_tree,
                         MahoSidebarTabSection::kPinned, &pinned_projection_,
                         force_split_stacked);
  BuildSectionProjection(effective_model->normal_tree,
                         MahoSidebarTabSection::kNormal, &normal_projection_,
                         force_split_stacked);

  // Realize only the rows inside each section's visible window; the spacers
  // absorb the culled extent. tab_id_to_row_view_ / folder_rows_by_id_ are
  // populated lazily by RealizeProjectionRow as rows enter the window.
  ReconcileSectionWindow(MahoSidebarTabSection::kPinned, browser);
  ReconcileSectionWindow(MahoSidebarTabSection::kNormal, browser);
#if DCHECK_IS_ON()
  if (drag_source_tab_id_.empty()) {
    DCHECK_LE(static_cast<int>(GetTabRowsInVisualOrder().size()),
              CountProjectionTabRows(pinned_projection_) +
                  CountProjectionTabRows(normal_projection_))
        << "Realized tab rows exceed the section projection";
  }
#endif

  // Prune stale selection IDs against the full MODEL (every tab, including
  // tabs hidden inside collapsed folders), NOT against realization or the
  // visible projection — an off-window OR collapsed selected tab must survive.
  {
    std::unordered_set<std::string> model_tab_ids;
    std::function<void(const std::vector<SidebarTreeNode>&)> collect_model_tabs;
    collect_model_tabs = [&](const std::vector<SidebarTreeNode>& nodes) {
      for (const SidebarTreeNode& n : nodes) {
        if (n.kind == SidebarNodeKind::kTab && !n.tab_id.empty()) {
          model_tab_ids.insert(n.tab_id);
        }
        collect_model_tabs(n.children);
      }
    };
    collect_model_tabs(effective_model->pinned_tree);
    collect_model_tabs(effective_model->normal_tree);
    for (auto it = selected_tab_ids_.begin(); it != selected_tab_ids_.end();) {
      if (model_tab_ids.find(*it) == model_tab_ids.end()) {
        it = selected_tab_ids_.erase(it);
      } else {
        ++it;
      }
    }
  }

  // Update selection anchor if it was pruned
  if (!selection_anchor_tab_id_.empty() &&
      selected_tab_ids_.count(selection_anchor_tab_id_) == 0) {
    selection_anchor_tab_id_.clear();
  }

  // Sync visual selection state on the realized rows (off-window selected tabs
  // are re-synced by RealizeProjectionRow when they enter the window).
  for (const auto& [tab_id, row] : tab_id_to_row_view_) {
    row->SetSelected(selected_tab_ids_.count(tab_id) > 0);
  }

  ScheduleAccessibilityTreeRebuild();

  last_rendered_split_ids_ = current_rebuild_split_ids_;

  if (!pending_folder_edit_id_.empty()) {
    std::string edit_id = std::move(pending_folder_edit_id_);
    pending_folder_edit_id_.clear();
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(
            [](base::WeakPtr<MahoSidebarTabListView> weak_self,
               std::string folder_id) {
              if (!weak_self) {
                return;
              }
              auto it = weak_self->folder_rows_by_id_.find(folder_id);
              if (it != weak_self->folder_rows_by_id_.end() && it->second) {
                it->second->ScrollViewToVisible();
                it->second->BeginFolderEditing();
              }
            },
            weak_factory_.GetWeakPtr(), std::move(edit_id)));
  }

  if (tab_rows_) {
    ApplyControlledTabState();
    tab_rows_->SetLayoutManager(std::make_unique<SidebarVirtualLayoutDelegate>(
        visibility_manager_.get(), kRowHeightDp));
    tab_rows_->InvalidateLayout();
  }

  if (GetWidget() && GetWidget()->IsVisible()) {
    GetWidget()->LayoutRootViewIfNecessary();
    // Creating one split triggers a burst of rebuilds; (re)start a short timer
    // so the reveal animation plays once, on the settled container, instead of
    // being torn down mid-flight by the next rebuild.
    if (!splits_awaiting_anim_.empty()) {
      split_anim_debounce_timer_.Start(
          FROM_HERE, base::Milliseconds(60),
          base::BindOnce(&MahoSidebarTabListView::AnimateAwaitingSplits,
                         weak_factory_.GetWeakPtr()));
    }
    if (GetWidget()->HasCapture()) {
      DVLOG(1) << "[MAHO_PERF]     RR TOTAL(has_capture)="
                << maho_rr_t.Elapsed().InMillisecondsF() << "ms";
      return;
    }
    const gfx::Point cursor_screen =
        display::Screen::Get()->GetCursorScreenPoint();
    const gfx::Rect widget_bounds_screen =
        GetWidget()->GetWindowBoundsInScreen();
    if (!widget_bounds_screen.Contains(cursor_screen)) {
      base::ElapsedTimer rr_syn_t;
      GetWidget()->SynthesizeMouseMoveEvent();
      DVLOG(1) << "[MAHO_PERF]     RR.SynthesizeMouseMove="
                << rr_syn_t.Elapsed().InMillisecondsF() << "ms";
    }
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(
            [](base::WeakPtr<MahoSidebarTabListView> weak_self) {
              if (!weak_self) {
                return;
              }
              weak_self->FlushDeferredRebuild();
              views::Widget* widget = weak_self->GetWidget();
              if (widget && widget->IsVisible() && !widget->HasCapture()) {
                const gfx::Point cursor =
                    display::Screen::Get()->GetCursorScreenPoint();
                const gfx::Rect bounds = widget->GetWindowBoundsInScreen();
                if (!bounds.Contains(cursor)) {
                  widget->SynthesizeMouseMoveEvent();
                }
              }
            },
             weak_factory_.GetWeakPtr()));
  }
  DVLOG(1) << "[MAHO_PERF]     RR TOTAL="
            << maho_rr_t.Elapsed().InMillisecondsF() << "ms";
}

void MahoSidebarTabListView::FlushDeferredRebuild() {
  if (!rebuild_deferred_) {
    return;
  }
  rebuild_deferred_ = false;
  last_model_fingerprint_ = ComputeModelFingerprint(deferred_model_);
  last_active_tab_id_ = deferred_model_.active_tab.tab_id;
  RebuildRows(deferred_model_, deferred_browser_);
  RebuildTabIndexCache();
  deferred_model_ = {};
  deferred_browser_ = nullptr;
}

void MahoSidebarTabListView::RebuildTreeSection(
    views::View* parent,
    const std::vector<SidebarTreeNode>& nodes,
    MahoSidebarTabSection section,
    Browser* browser) {
  auto* section_container =
      AddSectionContainer(parent, section, active_space_id_,
                          weak_factory_.GetWeakPtr());

  if (section == MahoSidebarTabSection::kPinned) {
    pinned_section_target_ = section_container;
  } else if (section == MahoSidebarTabSection::kNormal) {
    normal_section_target_ = section_container;
  }

  if (favorites_drop_accepted_callback_) {
    auto* section_drop_target =
        static_cast<SidebarSectionDropTarget*>(section_container);
    section_drop_target->SetFavoritesDropAcceptedCallback(
        favorites_drop_accepted_callback_);
  }

  auto* rows_container = section_container->AddChildView(
      std::unique_ptr<views::View>(
          std::make_unique<SidebarDropForwardingView>(section_container)
              .release()));
  auto* rows_layout = rows_container->SetLayoutManager(
      std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical, gfx::Insets(), kRowSpacingDp));
  rows_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  // Leading/trailing spacers bound the realized window; ReconcileSectionWindow
  // sizes them so the section's preferred height equals the full projected
  // extent even though only the windowed rows between them are realized.
  auto* top_spacer =
      rows_container->AddChildView(std::make_unique<SidebarSkeletonSpacerView>());
  top_spacer->SetPreferredSize(gfx::Size(0, 0));
  top_spacer->SetSidebarPalette(palette_);
  auto* bottom_spacer =
      rows_container->AddChildView(std::make_unique<SidebarSkeletonSpacerView>());
  bottom_spacer->SetPreferredSize(gfx::Size(0, 0));
  bottom_spacer->SetSidebarPalette(palette_);
  if (section == MahoSidebarTabSection::kPinned) {
    pinned_top_spacer_ = top_spacer;
    pinned_bottom_spacer_ = bottom_spacer;
    pinned_window_ = {0, -1};
  } else {
    normal_top_spacer_ = top_spacer;
    normal_bottom_spacer_ = bottom_spacer;
    normal_window_ = {0, -1};
  }

  if (nodes.empty()) {
    auto* drop_target =
        static_cast<SidebarSectionDropTarget*>(section_container);
    drop_target->SetMinDropTargetHeight(
        (private_mode_ && section == MahoSidebarTabSection::kPinned)
            ? 0
            : kRowHeightDp);
  }
}

void MahoSidebarTabListView::RebuildTreeNodes(
    views::View* parent,
    const std::vector<SidebarTreeNode>& nodes,
    MahoSidebarTabSection section,
    Browser* browser,
    const std::string& parent_folder_id) {
  // Eager row builder for non-windowed/split-member construction only. Do not
  // use this to realize full virtualized sections; use projection rows plus
  // ReconcileSectionWindow instead.
  std::unordered_map<std::string, std::string> sibling_parent_to_last_seen_folder_id;
  std::vector<std::string> next_sibling_folder_ids(nodes.size());
  for (int i = static_cast<int>(nodes.size()) - 1; i >= 0; --i) {
    const auto& n = nodes[i];
    if (n.kind == SidebarNodeKind::kFolder) {
      const std::string& parent_id = n.parent_of_folder_id;
      next_sibling_folder_ids[i] = sibling_parent_to_last_seen_folder_id[parent_id];
      if (!n.folder_id.empty()) {
        sibling_parent_to_last_seen_folder_id[parent_id] = n.folder_id;
      }
    }
  }

  for (size_t index = 0; index < nodes.size(); ++index) {
    const auto& node = nodes[index];
    if (node.kind == SidebarNodeKind::kFolder) {
      std::string next_sibling_folder_id = next_sibling_folder_ids[index];
      const std::string sibling_parent_folder_id = node.parent_of_folder_id;

      auto* before_folder_lane =
          parent->AddChildView(std::make_unique<SidebarInsertionLaneView>(
              active_space_id_, node.folder_id, SidebarNodeKind::kFolder,
              node.folder_id, false, sibling_parent_folder_id, -1, section,
              weak_factory_.GetWeakPtr()));
      if (favorites_drop_accepted_callback_) {
        before_folder_lane->SetFavoritesDropAcceptedCallback(
            favorites_drop_accepted_callback_);
      }
      before_folder_lane->SetRevealEmptyPinnedDropLaneCallback(
          base::BindRepeating(&MahoSidebarTabListView::RevealEmptyPinnedDropLane,
                              base::Unretained(this)));
      auto* folder_row = parent->AddChildView(std::make_unique<SidebarFolderRowView>(
          node, section, active_space_id_, browser,
          base::BindRepeating(&MahoSidebarTabListView::ToggleFolderExpanded,
                              weak_factory_.GetWeakPtr(), node.folder_id),
           base::BindRepeating(&MahoSidebarTabListView::CommitFolderNameChange,
                               weak_factory_.GetWeakPtr()),
           next_sibling_folder_id, sibling_parent_folder_id,
           !node.children.empty()));
      folder_row->SetSidebarPalette(palette_);
      if (favorites_drop_accepted_callback_) {
        folder_row->SetFavoritesDropAcceptedCallback(
            favorites_drop_accepted_callback_);
      }
      folder_row->SetRevealEmptyPinnedDropLaneCallback(
          base::BindRepeating(&MahoSidebarTabListView::RevealEmptyPinnedDropLane,
                              base::Unretained(this)));

      folder_rows_by_id_[node.folder_id] = folder_row;

      folder_row->SetHoverPopupCallbacks(
          base::BindRepeating(&MahoSidebarTabListView::ScheduleFolderHoverPopup,
                              weak_factory_.GetWeakPtr(), folder_row,
                              node.folder_id, node.folder_name),
          base::BindRepeating(&MahoSidebarFolderHoverController::OnRowMouseExited,
                              folder_hover_controller_->AsWeakPtr()));

      folder_row->SetSpringExpandCallback(
          base::BindRepeating(&MahoSidebarTabListView::ToggleFolderExpanded,
                              weak_factory_.GetWeakPtr(), node.folder_id));

      if (node.is_expanded && !node.children.empty()) {
        RebuildTreeNodes(parent, node.children, section, browser,
                         node.folder_id);
      } else if (!node.children.empty()) {
        // Collapsed folder: keep the active descendant visible as a sticky
        // row directly under the folder row.
        AddCollapsedActiveRow(parent, parent->children().size(), node, section,
                              browser);
      }

      if (index + 1 == nodes.size()) {
        auto* append_folder_lane =
            parent->AddChildView(std::make_unique<SidebarInsertionLaneView>(
                active_space_id_, node.folder_id, SidebarNodeKind::kFolder,
                next_sibling_folder_id.empty() ? node.folder_id
                                               : next_sibling_folder_id,
                true, sibling_parent_folder_id, -1, section,
                weak_factory_.GetWeakPtr()));
        if (favorites_drop_accepted_callback_) {
          append_folder_lane->SetFavoritesDropAcceptedCallback(
              favorites_drop_accepted_callback_);
        }
        append_folder_lane->SetRevealEmptyPinnedDropLaneCallback(
            base::BindRepeating(&MahoSidebarTabListView::RevealEmptyPinnedDropLane,
                                base::Unretained(this)));
      }
    } else if (node.kind == SidebarNodeKind::kSplitGroup) {
      RebuildSplitGroupContainer(parent, node, section, browser,
                                 parent_folder_id);
    } else {
      auto* before_tab_lane =
          parent->AddChildView(std::make_unique<SidebarInsertionLaneView>(
              active_space_id_, node.tab_id, SidebarNodeKind::kTab, node.tab_id,
              false, parent_folder_id, static_cast<int>(index), section,
              weak_factory_.GetWeakPtr()));
      if (favorites_drop_accepted_callback_) {
        before_tab_lane->SetFavoritesDropAcceptedCallback(
            favorites_drop_accepted_callback_);
      }
      before_tab_lane->SetRevealEmptyPinnedDropLaneCallback(
          base::BindRepeating(&MahoSidebarTabListView::RevealEmptyPinnedDropLane,
                              base::Unretained(this)));
      parent->AddChildView(CreateTabRowView(node, section, browser));

      if (index + 1 == nodes.size()) {
        auto* append_tab_lane =
            parent->AddChildView(std::make_unique<SidebarInsertionLaneView>(
                active_space_id_, node.tab_id, SidebarNodeKind::kTab, node.tab_id,
                true, parent_folder_id, static_cast<int>(index + 1), section,
                weak_factory_.GetWeakPtr()));
        if (favorites_drop_accepted_callback_) {
          append_tab_lane->SetFavoritesDropAcceptedCallback(
              favorites_drop_accepted_callback_);
        }
        append_tab_lane->SetRevealEmptyPinnedDropLaneCallback(
            base::BindRepeating(&MahoSidebarTabListView::RevealEmptyPinnedDropLane,
                                base::Unretained(this)));
      }
    }
  }
}

std::unique_ptr<SidebarTabRowView> MahoSidebarTabListView::CreateTabRowView(
    const SidebarTreeNode& node,
    MahoSidebarTabSection section,
    Browser* browser) {
  const int live_index = ResolveTabStripIndexForTabId(node.tab_id);
  // The close-affordance icon reflects CORE suspend state only. A lazily
  // restored pinned tab is live in core (is_suspended=false) but has no live
  // WebContents yet (live_index<0); it must still render "−" (unload), not "X"
  // (delete). Deriving this from live_index<0 caused restored live pinned tabs
  // to render X and — worse — CloseTabById to delete them on first click.
  const bool is_suspended = node.is_suspended;
  content::WebContents* contents = nullptr;
  tabs::TabInterface* tab_iface = nullptr;
  if (browser && browser->GetTabStripModel() && live_index >= 0) {
    contents = browser->GetTabStripModel()->GetWebContentsAt(live_index);
    if (contents) {
      tab_iface = tabs::TabInterface::GetFromContents(contents);
    }
  }
  TabUIHelper* tab_ui =
      tab_iface ? TabUIHelper::From(tab_iface) : nullptr;
  ui::ImageModel fav = tab_ui ? tab_ui->GetFavicon() : ui::ImageModel();
  GURL page_url(node.url);
  const uint64_t favicon_generation =
      UpdateRowFaviconRequest(node.tab_id, page_url.is_valid() ? page_url
                                                               : GURL());
  if (fav.IsEmpty() && !node.favicon_png_data.empty()) {
    gfx::Image png = gfx::Image::CreateFrom1xPNGBytes(node.favicon_png_data);
    if (!png.IsEmpty()) {
      fav = ui::ImageModel::FromImage(png);
    }
  }
  const bool needs_async_favicon = fav.IsEmpty();
  if (fav.IsEmpty()) {
    fav = ui::ImageModel::FromVectorIcon(
        vector_icons::kGlobeIcon,
        ResolveSidebarPaletteColor(palette_, this, palette_.neutral_glyph,
                                   ui::kColorSysOnSurfaceSubtle),
        kRowIconSizeDp);
  }
  if (needs_async_favicon && browser_ && !node.url.empty()) {
    if (page_url.is_valid()) {
      favicon::FaviconService* favicon_service =
          FaviconServiceFactory::GetForProfile(
              browser_->GetProfile(), ServiceAccessType::EXPLICIT_ACCESS);
      if (favicon_service) {
        favicon_service->GetFaviconImageForPageURL(
            page_url,
            base::BindOnce(&MahoSidebarTabListView::OnFaviconLoaded,
                            weak_factory_.GetWeakPtr(), node.tab_id,
                            page_url, favicon_generation),
            &favicon_task_tracker_);
      }
    }
  }

  auto row_view = std::make_unique<SidebarTabRowView>(
      node, std::move(fav), browser, section, active_space_id_,
      ResolveTabDisplayText(node),
      base::BindRepeating(&MahoSidebarTabListView::ActivateTabById,
                          weak_factory_.GetWeakPtr(),
                          node.tab_id),
      base::BindRepeating(&MahoSidebarTabListView::CloseTabById,
                          weak_factory_.GetWeakPtr(),
                          node.tab_id),
      base::BindRepeating(&MahoSidebarTabListView::MuteTabById,
                          weak_factory_.GetWeakPtr(),
                          node.tab_id),
      base::BindRepeating(&MahoSidebarTabListView::CommitTabTitleChange,
                          weak_factory_.GetWeakPtr()),
      base::BindRepeating(&MahoSidebarTabListView::ResolveTabStripIndexForTabId,
                          base::Unretained(this)),
      base::BindRepeating(&MahoSidebarTabListView::HandleTabRowHoverStart,
                          base::Unretained(this)),
      base::BindRepeating(&MahoSidebarTabListView::HandleTabRowHoverEnd,
                          base::Unretained(this)));
  row_view->SetSuspended(is_suspended);
  row_view->SetSidebarPalette(palette_);
  if (favorites_drop_accepted_callback_) {
    row_view->SetFavoritesDropAcceptedCallback(
        favorites_drop_accepted_callback_);
  }
  row_view->SetRevealEmptyPinnedDropLaneCallback(
      base::BindRepeating(&MahoSidebarTabListView::RevealEmptyPinnedDropLane,
                          base::Unretained(this)));
  row_view->SetOwningTabListView(weak_factory_.GetWeakPtr());
  row_view->SetTooltipText(GetNodeTooltipText(node));
  return row_view;
}

namespace {

const SidebarTreeNode* FindActiveTabDescendant(
    const std::vector<SidebarTreeNode>& nodes) {
  for (const auto& n : nodes) {
    if (n.kind == SidebarNodeKind::kTab && n.is_active) {
      return &n;
    }
    if (!n.children.empty()) {
      if (const SidebarTreeNode* found = FindActiveTabDescendant(n.children)) {
        return found;
      }
    }
  }
  return nullptr;
}

}  // namespace

SidebarTabRowView* MahoSidebarTabListView::AddCollapsedActiveRow(
    views::View* parent,
    size_t insert_index,
    const SidebarTreeNode& folder_node,
    MahoSidebarTabSection section,
    Browser* browser) {
  const SidebarTreeNode* active_desc =
      FindActiveTabDescendant(folder_node.children);
  if (!active_desc) {
    return nullptr;
  }
  SidebarTreeNode sticky = *active_desc;
  sticky.children.clear();
  // Render at one level under the visible folder row even when the tab sits
  // deeper in a nested collapsed folder; is_in_split is cleared so the row
  // paints as an ordinary standalone row outside any split container.
  sticky.depth = folder_node.depth + 1;
  sticky.is_in_split = false;
  SidebarTabRowView* row = parent->AddChildViewAt(
      CreateTabRowView(sticky, section, browser), insert_index);
  row->SetCollapsedStickyFolderId(folder_node.folder_id);
  tab_id_to_row_view_[row->tab_id()] = row;
  row->SetControlled(IsControlledTabRow(*row));
  return row;
}

void MahoSidebarTabListView::MaterializeStickyRowForTab(
    const std::string& tab_id) {
  if (!has_last_model_ || tab_id.empty()) {
    return;
  }
  // Anchor on the OUTERMOST collapsed folder on the path to |tab_id|: that is
  // the only folder row that is actually materialized (rows inside collapsed
  // folders are not built), so the sticky row must hang directly under it.
  const SidebarTreeNode* anchor_folder = nullptr;
  const SidebarTreeNode* first_collapsed = nullptr;
  std::function<bool(const std::vector<SidebarTreeNode>&)> find_path;
  find_path = [&](const std::vector<SidebarTreeNode>& nodes) -> bool {
    for (const auto& n : nodes) {
      if (n.kind == SidebarNodeKind::kTab && n.tab_id == tab_id) {
        return first_collapsed != nullptr;
      }
      if (!n.children.empty()) {
        const SidebarTreeNode* saved = first_collapsed;
        if (n.kind == SidebarNodeKind::kFolder && !n.is_expanded &&
            !first_collapsed) {
          first_collapsed = &n;
        }
        if (find_path(n.children)) {
          return true;
        }
        first_collapsed = saved;
      }
    }
    return false;
  };
  if (!find_path(last_model_.pinned_tree) &&
      !find_path(last_model_.normal_tree)) {
    return;
  }
  anchor_folder = first_collapsed;
  auto it = folder_rows_by_id_.find(anchor_folder->folder_id);
  if (it == folder_rows_by_id_.end() || !it->second) {
    return;
  }
  SidebarFolderRowView* folder_row = it->second;
  views::View* parent_view = folder_row->parent();
  if (!parent_view) {
    return;
  }
  auto folder_index = parent_view->GetIndexOf(folder_row);
  if (!folder_index.has_value()) {
    return;
  }
  if (AddCollapsedActiveRow(parent_view, *folder_index + 1, *anchor_folder,
                            folder_row->section(), browser_)) {
    parent_view->InvalidateLayout();
  }
}

void MahoSidebarTabListView::RebuildSplitGroupContainer(
    views::View* parent,
    const SidebarTreeNode& group_node,
    MahoSidebarTabSection section,
    Browser* browser,
    const std::string& parent_folder_id) {
  parent->AddChildView(
      BuildSplitGroupContainer(group_node, section, browser, parent_folder_id));
}

std::unique_ptr<views::View> MahoSidebarTabListView::BuildSplitGroupContainer(
    const SidebarTreeNode& group_node,
    MahoSidebarTabSection section,
    Browser* browser,
    const std::string& parent_folder_id) {
  auto split_container = std::make_unique<SplitGroupContainerView>();
  split_container->SetSidebarPalette(palette_);

  const bool is_side_by_side = (group_node.split_orientation == "vertical");
  bool force_vertical = false;
  if (browser && browser->GetProfile()) {
    force_vertical = !maho::sidebar_prefs::IsSidebarPanelExpanded(browser->GetProfile()->GetPrefs());
  }
  const bool effective_side_by_side = is_side_by_side && !force_vertical;
  split_container->SetOrientation(
      effective_side_by_side ? views::LayoutOrientation::kHorizontal
                             : views::LayoutOrientation::kVertical);

  // No horizontal inset: each pane row already carries its own 9px leading
  // inset, so any container padding would push the first pane's favicon out of
  // alignment with the ordinary (non-split) rows above and below it.
  split_container->SetInsideBorderInsets(gfx::Insets());

  split_container->GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
  split_container->GetViewAccessibility().SetName(u"Split view");

  std::function<bool(const SidebarTreeNode&)> has_active_descendant;
  has_active_descendant = [&](const SidebarTreeNode& node) -> bool {
    if (node.is_active) {
      return true;
    }
    return std::any_of(
        node.children.begin(), node.children.end(),
        [&](const SidebarTreeNode& child) {
          return has_active_descendant(child);
        });
  };
  const bool has_active_child = has_active_descendant(group_node);
  split_container->SetActiveState(has_active_child);

  const bool is_new_split =
      !group_node.split_id.empty() &&
      !last_rendered_split_ids_.count(group_node.split_id);
  if (!group_node.split_id.empty()) {
    current_rebuild_split_ids_.insert(group_node.split_id);
  }
  if (is_new_split) {
    splits_awaiting_anim_.insert(group_node.split_id);
  }

  auto* container_ptr = split_container.get();

  if (!group_node.split_id.empty()) {
    split_id_to_container_[group_node.split_id] = container_ptr;
  }

  const double clamped_ratio =
      std::clamp(group_node.split_ratio, 0.05, 0.95);
  for (size_t i = 0; i < group_node.children.size(); ++i) {
    const size_t child_start = container_ptr->children().size();
    const double child_ratio =
        i == 0 ? clamped_ratio : 1.0 - clamped_ratio;
    const int child_flex =
        std::max(1, static_cast<int>(child_ratio * 1000.0));

    RebuildTreeNodes(container_ptr, {group_node.children[i]}, section,
                     browser, parent_folder_id);

    for (size_t v = child_start; v < container_ptr->children().size(); ++v) {
      views::View* child = container_ptr->children()[v];
      container_ptr->SetFlexForView(child, child_flex);
      if (auto* row = views::AsViewClass<SidebarTabRowView>(child);
          row && effective_side_by_side) {
        row->SetSplitCompactMode(true);
      }
    }

    if (i + 1 < group_node.children.size()) {
      auto divider = std::make_unique<views::View>();
      divider->SetPreferredSize(
          effective_side_by_side ? gfx::Size(1, kRowHeightDp - 8)
                                 : gfx::Size(kRowHeightDp - 8, 1));
      container_ptr->AddDivider(std::move(divider));
    }
  }

  // A split still awaiting its reveal needs a TEXTURED layer ready on this
  // (possibly just-rebuilt) container so AnimateAwaitingSplits can transform it
  // once the rebuild burst settles. TEXTURED avoids the LAYER_SOLID_COLOR path,
  // on which SetFillsBoundsOpaquely() CHECK-fails (ui/compositor/layer.cc:955).
  if (effective_side_by_side && !group_node.split_id.empty() &&
      splits_awaiting_anim_.count(group_node.split_id)) {
    container_ptr->SetPaintToLayer(ui::LAYER_TEXTURED);
    container_ptr->layer()->SetFillsBoundsOpaquely(false);
    // Start hidden so the split does not flash in at full size before the
    // debounced reveal animation runs; AnimateAwaitingSplits fades/expands it.
    container_ptr->layer()->SetOpacity(0.0f);
  }

  return split_container;
}

std::unique_ptr<views::View> MahoSidebarTabListView::RealizeProjectionRow(
    const SidebarVisualRow& row,
    Browser* browser) {
  switch (row.kind) {
    case SidebarVisualRowKind::kInsertionLane: {
      auto lane = std::make_unique<SidebarInsertionLaneView>(
          active_space_id_, row.lane_target_id, row.lane_target_kind,
          row.lane_target_id, row.lane_append, row.lane_parent_folder_id,
          row.lane_target_child_index, row.section,
          weak_factory_.GetWeakPtr());
      if (favorites_drop_accepted_callback_) {
        lane->SetFavoritesDropAcceptedCallback(
            favorites_drop_accepted_callback_);
      }
      lane->SetRevealEmptyPinnedDropLaneCallback(
          base::BindRepeating(&MahoSidebarTabListView::RevealEmptyPinnedDropLane,
                              base::Unretained(this)));
      return lane;
    }
    case SidebarVisualRowKind::kTab:
    case SidebarVisualRowKind::kCollapsedStickyTab: {
      std::unique_ptr<SidebarTabRowView> tab_row =
          CreateTabRowView(row.node, row.section, browser);
      if (row.kind == SidebarVisualRowKind::kCollapsedStickyTab) {
        tab_row->SetCollapsedStickyFolderId(row.parent_folder_id);
      }
      tab_row->SetSelected(selected_tab_ids_.count(row.node.tab_id) > 0);
      // The full projection is exposed via AXVirtualView children; ignore the
      // realized View so assistive tech does not see it twice.
      tab_row->GetViewAccessibility().SetIsIgnored(true);
      tab_id_to_row_view_[row.node.tab_id] = tab_row.get();
      tab_row->SetControlled(IsControlledTabRow(*tab_row));
      return tab_row;
    }
    case SidebarVisualRowKind::kFolder: {
      auto folder_row = std::make_unique<SidebarFolderRowView>(
          row.node, row.section, active_space_id_, browser,
          base::BindRepeating(&MahoSidebarTabListView::ToggleFolderExpanded,
                              weak_factory_.GetWeakPtr(), row.node.folder_id),
           base::BindRepeating(&MahoSidebarTabListView::CommitFolderNameChange,
                               weak_factory_.GetWeakPtr()),
           row.folder_next_sibling_id, row.node.parent_of_folder_id,
           row.folder_has_children);
      folder_row->SetSidebarPalette(palette_);
      if (favorites_drop_accepted_callback_) {
        folder_row->SetFavoritesDropAcceptedCallback(
            favorites_drop_accepted_callback_);
      }
      folder_row->SetRevealEmptyPinnedDropLaneCallback(
          base::BindRepeating(&MahoSidebarTabListView::RevealEmptyPinnedDropLane,
                              base::Unretained(this)));
      folder_rows_by_id_[row.node.folder_id] = folder_row.get();
      folder_row->GetViewAccessibility().SetIsIgnored(true);
      folder_row->SetHoverPopupCallbacks(
          base::BindRepeating(&MahoSidebarTabListView::ScheduleFolderHoverPopup,
                              weak_factory_.GetWeakPtr(), folder_row.get(),
                              row.node.folder_id, row.node.folder_name),
          base::BindRepeating(
              &MahoSidebarFolderHoverController::OnRowMouseExited,
              folder_hover_controller_->AsWeakPtr()));
      folder_row->SetSpringExpandCallback(
          base::BindRepeating(&MahoSidebarTabListView::ToggleFolderExpanded,
                              weak_factory_.GetWeakPtr(), row.node.folder_id));
      return folder_row;
    }
    case SidebarVisualRowKind::kSplitGroup: {
      std::unique_ptr<views::View> container = BuildSplitGroupContainer(
          row.node, row.section, browser, row.parent_folder_id);
      // The split's member tab rows are built inside the container; register
      // and selection-sync them the way top-level tab rows are, so the
      // realized-only lookup stays complete for the window.
      std::function<void(views::View*)> register_members;
      register_members = [&](views::View* view) {
        for (views::View* child : view->children()) {
          if (auto* member = views::AsViewClass<SidebarTabRowView>(child)) {
            if (!member->tab_id().empty()) {
              tab_id_to_row_view_[member->tab_id()] = member;
              member->SetSelected(selected_tab_ids_.count(member->tab_id()) >
                                  0);
              member->SetControlled(IsControlledTabRow(*member));
              member->GetViewAccessibility().SetIsIgnored(true);
            }
          }
          register_members(child);
        }
      };
      register_members(container.get());
      return container;
    }
  }
  return nullptr;
}

void MahoSidebarTabListView::ScheduleAccessibilityTreeRebuild() {
  if (!ShouldBuildAccessibilityTree()) {
    accessibility_rebuild_pending_ = false;
    GetViewAccessibility().RemoveAllVirtualChildViews();
    return;
  }
  if (accessibility_rebuild_pending_) {
    return;
  }
  accessibility_rebuild_pending_ = true;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(
          &MahoSidebarTabListView::RunScheduledAccessibilityTreeRebuild,
          weak_factory_.GetWeakPtr()));
}

void MahoSidebarTabListView::RunScheduledAccessibilityTreeRebuild() {
  accessibility_rebuild_pending_ = false;
  if (!ShouldBuildAccessibilityTree()) {
    GetViewAccessibility().RemoveAllVirtualChildViews();
    return;
  }
  RebuildAccessibilityTree();
}

void MahoSidebarTabListView::RebuildAccessibilityTree() {
  base::ElapsedTimer accessibility_t;
  ++accessibility_rebuild_count_for_testing_;
  views::ViewAccessibility& ax = GetViewAccessibility();
  ax.RemoveAllVirtualChildViews();
  if (!ShouldBuildAccessibilityTree()) {
    return;
  }

  const int set_size = CountProjectionTabRows(pinned_projection_) +
                       CountProjectionTabRows(normal_projection_);
  int pos_in_set = 0;

  auto add_tab_node = [&](const SidebarTreeNode& node) {
    auto virtual_view = std::make_unique<views::AXVirtualView>();
    virtual_view->SetRole(ax::mojom::Role::kTab);
    std::u16string name = ResolveTabDisplayText(node);
    if (name.empty()) {
      name = base::UTF8ToUTF16(node.tab_id);
    }
    virtual_view->SetName(name);
    virtual_view->SetPosInSet(++pos_in_set);
    virtual_view->SetSetSize(set_size);
    virtual_view->SetIsSelected(selected_tab_ids_.count(node.tab_id) > 0);
    ax.AddVirtualChildView(std::move(virtual_view));
  };
  auto add_folder_node = [&](const SidebarTreeNode& node) {
    auto virtual_view = std::make_unique<views::AXVirtualView>();
    virtual_view->SetRole(ax::mojom::Role::kGroup);
    virtual_view->SetName(node.folder_name.empty() ? std::u16string(u"Folder")
                                                    : node.folder_name);
    ax.AddVirtualChildView(std::move(virtual_view));
  };
  std::function<void(const std::vector<SidebarTreeNode>&)> add_split_members;
  add_split_members = [&](const std::vector<SidebarTreeNode>& nodes) {
    for (const SidebarTreeNode& n : nodes) {
      if (n.kind == SidebarNodeKind::kTab && !n.tab_id.empty()) {
        add_tab_node(n);
      }
      add_split_members(n.children);
    }
  };

  for (const std::vector<SidebarVisualRow>* proj :
       {&pinned_projection_, &normal_projection_}) {
    for (const SidebarVisualRow& row : *proj) {
      switch (row.kind) {
        case SidebarVisualRowKind::kTab:
        case SidebarVisualRowKind::kCollapsedStickyTab:
          add_tab_node(row.node);
          break;
        case SidebarVisualRowKind::kFolder:
          add_folder_node(row.node);
          break;
        case SidebarVisualRowKind::kSplitGroup:
          add_split_members(row.node.children);
          break;
        case SidebarVisualRowKind::kInsertionLane:
          break;
      }
    }
  }
  DVLOG(1) << "[MAHO_PERF] RebuildAccessibilityTree "
           << accessibility_t.Elapsed().InMillisecondsF() << "ms";
}

bool MahoSidebarTabListView::ShouldBuildAccessibilityTree() const {
  if (accessibility_tree_enabled_for_testing_.has_value()) {
    return *accessibility_tree_enabled_for_testing_;
  }
  return ShouldBuildSidebarVirtualAccessibilityTree();
}

void MahoSidebarTabListView::set_accessibility_tree_enabled_for_testing(
    bool enabled) {
  accessibility_tree_enabled_for_testing_ = enabled;
  if (!enabled) {
    accessibility_rebuild_pending_ = false;
    GetViewAccessibility().RemoveAllVirtualChildViews();
  }
}

void MahoSidebarTabListView::ReconcileSectionWindow(
    MahoSidebarTabSection section,
    Browser* browser) {
  base::ElapsedTimer reconcile_t;
  // Never re-enter reconciliation: a synchronous scroll callback (e.g. a spacer
  // resize clamping the offset) must not corrupt window_/children mid-mutation.
  if (reconciling_) {
    return;
  }
  base::AutoReset<bool> reentry_guard(&reconciling_, true);

  const bool is_pinned = (section == MahoSidebarTabSection::kPinned);
  std::vector<SidebarVisualRow>& projection =
      is_pinned ? pinned_projection_ : normal_projection_;
  RealizedWindow& window = is_pinned ? pinned_window_ : normal_window_;
  views::View* top_spacer =
      is_pinned ? pinned_top_spacer_ : normal_top_spacer_;
  views::View* bottom_spacer =
      is_pinned ? pinned_bottom_spacer_ : normal_bottom_spacer_;
  if (!top_spacer || !bottom_spacer) {
    return;
  }
  views::View* rows_container = top_spacer->parent();
  if (!rows_container) {
    return;
  }

  auto remove_realized_at = [&](size_t pos) {
    views::View* child = rows_container->children()[pos];
    // Never free the active drag source: its drag runs a nested run loop on its
    // own stack frame (macOS), so move it to the hidden preserve holder instead
    // (mirrors the RebuildRows path); OnDragSourceFinished releases it later.
    if (!drag_source_tab_id_.empty()) {
      if (auto* tab_row = views::AsViewClass<SidebarTabRowView>(child);
          tab_row && tab_row->tab_id() == drag_source_tab_id_) {
        if (!drag_preserve_holder_) {
          auto holder = std::make_unique<views::View>();
          holder->SetVisible(false);
          drag_preserve_holder_ = AddChildView(std::move(holder));
        }
        tab_id_to_row_view_.erase(tab_row->tab_id());
        drag_preserve_holder_->AddChildView(
            rows_container->RemoveChildViewT(child));
        return;
      }
    }
    // Drop registry entries before the view is freed (UAF/dangling raw_ptr).
    UnregisterRowsRecursive(child);
    rows_container->RemoveChildViewT(child);
  };

  const int row_count = static_cast<int>(projection.size());
  if (row_count == 0) {
    while (rows_container->children().size() > 2u) {
      remove_realized_at(1);
    }
    top_spacer->SetPreferredSize(gfx::Size(0, 0));
    bottom_spacer->SetPreferredSize(gfx::Size(0, 0));
    window = {0, -1};
    DVLOG(1) << "[MAHO_PERF] ReconcileSectionWindow "
             << (is_pinned ? "pinned" : "normal") << " "
             << reconcile_t.Elapsed().InMillisecondsF() << "ms rows=0";
    return;
  }

  // Section-local visible rect. Before the first real layout the scroll
  // geometry is not yet meaningful, so fall back to a top-anchored window.
  gfx::Rect local_rect;
  if (scroll_view_) {
    local_rect = views::View::ConvertRectToTarget(
        this, rows_container, scroll_view_->GetVisibleRect());
  }
  if (local_rect.height() <= 0) {
    local_rect = gfx::Rect(0, 0, 1, 40 * kRowHeightDp);
  }

  const int total_height =
      projection.back().cumulative_top_dp + projection.back().height_dp;

  const SidebarVisibilityManager::VisibleRange range =
      visibility_manager_->ComputeVisibleRange(
          local_rect, row_count, total_height, [&projection](int index) {
            return projection[index].cumulative_top_dp;
          });
  const int new_first = std::clamp(range.first_visible_index, 0, row_count - 1);
  const int new_last =
      std::clamp(range.last_visible_index, new_first, row_count - 1);

  const int old_first = window.first;
  const int old_last = window.last;
  const bool have_old = (old_last >= old_first);

  if (!have_old || new_first > old_last || new_last < old_first) {
    // No overlap: drop every realized row, rebuild the window fresh.
    while (rows_container->children().size() > 2u) {
      remove_realized_at(1);
    }
    for (int i = new_first; i <= new_last; ++i) {
      rows_container->AddChildViewAt(RealizeProjectionRow(projection[i], browser),
                                     1 + (i - new_first));
    }
  } else {
    // Remove rows that left the window; keep the overlap untouched.
    for (int i = old_first; i < new_first; ++i) {
      remove_realized_at(1);  // just after top_spacer
    }
    for (int i = old_last; i > new_last; --i) {
      remove_realized_at(rows_container->children().size() - 2u);
    }
    // Add rows that entered. Leading rows are inserted just after top_spacer
    // in descending order so the final ascending order is preserved.
    for (int i = old_first - 1; i >= new_first; --i) {
      rows_container->AddChildViewAt(RealizeProjectionRow(projection[i], browser),
                                     1);
    }
    for (int i = old_last + 1; i <= new_last; ++i) {
      rows_container->AddChildViewAt(
          RealizeProjectionRow(projection[i], browser),
          rows_container->children().size() - 1u);
    }
  }
  window = {new_first, new_last};

  // Refresh the projected height of each realized row from its measured height
  // so variable-height (split) rows keep the extent invariant exact. Most
  // scroll ticks do not change row heights, so only recompute prefix sums from
  // the first changed row onward.
  int dirty_from = row_count;
  for (int i = new_first; i <= new_last; ++i) {
    views::View* child = rows_container->children()[1 + (i - new_first)];
    const int measured = child->GetPreferredSize(views::SizeBounds()).height();
    if (measured != projection[i].height_dp) {
      projection[i].height_dp = measured;
      dirty_from = std::min(dirty_from, i);
    }
  }
  if (dirty_from < row_count) {
    int cumulative = projection[dirty_from].cumulative_top_dp;
    for (int i = dirty_from; i < row_count; ++i) {
      projection[i].cumulative_top_dp = cumulative;
      cumulative += projection[i].height_dp;
    }
  }
  const int extent =
      projection.back().cumulative_top_dp + projection.back().height_dp;
  const int top_height = projection[new_first].cumulative_top_dp;
  const int bottom_height =
      extent - (projection[new_last].cumulative_top_dp +
                projection[new_last].height_dp);
  top_spacer->SetPreferredSize(gfx::Size(0, top_height));
  bottom_spacer->SetPreferredSize(gfx::Size(0, bottom_height));
  DCHECK_EQ(top_height + bottom_height +
                (projection[new_last].cumulative_top_dp +
                 projection[new_last].height_dp -
                 projection[new_first].cumulative_top_dp),
            extent)
      << "Section window extent invariant violated";
  DVLOG(1) << "[MAHO_PERF] ReconcileSectionWindow "
           << (is_pinned ? "pinned" : "normal") << " "
           << reconcile_t.Elapsed().InMillisecondsF() << "ms rows=" << row_count;
}

void MahoSidebarTabListView::AnimateAwaitingSplits() {
  for (const auto& split_id : splits_awaiting_anim_) {
    auto it = split_id_to_container_.find(split_id);
    if (it == split_id_to_container_.end() || !it->second) {
      continue;
    }
    views::View* container = it->second;
    if (!container->layer()) {
      continue;
    }
    const int w = container->width();
    const int h = container->height();
    if (w <= 0 || h <= 0) {
      continue;
    }
    ui::Layer* layer = container->layer();
    // Reveal by unfolding horizontally about the row's center so the two panes
    // appear to split apart to the left and right.
    gfx::Transform start;
    start.Translate(w / 2.0, h / 2.0);
    start.Scale(0.35, 1.0);
    start.Translate(-w / 2.0, -h / 2.0);
    layer->SetTransform(start);
    layer->SetOpacity(0.0f);
    ui::ScopedLayerAnimationSettings settings(layer->GetAnimator());
    settings.SetTransitionDuration(base::Milliseconds(200));
    settings.SetTweenType(gfx::Tween::EASE_OUT);
    layer->SetTransform(gfx::Transform());
    layer->SetOpacity(1.0f);
  }
  splits_awaiting_anim_.clear();
}

std::u16string MahoSidebarTabListView::GetNodeDisplayText(
    const SidebarTreeNode& node) const {
  return ResolveTabDisplayText(node);
}

std::u16string MahoSidebarTabListView::GetNodeTooltipText(
    const SidebarTreeNode& node) const {
  const std::u16string display = ResolveTabDisplayText(node);
  if (node.host.empty() || node.host == display) {
    return display;
  }
  if (display.empty()) {
    return node.host;
  }
  return display + u"\n" + node.host;
}

void MahoSidebarTabListView::ActivateTab(int tab_index) {
  TabStripModel* model = browser_ ? browser_->GetTabStripModel() : nullptr;
  if (!model || !model->ContainsIndex(tab_index)) {
    return;
  }

  model->ActivateTabAt(tab_index);
}

namespace {

struct PersistedTabInfo {
  std::string url;
  std::string title;
  bool is_pinned = false;
  std::string role_type;
  std::string pinned_url;
  double scroll_x = 0.0;
  double scroll_y = 0.0;
};

std::optional<PersistedTabInfo> QueryTabFromCore(const std::string& tab_id) {
  auto* core = maho::GetCore();
  if (!core) {
    return std::nullopt;
  }
  // O(1) single-tab lookup — replaces O(N) maho_core_get_tab_view_models scan
  char* json_str = maho_core_get_tab_snapshot_by_id(core, tab_id.c_str());
  if (!json_str) {
    return std::nullopt;
  }
  std::string json(json_str);
  maho_string_free(json_str);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return std::nullopt;
  }

  const auto& dict = parsed->GetDict();
  PersistedTabInfo info;
  if (const std::string* url = dict.FindString("url")) info.url = *url;
  if (const std::string* title = dict.FindString("title")) info.title = *title;
  if (auto is_pinned = dict.FindBool("isPinned")) info.is_pinned = *is_pinned;
  if (const auto* role = dict.FindDict("role")) {
    if (const std::string* type = role->FindString("type")) {
      info.role_type = *type;
    }
  }
  if (const std::string* pinned_url = dict.FindString("pinnedUrl")) {
    info.pinned_url = *pinned_url;
  }
  if (const auto* scroll = dict.FindDict("scrollPosition")) {
    if (auto x = scroll->FindDouble("x")) info.scroll_x = *x;
    if (auto y = scroll->FindDouble("y")) info.scroll_y = *y;
  }
  return info;
}

}  // namespace

void MahoSidebarTabListView::ActivateTabById(const std::string& tab_id) {
  const int index = ResolveTabStripIndexForTabId(tab_id);
  if (index >= 0) {
    ActivateTab(index);
  } else {
    WakeSuspendedTab(tab_id);
  }
}

std::vector<std::string> MahoSidebarTabListView::GetTabVisualOrderIds() const {
  std::vector<std::string> ids;
  std::function<void(const std::vector<SidebarTreeNode>&)> collect_split_tabs;
  collect_split_tabs = [&](const std::vector<SidebarTreeNode>& nodes) {
    for (const SidebarTreeNode& n : nodes) {
      if (n.kind == SidebarNodeKind::kTab && !n.tab_id.empty()) {
        ids.push_back(n.tab_id);
      }
      collect_split_tabs(n.children);
    }
  };
  for (const std::vector<SidebarVisualRow>* proj :
       {&pinned_projection_, &normal_projection_}) {
    for (const SidebarVisualRow& row : *proj) {
      switch (row.kind) {
        case SidebarVisualRowKind::kTab:
        case SidebarVisualRowKind::kCollapsedStickyTab:
          if (!row.node.tab_id.empty()) {
            ids.push_back(row.node.tab_id);
          }
          break;
        case SidebarVisualRowKind::kSplitGroup:
          collect_split_tabs(row.node.children);
          break;
        case SidebarVisualRowKind::kInsertionLane:
        case SidebarVisualRowKind::kFolder:
          break;
      }
    }
  }
  return ids;
}

void MahoSidebarTabListView::RevealTabById(const std::string& tab_id) {
  if (tab_id.empty() || !visibility_manager_) {
    return;
  }
  auto it = tab_id_to_row_view_.find(tab_id);
  if (it != tab_id_to_row_view_.end() && it->second) {
    if (scroll_view_) {
      const gfx::Rect row_bounds = views::View::ConvertRectToTarget(
          it->second, this, it->second->GetLocalBounds());
      if (scroll_view_->GetVisibleRect().Contains(row_bounds)) {
        return;
      }
    }
    if (GetWidget()) {
      GetWidget()->LayoutRootViewIfNecessary();
    }
    it->second->ScrollViewToVisible();
    return;
  }
  auto index_in = [&tab_id, this](const std::vector<SidebarVisualRow>& proj) -> int {
    for (size_t i = 0; i < proj.size(); ++i) {
      const SidebarVisualRow& row = proj[i];
      if ((row.kind == SidebarVisualRowKind::kTab ||
           row.kind == SidebarVisualRowKind::kCollapsedStickyTab) &&
          row.node.tab_id == tab_id) {
        return static_cast<int>(i);
      }
      if (row.kind == SidebarVisualRowKind::kSplitGroup) {
        for (const SidebarTreeNode& child : row.node.children) {
          if (child.kind == SidebarNodeKind::kTab && child.tab_id == tab_id) {
            return static_cast<int>(i);
          }
        }
      }
      if (row.kind == SidebarVisualRowKind::kFolder) {
        if (const std::vector<SidebarTreeNode>* children =
                FindFolderChildren(row.stable_id)) {
          std::function<bool(const std::vector<SidebarTreeNode>&)> contains_tab;
          contains_tab = [&](const std::vector<SidebarTreeNode>& nodes) {
            for (const auto& n : nodes) {
              if (n.kind == SidebarNodeKind::kTab && n.tab_id == tab_id) {
                return true;
              }
              if (!n.children.empty() && contains_tab(n.children)) {
                return true;
              }
            }
            return false;
          };
          if (contains_tab(*children)) {
            return static_cast<int>(i);
          }
        }
      }
    }
    return -1;
  };
  MahoSidebarTabSection section = MahoSidebarTabSection::kNormal;
  int index = index_in(normal_projection_);
  if (index < 0) {
    index = index_in(pinned_projection_);
    section = MahoSidebarTabSection::kPinned;
  }
  if (index < 0) {
    return;
  }

  // Force the descriptor into the realized window so it materializes even when
  // geometrically off-window, lay it out, then scroll it into view. The forced
  // index keeps it realized across the (possibly async) scroll; the geometric
  // window covers it once the scroll settles, so bounded realization is
  // restored by the next scroll-driven reconcile.
  visibility_manager_->SetForcedVisibleIndex(index);
  ReconcileSectionWindow(section, browser_);
  if (GetWidget()) {
    GetWidget()->LayoutRootViewIfNecessary();
  }
  it = tab_id_to_row_view_.find(tab_id);
  if (it == tab_id_to_row_view_.end() || !it->second) {
    MaterializeStickyRowForTab(tab_id);
    if (GetWidget()) {
      GetWidget()->LayoutRootViewIfNecessary();
    }
    it = tab_id_to_row_view_.find(tab_id);
  }
  if (it != tab_id_to_row_view_.end() && it->second) {
    it->second->ScrollViewToVisible();
  }
  visibility_manager_->ClearForcedVisibleIndex();
  if (tab_rows_) {
    tab_rows_->InvalidateLayout();
  }
}

bool MahoSidebarTabListView::ActivateAdjacentTabInVisualOrder(bool forward) {
  // Projection order includes every tab (realized or not); a collapsed folder's
  // sticky descendant appears once here, so no realized-row dedupe is needed.
  std::vector<std::string> ordered = GetTabVisualOrderIds();
  if (ordered.empty()) {
    return false;
  }

  std::string active_tab_id;
  if (browser_ && browser_->GetTabStripModel()) {
    active_tab_id = FindCoreTabIdByWebContents(
        browser_->GetTabStripModel()->GetActiveWebContents());
  }

  int current = -1;
  for (size_t i = 0; i < ordered.size(); ++i) {
    if (ordered[i] == active_tab_id) {
      current = static_cast<int>(i);
      break;
    }
  }

  const int count = static_cast<int>(ordered.size());
  int target = 0;
  if (current < 0) {
    target = forward ? 0 : count - 1;
  } else {
    target = (current + (forward ? 1 : -1) + count) % count;
  }

  ActivateTabById(ordered[target]);
  return true;
}

void MahoSidebarTabListView::SetDragSourceTabId(const std::string& tab_id) {
  drag_source_tab_id_ = tab_id;
}

void MahoSidebarTabListView::OnDragSourceFinished() {
  drag_source_tab_id_.clear();
  // Drag completion, not target handoff, owns transient-lane teardown. This
  // keeps hit-test geometry stable for the entire native drag session.
  HideEmptyPinnedDropLane();
  if (!drag_preserve_holder_ || drag_preserve_holder_->children().empty()) {
    return;
  }
  // The drag's nested run loop is still unwinding through the source row's own
  // stack frame here (macOS), so hand the preserved rows to a follow-up task and
  // let them die only once control has returned to the message loop.
  std::vector<std::unique_ptr<views::View>> preserved;
  while (!drag_preserve_holder_->children().empty()) {
    preserved.push_back(drag_preserve_holder_->RemoveChildViewT(
        drag_preserve_holder_->children().front()));
  }
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce([](std::vector<std::unique_ptr<views::View>>) {},
                     std::move(preserved)));
}

void MahoSidebarTabListView::FlushSpringLoadedSpaceSwitch() {
  FlushDeferredRebuild();
}

content::WebContents* MahoSidebarTabListView::WakeSuspendedTab(
    const std::string& tab_id) {
  TRACE_EVENT1("browser", "WakeSuspendedTab", "tab_id", tab_id);
  base::ElapsedTimer maho_perf_t;
  DVLOG(1) << "[MAHO_PERF] WakeSuspendedTab START tab_id=" << tab_id;

  TRACE_EVENT_INSTANT0("browser", "WakeSuspendedTab.QueryStart",
                       TRACE_EVENT_SCOPE_THREAD);
  base::ElapsedTimer query_t;
  auto persisted = QueryTabFromCore(tab_id);
  const double query_ms = query_t.Elapsed().InMillisecondsF();
  TRACE_EVENT_INSTANT1("browser", "WakeSuspendedTab.QueryEnd",
                       TRACE_EVENT_SCOPE_THREAD, "ms", query_ms);
  DVLOG(1) << "[MAHO_PERF]   QueryTabFromCore=" << query_ms << "ms";
  if (!persisted) {
    DVLOG(1) << "[MAHO_PERF] WakeSuspendedTab END(no_persisted) "
              << maho_perf_t.Elapsed().InMillisecondsF() << "ms";
    return nullptr;
  }

  // A favorite wakes at its home (pinned) page: the live URL only records the
  // page the tab was left on, and the sidebar tile stands for the pinned page.
  // Pinned (non-favorite) tabs keep resuming their live URL so
  // pinned_close_behavior stays the authority there.
  const bool wakes_at_pinned_url =
      persisted->role_type == "favorite" && !persisted->pinned_url.empty();
  const std::string wake_url =
      wakes_at_pinned_url ? persisted->pinned_url : persisted->url;

  TRACE_EVENT_INSTANT0("browser", "WakeSuspendedTab.NavigateStart",
                       TRACE_EVENT_SCOPE_THREAD);
  base::ElapsedTimer nav_t;
  NavigateParams params(browser_, GURL(wake_url),
                        ui::PAGE_TRANSITION_AUTO_BOOKMARK);
  params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  {
    // The strip-insert observer announces create_tab synchronously INSIDE
    // Navigate(), while the fresh WebContents still carries the throwaway
    // UUID minted by TabHelpers. This scope makes that announcement adopt
    // |tab_id| first, so maho-core restores the existing suspended tab
    // instead of creating a new Normal tab. The direct SetRestoredTabId
    // below stays as a fallback for paths where no announcement happened
    // (e.g. space bridge not yet initialized).
    maho::ScopedMahoTabWakeId wake_scope(tab_id);
    waking_tab_id_ = tab_id;
    Navigate(&params);
    waking_tab_id_.clear();
  }
  const double navigate_ms = nav_t.Elapsed().InMillisecondsF();
  TRACE_EVENT_INSTANT1("browser", "WakeSuspendedTab.NavigateEnd",
                       TRACE_EVENT_SCOPE_THREAD, "ms", navigate_ms);
  DVLOG(1) << "[MAHO_PERF]   Navigate()=" << navigate_ms << "ms";

  if (params.navigated_or_inserted_contents) {
    // Bind the stable id directly on the freshly-created WebContents instead of
    // relying on the global SetPendingRestoredTabId FIFO queue. The queue's
    // blind front-pop can hand a stale, unconsumed id (e.g. a favorite whose
    // WebContents was never materialized under Model B) to this new tab,
    // branding it as the wrong tab. Direct binding is immune to that pollution.
    if (auto* id_helper =
            MahoTabIdHelper::FromWebContents(params.navigated_or_inserted_contents);
        id_helper && id_helper->stable_tab_id() != tab_id) {
      id_helper->SetRestoredTabId(tab_id);
    }

    // Scroll restoration only applies when the wake resumes the page the tab
    // was on; a home-page wake starts at the top.
    MahoTabWakeHelper::CreateForWebContents(
        params.navigated_or_inserted_contents, tab_id,
        wakes_at_pinned_url ? 0.0 : persisted->scroll_x,
        wakes_at_pinned_url ? 0.0 : persisted->scroll_y);

    if (persisted->is_pinned && persisted->role_type == "pinned") {
      TabStripModel* model = browser_->GetTabStripModel();
      int new_idx = model->GetIndexOfWebContents(params.navigated_or_inserted_contents);
      if (new_idx != TabStripModel::kNoTab && !model->IsTabPinned(new_idx)) {
        model->SetTabPinned(new_idx, true);
      }
    }
  }
  const double total_ms = maho_perf_t.Elapsed().InMillisecondsF();
  DVLOG(1) << "[MAHO_PERF] WakeSuspendedTab BREAKDOWN"
            << " query=" << query_ms << "ms"
            << " navigate=" << navigate_ms << "ms"
            << " total=" << total_ms << "ms";
  return params.navigated_or_inserted_contents;
}

bool MahoSidebarTabListView::CreateSidebarTwoPaneSplit(
    const std::string& source_tab_id,
    const std::string& target_tab_id,
    bool source_before_target) {
  if (!browser_ || source_tab_id.empty() || target_tab_id.empty() ||
      source_tab_id == target_tab_id) {
    return false;
  }

  TabStripModel* model = browser_->GetTabStripModel();
  if (!model) {
    return false;
  }

  const auto contents_for_id = [this, model](const std::string& tab_id) {
    const int index = ResolveTabStripIndexForTabId(tab_id);
    return model->ContainsIndex(index) ? model->GetWebContentsAt(index)
                                       : nullptr;
  };
  content::WebContents* source_contents = contents_for_id(source_tab_id);
  content::WebContents* target_contents = contents_for_id(target_tab_id);

  if (!source_contents) {
    source_contents = WakeSuspendedTab(source_tab_id);
  }
  if (!target_contents) {
    target_contents = WakeSuspendedTab(target_tab_id);
  }
  if (!source_contents || !target_contents ||
      source_contents == target_contents) {
    return false;
  }

  const auto stamp_stable_id = [](content::WebContents* contents,
                                  const std::string& stable_id) {
    auto* helper = MahoTabIdHelper::FromWebContents(contents);
    if (helper && helper->stable_tab_id() != stable_id) {
      helper->SetRestoredTabId(stable_id);
    }
  };
  stamp_stable_id(source_contents, source_tab_id);
  stamp_stable_id(target_contents, target_tab_id);

  int source_index = model->GetIndexOfWebContents(source_contents);
  int target_index = model->GetIndexOfWebContents(target_contents);
  if (!model->ContainsIndex(source_index) ||
      !model->ContainsIndex(target_index) ||
      !MahoSplitViewController::IsSplitDropEligible(
          /*is_multi_drag=*/false, /*source_is_target=*/false,
          model->GetSplitForTab(source_index).has_value(),
          model->GetSplitForTab(target_index).has_value())) {
    return false;
  }

  // Move source directly beside target. MoveWebContentsAt interprets the
  // destination in the post-removal strip, so account for whether source is
  // currently before target, then discard both indices immediately.
  const int move_destination = source_before_target
                                   ? (source_index < target_index
                                          ? target_index - 1
                                          : target_index)
                                   : (source_index < target_index
                                          ? target_index
                                          : target_index + 1);
  if (source_index != move_destination) {
    model->MoveWebContentsAt(source_index, move_destination,
                             /*select_after_move=*/false);
  }

  source_index = model->GetIndexOfWebContents(source_contents);
  target_index = model->GetIndexOfWebContents(target_contents);
  if (!model->ContainsIndex(source_index) ||
      !model->ContainsIndex(target_index) ||
      model->GetSplitForTab(source_index).has_value() ||
      model->GetSplitForTab(target_index).has_value()) {
    return false;
  }

  // AddToNewSplit uses the active tab as its implicit pivot. Activate target,
  // then resolve the partner index one final time after activation observers.
  model->ActivateTabAt(
      target_index,
      TabStripUserGestureDetails(
          TabStripUserGestureDetails::GestureType::kOther));
  source_index = model->GetIndexOfWebContents(source_contents);
  target_index = model->GetIndexOfWebContents(target_contents);
  if (!model->ContainsIndex(source_index) ||
      !model->ContainsIndex(target_index) || source_index == target_index ||
      model->active_index() != target_index) {
    return false;
  }

  split_tabs::SplitTabVisualData visual_data(
      split_tabs::SplitTabLayout::kSideBySide, 0.5);
  const split_tabs::SplitTabId split_id = model->AddToNewSplit(
      {source_index}, visual_data,
      split_tabs::SplitTabCreatedSource::kDragAndDropTab);

  split_tabs::SplitTabData* split_data = model->GetSplitData(split_id);
  if (!split_data ||
      split_data->visual_data()->split_layout() !=
          split_tabs::SplitTabLayout::kSideBySide) {
    return false;
  }

  std::vector<std::string> split_tab_ids;
  bool contains_source = false;
  bool contains_target = false;
  for (tabs::TabInterface* tab : split_data->ListTabs()) {
    content::WebContents* contents = tab ? tab->GetContents() : nullptr;
    if (contents == source_contents) {
      stamp_stable_id(contents, source_tab_id);
      contains_source = true;
    } else if (contents == target_contents) {
      stamp_stable_id(contents, target_tab_id);
      contains_target = true;
    }
    auto* helper = contents ? MahoTabIdHelper::FromWebContents(contents)
                            : nullptr;
    if (!helper || helper->stable_tab_id().empty()) {
      return false;
    }
    split_tab_ids.push_back(helper->stable_tab_id());
  }
  if (split_tab_ids.size() != 2u || !contains_source || !contains_target) {
    return false;
  }

  if (MahoIsCapabilityAllowed(browser_->GetProfile(),
                              MahoPrivateCapability::kPersistentSplit)) {
    std::string event_json = base::StringPrintf(
        "{\"kind\":\"create_split\",\"window_id\":\"%s\","
        "\"tab_ids\":[\"%s\",\"%s\"],\"orientation\":\"vertical\"}",
        std::to_string(browser_->GetSessionID().id()).c_str(),
        split_tab_ids[0].c_str(), split_tab_ids[1].c_str());
    if (auto* core = maho::GetCore()) {
      char* result = maho_core_handle_event(core, event_json.c_str());
      if (result) {
        maho_core_free_string(result);
      }
    }
  }
  return true;
}

void MahoSidebarTabListView::CloseTab(int tab_index) {
  TabStripModel* model = browser_ ? browser_->GetTabStripModel() : nullptr;
  if (!model || !model->ContainsIndex(tab_index)) {
    return;
  }

  if (model->count() == 1 && browser_ &&
      browser_->GetType() == BrowserWindowInterface::TYPE_NORMAL &&
      browser_->GetProfile() && !browser_->GetProfile()->IsOffTheRecord()) {
    content::WebContents* active_wc = model->GetWebContentsAt(tab_index);
    if (active_wc) {
      const GURL& url = active_wc->GetVisibleURL();
      const bool is_empty_ntp =
          (url.is_empty() || url == GURL(chrome::kChromeUINewTabURL) ||
           url == GURL("about:blank")) &&
          !active_wc->GetController().CanGoBack();
      if (is_empty_ntp) {
        return;
      }
    }
    chrome::NewTab(browser_, NewTabTypes::kNoUserAction);
  }

  if (content::WebContents* wc = model->GetWebContentsAt(tab_index)) {
    if (auto* helper = MahoTabIdHelper::FromWebContents(wc);
        helper && !helper->stable_tab_id().empty()) {
      auto* bridge = MahoSpaceProfileBridge::GetInstance();
      std::string active_space_id = bridge ? bridge->GetActiveSpaceId(browser_) : std::string();
      if (!active_space_id.empty()) {
        maho::DispatchShellEvent("close_tab", {
            {"tab_id", helper->stable_tab_id()},
            {"expected_space_id", active_space_id}
        });
      } else {
        // expected_space_id is intentionally omitted when active_space_id is empty
        // (bridge not yet initialized — startup window). Rust core's CloseTab handler
        // treats None as "no validation"; the close is processed against tab.space_id
        // directly, which is the correct fallback during startup before the bridge
        // has a space context.
        maho::DispatchShellEvent("close_tab", {{"tab_id", helper->stable_tab_id()}});
      }
    }
  }
  model->CloseWebContentsAt(tab_index, TabCloseTypes::CLOSE_USER_GESTURE |
                                           TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB);
}

void MahoSidebarTabListView::CloseTabById(const std::string& tab_id) {
  const int index = ResolveTabStripIndexForTabId(tab_id);
  if (index >= 0) {
    if (maho::IsCoreCloseProtectedTab(tab_id)) {
      // Stage 1 of the close-protected (pinned or favorite) two-stage close:
      // suspend in core and drop the live WebContents. The row then resolves to
      // index -1 on rebuild, flipping "−" to "X" and routing the next press to
      // real-delete below. StripObserver's IsCorePinnedTab guard suppresses the
      // kRemoved close_tab dispatch, so the core record survives as Suspended
      // (not deleted here).
      maho::DispatchShellEvent("suspend_tab", {{"tab_id", tab_id}});
      if (TabStripModel* model =
              browser_ ? browser_->GetTabStripModel() : nullptr;
          model && model->ContainsIndex(index)) {
        model->CloseWebContentsAt(index, TabCloseTypes::CLOSE_USER_GESTURE);
      }
    } else {
      CloseTab(index);
    }
  } else {
    // Two-stage close for a close-protected (pinned/favorite) tab with no live
    // WebContents (index < 0): a core-LIVE tab suspends (stage 1); only a
    // core-SUSPENDED tab is really deleted (stage 2). Keying stage 2 on core
    // suspend state (not index) prevents destroying a lazily-restored live tab.
    // Absent from last_model_ defaults to the non-destructive suspend path.
    if (maho::IsCoreCloseProtectedTab(tab_id) && !CoreSuspendedForTab(tab_id)) {
      maho::DispatchShellEvent("suspend_tab", {{"tab_id", tab_id}});
      return;
    }
    auto* bridge = MahoSpaceProfileBridge::GetInstance();
    std::string active_space_id = bridge ? bridge->GetActiveSpaceId(browser_) : std::string();
    if (!active_space_id.empty()) {
      maho::DispatchShellEvent("close_tab", {
          {"tab_id", tab_id},
          {"expected_space_id", active_space_id}
      });
    } else {
      // expected_space_id is intentionally omitted when active_space_id is empty
      // (bridge not yet initialized — startup window). Rust core's CloseTab handler
      // treats None as "no validation"; the close is processed against tab.space_id
      // directly, which is the correct fallback during startup before the bridge
      // has a space context.
      maho::DispatchShellEvent("close_tab", {{"tab_id", tab_id}});
    }
  }
}

void MahoSidebarTabListView::MuteTabById(const std::string& tab_id) {
  const int index = ResolveTabStripIndexForTabId(tab_id);
  if (index >= 0) {
    TabStripModel* model = browser_->GetTabStripModel();
    if (model) {
      content::WebContents* contents = model->GetWebContentsAt(index);
      if (contents) {
        ::SetTabAudioMuted(contents, !contents->IsAudioMuted(),
                                 TabMutedReason::kAudioIndicator, std::string());
      }
    }
  }
}

int MahoSidebarTabListView::ResolveTabStripIndexForTabId(
    const std::string& tab_id) const {
  if (tab_id.empty()) {
    return -1;
  }
  TabStripModel* strip = browser_ ? browser_->GetTabStripModel() : nullptr;
  // P3: O(1) cached lookup eliminates O(N) strip scan per hover event;
  // N=870+ tabs, called multiple times per mouse enter/exit cycle.
  auto it = tab_id_to_index_.find(tab_id);
  if (it != tab_id_to_index_.end()) {
    // Defensive validation: index-only strip mutations (split-drop
    // MoveWebContentsAt, AddToNewSplit, a tab closed before the next rebuild)
    // shift indices without a fingerprint change, so the cache can point at a
    // neighbor. A blind hit here would activate — or worse, CloseTabById the
    // wrong tab and delete its core record. Trust the entry only if the live
    // WebContents at that index still carries this tab_id.
    if (strip && it->second >= 0 && it->second < strip->count()) {
      auto* contents = strip->GetWebContentsAt(it->second);
      auto* helper =
          contents ? MahoTabIdHelper::FromWebContents(contents) : nullptr;
      if (helper && helper->stable_tab_id() == tab_id) {
        return it->second;
      }
    }
    // Stale: fall through to the scan and refresh the entry below.
  }
  // Linear-scan fallback for a cache miss or a stale entry the validation
  // above rejected. On success the corrected index is written back.
  if (!strip) {
    return -1;
  }
  for (int i = 0; i < strip->count(); ++i) {
    auto* contents = strip->GetWebContentsAt(i);
    if (!contents) {
      continue;
    }
    auto* helper = MahoTabIdHelper::FromWebContents(contents);
    if (helper && helper->stable_tab_id() == tab_id) {
      tab_id_to_index_[tab_id] = i;
      return i;
    }
  }
  return -1;
}

void MahoSidebarTabListView::RebuildTabIndexCache() {
  tab_id_to_index_.clear();
  TabStripModel* id_strip = browser_ ? browser_->GetTabStripModel() : nullptr;
  if (!id_strip) {
    return;
  }
  tab_id_to_index_.reserve(id_strip->count());
  for (int i = 0; i < id_strip->count(); ++i) {
    auto* contents = id_strip->GetWebContentsAt(i);
    if (!contents) {
      continue;
    }
    auto* helper = MahoTabIdHelper::FromWebContents(contents);
    if (helper && !helper->stable_tab_id().empty()) {
      tab_id_to_index_[helper->stable_tab_id()] = i;
    }
  }
}

bool MahoSidebarTabListView::IsTabRowRealizedAt(int strip_index) const {
  TabStripModel* strip = browser_ ? browser_->GetTabStripModel() : nullptr;
  if (!strip || !strip->ContainsIndex(strip_index)) {
    return false;
  }
  content::WebContents* contents = strip->GetWebContentsAt(strip_index);
  if (!contents) {
    return false;
  }
  auto* helper = MahoTabIdHelper::FromWebContents(contents);
  if (!helper) {
    return false;
  }
  const std::string tab_id = helper->stable_tab_id();
  if (tab_id.empty()) {
    return false;
  }
  return tab_id_to_row_view_.contains(tab_id);
}

void MahoSidebarTabListView::InvalidateTabIndexCache() {
  tab_id_to_index_.clear();
}

void MahoSidebarTabListView::OpenNewTab(const ui::Event& event) {
  if (!browser_) {
    return;
  }
  if (action_placeholder_active_) {
    return;
  }

  // Open the command palette in new-tab mode. The tab is only created once the
  // user enters a URL; dismissing without input creates no tab.
  auto* browser_view = BrowserView::GetBrowserViewForBrowser(browser_);
  if (!browser_view) {
    return;
  }
  for (views::View* child : browser_view->children()) {
    auto* container =
        views::AsViewClass<MahoSidebarContainerView>(child);
    if (container) {
      container->ShowCommandOverlayForNewTab();
      return;
    }
  }
}

namespace {
enum ClearCommandId : int {
  kClearLastHour = 100,
  kClearToday = 101,
  kClearThisWeek = 102,
  kClearThisMonth = 103,
  kClearOlderThan30Days = 104,
};

bool IsClearCommand(int command_id) {
  switch (command_id) {
    case kClearLastHour:
    case kClearToday:
    case kClearThisWeek:
    case kClearThisMonth:
    case kClearOlderThan30Days:
      return true;
  }
  return false;
}

int64_t ComputeClearCutoffSeconds(int command_id) {
  const base::Time now = base::Time::Now();
  switch (command_id) {
    case kClearLastHour:
      return (now - base::Hours(1)).InSecondsFSinceUnixEpoch();
    case kClearToday:
      return now.LocalMidnight().InSecondsFSinceUnixEpoch();
    case kClearThisWeek: {
      base::Time::Exploded exploded;
      now.LocalExplode(&exploded);
      // day_of_week: Sunday=0..Saturday=6. Treat Monday as week start.
      int days_since_monday = (exploded.day_of_week + 6) % 7;
      return (now.LocalMidnight() - base::Days(days_since_monday))
          .InSecondsFSinceUnixEpoch();
    }
    case kClearThisMonth: {
      base::Time::Exploded exploded;
      now.LocalExplode(&exploded);
      exploded.day_of_month = 1;
      exploded.hour = 0;
      exploded.minute = 0;
      exploded.second = 0;
      exploded.millisecond = 0;
      base::Time start_of_month;
      if (base::Time::FromLocalExploded(exploded, &start_of_month)) {
        return start_of_month.InSecondsFSinceUnixEpoch();
      }
      return now.LocalMidnight().InSecondsFSinceUnixEpoch();
    }
    case kClearOlderThan30Days:
      return (now - base::Days(30)).InSecondsFSinceUnixEpoch();
  }
  return 0;
}
}  // namespace

std::unique_ptr<views::View> MahoSidebarTabListView::CreateActionRow() {
  constexpr int kActionRowHeightDp = 42;
  auto action_row = std::make_unique<MahoSidebarActionRowView>();
  action_row_ = action_row.get();

  auto button_surface = std::make_unique<views::BoxLayoutView>();
  button_surface->SetOrientation(views::BoxLayout::Orientation::kHorizontal);
  button_surface->SetCrossAxisAlignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  button_surface->SetBetweenChildSpacing(kInlineActionsSpacingDp);
  button_surface->SetProperty(views::kMarginsKey, kNewTabRowMargins);
  button_surface->SetPaintToLayer();
  button_surface->layer()->SetFillsBoundsOpaquely(false);

  action_buttons_ = button_surface.get();
  action_dividers_.clear();

  auto new_tab = std::make_unique<MahoInlineActionButton>(
      base::BindRepeating(&MahoSidebarTabListView::OpenNewTab,
                          weak_factory_.GetWeakPtr()),
      u"+ New Tab", kRowCornerRadiusDp);
  // Text/glyph colors for every state come from ApplyActionRowPalette().
  new_tab->SetTextSubpixelRenderingEnabled(false);
  new_tab->SetBorder(views::CreateEmptyBorder(kInlineRowInsets));
  new_tab->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  new_tab->SetMinSize(gfx::Size(0, kActionRowHeightDp));
  new_tab->SetInstallFocusRingOnFocus(false);
  auto* new_tab_ptr = button_surface->AddChildView(std::move(new_tab));
  button_surface->SetFlexForView(new_tab_ptr, 1);
  new_tab_button_ = new_tab_ptr;

  if (!private_mode_) {
    auto divider1 = std::make_unique<views::View>();
    divider1->SetPreferredSize(gfx::Size(1, 20));
    divider1->SetBackground(views::CreateSolidBackground(
        ResolveSidebarPaletteColor(palette_, this, palette_.outline,
                                   ui::kColorSysNeutralOutline)));
    action_dividers_.push_back(button_surface->AddChildView(std::move(divider1)));

    auto tidy = std::make_unique<MahoInlineActionButton>(
        base::BindRepeating(&MahoSidebarTabListView::OnTidyPressed,
                            weak_factory_.GetWeakPtr()),
        u"Tidy", kRowCornerRadiusDp);
    tidy->SetImageLabelSpacing(kInlineActionsSpacingDp);
    tidy->SetTextSubpixelRenderingEnabled(false);
    tidy->SetBorder(views::CreateEmptyBorder(kInlineRowInsets));
    tidy->SetHorizontalAlignment(gfx::ALIGN_CENTER);
    tidy->SetMinSize(gfx::Size(0, kActionRowHeightDp));
    tidy->SetTooltipText(u"Tidy tabs with AI");
    tidy->SetInstallFocusRingOnFocus(false);
    auto* tidy_ptr = button_surface->AddChildView(std::move(tidy));
    button_surface->SetFlexForView(tidy_ptr, 1);
    tidy_button_ = tidy_ptr;

    auto divider2 = std::make_unique<views::View>();
    divider2->SetPreferredSize(gfx::Size(1, 20));
    divider2->SetBackground(views::CreateSolidBackground(
        ResolveSidebarPaletteColor(palette_, this, palette_.outline,
                                   ui::kColorSysNeutralOutline)));
    action_dividers_.push_back(button_surface->AddChildView(std::move(divider2)));

    auto clear = std::make_unique<MahoInlineActionButton>(
        base::BindRepeating(&MahoSidebarTabListView::OnClearPressed,
                            weak_factory_.GetWeakPtr()),
        u"Clear", kRowCornerRadiusDp);
    clear->SetImageLabelSpacing(kInlineActionsSpacingDp);
    clear->SetTextSubpixelRenderingEnabled(false);
    clear->SetBorder(views::CreateEmptyBorder(kInlineRowInsets));
    clear->SetHorizontalAlignment(gfx::ALIGN_CENTER);
    clear->SetMinSize(gfx::Size(0, kActionRowHeightDp));
    clear->SetTooltipText(u"Close old tabs");
    clear->SetInstallFocusRingOnFocus(false);
    auto* clear_ptr = button_surface->AddChildView(std::move(clear));
    button_surface->SetFlexForView(clear_ptr, 1);
    clear_button_ = clear_ptr;
  } else {
    tidy_button_ = nullptr;
    clear_button_ = nullptr;
  }

  auto* button_surface_ptr =
      action_row->AddChildView(std::move(button_surface));

  if (!private_mode_) {
    auto processing_view =
        std::make_unique<MahoSidebarProcessingPlaceholderView>();
    processing_view->SetVisible(false);
    processing_view->SetButtonSurface(button_surface_ptr);
    processing_view->SetSidebarPalette(palette_);
    action_processing_view_ = processing_view.get();
    action_row->AddChildView(std::move(processing_view));
  } else {
    action_processing_view_ = nullptr;
  }

  if (action_placeholder_active_) {
    ApplyActionPlaceholderButtonState();
  }

  ApplyActionRowPalette();

  return action_row;
}

void MahoSidebarTabListView::ApplyActionRowPalette() {
  const SkColor secondary_text = ResolveSidebarPaletteColor(
      palette_, this, palette_.secondary_text, ui::kColorSysOnSurfaceSubtle);
  const SkColor neutral_glyph = ResolveSidebarPaletteColor(
      palette_, this, palette_.neutral_glyph, ui::kColorSysOnSurfaceSubtle);
  const auto apply_button = [secondary_text, this](views::View* button) {
    if (auto* action_button =
            views::AsViewClass<MahoInlineActionButton>(button)) {
      action_button->SetSidebarPalette(palette_);
      action_button->SetEnabledTextColors(secondary_text);
      // macOS paints STATE_DISABLED for inactive windows; keep it on-palette
      // so the label does not flip color when the window loses focus.
      action_button->SetTextColor(views::Button::STATE_DISABLED,
                                  secondary_text);
    }
  };
  apply_button(new_tab_button_);
  apply_button(tidy_button_);
  apply_button(clear_button_);
  const auto apply_glyph = [neutral_glyph](views::View* button,
                                           const gfx::VectorIcon& icon) {
    if (auto* action_button =
            views::AsViewClass<MahoInlineActionButton>(button)) {
      const ui::ImageModel image = ui::ImageModel::FromVectorIcon(
          icon, neutral_glyph, kInlineActionIconSizeDp);
      for (views::Button::ButtonState state :
           {views::Button::STATE_NORMAL, views::Button::STATE_HOVERED,
            views::Button::STATE_PRESSED, views::Button::STATE_DISABLED}) {
        action_button->SetImageModel(state, image);
      }
    }
  };
  apply_glyph(tidy_button_, maho_lucide_icons::kWandSparklesIcon);
  apply_glyph(clear_button_, maho_lucide_icons::kArrowDownToLineIcon);
  for (views::View* divider : action_dividers_) {
    if (divider) {
      divider->SetBackground(views::CreateSolidBackground(
          ResolveSidebarPaletteColor(palette_, this, palette_.outline,
                                     ui::kColorSysNeutralOutline)));
    }
  }
}

void MahoSidebarTabListView::OnTidyPressed(const ui::Event& event) {
  if (!browser_) {
    LOG(INFO) << "[maho-tidy] No browser";
    return;
  }
  if (action_placeholder_active_) {
    return;
  }
  LOG(INFO) << "[maho-tidy] starting orchestrator";

  StartActionProcessing(MahoSidebarProcessingPlaceholderView::Mode::kLoop,
                        tidy_button_);

  content::WebContents* active_wc =
      browser_->GetTabStripModel()
          ? browser_->GetTabStripModel()->GetActiveWebContents()
          : nullptr;
  auto token =
      std::make_shared<MahoPrivateContextToken>(browser_, active_wc);
  maho::MahoTabTidyOrchestrator::RunOneShot(
      browser_,
      base::BindOnce(&MahoSidebarTabListView::OnTidyFinished,
                     weak_factory_.GetWeakPtr()),
      base::BindRepeating(
          [](std::shared_ptr<MahoPrivateContextToken> t) {
            return t->Revalidate(MahoPrivateCapability::kAI);
          },
          std::move(token)));
}

void MahoSidebarTabListView::OnTidyFinished(int folder_count, const std::string& error_message) {
  LOG(INFO) << "[maho-tidy] result folder_count=" << folder_count
            << " error='" << error_message << "'";

  const bool tidy_success = folder_count > 0 || error_message.empty();
  base::ScopedClosureRunner resolve_on_exit;
  if (action_placeholder_active_) {
    resolve_on_exit.ReplaceClosure(base::BindOnce(
        [](base::WeakPtr<MahoSidebarTabListView> self, bool success) {
          if (!self) {
            return;
          }
          base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
              FROM_HERE,
              base::BindOnce(
                  &MahoSidebarTabListView::ResolveActionPlaceholderDeferred,
                  self, success));
        },
        weak_factory_.GetWeakPtr(), tidy_success));
  }

  if (folder_count == 0 && !error_message.empty()) {
    auto* overlay = MahoNotificationOverlay::GetOrCreateForBrowser(browser_);
    if (overlay) {
      std::u16string title = u"Tab Tidy";
      std::u16string body = base::UTF8ToUTF16(error_message);
      overlay->Show(title, body);
    }
  }
  // Post-Tidy cleanup: sweep any empty folders in the active space. Runs
  // regardless of Rust-side auto_delete setting because the Rust sweep path
  // in ApplyTidyTabs has proven unreliable — it either does not delete the
  // folders from the persisted state or the sidebar view fails to reflect
  // the deletion. Dispatching delete_folder shell events for each currently
  // empty folder in this space is a robust C++-side fallback: Rust receives
  // the events one-by-one and each triggers the usual NotifyChanged refresh
  // pipeline, so the sidebar can no longer show a stale folder that has
  // zero children after Tidy.
  auto* bridge = MahoSpaceProfileBridge::GetInstance();
  const std::string space_id = bridge ? bridge->GetActiveSpaceId(browser_) : std::string();
  if (space_id.empty()) {
    return;
  }
  MahoCore* core = maho::GetCore();
  if (!core) {
    return;
  }
  const std::string space_id_json = ::base::GetQuotedJSONString(space_id);
  char* raw = maho_core_get_sidebar_state_v2(core, space_id_json.c_str());
  if (!raw) {
    return;
  }
  std::string state_json(raw);
  maho_string_free(raw);
  std::optional<base::Value> parsed = base::JSONReader::Read(state_json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return;
  }
  const base::ListValue* tree = parsed->GetDict().FindList("tree");
  if (!tree) {
    return;
  }
  std::vector<std::string> empty_folder_ids;
  for (const auto& node : *tree) {
    const auto* dict = node.GetIfDict();
    if (!dict) {
      continue;
    }
    const std::string* kind = dict->FindString("kind");
    if (!kind || *kind != "folder") {
      continue;
    }
    const std::string* fid = dict->FindString("id");
    const base::ListValue* children = dict->FindList("children");
    if (!fid || fid->empty()) {
      continue;
    }
    if (!children || children->empty()) {
      empty_folder_ids.push_back(*fid);
    }
  }
  int cleaned = 0;
  for (const std::string& fid : empty_folder_ids) {
    maho::DispatchShellEvent("delete_folder", {
        {"space_id", space_id},
        {"folder_id", fid},
    });
    cleaned++;
  }
  LOG(INFO) << "[maho-tidy] post-tidy empty-folder cleanup: removed " << cleaned;
}

void MahoSidebarTabListView::OnClearPressed(const ui::Event& event) {
  if (action_placeholder_active_) {
    return;
  }
  if (!clear_button_) {
    return;
  }
  clear_menu_model_ = std::make_unique<ui::SimpleMenuModel>(this);
  clear_menu_model_->AddItem(kClearLastHour, u"Last hour");
  clear_menu_model_->AddItem(kClearToday, u"Today");
  clear_menu_model_->AddItem(kClearThisWeek, u"This week");
  clear_menu_model_->AddItem(kClearThisMonth, u"This month");
  clear_menu_model_->AddItem(kClearOlderThan30Days, u"Older than 30 days");

  clear_menu_runner_ = std::make_unique<views::MenuRunner>(
      clear_menu_model_.get(), views::MenuRunner::HAS_MNEMONICS);

  views::Widget* widget = clear_button_->GetWidget();
  if (!widget) {
    return;
  }
  clear_menu_runner_->RunMenuAt(widget,
                                /*menu_button_controller=*/nullptr,
                                clear_button_->GetBoundsInScreen(),
                                views::MenuAnchorPosition::kBubbleBottomRight,
                                ui::mojom::MenuSourceType::kMouse);
}

void MahoSidebarTabListView::ExecuteCommand(int command_id, int event_flags) {
  if (!IsClearCommand(command_id)) {
    return;
  }
  if (action_placeholder_active_) {
    return;
  }
  StartActionProcessing(MahoSidebarProcessingPlaceholderView::Mode::kLoop,
                        clear_button_);
  bool succeeded = false;
  std::u16string result_message = u"Unable to clear tabs. Please try again.";
  auto weak = weak_factory_.GetWeakPtr();
  base::ScopedClosureRunner resolve_on_exit(base::BindOnce(
      [](base::WeakPtr<MahoSidebarTabListView> view, const bool& result_success,
         const std::u16string& result_text) {
        if (!view) {
          return;
        }
        base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
            FROM_HERE,
            base::BindOnce(
                [](base::WeakPtr<MahoSidebarTabListView> self, bool success,
                   std::u16string message) {
                  if (!self) {
                    return;
                  }
                  self->ResolveActionPlaceholderDeferred(success);
                  if (self->browser_) {
                    auto* overlay =
                        MahoNotificationOverlay::GetOrCreateForBrowser(
                            self->browser_);
                    if (overlay) {
                      overlay->Show(u"Clear Tabs", message);
                    }
                  }
                },
                view, result_success, result_text));
      },
      weak, std::cref(succeeded), std::cref(result_message)));

  MahoCore* core = maho::GetCore();
  if (!core) {
    LOG(INFO) << "[maho-clear] No core available";
    return;
  }
  TabStripModel* model = browser_ ? browser_->GetTabStripModel() : nullptr;
  if (!model) {
    LOG(INFO) << "[maho-clear] No tab strip model";
    return;
  }
  const int64_t cutoff_ts = ComputeClearCutoffSeconds(command_id);
  if (cutoff_ts <= 0) {
    LOG(INFO) << "[maho-clear] Invalid cutoff";
    return;
  }

  auto* bridge = MahoSpaceProfileBridge::GetInstance();
  std::string space_id = bridge ? bridge->GetActiveSpaceId(browser_) : std::string();
  if (space_id.empty()) {
    return;
  }
  char* ids_json = maho_core_find_tabs_older_than(core, cutoff_ts, space_id.c_str());
  if (!ids_json) {
    LOG(INFO) << "[maho-clear] FFI returned null";
    return;
  }
  std::string json_str(ids_json);
  maho_string_free(ids_json);
  LOG(INFO) << "[maho-clear] cmd=" << command_id << " cutoff_ts=" << cutoff_ts
            << " candidates_json=" << json_str;

  std::optional<base::Value> parsed =
      base::JSONReader::Read(json_str, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    LOG(INFO) << "[maho-clear] JSON parse failed or not a list";
    return;
  }
  if (parsed->GetList().empty()) {
    result_message = u"No older tabs to clear in this space.";
    return;
  }

  std::vector<int> indices_to_close;
  std::vector<std::string> unresolved_tab_ids;
  indices_to_close.reserve(parsed->GetList().size());
  for (const auto& v : parsed->GetList()) {
    if (!v.is_string()) {
      continue;
    }
    const std::string& tab_id = v.GetString();
    const int idx = ResolveTabStripIndexForTabId(tab_id);
    if (idx >= 0 && model->ContainsIndex(idx)) {
      indices_to_close.push_back(idx);
    } else {
      unresolved_tab_ids.push_back(tab_id);
    }
  }
  LOG(INFO) << "[maho-clear] candidates=" << parsed->GetList().size()
            << " resolved=" << indices_to_close.size()
            << " unresolved=" << unresolved_tab_ids.size();
  std::sort(indices_to_close.rbegin(), indices_to_close.rend());
  int closed_count = 0;
  for (int idx : indices_to_close) {
    if (!model->ContainsIndex(idx)) {
      continue;
    }
    const int previous_count = model->count();
    model->CloseWebContentsAt(
        idx,
        TabCloseTypes::CLOSE_USER_GESTURE |
            TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB);
    if (!weak) {
      return;
    }
    closed_count += previous_count - model->count();
  }
  // Sidebar-only tabs (not backed by a live WebContents in TabStripModel)
  // must be removed via the shell-event path so Rust tab table + sidebar
  // stay in sync. Without this, Clear silently skips 100% of stale tabs
  // for users who keep their tabs in the sidebar without opening them.
  for (const std::string& tab_id : unresolved_tab_ids) {
    maho::DispatchShellEvent("close_tab", {{"tab_id", tab_id}});
    if (!weak) {
      return;
    }
    char* remaining = maho_core_get_tab_snapshot_by_id(core, tab_id.c_str());
    if (!remaining) {
      ++closed_count;
    } else {
      maho_string_free(remaining);
    }
  }
  succeeded = closed_count > 0;
  result_message = succeeded
                       ? u"Closed " + base::NumberToString16(closed_count) +
                             (closed_count == 1 ? u" tab." : u" tabs.")
                       : u"No tabs were closed.";
}

void MahoSidebarTabListView::ToggleFolderExpanded(
    const std::string& folder_id) {
  if (folder_hover_controller_) {
    folder_hover_controller_->HideImmediately();
  }
  // DispatchShellEvent triggers MahoSpaceProfileBridge::NotifyChanged,
  // which schedules an asynchronous ThreadPool round-trip ending in our
  // own Update() → RebuildRows(). For 870+ tabs that second pass takes
  // ~1s and is wasted work because we are about to do an optimistic
  // local rebuild below with the same effective state. We arm suppression
  // of that single matching state-push below, once the optimistic flip has
  // produced the fingerprint the push will carry.
  if (!active_space_id_.empty() && !folder_id.empty()) {
    DispatchShellEvent("toggle_folder_expanded", {
        {"space_id", active_space_id_},
        {"folder_id", folder_id}});
  }

  if (has_last_model_ && !folder_id.empty()) {
    bool new_expanded_state = false;
    auto toggle_in_tree = [&folder_id, &new_expanded_state](
        const auto& self,
        std::vector<SidebarTreeNode>& nodes) -> bool {
      for (auto& node : nodes) {
        if (node.kind == SidebarNodeKind::kFolder &&
            node.folder_id == folder_id) {
          node.is_expanded = !node.is_expanded;
          new_expanded_state = node.is_expanded;
          return true;
        }
        if (!node.children.empty() && self(self, node.children)) {
          return true;
        }
      }
      return false;
    };

    const bool found =
        toggle_in_tree(toggle_in_tree, last_model_.pinned_tree) ||
        toggle_in_tree(toggle_in_tree, last_model_.normal_tree);

    if (found) {
      rebuild_deferred_ = false;
      deferred_model_ = {};
      deferred_browser_ = nullptr;
      // last_model_ now carries the toggled expansion state; the async push
      // reflecting this toggle will fingerprint identically, so arm exactly
      // that fingerprint for one-shot suppression in Update().
      suppress_push_fingerprint_ = ComputeModelFingerprint(last_model_);
      RebuildRows(last_model_, browser_);
      return;
    }
  }

  FlushDeferredRebuild();
}

void MahoSidebarTabListView::UnregisterRowsRecursive(views::View* view) {
  if (!view) {
    return;
  }
  if (auto* row = views::AsViewClass<SidebarTabRowView>(view)) {
    if (!row->tab_id().empty()) {
      auto it = tab_id_to_row_view_.find(row->tab_id());
      if (it != tab_id_to_row_view_.end() && it->second == row) {
        tab_id_to_row_view_.erase(it);
      }
    }
  } else if (auto* folder = views::AsViewClass<SidebarFolderRowView>(view)) {
    auto it = folder_rows_by_id_.find(folder->folder_id());
    if (it != folder_rows_by_id_.end() && it->second == folder) {
      folder_rows_by_id_.erase(it);
    }
  }
  // split_id_to_container_ and splits_awaiting_anim_ hold raw_ptr/id trackers
  // for a split container that AnimateAwaitingSplits() later dereferences; they
  // must be dropped before RemoveChildViewT frees this view, or the debounced
  // timer derefs a dangling raw_ptr (its non-null check won't catch a freed
  // pointer).
  for (auto it = split_id_to_container_.begin();
       it != split_id_to_container_.end();) {
    if (it->second == view) {
      splits_awaiting_anim_.erase(it->first);
      it = split_id_to_container_.erase(it);
    } else {
      ++it;
    }
  }
  for (views::View* child : view->children()) {
    UnregisterRowsRecursive(child);
  }
}

void MahoSidebarTabListView::CommitTabTitleChange(
    const std::string& tab_id,
    const std::u16string& new_title) {
  local_custom_titles_[tab_id] = new_title;
  auto* core = maho::GetCore();
  if (!core || tab_id.empty()) {
    return;
  }

  std::string utf8_title = base::UTF16ToUTF8(new_title);
  maho_core_set_tab_custom_title(core, tab_id.c_str(),
                                 utf8_title.empty() ? nullptr
                                                    : utf8_title.c_str());
  SidebarCacheInvalidation invalidation;
  invalidation.fragments = SidebarCoreFragment::kTreeBundle;
  InvalidateSidebarCoreCache(invalidation);
}

void MahoSidebarTabListView::CommitFolderNameChange(
    const std::string& folder_id,
    const std::u16string& new_name) {
  if (active_space_id_.empty() || folder_id.empty()) {
    return;
  }
  DispatchShellEvent("rename_folder",
                     {{"space_id", active_space_id_},
                      {"folder_id", folder_id},
                      {"name", base::UTF16ToUTF8(new_name)}});
}

void MahoSidebarTabListView::SetPendingFolderEdit(
    const std::string& folder_id) {
  pending_folder_edit_id_ = folder_id;
}

std::u16string MahoSidebarTabListView::ResolveTabDisplayText(
    const SidebarTreeNode& node) const {
  auto local_it = local_custom_titles_.find(node.tab_id);
  if (local_it != local_custom_titles_.end()) {
    return local_it->second;
  }
  const std::u16string resolved = MahoDisplayPolicy::ResolveTabDisplayText(
      GURL(node.url), node.title, node.custom_title);
  if (!resolved.empty()) {
    return resolved;
  }
  if (!node.host.empty()) {
    return node.host;
  }
  return node.title;
}

void MahoSidebarTabListView::SetLastFavoritesModel(
    const MahoSidebarFavoritesModel& model) {
  last_favorites_model_ = model;
}

void MahoSidebarTabListView::PruneLocalTitleCache(
    const MahoSidebarTabListModel& model) {
  std::unordered_set<std::string> live_tab_ids;
  auto collect_tree = [&](const auto& self,
                          const std::vector<SidebarTreeNode>& nodes) -> void {
    for (const auto& node : nodes) {
      if (node.kind == SidebarNodeKind::kTab) {
        live_tab_ids.insert(node.tab_id);
      } else {
        self(self, node.children);
      }
    }
  };
  collect_tree(collect_tree, model.pinned_tree);
  collect_tree(collect_tree, model.normal_tree);
  for (const auto& item : last_favorites_model_.items) {
    if (!item.tab_id.empty()) {
      live_tab_ids.insert(item.tab_id);
    }
  }

  for (auto it = local_custom_titles_.begin(); it != local_custom_titles_.end();) {
    if (!live_tab_ids.contains(it->first)) {
      it = local_custom_titles_.erase(it);
    } else {
      ++it;
    }
  }
}

void MahoSidebarTabListView::HandleTabRowHoverStart(views::View* anchor_view,
                                                    int tab_index) {
  if (tab_preview_controller_) {
    tab_preview_controller_->ShowPreview(anchor_view, tab_index);
  }
}

void MahoSidebarTabListView::HandleTabRowHoverEnd() {
  if (tab_preview_controller_) {
    tab_preview_controller_->HidePreview();
  }
}

void MahoSidebarTabListView::OnFaviconLoaded(
    const std::string& tab_id,
    const GURL& requested_url,
    uint64_t generation,
    const favicon_base::FaviconImageResult& result) {
  if (!tab_rows_ || tab_id.empty() ||
      !IsCurrentRowFaviconRequest(tab_id, requested_url, generation)) {
    return;
  }
  auto row_it = tab_id_to_row_view_.find(tab_id);
  if (row_it == tab_id_to_row_view_.end() || !row_it->second ||
      row_it->second->tab_id() != tab_id) {
    return;
  }

  if (result.image.IsEmpty()) {
    GURL origin = requested_url.DeprecatedGetOriginAsURL();
    if (browser_ && origin.is_valid() && origin != requested_url) {
      favicon::FaviconService* favicon_service =
          FaviconServiceFactory::GetForProfile(
              browser_->GetProfile(), ServiceAccessType::EXPLICIT_ACCESS);
      if (favicon_service) {
        const uint64_t origin_generation =
            UpdateRowFaviconRequest(tab_id, origin);
        favicon_service->GetFaviconImageForPageURL(
            origin,
            base::BindOnce(&MahoSidebarTabListView::OnFaviconLoaded,
                           weak_factory_.GetWeakPtr(), tab_id, origin,
                           origin_generation),
            &favicon_task_tracker_);
      }
    }
    return;
  }
  row_it->second->SetFavicon(ui::ImageModel::FromImage(result.image));
}

void MahoSidebarTabListView::RefreshRowFaviconRequestsFromModel(
    const MahoSidebarTabListModel& model) {
  std::function<void(const std::vector<SidebarTreeNode>&)> refresh_tree;
  refresh_tree = [&](const std::vector<SidebarTreeNode>& nodes) {
    for (const SidebarTreeNode& node : nodes) {
      if (node.kind == SidebarNodeKind::kTab && !node.tab_id.empty()) {
        GURL page_url(node.url);
        UpdateRowFaviconRequest(node.tab_id,
                                page_url.is_valid() ? page_url : GURL());
      }
      if (!node.children.empty()) {
        refresh_tree(node.children);
      }
    }
  };

  refresh_tree(model.pinned_tree);
  refresh_tree(model.normal_tree);
}

uint64_t MahoSidebarTabListView::UpdateRowFaviconRequest(
    const std::string& tab_id,
    const GURL& requested_url) {
  if (tab_id.empty()) {
    return 0;
  }
  RowFaviconRequestState& state = row_favicon_requests_[tab_id];
  if (state.requested_url != requested_url) {
    state.requested_url = requested_url;
    ++state.generation;
  }
  return state.generation;
}

bool MahoSidebarTabListView::IsCurrentRowFaviconRequest(
    const std::string& tab_id,
    const GURL& requested_url,
    uint64_t generation) const {
  const auto state_it = row_favicon_requests_.find(tab_id);
  return state_it != row_favicon_requests_.end() &&
         state_it->second.requested_url == requested_url &&
         state_it->second.generation == generation;
}

std::string MahoSidebarTabListView::FindCoreTabIdByWebContents(
    content::WebContents* contents) const {
  if (!contents) {
    return std::string();
  }
  auto* helper = MahoTabIdHelper::FromWebContents(contents);
  if (helper && !helper->stable_tab_id().empty()) {
    return helper->stable_tab_id();
  }
  return std::string();
}

void MahoSidebarTabListView::ApplyCosmeticRefresh() {
  if (!has_last_model_ || !browser_) {
    return;
  }
  TabStripModel* tab_strip_model = browser_->GetTabStripModel();
  if (!tab_strip_model) {
    return;
  }

  base::ElapsedTimer cosmetic_t;

  // Direct row-view update path: bypasses Update() because ComputeModelFingerprint
  // intentionally excludes title/URL (to skip RebuildRows on cosmetic events),
  // so the fingerprint would match and row labels would go stale otherwise.
  // Instead push new title/url straight to SidebarTabRowView via tab_id_to_row_view_.
  std::unordered_map<std::string, const SidebarTreeNode*> refreshed_nodes;
  std::function<void(std::vector<SidebarTreeNode>&)> overlay_tree;
  overlay_tree = [&](std::vector<SidebarTreeNode>& nodes) {
    for (auto& node : nodes) {
      if (node.kind == SidebarNodeKind::kTab && !node.tab_id.empty()) {
        auto index_it = tab_id_to_index_.find(node.tab_id);
        if (index_it != tab_id_to_index_.end() &&
            tab_strip_model->ContainsIndex(index_it->second)) {
          const int idx = index_it->second;
          content::WebContents* contents =
              tab_strip_model->GetWebContentsAt(idx);
          auto* helper =
              contents ? MahoTabIdHelper::FromWebContents(contents) : nullptr;
          if (helper && helper->stable_tab_id() == node.tab_id) {
            std::u16string new_title = contents->GetTitle();
            const GURL& url = contents->GetVisibleURL();
            std::string new_url = url.is_valid() ? url.spec() : std::string();
            std::u16string new_host = url.is_valid()
                ? base::UTF8ToUTF16(url.host()) : std::u16string();
            node.title = new_title;
            node.url = new_url;
            node.host = new_host;
            node.is_loading = contents->IsLoading();
            node.is_audible = contents->IsCurrentlyAudible();
            node.is_muted = contents->IsAudioMuted();
            node.is_active = (tab_strip_model->active_index() == idx);
            refreshed_nodes[node.tab_id] = &node;
            UpdateRowFaviconRequest(node.tab_id,
                                    url.is_valid() ? url : GURL());

            auto row_it = tab_id_to_row_view_.find(node.tab_id);
            if (row_it != tab_id_to_row_view_.end() && row_it->second) {
              std::u16string display_text =
                  new_title.empty() ? new_host : new_title;
              row_it->second->ApplyDisplayedTitle(display_text);
              row_it->second->UpdateAudioState(node.is_audible, node.is_muted);
              tabs::TabInterface* tab_iface =
                  tabs::TabInterface::GetFromContents(contents);
              TabUIHelper* tab_ui =
                  tab_iface ? TabUIHelper::From(tab_iface) : nullptr;
              ui::ImageModel live_favicon =
                  tab_ui ? tab_ui->GetFavicon() : ui::ImageModel();
              if (!live_favicon.IsEmpty()) {
                row_it->second->SetFavicon(std::move(live_favicon));
              }
            }
          }
        }
      }
      if (!node.children.empty()) {
        overlay_tree(node.children);
      }
    }
  };

  overlay_tree(last_model_.pinned_tree);
  overlay_tree(last_model_.normal_tree);

  auto refresh_projection = [&](std::vector<SidebarVisualRow>& projection) {
    for (SidebarVisualRow& row : projection) {
      switch (row.kind) {
        case SidebarVisualRowKind::kTab:
        case SidebarVisualRowKind::kCollapsedStickyTab: {
          auto it = refreshed_nodes.find(row.node.tab_id);
          if (it != refreshed_nodes.end()) {
            row.node = CopyNodeWithoutChildren(*it->second);
          }
          break;
        }
        case SidebarVisualRowKind::kSplitGroup:
          for (SidebarTreeNode& child : row.node.children) {
            if (child.kind != SidebarNodeKind::kTab || child.tab_id.empty()) {
              continue;
            }
            auto it = refreshed_nodes.find(child.tab_id);
            if (it != refreshed_nodes.end()) {
              child = *it->second;
            }
          }
          break;
        case SidebarVisualRowKind::kInsertionLane:
        case SidebarVisualRowKind::kFolder:
          break;
      }
    }
  };
  refresh_projection(pinned_projection_);
  refresh_projection(normal_projection_);

  if (tab_strip_model->ContainsIndex(tab_strip_model->active_index())) {
    content::WebContents* active_contents = tab_strip_model->GetWebContentsAt(tab_strip_model->active_index());
    if (active_contents) {
      last_model_.active_tab.index = tab_strip_model->active_index();
      last_model_.active_tab.title = active_contents->GetTitle();
      const GURL& url = active_contents->GetVisibleURL();
      last_model_.active_tab.host = url.is_valid() ? base::UTF8ToUTF16(url.host()) : std::u16string();
      if (auto* helper = MahoTabIdHelper::FromWebContents(active_contents)) {
        last_model_.active_tab.tab_id = helper->stable_tab_id();
      }
    }
  }
  ScheduleAccessibilityTreeRebuild();
  DVLOG(1) << "[MAHO_PERF] ApplyCosmeticRefresh "
           << cosmetic_t.Elapsed().InMillisecondsF() << "ms";
}

void MahoSidebarTabListView::ToggleTabSelected(const std::string& tab_id) {
  if (selected_tab_ids_.empty() && !last_active_tab_id_.empty() &&
      last_active_tab_id_ != tab_id &&
      tab_id_to_row_view_.count(last_active_tab_id_) > 0) {
    selected_tab_ids_.insert(last_active_tab_id_);
    auto active_it = tab_id_to_row_view_.find(last_active_tab_id_);
    if (active_it != tab_id_to_row_view_.end() && active_it->second) {
      active_it->second->SetSelected(true);
    }
  }

  auto it = selected_tab_ids_.find(tab_id);
  if (it != selected_tab_ids_.end()) {
    selected_tab_ids_.erase(it);
    if (selection_anchor_tab_id_ == tab_id) {
      selection_anchor_tab_id_.clear();
    }
  } else {
    selected_tab_ids_.insert(tab_id);
    selection_anchor_tab_id_ = tab_id;
  }

  auto row_it = tab_id_to_row_view_.find(tab_id);
  if (row_it != tab_id_to_row_view_.end() && row_it->second) {
    row_it->second->SetSelected(selected_tab_ids_.count(tab_id) > 0);
  }
  ScheduleAccessibilityTreeRebuild();
}

void MahoSidebarTabListView::SelectRangeTo(const std::string& tab_id) {
  std::string anchor = selection_anchor_tab_id_.empty()
                           ? last_active_tab_id_
                           : selection_anchor_tab_id_;
  if (anchor.empty()) {
    ToggleTabSelected(tab_id);
    return;
  }

  // Range over the projection order (every tab), so a range that crosses the
  // realized window boundary selects the correct contiguous id run.
  const std::vector<std::string> order = GetTabVisualOrderIds();
  int anchor_idx = -1;
  int target_idx = -1;
  for (size_t i = 0; i < order.size(); ++i) {
    if (order[i] == anchor) {
      anchor_idx = static_cast<int>(i);
    }
    if (order[i] == tab_id) {
      target_idx = static_cast<int>(i);
    }
  }

  if (anchor_idx == -1 || target_idx == -1) {
    ToggleTabSelected(tab_id);
    return;
  }

  int start = std::min(anchor_idx, target_idx);
  int end = std::max(anchor_idx, target_idx);

  selected_tab_ids_.clear();
  for (int i = start; i <= end; ++i) {
    selected_tab_ids_.insert(order[i]);
  }
  selection_anchor_tab_id_ = anchor;

  // Apply visual selection to realized rows; off-window rows pick it up from
  // selected_tab_ids_ when they enter the window (RealizeProjectionRow).
  for (const auto& [id, row] : tab_id_to_row_view_) {
    if (row) {
      row->SetSelected(selected_tab_ids_.count(id) > 0);
    }
  }
  ScheduleAccessibilityTreeRebuild();
}

void MahoSidebarTabListView::ClearSelection() {
  if (selected_tab_ids_.empty()) {
    return;
  }
  auto old_selected = std::move(selected_tab_ids_);
  selected_tab_ids_.clear();
  selection_anchor_tab_id_.clear();

  for (const auto& id : old_selected) {
    auto it = tab_id_to_row_view_.find(id);
    if (it != tab_id_to_row_view_.end() && it->second) {
      it->second->SetSelected(false);
    }
  }
  ScheduleAccessibilityTreeRebuild();
}

bool MahoSidebarTabListView::IsTabSelected(const std::string& tab_id) const {
  return selected_tab_ids_.count(tab_id) > 0;
}

std::vector<SidebarTabRowView*> MahoSidebarTabListView::GetTabRowsInVisualOrder() {
  std::vector<SidebarTabRowView*> rows;
  if (!tab_rows_) {
    return rows;
  }
  std::function<void(views::View*)> collect;
  collect = [&](views::View* parent) {
    for (views::View* child : parent->children()) {
      if (auto* row = views::AsViewClass<SidebarTabRowView>(child)) {
        rows.push_back(row);
      }
      collect(child);
    }
  };
  collect(tab_rows_);
  return rows;
}

void MahoSidebarTabListView::CloseSelected() {
  std::vector<std::string> snapshot_ids(selected_tab_ids_.begin(), selected_tab_ids_.end());
  std::erase_if(snapshot_ids, [this](const std::string& tab_id) {
    return !HasTabInLastModel(tab_id);
  });
  if (snapshot_ids.empty()) {
    return;
  }

  std::vector<std::string> live_ids;
  std::vector<std::string> suspended_ids;
  for (const auto& tab_id : snapshot_ids) {
    int idx = ResolveTabStripIndexForTabId(tab_id);
    if (idx >= 0) {
      live_ids.push_back(tab_id);
    } else {
      suspended_ids.push_back(tab_id);
    }
  }

  RebuildRowsBatchScoper scoper(this);

  for (const auto& tab_id : live_ids) {
    CloseTabById(tab_id);
  }

  for (const auto& tab_id : suspended_ids) {
    CloseTabById(tab_id);
  }

  ClearSelection();
}

void MahoSidebarTabListView::PinSelected(bool pin) {
  std::vector<std::string> snapshot_ids(selected_tab_ids_.begin(), selected_tab_ids_.end());
  std::erase_if(snapshot_ids, [this](const std::string& tab_id) {
    return !HasTabInLastModel(tab_id);
  });
  if (snapshot_ids.empty()) {
    return;
  }

  RebuildRowsBatchScoper scoper(this);

  for (const auto& tab_id : snapshot_ids) {
    maho::DispatchShellEvent(pin ? "pin_tab" : "unpin_tab",
                             {{"tab_id", tab_id}});
  }
}

void MahoSidebarTabListView::MoveSelectedToSpace(const std::string& space_id) {
  std::vector<std::string> snapshot_ids(selected_tab_ids_.begin(), selected_tab_ids_.end());
  std::erase_if(snapshot_ids, [this](const std::string& tab_id) {
    return !HasTabInLastModel(tab_id);
  });
  if (snapshot_ids.empty()) {
    return;
  }

  RebuildRowsBatchScoper scoper(this);

  for (const auto& tab_id : snapshot_ids) {
    maho::DispatchShellEvent("move_tab_to_space",
                             {{"target_space_id", space_id},
                              {"tab_id", tab_id},
                              {"section", "normal"}});
  }

  ClearSelection();
}

void MahoSidebarTabListView::MuteSelected(bool mute) {
  std::vector<std::string> snapshot_ids(selected_tab_ids_.begin(), selected_tab_ids_.end());
  std::erase_if(snapshot_ids, [this](const std::string& tab_id) {
    return !HasTabInLastModel(tab_id);
  });
  if (snapshot_ids.empty()) {
    return;
  }

  RebuildRowsBatchScoper scoper(this);

  TabStripModel* model = browser_ ? browser_->GetTabStripModel() : nullptr;
  for (const auto& tab_id : snapshot_ids) {
    int idx = ResolveTabStripIndexForTabId(tab_id);
    if (idx >= 0 && model) {
      if (content::WebContents* contents = model->GetWebContentsAt(idx)) {
        ::SetTabAudioMuted(contents, mute, TabMutedReason::kAudioIndicator, std::string());
      }
    }
    maho::DispatchShellEvent(mute ? "mute_tab" : "unmute_tab",
                             {{"tab_id", tab_id}});
  }
}

void MahoSidebarTabListView::ArchiveSelected() {
  std::vector<std::string> snapshot_ids(selected_tab_ids_.begin(), selected_tab_ids_.end());
  std::erase_if(snapshot_ids, [this](const std::string& tab_id) {
    return !HasTabInLastModel(tab_id);
  });
  if (snapshot_ids.empty()) {
    return;
  }
  RebuildRowsBatchScoper scoper(this);
  MahoTabRegistry::Get()->ArchiveTabsAndRemoveFromStrip(browser_, snapshot_ids);
  ClearSelection();
}

void MahoSidebarTabListView::OpenSelectedInSplit() {
  TabStripModel* model = browser_ ? browser_->GetTabStripModel() : nullptr;
  if (!model) {
    return;
  }
  std::vector<std::pair<int, std::string>> live;
  for (const auto& tab_id : selected_tab_ids_) {
    int idx = ResolveTabStripIndexForTabId(tab_id);
    if (idx >= 0 && model->ContainsIndex(idx) &&
        !model->GetSplitForTab(idx).has_value()) {
      live.emplace_back(idx, tab_id);
    }
  }
  if (live.size() < 2) {
    return;
  }
  // Contract: more than two eligible tabs collapse to the first two in strip
  // order — Maho splits are strictly two-pane; never more than two panes.
  std::sort(live.begin(), live.end());
  const int first_idx = live[0].first;
  const int second_idx = live[1].first;

  split_tabs::SplitTabVisualData visual_data(
      split_tabs::SplitTabLayout::kSideBySide, 0.5);
  model->ActivateTabAt(first_idx,
                       TabStripUserGestureDetails(
                           TabStripUserGestureDetails::GestureType::kOther));
  model->AddToNewSplit({second_idx}, visual_data,
                       split_tabs::SplitTabCreatedSource::kTabContextMenu);

  if (browser_) {
    std::string event_json = base::StringPrintf(
        "{\"kind\":\"create_split\",\"window_id\":\"%s\","
        "\"tab_ids\":[\"%s\",\"%s\"],\"orientation\":\"vertical\"}",
        std::to_string(browser_->GetSessionID().id()).c_str(),
        live[0].second.c_str(), live[1].second.c_str());
    if (auto* core = maho::GetCore()) {
      if (char* result = maho_core_handle_event(core, event_json.c_str())) {
        maho_core_free_string(result);
      }
    }
  }
}

void MahoSidebarTabListView::NewFolderWithSelected() {
  std::vector<std::string> snapshot_ids(selected_tab_ids_.begin(), selected_tab_ids_.end());
  std::erase_if(snapshot_ids, [this](const std::string& tab_id) {
    return !HasTabInLastModel(tab_id);
  });
  if (snapshot_ids.empty()) {
    return;
  }
  auto* bridge = MahoSpaceProfileBridge::GetInstance();
  std::string active_space_id =
      bridge ? bridge->GetActiveSpaceId(browser_) : std::string();
  if (active_space_id.empty()) {
    return;
  }
  base::ListValue ids;
  for (const auto& tab_id : snapshot_ids) {
    ids.Append(tab_id);
  }
  base::DictValue dict;
  dict.Set("space_id", active_space_id);
  dict.Set("name", "New Folder");
  dict.Set("tab_ids", std::move(ids));
  DispatchShellEventDict("create_folder_with_tabs", std::move(dict));

  ClearSelection();
}

void SidebarTabRowView::SetSelected(bool selected) {
  if (is_selected_ == selected) {
    return;
  }
  is_selected_ = selected;
  GetViewAccessibility().SetIsSelected(is_selected_);
  UpdateAppearance();
}

MahoSidebarTabListView* SidebarTabRowView::GetTabListView() {
  views::View* p = parent();
  while (p) {
    if (auto* v = views::AsViewClass<MahoSidebarTabListView>(p)) {
      return v;
    }
    p = p->parent();
  }
  return nullptr;
}

void SidebarTabRowView::CloseSelectedTabs() {
  if (MahoSidebarTabListView* list_view = GetTabListView()) {
    list_view->CloseSelected();
  }
}

void SidebarTabRowView::CloseTabById(const std::string& tab_id) {
  if (MahoSidebarTabListView* list_view = GetTabListView()) {
    list_view->CloseTabById(tab_id);
  }
}

void SidebarTabRowView::PinSelectedTabs(bool pin) {
  if (MahoSidebarTabListView* list_view = GetTabListView()) {
    list_view->PinSelected(pin);
  }
}

void SidebarTabRowView::MoveSelectedTabsToSpace(const std::string& space_id) {
  if (MahoSidebarTabListView* list_view = GetTabListView()) {
    list_view->MoveSelectedToSpace(space_id);
  }
}

RebuildRowsBatchScoper::RebuildRowsBatchScoper(MahoSidebarTabListView* view) : view_(view) {
  ++view_->batch_depth_;
}

RebuildRowsBatchScoper::~RebuildRowsBatchScoper() {
  if (--view_->batch_depth_ > 0) {
    return;
  }
  if (view_->batch_rebuild_needed_) {
    view_->batch_rebuild_needed_ = false;
    view_->Update(view_->last_model_, view_->browser_);
  }
}

void SidebarTabRowView::MuteSelectedTabs(bool mute) {
  if (MahoSidebarTabListView* list_view = GetTabListView()) {
    list_view->MuteSelected(mute);
  }
}

void SidebarTabRowView::ArchiveSelectedTabs() {
  if (MahoSidebarTabListView* list_view = GetTabListView()) {
    list_view->ArchiveSelected();
  }
}

void SidebarTabRowView::OpenSelectedTabsInSplit() {
  if (MahoSidebarTabListView* list_view = GetTabListView()) {
    list_view->OpenSelectedInSplit();
  }
}

void SidebarTabRowView::NewFolderWithSelectedTabs() {
  if (MahoSidebarTabListView* list_view = GetTabListView()) {
    list_view->NewFolderWithSelected();
  }
}

MahoSidebarView* MahoSidebarTabListView::GetSidebarView() {
  for (views::View* v = this; v; v = v->parent()) {
    if (auto* sidebar_view = views::AsViewClass<MahoSidebarView>(v)) {
      return sidebar_view;
    }
  }
  return nullptr;
}

void MahoSidebarTabListView::HandlePostDropTransition(
    const SidebarDragPayload& payload,
    const SidebarDropPlan& plan) {
  if (plan.pin_transition.kind == SidebarDropPlan::PinTransition::Kind::kNone) {
    return;
  }

  const bool to_pinned =
      (plan.pin_transition.kind == SidebarDropPlan::PinTransition::Kind::kPin);

  if (plan.pin_transition.is_folder) {
    DispatchShellEventEx(
        maho::sidebar::kSetFolderPinned,
        {ShellEventField("space_id", plan.pin_transition.space_id),
         ShellEventField("folder_id", plan.pin_transition.node_id),
         ShellEventField("is_pinned", to_pinned)});
    return;
  }

  const std::string& tab_id = plan.pin_transition.node_id;

  int index = payload.tab_strip_index;
  if (index < 0 || !browser_ || !browser_->GetTabStripModel() ||
      index >= browser_->GetTabStripModel()->count() ||
      FindCoreTabIdByWebContents(
          browser_->GetTabStripModel()->GetWebContentsAt(index)) != tab_id) {
    index = ResolveTabStripIndexForTabId(tab_id);
  }

  if (index < 0) {
    return;
  }

  TabStripModel* model = browser_ ? browser_->GetTabStripModel() : nullptr;
  if (!model) {
    return;
  }

  // B2: Split pane pinned, dissolve split first.
  if (to_pinned && model->GetSplitForTab(index).has_value()) {
    MahoSplitViewController(browser_).RemoveSplitForTab(index);
  }

  // Set the pin state on the native tab strip model
  MahoSidebarView* sidebar_view = GetSidebarView();
  if (sidebar_view) {
    sidebar_view->SetApplyingPin(true);
  }
  model->SetTabPinned(index, to_pinned);
  if (sidebar_view) {
    sidebar_view->SetApplyingPin(false);
  }
}

}  // namespace maho
