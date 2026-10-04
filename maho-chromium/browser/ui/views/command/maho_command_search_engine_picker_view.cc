#include "maho/browser/ui/views/command/maho_command_search_engine_picker_view.h"

#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/location.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/favicon/large_icon_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "components/favicon/core/large_icon_service.h"
#include "components/vector_icons/vector_icons.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/ui/views/command/maho_command_model.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/class_property.h"
#include "ui/base/ui_base_types.h"
#include "ui/color/color_id.h"
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
#include "url/gurl.h"

namespace maho {

namespace {

DEFINE_OWNED_UI_CLASS_PROPERTY_KEY(views::BubbleDialogDelegate,
                                 kSearchEngineDelegateKey)

constexpr int kCardCornerRadius = 16;
constexpr int kCardPadding = 8;
constexpr int kRowSpacing = 2;
constexpr int kRowWidth = 280;
constexpr int kRowHeight = 40;
constexpr int kRowCornerRadius = 8;
constexpr int kRowHorizontalPadding = 12;
constexpr int kIconSize = 16;
constexpr int kIconLabelSpacing = 10;

}  // namespace

class MahoCommandSearchEnginePickerView::SearchEngineRow : public views::View {
  METADATA_HEADER(SearchEngineRow, views::View)

 public:
  SearchEngineRow(const std::u16string& name,
                  bool is_default,
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
    selection_background_->SetBackground(
        views::CreateRoundedRectBackground(ui::kColorSysSurfaceVariant,
                                           kRowCornerRadius));

    auto* content_row = AddChildView(std::make_unique<views::View>());
    auto* row_layout =
        content_row->SetLayoutManager(std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kHorizontal,
            gfx::Insets::VH(0, kRowHorizontalPadding), kIconLabelSpacing));
    row_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);

    favicon_view_ = content_row->AddChildView(std::make_unique<views::ImageView>());
    favicon_view_->SetPreferredSize(gfx::Size(kIconSize, kIconSize));
    favicon_view_->SetImage(ui::ImageModel::FromVectorIcon(
        vector_icons::kGlobeIcon, ui::kColorSysOnSurfaceSubtle, kIconSize));

    name_label_ = content_row->AddChildView(std::make_unique<views::Label>(
        name, views::style::CONTEXT_LABEL, views::style::STYLE_BODY_3_MEDIUM));
    name_label_->SetAutoColorReadabilityEnabled(false);
    name_label_->SetEnabledColor(ui::kColorSysOnSurface);
    name_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    row_layout->SetFlexForView(name_label_, 1);

    checkmark_view_ = content_row->AddChildView(std::make_unique<views::ImageView>());
    checkmark_view_->SetPreferredSize(gfx::Size(kIconSize, kIconSize));
    checkmark_view_->SetImage(ui::ImageModel::FromVectorIcon(
        vector_icons::kCheckCircleOldIcon, ui::kColorSysPrimary, kIconSize));
    checkmark_view_->SetVisible(is_default);

    GetViewAccessibility().SetRole(ax::mojom::Role::kButton);
    SetAccessibleName(name);
  }

  void SetHighlighted(bool highlighted) {
    selection_background_->SetVisible(highlighted);
  }

  void SetFavicon(const gfx::Image& image) {
    if (!image.IsEmpty()) {
      favicon_view_->SetImage(ui::ImageModel::FromImageSkia(image.AsImageSkia()));
    }
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
  raw_ptr<views::ImageView> favicon_view_ = nullptr;
  raw_ptr<views::Label> name_label_ = nullptr;
  raw_ptr<views::ImageView> checkmark_view_ = nullptr;
};

BEGIN_METADATA(MahoCommandSearchEnginePickerView, SearchEngineRow)
END_METADATA

BEGIN_METADATA(MahoCommandSearchEnginePickerView)
END_METADATA

MahoCommandSearchEnginePickerView::SearchEngineItem::SearchEngineItem() = default;
MahoCommandSearchEnginePickerView::SearchEngineItem::SearchEngineItem(
    const SearchEngineItem&) = default;
MahoCommandSearchEnginePickerView::SearchEngineItem::SearchEngineItem(
    SearchEngineItem&&) = default;
MahoCommandSearchEnginePickerView::SearchEngineItem&
MahoCommandSearchEnginePickerView::SearchEngineItem::operator=(
    const SearchEngineItem&) = default;
MahoCommandSearchEnginePickerView::SearchEngineItem&
MahoCommandSearchEnginePickerView::SearchEngineItem::operator=(
    SearchEngineItem&&) = default;
MahoCommandSearchEnginePickerView::SearchEngineItem::~SearchEngineItem() =
    default;

