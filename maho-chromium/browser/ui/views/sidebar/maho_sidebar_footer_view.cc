// Copyright 2026 Maho Browser. All rights reserved.

#include "maho_sidebar_footer_view.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

#include "base/json/json_writer.h"
#include "base/values.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/third_party/maho/maho_ffi.h"

#include "base/functional/bind.h"
#include "base/task/single_thread_task_runner.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_tab_list_view.h"
#include "base/functional/callback.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "ui/views/view_utils.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/vector_icons/vector_icons.h"
#include "maho/browser/ui/context_menu/maho_space_context_menu.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_action_popover_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_container_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_library_rail_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_state_adapter.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_tab_list_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_view.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/base/ui_base_types.h"
#include "ui/color/color_id.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/vector_icon_types.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/label.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/style/typography.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/view_utils.h"
#include "url/gurl.h"
#include "cc/paint/paint_flags.h"
#include "cc/paint/paint_shader.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/canvas.h"
#include "ui/compositor/layer.h"

namespace maho {

namespace {

MahoSidebarView* FindSidebarViewAncestor(views::View* view) {
  for (views::View* current = view; current; current = current->parent()) {
    if (auto* sidebar_view = views::AsViewClass<MahoSidebarView>(current)) {
      return sidebar_view;
    }
  }
  return nullptr;
}

constexpr int kUpdatePillCornerRadius = 11;
constexpr int kUpdateIndicatorSize = 5;
constexpr int kFooterRowHeight = 32;
constexpr int kFooterControlSize = 28;
constexpr int kFooterControlCornerRadius = 14;
constexpr int kFooterButtonIconSize = 14;
constexpr int kFooterRowSpacing = 0;
constexpr int kDotSpacing = 4;
constexpr int kFadeWidth = 8;
constexpr int kDotTrackHeight = 14;

const gfx::Insets kFooterInsets = gfx::Insets::TLBR(0, 0, 0, 0);
const gfx::Insets kUpdatePillInsets = gfx::Insets::TLBR(3, 10, 3, 10);
const gfx::Insets kFooterRowInsets = gfx::Insets::TLBR(1, 8, 1, 8);
const gfx::Insets kClusterInsets = gfx::Insets::TLBR(0, 8, 0, 8);

bool ParseHexColor(const std::string& color, SkColor* out) {
  if (!out || color.size() != 7 || color[0] != '#') {
    return false;
  }

  unsigned int red = 0;
  unsigned int green = 0;
  unsigned int blue = 0;
  if (!base::HexStringToUInt(color.substr(1, 2), &red) ||
      !base::HexStringToUInt(color.substr(3, 2), &green) ||
      !base::HexStringToUInt(color.substr(5, 2), &blue)) {
    return false;
  }

  *out = SkColorSetRGB(red, green, blue);
  return true;
}

class MahoFooterIconButton : public views::MdTextButton {
  METADATA_HEADER(MahoFooterIconButton, views::MdTextButton)

 public:
  using views::MdTextButton::MdTextButton;

  void StateChanged(ButtonState old_state) override {
    views::MdTextButton::StateChanged(old_state);
    UpdateHoverBackground();
  }

  void OnThemeChanged() override {
    views::MdTextButton::OnThemeChanged();
    UpdateHoverBackground();
  }

  void SetSidebarPalette(const MahoSidebarPalette& palette,
                         const gfx::VectorIcon& icon) {
    palette_ = palette;
    icon_ = &icon;
    UpdateIcon();
    UpdateHoverBackground();
  }

 private:
  void UpdateIcon() {
    if (!icon_) {
      return;
    }
    // The footer is hidden in private windows, so a resolved palette is
    // always present here. macOS paints STATE_DISABLED in inactive windows.
    SetImageModel(views::Button::STATE_NORMAL,
                  ui::ImageModel::FromVectorIcon(*icon_, palette_.neutral_glyph,
                                                 kFooterButtonIconSize));
    SetImageModel(views::Button::STATE_DISABLED,
                  ui::ImageModel::FromVectorIcon(*icon_, palette_.disabled_text,
                                                 kFooterButtonIconSize));
  }

  void UpdateHoverBackground() {
    const ButtonState state = GetState();
    if (state == STATE_PRESSED) {
      SetBackground(views::CreateRoundedRectBackground(
          palette_.row_selected, kFooterControlCornerRadius));
    } else if (state == STATE_HOVERED) {
      SetBackground(views::CreateRoundedRectBackground(
          palette_.row_hover, kFooterControlCornerRadius));
    } else {
      SetBackground(nullptr);
    }
  }

