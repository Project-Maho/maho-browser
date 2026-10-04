// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/theme/maho_color_id.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_favorites_grid_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_view.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <optional>
#include <set>

#include "base/containers/flat_map.h"
#include "base/auto_reset.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/logging.h"
#include "base/memory/raw_ptr.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/tab_ui_helper.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/favicon/favicon_service_factory.h"
#include "components/favicon/core/favicon_service.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/favicon_status.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/page_navigator.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/referrer.h"
#include "components/vector_icons/vector_icons.h"
#include "maho/browser/maho_tab_id_helper.h"
#include "maho/browser/ui/context_menu/maho_favorites_context_menu.h"
#include "maho/browser/ui/notifications/maho_notification_overlay.h"  // nogncheck
#if BUILDFLAG(IS_MAC)
#include "maho/browser/ui/notifications/maho_notification_share_mac.h"  // nogncheck
#endif
#include "maho/browser/ui/views/maho_lucide_icons/vector_icons.h"
#include "maho/browser/ui/views/sidebar/maho_favorite_edit_dialog.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_state_adapter.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_tab_list_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_drag_util.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_drop_planner.h"
#include "ui/base/clipboard/clipboard_format_type.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/dragdrop/mojom/drag_drop_types.mojom.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/events/event_constants.h"
#include "ui/base/models/image_model.h"
#include "ui/base/mojom/menu_source_type.mojom-shared.h"
#include "cc/paint/path_effect.h"
#include "ui/base/ui_base_types.h"
#include "ui/base/window_open_disposition.h"
#include "ui/color/color_id.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_animator.h"
#include "ui/compositor/layer_tree_owner.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_analysis.h"
#include "ui/gfx/font.h"
#include "ui/gfx/font_list.h"
#include "ui/color/color_variant.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/size.h"
#include "ui/display/screen.h"
#include "cc/paint/paint_flags.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/context_menu_controller.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/paint_info.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/image_button_factory.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/drag_controller.h"
#include "ui/views/animation/animation_builder.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/style/typography.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

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

constexpr int kFavoriteSectionSpacingDp = 6;
constexpr int kFavoriteShellContentSpacingDp = 0;
constexpr int kFavoriteTileContentSpacingDp = 0;
constexpr int kFavoriteSingleCardHeightDp = sidebar_layout::kFavoriteTileHeightDp;
constexpr int kFavoriteTwoTileHeightDp = sidebar_layout::kFavoriteTileHeightDp;
constexpr int kFavoriteEmptyStateHeightDp = 108;
constexpr int kFavoriteEmptyBadgeSizeDp = 24;
constexpr int kFavoriteEmptyBadgeIconSizeDp = 14;
constexpr int kFavoriteEmptyCardCornerRadiusDp = 15;
constexpr int kFavoriteEmptyCardTopInsetDp = 12;
constexpr int kFavoriteEmptyCardSideInsetDp = 18;
constexpr int kFavoriteEmptyBadgeTopInsetDp = 1;
constexpr int kFavoriteEmptyTitleTopInsetDp = 18;
constexpr int kFavoriteEmptyTitleDescriptionSpacingDp = 6;
constexpr int kFavoriteEmptyCloseSizeDp = 12;
constexpr int kFavoriteEmptyCloseHitTargetDp = 22;
constexpr int kFavoriteEmptyCloseInsetDp = 8;
constexpr int kDragStartThresholdDp = 5;
constexpr int kFavoriteCustomGlyphFontSizeDelta = 6;

constexpr base::TimeDelta kReorderDuration = base::Milliseconds(220);
constexpr base::TimeDelta kInsertionDuration = base::Milliseconds(220);
constexpr base::TimeDelta kRemovalDuration = base::Milliseconds(180);
constexpr float kInsertionStartScale = 0.7f;
constexpr float kRemovalEndScale = 0.7f;

// Private windows carry no resolved palette (transparent roles); they keep
// the platform ColorId so the glyph stays visible there.
ui::ColorVariant FavoritesGlyphColor(const MahoSidebarPalette& palette) {
  if (palette.neutral_glyph != SK_ColorTRANSPARENT) {
    return palette.neutral_glyph;
  }
  return ui::kColorSysOnSurfaceSubtle;
}

ui::ImageModel CreateThemedGlobeIcon(const MahoSidebarPalette& palette,
                                     int size) {
  return ui::ImageModel::FromVectorIcon(vector_icons::kGlobeIcon,
                                        FavoritesGlyphColor(palette), size);
}

class EmptyFavoritesPlaceholderView : public views::View {
  METADATA_HEADER(EmptyFavoritesPlaceholderView, views::View)

 public:
  explicit EmptyFavoritesPlaceholderView(
      base::RepeatingClosure on_dismiss = base::RepeatingClosure())
      : on_dismiss_(std::move(on_dismiss)) {
    SetPreferredSize(gfx::Size(1, kFavoriteEmptyStateHeightDp));

    card_background_ = AddChildView(std::make_unique<views::View>());
    card_background_->SetCanProcessEventsWithinSubtree(false);

    badge_ = AddChildView(std::make_unique<views::View>());
    badge_->SetPreferredSize(
        gfx::Size(kFavoriteEmptyBadgeSizeDp, kFavoriteEmptyBadgeSizeDp));
    auto* badge_layout =
        badge_->SetLayoutManager(std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 0));
    badge_layout->set_main_axis_alignment(
        views::BoxLayout::MainAxisAlignment::kCenter);
    badge_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);

    badge_image_ = badge_->AddChildView(std::make_unique<views::ImageView>());
    badge_image_->SetImageSize(
        gfx::Size(kFavoriteEmptyBadgeIconSizeDp,
                  kFavoriteEmptyBadgeIconSizeDp));

    // Glyph images for every state are bound in UpdateAppearance().
    close_button_ = AddChildView(views::CreateVectorImageButton(
        base::BindRepeating(&EmptyFavoritesPlaceholderView::OnClosePressed,
                            base::Unretained(this))));
    close_button_->SetPreferredSize(
        gfx::Size(kFavoriteEmptyCloseHitTargetDp,
                  kFavoriteEmptyCloseHitTargetDp));
    close_button_->SetImageHorizontalAlignment(
        views::ImageButton::ALIGN_CENTER);
    close_button_->SetImageVerticalAlignment(
        views::ImageButton::ALIGN_MIDDLE);
    close_button_->SetTooltipText(u"Dismiss");
    close_button_->SetAccessibleName(u"Dismiss favorites hint");
    views::InstallCircleHighlightPathGenerator(close_button_);

    title_label_ = AddChildView(std::make_unique<views::Label>(
        std::u16string(u"Drag to add Favorites")));
    title_label_->SetAutoColorReadabilityEnabled(false);
    title_label_->SetTextStyle(views::style::STYLE_BODY_3_MEDIUM);
    title_label_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
    // Required for kGlassFrame: BrowserFrameView is non-opaque, so
    // Label::PaintText's opacity DCHECK trips without this suppression.
    title_label_->SetSubpixelRenderingEnabled(false);

    description_label_ = AddChildView(std::make_unique<views::Label>(
        std::u16string(u"Favorites keep your most used sites\nand apps close")));
    description_label_->SetAutoColorReadabilityEnabled(false);
    description_label_->SetTextStyle(views::style::STYLE_BODY_5_MEDIUM);
    description_label_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
    description_label_->SetMultiLine(true);
    description_label_->SetSubpixelRenderingEnabled(false);

    UpdateAppearance();
  }

  void Layout(PassKey) override {
    LayoutSuperclass<views::View>(this);
    const gfx::Rect bounds = GetContentsBounds();
    const int badge_x = bounds.x() +
                        (bounds.width() - kFavoriteEmptyBadgeSizeDp) / 2;
    badge_->SetBounds(badge_x, kFavoriteEmptyBadgeTopInsetDp,
                      kFavoriteEmptyBadgeSizeDp,
                      kFavoriteEmptyBadgeSizeDp);

    const int card_top =
        (kFavoriteEmptyBadgeSizeDp / 2) + kFavoriteEmptyBadgeTopInsetDp;
    const gfx::Rect card_bounds(bounds.x(), card_top, bounds.width(),
                                std::max(0, bounds.height() - card_top));
    card_background_->SetBoundsRect(card_bounds);

    close_button_->SetBounds(card_bounds.right() - kFavoriteEmptyCloseInsetDp -
                                 kFavoriteEmptyCloseHitTargetDp,
                             card_bounds.y() + kFavoriteEmptyCloseInsetDp,
                             kFavoriteEmptyCloseHitTargetDp,
                             kFavoriteEmptyCloseHitTargetDp);

    const int content_width =
        std::max(0, card_bounds.width() - (kFavoriteEmptyCardSideInsetDp * 2));
    const gfx::Size title_size = title_label_->GetPreferredSize();
    const int title_x =
        card_bounds.x() +
        std::max(0, (card_bounds.width() - title_size.width()) / 2);
    const int title_y = card_bounds.y() + kFavoriteEmptyTitleTopInsetDp;
    title_label_->SetBounds(title_x, title_y, title_size.width(),
                            title_size.height());

    const int description_y = title_y + title_size.height() +
                              kFavoriteEmptyTitleDescriptionSpacingDp;
    description_label_->SetBounds(
        card_bounds.x() + kFavoriteEmptyCardSideInsetDp, description_y,
        content_width, description_label_->GetHeightForWidth(content_width));
  }

  void SetHighlighted(bool highlighted) {
    if (highlighted_ == highlighted) {
      return;
    }
    highlighted_ = highlighted;
    UpdateAppearance();
  }

  void SetSidebarPalette(const MahoSidebarPalette& palette) {
    palette_ = palette;
    UpdateAppearance();
  }

 private:
  void OnClosePressed() {
    if (on_dismiss_) {
      on_dismiss_.Run();
    }
  }

  void UpdateAppearance() {
    if (palette_.primary_text == SK_ColorTRANSPARENT) {
      card_background_->SetBackground(views::CreateRoundedRectBackground(
          ui::kColorSysSurface1, kFavoriteEmptyCardCornerRadiusDp));
      card_background_->SetBorder(views::CreateRoundedRectBorder(
          1, kFavoriteEmptyCardCornerRadiusDp,
          highlighted_ ? ui::kColorSysPrimary : ui::kColorSysSurfaceVariant));
      badge_->SetBackground(views::CreateRoundedRectBackground(
          ui::kColorSysSurface, kFavoriteEmptyBadgeSizeDp / 2));
      badge_->SetBorder(views::CreateRoundedRectBorder(
          1, kFavoriteEmptyBadgeSizeDp / 2,
          highlighted_ ? ui::kColorSysPrimary : ui::kColorSysSurfaceVariant));
      badge_image_->SetImage(ui::ImageModel::FromVectorIcon(
          maho_lucide_icons::kWandSparklesIcon,
          highlighted_ ? ui::kColorSysPrimary : ui::kColorSysOnSurfaceSubtle,
          kFavoriteEmptyBadgeIconSizeDp));
      // Private windows have no resolved palette.
      title_label_->SetEnabledColor(ui::kColorSysOnSurface);
      description_label_->SetEnabledColor(ui::kColorSysOnSurfaceSubtle);
      SetCloseButtonImages(ui::kColorSysOnSurfaceSubtle,
                           ui::kColorSysOnSurface);
      return;
    }
    SetCloseButtonImages(palette_.neutral_glyph, palette_.disabled_text);
    const SkColor border = highlighted_ ? palette_.focus_ring : palette_.outline;
    card_background_->SetBackground(views::CreateRoundedRectBackground(
        palette_.row_selected, kFavoriteEmptyCardCornerRadiusDp));
    card_background_->SetBorder(views::CreateRoundedRectBorder(
        1, kFavoriteEmptyCardCornerRadiusDp, border));
    badge_->SetBackground(views::CreateRoundedRectBackground(
        palette_.row_hover, kFavoriteEmptyBadgeSizeDp / 2));
    badge_->SetBorder(views::CreateRoundedRectBorder(
        1, kFavoriteEmptyBadgeSizeDp / 2, border));
    badge_image_->SetImage(ui::ImageModel::FromVectorIcon(
        maho_lucide_icons::kWandSparklesIcon,
        highlighted_ ? palette_.focus_ring : palette_.neutral_glyph,
        kFavoriteEmptyBadgeIconSizeDp));
    title_label_->SetEnabledColor(palette_.primary_text);
    description_label_->SetEnabledColor(palette_.secondary_text);
  }

  void SetCloseButtonImages(ui::ColorVariant glyph, ui::ColorVariant disabled) {
    const ui::ImageModel normal = ui::ImageModel::FromVectorIcon(
        maho_lucide_icons::kXIcon, glyph, kFavoriteEmptyCloseSizeDp);
    close_button_->SetImageModel(views::Button::STATE_NORMAL, normal);
    close_button_->SetImageModel(views::Button::STATE_HOVERED, normal);
    close_button_->SetImageModel(views::Button::STATE_PRESSED, normal);
    close_button_->SetImageModel(
        views::Button::STATE_DISABLED,
        ui::ImageModel::FromVectorIcon(maho_lucide_icons::kXIcon, disabled,
                                       kFavoriteEmptyCloseSizeDp));
  }

  base::RepeatingClosure on_dismiss_;
  raw_ptr<views::View> card_background_ = nullptr;
  raw_ptr<views::View> badge_ = nullptr;
  raw_ptr<views::ImageView> badge_image_ = nullptr;
  raw_ptr<views::ImageButton> close_button_ = nullptr;
  raw_ptr<views::Label> title_label_ = nullptr;
  raw_ptr<views::Label> description_label_ = nullptr;
  bool highlighted_ = false;
  MahoSidebarPalette palette_;
};

