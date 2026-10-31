#include "maho_sidebar_action_popover_view.h"

#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/task/sequenced_task_runner.h"
#include "components/vector_icons/vector_icons.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_view.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/class_property.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/style/typography.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace maho {

namespace {

DEFINE_OWNED_UI_CLASS_PROPERTY_KEY(views::BubbleDialogDelegate,
                                 kActionPopoverDelegateKey)

constexpr int kCardCornerRadius = 16;
constexpr int kCardPadding = 8;
constexpr int kRowSpacing = 2;
constexpr int kRowWidth = 256;
constexpr int kRowHeight = 48;
constexpr int kRowCornerRadius = 10;
constexpr int kIconTileSize = 28;
constexpr int kIconTileCornerRadius = 8;
constexpr int kIconSize = 16;
constexpr int kRowHorizontalPadding = 10;
constexpr int kIconLabelSpacing = 10;
constexpr int kLabelVerticalSpacing = 1;

class ActionRow : public views::View {
  METADATA_HEADER(ActionRow, views::View)

 public:
  ActionRow(const gfx::VectorIcon& icon,
            const std::u16string& title,
            const std::u16string& subtitle,
            int index,
            base::RepeatingCallback<void(int)> hover_callback,
            base::RepeatingCallback<void(int)> click_callback)
      : index_(index),
        hover_callback_(std::move(hover_callback)),
        click_callback_(std::move(click_callback)) {
    SetPreferredSize(gfx::Size(kRowWidth, kRowHeight));

    SetLayoutManager(std::make_unique<views::FillLayout>());

    selection_background_ = AddChildView(std::make_unique<views::View>());
    selection_background_->SetVisible(false);
    selection_background_->SetCanProcessEventsWithinSubtree(false);

    auto* content_row = AddChildView(std::make_unique<views::View>());
    content_row->SetCanProcessEventsWithinSubtree(false);
    auto* row_layout =
        content_row->SetLayoutManager(std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kHorizontal,
            gfx::Insets::VH(0, kRowHorizontalPadding), kIconLabelSpacing));
    row_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);

    auto* icon_tile = content_row->AddChildView(std::make_unique<views::View>());
    icon_tile->SetPreferredSize(gfx::Size(kIconTileSize, kIconTileSize));
    icon_tile->SetLayoutManager(std::make_unique<views::FillLayout>());
    icon_tile_ = icon_tile;

    icon_ = &icon;
    auto icon_image = std::make_unique<views::ImageView>();
    icon_image->SetPreferredSize(gfx::Size(kIconSize, kIconSize));
    icon_image->SetHorizontalAlignment(views::ImageView::Alignment::kCenter);
    icon_image->SetVerticalAlignment(views::ImageView::Alignment::kCenter);
    icon_image_ = icon_tile->AddChildView(std::move(icon_image));