  MahoSidebarPalette palette_;
  raw_ptr<const gfx::VectorIcon> icon_ = nullptr;
};

BEGIN_METADATA(MahoFooterIconButton)
END_METADATA

views::MdTextButton* CreateRoundActionButton(
    views::View* parent,
    views::Button::PressedCallback callback,
    const gfx::VectorIcon& icon,
    const std::u16string& tooltip) {
  auto button = std::make_unique<MahoFooterIconButton>(std::move(callback),
                                                       std::u16string());
  button->SetStyle(ui::ButtonStyle::kText);
  button->SetCornerRadius(kFooterControlCornerRadius);
  button->SetMinSize(gfx::Size(kFooterControlSize, kFooterControlSize));
  button->SetMaxSize(gfx::Size(kFooterControlSize, kFooterControlSize));
  // Icon and text colors are bound from the palette in
  // UpdateFooterChromeAppearance().
  button->SetTooltipText(tooltip);
  button->SetAccessibleName(tooltip);
  button->SetBorder(views::CreateEmptyBorder(gfx::Insets()));
  button->SetBackground(nullptr);
  return parent->AddChildView(std::move(button));
}

class MahoSidebarFadeMaskView : public views::View {
  METADATA_HEADER(MahoSidebarFadeMaskView, views::View)

 public:
  MahoSidebarFadeMaskView() {
    SetCanProcessEventsWithinSubtree(false);
  }

  void SetFadeState(bool show_left, bool show_right, SkColor surface_color) {
    if (show_left_ == show_left && show_right_ == show_right &&
        surface_color_ == surface_color) {
      return;
    }
    show_left_ = show_left;
    show_right_ = show_right;
    surface_color_ = surface_color;
    SchedulePaint();
  }

  void OnPaint(gfx::Canvas* canvas) override {
    const gfx::Rect bounds = GetContentsBounds();
    if (bounds.IsEmpty()) {
      return;
    }

    const SkColor4f opaque = SkColor4f::FromColor(surface_color_);
    const SkColor4f transparent = {opaque.fR, opaque.fG, opaque.fB, 0.0f};

    if (show_left_) {
      const SkPoint points[2] = {
          {static_cast<float>(bounds.x()), 0},
          {static_cast<float>(bounds.x() + kFadeWidth), 0}};
      const SkColor4f colors[2] = {opaque, transparent};
      cc::PaintFlags flags;
      flags.setAntiAlias(true);
      flags.setStyle(cc::PaintFlags::kFill_Style);
      flags.setShader(cc::PaintShader::MakeLinearGradient(
          points, colors, nullptr, 2, SkTileMode::kClamp));
      canvas->DrawRect(
          gfx::Rect(bounds.x(), bounds.y(), kFadeWidth, bounds.height()),
          flags);
    }

    if (show_right_) {
      const int right_x = bounds.right() - kFadeWidth;
      const SkPoint points[2] = {
          {static_cast<float>(right_x), 0},
          {static_cast<float>(bounds.right()), 0}};
      const SkColor4f colors[2] = {transparent, opaque};
      cc::PaintFlags flags;
      flags.setAntiAlias(true);
      flags.setStyle(cc::PaintFlags::kFill_Style);
      flags.setShader(cc::PaintShader::MakeLinearGradient(
          points, colors, nullptr, 2, SkTileMode::kClamp));
      canvas->DrawRect(
          gfx::Rect(right_x, bounds.y(), kFadeWidth, bounds.height()), flags);
    }
  }

