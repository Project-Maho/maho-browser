// Copyright 2026 Maho Browser. All rights reserved.

#include "maho_sidebar_folder_hover_popup_view.h"

#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/i18n/case_conversion.h"
#include "base/i18n/rtl.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tab_ui_helper.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/tabs/public/tab_interface.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/web_contents.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/browser/maho_tab_id_helper.h"
#include "maho/browser/ui/views/maho_lucide_icons/vector_icons.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_drag_util.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_tab_list_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_view.h"
#include "ui/base/dragdrop/mojom/drag_drop_types.mojom.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/class_property.h"
#include "ui/base/ui_base_types.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/geometry/vector2d.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/drag_controller.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/style/typography.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"

namespace maho {

namespace {

DEFINE_OWNED_UI_CLASS_PROPERTY_KEY(views::BubbleDialogDelegate,
                                 kFolderPopupDelegateKey)

constexpr int kPopupWidth = 260;
constexpr int kMaxScrollHeight = 312;
constexpr int kRowHeight = 40;
constexpr int kCornerRadius = 16;
constexpr int kRowCornerRadius = 10;
constexpr int kRowHorizontalInset = 8;
constexpr int kMaxNavDepth = 10;
constexpr int kDragStartThresholdDp = 5;

struct ItemRowDragData {
  std::string tab_id;
  std::string space_id;
  std::string source_parent_folder_id;
  int tab_strip_index = -1;
  int source_folder_child_index = -1;
};

int ResolveTabStripIndex(Browser *browser, const std::string &tab_id) {
  if (!browser || !browser->GetTabStripModel() || tab_id.empty()) {
    return -1;
  }

  TabStripModel *strip = browser->GetTabStripModel();
  for (int i = 0; i < strip->count(); ++i) {
    content::WebContents *contents = strip->GetWebContentsAt(i);
    if (!contents) {
      continue;
    }
    auto *helper = MahoTabIdHelper::FromWebContents(contents);
    if (helper && helper->stable_tab_id() == tab_id) {
      return i;
    }
  }
  return -1;
}

std::string GetActiveSpaceIdForBrowser(Browser *browser) {
  auto *bridge = MahoSpaceProfileBridge::GetInstance();
  return bridge ? bridge->GetActiveSpaceId(browser) : std::string();
}

class ItemRow : public views::View, public views::DragController {
  METADATA_HEADER(ItemRow, views::View)
public:
  ItemRow(const ui::ImageModel &icon, const std::u16string &title,
          bool is_folder, int index, ItemRowDragData drag_data,
          const MahoSidebarPalette &palette,
          base::RepeatingCallback<void(int)> hover_cb,
          base::RepeatingCallback<void(int)> click_cb)
      : is_folder_(is_folder), index_(index), drag_data_(std::move(drag_data)),
        hover_cb_(std::move(hover_cb)), click_cb_(std::move(click_cb)),
        palette_(palette) {
    DETACH_FROM_SEQUENCE(sequence_checker_);
    SetPreferredSize(
        gfx::Size(kPopupWidth - 2 * kRowHorizontalInset, kRowHeight));

    if (CanDragTab()) {
      set_drag_controller(this);
    }

    SetLayoutManager(std::make_unique<views::FillLayout>());

    highlight_bg_ = AddChildView(std::make_unique<views::View>());
    highlight_bg_->SetVisible(false);
    highlight_bg_->SetBackground(views::CreateRoundedRectBackground(
        palette.row_hover, kRowCornerRadius));

    auto *contents = AddChildView(std::make_unique<views::View>());
    if (CanDragTab()) {
      contents->set_drag_controller(this);
    }
    auto *row_layout =
        contents->SetLayoutManager(std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kHorizontal,
            gfx::Insets::VH(0, kRowHorizontalInset), 10));
    row_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);

    auto *icon_view =
        contents->AddChildView(std::make_unique<views::ImageView>());
    icon_view->SetImage(icon);
    icon_view->SetImageSize(gfx::Size(16, 16));
    if (CanDragTab()) {
      icon_view->set_drag_controller(this);
    }

    auto *label = contents->AddChildView(std::make_unique<views::Label>(
        title, views::style::CONTEXT_LABEL, views::style::STYLE_BODY_3));
    label->SetEnabledColor(palette.primary_text);
    label->SetAutoColorReadabilityEnabled(false);
    label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    label->SetElideBehavior(gfx::ELIDE_TAIL);
    label->SetSubpixelRenderingEnabled(false);
    if (CanDragTab()) {
      label->set_drag_controller(this);
    }
    row_layout->SetFlexForView(label, 1);

