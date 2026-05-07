// Copyright 2026 Maho Browser. All rights reserved.

#include "maho_sidebar_footer_view.h"

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <vector>

#include "base/json/json_writer.h"
#include "base/values.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/third_party/maho/maho_ffi.h"

#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/browser.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/common/referrer.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_action_popover_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_view.h"
#include "maho/browser/ui/views/space_manage/maho_space_manage_dialog.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/browser/ui/context_menu/maho_space_context_menu.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/ui_base_types.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "ui/color/color_id.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/label.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/style/typography.h"
#include "ui/views/view_class_properties.h"
#include "url/gurl.h"

namespace maho {

namespace {

constexpr int kUpdatePillCornerRadius = 11;
constexpr int kUpdateIndicatorSize = 5;
constexpr int kFooterControlSize = 24;
constexpr int kClusterCornerRadius = 12;
constexpr int kClusterSpacing = 4;
constexpr int kLibraryButtonCornerRadius = 12;
constexpr int kSpaceIconSize = 18;
constexpr int kSpaceIconCornerRadius = 9;
constexpr int kSpaceTintAlpha = 24;

const gfx::Insets kFooterInsets = gfx::Insets::TLBR(2, 0, 0, 0);
const gfx::Insets kUpdatePillInsets = gfx::Insets::TLBR(3, 10, 3, 10);
const gfx::Insets kClusterInsets = gfx::Insets::VH(2, 5);

constexpr std::array<const char16_t*, 8> kSpaceEmojiIcons = {
    u"🏠", u"💼", u"🎨", u"🎮", u"📚", u"🎵", u"🌟", u"⚡"};

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

SkColor BlendTint(SkColor color, SkColor surface) {
  const int tint = kSpaceTintAlpha;
  const int base_alpha = 255 - tint;
  return SkColorSetRGB(
      (SkColorGetR(color) * tint + SkColorGetR(surface) * base_alpha) / 255,
      (SkColorGetG(color) * tint + SkColorGetG(surface) * base_alpha) / 255,
      (SkColorGetB(color) * tint + SkColorGetB(surface) * base_alpha) / 255);
}

std::u16string GetSpaceEmojiForIndex(int index) {
  if (index < 0) {
    return u"✨";
  }
  return kSpaceEmojiIcons[index % kSpaceEmojiIcons.size()];
}

views::LabelButton* CreateIconPillButton(
    views::View* parent,
    views::Button::PressedCallback callback,
    const gfx::VectorIcon& icon,
    const std::u16string& tooltip) {
  auto button = std::make_unique<views::LabelButton>(std::move(callback), u"");
  button->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  button->SetMinSize(gfx::Size(kFooterControlSize, kFooterControlSize));
  button->SetRequestFocusOnPress(false);
  button->SetImageModel(
      views::Button::STATE_NORMAL,
      ui::ImageModel::FromVectorIcon(icon, ui::kColorSysOnSurface,
                                     sidebar_layout::kTopBarIconSizeDp));
  button->SetTooltipText(tooltip);
  button->SetAccessibleName(tooltip);
  button->SetLabelStyle(views::style::STYLE_BODY_4_MEDIUM);
  button->SetTextColor(views::Button::STATE_NORMAL, ui::kColorSysOnSurfaceSubtle);
  button->SetBackground(views::CreateRoundedRectBackground(
      ui::kColorSysSurface1, kLibraryButtonCornerRadius));
  button->SetBorder(views::CreateRoundedRectBorder(
      1, kLibraryButtonCornerRadius, ui::kColorSysSurfaceVariant));
  return parent->AddChildView(std::move(button));
}

views::MdTextButton* CreateRoundActionButton(
    views::View* parent,
    views::Button::PressedCallback callback,
    const std::u16string& text,
    const std::u16string& tooltip) {
  auto button = std::make_unique<views::MdTextButton>(std::move(callback), text);
  button->SetStyle(ui::ButtonStyle::kTonal);
  button->SetCornerRadius(kFooterControlSize / 2);
  button->SetMinSize(gfx::Size(kFooterControlSize, kFooterControlSize));
  button->SetMaxSize(gfx::Size(kFooterControlSize, kFooterControlSize));
  button->SetTooltipText(tooltip);
  button->SetAccessibleName(tooltip);
  return parent->AddChildView(std::move(button));
}

void DispatchCreateFolderForSpace(const std::string& space_id) {
  MahoCore* core = maho::GetCore();
  if (!core || space_id.empty()) {
    return;
  }

  base::DictValue event;
  event.Set("kind", "create_folder");
  event.Set("space_id", space_id);

  std::string event_json;
  base::JSONWriter::Write(event, &event_json);
  char* result = maho_core_handle_event(core, event_json.c_str());
  if (result) {
    maho_string_free(result);
  }
}

}  // namespace

BEGIN_METADATA(MahoSidebarFooterView)
END_METADATA

MahoSidebarFooterView::MahoSidebarFooterView(Browser* browser)
    : browser_(browser) {
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, kFooterInsets, 7));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  update_pill_ = AddChildView(std::make_unique<views::View>());
  auto* update_pill_layout =
      update_pill_->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, kUpdatePillInsets, 6));
  update_pill_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  update_pill_->SetBackground(views::CreateRoundedRectBackground(
      ui::kColorSysSurface1, kUpdatePillCornerRadius));
  update_pill_->SetBorder(views::CreateRoundedRectBorder(
      1, kUpdatePillCornerRadius, ui::kColorSysSurfaceVariant));

  update_pill_indicator_ =
      update_pill_->AddChildView(std::make_unique<views::View>());
  update_pill_indicator_->SetPreferredSize(
      gfx::Size(kUpdateIndicatorSize, kUpdateIndicatorSize));
  update_pill_indicator_->SetBackground(views::CreateRoundedRectBackground(
      ui::kColorSysPrimary, kUpdateIndicatorSize / 2));

  update_pill_label_ = update_pill_->AddChildView(
      std::make_unique<views::Label>(u"Maho is up to date"));
  update_pill_label_->SetAutoColorReadabilityEnabled(false);
  update_pill_label_->SetEnabledColor(ui::kColorSysOnSurfaceSubtle);
  update_pill_label_->SetTextStyle(views::style::STYLE_BODY_5);
  update_pill_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);

  auto* footer_row = AddChildView(std::make_unique<views::View>());
  auto* footer_row_layout =
      footer_row->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(),
          kClusterSpacing));
  footer_row_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  library_button_ = CreateIconPillButton(
      footer_row,
      base::BindRepeating(&MahoSidebarFooterView::OnLibraryPressed,
                          weak_factory_.GetWeakPtr()),
      vector_icons::kGlobeIcon, u"Open spaces");

  spaces_cluster_ = footer_row->AddChildView(std::make_unique<views::View>());
  auto* cluster_layout =
      spaces_cluster_->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, kClusterInsets,
          kClusterSpacing));
  cluster_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  footer_row_layout->SetFlexForView(spaces_cluster_, 1);

  dot_container_ = spaces_cluster_->AddChildView(std::make_unique<views::View>());
  auto* dots_layout =
      dot_container_->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal,
          gfx::Insets(),
          kClusterSpacing));
  dots_layout->set_main_axis_alignment(
      views::BoxLayout::MainAxisAlignment::kCenter);
  dots_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  plus_button_ = CreateRoundActionButton(
      footer_row,
      base::BindRepeating(&MahoSidebarFooterView::OnPlusPressed,
                          weak_factory_.GetWeakPtr()),
      u"+", u"Create space");

  spaces_cluster_->set_context_menu_controller(this);
  UpdateFooterChromeAppearance();
  UpdateUpdatePill(MahoSidebarFooterUpdatePillModel());
  RebuildDots(0, -1, {}, {}, {});
}