 private:
  bool show_left_ = false;
  bool show_right_ = false;
  SkColor surface_color_ = SK_ColorWHITE;
};

BEGIN_METADATA(MahoSidebarFadeMaskView)
END_METADATA

void DispatchCreateFolderForSpace(views::View* origin,
                                  const std::string& space_id) {
  if (space_id.empty()) {
    return;
  }
  std::string new_folder_id = DispatchCreateFolder(space_id, "New Folder");
  if (new_folder_id.empty() || !origin) {
    return;
  }
  MahoSidebarView* sidebar_view = FindSidebarViewAncestor(origin);
  if (!sidebar_view || !sidebar_view->tab_list_view()) {
    return;
  }
  sidebar_view->tab_list_view()->SetPendingFolderEdit(new_folder_id);
}

}  // namespace

BEGIN_METADATA(MahoSidebarFooterView)
END_METADATA

MahoSidebarFooterView::MahoSidebarFooterView(Browser* browser)
    : browser_(browser) {
  SetAccessibleRole(ax::mojom::Role::kToolbar);
  GetViewAccessibility().SetName(u"Sidebar footer controls");
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, kFooterInsets, 4));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  update_pill_ = AddChildView(std::make_unique<views::View>());
  auto* update_pill_layout =
      update_pill_->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, kUpdatePillInsets, 6));
  update_pill_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  update_pill_indicator_ =
      update_pill_->AddChildView(std::make_unique<views::View>());
  update_pill_indicator_->SetPreferredSize(
      gfx::Size(kUpdateIndicatorSize, kUpdateIndicatorSize));

  update_pill_label_ = update_pill_->AddChildView(
      std::make_unique<views::Label>(u"Maho is up to date"));
  update_pill_label_->SetAutoColorReadabilityEnabled(false);
  update_pill_label_->SetTextStyle(views::style::STYLE_BODY_5);
  update_pill_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);

  footer_region_ = AddChildView(std::make_unique<views::View>());
  auto* footer_row_layout =
      footer_region_->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, kFooterRowInsets,
          kFooterRowSpacing));
  footer_row_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  footer_region_->SetPreferredSize(gfx::Size(0, kFooterRowHeight));
  footer_region_->SetBackground(nullptr);
  footer_region_->SetBorder(nullptr);

  archive_button_ = CreateRoundActionButton(
      footer_region_,
      base::BindRepeating(&MahoSidebarFooterView::OnArchivePressed,
                          weak_factory_.GetWeakPtr()),
      GetMahoSidebarArchiveboxIcon(), u"Library");

  spaces_cluster_ = footer_region_->AddChildView(std::make_unique<views::View>());
  auto* cluster_layout =
      spaces_cluster_->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, kClusterInsets,
          kDotSpacing));
  cluster_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  footer_row_layout->SetFlexForView(spaces_cluster_, 1, true);
  spaces_cluster_->SetPreferredSize(gfx::Size(0, kDotTrackHeight));

  auto scroll_view = std::make_unique<views::ScrollView>();
  scroll_view->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kHiddenButEnabled);
  scroll_view->SetVerticalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  scroll_view->SetDrawOverflowIndicator(false);
  scroll_view->SetBackgroundColor(std::nullopt);
  scroll_view->ClipHeightTo(0, kDotTrackHeight);
  scroll_view->SetProperty(views::kMarginsKey, gfx::Insets());

  auto dot_container_owned = std::make_unique<views::View>();
  dot_container_ = dot_container_owned.get();
  auto* dots_layout =
      dot_container_->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal,
          gfx::Insets(),
          kDotSpacing));
  dots_layout->set_main_axis_alignment(
      views::BoxLayout::MainAxisAlignment::kCenter);
  dots_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  dot_container_->SetProperty(views::kMarginsKey, gfx::Insets());

  scroll_view->SetContents(std::move(dot_container_owned));

  auto fade_mask_owned = std::make_unique<MahoSidebarFadeMaskView>();
  fade_mask_ = fade_mask_owned.get();
  fade_mask_->SetPaintToLayer();
  fade_mask_->layer()->SetFillsBoundsOpaquely(false);

  scroll_view_ = spaces_cluster_->AddChildView(std::move(scroll_view));
  cluster_layout->SetFlexForView(scroll_view_, 1);

  spaces_cluster_->AddChildView(std::move(fade_mask_owned));
  fade_mask_->SetPreferredSize(gfx::Size(0, 0));

  plus_button_ = CreateRoundActionButton(
      footer_region_,
      base::BindRepeating(&MahoSidebarFooterView::OnPlusPressed,
                          weak_factory_.GetWeakPtr()),
      vector_icons::kAddOldIcon, u"Create space");

  UpdateFooterChromeAppearance();
  UpdateUpdatePill(MahoSidebarFooterUpdatePillModel());
  RebuildDots(0, -1, {}, {}, {}, {});
  SetFocusBehavior(FocusBehavior::ALWAYS);
}

MahoSidebarFooterView::~MahoSidebarFooterView() {
  CloseActionPopover();
}

void MahoSidebarFooterView::RefreshPreferredSize() {
  PreferredSizeChanged();
}

void MahoSidebarFooterView::OnSidebarPaletteChanged(
    const MahoSidebarPalette& palette) {
  palette_ = palette;
  UpdateFooterChromeAppearance();
  UpdateFadeMask();
  if (!dot_container_) {
    return;
  }
  for (views::View* child : dot_container_->children()) {
    if (auto* dot = views::AsViewClass<MahoSidebarSpaceDotView>(child)) {
      dot->SetSidebarPalette(palette_);
    }
  }
}

void MahoSidebarFooterView::Update(const MahoSidebarFooterModel& model) {
  UpdateUpdatePill(model.update_pill);
  plus_button_->SetEnabled(model.can_create_space);
  active_space_color_ = model.space_color;
  UpdateFooterChromeAppearance();
  RebuildDots(model.space_count, model.active_space_index, model.space_ids,
              model.space_icons, model.space_names, model.space_colors);
}

void MahoSidebarFooterView::ShowContextMenuForViewImpl(
    views::View* source,
    const gfx::Point& point,
    ui::mojom::MenuSourceType source_type) {
  if (!browser_) {
    return;
  }

  context_menu_runner_.reset();
  active_context_menu_model_.reset();
  active_space_context_menu_.reset();

  active_space_context_menu_ =
      std::make_unique<MahoSpaceContextMenu>(browser_, "sidebar-spaces");
  active_context_menu_model_ = active_space_context_menu_->BuildMenuModel();
  context_menu_runner_ = std::make_unique<views::MenuRunner>(
      active_context_menu_model_.get(), views::MenuRunner::CONTEXT_MENU);
  context_menu_runner_->RunMenuAt(source->GetWidget(), nullptr,
                                  gfx::Rect(point, gfx::Size()),
                                  views::MenuAnchorPosition::kTopLeft,
                                  source_type);
}

void MahoSidebarFooterView::ExecuteCommand(int command_id, int event_flags) {
  (void)command_id;
  (void)event_flags;
}

bool MahoSidebarFooterView::IsCommandIdEnabled(int command_id) const {
  return true;
}