BEGIN_METADATA(EmptyFavoritesPlaceholderView)
END_METADATA

class FavoriteTileButton : public views::Button,
                           public views::DragController,
                           public views::ContextMenuController {
 public:
  FavoriteTileButton(views::Button::PressedCallback callback,
                     base::RepeatingCallback<void(const std::string&)> drag_done_callback,
                     raw_ptr<MahoSidebarFavoritesGridView> drop_target,
                     std::string tab_id,
                     size_t tile_index)
      : views::Button(std::move(callback)),
        drag_done_callback_(std::move(drag_done_callback)),
        drop_target_(drop_target),
        tab_id_(std::move(tab_id)),
        tile_index_(tile_index) {
    UpdateAppearance();
    set_drag_controller(this);
    set_context_menu_controller(this);
  }

  void StateChanged(views::Button::ButtonState old_state) override {
    views::Button::StateChanged(old_state);
    UpdateAppearance();
  }

  void OnEnabledChanged() override {
    views::Button::OnEnabledChanged();
    UpdateAppearance();
  }

  void SetIsActive(bool is_active) {
    if (is_active_ == is_active) {
      return;
    }
    is_active_ = is_active;
    UpdateAppearance();
  }

  void SetSidebarPalette(const MahoSidebarPalette& palette) {
    palette_ = palette;
    visual_state_valid_ = false;
    UpdateAppearance();
  }

  void SetActiveBorderColor(SkColor color) {
    if (has_active_border_color_ && active_border_color_ == color) {
      return;
    }
    active_border_color_ = color;
    has_active_border_color_ = true;
    if (is_active_) {
      UpdateAppearance();
    }
  }

  void ClearActiveBorderColor() {
    if (!has_active_border_color_) {
      return;
    }
    has_active_border_color_ = false;
    if (is_active_) {
      UpdateAppearance();
    }
  }

  void WriteDragDataForView(views::View* sender,
                            const gfx::Point& press_pt,
                            ui::OSExchangeData* data) override {
    if (tab_id_.empty()) {
      return;
    }
    dragging_tab_id_ = tab_id_;
    SidebarDragPayload payload;
    payload.node_kind = SidebarNodeKind::kTab;
    payload.node_id = dragging_tab_id_;
    payload.origin = SidebarDragOrigin::kFavorites;
    WriteMahoDragData(payload, data);
    data->provider().SetDragImage(CreateSidebarDragImage(sender),
                                  press_pt.OffsetFromOrigin());
    // Hide the source tile so the drag preview gap animation works correctly.
    if (layer()) {
      layer()->SetOpacity(0.f);
    }
  }

  int GetDragOperationsForView(views::View* sender,
                               const gfx::Point& p) override {
    if (tab_id_.empty()) {
      return static_cast<int>(ui::mojom::DragOperation::kNone);
    }
    return static_cast<int>(ui::mojom::DragOperation::kMove);
  }

  bool CanStartDragForView(views::View* sender,
                           const gfx::Point& press_pt,
                           const gfx::Point& current_pt) override {
    return !tab_id_.empty() &&
           (press_pt - current_pt).Length() >= kDragStartThresholdDp;
  }

  void set_tab_id(std::string tab_id) { tab_id_ = std::move(tab_id); }

  void ShowContextMenuForViewImpl(views::View* source,
                                  const gfx::Point& point,
                                  ui::mojom::MenuSourceType source_type) override {
    if (drop_target_) {
      drop_target_->ShowTileContextMenu(tile_index_, point, source_type);
    }
  }

  void OnDragDone() override {
    views::Button::OnDragDone();
    if (layer()) {
      layer()->SetOpacity(1.f);
    }
    if (drag_done_callback_) {
      drag_done_callback_.Run(dragging_tab_id_);
    }
    dragging_tab_id_.clear();
  }

  struct TileVisualState {
    SkColor background_color = SK_ColorTRANSPARENT;
    bool is_active = false;
    bool is_hovered = false;
    bool is_pressed = false;
    bool has_active_border_color = false;
    SkColor active_border_color = SK_ColorTRANSPARENT;

    bool operator==(const TileVisualState& o) const {
      return background_color == o.background_color &&
             is_active == o.is_active &&
             is_hovered == o.is_hovered &&
             is_pressed == o.is_pressed &&
             has_active_border_color == o.has_active_border_color &&
             active_border_color == o.active_border_color;
    }
    bool operator!=(const TileVisualState& o) const {
      return !(*this == o);
    }
  };

  void OnThemeChanged() override {
    views::Button::OnThemeChanged();
    visual_state_valid_ = false;
    UpdateAppearance();
  }

 private:
  void UpdateAppearance() {
    const bool enabled = GetEnabled();
    const bool is_press = enabled && GetState() == STATE_PRESSED;
    const bool is_hover = enabled && GetState() == STATE_HOVERED;
    const SkColor palette_background =
        is_press || is_active_ ? palette_.row_active
                               : (is_hover ? palette_.row_hover
                                           : palette_.row_active);

    TileVisualState current_state{palette_background, is_active_, is_hover,
                                  is_press, has_active_border_color_,
                                  active_border_color_};
    if (!visual_state_valid_ || current_state != last_visual_state_) {
      last_visual_state_ = current_state;
      visual_state_valid_ = true;

      SetBackground(views::CreateRoundedRectBackground(
          palette_background, sidebar_layout::kFavoriteTileCornerRadiusDp));
      // 1dp inset reserved unconditionally to prevent icon jitter on active toggle.
      if (is_active_) {
        ui::ColorVariant border_color =
            has_active_border_color_
                ? ui::ColorVariant(active_border_color_)
                : ui::ColorVariant(palette_.row_active);
        SetBorder(views::CreateRoundedRectBorder(
            1, sidebar_layout::kFavoriteTileCornerRadiusDp, border_color));
      } else {
        SetBorder(views::CreateEmptyBorder(gfx::Insets(1)));
      }
    }
  }

  base::RepeatingCallback<void(const std::string&)> drag_done_callback_;
  raw_ptr<MahoSidebarFavoritesGridView> drop_target_ = nullptr;
  std::string tab_id_;
  std::string dragging_tab_id_;
  size_t tile_index_ = 0;
  bool is_active_ = false;
  bool has_active_border_color_ = false;
  SkColor active_border_color_ = SK_ColorTRANSPARENT;
  MahoSidebarPalette palette_;

  TileVisualState last_visual_state_;
  bool visual_state_valid_ = false;
};

struct FavoriteTileSpec {
  raw_ptr<const gfx::VectorIcon> icon;
  std::u16string text;
  // Raw authored custom title (empty = never renamed). Dialog prefill source.
  std::u16string custom_title;
  GURL url;
  std::string tab_id;
  bool is_active = false;
  std::vector<uint8_t> favicon_png_data;
  std::u16string custom_icon;
  std::string pinned_url;
};

std::u16string BuildFavoriteText(const MahoSidebarFavoriteItemModel& item) {
  const std::u16string resolved = MahoDisplayPolicy::ResolveTabDisplayText(
      item.url, item.title);
  if (!resolved.empty()) {
    return resolved;
  }
  if (item.url.is_valid() && !item.url.host().empty() &&
      !MahoDisplayPolicy::IsNtpOrBlankUrl(item.url)) {
    std::string host(item.url.host());
    if (host.rfind("www.", 0) == 0) {
      host.erase(0, 4);
    }
    return base::UTF8ToUTF16(host);
  }
  if (item.url.is_valid() && !MahoDisplayPolicy::IsNtpOrBlankUrl(item.url)) {
    return base::UTF8ToUTF16(item.url.possibly_invalid_spec());
  }
  return std::u16string();
}

std::array<FavoriteTileSpec, MahoSidebarFavoritesGridView::kTileCount>
BuildTileSpecs(const MahoSidebarFavoritesModel& model) {
  std::array<FavoriteTileSpec, MahoSidebarFavoritesGridView::kTileCount> specs = {};
  for (auto& spec : specs) {
    spec.icon = &vector_icons::kGlobeIcon;
  }
  for (size_t index = 0; index < model.items.size() && index < specs.size();
       ++index) {
    specs[index].text = BuildFavoriteText(model.items[index]);
    specs[index].custom_title = model.items[index].custom_title;
    specs[index].url = model.items[index].url;
    specs[index].tab_id = model.items[index].tab_id;
    specs[index].is_active = model.items[index].is_active;
    specs[index].favicon_png_data = model.items[index].favicon_png_data;
    specs[index].custom_icon = model.items[index].custom_icon;
    specs[index].pinned_url = model.items[index].pinned_url;
  }
  return specs;
}

views::Label* AddPlaceholderLabel(views::View* parent,
                                  std::u16string text,
                                  const MahoSidebarPalette& palette) {
  auto label = std::make_unique<views::Label>(std::move(text));
  label->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  label->SetAutoColorReadabilityEnabled(false);
  if (palette.primary_text != SK_ColorTRANSPARENT) {
    label->SetEnabledColor(palette.secondary_text);
  } else {
    // Private windows have no resolved palette.
    label->SetEnabledColor(ui::kColorSysOnSurfaceSubtle);
  }
  label->SetMultiLine(false);
  label->SetElideBehavior(gfx::ELIDE_TAIL);
  label->SetTextStyle(views::style::STYLE_BODY_3);
  label->SetSubpixelRenderingEnabled(false);
  return parent->AddChildView(std::move(label));
}

