// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_favorites_grid_view.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <set>

#include "base/containers/flat_map.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/logging.h"
#include "base/memory/raw_ptr.h"
#include "base/strings/utf_string_conversions.h"
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
#include "maho/browser/ui/views/maho_lucide_icons/vector_icons.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_state_adapter.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_tab_list_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_drag_util.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_drop_planner.h"
#include "ui/base/clipboard/clipboard_format_type.h"
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
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace maho {

namespace {

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
constexpr int kDropIndicatorThicknessDp = 2;
constexpr int kDropIndicatorInsetDp = 3;

constexpr base::TimeDelta kReorderDuration = base::Milliseconds(220);
constexpr base::TimeDelta kInsertionDuration = base::Milliseconds(220);
constexpr base::TimeDelta kRemovalDuration = base::Milliseconds(180);
constexpr float kInsertionStartScale = 0.7f;
constexpr float kRemovalEndScale = 0.7f;

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

    close_button_ = AddChildView(views::CreateVectorImageButtonWithNativeTheme(
        base::BindRepeating(&EmptyFavoritesPlaceholderView::OnClosePressed,
                            base::Unretained(this)),
        maho_lucide_icons::kXIcon, kFavoriteEmptyCloseSizeDp,
        ui::kColorSysOnSurfaceSubtle, ui::kColorSysOnSurface,
        ui::kColorSysOnSurface));
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
    title_label_->SetEnabledColor(ui::kColorSysOnSurface);
    title_label_->SetTextStyle(views::style::STYLE_BODY_3_MEDIUM);
    title_label_->SetHorizontalAlignment(gfx::ALIGN_CENTER);

    description_label_ = AddChildView(std::make_unique<views::Label>(
        std::u16string(u"Favorites keep your most used sites\nand apps close")));
    description_label_->SetAutoColorReadabilityEnabled(false);
    description_label_->SetEnabledColor(ui::kColorSysOnSurfaceSubtle);
    description_label_->SetTextStyle(views::style::STYLE_BODY_5_MEDIUM);
    description_label_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
    description_label_->SetMultiLine(true);

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

 private:
  void OnClosePressed() {
    if (on_dismiss_) {
      on_dismiss_.Run();
    }
  }

  void UpdateAppearance() {
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
  }

  base::RepeatingClosure on_dismiss_;
  raw_ptr<views::View> card_background_ = nullptr;
  raw_ptr<views::View> badge_ = nullptr;
  raw_ptr<views::ImageView> badge_image_ = nullptr;
  raw_ptr<views::ImageButton> close_button_ = nullptr;
  raw_ptr<views::Label> title_label_ = nullptr;
  raw_ptr<views::Label> description_label_ = nullptr;
  bool highlighted_ = false;
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

  void OnThemeChanged() override {
    views::Button::OnThemeChanged();
    UpdateAppearance();
  }

  void StateChanged(views::Button::ButtonState old_state) override {
    views::Button::StateChanged(old_state);
    UpdateAppearance();
  }

  void OnEnabledChanged() override {
    views::Button::OnEnabledChanged();
    UpdateAppearance();
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
        drop_target_->ForwardDropEventToGrid(event, this));
  }

  DropCallback GetDropCallback(const ui::DropTargetEvent& event) override {
    if (!drop_target_) {
      return base::BindOnce(
          [](const ui::DropTargetEvent&,
             ui::mojom::DragOperation& output_drag_op,
             std::unique_ptr<ui::LayerTreeOwner>) {
            output_drag_op = ui::mojom::DragOperation::kNone;
          });
    }
    return drop_target_->GetDropCallback(
        drop_target_->ForwardDropEventToGrid(event, this));
  }

  void OnDragExited() override {
    views::Button::OnDragExited();
    if (drop_target_) {
      drop_target_->OnDragExited();
    }
  }

 private:
  void UpdateAppearance() {
    const bool hovered =
        GetEnabled() &&
        (GetState() == STATE_HOVERED || GetState() == STATE_PRESSED);
    SetBorder(views::CreateRoundedRectBorder(
        1, sidebar_layout::kFavoriteTileCornerRadiusDp,
        hovered ? ui::kColorSysNeutralOutline : ui::kColorSysSurfaceVariant));
    SetBackground(views::CreateRoundedRectBackground(
        hovered ? ui::kColorSysSurface3 : ui::kColorSysSurface1,
        sidebar_layout::kFavoriteTileCornerRadiusDp));
  }

  base::RepeatingCallback<void(const std::string&)> drag_done_callback_;
  raw_ptr<MahoSidebarFavoritesGridView> drop_target_ = nullptr;
  std::string tab_id_;
  std::string dragging_tab_id_;
  size_t tile_index_ = 0;
};