bool MahoSidebarFooterView::IsCommandIdChecked(int command_id) const {
  return false;
}

void MahoSidebarFooterView::UpdateUpdatePill(
    const MahoSidebarFooterUpdatePillModel& model) {
  const bool show = model.visible;
  update_pill_->SetVisible(show);
  update_pill_indicator_->SetVisible(show && model.has_update);
  if (update_pill_label_) {
    const std::u16string label =
        model.label.empty() ? std::u16string(u"Maho is up to date")
                            : model.label;
    const std::u16string tooltip_text =
        model.accessible_label.empty()
            ? label
            : std::u16string(model.accessible_label.begin(),
                              model.accessible_label.end());
    update_pill_label_->SetText(label);
    update_pill_label_->SetTooltipText(tooltip_text);
  }
}

void MahoSidebarFooterView::OnPlusPressed(const ui::Event& event) {
  (void)event;
  OpenSpaceCreationSurface();
}

void MahoSidebarFooterView::OnArchivePressed(const ui::Event& event) {
  (void)event;
  auto* sidebar_view = FindSidebarViewAncestor(this);
  if (sidebar_view) {
    // Footer archive button now opens Library mode (defaulting to
    // Archived Tabs). Toggling back to tabs still works via library
    // back button.
    sidebar_view->OpenLibrary();
  }
}

void MahoSidebarFooterView::OpenSpaceCreationSurface() {
  if (!plus_button_) {
    return;
  }

  if (action_popover_widget_) {
    CloseActionPopover();
    return;
  }

  action_popover_widget_ = MahoSidebarActionPopoverView::Show(
      plus_button_,
      base::BindRepeating(&MahoSidebarFooterView::HandleActionPopoverAction,
                          weak_factory_.GetWeakPtr()));
  if (action_popover_widget_) {
    action_popover_widget_->AddObserver(this);
  }
}

void MahoSidebarFooterView::HandleActionPopoverAction(
    MahoSidebarActionPopoverAction action) {
  CloseActionPopover();

  switch (action) {
    case MahoSidebarActionPopoverAction::kNewSpace: {
      auto* sidebar_view = FindSidebarViewAncestor(this);
      if (sidebar_view) {
        sidebar_view->ShowCreateSpace();
      }
      break;
    }
    case MahoSidebarActionPopoverAction::kNewFolder: {
      auto* bridge = maho::MahoSpaceProfileBridge::GetInstance();
      if (bridge) {
        DispatchCreateFolderForSpace(this, bridge->GetActiveSpaceId());
      }
      break;
    }
    case MahoSidebarActionPopoverAction::kNewTab:
      if (browser_) {
        auto* browser_view =
            BrowserView::GetBrowserViewForBrowser(browser_);
        if (browser_view) {
          for (views::View* child : browser_view->children()) {
            auto* container =
                views::AsViewClass<MahoSidebarContainerView>(child);
            if (container) {
              container->ShowCommandOverlayForNewTab();
              break;
            }
          }
        }
      }
      break;
  }
}

void MahoSidebarFooterView::CloseActionPopover() {
  if (action_popover_widget_) {
    action_popover_widget_->CloseNow();
  }
}

void MahoSidebarFooterView::OnWidgetDestroying(views::Widget* widget) {
  if (widget == action_popover_widget_) {
    action_popover_widget_->RemoveObserver(this);
    action_popover_widget_ = nullptr;
    open_create_space_after_popover_close_ = false;
  }
}

void MahoSidebarFooterView::RebuildDots(
    int space_count,
    int active_index,
    const std::vector<std::string>& space_ids,
    const std::vector<std::string>& space_icons,
    const std::vector<std::string>& space_names,
    const std::vector<std::string>& space_colors) {
  const int clamped_count = std::max(space_count, 0);
  const int target_count = space_ids.empty()
                               ? clamped_count
                               : std::min(clamped_count,
                                          static_cast<int>(space_ids.size()));

  if (target_count == 0) {
    dot_container_->RemoveAllChildViews();
    selected_dot_index_ = -1;
    return;
  }

  const auto& existing = dot_container_->children();
  const int existing_count = static_cast<int>(existing.size());

  // Phase 1 — reuse existing dots; Phase 2 — trim tail; Phase 3 — append new.
  const int reuse_count = std::min(existing_count, target_count);
  for (int i = 0; i < reuse_count; ++i) {
    auto* dot =
        views::AsViewClass<MahoSidebarSpaceDotView>(existing[i].get());
    if (!dot) {
      continue;
    }

    const std::string& sid =
        i < static_cast<int>(space_ids.size()) ? space_ids[i] : std::string();
    const bool active = (i == active_index);
    const std::string& icon =
        i < static_cast<int>(space_icons.size()) ? space_icons[i]
                                                  : std::string();
    const std::string& name =
        i < static_cast<int>(space_names.size()) ? space_names[i]
                                                  : std::string();
    SkColor dot_color = SK_ColorGRAY;
    if (i < static_cast<int>(space_colors.size())) {
      ParseHexColor(space_colors[i], &dot_color);
    }
    dot->Configure(sid, name, icon, dot_color, active);
  }

  while (static_cast<int>(dot_container_->children().size()) > target_count) {
    dot_container_->RemoveChildViewT(
        dot_container_->children().back().get());
  }

  for (int i = existing_count; i < target_count; ++i) {
    const bool active = (i == active_index);
    const std::string& sid =
        i < static_cast<int>(space_ids.size()) ? space_ids[i] : std::string();
    const std::string& icon =
        i < static_cast<int>(space_icons.size()) ? space_icons[i]
                                                  : std::string();
    const std::string& name =
        i < static_cast<int>(space_names.size()) ? space_names[i]
                                                  : std::string();

    SkColor dot_color = SK_ColorGRAY;
    if (i < static_cast<int>(space_colors.size())) {
      ParseHexColor(space_colors[i], &dot_color);
    }

    auto dot = std::make_unique<MahoSidebarSpaceDotView>();
    dot->Configure(sid, name, icon, dot_color, active);
    dot->SetSidebarPalette(palette_);
    dot->set_delegate(this);
    dot_container_->AddChildView(std::move(dot));
  }

  selected_dot_index_ = active_index;
  UpdateDotSelectionState();
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&MahoSidebarFooterView::ScrollToActiveSpace,
                     weak_factory_.GetWeakPtr()));
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&MahoSidebarFooterView::UpdateFadeMask,
                     weak_factory_.GetWeakPtr()));
}

