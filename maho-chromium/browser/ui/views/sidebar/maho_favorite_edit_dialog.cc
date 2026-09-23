// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_favorite_edit_dialog.h"

#include <array>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/strings/utf_string_conversions.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/base/mojom/ui_base_types.mojom.h"
#include "ui/base/ui_base_types.h"
#include "ui/color/color_id.h"
#include "ui/gfx/font.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/text_constants.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/dialog_delegate.h"

namespace maho {

namespace {

constexpr int kDialogWidth = 380;
constexpr int kContentInset = 16;
constexpr int kSectionSpacing = 12;
constexpr int kRowSpacing = 6;
constexpr int kGlyphButtonSizeDp = 34;
constexpr int kGlyphButtonCornerRadiusDp = 8;
constexpr int kGlyphFontSizeDelta = 7;
constexpr size_t kGlyphsPerRow = 9;
constexpr int kFieldHeightDp = 32;

constexpr auto kCuratedGlyphs = std::to_array<const char16_t*>({
    u"🚀", u"⭐", u"🔥", u"💡", u"📌", u"📚", u"🎧", u"🎮", u"🧠",
    u"🛠", u"🌍", u"🍿", u"☕", u"💬", u"📷", u"🏠", u"💼", u"🎨",
});

using AcceptCallback = base::OnceCallback<
    void(std::u16string name, std::string icon, std::string url)>;

class GlyphPickerButton : public views::LabelButton {
  METADATA_HEADER(GlyphPickerButton, views::LabelButton)

 public:
  GlyphPickerButton(PressedCallback callback, const std::u16string& glyph)
      : views::LabelButton(std::move(callback), glyph) {
    SetPreferredSize(gfx::Size(kGlyphButtonSizeDp, kGlyphButtonSizeDp));
    SetMinSize(gfx::Size(kGlyphButtonSizeDp, kGlyphButtonSizeDp));
    SetHorizontalAlignment(gfx::ALIGN_CENTER);
    SetAccessibleName(glyph);
    label()->SetFontList(label()->font_list().Derive(
        kGlyphFontSizeDelta, gfx::Font::NORMAL, gfx::Font::Weight::NORMAL));
  }

  GlyphPickerButton(const GlyphPickerButton&) = delete;
  GlyphPickerButton& operator=(const GlyphPickerButton&) = delete;
  ~GlyphPickerButton() override = default;

  void SetSelected(bool selected) {
    SetBackground(selected
                      ? views::CreateRoundedRectBackground(
                            ui::kColorSysTonalContainer,
                            kGlyphButtonCornerRadiusDp)
                      : nullptr);
  }
};

BEGIN_METADATA(GlyphPickerButton)
END_METADATA

class FavoriteEditDialogDelegate : public views::DialogDelegate {
 public:
  FavoriteEditDialogDelegate(MahoFavoriteEditDialog::Focus focus,
                             const std::u16string& initial_name,
                             const std::string& initial_icon,
                             const std::string& initial_url,
                             AcceptCallback on_accept)
      : selected_icon_(initial_icon), on_accept_(std::move(on_accept)) {
    SetButtons(static_cast<int>(ui::mojom::DialogButton::kOk) |
               static_cast<int>(ui::mojom::DialogButton::kCancel));
    SetModalType(ui::mojom::ModalType::kWindow);
    SetDefaultButton(static_cast<int>(ui::mojom::DialogButton::kOk));
    SetButtonLabel(ui::mojom::DialogButton::kOk, u"Save");
    SetButtonLabel(ui::mojom::DialogButton::kCancel, u"Cancel");
    SetShowCloseButton(false);
    set_fixed_width(kDialogWidth);
    SetAcceptCallback(base::BindOnce(&FavoriteEditDialogDelegate::OnAccepted,
                                     base::Unretained(this)));

    auto contents = std::make_unique<views::View>();
    auto* layout = contents->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(kContentInset),
        kSectionSpacing));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);

    BuildIconSection(contents.get());
    name_field_ = BuildLabeledField(contents.get(), u"Name", initial_name);
    url_field_ = BuildLabeledField(contents.get(), u"URL",
                                   base::UTF8ToUTF16(initial_url));

    views::View* initial_focus = name_field_;
    switch (focus) {
      case MahoFavoriteEditDialog::Focus::kName:
        initial_focus = name_field_;
        break;
      case MahoFavoriteEditDialog::Focus::kIcon:
        if (!glyph_buttons_.empty()) {
          initial_focus = glyph_buttons_.front();
        }
        break;
      case MahoFavoriteEditDialog::Focus::kUrl:
        initial_focus = url_field_;
        break;
    }
    SetInitiallyFocusedView(initial_focus);