std::optional<SkColor> ExtractDominantColor(const ui::ImageModel& fav) {
  if (fav.IsEmpty() || !fav.IsImage()) {
    return std::nullopt;
  }
  const SkBitmap bitmap = fav.GetImage().AsBitmap();
  if (bitmap.drawsNothing() || bitmap.width() <= 0 || bitmap.height() <= 0) {
    return std::nullopt;
  }
  // Ask for vibrant profiles in order of preference: normal → dark → light.
  // Prominent-color extraction returns the actual dominant hue per profile
  // (unlike k-means which averages Google's four colors into orange).
  gfx::Rect region(0, 0, bitmap.width(), bitmap.height());
  const std::vector<color_utils::ColorProfile> profiles = {
      {color_utils::LumaRange::NORMAL, color_utils::SaturationRange::VIBRANT},
      {color_utils::LumaRange::DARK, color_utils::SaturationRange::VIBRANT},
      {color_utils::LumaRange::LIGHT, color_utils::SaturationRange::VIBRANT},
      {color_utils::LumaRange::NORMAL, color_utils::SaturationRange::ANY},
  };
  const auto swatches = color_utils::CalculateProminentColorsOfBitmap(
      bitmap, profiles, &region,
      base::BindRepeating([](const SkColor&) { return true; }));
  for (const auto& swatch : swatches) {
    if (SkColorGetA(swatch.color) != 0) {
      return swatch.color;
    }
  }
  return std::nullopt;
}

}  // namespace

BEGIN_METADATA(MahoSidebarFavoritesGridView)
END_METADATA

MahoSidebarFavoritesGridView::LayoutSpec::LayoutSpec() = default;

MahoSidebarFavoritesGridView::LayoutSpec::LayoutSpec(const LayoutSpec&) =
    default;

MahoSidebarFavoritesGridView::LayoutSpec&
MahoSidebarFavoritesGridView::LayoutSpec::operator=(const LayoutSpec&) =
    default;

MahoSidebarFavoritesGridView::LayoutSpec::~LayoutSpec() = default;

MahoSidebarFavoritesGridView::MahoSidebarFavoritesGridView(Browser* browser)
    : browser_(browser),
      prefs_(browser && browser->GetProfile() ? browser->GetProfile()->GetPrefs()
                                           : nullptr) {
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(),
      kFavoriteSectionSpacingDp));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  auto* shell_view = AddChildView(std::make_unique<views::View>());
  auto* shell_layout = shell_view->SetLayoutManager(
      std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kVertical,
                                         gfx::Insets::TLBR(0, 0, 0, 0),
                                         kFavoriteShellContentSpacingDp));
  shell_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  shell_view->SetBackground(nullptr);
  shell_view->SetBorder(nullptr);

  content_view_ = shell_view->AddChildView(std::make_unique<views::View>());
  auto* content_layout = content_view_->SetLayoutManager(
      std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kVertical,
                                         gfx::Insets(),
                                         sidebar_layout::kFavoriteTileSpacingDp));
  content_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  empty_state_view_ = shell_view->AddChildView(std::make_unique<views::View>());
  auto* empty_layout = empty_state_view_->SetLayoutManager(
      std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kVertical,
                                         gfx::Insets::TLBR(
                                             kFavoriteEmptyCardTopInsetDp, 0, 0,
                                             0),
                                         0));
  empty_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  auto empty_state_card = std::make_unique<EmptyFavoritesPlaceholderView>(
      base::BindRepeating(&MahoSidebarFavoritesGridView::OnFavoritesHintDismissed,
                          weak_factory_.GetWeakPtr()));
  empty_state_card_ = empty_state_card.get();
  empty_state_view_->AddChildView(std::move(empty_state_card));

  for (size_t index = 0; index < tile_views_.size(); ++index) {
    auto tile = std::make_unique<FavoriteTileButton>(
        base::BindRepeating(&MahoSidebarFavoritesGridView::OnTilePressed,
                            weak_factory_.GetWeakPtr(), index),
        base::BindRepeating(&MahoSidebarFavoritesGridView::OnTileDragDone,
                            weak_factory_.GetWeakPtr()),
        this,
        std::string(),
        index);
    tile->SetPreferredSize(
        gfx::Size(1, sidebar_layout::kFavoriteTileHeightDp));
    tile->SetPaintToLayer();
    tile->layer()->SetFillsBoundsOpaquely(false);
    tile->layer()->GetAnimator()->set_preemption_strategy(
        ui::LayerAnimator::IMMEDIATELY_ANIMATE_TO_NEW_TARGET);
    tile->SetBorder(nullptr);
    tile->SetRequestFocusOnPress(false);

    auto* tile_layout = tile->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical,
        sidebar_layout::kFavoriteTileInsets,
        kFavoriteTileContentSpacingDp));
    tile_layout->set_main_axis_alignment(
        views::BoxLayout::MainAxisAlignment::kCenter);
    tile_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);

    auto glyph_container = std::make_unique<views::View>();
    glyph_container->SetPreferredSize(
        gfx::Size(sidebar_layout::kFavoriteGlyphContainerSizeDp,
                  sidebar_layout::kFavoriteGlyphContainerSizeDp));
    glyph_container->SetPaintToLayer();
    glyph_container->layer()->SetFillsBoundsOpaquely(false);
    glyph_container->layer()->GetAnimator()->set_preemption_strategy(
        ui::LayerAnimator::IMMEDIATELY_ANIMATE_TO_NEW_TARGET);
    glyph_container->SetBackground(nullptr);
    auto* glyph_layout = glyph_container->SetLayoutManager(
        std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 0));
    glyph_layout->set_main_axis_alignment(
        views::BoxLayout::MainAxisAlignment::kCenter);
    glyph_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);

    tile_icons_[index] = glyph_container->AddChildView(
        std::make_unique<views::ImageView>());
    auto icon_label = std::make_unique<views::Label>(std::u16string());
    icon_label->SetHorizontalAlignment(gfx::ALIGN_CENTER);
    icon_label->SetVerticalAlignment(gfx::ALIGN_MIDDLE);
    icon_label->SetAutoColorReadabilityEnabled(false);
    icon_label->SetSubpixelRenderingEnabled(false);
    icon_label->SetMultiLine(false);
    icon_label->SetFontList(icon_label->font_list().Derive(
        kFavoriteCustomGlyphFontSizeDelta, gfx::Font::NORMAL,
        gfx::Font::Weight::NORMAL));
    icon_label->SetPreferredSize(
        gfx::Size(sidebar_layout::kFavoriteGlyphContainerSizeDp,
                  sidebar_layout::kFavoriteGlyphContainerSizeDp));
    icon_label->SetVisible(false);
    tile_icon_labels_[index] =
        glyph_container->AddChildView(std::move(icon_label));
    glyph_container->SetBorder(nullptr);
    tile_glyph_containers_[index] = glyph_container.get();
    tile->AddChildView(std::move(glyph_container));

    tile_labels_[index] = AddPlaceholderLabel(tile.get(), std::u16string(), palette_);
    tile_labels_[index]->SetBorder(views::CreateEmptyBorder(gfx::Insets::TLBR(
        0, 4, 0, 4)));

    tile_views_[index] = tile.get();
    tile_views_[index]->SetVisible(false);
    tile_views_[index]->SetEnabled(false);
    owned_tiles_[index] = std::move(tile);
  }

  RebuildGridRows(0);
  Update(MahoSidebarFavoritesModel());
}

MahoSidebarFavoritesGridView::~MahoSidebarFavoritesGridView() {
  for (size_t i = 0; i < kTileCount; ++i) {
    if (tile_views_[i] && tile_views_[i]->layer()) {
      tile_views_[i]->layer()->GetAnimator()->StopAnimating();
    }
    if (tile_glyph_containers_[i] && tile_glyph_containers_[i]->layer()) {
      tile_glyph_containers_[i]->layer()->GetAnimator()->StopAnimating();
    }
  }
}

ui::ImageModel MahoSidebarFavoritesGridView::tile_favicon_for_testing(
    size_t index) const {
  if (index >= tile_icons_.size() || !tile_icons_[index]) {
    return ui::ImageModel();
  }
  return tile_icons_[index]->GetImageModel();
}

ui::ImageModel
MahoSidebarFavoritesGridView::cached_favicon_for_tab_id_for_testing(
    const std::string& tab_id) const {
  const auto it = cached_favicons_by_tab_id_.find(tab_id);
  return it == cached_favicons_by_tab_id_.end() ? ui::ImageModel()
                                                : it->second;
}

void MahoSidebarFavoritesGridView::CleanupAfterDrop() {
  ResetDragFeedback(DragIdentityCleanup::kClear, false);
}

void MahoSidebarFavoritesGridView::HideOverlayAndPhantom() {
  HideInsertionPreview();
}

void MahoSidebarFavoritesGridView::ResetDragFeedback(
    DragIdentityCleanup identity_cleanup,
    bool restore_os_drag_image) {
  if (restore_os_drag_image) {
    RestoreOsDragGhostImage();
  }
  current_drag_preview_index_ = -1;
  drag_over_drop_target_ = false;
  drag_reveal_active_ = false;
  SetBorder(nullptr);
  UpdateEmptyStateHighlight(false);
  HideInsertionPreview();
  const bool hint_dismissed =
      prefs_ && prefs_->GetBoolean(sidebar_prefs::kFavoritesDragHintDismissed);
  if (empty_state_view_ && favorite_count_ == 0 && hint_dismissed) {
    empty_state_view_->SetVisible(false);
  }
  UpdateContainerOwnedPreferredHeight(favorite_count_);
  if (identity_cleanup == DragIdentityCleanup::kClear) {
    active_drag_tab_id_.clear();
    pending_external_drag_tab_id_.clear();
    drag_accepted_tab_id_.clear();
  }
}

void MahoSidebarFavoritesGridView::OnSidebarPaletteChanged(
    const MahoSidebarPalette& palette) {
  // Placeholder globes bake the palette glyph color; repaint the ones that
  // were drawn with the previous palette.
  const ui::ImageModel old_globe =
      CreateThemedGlobeIcon(palette_, sidebar_layout::kFavoriteGlyphSizeDp);
  palette_ = palette;
  const ui::ImageModel new_globe =
      CreateThemedGlobeIcon(palette_, sidebar_layout::kFavoriteGlyphSizeDp);
  for (auto& icon : tile_icons_) {
    if (icon && icon->GetImageModel() == old_globe) {
      icon->SetImage(new_globe);
    }
  }
  if (drag_preview_icon_ &&
      drag_preview_icon_->GetImageModel() == old_globe) {
    drag_preview_icon_->SetImage(new_globe);
  }
  if (empty_state_card_) {
    static_cast<EmptyFavoritesPlaceholderView*>(empty_state_card_.get())
        ->SetSidebarPalette(palette_);
  }
  for (size_t index = 0; index < tile_views_.size(); ++index) {
    if (tile_views_[index]) {
      static_cast<FavoriteTileButton*>(tile_views_[index].get())
          ->SetSidebarPalette(palette_);
    }
    // Private windows have no resolved palette (transparent roles).
    const bool has_palette = palette_.primary_text != SK_ColorTRANSPARENT;
    if (tile_labels_[index]) {
      tile_labels_[index]->SetEnabledColor(
          has_palette ? ui::ColorVariant(palette_.secondary_text)
                      : ui::ColorVariant(ui::kColorSysOnSurfaceSubtle));
    }
    if (tile_icon_labels_[index]) {
      tile_icon_labels_[index]->SetEnabledColor(
          has_palette ? ui::ColorVariant(palette_.primary_text)
                      : ui::ColorVariant(ui::kColorSysOnSurface));
    }
  }
  if (drag_preview_tile_) {
    drag_preview_tile_->SetBackground(views::CreateRoundedRectBackground(
        palette_.row_active, sidebar_layout::kFavoriteTileCornerRadiusDp));
  }
  SchedulePaint();
}

void MahoSidebarFavoritesGridView::OnBoundsChanged(
    const gfx::Rect& previous_bounds) {
  if (drag_over_drop_target_) {
    views::View::OnBoundsChanged(previous_bounds);
    return;
  }
  CancelAnimations();
  views::View::OnBoundsChanged(previous_bounds);
  if (bounds().size() != previous_bounds.size()) {
    RebuildGridRows(favorite_count_);
    UpdateContainerOwnedPreferredHeight(favorite_count_);
  }
}

void MahoSidebarFavoritesGridView::Layout(PassKey) {
  LayoutSuperclass<views::View>(this);
}