void MahoSidebarFooterView::UpdateFooterChromeAppearance() {
  SetBackground(nullptr);
  if (footer_region_) {
    footer_region_->SetBackground(nullptr);
    footer_region_->SetBorder(nullptr);
  }
  spaces_cluster_->SetBackground(nullptr);
  spaces_cluster_->SetBorder(nullptr);

  auto style_button = [this](views::MdTextButton* button,
                             const gfx::VectorIcon& icon) {
    if (!button) {
      return;
    }
    button->SetBgColorOverrideDeprecated(SK_ColorTRANSPARENT);
    button->SetBorder(views::CreateEmptyBorder(gfx::Insets()));
    button->SetPaintToLayer();
    button->layer()->SetFillsBoundsOpaquely(false);
    button->SetTextSubpixelRenderingEnabled(false);
    // No layer dimming: neutral_glyph / disabled_text already carry the
    // palette's contrast guarantee, and layer opacity would re-blend them
    // below the 3:1 glyph threshold.
    auto* footer_button = static_cast<MahoFooterIconButton*>(button);
    footer_button->SetSidebarPalette(palette_, icon);
  };
  style_button(archive_button_, GetMahoSidebarArchiveboxIcon());
  style_button(plus_button_, vector_icons::kAddOldIcon);

  for (views::MdTextButton* button : {archive_button_.get(),
                                      plus_button_.get()}) {
    if (button) {
      button->SetEnabledTextColors(palette_.neutral_glyph);
      button->SetTextColor(views::Button::STATE_DISABLED,
                           palette_.disabled_text);
    }
  }

  if (update_pill_) {
    update_pill_->SetBackground(views::CreateRoundedRectBackground(
        palette_.row_hover, kUpdatePillCornerRadius));
    update_pill_->SetBorder(views::CreateRoundedRectBorder(
        1, kUpdatePillCornerRadius, palette_.outline));
  }
  if (update_pill_indicator_) {
    update_pill_indicator_->SetBackground(views::CreateRoundedRectBackground(
        palette_.focus_ring, kUpdateIndicatorSize / 2));
  }
  if (update_pill_label_) {
    update_pill_label_->SetEnabledColor(palette_.secondary_text);
  }
}

void MahoSidebarFooterView::UpdateDotSelectionState() {
  if (!dot_container_) {
    return;
  }

  const auto& children = dot_container_->children();
  for (size_t i = 0; i < children.size(); ++i) {
    auto* dot = views::AsViewClass<MahoSidebarSpaceDotView>(children[i]);
    if (!dot) {
      continue;
    }
    dot->SetSelected(static_cast<int>(i) == selected_dot_index_);
  }
}

void MahoSidebarFooterView::OnThemeChanged() {
  views::View::OnThemeChanged();
  UpdateFooterChromeAppearance();
  UpdateFadeMask();
}

void MahoSidebarFooterView::OnSpaceDotActivated(
    MahoSidebarSpaceDotView* dot) {
  if (!dot) {
    return;
  }
  const auto& children = dot_container_->children();
  for (size_t i = 0; i < children.size(); ++i) {
    if (children[i] == dot) {
      selected_dot_index_ = static_cast<int>(i);
      break;
    }
  }
  UpdateDotSelectionState();
  if (!dot->space_id().empty()) {
    MahoSidebarView::ActivateSpaceAndTab(browser_, dot->space_id());
  }
}