    if (is_folder) {
      auto *chevron =
          contents->AddChildView(std::make_unique<views::ImageView>());
      chevron->SetImage(ui::ImageModel::FromVectorIcon(
          maho_lucide_icons::kChevronRightIcon, palette.secondary_text, 12));
    }
  }

  void SetSelected(bool selected) {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    selected_ = selected;
    UpdateRowBackground();
  }

  void OnMouseEntered(const ui::MouseEvent &event) override {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    views::View::OnMouseEntered(event);
    hovered_ = true;
    UpdateRowBackground();
    hover_cb_.Run(index_);
  }

  void OnMouseExited(const ui::MouseEvent &event) override {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    views::View::OnMouseExited(event);
    hovered_ = false;
    UpdateRowBackground();
  }

  bool OnMousePressed(const ui::MouseEvent &event) override {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    if (event.IsOnlyLeftMouseButton()) {
      if (CanDragTab()) {
        click_pending_ = true;
        views::View::OnMousePressed(event);
        return true;
      }
      click_cb_.Run(index_);
      return true;
    }
    return views::View::OnMousePressed(event);
  }

  void OnMouseReleased(const ui::MouseEvent &event) override {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    const bool should_click = click_pending_;
    click_pending_ = false;
    views::View::OnMouseReleased(event);
    if (should_click &&
        (event.changed_button_flags() & ui::EF_LEFT_MOUSE_BUTTON) &&
        HitTestPoint(event.location())) {
      click_cb_.Run(index_);
    }
  }

  void WriteDragDataForView(views::View *sender, const gfx::Point &press_pt,
                            ui::OSExchangeData *data) override {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    if (!CanDragTab()) {
      return;
    }

    click_pending_ = false;

    SidebarDragPayload payload;
    payload.node_kind = SidebarNodeKind::kTab;
    payload.node_id = drag_data_.tab_id;
    payload.origin = SidebarDragOrigin::kNormalSection;
    payload.space_id = drag_data_.space_id;
    payload.tab_strip_index = drag_data_.tab_strip_index;
    payload.source_parent_folder_id = drag_data_.source_parent_folder_id;
    payload.source_folder_child_index = drag_data_.source_folder_child_index;
    WriteMahoDragData(payload, data);
    data->provider().SetDragImage(CreateSidebarDragImage(this),
                                  gfx::Vector2d());
  }

  int GetDragOperationsForView(views::View *sender,
                               const gfx::Point &p) override {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    return static_cast<int>(CanDragTab() ? ui::mojom::DragOperation::kMove
                                         : ui::mojom::DragOperation::kNone);
  }

  bool CanStartDragForView(views::View *sender, const gfx::Point &press_pt,
                           const gfx::Point &current_pt) override {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    return CanDragTab() &&
           (press_pt - current_pt).Length() >= kDragStartThresholdDp;
  }

private:
  bool CanDragTab() const { return !is_folder_ && !drag_data_.tab_id.empty(); }

  void UpdateRowBackground() {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    if (selected_) {
      highlight_bg_->SetVisible(true);
      highlight_bg_->SetBackground(views::CreateRoundedRectBackground(
          palette_.row_selected, kRowCornerRadius));
    } else if (hovered_) {
      highlight_bg_->SetVisible(true);
      highlight_bg_->SetBackground(views::CreateRoundedRectBackground(
          palette_.row_hover, kRowCornerRadius));
    } else {
      highlight_bg_->SetVisible(false);
    }
  }

  bool is_folder_ = false;
  int index_;
  ItemRowDragData drag_data_;
  base::RepeatingCallback<void(int)> hover_cb_;
  base::RepeatingCallback<void(int)> click_cb_;
  MahoSidebarPalette palette_;
  raw_ptr<views::View> highlight_bg_ = nullptr;
  bool hovered_ = false;
  bool selected_ = false;
  bool click_pending_ = false;

  SEQUENCE_CHECKER(sequence_checker_);
};

BEGIN_METADATA(ItemRow)
END_METADATA