void MahoSidebarFavoritesGridView::Update(
    const MahoSidebarFavoritesModel& model) {
  last_update_had_reorder_animation_for_testing_ = false;
  last_insertion_animation_bounds_for_testing_ = gfx::Rect();
  ApplyImmediateLayoutForAnimation();
  SnapshotCurrentBounds();
  const size_t old_count = favorite_count_;

  favorite_count_ = std::min(model.items.size(), tile_views_.size());
  const bool has_items = !model.items.empty();
  const bool hint_dismissed =
      prefs_ && prefs_->GetBoolean(sidebar_prefs::kFavoritesDragHintDismissed);
  const bool show_empty_state =
      !has_items && (!hint_dismissed || drag_reveal_active_);
  const size_t visible_item_count = favorite_count_;
  RebuildGridRows(visible_item_count);
  content_view_->SetVisible(has_items);
  empty_state_view_->SetVisible(show_empty_state);
  UpdateContainerOwnedPreferredHeight(visible_item_count);
  SetVisible(true);
  UpdateEmptyStateHighlight(!has_items && drag_over_drop_target_);

  if (!has_items) {
    for (size_t index = 0; index < tile_views_.size(); ++index) {
      if (!tile_tab_ids_[index].empty() || tile_urls_[index].is_valid()) {
        ++tile_favicon_generations_[index];
      }
      tile_views_[index]->SetVisible(false);
      tile_urls_[index] = GURL();
      tile_titles_[index] = std::u16string();
      tile_custom_titles_[index] = std::u16string();
      tile_tab_ids_[index].clear();
      tile_custom_icons_[index].clear();
      tile_pinned_urls_[index].clear();
      if (tile_icon_labels_[index]) {
        tile_icon_labels_[index]->SetVisible(false);
      }
      if (tile_icons_[index]) {
        tile_icons_[index]->SetVisible(true);
      }
    }
    return;
  }

  const auto specs = BuildTileSpecs(model);

  for (size_t index = 0; index < tile_views_.size(); ++index) {
    const bool has_item = specs[index].url.is_valid();
    const GURL previous_url = tile_urls_[index];
    const std::string previous_tab_id = tile_tab_ids_[index];
    tile_urls_[index] = has_item ? specs[index].url : GURL();
    tile_titles_[index] = has_item ? specs[index].text : std::u16string();
    tile_custom_titles_[index] =
        has_item ? specs[index].custom_title : std::u16string();
    tile_tab_ids_[index] = has_item ? specs[index].tab_id : std::string();
    tile_custom_icons_[index] =
        has_item ? specs[index].custom_icon : std::u16string();
    tile_pinned_urls_[index] =
        has_item ? specs[index].pinned_url : std::string();
    if (previous_url != tile_urls_[index] ||
        previous_tab_id != tile_tab_ids_[index]) {
      ++tile_favicon_generations_[index];
    }
    tile_views_[index]->SetVisible(has_item);
    tile_views_[index]->SetEnabled(has_item);
    tile_views_[index]->SetTooltipText(has_item ? specs[index].text
                                                : std::u16string());
    tile_views_[index]->SetAccessibleName(has_item ? specs[index].text
                                                   : u"Empty favorite slot");

    auto* tile_btn = static_cast<FavoriteTileButton*>(tile_views_[index].get());
    tile_btn->set_tab_id(tile_tab_ids_[index]);
    tile_btn->SetIsActive(has_item && specs[index].is_active);

    if (has_item) {
      ui::ImageModel fav = ResolveFaviconForTabId(specs[index].tab_id);
      bool cache_by_tab_id = !fav.IsEmpty();

      if (fav.IsEmpty()) {
        const auto cached_it =
            cached_favicons_by_tab_id_.find(specs[index].tab_id);
        if (cached_it != cached_favicons_by_tab_id_.end()) {
          fav = cached_it->second;
        }
      }

      if (fav.IsEmpty() && !specs[index].favicon_png_data.empty()) {
        gfx::Image png =
            gfx::Image::CreateFrom1xPNGBytes(specs[index].favicon_png_data);
        if (!png.IsEmpty()) {
          fav = ui::ImageModel::FromImage(png);
          cache_by_tab_id = true;
        }
      }

      if (fav.IsEmpty()) {
        if (browser_) {
          favicon::FaviconService* favicon_service =
              FaviconServiceFactory::GetForProfile(
                  browser_->GetProfile(), ServiceAccessType::EXPLICIT_ACCESS);
          if (favicon_service) {
            const uint64_t generation = tile_favicon_generations_[index];
            favicon_service->GetFaviconImageForPageURL(
                specs[index].url,
                base::BindOnce(&MahoSidebarFavoritesGridView::OnFaviconLoaded,
                               weak_factory_.GetWeakPtr(), index,
                               specs[index].tab_id, specs[index].url,
                               generation),
                &cancelable_task_tracker_);
          }
        }
        fav = CreateThemedGlobeIcon(palette_, sidebar_layout::kFavoriteGlyphSizeDp);
        cache_by_tab_id = false;
      }

      ApplyTileFavicon(index, specs[index].tab_id, fav, cache_by_tab_id);
      ApplyTileCustomIcon(index);
      if (tile_glyph_containers_[index]) {
        tile_glyph_containers_[index]->SetBackground(nullptr);
        tile_glyph_containers_[index]->SetBorder(nullptr);
      }
      tile_labels_[index]->SetText(specs[index].text);
      tile_labels_[index]->SetVisible(false);
      if (tile_views_[index]->layer()) {
        const bool masked = drag_accepted_tab_id_.empty() &&
            (tile_tab_ids_[index] == active_drag_tab_id_ ||
             tile_tab_ids_[index] == pending_external_drag_tab_id_);
        tile_views_[index]->layer()->SetOpacity(masked ? 0.f : 1.f);
        if (!masked) tile_views_[index]->layer()->SetTransform(gfx::Transform());
      }
      if (tile_glyph_containers_[index] &&
          tile_glyph_containers_[index]->layer()) {
        tile_glyph_containers_[index]->layer()->SetTransform(gfx::Transform());
      }
    } else {
      tile_labels_[index]->SetVisible(false);
      if (tile_icon_labels_[index]) {
        tile_icon_labels_[index]->SetVisible(false);
      }
      if (tile_icons_[index]) {
        tile_icons_[index]->SetVisible(true);
      }
    }
  }

  ApplyImmediateLayoutForAnimation();

  if (old_count == 0) {
    return;
  }

  bool has_reorder = false;
  for (size_t i = 0; i < kTileCount; ++i) {
    if (!tile_views_[i]->GetVisible() || tile_tab_ids_[i].empty()) {
      continue;
    }

    const int old_idx = FindSnapshotIndexForTabId(tile_tab_ids_[i]);
    if (old_idx < 0) {
      AnimateInsertion(i);
    } else {
      const gfx::Rect current_bounds = TileBoundsInGrid(i);
      if (!snapshot_bounds_[old_idx].IsEmpty() &&
          !current_bounds.IsEmpty() &&
          snapshot_bounds_[old_idx] != current_bounds) {
        has_reorder = true;
      }
    }
  }

  if (has_reorder) {
    last_update_had_reorder_animation_for_testing_ = true;
    AnimateReorder();
  }
}

void MahoSidebarFavoritesGridView::OnFaviconLoaded(
    size_t,
    const std::string& tab_id,
    const GURL& requested_url,
    uint64_t generation,
    const favicon_base::FaviconImageResult& result) {
  const size_t current_index = FindTileIndexForTabId(tab_id);
  if (!IsCurrentFaviconRequest(current_index, tab_id, requested_url,
                               generation)) {
    return;
  }
  ui::ImageModel fav;
  bool cache_by_tab_id = false;
  if (!result.image.IsEmpty()) {
    fav = ui::ImageModel::FromImage(result.image);
    cache_by_tab_id = true;
  } else {
    const auto cached_it = cached_favicons_by_tab_id_.find(tab_id);
    fav = cached_it == cached_favicons_by_tab_id_.end()
              ? CreateThemedGlobeIcon(palette_, sidebar_layout::kFavoriteGlyphSizeDp)
              : cached_it->second;
  }
  ApplyTileFavicon(current_index, tab_id, fav, cache_by_tab_id);
  ApplyTileCustomIcon(current_index);
}

void MahoSidebarFavoritesGridView::ApplyTileCustomIcon(size_t index) {
  if (index >= kTileCount) {
    return;
  }
  const bool has_custom_icon = !tile_custom_icons_[index].empty();
  if (tile_icon_labels_[index]) {
    tile_icon_labels_[index]->SetText(tile_custom_icons_[index]);
    tile_icon_labels_[index]->SetEnabledColor(palette_.primary_text);
    tile_icon_labels_[index]->SetVisible(has_custom_icon);
  }
  if (tile_icons_[index]) {
    tile_icons_[index]->SetVisible(!has_custom_icon);
  }
}

bool MahoSidebarFavoritesGridView::IsCurrentFaviconRequest(
    size_t index,
    const std::string& tab_id,
    const GURL& requested_url,
    uint64_t generation) const {
  return index < tile_urls_.size() && tile_views_[index] &&
         tile_views_[index]->GetVisible() && tile_tab_ids_[index] == tab_id &&
         tile_urls_[index] == requested_url &&
         tile_favicon_generations_[index] == generation;
}

size_t MahoSidebarFavoritesGridView::FindTileIndexForTabId(
    const std::string& tab_id) const {
  if (tab_id.empty()) {
    return tile_tab_ids_.size();
  }
  for (size_t index = 0; index < tile_tab_ids_.size(); ++index) {
    if (tile_tab_ids_[index] == tab_id) {
      return index;
    }
  }
  return tile_tab_ids_.size();
}

void MahoSidebarFavoritesGridView::ApplyTileFavicon(
    size_t index,
    const std::string& tab_id,
    const ui::ImageModel& favicon,
    bool cache_by_tab_id) {
  if (index >= tile_icons_.size() || !tile_icons_[index]) {
    return;
  }
  ui::ImageModel fav = favicon;
  tile_icons_[index]->SetImage(fav);
  tile_icons_[index]->SetImageSize(
      gfx::Size(sidebar_layout::kFavoriteGlyphSizeDp,
                sidebar_layout::kFavoriteGlyphSizeDp));
  if (cache_by_tab_id && !tab_id.empty() && !fav.IsEmpty()) {
    cached_favicons_by_tab_id_[tab_id] = fav;
  }
  if (index < tile_views_.size() && tile_views_[index]) {
    auto* tile_btn = static_cast<FavoriteTileButton*>(tile_views_[index].get());
    std::optional<SkColor> color;
    if (!tab_id.empty()) {
      const auto color_it = dominant_color_cache_by_tab_id_.find(tab_id);
      if (color_it != dominant_color_cache_by_tab_id_.end() &&
          color_it->second.favicon == fav) {
        color = color_it->second.color;
      } else {
        color = ExtractDominantColor(fav);
        dominant_color_cache_by_tab_id_[tab_id] =
            DominantColorCacheEntry{fav, color};
      }
    } else {
      color = ExtractDominantColor(fav);
    }
    if (color.has_value()) {
      tile_btn->SetActiveBorderColor(*color);
    } else {
      tile_btn->ClearActiveBorderColor();
    }
  }
}