    SetContentsView(std::move(contents));
    UpdateGlyphHighlight();
  }

  FavoriteEditDialogDelegate(const FavoriteEditDialogDelegate&) = delete;
  FavoriteEditDialogDelegate& operator=(const FavoriteEditDialogDelegate&) =
      delete;
  ~FavoriteEditDialogDelegate() override = default;

  std::u16string GetWindowTitle() const override { return u"Edit Favorite"; }

 private:
  void BuildIconSection(views::View* parent) {
    auto* section = parent->AddChildView(std::make_unique<views::View>());
    auto* section_layout =
        section->SetLayoutManager(std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kVertical, gfx::Insets(),
            kRowSpacing));
    section_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);

    auto* caption =
        section->AddChildView(std::make_unique<views::Label>(u"Icon"));
    caption->SetHorizontalAlignment(gfx::ALIGN_LEFT);

    views::View* row = nullptr;
    for (size_t index = 0; index < kCuratedGlyphs.size(); ++index) {
      if (index % kGlyphsPerRow == 0) {
        row = section->AddChildView(std::make_unique<views::View>());
        auto* row_layout =
            row->SetLayoutManager(std::make_unique<views::BoxLayout>(
                views::BoxLayout::Orientation::kHorizontal, gfx::Insets(),
                kRowSpacing));
        row_layout->set_main_axis_alignment(
            views::BoxLayout::MainAxisAlignment::kStart);
        row_layout->set_cross_axis_alignment(
            views::BoxLayout::CrossAxisAlignment::kCenter);
      }
      const std::u16string glyph(kCuratedGlyphs[index]);
      auto* button = row->AddChildView(std::make_unique<GlyphPickerButton>(
          base::BindRepeating(&FavoriteEditDialogDelegate::OnGlyphPressed,
                              base::Unretained(this), glyph),
          glyph));
      glyph_buttons_.push_back(button);
      glyph_values_.push_back(base::UTF16ToUTF8(glyph));
    }

    auto* reset_row = section->AddChildView(std::make_unique<views::View>());
    auto* reset_layout =
        reset_row->SetLayoutManager(std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 0));
    reset_layout->set_main_axis_alignment(
        views::BoxLayout::MainAxisAlignment::kStart);
    auto* reset_button =
        reset_row->AddChildView(std::make_unique<views::MdTextButton>(
            base::BindRepeating(&FavoriteEditDialogDelegate::OnResetPressed,
                                base::Unretained(this)),
            u"Reset to Favicon"));
    reset_button->SetStyle(ui::ButtonStyle::kText);
    reset_button->SetAccessibleName(u"Reset to Favicon");
  }

  views::Textfield* BuildLabeledField(views::View* parent,
                                      const std::u16string& caption_text,
                                      const std::u16string& value) {
    auto* section = parent->AddChildView(std::make_unique<views::View>());
    auto* section_layout =
        section->SetLayoutManager(std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kVertical, gfx::Insets(),
            kRowSpacing));
    section_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);

    auto* caption =
        section->AddChildView(std::make_unique<views::Label>(caption_text));
    caption->SetHorizontalAlignment(gfx::ALIGN_LEFT);

    auto* field = section->AddChildView(std::make_unique<views::Textfield>());
    field->SetText(value);
    field->SetAccessibleName(caption_text);
    field->SetPreferredSize(gfx::Size(0, kFieldHeightDp));
    return field;
  }

  void OnGlyphPressed(const std::u16string& glyph) {
    selected_icon_ = base::UTF16ToUTF8(glyph);
    UpdateGlyphHighlight();
  }

  void OnResetPressed() {
    selected_icon_.clear();
    UpdateGlyphHighlight();
  }

  void UpdateGlyphHighlight() {
    for (size_t index = 0; index < glyph_buttons_.size(); ++index) {
      glyph_buttons_[index]->SetSelected(
          !selected_icon_.empty() && glyph_values_[index] == selected_icon_);
    }
  }

  void OnAccepted() {
    if (!on_accept_) {
      return;
    }
    std::move(on_accept_)
        .Run(std::u16string(name_field_->GetText()), selected_icon_,
             base::UTF16ToUTF8(url_field_->GetText()));
  }

  std::string selected_icon_;
  AcceptCallback on_accept_;
  std::vector<raw_ptr<GlyphPickerButton>> glyph_buttons_;
  std::vector<std::string> glyph_values_;
  raw_ptr<views::Textfield> name_field_ = nullptr;
  raw_ptr<views::Textfield> url_field_ = nullptr;
};

}  // namespace

// static
void MahoFavoriteEditDialog::Show(gfx::NativeWindow parent,
                                  Focus focus,
                                  const std::u16string& initial_name,
                                  const std::string& initial_icon,
                                  const std::string& initial_url,
                                  AcceptCallback on_accept) {
  auto delegate = std::make_unique<FavoriteEditDialogDelegate>(
      focus, initial_name, initial_icon, initial_url, std::move(on_accept));
  views::Widget* widget = views::DialogDelegate::CreateDialogWidget(
      std::move(delegate), parent, gfx::NativeView());
  if (widget) {
    widget->Show();
  }
}

}  // namespace maho
