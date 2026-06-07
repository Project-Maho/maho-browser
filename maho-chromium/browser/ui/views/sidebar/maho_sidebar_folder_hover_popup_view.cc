// Copyright 2026 Maho Browser. All rights reserved.

#include "maho_sidebar_folder_hover_popup_view.h"

#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "content/public/browser/web_contents.h"
#include "base/i18n/case_conversion.h"
#include "base/i18n/rtl.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tab_ui_helper.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/tabs/public/tab_interface.h"
#include "components/vector_icons/vector_icons.h"
#include "maho/browser/maho_tab_id_helper.h"
#include "maho/browser/ui/views/maho_lucide_icons/vector_icons.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/ui_base_types.h"
#include "ui/color/color_id.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/separator.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/style/typography.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"

namespace maho {

namespace {

constexpr int kPopupWidth = 260;
constexpr int kMaxScrollHeight = 312;
constexpr int kRowHeight = 40;
constexpr int kCornerRadius = 16;
constexpr int kRowCornerRadius = 10;
constexpr int kRowHorizontalInset = 8;
constexpr int kMaxNavDepth = 10;

class ItemRow : public views::View {
  METADATA_HEADER(ItemRow, views::View)
 public:
  ItemRow(const ui::ImageModel& icon,
          const std::u16string& title,
          bool is_folder,
          int index,
          base::RepeatingCallback<void(int)> hover_cb,
          base::RepeatingCallback<void(int)> click_cb)
      : index_(index), hover_cb_(std::move(hover_cb)), click_cb_(std::move(click_cb)) {
    DETACH_FROM_SEQUENCE(sequence_checker_);
    SetPreferredSize(gfx::Size(kPopupWidth - 2 * kRowHorizontalInset, kRowHeight));
    
    SetLayoutManager(std::make_unique<views::FillLayout>());
    
    highlight_bg_ = AddChildView(std::make_unique<views::View>());
    highlight_bg_->SetVisible(false);
    highlight_bg_->SetBackground(views::CreateRoundedRectBackground(
        ui::kColorSysStateHoverOnSubtle, kRowCornerRadius));
        
    auto* contents = AddChildView(std::make_unique<views::View>());
    auto* row_layout = contents->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(0, kRowHorizontalInset), 10));
    row_layout->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kCenter);
    
    auto* icon_view = contents->AddChildView(std::make_unique<views::ImageView>());
    icon_view->SetImage(icon);
    icon_view->SetImageSize(gfx::Size(16, 16));
    
    auto* label = contents->AddChildView(std::make_unique<views::Label>(
        title, views::style::CONTEXT_LABEL, views::style::STYLE_BODY_3));
    label->SetEnabledColor(ui::kColorSysOnSurface);
    label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    label->SetElideBehavior(gfx::ELIDE_TAIL);
    label->SetSubpixelRenderingEnabled(false);
    row_layout->SetFlexForView(label, 1);
    
    if (is_folder) {
      auto* chevron = contents->AddChildView(std::make_unique<views::ImageView>());
      chevron->SetImage(ui::ImageModel::FromVectorIcon(
          maho_lucide_icons::kChevronRightIcon, ui::kColorSysOnSurfaceSubtle, 12));
    }
  }
  
  void SetSelected(bool selected) {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    selected_ = selected;
    UpdateRowBackground();
  }
  
  void OnMouseEntered(const ui::MouseEvent& event) override {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    views::View::OnMouseEntered(event);
    hovered_ = true;
    UpdateRowBackground();
    hover_cb_.Run(index_);
  }
  
  void OnMouseExited(const ui::MouseEvent& event) override {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    views::View::OnMouseExited(event);
    hovered_ = false;
    UpdateRowBackground();
  }
  
  bool OnMousePressed(const ui::MouseEvent& event) override {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    if (event.IsOnlyLeftMouseButton()) {
      click_cb_.Run(index_);
      return true;
    }
    return views::View::OnMousePressed(event);
  }
  
 private:
  void UpdateRowBackground() {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    if (selected_) {
      highlight_bg_->SetVisible(true);
      highlight_bg_->SetBackground(views::CreateRoundedRectBackground(
          ui::kColorSysSurfaceVariant, kRowCornerRadius));
    } else if (hovered_) {
      highlight_bg_->SetVisible(true);
      highlight_bg_->SetBackground(views::CreateRoundedRectBackground(
          ui::kColorSysStateHoverOnSubtle, kRowCornerRadius));
    } else {
      highlight_bg_->SetVisible(false);
    }
  }

  int index_;
  base::RepeatingCallback<void(int)> hover_cb_;
  base::RepeatingCallback<void(int)> click_cb_;
  raw_ptr<views::View> highlight_bg_ = nullptr;
  bool hovered_ = false;
  bool selected_ = false;
  
  SEQUENCE_CHECKER(sequence_checker_);
};

