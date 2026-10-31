// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_CONTROL_ACTIVITY_INDICATOR_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_CONTROL_ACTIVITY_INDICATOR_VIEW_H_

#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "maho/browser/ui/theme/maho_theme_helper.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

namespace gfx {
struct VectorIcon;
}  // namespace gfx

namespace views {
class ImageView;
class Label;
class LabelButton;
}  // namespace views

namespace maho {

// Which runtime is driving the browser. Rendered as the controller prefix in
// the indicator's leading label (e.g. "AI - CLI").
enum class MahoControlPlane {
  kEmbedded,
  kCli,
  kExternalMcp,
};

// The narrow visual state set approved in the visual-feedback report. This
// view owns no business state: the hosting coordinator resolves the semantic
// state and pushes a fully-formed model in.
enum class MahoControlActivityState {
  // No controller is attached; the indicator hides itself entirely.
  kIdle,
  // Observation only: text/AX/screenshot/network reads.
  kReading,
  // Mutating the page or browser state.
  kActing,
  // Blocked on a consequence checkpoint.
  kWaitingApproval,
  // Controller is attached but intentionally halted.
  kPaused,
  // Transport dropped or lease expired without an explicit end.
  kDisconnected,
  // The last operation failed.
  kError,
};

// Everything the indicator renders. Strings arrive pre-redacted; the view
// never derives, stores, or displays raw tool arguments or page content.
struct MahoControlActivityIndicatorModel {
  MahoControlActivityIndicatorModel();
  MahoControlActivityIndicatorModel(const MahoControlActivityIndicatorModel&);
  MahoControlActivityIndicatorModel& operator=(
      const MahoControlActivityIndicatorModel&);
  ~MahoControlActivityIndicatorModel();

  MahoControlActivityState state = MahoControlActivityState::kIdle;
  MahoControlPlane plane = MahoControlPlane::kEmbedded;
  // Human-readable controller name, e.g. "Maho CLI". Never an authority claim.
  std::u16string controller_name;
  // Current active tool / step summary.
  std::u16string current_step;
  // Title of the tab under control. May be empty when the target has no title.
  std::u16string target_title;
  // True when the controlled tab is not the active tab of this window.
  bool target_is_background_tab = false;
  // True when the controlled tab lives in a different browser window.
  bool target_is_other_window = false;
};

// Static, accessible control-activity chip. Hosted by the sidebar top bar in
// expanded mode and by the collapsed-sidebar toolbar fallback. It maps the
// semantic state to a Lucide icon, palette color role, label text, and the
// availability of the Details and Stop/Revoke affordances.
//
// Deliberately excluded: live service subscriptions, timers, animations,
// per-app colors, and any ownership of controller/session truth.
class MahoControlActivityIndicatorView : public views::View {
  METADATA_HEADER(MahoControlActivityIndicatorView, views::View)

 public:
  MahoControlActivityIndicatorView();
  MahoControlActivityIndicatorView(const MahoControlActivityIndicatorView&) =
      delete;
  MahoControlActivityIndicatorView& operator=(
      const MahoControlActivityIndicatorView&) = delete;
  ~MahoControlActivityIndicatorView() override;

  // Static, pure mappings. Exposed so the state contract can be asserted
  // without instantiating a widget.
  static const gfx::VectorIcon& IconForState(MahoControlActivityState state);
  static std::u16string PlaneLabel(MahoControlPlane plane);
  static std::u16string StatusTextForState(
      const MahoControlActivityIndicatorModel& model);
  static std::u16string AccessibleNameForModel(
      const MahoControlActivityIndicatorModel& model);
  // Stop/Revoke is only meaningful while a controller can still act.
  static bool StateAllowsStop(MahoControlActivityState state);
  // Details opens the session detail surface; available whenever a session
  // exists, including after it failed or disconnected.
  static bool StateAllowsDetails(MahoControlActivityState state);
  // Stop reads "Revoke" once the controller holds an ongoing grant the user
  // must withdraw rather than merely interrupt.
  static std::u16string StopButtonLabelForState(MahoControlActivityState state);

  void SetModel(const MahoControlActivityIndicatorModel& model);
  const MahoControlActivityIndicatorModel& model() const { return model_; }

  void OnSidebarPaletteChanged(const MahoSidebarPalette& palette);

  // Compact mode drops the status text and target title, keeping the state
  // glyph plus Stop so a 320dp-wide window never hides the kill switch.
  void SetCompact(bool compact);
  bool compact() const { return compact_; }

  void SetDetailsCallback(base::RepeatingClosure callback);
  void SetStopCallback(base::RepeatingClosure callback);

  views::ImageView* state_icon_for_testing() { return state_icon_; }
  views::Label* status_label_for_testing() { return status_label_; }
  views::Label* target_label_for_testing() { return target_label_; }
  views::LabelButton* details_button_for_testing() { return details_button_; }
  views::LabelButton* stop_button_for_testing() { return stop_button_; }
  const MahoSidebarPalette& sidebar_palette_for_testing() const {
    return palette_;
  }

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;

 private:
  void RebuildFromModel();
  void ApplyPalette();
  // Resolves the palette role for the current state. Reading/paused states are
  // deliberately quieter than acting/approval/error states.
  SkColor ResolveStateColor() const;

  MahoControlActivityIndicatorModel model_;
  MahoSidebarPalette palette_;
  bool compact_ = false;

  raw_ptr<views::ImageView> state_icon_ = nullptr;
  raw_ptr<views::Label> status_label_ = nullptr;
  raw_ptr<views::Label> target_label_ = nullptr;
  raw_ptr<views::LabelButton> details_button_ = nullptr;
  raw_ptr<views::LabelButton> stop_button_ = nullptr;

  base::RepeatingClosure details_callback_;
  base::RepeatingClosure stop_callback_;

  base::WeakPtrFactory<MahoControlActivityIndicatorView> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_CONTROL_ACTIVITY_INDICATOR_VIEW_H_