struct FavoriteTileSpec {
  raw_ptr<const gfx::VectorIcon> icon;
  std::u16string text;
  GURL url;
  std::string tab_id;
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
    specs[index].url = model.items[index].url;
    specs[index].tab_id = model.items[index].tab_id;
  }
  return specs;
}

views::Label* AddPlaceholderLabel(views::View* parent, std::u16string text) {
  auto label = std::make_unique<views::Label>(std::move(text));
  label->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  label->SetAutoColorReadabilityEnabled(false);
  label->SetEnabledColor(ui::kColorSysOnSurfaceSubtle);
  label->SetMultiLine(false);
  label->SetElideBehavior(gfx::ELIDE_TAIL);
  label->SetTextStyle(views::style::STYLE_BODY_4);
  label->SetSubpixelRenderingEnabled(false);
  return parent->AddChildView(std::move(label));
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
      prefs_(browser && browser->profile() ? browser->profile()->GetPrefs()
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
      glyph_container->SetBorder(nullptr);
    tile_glyph_containers_[index] = glyph_container.get();
    tile->AddChildView(std::move(glyph_container));

    tile_labels_[index] = AddPlaceholderLabel(tile.get(), std::u16string());
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

MahoSidebarFavoritesGridView::~MahoSidebarFavoritesGridView() = default;

void MahoSidebarFavoritesGridView::OnBoundsChanged(
    const gfx::Rect& previous_bounds) {
  CancelAnimations();
  views::View::OnBoundsChanged(previous_bounds);
  if (bounds().size() != previous_bounds.size()) {
    RebuildGridRows(favorite_count_);
    UpdateContainerOwnedPreferredHeight(favorite_count_);
  }
}

void MahoSidebarFavoritesGridView::Update(
    const MahoSidebarFavoritesModel& model) {
  cancelable_task_tracker_.TryCancelAll();
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
      tile_views_[index]->SetVisible(false);
      tile_urls_[index] = GURL();
      tile_titles_[index] = std::u16string();
      tile_tab_ids_[index].clear();
    }
    return;
  }

  const auto specs = BuildTileSpecs(model);

  // Build a URL→favicon map by scanning the tab strip once (O(M log M)),
  // replacing the previous O(N×M) per-tile inner loop.
  base::flat_map<GURL, ui::ImageModel> live_tab_favicons;
  if (browser_ && browser_->tab_strip_model()) {
    TabStripModel* strip = browser_->tab_strip_model();
    const int strip_count = strip->count();
    std::vector<std::pair<GURL, ui::ImageModel>> favicon_pairs;
    favicon_pairs.reserve(static_cast<size_t>(strip_count));
    for (int i = 0; i < strip_count; ++i) {
      content::WebContents* contents = strip->GetWebContentsAt(i);
      if (!contents) {
        continue;
      }
      GURL url = contents->GetVisibleURL();
      if (!url.is_valid()) {
        continue;
      }
      content::NavigationEntry* entry =
          contents->GetController().GetLastCommittedEntry();
      const bool has_real_favicon =
          entry && entry->GetFavicon().valid;
      if (!has_real_favicon) {
        continue;
      }
      auto* tab_iface = tabs::TabInterface::GetFromContents(contents);
      if (tab_iface) {
        favicon_pairs.emplace_back(std::move(url),
                                   TabUIHelper::From(tab_iface)->GetFavicon());
      }
    }
    live_tab_favicons = base::flat_map<GURL, ui::ImageModel>(
        std::move(favicon_pairs));
  }

  for (size_t index = 0; index < tile_views_.size(); ++index) {
    const bool has_item = specs[index].url.is_valid();
    tile_urls_[index] = has_item ? specs[index].url : GURL();
    tile_titles_[index] = has_item ? specs[index].text : std::u16string();
    tile_tab_ids_[index] = has_item ? specs[index].tab_id : std::string();
    tile_views_[index]->SetVisible(has_item);
    tile_views_[index]->SetEnabled(has_item);
    tile_views_[index]->SetTooltipText(has_item ? specs[index].text
                                                : std::u16string());
    tile_views_[index]->SetAccessibleName(has_item ? specs[index].text
                                                   : u"Empty favorite slot");

    auto* tile_btn = static_cast<FavoriteTileButton*>(tile_views_[index].get());
    tile_btn->set_tab_id(tile_tab_ids_[index]);

    if (has_item) {
      // O(log M) map lookup instead of O(M) tab-strip scan per tile.
      ui::ImageModel fav;
      const auto it = live_tab_favicons.find(specs[index].url);
      if (it != live_tab_favicons.end()) {
        fav = it->second;
      }

      if (fav.IsEmpty()) {
        bool async_requested = false;
        if (browser_) {
          favicon::FaviconService* favicon_service =
              FaviconServiceFactory::GetForProfile(
                  browser_->profile(), ServiceAccessType::EXPLICIT_ACCESS);
          if (favicon_service) {
            favicon_service->GetFaviconImageForPageURL(
                specs[index].url,
                base::BindOnce(&MahoSidebarFavoritesGridView::OnFaviconLoaded,
                               weak_factory_.GetWeakPtr(), index, specs[index].url),
                &cancelable_task_tracker_);
            async_requested = true;
          }
        }
        if (!async_requested) {
          fav = ui::ImageModel::FromVectorIcon(vector_icons::kGlobeIcon);
        } else {
          // Temporarily show globe icon while async load is in flight.
          fav = ui::ImageModel::FromVectorIcon(vector_icons::kGlobeIcon);
        }
      }

      tile_icons_[index]->SetImage(std::move(fav));
      tile_icons_[index]->SetImageSize(
          gfx::Size(sidebar_layout::kFavoriteGlyphSizeDp,
                    sidebar_layout::kFavoriteGlyphSizeDp));
      if (tile_glyph_containers_[index]) {
        tile_glyph_containers_[index]->SetBackground(
            views::CreateRoundedRectBackground(
                ui::kColorSysSurface,
                sidebar_layout::kFavoriteGlyphContainerRadiusDp));
        tile_glyph_containers_[index]->SetBorder(views::CreateRoundedRectBorder(
            1, sidebar_layout::kFavoriteGlyphContainerRadiusDp,
            ui::kColorSysSurfaceVariant));
      }
      tile_labels_[index]->SetText(specs[index].text);
      tile_labels_[index]->SetVisible(false);
    } else {
      tile_labels_[index]->SetVisible(false);
    }
  }

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
    } else if (!snapshot_bounds_[old_idx].IsEmpty() &&
               snapshot_bounds_[old_idx] != tile_views_[i]->bounds()) {
      has_reorder = true;
    }
  }

  if (has_reorder) {
    AnimateReorder();
  }
}