BEGIN_METADATA(ItemRow)
END_METADATA

ui::ImageModel GetFaviconForTab(Browser* browser, const SidebarTreeNode& node) {
  if (!browser || !browser->tab_strip_model()) {
    return ui::ImageModel();
  }
  int live_index = -1;
  TabStripModel* strip = browser->tab_strip_model();
  for (int i = 0; i < strip->count(); ++i) {
    auto* contents = strip->GetWebContentsAt(i);
    if (!contents) {
      continue;
    }
    auto* helper = MahoTabIdHelper::FromWebContents(contents);
    if (helper && helper->stable_tab_id() == node.tab_id) {
      live_index = i;
      break;
    }
  }
  
  if (live_index >= 0) {
    content::WebContents* contents = strip->GetWebContentsAt(live_index);
    if (contents) {
      if (auto* tab_iface = tabs::TabInterface::GetFromContents(contents)) {
        if (auto* tab_ui = TabUIHelper::From(tab_iface)) {
          return tab_ui->GetFavicon();
        }
      }
    }
  }
  return ui::ImageModel();
}

}  // namespace

MahoSidebarFolderHoverPopupView::Level::Level(std::u16string name, std::vector<SidebarTreeNode> children)
    : name(std::move(name)), children(std::move(children)) {}

MahoSidebarFolderHoverPopupView::Level::Level(const Level&) = default;
MahoSidebarFolderHoverPopupView::Level::Level(Level&&) noexcept = default;
MahoSidebarFolderHoverPopupView::Level& MahoSidebarFolderHoverPopupView::Level::operator=(const Level&) = default;
MahoSidebarFolderHoverPopupView::Level& MahoSidebarFolderHoverPopupView::Level::operator=(Level&&) noexcept = default;
MahoSidebarFolderHoverPopupView::Level::~Level() = default;

BEGIN_METADATA(MahoSidebarFolderHoverPopupView)
END_METADATA

views::Widget* MahoSidebarFolderHoverPopupView::Show(
    views::View* anchor_view,
    Browser* browser,
    const std::u16string& folder_name,
    std::string folder_id,
    std::vector<SidebarTreeNode> children,
    ActivateTabCallback on_activate) {
  if (!anchor_view) {
    return nullptr;
  }
  
  auto delegate = std::make_unique<views::BubbleDialogDelegate>(
      anchor_view,
      base::i18n::IsRTL() ? views::BubbleBorder::RIGHT_TOP
                          : views::BubbleBorder::LEFT_TOP);
  
  delegate->SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  delegate->set_margins(gfx::Insets());
  delegate->SetCanActivate(true);
  
  auto contents = std::make_unique<MahoSidebarFolderHoverPopupView>(
      browser, folder_name, folder_id, std::move(children),
      std::move(on_activate));
  
  delegate->SetContentsView(std::move(contents));
  
  views::Widget* widget = views::BubbleDialogDelegate::CreateBubble(
      std::move(delegate));
  if (widget) {
    widget->ShowInactive();
  }
  return widget;
}

MahoSidebarFolderHoverPopupView::MahoSidebarFolderHoverPopupView(
    Browser* browser,
    std::u16string folder_name,
    std::string folder_id,
    std::vector<SidebarTreeNode> children,
    ActivateTabCallback on_activate)
    : browser_(browser),
      root_folder_id_(std::move(folder_id)),
      activate_callback_(std::move(on_activate)) {
  DETACH_FROM_SEQUENCE(sequence_checker_);
  nav_stack_.push_back({std::move(folder_name), std::move(children)});
  Init();
}

MahoSidebarFolderHoverPopupView::~MahoSidebarFolderHoverPopupView() = default;