    auto* label_column =
        content_row->AddChildView(std::make_unique<views::View>());
    auto* label_layout =
        label_column->SetLayoutManager(std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kVertical, gfx::Insets(),
            kLabelVerticalSpacing));
    label_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStart);
    label_layout->set_main_axis_alignment(
        views::BoxLayout::MainAxisAlignment::kCenter);
    row_layout->SetFlexForView(label_column, 1);

    auto title_label = std::make_unique<views::Label>(
        title, views::style::CONTEXT_LABEL, views::style::STYLE_BODY_3_MEDIUM);
    title_label->SetAutoColorReadabilityEnabled(false);
    title_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    title_label_ = label_column->AddChildView(std::move(title_label));

    auto subtitle_label = std::make_unique<views::Label>(
        subtitle, views::style::CONTEXT_LABEL, views::style::STYLE_BODY_5);
    subtitle_label->SetAutoColorReadabilityEnabled(false);
    subtitle_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    subtitle_label_ = label_column->AddChildView(std::move(subtitle_label));

    GetViewAccessibility().SetRole(ax::mojom::Role::kButton);
    GetViewAccessibility().AddAction(ax::mojom::Action::kDoDefault);
    GetViewAccessibility().SetDefaultActionVerb(
        ax::mojom::DefaultActionVerb::kClick);
    SetAccessibleName(title + u". " + subtitle);
    SetFocusBehavior(FocusBehavior::ALWAYS);
  }

  void SetHighlighted(bool highlighted) {
    selection_background_->SetVisible(highlighted);
  }

  void SetSidebarPalette(const MahoSidebarPalette& palette) {
    selection_background_->SetBackground(views::CreateRoundedRectBackground(
        palette.row_selected, kRowCornerRadius));
    icon_tile_->SetBackground(views::CreateRoundedRectBackground(
        palette.row_active, kIconTileCornerRadius));
    icon_image_->SetImage(ui::ImageModel::FromVectorIcon(
        *icon_, palette.primary_text, kIconSize));
    title_label_->SetEnabledColor(palette.primary_text);
    subtitle_label_->SetEnabledColor(palette.secondary_text);
  }

  void OnMouseEntered(const ui::MouseEvent& event) override {
    hover_callback_.Run(index_);
  }

  bool OnMousePressed(const ui::MouseEvent& event) override {
    if (event.IsOnlyLeftMouseButton()) {
      click_callback_.Run(index_);
      return true;
    }
    return false;
  }

 private:
  int index_;
  base::RepeatingCallback<void(int)> hover_callback_;
  base::RepeatingCallback<void(int)> click_callback_;
  raw_ptr<views::View> selection_background_ = nullptr;
  raw_ptr<views::View> icon_tile_ = nullptr;
  raw_ptr<views::ImageView> icon_image_ = nullptr;
  raw_ptr<views::Label> title_label_ = nullptr;
  raw_ptr<views::Label> subtitle_label_ = nullptr;
  raw_ptr<const gfx::VectorIcon> icon_ = nullptr;
};

BEGIN_METADATA(ActionRow)
END_METADATA

}  // namespace

BEGIN_METADATA(MahoSidebarActionPopoverView)
END_METADATA

MahoSidebarActionPopoverView::MahoSidebarActionPopoverView(
    ActionCallback action_callback)
    : action_callback_(std::move(action_callback)) {
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets(kCardPadding), kRowSpacing));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  GetViewAccessibility().SetRole(ax::mojom::Role::kDialog);
  SetAccessibleName(u"Sidebar actions");
  SetFocusBehavior(FocusBehavior::ALWAYS);

  AddActionRow(MahoSidebarActionPopoverAction::kNewSpace,
               vector_icons::kSelectWindowChromeRefreshOldIcon,
               u"New Space", u"Create a fresh workspace");
  AddActionRow(MahoSidebarActionPopoverAction::kNewFolder,
               vector_icons::kFolderChromeRefreshOldIcon,
               u"New Folder", u"Organize tabs in the current space");
  AddActionRow(MahoSidebarActionPopoverAction::kNewTab,
               vector_icons::kAddOldIcon,
               u"New Tab", u"Open a page in the current space");

  UpdateHighlightState();
}

MahoSidebarActionPopoverView::~MahoSidebarActionPopoverView() = default;

views::Widget* MahoSidebarActionPopoverView::Show(
    views::View* anchor_view,
    ActionCallback action_callback) {
  if (!anchor_view) {
    return nullptr;
  }

  auto delegate = std::make_unique<views::BubbleDialogDelegate>(
      anchor_view, views::BubbleBorder::BOTTOM_RIGHT);
  delegate->SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  delegate->set_margins(gfx::Insets());

  auto contents = std::make_unique<MahoSidebarActionPopoverView>(
      std::move(action_callback));
  for (views::View* current = anchor_view; current; current = current->parent()) {
    if (auto* sidebar = views::AsViewClass<MahoSidebarView>(current)) {
      contents->SetSidebarPalette(sidebar->sidebar_palette());
      contents->palette_subscription_ = sidebar->AddSidebarPaletteChangedCallback(
          base::BindRepeating(&MahoSidebarActionPopoverView::SetSidebarPalette,
                              base::Unretained(contents.get())));
      break;
    }
  }
  delegate->SetContentsView(std::move(contents));

  views::Widget* widget =
      views::BubbleDialogDelegate::CreateBubbleDeprecated(
          delegate.get(), views::Widget::InitParams::NATIVE_WIDGET_OWNS_WIDGET);
  if (widget) {
    widget->SetProperty(kActionPopoverDelegateKey, std::move(delegate));
    widget->Show();
  }
  return widget;
}