MahoSidebarFavoritesGridView::LayoutSpec
MahoSidebarFavoritesGridView::GetLayoutSpec(size_t favorite_count,
                                            int available_width) {
  const size_t count = std::min(favorite_count, kTileCount);
  LayoutSpec spec;

  // Zen essentials layout table. Maps N favorites → (row_counts, max_columns).
  // Mirrors zen-browser/desktop ZenSpaceManager.mjs essentialHackType logic.
  switch (count) {
    case 0:
      break;
    case 1:
      spec.row_counts = {1};
      spec.max_columns = 1;
      break;
    case 2:
      spec.row_counts = {2};
      spec.max_columns = 2;
      break;
    case 3:
      spec.row_counts = {3};
      spec.max_columns = 3;
      break;
    case 4:
      spec.row_counts = {2, 2};
      spec.max_columns = 2;
      break;
    case 5:
      spec.row_counts = {4, 1};
      spec.max_columns = 4;
      break;
    case 6:
      spec.row_counts = {3, 3};
      spec.max_columns = 3;
      break;
    case 7:
      spec.row_counts = {4, 3};
      spec.max_columns = 4;
      break;
    case 8:
      spec.row_counts = {4, 4};
      spec.max_columns = 4;
      break;
    case 9:
      spec.row_counts = {3, 3, 3};
      spec.max_columns = 3;
      break;
    case 10:
      spec.row_counts = {4, 4, 2};
      spec.max_columns = 4;
      break;
    case 11:
      spec.row_counts = {4, 4, 3};
      spec.max_columns = 4;
      break;
    case 12:
      spec.row_counts = {4, 4, 4};
      spec.max_columns = 4;
      break;
    default:
      spec.max_columns = 4;
      for (size_t remaining = count; remaining > 0;) {
        const int row_count = std::min<int>(4, remaining);
        spec.row_counts.push_back(row_count);
        remaining -= row_count;
      }
      break;
  }

  return spec;
}

int MahoSidebarFavoritesGridView::GetPreferredHeightForWidth(
    int available_width,
    size_t favorite_count) {
  const size_t count = std::min(favorite_count, kTileCount);
  if (count == 0) {
    return kFavoriteEmptyStateHeightDp + kFavoriteEmptyCardTopInsetDp;
  }

  const LayoutSpec layout_spec = GetLayoutSpec(count, available_width);
  int total_height = 0;
  for (size_t row_index = 0; row_index < layout_spec.row_counts.size(); ++row_index) {
    const int row_count = layout_spec.row_counts[row_index];
    const int row_height = layout_spec.uses_compact_row_card
                               ? kFavoriteSingleCardHeightDp
                               : (row_count == 2 ? kFavoriteTwoTileHeightDp
                                                 : sidebar_layout::kFavoriteTileHeightDp);
    total_height += row_height;
    if (row_index + 1 < layout_spec.row_counts.size()) {
      total_height += sidebar_layout::kFavoriteTileSpacingDp;
    }
  }
  return total_height;
}

void MahoSidebarFavoritesGridView::UpdateContainerOwnedPreferredHeight(
    size_t favorite_count) {
  const bool hint_dismissed =
      prefs_ && prefs_->GetBoolean(sidebar_prefs::kFavoritesDragHintDismissed);
  const bool collapse_empty =
      favorite_count == 0 && hint_dismissed && !drag_reveal_active_;
  const int height = collapse_empty
                         ? 0
                         : GetPreferredHeightForWidth(width(), favorite_count);
  SetPreferredSize(gfx::Size(1, height));
  PreferredSizeChanged();
}

void MahoSidebarFavoritesGridView::UpdateEmptyStateHighlight(bool highlighted) {
  if (!empty_state_card_) {
    return;
  }
  static_cast<EmptyFavoritesPlaceholderView*>(empty_state_card_.get())
      ->SetHighlighted(highlighted);
}

void MahoSidebarFavoritesGridView::RebuildGridRows(size_t favorite_count) {
  LayoutSpec layout_spec = GetLayoutSpec(favorite_count, width());
  if (layout_spec.row_counts == current_layout_spec_.row_counts &&
      layout_spec.max_columns == current_layout_spec_.max_columns &&
      layout_spec.uses_compact_row_card == current_layout_spec_.uses_compact_row_card) {
    return;
  }

  for (size_t index = 0; index < tile_views_.size(); ++index) {
    views::View* tile = tile_views_[index];
    if (tile && tile->parent()) {
      owned_tiles_[index] = tile->parent()->RemoveChildViewT(tile);
      tile_views_[index] = static_cast<views::Button*>(owned_tiles_[index].get());
    }
  }

  current_layout_spec_ = layout_spec;

  row_views_.clear();
  while (!content_view_->children().empty()) {
    content_view_->RemoveChildViewT(content_view_->children().front());
  }

  if (favorite_count == 0) {
    PreferredSizeChanged();
    return;
  }

  size_t tile_index = 0;
  for (size_t row = 0; row < layout_spec.row_counts.size(); ++row) {
    const int row_count = layout_spec.row_counts[row];
    auto row_view = std::make_unique<views::View>();
    auto* row_ptr = row_view.get();
    row_views_.push_back(content_view_->AddChildView(std::move(row_view)));
    auto* row_layout = row_ptr->SetLayoutManager(
        std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kHorizontal,
                                           gfx::Insets(),
                                           sidebar_layout::kFavoriteTileSpacingDp));
    row_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);

    for (int column = 0; column < row_count && tile_index < tile_views_.size();
         ++column, ++tile_index) {
      views::View* tile = tile_views_[tile_index];
      if (owned_tiles_[tile_index]) {
        row_ptr->AddChildView(std::move(owned_tiles_[tile_index]));
        tile_views_[tile_index] = static_cast<views::Button*>(tile);
      }
      tile->SetPreferredSize(gfx::Size(1, sidebar_layout::kFavoriteTileHeightDp));
      row_layout->SetFlexForView(tile, 1);
    }

    for (int filler = row_count; filler < layout_spec.max_columns; ++filler) {
      auto spacer = std::make_unique<views::View>();
      auto* spacer_raw = row_ptr->AddChildView(std::move(spacer));
      row_layout->SetFlexForView(spacer_raw, 1);
    }
  }

  for (; tile_index < tile_views_.size(); ++tile_index) {
    tile_views_[tile_index]->SetVisible(false);
  }

  PreferredSizeChanged();
}

bool MahoSidebarFavoritesGridView::GetDropFormats(
    int* formats,
    std::set<ui::ClipboardFormatType>* format_types) {
  format_types->insert(GetMahoDragFormatType());
  return true;
}

bool MahoSidebarFavoritesGridView::CanDrop(const ui::OSExchangeData& data) {
  SidebarDragPayload payload;
  if (ReadMahoDragData(data, payload)) {
    return payload.node_kind == SidebarNodeKind::kTab;
  }
  return false;
}

int MahoSidebarFavoritesGridView::OnDragUpdated(
    const ui::DropTargetEvent& event) {
  SidebarDragPayload payload;
  if (!ReadMahoDragData(event.data(), payload) ||
      payload.node_kind != SidebarNodeKind::kTab) {
    ResetDragFeedback(DragIdentityCleanup::kClear, true);
    return static_cast<int>(ui::mojom::DragOperation::kNone);
  }

  if (!os_ghost_hidden_) {
    // Widget::UpdateDragImage() does not exist at this upstream pin; the OS
    // drag ghost already shows the custom image set via WriteDragDataForView,
    // so there is nothing to hide. Keep the flag consistent for the restore
    // path, which is also a no-op without the widget API.
    os_ghost_hidden_ = true;
  }

  active_drag_tab_id_ = payload.node_id;

  const size_t favorite_count = favorite_count_;
  if (payload.origin != SidebarDragOrigin::kFavorites &&
      favorite_count >= kTileCount) {
    ResetDragFeedback(DragIdentityCleanup::kClear, true);
    return static_cast<int>(ui::mojom::DragOperation::kNone);
  }

  if (!drag_reveal_active_ && favorite_count_ == 0) {
    RevealForDrag();
  }

  drag_over_drop_target_ = true;
  if (favorite_count_ == 0) {
    SetBorder(nullptr);
    UpdateEmptyStateHighlight(true);
  } else {
    UpdateEmptyStateHighlight(false);
  }

  if (favorite_count_ > 0) {
    const int preview_index = ResolveDropIndex(
        event.location(), payload.node_id,
        payload.origin == SidebarDragOrigin::kFavorites);

    const bool is_external =
        payload.origin != SidebarDragOrigin::kFavorites;
    const int height_count =
        is_external ? std::min(static_cast<int>(favorite_count_) + 1,
                               static_cast<int>(kTileCount))
                    : static_cast<int>(favorite_count_);
    UpdateContainerOwnedPreferredHeight(height_count);

    if (preview_index != current_drag_preview_index_) {
      AnimateDragPreview(preview_index, payload.node_id);
      current_drag_preview_index_ = preview_index;
    }
    if (is_external) {
      ShowInsertionPreview(event.location(), payload.node_id);
    } else {
      HideInsertionPreview();
    }
    SetBorder(nullptr);
  } else {
    current_drag_preview_index_ = 0;
    if (payload.origin != SidebarDragOrigin::kFavorites) {
      ShowInsertionPreview(event.location(), payload.node_id);
    } else {
      HideInsertionPreview();
    }
  }

  return static_cast<int>(ui::mojom::DragOperation::kMove);
}

views::View::DropCallback MahoSidebarFavoritesGridView::GetDropCallback(
    const ui::DropTargetEvent& event) {
  SidebarDragPayload payload;
  const bool has_payload = ReadMahoDragData(event.data(), payload) &&
                           payload.node_kind == SidebarNodeKind::kTab;
  if (!has_payload) {
    return base::BindOnce(
        [](base::WeakPtr<MahoSidebarFavoritesGridView> weak_grid,
           const ui::DropTargetEvent&, ui::mojom::DragOperation& output_drag_op,
           std::unique_ptr<ui::LayerTreeOwner>) {
          output_drag_op = ui::mojom::DragOperation::kNone;
          if (weak_grid) {
            weak_grid->ResetDragFeedback(DragIdentityCleanup::kClear, false);
          }
        },
        weak_factory_.GetWeakPtr());
  }
  const size_t favorite_count = favorite_count_;
  const int target_index = ResolveDropIndex(
      event.location(), payload.node_id,
      payload.origin == SidebarDragOrigin::kFavorites);
  return base::BindOnce(
      [](int target_index,
         size_t favorite_count,
         base::WeakPtr<MahoSidebarFavoritesGridView> weak_grid,
         const ui::DropTargetEvent& event,
         ui::mojom::DragOperation& output_drag_op,
         std::unique_ptr<ui::LayerTreeOwner> drag_image_layer_owner) {
        SidebarDragPayload payload;
        if (!ReadMahoDragData(event.data(), payload) ||
            payload.node_kind != SidebarNodeKind::kTab) {
          output_drag_op = ui::mojom::DragOperation::kNone;
          if (weak_grid) {
            weak_grid->ResetDragFeedback(DragIdentityCleanup::kClear, false);
          }
          return;
        }
        output_drag_op = ui::mojom::DragOperation::kMove;
        if (weak_grid) {
          weak_grid->HideOverlayAndPhantom();
          weak_grid->MarkDropAccepted(payload.node_id);
        }

        const SidebarDropPlan plan = PlanFavoritesGridDrop(payload);
        ExecuteDropPlan(plan);

        const bool from_favorites =
            (payload.origin == SidebarDragOrigin::kFavorites);
        if (from_favorites) {
          DispatchShellEventEx("reorder_favorite", {
              ShellEventField("tab_id", payload.node_id),
              ShellEventField("new_index", target_index)});
        } else if (target_index <= static_cast<int>(favorite_count)) {
          DispatchShellEventEx("reorder_favorite", {
              ShellEventField("tab_id", payload.node_id),
              ShellEventField("new_index", target_index)});
        }

        if (weak_grid) {
          weak_grid->CleanupAfterDrop();
          weak_grid->ForceRefreshFromDrop();
        }
      },
      target_index, favorite_count, weak_factory_.GetWeakPtr());
}

void MahoSidebarFavoritesGridView::RevealForDrag() {
  if (drag_reveal_active_ || favorite_count_ > 0) {
    return;
  }
  drag_reveal_active_ = true;
  SetVisible(true);
  if (empty_state_view_) {
    empty_state_view_->SetVisible(true);
  }
  SetPreferredSize(
      gfx::Size(1, kFavoriteEmptyStateHeightDp + kFavoriteEmptyCardTopInsetDp));
  PreferredSizeChanged();
}