MahoSidebarFooterView::~MahoSidebarFooterView() {
  CloseActionPopover();
}

void MahoSidebarFooterView::Update(const MahoSidebarFooterModel& model) {
  UpdateUpdatePill(model.update_pill);
  library_button_->SetEnabled(model.can_open_space_controls);
  plus_button_->SetEnabled(model.can_create_space);
  active_space_color_ = model.space_color;
  UpdateFooterChromeAppearance();
  RebuildDots(model.space_count, model.active_space_index, model.space_icons,
              model.space_names, model.space_colors);
}

void MahoSidebarFooterView::ShowContextMenuForViewImpl(
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

void MahoSidebarFooterView::OnLibraryPressed(const ui::Event& event) {
  (void)event;
  OpenLibrarySurface();
}

void MahoSidebarFooterView::OnPlusPressed(const ui::Event& event) {
  (void)event;
  OpenSpaceCreationSurface();
}

void MahoSidebarFooterView::OnSpaceIconPressed(int index, const ui::Event& event) {
  (void)event;
  SwitchToSpaceAtIndex(index);
}

void MahoSidebarFooterView::OpenLibrarySurface() {
  maho::MahoSpaceManageDialog::Open(browser_);
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
      auto* sidebar_view = static_cast<MahoSidebarView*>(
          parent() && parent()->parent() ? parent()->parent() : nullptr);
      if (sidebar_view) {
        sidebar_view->ShowCreateSpace();
      }
      break;
    }
    case MahoSidebarActionPopoverAction::kNewFolder: {
      auto* bridge = maho::MahoSpaceProfileBridge::GetInstance();
      if (bridge) {
        DispatchCreateFolderForSpace(bridge->GetActiveSpaceId());
      }
      break;
    }
    case MahoSidebarActionPopoverAction::kNewTab:
      if (browser_) {
        browser_->OpenURL(content::OpenURLParams(
            GURL("chrome://newtab"), content::Referrer(),
            WindowOpenDisposition::NEW_FOREGROUND_TAB,
            ui::PAGE_TRANSITION_TYPED, false),
            base::NullCallback());
      }
      break;
  }
}

void MahoSidebarFooterView::CloseActionPopover() {
  if (action_popover_widget_) {
    action_popover_widget_->RemoveObserver(this);
    action_popover_widget_->Close();
    action_popover_widget_ = nullptr;
  }
}