ui::ImageModel GetFaviconForTab(Browser *browser, const SidebarTreeNode &node) {
  if (!browser || !browser->GetTabStripModel()) {
    return ui::ImageModel();
  }
  int live_index = -1;
  TabStripModel *strip = browser->GetTabStripModel();
  for (int i = 0; i < strip->count(); ++i) {
    auto *contents = strip->GetWebContentsAt(i);
    if (!contents) {
      continue;
    }
    auto *helper = MahoTabIdHelper::FromWebContents(contents);
    if (helper && helper->stable_tab_id() == node.tab_id) {
      live_index = i;
      break;
    }
  }

  if (live_index >= 0) {
    content::WebContents *contents = strip->GetWebContentsAt(live_index);
    if (contents) {
      if (auto *tab_iface = tabs::TabInterface::GetFromContents(contents)) {
        if (auto *tab_ui = TabUIHelper::From(tab_iface)) {
          return tab_ui->GetFavicon();
        }
      }
    }
  }
  return ui::ImageModel();
}

} // namespace

MahoSidebarFolderHoverPopupView::Level::Level(
    std::u16string name, std::string folder_id,
    std::vector<SidebarTreeNode> children)
    : name(std::move(name)), folder_id(std::move(folder_id)),
      children(std::move(children)) {}

MahoSidebarFolderHoverPopupView::Level::Level(const Level &) = default;
MahoSidebarFolderHoverPopupView::Level::Level(Level &&) noexcept = default;
MahoSidebarFolderHoverPopupView::Level &
MahoSidebarFolderHoverPopupView::Level::operator=(const Level &) = default;
MahoSidebarFolderHoverPopupView::Level &
MahoSidebarFolderHoverPopupView::Level::operator=(Level &&) noexcept = default;
MahoSidebarFolderHoverPopupView::Level::~Level() = default;

BEGIN_METADATA(MahoSidebarFolderHoverPopupView)
END_METADATA

views::Widget *MahoSidebarFolderHoverPopupView::Show(
    views::View *anchor_view, Browser *browser,
    const std::u16string &folder_name, std::string folder_id,
    std::vector<SidebarTreeNode> children, ActivateTabCallback on_activate) {
  if (!anchor_view) {
    return nullptr;
  }

  auto delegate = std::make_unique<views::BubbleDialogDelegate>(
      anchor_view, base::i18n::IsRTL() ? views::BubbleBorder::RIGHT_TOP
                                       : views::BubbleBorder::LEFT_TOP);

  delegate->SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  delegate->set_margins(gfx::Insets());
  delegate->SetCanActivate(true);

  auto contents = std::make_unique<MahoSidebarFolderHoverPopupView>(
      browser, folder_name, folder_id, std::move(children),
      std::move(on_activate));
  for (views::View *current = anchor_view; current;
       current = current->parent()) {
    if (auto *sidebar = views::AsViewClass<MahoSidebarView>(current)) {
      contents->SetSidebarPalette(sidebar->sidebar_palette());
      contents->palette_subscription_ =
          sidebar->AddSidebarPaletteChangedCallback(base::BindRepeating(
              &MahoSidebarFolderHoverPopupView::SetSidebarPalette,
              base::Unretained(contents.get())));
      break;
    }
  }

  delegate->SetContentsView(std::move(contents));

  views::Widget *widget =
      views::BubbleDialogDelegate::CreateBubbleDeprecated(
          delegate.get(), views::Widget::InitParams::NATIVE_WIDGET_OWNS_WIDGET);
  if (widget) {
    widget->SetProperty(kFolderPopupDelegateKey, std::move(delegate));
    widget->ShowInactive();
  }
  return widget;
}

MahoSidebarFolderHoverPopupView::MahoSidebarFolderHoverPopupView(
    Browser *browser, std::u16string folder_name, std::string folder_id,
    std::vector<SidebarTreeNode> children, ActivateTabCallback on_activate)
    : browser_(browser), root_folder_id_(std::move(folder_id)),
      activate_callback_(std::move(on_activate)) {
  DETACH_FROM_SEQUENCE(sequence_checker_);
  nav_stack_.emplace_back(std::move(folder_name), root_folder_id_,
                          std::move(children));
  Init();
}

MahoSidebarFolderHoverPopupView::~MahoSidebarFolderHoverPopupView() = default;