void MahoSidebarFolderHoverPopupView::Init() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 0));

  SetBackground(views::CreateRoundedRectBackground(ui::kColorSysSurface2, kCornerRadius));
  SetBorder(views::CreateRoundedRectBorder(1, kCornerRadius, ui::kColorSysNeutralOutline));

  // 1. Breadcrumb Row (hidden at root)
  breadcrumb_row_ = AddChildView(std::make_unique<views::View>());
  auto* breadcrumb_layout = breadcrumb_row_->SetLayoutManager(
      std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kHorizontal,
                                         gfx::Insets::VH(6, 12), 8));
  breadcrumb_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  
  auto back_btn = std::make_unique<views::LabelButton>(
      base::BindRepeating(
          [](MahoSidebarFolderHoverPopupView* view, const ui::Event& event) {
            view->PopLevel();
          },
          base::Unretained(this)),
      u"Back");
  back_btn->SetImageModel(views::Button::STATE_NORMAL, ui::ImageModel::FromVectorIcon(
      maho_lucide_icons::kChevronLeftIcon, ui::kColorSysPrimary, 14));
  back_btn->SetTextColor(views::Button::STATE_NORMAL, ui::kColorSysPrimary);
  back_btn->SetTextColor(views::Button::STATE_HOVERED, ui::kColorSysPrimary);
  back_btn->SetBorder(nullptr);
  
  breadcrumb_row_->AddChildView(std::move(back_btn));
  
  auto path_label = std::make_unique<views::Label>(
      u"", views::style::CONTEXT_LABEL, views::style::STYLE_BODY_5);
  path_label->SetEnabledColor(ui::kColorSysOnSurfaceSubtle);
  path_label->SetElideBehavior(gfx::ELIDE_MIDDLE);
  path_label->SetSubpixelRenderingEnabled(false);
  path_label_ = breadcrumb_row_->AddChildView(std::move(path_label));
  
  breadcrumb_row_->SetVisible(false);

  // 2. Search Row
  auto* search_row = AddChildView(std::make_unique<views::View>());
  auto* search_layout = search_row->SetLayoutManager(
      std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kHorizontal,
                                         gfx::Insets::VH(6, 12), 8));
  search_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  auto* search_icon = search_row->AddChildView(std::make_unique<views::ImageView>());
  search_icon->SetImage(ui::ImageModel::FromVectorIcon(
      maho_lucide_icons::kSearchIcon, ui::kColorSysOnSurfaceSubtle, 16));

  auto search_field = std::make_unique<views::Textfield>();
  search_field->SetPlaceholderText(u"Search " + nav_stack_.back().name + u"…");
  search_field->SetBorder(nullptr);
  search_field->SetBackgroundColor(SK_ColorTRANSPARENT);
  search_field->SetTextColor(ui::kColorSysOnSurface);
  search_field->SetController(this);
  search_field_ = search_row->AddChildView(std::move(search_field));
  search_layout->SetFlexForView(search_field_, 1);

  // 3. Separator Line
  auto* separator = AddChildView(std::make_unique<views::Separator>());
  separator->SetColorId(ui::kColorSysNeutralOutline);

  // 4. Scroll View
  scroll_view_ = AddChildView(std::make_unique<views::ScrollView>());
  scroll_view_->SetHorizontalScrollBarMode(views::ScrollView::ScrollBarMode::kDisabled);
  scroll_view_->ClipHeightTo(0, kMaxScrollHeight);
  
  auto rows_container = std::make_unique<views::View>();
  rows_container->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets::VH(4, 8), 2));
  rows_container_ = scroll_view_->SetContents(std::move(rows_container));

  ApplyFilter(u"");
}

void MahoSidebarFolderHoverPopupView::ContentsChanged(
    views::Textfield* sender,
    const std::u16string& new_contents) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ApplyFilter(new_contents);
}