bool MahoSidebarActionPopoverView::OnKeyPressed(const ui::KeyEvent& event) {
  const int count = static_cast<int>(action_rows_.size());
  if (count == 0) {
    return false;
  }

  switch (event.key_code()) {
    case ui::VKEY_DOWN:
      highlighted_index_ = std::min(highlighted_index_ + 1, count - 1);
      UpdateHighlightState();
      return true;
    case ui::VKEY_UP:
      highlighted_index_ = std::max(highlighted_index_ - 1, 0);
      UpdateHighlightState();
      return true;
    case ui::VKEY_TAB:
      if (event.IsShiftDown()) {
        highlighted_index_ =
            (highlighted_index_ - 1 + count) % count;
      } else {
        highlighted_index_ = (highlighted_index_ + 1) % count;
      }
      UpdateHighlightState();
      return true;
    case ui::VKEY_RETURN:
    case ui::VKEY_SPACE:
      ActivateHighlightedRow();
      return true;
    case ui::VKEY_ESCAPE:
      if (GetWidget()) {
        GetWidget()->Close();
      }
      return true;
    default:
      return false;
  }
}

void MahoSidebarActionPopoverView::SetSidebarPalette(
    const MahoSidebarPalette& palette) {
  palette_ = palette;
  SetBackground(views::CreateRoundedRectBackground(palette_.row_active,
                                                    kCardCornerRadius));
  SetBorder(views::CreateRoundedRectBorder(1, kCardCornerRadius,
                                            palette_.outline));
  for (views::View* row : action_rows_) {
    static_cast<ActionRow*>(row)->SetSidebarPalette(palette_);
  }
}

void MahoSidebarActionPopoverView::AddActionRow(
    MahoSidebarActionPopoverAction action,
    const gfx::VectorIcon& icon,
    const std::u16string& title,
    const std::u16string& subtitle) {
  int index = static_cast<int>(action_rows_.size());
  auto* row = AddChildView(std::make_unique<ActionRow>(
      icon, title, subtitle, index,
      base::BindRepeating(&MahoSidebarActionPopoverView::OnRowHovered,
                          base::Unretained(this)),
      base::BindRepeating(&MahoSidebarActionPopoverView::OnRowClicked,
                          base::Unretained(this))));
  action_rows_.push_back(row);
  action_types_.push_back(action);
}

void MahoSidebarActionPopoverView::OnRowHovered(int index) {
  highlighted_index_ = index;
  UpdateHighlightState();
}

void MahoSidebarActionPopoverView::OnRowClicked(int index) {
  highlighted_index_ = index;
  ActivateHighlightedRow();
}

void MahoSidebarActionPopoverView::UpdateHighlightState() {
  for (int i = 0; i < static_cast<int>(action_rows_.size()); ++i) {
    static_cast<ActionRow*>(action_rows_[i].get())
        ->SetHighlighted(i == highlighted_index_);
  }
}

void MahoSidebarActionPopoverView::ActivateHighlightedRow() {
  if (highlighted_index_ < 0 ||
      highlighted_index_ >= static_cast<int>(action_types_.size()) ||
      !action_callback_) {
    return;
  }

  const auto action = action_types_[highlighted_index_];
  const ActionCallback action_callback = action_callback_;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](ActionCallback callback, MahoSidebarActionPopoverAction action) {
            callback.Run(action);
          },
          action_callback, action));
}

}  // namespace maho