void MahoSidebarFolderHoverPopupView::Init() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 0));

  // 1. Breadcrumb Row (hidden at root)
  breadcrumb_row_ = AddChildView(std::make_unique<views::View>());
  auto *breadcrumb_layout =
      breadcrumb_row_->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(6, 12),
          8));
  breadcrumb_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  auto back_btn = std::make_unique<views::LabelButton>(
      base::BindRepeating([](MahoSidebarFolderHoverPopupView *view,
                             const ui::Event &event) { view->PopLevel(); },
                          base::Unretained(this)),
      u"Back");
  // Glyph and text colors are bound in SetSidebarPalette().
  back_btn->SetBorder(nullptr);
  back_button_ = breadcrumb_row_->AddChildView(std::move(back_btn));

  auto path_label = std::make_unique<views::Label>(
      u"", views::style::CONTEXT_LABEL, views::style::STYLE_BODY_5);
  path_label->SetElideBehavior(gfx::ELIDE_MIDDLE);
  path_label->SetSubpixelRenderingEnabled(false);
  path_label->SetAutoColorReadabilityEnabled(false);
  path_label_ = breadcrumb_row_->AddChildView(std::move(path_label));

  breadcrumb_row_->SetVisible(false);

  // 2. Search Row
  auto *search_row = AddChildView(std::make_unique<views::View>());
  auto *search_layout =
      search_row->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(6, 12),
          8));
  search_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  search_icon_ = search_row->AddChildView(std::make_unique<views::ImageView>());

  auto search_field = std::make_unique<views::Textfield>();
  search_field->SetPlaceholderText(u"Search " + nav_stack_.back().name + u"…");
  search_field->SetBorder(nullptr);
  search_field->SetBackgroundColor(SK_ColorTRANSPARENT);
  search_field->SetController(this);
  search_field_ = search_row->AddChildView(std::move(search_field));
  search_layout->SetFlexForView(search_field_, 1);

  // 3. Separator Line
  separator_ = AddChildView(std::make_unique<views::View>());
  separator_->SetPreferredSize(gfx::Size(1, 1));

  // 4. Scroll View
  scroll_view_ = AddChildView(std::make_unique<views::ScrollView>());
  scroll_view_->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  scroll_view_->ClipHeightTo(0, kMaxScrollHeight);

  auto rows_container = std::make_unique<views::View>();
  rows_container->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets::VH(4, 8), 2));
  rows_container_ = scroll_view_->SetContents(std::move(rows_container));

  SetSidebarPalette(palette_);
  ApplyFilter(u"");
}

void MahoSidebarFolderHoverPopupView::OnThemeChanged() {
  views::View::OnThemeChanged();
  SetSidebarPalette(palette_);
}

void MahoSidebarFolderHoverPopupView::SetSidebarPalette(
    const MahoSidebarPalette &palette) {
  palette_ = palette;
  SetBackground(
      views::CreateRoundedRectBackground(palette_.row_active, kCornerRadius));
  SetBorder(views::CreateRoundedRectBorder(1, kCornerRadius, palette_.outline));
  if (search_icon_) {
    search_icon_->SetImage(ui::ImageModel::FromVectorIcon(
        maho_lucide_icons::kSearchIcon, palette_.secondary_text, 16));
  }
  if (separator_) {
    separator_->SetBackground(views::CreateSolidBackground(palette_.outline));
  }
  if (search_field_) {
    search_field_->SetMahoResolvedTextColor(palette_.primary_text);
    search_field_->SetMahoResolvedPlaceholderTextColor(palette_.secondary_text);
  }
  if (path_label_) {
    path_label_->SetEnabledColor(palette_.secondary_text);
  }
  if (back_button_) {
    const ui::ImageModel chevron = ui::ImageModel::FromVectorIcon(
        maho_lucide_icons::kChevronLeftIcon, palette_.neutral_glyph, 14);
    back_button_->SetImageModel(views::Button::STATE_NORMAL, chevron);
    back_button_->SetImageModel(
        views::Button::STATE_DISABLED,
        ui::ImageModel::FromVectorIcon(maho_lucide_icons::kChevronLeftIcon,
                                       palette_.disabled_text, 14));
    back_button_->SetEnabledTextColors(palette_.primary_text);
    back_button_->SetTextColor(views::Button::STATE_DISABLED,
                               palette_.disabled_text);
  }
  RebuildResultRows();
}

void MahoSidebarFolderHoverPopupView::ContentsChanged(
    views::Textfield *sender, const std::u16string &new_contents) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ApplyFilter(new_contents);
}