void MahoSidebarFavoritesGridView::OnFaviconLoaded(
    size_t index,
    const GURL& requested_url,
    const favicon_base::FaviconImageResult& result) {
  if (index >= tile_urls_.size() || tile_urls_[index] != requested_url) {
    return;
  }
  if (!result.image.IsEmpty()) {
    tile_icons_[index]->SetImage(ui::ImageModel::FromImage(result.image));
  } else {
    tile_icons_[index]->SetImage(ui::ImageModel::FromVectorIcon(
        vector_icons::kGlobeIcon));
  }
  tile_icons_[index]->SetImageSize(
      gfx::Size(sidebar_layout::kFavoriteGlyphSizeDp,
                sidebar_layout::kFavoriteGlyphSizeDp));
}

MahoSidebarFavoritesGridView::LayoutSpec
MahoSidebarFavoritesGridView::GetLayoutSpec(size_t favorite_count,
                                           int available_width) {
  const size_t count = std::min(favorite_count, kTileCount);
  LayoutSpec spec;

  switch (count) {
    case 0:
      break;
    case 1:
      spec.row_counts = {1};
      spec.max_columns = 3;
      break;
    case 2:
      spec.row_counts = {2};
      spec.max_columns = 3;
      break;
    default:
      spec.max_columns = 3;
      for (size_t remaining = count; remaining > 0;) {
        const int row_count = std::min<int>(sidebar_layout::kFavoritesColumns,
                                            remaining);
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
    drag_over_drop_target_ = false;
    drag_reveal_active_ = false;
    SetBorder(nullptr);
    UpdateEmptyStateHighlight(false);
    UpdateContainerOwnedPreferredHeight(favorite_count_);
    return static_cast<int>(ui::mojom::DragOperation::kNone);
  }

  const size_t favorite_count = favorite_count_;
  if (payload.origin != SidebarDragOrigin::kFavorites &&
      favorite_count >= kTileCount) {
    drag_over_drop_target_ = false;
    drag_reveal_active_ = false;
    SetBorder(nullptr);
    UpdateEmptyStateHighlight(false);
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
    if (preview_index != current_drag_preview_index_) {
      current_drag_preview_index_ = preview_index;
      AnimateDragPreview(preview_index, payload.node_id);
    }

    SetBorder(views::CreateSolidSidedBorder(
        gfx::Insets::TLBR(kDropIndicatorThicknessDp, kDropIndicatorInsetDp,
                          kDropIndicatorThicknessDp, kDropIndicatorInsetDp),
        ui::kColorSysPrimary));
  } else {
    current_drag_preview_index_ = -1;
  }

  return static_cast<int>(ui::mojom::DragOperation::kMove);
}

views::View::DropCallback MahoSidebarFavoritesGridView::GetDropCallback(
    const ui::DropTargetEvent& event) {
  SidebarDragPayload payload;
  const bool has_payload = ReadMahoDragData(event.data(), payload) &&
                           payload.node_kind == SidebarNodeKind::kTab;
  const size_t favorite_count = favorite_count_;
  const int target_index = has_payload
                               ? ResolveDropIndex(
                                     event.location(), payload.node_id,
                                     payload.origin == SidebarDragOrigin::kFavorites)
                                : 0;
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
          return;
        }
        output_drag_op = ui::mojom::DragOperation::kMove;
        if (weak_grid) {
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

void MahoSidebarFavoritesGridView::OnDragEnded() {
  active_drag_tab_id_.clear();
  current_drag_preview_index_ = -1;
  for (size_t i = 0; i < kTileCount; ++i) {
    if (tile_views_[i] && tile_views_[i]->layer()) {
      tile_views_[i]->layer()->GetAnimator()->StopAnimating();
      tile_views_[i]->layer()->SetOpacity(1.f);
      tile_views_[i]->layer()->SetTransform(gfx::Transform());
    }
  }
}

void MahoSidebarFavoritesGridView::OnDragExited() {
  current_drag_preview_index_ = -1;
  drag_over_drop_target_ = false;
  SetBorder(nullptr);
  UpdateEmptyStateHighlight(false);
  for (size_t i = 0; i < kTileCount; ++i) {
    if (tile_views_[i] && tile_views_[i]->GetVisible() &&
        tile_views_[i]->layer()) {
      views::AnimationBuilder()
          .Once()
          .SetDuration(base::Milliseconds(180))
          .SetTransform(tile_views_[i]->layer(), gfx::Transform(),
                        gfx::Tween::EASE_IN_OUT);
    }
  }
  if (drag_reveal_active_) {
    drag_reveal_active_ = false;
    UpdateContainerOwnedPreferredHeight(0);
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
    bool source_is_favorite) const {
  const int favorite_count = static_cast<int>(GetVisibleFavoriteCount());
  // Always resolve against the full N-item grid so the index matches
  // AnimateDragPreview's GetTileBoundsForCount(N) layout.  Using N-1
  // produced a different grid geometry, mapping the cursor to the wrong slot.
  const int item_count = favorite_count;
  const int target_index = TileIndexForPoint(point, item_count);

  if (!source_is_favorite) {
    return std::clamp(target_index, 0, item_count);
  }

  const bool source_exists =
      std::find(tile_tab_ids_.begin(), tile_tab_ids_.end(), source_tab_id) !=
      tile_tab_ids_.end();
  if (!source_exists) {
    return std::clamp(target_index, 0, item_count);
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
    if (point.x() >= last_bounds.CenterPoint().x() ||
        point.y() >= last_bounds.CenterPoint().y()) {
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
  const int content_width = std::max(0, content_view_->width());
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
  return std::count_if(tile_urls_.begin(), tile_urls_.end(),
                       [](const GURL& url) { return url.is_valid(); });
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

  DispatchShellEvent("unfavorite_tab", {{"tab_id", tab_id}});
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

  // 1. Prefer activating an existing live tab when the favorite has a tab id
  //    that matches a live WebContents. Single authoritative path:
  //    MahoTabIdHelper::stable_tab_id(). Mirrors
  //    MahoSidebarTabListView::ResolveTabStripIndexForTabId().
  const std::string& tab_id = tile_tab_ids_[index];
  if (!tab_id.empty()) {
    TabStripModel* strip = browser_->tab_strip_model();
    if (strip) {
      for (int i = 0; i < strip->count(); ++i) {
        content::WebContents* contents = strip->GetWebContentsAt(i);
        if (!contents) {
          continue;
        }
        auto* helper = MahoTabIdHelper::FromWebContents(contents);
        if (helper && helper->stable_tab_id() == tab_id) {
          strip->ActivateTabAt(
              i, TabStripUserGestureDetails(
                     TabStripUserGestureDetails::GestureType::kOther));
          return;
        }
      }
    }
  }

  // 2. Fallback: no live tab matches. Open the favorite URL in a NEW
  //    foreground tab so the user's current tab is preserved.
  if (!tile_urls_[index].is_valid()) {
    return;
  }
  browser_->OpenURL(content::OpenURLParams(
                        tile_urls_[index], content::Referrer(),
                        WindowOpenDisposition::NEW_FOREGROUND_TAB,
                        ui::PAGE_TRANSITION_AUTO_BOOKMARK, false),
                    base::NullCallback());
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

  active_context_menu_ = std::make_unique<MahoFavoritesContextMenu>(
      browser_, tile_urls_[index], tile_titles_[index], tile_tab_ids_[index]);
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

void MahoSidebarFavoritesGridView::SnapshotCurrentBounds() {
  for (size_t i = 0; i < kTileCount; ++i) {
    if (tile_views_[i] && tile_views_[i]->GetVisible() &&
        tile_views_[i]->parent()) {
      // Store in grid-view-local coordinates (tiles live inside row views)
      gfx::Rect local = tile_views_[i]->bounds();
      gfx::Point origin = local.origin();
      views::View::ConvertPointToTarget(tile_views_[i]->parent(), this,
                                        &origin);
      snapshot_bounds_[i] = gfx::Rect(origin, local.size());
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

    gfx::Rect new_bounds = tile_views_[i]->bounds();
    gfx::Point new_origin = new_bounds.origin();
    views::View::ConvertPointToTarget(tile_views_[i]->parent(), this,
                                      &new_origin);
    new_bounds.set_origin(new_origin);

    if (old_bounds.IsEmpty() || old_bounds == new_bounds) {
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

  tile->layer()->SetOpacity(0.0f);
  const gfx::Rect bounds = tile->bounds();
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
  const int item_count = static_cast<int>(GetVisibleFavoriteCount());
  if (item_count <= 0) {
    return;
  }

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

    if (std::abs(dx) > 0.5f || std::abs(dy) > 0.5f) {
      gfx::Transform offset;
      offset.Translate(dx, dy);

      views::AnimationBuilder()
          .Once()
          .SetDuration(base::Milliseconds(180))
          .SetTransform(tile_views_[i]->layer(), offset,
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
  }
  current_drag_preview_index_ = -1;
}

}  // namespace maho