void MahoSidebarFooterView::OnSpaceDotSpringLoad(
    MahoSidebarSpaceDotView* dot) {
  if (!browser_ || !dot || dot->space_id().empty()) {
    return;
  }
  auto* bridge = MahoSpaceProfileBridge::GetInstance();
  if (!bridge || !bridge->SwitchToSpace(browser_, dot->space_id())) {
    return;
  }
  // The Space switch schedules an async sidebar refresh that a drag's capture
  // defers; flush it (best-effort, after the refresh lands) so the switched
  // Space becomes visible during the drag rather than only on drop.
  auto* browser_view = BrowserView::GetBrowserViewForBrowser(browser_);
  auto* container =
      browser_view ? static_cast<MahoSidebarContainerView*>(
                         browser_view->maho_sidebar_container())
                   : nullptr;
  auto* sidebar =
      container ? views::AsViewClass<MahoSidebarView>(container->sidebar_view())
                : nullptr;
  if (sidebar) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(
                       [](base::WeakPtr<MahoSidebarView> weak_sidebar) {
                         if (weak_sidebar && weak_sidebar->tab_list_view()) {
                           weak_sidebar->tab_list_view()
                               ->FlushSpringLoadedSpaceSwitch();
                         }
                       },
                       sidebar->GetWeakPtr()));
  }
}

void MahoSidebarFooterView::OnSpaceDotDropCompleted() {
  auto* browser_view = BrowserView::GetBrowserViewForBrowser(browser_);
  auto* container =
      browser_view ? static_cast<MahoSidebarContainerView*>(
                         browser_view->maho_sidebar_container())
                   : nullptr;
  auto* sidebar =
      container ? views::AsViewClass<MahoSidebarView>(container->sidebar_view())
                : nullptr;
  if (sidebar) {
    sidebar->RefreshAllSynchronously();
  }
}

void MahoSidebarFooterView::OnSpaceDotContextMenu(
    MahoSidebarSpaceDotView* dot,
    const gfx::Point& screen_point) {
  if (!browser_ || !dot) {
    return;
  }
  context_menu_runner_.reset();
  active_context_menu_model_.reset();
  active_space_context_menu_.reset();

  active_space_context_menu_ =
      std::make_unique<MahoSpaceContextMenu>(browser_, dot->space_id());
  active_context_menu_model_ = active_space_context_menu_->BuildMenuModel();
  context_menu_runner_ = std::make_unique<views::MenuRunner>(
      active_context_menu_model_.get(), views::MenuRunner::CONTEXT_MENU);
  context_menu_runner_->RunMenuAt(
      dot->GetWidget(), nullptr,
      gfx::Rect(screen_point, gfx::Size()),
      views::MenuAnchorPosition::kTopLeft,
      ui::mojom::MenuSourceType::kMouse);
}

void MahoSidebarFooterView::OnSpaceDotDragStarted(
    MahoSidebarSpaceDotView* dot,
    const gfx::Point& press_point) {
  (void)press_point;
  if (!dot || !dot_container_) {
    return;
  }
  const auto& children = dot_container_->children();
  drag_source_index_ = -1;
  for (size_t i = 0; i < children.size(); ++i) {
    if (children[i] == dot) {
      drag_source_index_ = static_cast<int>(i);
      break;
    }
  }
}

void MahoSidebarFooterView::OnSpaceDotDragMoved(
    MahoSidebarSpaceDotView* dot,
    const gfx::Point& screen_point) {
  (void)dot;
  if (drag_source_index_ < 0 || !dot_container_) {
    return;
  }
  gfx::Point local_point = screen_point;
  views::View::ConvertPointFromScreen(dot_container_, &local_point);
  int target = GetDropTargetIndex(local_point);
  ShowDropIndicator(target);
}

void MahoSidebarFooterView::OnSpaceDotDragEnded(
    MahoSidebarSpaceDotView* dot,
    bool cancelled) {
  (void)dot;
  if (!cancelled && drag_source_index_ >= 0 && drop_indicator_ &&
      dot_container_) {
    gfx::Point indicator_center = drop_indicator_->bounds().CenterPoint();
    views::View::ConvertPointToTarget(spaces_cluster_, dot_container_,
                                      &indicator_center);
    int target = GetDropTargetIndex(indicator_center);
    if (target != drag_source_index_ && target != drag_source_index_ + 1) {
      PerformReorder(drag_source_index_, target);
    }
  }
  HideDropIndicator();
  drag_source_index_ = -1;
}

int MahoSidebarFooterView::GetDropTargetIndex(
    const gfx::Point& point_in_dot_container) const {
  if (!dot_container_) {
    return 0;
  }
  const auto& children = dot_container_->children();
  const int count = static_cast<int>(children.size());
  if (count == 0) {
    return 0;
  }

  for (int i = 0; i < count; ++i) {
    const gfx::Rect bounds = children[i]->bounds();
    if (point_in_dot_container.x() < bounds.CenterPoint().x()) {
      return i;
    }
  }
  return count;
}