bool MahoSidebarFolderHoverPopupView::HandleKeyEvent(
    views::Textfield *sender, const ui::KeyEvent &key_event) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (key_event.type() != ui::EventType::kKeyPressed) {
    return false;
  }

  switch (key_event.key_code()) {
  case ui::VKEY_UP:
    MoveSelection(-1);
    return true;
  case ui::VKEY_DOWN:
    MoveSelection(1);
    return true;
  case ui::VKEY_RETURN:
    ActivateSelected();
    return true;
  case ui::VKEY_ESCAPE:
    if (GetWidget()) {
      GetWidget()->Close();
      return true;
    }
    return false;
  case ui::VKEY_BACK:
    if (search_field_->GetText().empty() && nav_stack_.size() > 1) {
      PopLevel();
      return true;
    }
    return false;
  default:
    return false;
  }
}

void MahoSidebarFolderHoverPopupView::RebuildResultRows() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  rows_container_->RemoveAllChildViews();
  selected_index_ = 0;

  if (scroll_view_) {
    scroll_view_->ScrollToOffset(gfx::PointF(0, 0));
  }

  const Level &level = nav_stack_.back();
  const auto &children = level.children;
  const std::string space_id = GetActiveSpaceIdForBrowser(browser_);
  if (filtered_indices_.empty()) {
    auto *empty_label =
        rows_container_->AddChildView(std::make_unique<views::Label>(
            children.empty() ? u"No tabs in this folder"
                             : u"No matching results",
            views::style::CONTEXT_LABEL, views::style::STYLE_BODY_3));
    empty_label->SetEnabledColor(palette_.secondary_text);
    empty_label->SetAutoColorReadabilityEnabled(false);
    empty_label->SetHorizontalAlignment(gfx::ALIGN_CENTER);
    empty_label->SetPreferredSize(
        gfx::Size(kPopupWidth - 2 * kRowHorizontalInset, kRowHeight));
    empty_label->SetSubpixelRenderingEnabled(false);
  } else {
    for (size_t i = 0; i < filtered_indices_.size(); ++i) {
      size_t child_idx = filtered_indices_[i];
      const auto &child = children[child_idx];

      ui::ImageModel icon;
      std::u16string display_name;
      bool is_folder = (child.kind == SidebarNodeKind::kFolder);

      if (is_folder) {
        icon = ui::ImageModel::FromVectorIcon(maho_lucide_icons::kFolderIcon,
                                              palette_.neutral_glyph, 16);
        display_name = child.folder_name;
      } else {
        icon = GetFaviconForTab(browser_, child);
        if (icon.IsEmpty()) {
          icon = ui::ImageModel::FromVectorIcon(vector_icons::kGlobeIcon,
                                                palette_.neutral_glyph, 16);
        }
        display_name =
            child.custom_title.empty() ? child.title : child.custom_title;
        if (display_name.empty()) {
          display_name = u"Untitled tab";
        }
      }

      ItemRowDragData drag_data;
      if (!is_folder) {
        drag_data.tab_id = child.tab_id;
        drag_data.space_id = space_id;
        drag_data.source_parent_folder_id = child.parent_folder_id.empty()
                                                ? level.folder_id
                                                : child.parent_folder_id;
        drag_data.tab_strip_index =
            ResolveTabStripIndex(browser_, child.tab_id);
        drag_data.source_folder_child_index = child.folder_child_index;
      }

      auto *row = rows_container_->AddChildView(std::make_unique<ItemRow>(
          icon, display_name, is_folder, static_cast<int>(i),
          std::move(drag_data), palette_,
          base::BindRepeating(&MahoSidebarFolderHoverPopupView::OnRowHovered,
                              weak_factory_.GetWeakPtr()),
          base::BindRepeating(&MahoSidebarFolderHoverPopupView::OnRowClicked,
                              weak_factory_.GetWeakPtr())));

      if (static_cast<int>(i) == selected_index_) {
        row->SetSelected(true);
      }
    }
  }

  rows_container_->InvalidateLayout();
  PreferredSizeChanged();
}

void MahoSidebarFolderHoverPopupView::ApplyFilter(const std::u16string &query) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  filtered_indices_.clear();
  const auto &children = nav_stack_.back().children;

  if (query.empty()) {
    for (size_t i = 0; i < children.size(); ++i) {
      filtered_indices_.push_back(i);
    }
  } else {
    std::u16string query_lower = base::i18n::ToLower(query);
    for (size_t i = 0; i < children.size(); ++i) {
      const auto &child = children[i];
      if (child.kind == SidebarNodeKind::kFolder) {
        std::u16string name_lower = base::i18n::ToLower(child.folder_name);
        if (name_lower.find(query_lower) != std::u16string::npos) {
          filtered_indices_.push_back(i);
        }
      } else {
        std::u16string title_lower = base::i18n::ToLower(
            child.custom_title.empty() ? child.title : child.custom_title);
        std::u16string url_lower =
            base::i18n::ToLower(base::UTF8ToUTF16(child.url));
        if (title_lower.find(query_lower) != std::u16string::npos ||
            url_lower.find(query_lower) != std::u16string::npos) {
          filtered_indices_.push_back(i);
        }
      }
    }
  }

  RebuildResultRows();
}

