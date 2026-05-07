// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_favorites_grid_view.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <set>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/raw_ptr.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tab_ui_helper.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/page_navigator.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/referrer.h"
#include "components/vector_icons/vector_icons.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_state_adapter.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_tab_list_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_drag_util.h"
#include "ui/base/clipboard/clipboard_format_type.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/dragdrop/mojom/drag_drop_types.mojom.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/events/event_constants.h"
#include "ui/base/models/image_model.h"
#include "cc/paint/path_effect.h"
#include "ui/base/ui_base_types.h"
#include "ui/base/window_open_disposition.h"
#include "ui/color/color_id.h"
#include "ui/compositor/layer_tree_owner.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/size.h"
#include "ui/display/screen.h"
#include "cc/paint/paint_flags.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/paint_info.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/drag_controller.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/style/typography.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace maho {

namespace {

constexpr int kFavoriteSectionSpacingDp = 4;
constexpr int kFavoriteShellContentSpacingDp = 0;
constexpr int kFavoriteTileContentSpacingDp = 0;
constexpr int kFavoriteSingleCardHeightDp = sidebar_layout::kFavoriteTileHeightDp;
constexpr int kFavoriteTwoTileHeightDp = sidebar_layout::kFavoriteTileHeightDp;
constexpr int kFavoriteEmptyStateHeightDp = 92;
constexpr int kDragStartThresholdDp = 5;
constexpr int kDropIndicatorThicknessDp = 2;
constexpr int kDropIndicatorInsetDp = 3;

class FavoriteTileButton : public views::Button,
                          public views::DragController {
 public:
  FavoriteTileButton(views::Button::PressedCallback callback,
                     base::RepeatingClosure drag_done_callback,
                     raw_ptr<MahoSidebarFavoritesGridView> drop_target,
                     std::string tab_id)
      : views::Button(std::move(callback)),
        drag_done_callback_(std::move(drag_done_callback)),
        drop_target_(drop_target),
        tab_id_(std::move(tab_id)) {
    UpdateAppearance();
    set_drag_controller(this);
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
    SidebarDragPayload payload;
    payload.node_kind = SidebarNodeKind::kTab;
    payload.node_id = tab_id_;
    payload.origin = SidebarDragOrigin::kFavorites;
    WriteMahoDragData(payload, data);
    data->provider().SetDragImage(CreateSidebarDragImage(sender),
                                  press_pt.OffsetFromOrigin());
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

  void OnDragDone() override {
    views::Button::OnDragDone();
    if (drag_done_callback_) {
      drag_done_callback_.Run();
    }
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
      return views::Button::GetDropCallback(event);
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

  base::RepeatingClosure drag_done_callback_;
  raw_ptr<MahoSidebarFavoritesGridView> drop_target_ = nullptr;
  std::string tab_id_;
};

struct FavoriteTileSpec {
  raw_ptr<const gfx::VectorIcon> icon;
  std::u16string text;
  GURL url;
  std::string tab_id;
};

std::u16string BuildFavoriteText(const MahoSidebarFavoriteItemModel& item) {
  if (!item.title.empty()) {
    return item.title;
  }
  if (item.url.is_valid()) {
    if (!item.url.host().empty()) {
      std::string host(item.url.host());
      if (host.rfind("www.", 0) == 0) {
        host.erase(0, 4);
      }
      return base::UTF8ToUTF16(host);
    }
    return base::UTF8ToUTF16(item.url.possibly_invalid_spec());
  }
  if (!item.subtitle.empty()) {
    return item.subtitle;
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
    : browser_(browser) {
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

  for (size_t index = 0; index < tile_views_.size(); ++index) {
    auto tile = std::make_unique<FavoriteTileButton>(
        base::BindRepeating(&MahoSidebarFavoritesGridView::OnTilePressed,
                            weak_factory_.GetWeakPtr(), index),
        base::BindRepeating(&MahoSidebarFavoritesGridView::OnTileDragDone,
                            weak_factory_.GetWeakPtr(), index),
        this,
        std::string());
    tile->SetPreferredSize(
        gfx::Size(1, sidebar_layout::kFavoriteTileHeightDp));
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
  views::View::OnBoundsChanged(previous_bounds);
  if (bounds().size() != previous_bounds.size()) {
    RebuildGridRows(favorite_count_);
    UpdateContainerOwnedPreferredHeight(favorite_count_);
  }
}

void MahoSidebarFavoritesGridView::Update(
    const MahoSidebarFavoritesModel& model) {
  favorite_count_ = std::min(model.items.size(), tile_views_.size());
  const bool has_items = !model.items.empty();
  const size_t visible_item_count = favorite_count_;
  RebuildGridRows(visible_item_count);
  content_view_->SetVisible(has_items);
  UpdateContainerOwnedPreferredHeight(visible_item_count);

  // Hide the entire view when empty and no drag is revealing it.
  SetVisible(has_items || drag_reveal_active_);

  if (!has_items) {
      for (size_t index = 0; index < tile_views_.size(); ++index) {
        tile_views_[index]->SetVisible(false);
        tile_urls_[index] = GURL();
        tile_tab_ids_[index].clear();
      }
    return;
  }

  const auto specs = BuildTileSpecs(model);

  for (size_t index = 0; index < tile_views_.size(); ++index) {
    const bool has_item = specs[index].url.is_valid();
    tile_urls_[index] = has_item ? specs[index].url : GURL();
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
      ui::ImageModel fav;
      if (browser_ && browser_->tab_strip_model()) {
        TabStripModel* strip = browser_->tab_strip_model();
        for (int i = 0; i < strip->count(); ++i) {
          content::WebContents* contents = strip->GetWebContentsAt(i);
          if (contents && contents->GetVisibleURL() == specs[index].url) {
            auto* tab_iface = tabs::TabInterface::GetFromContents(contents);
            if (tab_iface) {
              fav = TabUIHelper::From(tab_iface)->GetFavicon();
            }
            break;
          }
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
    return 0;
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
  SetPreferredSize(gfx::Size(
      1, GetPreferredHeightForWidth(width(), favorite_count)));
  PreferredSizeChanged();
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
    if (favorite_count_ == 0) {
      SetVisible(false);
      UpdateContainerOwnedPreferredHeight(0);
    }
    return static_cast<int>(ui::mojom::DragOperation::kNone);
  }

  const size_t favorite_count = GetVisibleFavoriteCount();
  if (payload.origin != SidebarDragOrigin::kFavorites &&
      favorite_count >= kTileCount) {
    drag_over_drop_target_ = false;
    drag_reveal_active_ = false;
    SetBorder(nullptr);
    return static_cast<int>(ui::mojom::DragOperation::kNone);
  }

  if (!drag_reveal_active_ && favorite_count_ == 0) {
    drag_reveal_active_ = true;
    SetVisible(true);
    SetPreferredSize(gfx::Size(1, kFavoriteEmptyStateHeightDp));
    PreferredSizeChanged();
  }

  drag_over_drop_target_ = true;
  SetBorder(views::CreateSolidSidedBorder(
      gfx::Insets::TLBR(kDropIndicatorThicknessDp, kDropIndicatorInsetDp,
                        kDropIndicatorThicknessDp, kDropIndicatorInsetDp),
      ui::kColorSysPrimary));

  return static_cast<int>(ui::mojom::DragOperation::kMove);
}

views::View::DropCallback MahoSidebarFavoritesGridView::GetDropCallback(
    const ui::DropTargetEvent& event) {
  SidebarDragPayload payload;
  const bool has_payload = ReadMahoDragData(event.data(), payload) &&
                           payload.node_kind == SidebarNodeKind::kTab;
  const size_t favorite_count = GetVisibleFavoriteCount();
  const int target_index = has_payload
                               ? ResolveDropIndex(
                                     event.location(), payload.node_id,
                                     payload.origin == SidebarDragOrigin::kFavorites)
                               : 0;
  return base::BindOnce(
      [](int target_index,
         size_t favorite_count,
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

        if (payload.origin == SidebarDragOrigin::kFavorites) {
          DispatchShellEventEx("reorder_favorite", {
              ShellEventField("tab_id", payload.node_id),
              ShellEventField("new_index", target_index)});
        } else {
          DispatchShellEvent("favorite_tab", {{"tab_id", payload.node_id}});
          if (target_index < static_cast<int>(favorite_count)) {
            DispatchShellEventEx("reorder_favorite", {
                ShellEventField("tab_id", payload.node_id),
                ShellEventField("new_index", target_index)});
          }
        }
      },
      target_index, favorite_count);
}

void MahoSidebarFavoritesGridView::RevealForDrag() {
  if (drag_reveal_active_ || favorite_count_ > 0) {
    return;
  }
  drag_reveal_active_ = true;
  SetVisible(true);
  SetPreferredSize(gfx::Size(1, kFavoriteEmptyStateHeightDp));
  PreferredSizeChanged();
}

void MahoSidebarFavoritesGridView::CollapseFromDrag() {
  if (!drag_reveal_active_) {
    return;
  }
  drag_reveal_active_ = false;
  drag_over_drop_target_ = false;
  SetBorder(nullptr);
  SetVisible(false);
  UpdateContainerOwnedPreferredHeight(0);
}

void MahoSidebarFavoritesGridView::OnDragExited() {
  drag_over_drop_target_ = false;
  SetBorder(nullptr);
  if (drag_reveal_active_) {
    drag_reveal_active_ = false;
    SetVisible(false);
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
  const int item_count = source_is_favorite ? std::max(0, favorite_count - 1)
                                            : favorite_count;
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

bool MahoSidebarFavoritesGridView::ShouldSuppressDragRemoval(
    const gfx::Point& screen_point,
    const std::string& tab_id) const {
  const views::Widget* widget = GetWidget();
  const views::View* root_view = widget ? widget->GetRootView() : nullptr;
  if (!root_view) {
    return false;
  }

  gfx::Point root_point = screen_point;
  views::View::ConvertPointFromScreen(root_view, &root_point);
  views::View* hovered_view = const_cast<views::View*>(root_view)->GetEventHandlerForPoint(root_point);
  if (!hovered_view) {
    return false;
  }

  ui::OSExchangeData data;
  SidebarDragPayload payload;
  payload.node_kind = SidebarNodeKind::kTab;
  payload.node_id = tab_id;
  payload.origin = SidebarDragOrigin::kFavorites;
  WriteMahoDragData(payload, &data);

  int formats = 0;
  std::set<ui::ClipboardFormatType> format_types;
  for (views::View* view = hovered_view; view; view = view->parent()) {
    if (view == this) {
      return false;
    }
    if (!view->GetEnabled()) {
      continue;
    }
    formats = 0;
    format_types.clear();
    if (!view->GetDropFormats(&formats, &format_types) ||
        !data.HasAnyFormat(formats, format_types) || !view->CanDrop(data)) {
      continue;
    }

    gfx::Point local_point = root_point;
    views::View::ConvertPointToTarget(root_view, view, &local_point);
    ui::DropTargetEvent event(data, gfx::PointF(local_point), gfx::PointF(root_point),
                              static_cast<int>(ui::mojom::DragOperation::kMove));
    if (view->OnDragUpdated(event) !=
        static_cast<int>(ui::mojom::DragOperation::kNone)) {
      return true;
    }
  }

  return false;
}

void MahoSidebarFavoritesGridView::OnTileDragDone(size_t index) {
  if (index >= tile_tab_ids_.size()) {
    return;
  }

  const std::string& tab_id = tile_tab_ids_[index];
  if (tab_id.empty()) {
    return;
  }

  const gfx::Point screen_point = display::Screen::Get()->GetCursorScreenPoint();
  if (GetBoundsInScreen().Contains(screen_point) ||
      ShouldSuppressDragRemoval(screen_point, tab_id)) {
    return;
  }

  DispatchShellEvent("unfavorite_tab", {{"tab_id", tab_id}});
}

void MahoSidebarFavoritesGridView::OnTilePressed(size_t index) {
  if (!browser_ || index >= tile_urls_.size() || !tile_urls_[index].is_valid()) {
    return;
  }

  browser_->OpenURL(content::OpenURLParams(
                        tile_urls_[index], content::Referrer(),
                        WindowOpenDisposition::CURRENT_TAB,
                        ui::PAGE_TRANSITION_AUTO_BOOKMARK, false),
                    base::NullCallback());
}

}  // namespace maho