void MahoSidebarFooterView::ShowDropIndicator(int target_index) {
  if (!dot_container_) {
    return;
  }

  constexpr int kIndicatorWidth = 2;
  constexpr int kIndicatorHeight = 10;
  constexpr int kIndicatorRadius = 1;

  if (!drop_indicator_) {
    auto indicator = std::make_unique<views::View>();
    indicator->SetBackground(views::CreateRoundedRectBackground(
        palette_.focus_ring, kIndicatorRadius));
    indicator->SetPaintToLayer();
    indicator->layer()->SetFillsBoundsOpaquely(false);
    drop_indicator_ = spaces_cluster_->AddChildView(std::move(indicator));
  }

  const auto& children = dot_container_->children();
  const int count = static_cast<int>(children.size());
  int x_in_dots = 0;
  if (target_index <= 0) {
    x_in_dots = children.empty() ? 0
                                 : children[0]->bounds().x() - kIndicatorWidth - 1;
  } else if (target_index >= count) {
    x_in_dots = children.back()->bounds().right() + 1;
  } else {
    const int left_edge = children[target_index - 1]->bounds().right();
    const int right_edge = children[target_index]->bounds().x();
    x_in_dots = (left_edge + right_edge - kIndicatorWidth) / 2;
  }

  gfx::Point cluster_origin;
  views::View::ConvertPointToTarget(dot_container_, spaces_cluster_,
                                    &cluster_origin);
  const int x = x_in_dots + cluster_origin.x();
  const int y = (spaces_cluster_->height() - kIndicatorHeight) / 2;
  drop_indicator_->SetBoundsRect(
      gfx::Rect(x, y, kIndicatorWidth, kIndicatorHeight));
  drop_indicator_->SetVisible(true);
}

void MahoSidebarFooterView::HideDropIndicator() {
  if (drop_indicator_) {
    auto removed = spaces_cluster_->RemoveChildViewT(drop_indicator_.get());
    drop_indicator_ = nullptr;
  }
}

void MahoSidebarFooterView::PerformReorder(int from_index, int to_index) {
  MahoCore* core = maho::GetCore();
  if (!core || !dot_container_) {
    return;
  }
  const auto& children = dot_container_->children();
  if (from_index < 0 ||
      from_index >= static_cast<int>(children.size())) {
    return;
  }
  auto* dot = static_cast<MahoSidebarSpaceDotView*>(children[from_index]);
  if (dot->space_id().empty()) {
    return;
  }
  ReorderSpaceForTesting(core, dot->space_id(), from_index, to_index);
}

void MahoSidebarFooterView::ReorderSpaceForTesting(
    ::MahoCore* core,
    const std::string& space_id,
    int from_index,
    int to_index) {
  maho_core_reorder_space(core, space_id.c_str(),
                          static_cast<uintptr_t>(from_index),
                          static_cast<uintptr_t>(to_index));
  SidebarCacheInvalidation invalidation;
  invalidation.fragments = SidebarCoreFragment::kFooter;
  InvalidateSidebarCoreCache(invalidation);
}

views::View* MahoSidebarFooterView::GetSpaceDotForTesting(int index) {
  if (!dot_container_ ||
      index < 0 ||
      index >= static_cast<int>(dot_container_->children().size())) {
    return nullptr;
  }
  return dot_container_->children()[index];
}

void MahoSidebarFooterView::UpdateFadeMask() {
  if (!fade_mask_ || !scroll_view_) {
    return;
  }

  fade_mask_->SetBoundsRect(scroll_view_->bounds());

  const int scroll_x = scroll_view_->GetVisibleRect().x();
  const int max_scroll = scroll_view_->contents()->width() -
                         scroll_view_->GetVisibleRect().width();
  const bool show_left = scroll_x > 2;
  const bool show_right = max_scroll > 0 && scroll_x < max_scroll - 2;

  static_cast<MahoSidebarFadeMaskView*>(fade_mask_.get())
      ->SetFadeState(show_left, show_right,
                     palette_.surface_stops.empty()
                         ? SK_ColorTRANSPARENT
                         : palette_.surface_stops.front());
}

void MahoSidebarFooterView::ScrollToActiveSpace() {
  if (!scroll_view_ || !dot_container_ || selected_dot_index_ < 0) {
    return;
  }

  const auto& children = dot_container_->children();
  if (selected_dot_index_ >= static_cast<int>(children.size())) {
    return;
  }

  views::View* active_dot = children[selected_dot_index_];
  const gfx::Rect dot_bounds = active_dot->bounds();
  const gfx::Rect visible = scroll_view_->GetVisibleRect();

  if (dot_bounds.x() < visible.x()) {
    scroll_view_->ScrollToPosition(scroll_view_->horizontal_scroll_bar(),
                                   dot_bounds.x());
  } else if (dot_bounds.right() > visible.right()) {
    scroll_view_->ScrollToPosition(
        scroll_view_->horizontal_scroll_bar(),
        dot_bounds.right() - visible.width());
  }
}