void MahoSidebarFolderHoverPopupView::MoveSelection(int delta) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  int count = static_cast<int>(filtered_indices_.size());
  if (count == 0) {
    return;
  }

  if (selected_index_ >= 0 &&
      selected_index_ < static_cast<int>(rows_container_->children().size())) {
    if (auto *old_row = views::AsViewClass<ItemRow>(
            rows_container_->children()[selected_index_])) {
      old_row->SetSelected(false);
    }
  }

  selected_index_ = (selected_index_ + delta + count) % count;

  if (selected_index_ >= 0 &&
      selected_index_ < static_cast<int>(rows_container_->children().size())) {
    if (auto *new_row = views::AsViewClass<ItemRow>(
            rows_container_->children()[selected_index_])) {
      new_row->SetSelected(true);
      rows_container_->ScrollRectToVisible(new_row->bounds());
    }
  }
}

void MahoSidebarFolderHoverPopupView::ActivateSelected() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (selected_index_ < 0 ||
      selected_index_ >= static_cast<int>(filtered_indices_.size())) {
    return;
  }

  size_t child_idx = filtered_indices_[selected_index_];
  const auto &child = nav_stack_.back().children[child_idx];

  if (child.kind == SidebarNodeKind::kFolder) {
    PushLevel(child);
  } else {
    if (activate_callback_) {
      activate_callback_.Run(child.tab_id);
    }
  }
}

void MahoSidebarFolderHoverPopupView::PushLevel(
    const SidebarTreeNode &folder_node) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (nav_stack_.size() >= kMaxNavDepth) {
    return;
  }
  nav_stack_.emplace_back(folder_node.folder_name, folder_node.folder_id,
                          folder_node.children);

  if (search_field_) {
    search_field_->SetController(nullptr);
    search_field_->SetText(u"");
    search_field_->SetController(this);
    search_field_->SetPlaceholderText(u"Search " + folder_node.folder_name +
                                      u"…");
  }

  RebuildBreadcrumb();
  ApplyFilter(u"");
}

void MahoSidebarFolderHoverPopupView::PopLevel() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (nav_stack_.size() > 1) {
    nav_stack_.pop_back();

    if (search_field_) {
      search_field_->SetController(nullptr);
      search_field_->SetText(u"");
      search_field_->SetController(this);
      search_field_->SetPlaceholderText(u"Search " + nav_stack_.back().name +
                                        u"…");
    }

    RebuildBreadcrumb();
    ApplyFilter(u"");
  } else {
    if (GetWidget()) {
      GetWidget()->Close();
    }
  }
}

void MahoSidebarFolderHoverPopupView::RebuildBreadcrumb() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (nav_stack_.size() <= 1) {
    breadcrumb_row_->SetVisible(false);
  } else {
    breadcrumb_row_->SetVisible(true);
    std::u16string path;
    for (size_t i = 0; i < nav_stack_.size(); ++i) {
      if (i > 0) {
        path += u" / ";
      }
      path += nav_stack_[i].name;
    }

    if (path_label_) {
      path_label_->SetText(path);
    }
  }
}

void MahoSidebarFolderHoverPopupView::OnRowHovered(int index) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (selected_index_ >= 0 &&
      selected_index_ < static_cast<int>(rows_container_->children().size())) {
    if (auto *old_row = views::AsViewClass<ItemRow>(
            rows_container_->children()[selected_index_])) {
      old_row->SetSelected(false);
    }
  }
  selected_index_ = index;
  if (selected_index_ >= 0 &&
      selected_index_ < static_cast<int>(rows_container_->children().size())) {
    if (auto *new_row = views::AsViewClass<ItemRow>(
            rows_container_->children()[selected_index_])) {
      new_row->SetSelected(true);
    }
  }
}

void MahoSidebarFolderHoverPopupView::OnRowClicked(int index) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  selected_index_ = index;
  ActivateSelected();
}

} // namespace maho