void MahoSidebarFavoritesGridView::CollapseFromDrag() {
  if (!drag_reveal_active_) {
    return;
  }
  drag_reveal_active_ = false;
  drag_over_drop_target_ = false;
  SetBorder(nullptr);
  UpdateEmptyStateHighlight(false);
  const bool hint_dismissed =
      prefs_ && prefs_->GetBoolean(sidebar_prefs::kFavoritesDragHintDismissed);
  if (empty_state_view_ && favorite_count_ == 0 && hint_dismissed) {
    empty_state_view_->SetVisible(false);
  }
  UpdateContainerOwnedPreferredHeight(0);
}

void MahoSidebarFavoritesGridView::OnDragStarted(const std::string& tab_id) {
  active_drag_tab_id_ = tab_id;
  for (size_t i = 0; i < kTileCount; ++i) {
    if (tile_tab_ids_[i] != tab_id) {
      continue;
    }
    if (tile_views_[i] && tile_views_[i]->layer()) {
      tile_views_[i]->layer()->SetOpacity(0.f);
    }
  }
}

void MahoSidebarFavoritesGridView::SetActiveDragGhostImage(
    const gfx::ImageSkia& image,
    const gfx::Vector2d& offset) {
  active_drag_ghost_image_ = image;
  active_drag_ghost_offset_ = offset;
}

void MahoSidebarFavoritesGridView::RestoreOsDragGhostImage() {
  // No-op at this upstream pin: without Widget::UpdateDragImage() the OS
  // ghost was never replaced, so there is nothing to restore.
  os_ghost_hidden_ = false;
}

void MahoSidebarFavoritesGridView::OnDragEnded() {
  os_ghost_hidden_ = false;
  active_drag_ghost_image_ = gfx::ImageSkia();
  CleanupAfterDrop();
  for (size_t i = 0; i < kTileCount; ++i) {
    if (tile_views_[i] && tile_views_[i]->layer()) {
      tile_views_[i]->layer()->GetAnimator()->StopAnimating();
      tile_views_[i]->layer()->SetOpacity(1.f);
      tile_views_[i]->layer()->SetTransform(gfx::Transform());
    }
    if (tile_glyph_containers_[i] && tile_glyph_containers_[i]->layer()) {
      tile_glyph_containers_[i]->layer()->GetAnimator()->StopAnimating();
      tile_glyph_containers_[i]->layer()->SetTransform(gfx::Transform());
    }
  }
}

void MahoSidebarFavoritesGridView::OnExternalDragStarted(
    const std::string& tab_id) {
  pending_external_drag_tab_id_ = tab_id;
  if (!browser_ || tab_id.empty()) return;
  TabStripModel* strip = browser_->GetTabStripModel();
  if (!strip) return;
  GURL url;
  for (int i = 0; i < strip->count(); ++i) {
    content::WebContents* contents = strip->GetWebContentsAt(i);
    if (!contents) continue;
    auto* helper = MahoTabIdHelper::FromWebContents(contents);
    if (helper && helper->stable_tab_id() == tab_id) {
      url = contents->GetVisibleURL();
      break;
    }
  }
  if (!url.is_valid()) return;
  favicon::FaviconService* favicon_service =
      FaviconServiceFactory::GetForProfile(
          browser_->GetProfile(), ServiceAccessType::EXPLICIT_ACCESS);
  if (favicon_service) {
    favicon_service->GetFaviconImageForPageURL(
        url, base::DoNothing(), &drag_preview_task_tracker_);
  }
}

void MahoSidebarFavoritesGridView::OnDragExited() {
  ResetDragFeedback(DragIdentityCleanup::kKeep, true);
  for (size_t i = 0; i < kTileCount; ++i) {
    if (tile_views_[i] && tile_views_[i]->GetVisible() &&
        tile_views_[i]->layer()) {
      views::AnimationBuilder()
          .Once()
          .SetDuration(base::Milliseconds(180))
          .SetTransform(tile_views_[i]->layer(), gfx::Transform(),
                        gfx::Tween::EASE_IN_OUT)
          .SetOpacity(tile_views_[i]->layer(), 1.0f,
                      gfx::Tween::EASE_IN_OUT);
      if (tile_glyph_containers_[i] && tile_glyph_containers_[i]->layer()) {
        views::AnimationBuilder()
            .Once()
            .SetDuration(base::Milliseconds(180))
            .SetTransform(tile_glyph_containers_[i]->layer(), gfx::Transform(),
                          gfx::Tween::EASE_IN_OUT);
      }
    }
  }
}

ui::DropTargetEvent MahoSidebarFavoritesGridView::ForwardDropEventToGrid(
    const ui::DropTargetEvent& event,
    const views::View* source) const {
  gfx::Point grid_location = event.location();
  views::View::ConvertPointToTarget(source, this, &grid_location);
  return ui::DropTargetEvent(event.data(), gfx::PointF(grid_location),
                             event.root_location_f(),
                             event.source_operations());
}

int MahoSidebarFavoritesGridView::ResolveDropIndex(
    const gfx::Point& point,
    const std::string& source_tab_id,
    bool source_is_favorite) const {  // L3-EXEMPT: local parameter
  const int favorite_count = static_cast<int>(GetVisibleFavoriteCount());

  const bool source_exists =
      source_is_favorite &&  // L3-EXEMPT: local parameter query
      std::find(tile_tab_ids_.begin(), tile_tab_ids_.end(), source_tab_id) !=
          tile_tab_ids_.end();

  // External drag inserts a new tile (N+1 geometry). Favorite reorder keeps
  // N geometry. Match the geometry that AnimateDragPreview/ShowInsertionPreview
  // will render.
  const int item_count =
      source_exists ? favorite_count : (favorite_count + 1);
  const int target_index = TileIndexForPoint(point, item_count);

  if (!source_is_favorite) {  // L3-EXEMPT: local parameter
    return std::clamp(target_index, 0, favorite_count);
  }
  if (!source_exists) {
    return std::clamp(target_index, 0, favorite_count);
  }
  return std::clamp(target_index, 0, std::max(0, favorite_count - 1));
}

int MahoSidebarFavoritesGridView::TileIndexForPoint(const gfx::Point& point,
                                                    int item_count) const {
  if (item_count <= 0) {
    return 0;
  }

  const auto tile_bounds = GetTileBoundsForCount(item_count);
  constexpr float kSpacingExpandFactor = 0.5f;
  const int expand =
      std::max(1, static_cast<int>(std::round(sidebar_layout::kFavoriteTileSpacingDp *
                                              kSpacingExpandFactor)));

  for (int i = 0; i < item_count; ++i) {
    gfx::Rect expanded = tile_bounds[i];
    expanded.Inset(-expand);
    if (expanded.Contains(point)) {
      return i;
    }
  }

  int nearest_index = 0;
  float nearest_distance = std::numeric_limits<float>::max();
  for (int i = 0; i < item_count; ++i) {
    const gfx::Point center = tile_bounds[i].CenterPoint();
    const float dx = static_cast<float>(point.x() - center.x());
    const float dy = static_cast<float>(point.y() - center.y());
    const float distance = (dx * dx) + (dy * dy);
    if (distance < nearest_distance) {
      nearest_distance = distance;
      nearest_index = i;
    }
  }

  if (nearest_index == item_count - 1) {
    const gfx::Rect& last_bounds = tile_bounds[nearest_index];
    if (point.x() >= last_bounds.CenterPoint().x()) {
      return item_count;
    }
  }

  return nearest_index;
}

std::array<gfx::Rect, MahoSidebarFavoritesGridView::kTileCount>
MahoSidebarFavoritesGridView::GetTileBoundsForCount(int item_count) const {
  std::array<gfx::Rect, kTileCount> bounds = {};
  if (item_count <= 0 || !content_view_) {
    return bounds;
  }

  const gfx::Point content_origin =
      views::View::ConvertPointToTarget(content_view_, this, gfx::Point());
  int content_width = std::max(0, content_view_->width());
  if (content_width <= 0) {
    // Empty grid: content_view_ hidden (width 0). Use grid's contents width.
    content_width = std::max(0, GetContentsBounds().width());
  }
  if (content_width <= 0) {
    return bounds;
  }
  const LayoutSpec layout_spec = GetLayoutSpec(item_count, content_width);

  const int tile_height = sidebar_layout::kFavoriteTileHeightDp;

  int tile_index = 0;
  int row_y = content_origin.y();
  for (int row_count : layout_spec.row_counts) {
    const int row_spacing =
        std::max(layout_spec.max_columns - 1, 0) * sidebar_layout::kFavoriteTileSpacingDp;
    const int tile_width =
        layout_spec.max_columns > 0
            ? std::max(0, (content_width - row_spacing) / layout_spec.max_columns)
            : 0;
    const int row_x = content_origin.x();

    for (int column = 0; column < row_count && tile_index < item_count;
         ++column, ++tile_index) {
      const int tile_x =
          row_x + column * (tile_width + sidebar_layout::kFavoriteTileSpacingDp);
      bounds[tile_index] = gfx::Rect(tile_x, row_y, tile_width, tile_height);
    }
    row_y += tile_height + sidebar_layout::kFavoriteTileSpacingDp;
  }
  return bounds;
}

size_t MahoSidebarFavoritesGridView::GetVisibleFavoriteCount() const {
  return favorite_count_;
}

void MahoSidebarFavoritesGridView::MarkDropAccepted(const std::string& tab_id) {
  drag_accepted_tab_id_ = tab_id;
}

void MahoSidebarFavoritesGridView::OnTileDragDone(const std::string& tab_id) {
  if (tab_id.empty()) {
    return;
  }

  const bool accepted = (drag_accepted_tab_id_ == tab_id);
  drag_accepted_tab_id_.clear();

  const gfx::Point screen_point = display::Screen::Get()->GetCursorScreenPoint();
  if (GetBoundsInScreen().Contains(screen_point) || accepted) {
    return;
  }

  // ADR-0013 (plan design decision 4): dragging a favorite tile off the grid
  // un-favorites it (transitions its role back to normal / Today), not closes
  // it. A plain close_tab only suspends a favorite and keeps the tile, so it
  // was a visual no-op. Matches the exact change_tab_role→normal payload used
  // by the favorites context-menu "Remove" action.
  base::DictValue event;
  event.Set("tab_id", tab_id);
  base::DictValue new_role;
  new_role.Set("type", "normal");
  event.Set("new_role", std::move(new_role));
  DispatchShellEventDict("change_tab_role", std::move(event));
}

void MahoSidebarFavoritesGridView::OnFavoritesHintDismissed() {
  if (!prefs_) {
    return;
  }
  prefs_->SetBoolean(sidebar_prefs::kFavoritesDragHintDismissed, true);
  if (empty_state_view_ && favorite_count_ == 0 && !drag_reveal_active_) {
    empty_state_view_->SetVisible(false);
    UpdateContainerOwnedPreferredHeight(0);
  }
}

void MahoSidebarFavoritesGridView::OnTilePressed(size_t index) {
  if (!browser_ || index >= tile_urls_.size()) {
    return;
  }
  const std::string& tab_id = tile_tab_ids_[index];
  if (tab_id.empty()) {
    return;
  }
  auto* sidebar_view = FindSidebarViewAncestor(this);
  if (sidebar_view) {
    sidebar_view->ActivateTabById(tab_id);
  }
}

bool MahoSidebarFavoritesGridView::ActivateFavoriteByIndex(size_t index) {
  if (index >= favorite_count_ || index >= tile_tab_ids_.size()) {
    return false;
  }
  auto* sidebar_view = FindSidebarViewAncestor(this);
  if (!sidebar_view) {
    return false;
  }
  const std::string& tab_id = tile_tab_ids_[index];
  if (!tab_id.empty()) {
    sidebar_view->ActivateTabById(tab_id);
    return true;
  }
  return false;
}