bool MahoSidebarFooterView::OnKeyPressed(const ui::KeyEvent& event) {
  const auto& children = dot_container_->children();
  const int count = static_cast<int>(children.size());
  if (count == 0) {
    return false;
  }

  if (event.key_code() == ui::VKEY_LEFT) {
    if (selected_dot_index_ <= 0) {
      return true;
    }
    selected_dot_index_--;
    UpdateDotSelectionState();
    auto* dot = static_cast<MahoSidebarSpaceDotView*>(
        children[selected_dot_index_]);
    if (!dot->space_id().empty()) {
      MahoSidebarView::ActivateSpaceAndTab(browser_, dot->space_id());
    }
    ScrollToActiveSpace();
    UpdateFadeMask();
    return true;
  }

  if (event.key_code() == ui::VKEY_RIGHT) {
    if (selected_dot_index_ >= count - 1) {
      return true;
    }
    selected_dot_index_++;
    UpdateDotSelectionState();
    auto* dot = static_cast<MahoSidebarSpaceDotView*>(
        children[selected_dot_index_]);
    if (!dot->space_id().empty()) {
      MahoSidebarView::ActivateSpaceAndTab(browser_, dot->space_id());
    }
    ScrollToActiveSpace();
    UpdateFadeMask();
    return true;
  }

  if (event.key_code() == ui::VKEY_RETURN ||
      event.key_code() == ui::VKEY_SPACE) {
    if (selected_dot_index_ >= 0 && selected_dot_index_ < count) {
      auto* dot = static_cast<MahoSidebarSpaceDotView*>(
          children[selected_dot_index_]);
      if (!dot->space_id().empty()) {
        MahoSidebarView::ActivateSpaceAndTab(browser_, dot->space_id());
      }
    }
    return true;
  }

  return false;
}

namespace {

constexpr base::TimeDelta kWheelGestureCooldown = base::Milliseconds(300);
constexpr int kPrecisionScrollThreshold = 40;

}  // namespace

int MahoSidebarFooterView::ClampDotIndex(int index) const {
  if (!dot_container_ || dot_container_->children().empty()) {
    return -1;
  }
  const int count = static_cast<int>(dot_container_->children().size());
  return std::clamp(index, 0, count - 1);
}

MahoSidebarSpaceDotView* MahoSidebarFooterView::GetDotViewAt(int index) const {
  if (!dot_container_) {
    return nullptr;
  }
  const auto& children = dot_container_->children();
  if (index < 0 || static_cast<size_t>(index) >= children.size()) {
    return nullptr;
  }
  return views::AsViewClass<MahoSidebarSpaceDotView>(children[index]);
}

void MahoSidebarFooterView::ResetWheelGestureLatch() {
  wheel_gesture_latched_ = false;
  accumulated_wheel_delta_ = 0;
}

bool MahoSidebarFooterView::OnMouseWheel(const ui::MouseWheelEvent& event) {
  if (!browser_ || !dot_container_) {
    return false;
  }

  const auto& children = dot_container_->children();
  const int count = static_cast<int>(children.size());
  if (count <= 1) {
    return false;
  }

  // If already latched during momentum scroll, reset timer and consume event.
  if (wheel_gesture_latched_) {
    wheel_cooldown_timer_.Start(
        FROM_HERE, kWheelGestureCooldown,
        base::BindOnce(&MahoSidebarFooterView::ResetWheelGestureLatch,
                       weak_factory_.GetWeakPtr()));
    return true;
  }

  // Interpret horizontal offset first; fallback to vertical for shift+scroll or wheel
  int raw_delta = event.x_offset();
  if (raw_delta == 0) {
    raw_delta = event.y_offset();
  }

  if (raw_delta == 0) {
    return false;
  }

  // Reset accumulation if direction flips
  if ((accumulated_wheel_delta_ > 0) != (raw_delta > 0) &&
      accumulated_wheel_delta_ != 0) {
    accumulated_wheel_delta_ = 0;
  }
  accumulated_wheel_delta_ += raw_delta;

  const int threshold = (std::abs(raw_delta) >= ui::MouseWheelEvent::kWheelDelta)
                            ? ui::MouseWheelEvent::kWheelDelta
                            : kPrecisionScrollThreshold;

  if (std::abs(accumulated_wheel_delta_) < threshold) {
    return true;
  }

  // Clamp current selected index safely
  int current_index = ClampDotIndex(selected_dot_index_);
  if (current_index < 0) {
    current_index = 0;
  }

  int target_index = current_index;
  if (accumulated_wheel_delta_ < 0) {
    // Scroll right / down -> Next space
    if (target_index < count - 1) {
      target_index++;
    }
  } else {
    // Scroll left / up -> Previous space
    if (target_index > 0) {
      target_index--;
    }
  }

  if (target_index != selected_dot_index_) {
    // Latch gesture to prevent rapid skipping across multiple spaces
    wheel_gesture_latched_ = true;
    wheel_cooldown_timer_.Start(
        FROM_HERE, kWheelGestureCooldown,
        base::BindOnce(&MahoSidebarFooterView::ResetWheelGestureLatch,
                       weak_factory_.GetWeakPtr()));

    selected_dot_index_ = target_index;
    UpdateDotSelectionState();
    auto* dot = GetDotViewAt(selected_dot_index_);
    if (dot && !dot->space_id().empty()) {
      MahoSidebarView::ActivateSpaceAndTab(browser_, dot->space_id());
    }
    ScrollToActiveSpace();
    UpdateFadeMask();
    return true;
  }

  return false;
}

}  // namespace maho