MahoCommandSearchEnginePickerView::MahoCommandSearchEnginePickerView(
    SelectedCallback selected_callback,
    raw_ptr<MahoCommandModel> model)
    : selected_callback_(std::move(selected_callback)),
      model_(model) {
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets(kCardPadding), kRowSpacing));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  SetBackground(views::CreateRoundedRectBackground(ui::kColorSysSurface2,
                                                   kCardCornerRadius));
  SetBorder(views::CreateRoundedRectBorder(1, kCardCornerRadius,
                                           ui::kColorSysNeutralOutline));

  GetViewAccessibility().SetRole(ax::mojom::Role::kDialog);
  SetAccessibleName(u"Search engines");
  SetFocusBehavior(FocusBehavior::ALWAYS);

  MahoCore* core = maho::GetCore();
  if (core) {
    char* json = maho_core_get_search_engines(core);
    if (json) {
      std::string json_str(json);
      maho_string_free(json);
      std::optional<base::Value> parsed =
          base::JSONReader::Read(json_str, base::JSON_PARSE_RFC);
      if (parsed && parsed->is_list()) {
        for (const auto& item : parsed->GetList()) {
          const auto* dict = item.GetIfDict();
          if (!dict)
            continue;
          const std::string* id = dict->FindString("id");
          const std::string* name = dict->FindString("name");
          const std::optional<bool> is_default = dict->FindBool("isDefault");
          if (!id || !name)
            continue;

          SearchEngineItem engine;
          engine.id = *id;
          engine.name = *name;
          engine.is_default = is_default.value_or(false);

          if (engine.id == "google") {
            engine.homepage_url = GURL("https://www.google.com");
          } else if (engine.id == "duckduckgo") {
            engine.homepage_url = GURL("https://duckduckgo.com");
          } else if (engine.id == "bing") {
            engine.homepage_url = GURL("https://www.bing.com");
          } else if (engine.id == "brave") {
            engine.homepage_url = GURL("https://search.brave.com");
          } else if (engine.id == "ecosia") {
            engine.homepage_url = GURL("https://www.ecosia.org");
          } else {
            engine.homepage_url = GURL("https://www.google.com");
          }
          engines_.push_back(std::move(engine));
        }
      }
    }
  }

  favicon::LargeIconService* large_icon_service = nullptr;
  if (model_ && model_->browser()) {
    large_icon_service =
        LargeIconServiceFactory::GetForBrowserContext(model_->browser()->GetProfile());
  }

  for (size_t i = 0; i < engines_.size(); ++i) {
    int index = static_cast<int>(i);
    auto* row = AddChildView(std::make_unique<SearchEngineRow>(
        base::UTF8ToUTF16(engines_[i].name), engines_[i].is_default, index,
        base::BindRepeating(&MahoCommandSearchEnginePickerView::OnRowHovered,
                            base::Unretained(this)),
        base::BindRepeating(&MahoCommandSearchEnginePickerView::OnRowClicked,
                            base::Unretained(this))));
    rows_.push_back(row);

    if (engines_[i].is_default) {
      highlighted_index_ = index;
    }

    if (large_icon_service && engines_[i].homepage_url.is_valid()) {
      large_icon_service->GetLargeIconImageOrFallbackStyleForPageUrl(
          engines_[i].homepage_url,
          16,
          16,
          base::BindOnce(&MahoCommandSearchEnginePickerView::OnFaviconLoaded,
                         weak_factory_.GetWeakPtr(), index),
          &favicon_task_tracker_);
    }
  }

  UpdateHighlightState();
}

MahoCommandSearchEnginePickerView::~MahoCommandSearchEnginePickerView() = default;

views::Widget* MahoCommandSearchEnginePickerView::Show(
    views::View* anchor_view,
    SelectedCallback selected_callback,
    raw_ptr<MahoCommandModel> model) {
  if (!anchor_view) {
    return nullptr;
  }

  auto delegate = std::make_unique<views::BubbleDialogDelegate>(
      anchor_view, views::BubbleBorder::TOP_RIGHT);
  delegate->SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  delegate->set_margins(gfx::Insets());
  delegate->set_close_on_deactivate(true);

  auto contents = std::make_unique<MahoCommandSearchEnginePickerView>(
      std::move(selected_callback), model);
  delegate->SetContentsView(std::move(contents));

  views::Widget* widget =
      views::BubbleDialogDelegate::CreateBubbleDeprecated(
          delegate.get(), views::Widget::InitParams::NATIVE_WIDGET_OWNS_WIDGET);
  if (widget) {
    widget->SetProperty(kSearchEngineDelegateKey, std::move(delegate));
    widget->Show();
  }
  return widget;
}

bool MahoCommandSearchEnginePickerView::OnKeyPressed(const ui::KeyEvent& event) {
  const int count = static_cast<int>(rows_.size());
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

void MahoCommandSearchEnginePickerView::OnRowHovered(int index) {
  highlighted_index_ = index;
  UpdateHighlightState();
}

void MahoCommandSearchEnginePickerView::OnRowClicked(int index) {
  highlighted_index_ = index;
  ActivateHighlightedRow();
}

void MahoCommandSearchEnginePickerView::UpdateHighlightState() {
  for (int i = 0; i < static_cast<int>(rows_.size()); ++i) {
    rows_[i]->SetHighlighted(i == highlighted_index_);
  }
}

void MahoCommandSearchEnginePickerView::ActivateHighlightedRow() {
  if (highlighted_index_ < 0 ||
      highlighted_index_ >= static_cast<int>(engines_.size()) ||
      !selected_callback_) {
    return;
  }

  const std::string engine_id = engines_[highlighted_index_].id;
  const SelectedCallback selected_callback = selected_callback_;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](SelectedCallback callback, const std::string& id, views::Widget* widget) {
            callback.Run(id);
            if (widget) {
              widget->Close();
            }
          },
          selected_callback, engine_id, GetWidget()));
}

void MahoCommandSearchEnginePickerView::OnFaviconLoaded(
    int row_index,
    const favicon_base::LargeIconImageResult& result) {
  if (result.image.IsEmpty()) {
    return;
  }
  if (row_index >= 0 && row_index < static_cast<int>(rows_.size())) {
    rows_[row_index]->SetFavicon(result.image);
  }
}

}  // namespace maho