bool MahoSidebarFolderHoverPopupView::HandleKeyEvent(
    views::Textfield* sender,
    const ui::KeyEvent& key_event) {
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

  const auto& children = nav_stack_.back().children;
  if (filtered_indices_.empty()) {
    auto* empty_label = rows_container_->AddChildView(std::make_unique<views::Label>(
        children.empty() ? u"No tabs in this folder" : u"No matching results",
        views::style::CONTEXT_LABEL, views::style::STYLE_BODY_3));
    empty_label->SetEnabledColor(ui::kColorSysOnSurfaceSubtle);
    empty_label->SetHorizontalAlignment(gfx::ALIGN_CENTER);
    empty_label->SetPreferredSize(gfx::Size(kPopupWidth - 2 * kRowHorizontalInset, kRowHeight));
    empty_label->SetSubpixelRenderingEnabled(false);
  } else {
    for (size_t i = 0; i < filtered_indices_.size(); ++i) {
      size_t child_idx = filtered_indices_[i];
      const auto& child = children[child_idx];
      
      ui::ImageModel icon;
      std::u16string display_name;
      bool is_folder = (child.kind == SidebarNodeKind::kFolder);
      
      if (is_folder) {
        icon = ui::ImageModel::FromVectorIcon(
            maho_lucide_icons::kFolderIcon, ui::kColorSysPrimary, 16);
        display_name = child.folder_name;
      } else {
        icon = GetFaviconForTab(browser_, child);
        if (icon.IsEmpty()) {
          icon = ui::ImageModel::FromVectorIcon(
              vector_icons::kGlobeIcon, ui::kColorSysOnSurfaceSubtle, 16);
        }
        display_name = child.custom_title.empty() ? child.title : child.custom_title;
        if (display_name.empty()) {
          display_name = u"Untitled tab";
        }
      }
      
      auto* row = rows_container_->AddChildView(std::make_unique<ItemRow>(
          icon, display_name, is_folder, static_cast<int>(i),
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

void MahoSidebarFolderHoverPopupView::ApplyFilter(const std::u16string& query) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  filtered_indices_.clear();
  const auto& children = nav_stack_.back().children;
  
  if (query.empty()) {
    for (size_t i = 0; i < children.size(); ++i) {
      filtered_indices_.push_back(i);
    }
  } else {
    std::u16string query_lower = base::i18n::ToLower(query);
    for (size_t i = 0; i < children.size(); ++i) {
      const auto& child = children[i];
      if (child.kind == SidebarNodeKind::kFolder) {
        std::u16string name_lower = base::i18n::ToLower(child.folder_name);
        if (name_lower.find(query_lower) != std::u16string::npos) {
          filtered_indices_.push_back(i);
        }
      } else {
        std::u16string title_lower = base::i18n::ToLower(
            child.custom_title.empty() ? child.title : child.custom_title);
        std::u16string url_lower = base::i18n::ToLower(base::UTF8ToUTF16(child.url));
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
  
  if (selected_index_ >= 0 && selected_index_ < static_cast<int>(rows_container_->children().size())) {
    if (auto* old_row = views::AsViewClass<ItemRow>(rows_container_->children()[selected_index_])) {
      old_row->SetSelected(false);
    }
  }
  
  selected_index_ = (selected_index_ + delta + count) % count;
  
  if (selected_index_ >= 0 && selected_index_ < static_cast<int>(rows_container_->children().size())) {
    if (auto* new_row = views::AsViewClass<ItemRow>(rows_container_->children()[selected_index_])) {
      new_row->SetSelected(true);
      rows_container_->ScrollRectToVisible(new_row->bounds());
    }
  }
}

void MahoSidebarFolderHoverPopupView::ActivateSelected() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (selected_index_ < 0 || selected_index_ >= static_cast<int>(filtered_indices_.size())) {
    return;
  }
  
  size_t child_idx = filtered_indices_[selected_index_];
  const auto& child = nav_stack_.back().children[child_idx];
  
  if (child.kind == SidebarNodeKind::kFolder) {
    PushLevel(child);
  } else {
    if (activate_callback_) {
      activate_callback_.Run(child.tab_id);
    }
  }
}

void MahoSidebarFolderHoverPopupView::PushLevel(const SidebarTreeNode& folder_node) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (nav_stack_.size() >= kMaxNavDepth) {
    return;
  }
  nav_stack_.push_back({folder_node.folder_name, folder_node.children});
  
  if (search_field_) {
    search_field_->SetController(nullptr);
    search_field_->SetText(u"");
    search_field_->SetController(this);
    search_field_->SetPlaceholderText(u"Search " + folder_node.folder_name + u"…");
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
      search_field_->SetPlaceholderText(u"Search " + nav_stack_.back().name + u"…");
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
  if (selected_index_ >= 0 && selected_index_ < static_cast<int>(rows_container_->children().size())) {
    if (auto* old_row = views::AsViewClass<ItemRow>(rows_container_->children()[selected_index_])) {
      old_row->SetSelected(false);
    }
  }
  selected_index_ = index;
  if (selected_index_ >= 0 && selected_index_ < static_cast<int>(rows_container_->children().size())) {
    if (auto* new_row = views::AsViewClass<ItemRow>(rows_container_->children()[selected_index_])) {
      new_row->SetSelected(true);
    }
  }
}

void MahoSidebarFolderHoverPopupView::OnRowClicked(int index) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  selected_index_ = index;
  ActivateSelected();
}

}  // namespace maho