void MahoSidebarFavoritesGridView::ShowTileContextMenu(
    size_t index,
    const gfx::Point& point,
    ui::mojom::MenuSourceType source_type) {
  if (!browser_ || index >= kTileCount) {
    return;
  }
  if (!tile_urls_[index].is_valid() && tile_tab_ids_[index].empty()) {
    return;
  }

  if (active_menu_runner_) {
    active_menu_runner_->Cancel();
    active_menu_runner_.reset();
    active_menu_model_.reset();
    active_context_menu_.reset();
  }

  // Snapshot the tile's values: the tile arrays are re-pointed by background
  // Update() while the menu is open, so deferred delegate callbacks must never
  // re-read them by index.
  active_context_menu_index_ = index;
  active_context_menu_tab_id_ = tile_tab_ids_[index];
  active_context_menu_url_ = tile_urls_[index];
  active_context_menu_title_ = tile_titles_[index];
  active_context_menu_pinned_url_ = tile_pinned_urls_[index];
  active_context_menu_custom_icon_ = tile_custom_icons_[index];
  active_context_menu_custom_title_ = tile_custom_titles_[index];
  active_context_menu_ = std::make_unique<MahoFavoritesContextMenu>(
      browser_, active_context_menu_url_, active_context_menu_title_,
      active_context_menu_tab_id_, this);
  active_menu_model_ = active_context_menu_->BuildMenuModel();
  active_menu_runner_ = std::make_unique<views::MenuRunner>(
      active_menu_model_.get(),
      views::MenuRunner::CONTEXT_MENU | views::MenuRunner::IS_NESTED);

  views::View* tile = tile_views_[index];
  active_menu_runner_->RunMenuAt(
      tile ? tile->GetWidget() : GetWidget(),
      nullptr,
      gfx::Rect(point, gfx::Size()),
      views::MenuAnchorPosition::kTopLeft,
      source_type);
}

void MahoSidebarFavoritesGridView::OnFavoriteShareRequested() {
  const GURL url = active_context_menu_pinned_url_.empty()
                       ? active_context_menu_url_
                       : GURL(active_context_menu_pinned_url_);
  if (!url.is_valid()) {
    return;
  }

#if BUILDFLAG(IS_MAC)
  const std::u16string& title = active_context_menu_custom_title_.empty()
                                    ? active_context_menu_title_
                                    : active_context_menu_custom_title_;
  const size_t index = active_context_menu_index_;
  views::View* anchor = this;
  if (index < kTileCount && tile_views_[index]) {
    anchor = tile_views_[index];
  }
  if (anchor->GetWidget()) {
    maho::ShowNativeSharePicker(anchor, url, title, base::DoNothing());
    return;
  }
#endif

  {
    ui::ScopedClipboardWriter writer(ui::ClipboardBuffer::kCopyPaste);
    writer.WriteText(base::UTF8ToUTF16(url.spec()));
  }
  maho::ShowLinkCopiedToast(browser_);
}

void MahoSidebarFavoritesGridView::OnFavoriteRenameRequested() {
  OpenEditDialog(MahoFavoriteEditDialog::Focus::kName);
}

void MahoSidebarFavoritesGridView::OnFavoriteIconChangeRequested() {
  OpenEditDialog(MahoFavoriteEditDialog::Focus::kIcon);
}

void MahoSidebarFavoritesGridView::OnFavoritePinnedUrlEditRequested() {
  OpenEditDialog(MahoFavoriteEditDialog::Focus::kUrl);
}

void MahoSidebarFavoritesGridView::OpenEditDialog(
    MahoFavoriteEditDialog::Focus focus) {
  if (active_context_menu_tab_id_.empty()) {
    return;
  }
  const std::string initial_url = active_context_menu_pinned_url_.empty()
                                      ? active_context_menu_url_.spec()
                                      : active_context_menu_pinned_url_;
  // Prefill with what the tile currently shows (authored title if any, else
  // the derived one) and remember both values as the change-detection
  // baseline, so saving a dialog without touching a field cannot author it.
  const std::u16string initial_name =
      active_context_menu_custom_title_.empty()
          ? active_context_menu_title_
          : active_context_menu_custom_title_;
  views::Widget* widget = GetWidget();
  MahoFavoriteEditDialog::Show(
      widget ? widget->GetNativeWindow() : gfx::NativeWindow(), focus,
      initial_name, base::UTF16ToUTF8(active_context_menu_custom_icon_),
      initial_url,
      base::BindOnce(&MahoSidebarFavoritesGridView::OnEditDialogAccepted,
                     weak_factory_.GetWeakPtr(), active_context_menu_tab_id_,
                     initial_name, initial_url));
}

void MahoSidebarFavoritesGridView::OnEditDialogAccepted(
    const std::string& tab_id,
    std::u16string original_name,
    std::string original_url,
    std::u16string name,
    std::string icon,
    std::string url) {
  if (tab_id.empty()) {
    return;
  }
  if (name != original_name) {
    DispatchShellEvent("set_tab_custom_title",
                       {{"tab_id", tab_id},
                        {"custom_title", base::UTF16ToUTF8(name)}});
  }
  DispatchShellEvent("set_tab_custom_icon",
                     {{"tab_id", tab_id}, {"custom_icon", icon}});
  if (!url.empty() && url != original_url && GURL(url).is_valid()) {
    DispatchShellEvent("set_tab_pinned_url",
                       {{"tab_id", tab_id}, {"url", url}});
  }
  ForceRefreshFromDrop();
}

void MahoSidebarFavoritesGridView::ApplyImmediateLayoutForAnimation() {
  if (views::Widget* widget = GetWidget()) {
    widget->LayoutRootViewIfNecessary();
    return;
  }
  DeprecatedLayoutImmediately();
}

gfx::Rect MahoSidebarFavoritesGridView::TileBoundsInGrid(
    size_t tile_index) const {
  if (tile_index >= kTileCount || !tile_views_[tile_index] ||
      !tile_views_[tile_index]->GetVisible() ||
      !tile_views_[tile_index]->parent()) {
    return gfx::Rect();
  }
  gfx::Rect bounds = tile_views_[tile_index]->bounds();
  gfx::Point origin = bounds.origin();
  views::View::ConvertPointToTarget(tile_views_[tile_index]->parent(), this,
                                    &origin);
  bounds.set_origin(origin);
  return bounds;
}

void MahoSidebarFavoritesGridView::SnapshotCurrentBounds() {
  for (size_t i = 0; i < kTileCount; ++i) {
    const gfx::Rect bounds = TileBoundsInGrid(i);
    if (!bounds.IsEmpty()) {
      snapshot_bounds_[i] = bounds;
      snapshot_tab_ids_[i] = tile_tab_ids_[i];
    } else {
      snapshot_bounds_[i] = gfx::Rect();
      snapshot_tab_ids_[i].clear();
    }
  }
}