void MahoSidebarFooterView::OnWidgetDestroying(views::Widget* widget) {
  if (widget == action_popover_widget_) {
    action_popover_widget_ = nullptr;
  }
}

void MahoSidebarFooterView::RebuildDots(
    int space_count,
    int active_index,
    const std::vector<std::string>& space_icons,
    const std::vector<std::string>& space_names,
    const std::vector<std::string>& space_colors) {
  dot_container_->RemoveAllChildViews();
  auto* bridge = maho::MahoSpaceProfileBridge::GetInstance();
  std::vector<std::string> space_ids = bridge ? bridge->GetSpaceIds()
                                              : std::vector<std::string>();
  const int clamped_count = std::max(space_count, 0);
  const int visible_count = space_ids.empty()
                                ? clamped_count
                                : std::min(clamped_count,
                                           static_cast<int>(space_ids.size()));
  if (visible_count == 0) {
    return;
  }

  auto* layout = static_cast<views::BoxLayout*>(dot_container_->GetLayoutManager());
  if (layout) {
    layout->set_between_child_spacing(kClusterSpacing);
  }

  for (int i = 0; i < visible_count; ++i) {
    const bool active = i == active_index;
    std::u16string emoji;
    if (i < static_cast<int>(space_icons.size()) && !space_icons[i].empty()) {
      emoji = base::UTF8ToUTF16(space_icons[i]);
    } else {
      emoji = GetSpaceEmojiForIndex(i);
    }
    auto button = std::make_unique<views::LabelButton>(
        base::BindRepeating(&MahoSidebarFooterView::OnSpaceIconPressed,
                            weak_factory_.GetWeakPtr(), i),
        emoji);
    button->SetHorizontalAlignment(gfx::ALIGN_CENTER);
    SkColor fill_color = active ? ui::kColorSysSurface1 : ui::kColorSysSurface;
    SkColor parsed_color = fill_color;
    if (i < static_cast<int>(space_colors.size()) &&
        ParseHexColor(space_colors[i], &parsed_color)) {
      const auto* cp = GetColorProvider();
      const SkColor surface = cp ? cp->GetColor(ui::kColorSysSurface3)
                                 : SkColorSetRGB(0xF0, 0xF0, 0xF0);
      fill_color = BlendTint(parsed_color, surface);
    }
    button->SetBorder(active ? views::CreateRoundedRectBorder(
                                  1, kSpaceIconCornerRadius,
                                  ui::kColorSysNeutralOutline)
                             : nullptr);
    button->SetBackground(views::CreateRoundedRectBackground(
        fill_color,
        kSpaceIconCornerRadius));
    button->SetRequestFocusOnPress(false);
    button->SetImageLabelSpacing(0);
    button->SetMinSize(gfx::Size(kSpaceIconSize, kSpaceIconSize));
    button->SetMaxSize(gfx::Size(kSpaceIconSize, kSpaceIconSize));
    std::u16string space_name =
        (i < static_cast<int>(space_names.size()) && !space_names[i].empty())
            ? base::UTF8ToUTF16(space_names[i])
            : u"Switch space";
    button->SetTooltipText(space_name);
    button->SetAccessibleName(space_name);
    button->SetLabelStyle(views::style::STYLE_BODY_4_MEDIUM);
    button->SetTextColor(views::Button::STATE_NORMAL, ui::kColorSysOnSurface);
    button->SetTextColor(views::Button::STATE_HOVERED, ui::kColorSysOnSurface);
    button->SetTextColor(views::Button::STATE_PRESSED, ui::kColorSysOnSurface);
    dot_container_->AddChildView(std::move(button));
  }
}

void MahoSidebarFooterView::UpdateFooterChromeAppearance() {
  // Resolve ColorId to actual SkColor via the widget's ColorProvider.
  const auto* cp = GetColorProvider();
  const SkColor surface = cp ? cp->GetColor(ui::kColorSysSurface3)
                             : SkColorSetRGB(0xF0, 0xF0, 0xF0);
  SkColor tint = surface;
  SkColor parsed = tint;
  if (!active_space_color_.empty() && ParseHexColor(active_space_color_, &parsed)) {
    tint = BlendTint(parsed, surface);
  }
  SetBackground(nullptr);
  spaces_cluster_->SetBackground(views::CreateRoundedRectBackground(
      tint, kClusterCornerRadius));
  spaces_cluster_->SetBorder(views::CreateRoundedRectBorder(
      1, kClusterCornerRadius, ui::kColorSysSurfaceVariant));
}

void MahoSidebarFooterView::SwitchToSpaceAtIndex(int index) {
  auto* bridge = maho::MahoSpaceProfileBridge::GetInstance();
  if (!bridge) {
    return;
  }

  const std::vector<std::string> space_ids = bridge->GetSpaceIds();
  if (index < 0 || index >= static_cast<int>(space_ids.size())) {
    return;
  }

  bridge->SwitchToSpace(space_ids[index]);
}

views::View* MahoSidebarFooterView::GetSpaceDotForTesting(int index) {
  if (!dot_container_ ||
      index < 0 ||
      index >= static_cast<int>(dot_container_->children().size())) {
    return nullptr;
  }
  return dot_container_->children()[index];
}

}  // namespace maho