int MahoSidebarFavoritesGridView::FindSnapshotIndexForTabId(
    const std::string& tab_id) const {
  if (tab_id.empty()) {
    return -1;
  }
  for (size_t i = 0; i < kTileCount; ++i) {
    if (snapshot_tab_ids_[i] == tab_id) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

void MahoSidebarFavoritesGridView::AnimateReorder() {
  for (size_t i = 0; i < kTileCount; ++i) {
    if (!tile_views_[i] || !tile_views_[i]->GetVisible()) {
      continue;
    }
    if (tile_tab_ids_[i].empty() || !tile_views_[i]->layer() ||
        !tile_views_[i]->parent()) {
      continue;
    }

    const int old_index = FindSnapshotIndexForTabId(tile_tab_ids_[i]);
    if (old_index < 0) {
      continue;
    }

    const gfx::Rect old_bounds = snapshot_bounds_[old_index];

    const gfx::Rect new_bounds = TileBoundsInGrid(i);

    if (old_bounds.IsEmpty() || new_bounds.IsEmpty() ||
        old_bounds == new_bounds) {
      continue;
    }

    const float dx = static_cast<float>(old_bounds.x() - new_bounds.x());
    const float dy = static_cast<float>(old_bounds.y() - new_bounds.y());

    gfx::Transform start;
    start.Translate(dx, dy);
    tile_views_[i]->layer()->SetTransform(start);

    views::AnimationBuilder()
        .Once()
        .SetDuration(kReorderDuration)
        .SetTransform(tile_views_[i]->layer(), gfx::Transform(),
                      gfx::Tween::EASE_IN_OUT);
  }
}

void MahoSidebarFavoritesGridView::AnimateInsertion(size_t tile_index) {
  if (tile_index >= kTileCount || !tile_views_[tile_index]) {
    return;
  }

  views::View* tile = tile_views_[tile_index];
  if (!tile->layer()) {
    return;
  }

  ApplyImmediateLayoutForAnimation();
  const gfx::Rect bounds = TileBoundsInGrid(tile_index);
  last_insertion_animation_bounds_for_testing_ = bounds;
  if (bounds.IsEmpty()) {
    return;
  }
  tile->layer()->SetOpacity(0.0f);
  const float cx = bounds.width() / 2.0f;
  const float cy = bounds.height() / 2.0f;
  gfx::Transform scale_from;
  scale_from.Translate(cx, cy);
  scale_from.Scale(kInsertionStartScale, kInsertionStartScale);
  scale_from.Translate(-cx, -cy);
  tile->layer()->SetTransform(scale_from);

  views::AnimationBuilder()
      .Once()
      .SetDuration(kInsertionDuration)
      .SetOpacity(tile->layer(), 1.0f, gfx::Tween::EASE_OUT)
      .SetTransform(tile->layer(), gfx::Transform(), gfx::Tween::EASE_OUT);
}

void MahoSidebarFavoritesGridView::AnimateRemoval(
    size_t tile_index,
    base::OnceClosure on_complete) {
  if (tile_index >= kTileCount || !tile_views_[tile_index]) {
    if (on_complete) {
      std::move(on_complete).Run();
    }
    return;
  }

  views::View* tile = tile_views_[tile_index];
  if (!tile->layer() || !tile->GetVisible()) {
    if (on_complete) {
      std::move(on_complete).Run();
    }
    return;
  }

  const gfx::Rect bounds = tile->bounds();
  const float cx = bounds.width() / 2.0f;
  const float cy = bounds.height() / 2.0f;
  gfx::Transform scale_to;
  scale_to.Translate(cx, cy);
  scale_to.Scale(kRemovalEndScale, kRemovalEndScale);
  scale_to.Translate(-cx, -cy);

  views::AnimationBuilder()
      .OnEnded(base::BindOnce(
          [](views::View* v, base::OnceClosure cb) {
            v->SetVisible(false);
            if (v->layer()) {
              v->layer()->SetOpacity(1.0f);
              v->layer()->SetTransform(gfx::Transform());
            }
            if (cb) {
              std::move(cb).Run();
            }
          },
          base::Unretained(tile), std::move(on_complete)))
      .Once()
      .SetDuration(kRemovalDuration)
      .SetOpacity(tile->layer(), 0.0f, gfx::Tween::EASE_IN)
      .SetTransform(tile->layer(), scale_to, gfx::Tween::EASE_IN);
}

void MahoSidebarFavoritesGridView::AnimateDragPreview(
    int preview_index,
    const std::string& dragged_tab_id) {
  const int base_count = static_cast<int>(GetVisibleFavoriteCount());
  if (base_count <= 0) {
    return;
  }
  const bool is_insertion =
      std::find(tile_tab_ids_.begin(), tile_tab_ids_.end(), dragged_tab_id) ==
      tile_tab_ids_.end();
  const int item_count =
      is_insertion ? std::min(base_count + 1, static_cast<int>(kTileCount))
                   : base_count;

  const auto ideal_bounds = GetTileBoundsForCount(item_count);

  int slot = 0;
  for (size_t i = 0; i < kTileCount; ++i) {
    if (!tile_views_[i] || !tile_views_[i]->GetVisible() ||
        !tile_views_[i]->layer() || !tile_views_[i]->parent()) {
      continue;
    }
    if (tile_tab_ids_[i] == dragged_tab_id) {
      continue;
    }

    if (slot == preview_index) {
      slot++;
    }

    if (slot >= item_count) {
      break;
    }

    gfx::Rect current = tile_views_[i]->bounds();
    gfx::Point current_origin = current.origin();
    views::View::ConvertPointToTarget(tile_views_[i]->parent(), this,
                                      &current_origin);

    const gfx::Rect target = ideal_bounds[slot];
    const float dx = static_cast<float>(target.x() - current_origin.x());
    const float dy = static_cast<float>(target.y() - current_origin.y());
    // Scale so structure-change transitions (e.g. 1-col wide card -> 2-col half
    // tile) morph correctly. Uniform ratio when current==target.
    const float sx = current.width() > 0
                         ? static_cast<float>(target.width()) / current.width()
                         : 1.0f;
    const float sy = current.height() > 0
                         ? static_cast<float>(target.height()) / current.height()
                         : 1.0f;

    const bool needs_translate =
        std::abs(dx) > 0.5f || std::abs(dy) > 0.5f;
    const bool needs_scale =
        std::abs(sx - 1.0f) > 0.01f || std::abs(sy - 1.0f) > 0.01f;

    gfx::Transform glyph_transform;
    if (needs_scale && tile_glyph_containers_[i]) {
      const gfx::Rect glyph_bounds = tile_glyph_containers_[i]->bounds();
      const float glyph_cx = glyph_bounds.width() / 2.0f;
      const float glyph_cy = glyph_bounds.height() / 2.0f;
      const float inverse_sx = std::abs(sx) > 0.001f ? 1.0f / sx : 1.0f;
      const float inverse_sy = std::abs(sy) > 0.001f ? 1.0f / sy : 1.0f;
      glyph_transform.Translate(glyph_cx, glyph_cy);
      glyph_transform.Scale(inverse_sx, inverse_sy);
      glyph_transform.Translate(-glyph_cx, -glyph_cy);
    }

    if (needs_translate || needs_scale) {
      gfx::Transform transform;
      transform.Translate(dx, dy);
      transform.Scale(sx, sy);

      views::AnimationBuilder()
          .Once()
          .SetDuration(base::Milliseconds(180))
          .SetTransform(tile_views_[i]->layer(), transform,
                        gfx::Tween::EASE_IN_OUT);
    } else {
      views::AnimationBuilder()
          .Once()
          .SetDuration(base::Milliseconds(180))
          .SetTransform(tile_views_[i]->layer(), gfx::Transform(),
                        gfx::Tween::EASE_IN_OUT);
    }
    if (tile_glyph_containers_[i] && tile_glyph_containers_[i]->layer()) {
      views::AnimationBuilder()
          .Once()
          .SetDuration(base::Milliseconds(180))
          .SetTransform(tile_glyph_containers_[i]->layer(), glyph_transform,
                        gfx::Tween::EASE_IN_OUT);
    }
    slot++;
  }
}

void MahoSidebarFavoritesGridView::CancelAnimations() {
  for (size_t i = 0; i < kTileCount; ++i) {
    if (tile_views_[i] && tile_views_[i]->layer()) {
      tile_views_[i]->layer()->GetAnimator()->StopAnimating();
      tile_views_[i]->layer()->SetTransform(gfx::Transform());
    }
    if (tile_glyph_containers_[i] && tile_glyph_containers_[i]->layer()) {
      tile_glyph_containers_[i]->layer()->GetAnimator()->StopAnimating();
      tile_glyph_containers_[i]->layer()->SetTransform(gfx::Transform());
    }
  }
  current_drag_preview_index_ = -1;
}

void MahoSidebarFavoritesGridView::ShowInsertionPreview(
    const gfx::Point& cursor,
    const std::string& tab_id) {
  if (tab_id.empty()) {
    HideInsertionPreview();
    return;
  }

  if (!drag_preview_tile_) {
    auto tile = std::make_unique<views::View>();
    tile->SetPaintToLayer();
    tile->layer()->SetFillsBoundsOpaquely(false);
    tile->SetPreferredSize(gfx::Size(0, 0));
    tile->SetVisible(false);
    tile->SetCanProcessEventsWithinSubtree(false);
    tile->SetBackground(views::CreateRoundedRectBackground(
        palette_.row_active, sidebar_layout::kFavoriteTileCornerRadiusDp));
    tile->SetBorder(views::CreateEmptyBorder(gfx::Insets(1)));

    auto* tile_layout = tile->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical,
        sidebar_layout::kFavoriteTileInsets, 0));
    tile_layout->set_main_axis_alignment(
        views::BoxLayout::MainAxisAlignment::kCenter);
    tile_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);

    auto glyph_container = std::make_unique<views::View>();
    glyph_container->SetPreferredSize(
        gfx::Size(sidebar_layout::kFavoriteGlyphContainerSizeDp,
                  sidebar_layout::kFavoriteGlyphContainerSizeDp));
    auto* glyph_layout = glyph_container->SetLayoutManager(
        std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kVertical, gfx::Insets(), 0));
    glyph_layout->set_main_axis_alignment(
        views::BoxLayout::MainAxisAlignment::kCenter);
    glyph_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);

    auto icon = std::make_unique<views::ImageView>();
    icon->SetImageSize(gfx::Size(sidebar_layout::kFavoriteGlyphSizeDp,
                                 sidebar_layout::kFavoriteGlyphSizeDp));
    drag_preview_icon_ = icon.get();
    glyph_container->AddChildView(std::move(icon));
    tile->AddChildView(std::move(glyph_container));

    drag_preview_tile_ = AddChildView(std::move(tile));
    drag_preview_tile_->SetProperty(views::kViewIgnoredByLayoutKey, true);
    drag_preview_tile_->SetPreferredSize(
        gfx::Size(sidebar_layout::kFavoriteTileHeightDp,
                  sidebar_layout::kFavoriteTileHeightDp));
  }

  const int slot_count = std::min(static_cast<int>(favorite_count_) + 1,
                                  static_cast<int>(kTileCount));
  const auto slot_bounds = GetTileBoundsForCount(slot_count);
  gfx::Size tile_size = slot_bounds[0].size();
  if (tile_size.IsEmpty()) {
    tile_size = gfx::Size(sidebar_layout::kFavoriteTileHeightDp,
                          sidebar_layout::kFavoriteTileHeightDp);
  }
  const gfx::Rect phantom_bounds(cursor.x() - tile_size.width() / 2,
                                 cursor.y() - tile_size.height() / 2,
                                 tile_size.width(), tile_size.height());

  drag_preview_slot_ = -1;
  const bool was_hidden = !drag_preview_tile_->GetVisible();
  drag_preview_tile_->SetBoundsRect(phantom_bounds);
  if (was_hidden) {
    drag_preview_tile_->SetVisible(true);
    drag_preview_tile_->layer()->SetOpacity(1.f);
    drag_preview_tile_->parent()->ReorderChildView(drag_preview_tile_.get(), -1);
  }

  if (drag_preview_tab_id_ == tab_id && drag_preview_url_.is_valid()) {
    return;
  }
  drag_preview_tab_id_ = tab_id;
  drag_preview_url_ = GURL();

  ui::ImageModel fav;
  GURL resolved_url;
  if (browser_ && browser_->GetTabStripModel()) {
    TabStripModel* strip = browser_->GetTabStripModel();
    const int strip_count = strip->count();
    for (int i = 0; i < strip_count; ++i) {
      content::WebContents* contents = strip->GetWebContentsAt(i);
      if (!contents) {
        continue;
      }
      auto* helper = MahoTabIdHelper::FromWebContents(contents);
      if (!helper || helper->stable_tab_id() != tab_id) {
        continue;
      }
      resolved_url = contents->GetVisibleURL();
      auto* tab_iface = tabs::TabInterface::GetFromContents(contents);
      if (tab_iface) {
        fav = TabUIHelper::From(tab_iface)->GetFavicon();
      }
      break;
    }
  }

  drag_preview_url_ = resolved_url;

  if (fav.IsEmpty() && resolved_url.is_valid() && browser_) {
    drag_preview_task_tracker_.TryCancelAll();
    favicon::FaviconService* favicon_service =
        FaviconServiceFactory::GetForProfile(
            browser_->GetProfile(), ServiceAccessType::EXPLICIT_ACCESS);
    if (favicon_service) {
      favicon_service->GetFaviconImageForPageURL(
          resolved_url,
          base::BindOnce(
              &MahoSidebarFavoritesGridView::OnInsertionPreviewFaviconLoaded,
              weak_factory_.GetWeakPtr(), tab_id, resolved_url),
          &drag_preview_task_tracker_);
    }
    fav = CreateThemedGlobeIcon(palette_, sidebar_layout::kFavoriteGlyphSizeDp);
  } else if (fav.IsEmpty()) {
    fav = CreateThemedGlobeIcon(palette_, sidebar_layout::kFavoriteGlyphSizeDp);
  }

  drag_preview_icon_->SetImage(fav);
  drag_preview_icon_->SetImageSize(
      gfx::Size(sidebar_layout::kFavoriteGlyphSizeDp,
                sidebar_layout::kFavoriteGlyphSizeDp));
}

void MahoSidebarFavoritesGridView::HideInsertionPreview() {
  drag_preview_task_tracker_.TryCancelAll();
  drag_preview_tab_id_.clear();
  drag_preview_url_ = GURL();
  drag_preview_slot_ = -1;
  if (drag_preview_tile_) {
    drag_preview_tile_->SetVisible(false);
  }
}

void MahoSidebarFavoritesGridView::OnInsertionPreviewFaviconLoaded(
    const std::string& tab_id,
    const GURL& requested_url,
    const favicon_base::FaviconImageResult& result) {
  if (drag_preview_tab_id_ != tab_id || drag_preview_url_ != requested_url ||
      !drag_preview_icon_) {
    return;
  }
  if (result.image.IsEmpty()) {
    return;
  }
  drag_preview_icon_->SetImage(ui::ImageModel::FromImage(result.image));
  drag_preview_icon_->SetImageSize(
      gfx::Size(sidebar_layout::kFavoriteGlyphSizeDp,
                sidebar_layout::kFavoriteGlyphSizeDp));
}

ui::ImageModel MahoSidebarFavoritesGridView::ResolveFaviconForTabId(
    const std::string& tab_id) {
  if (tab_id.empty() || !browser_ || !browser_->GetTabStripModel()) {
    return ui::ImageModel();
  }
  TabStripModel* strip = browser_->GetTabStripModel();
  const int strip_count = strip->count();
  for (int i = 0; i < strip_count; ++i) {
    content::WebContents* contents = strip->GetWebContentsAt(i);
    if (!contents) {
      continue;
    }
    auto* helper = MahoTabIdHelper::FromWebContents(contents);
    if (!helper || helper->stable_tab_id() != tab_id) {
      continue;
    }
    content::NavigationEntry* entry =
        contents->GetController().GetLastCommittedEntry();
    if (!entry || !entry->GetFavicon().valid) {
      break;
    }
    auto* tab_iface = tabs::TabInterface::GetFromContents(contents);
    if (tab_iface) {
      ui::ImageModel fav = TabUIHelper::From(tab_iface)->GetFavicon();
      if (!fav.IsEmpty()) {
        return fav;
      }
    }
    break;
  }
  return ui::ImageModel();
}


void MahoSidebarFavoritesGridView::OnFavoritesZoneExited() {
  ResetDragFeedback(DragIdentityCleanup::kKeep, true);
}

void MahoSidebarFavoritesGridView::ForceRefreshFromDrop() {
  auto* sidebar_view = FindSidebarViewAncestor(this);
  if (sidebar_view) {
    sidebar_view->RefreshFavoritesSynchronouslyAfterDrop();
  }
}

}  // namespace maho
