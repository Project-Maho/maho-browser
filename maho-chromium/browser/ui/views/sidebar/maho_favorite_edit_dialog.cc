// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_favorite_edit_dialog.h"

#include <memory>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/strings/utf_string_conversions.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/base/mojom/ui_base_types.mojom.h"
#include "ui/base/ui_base_types.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/text_constants.h"
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
constexpr int kRowSpacing = 6;
constexpr int kFieldHeightDp = 32;

using AcceptCallback = base::OnceCallback<void(std::string url)>;

class FavoriteEditDialogDelegate : public views::DialogDelegate {
 public:
  FavoriteEditDialogDelegate(const std::string& initial_url,
                             AcceptCallback on_accept)
      : on_accept_(std::move(on_accept)) {
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
        kRowSpacing));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);

    url_field_ = BuildUrlField(contents.get(), initial_url);
    SetInitiallyFocusedView(url_field_);

    SetContentsView(std::move(contents));
  }

  FavoriteEditDialogDelegate(const FavoriteEditDialogDelegate&) = delete;
  FavoriteEditDialogDelegate& operator=(const FavoriteEditDialogDelegate&) =
      delete;
  ~FavoriteEditDialogDelegate() override = default;

  std::u16string GetWindowTitle() const override {
    return u"Edit Pinned Page";
  }

 private:
  views::Textfield* BuildUrlField(views::View* parent,
                                  const std::string& value) {
    auto* caption = parent->AddChildView(std::make_unique<views::Label>(u"URL"));
    caption->SetHorizontalAlignment(gfx::ALIGN_LEFT);

    auto* field = parent->AddChildView(std::make_unique<views::Textfield>());
    field->SetText(base::UTF8ToUTF16(value));
    field->SetAccessibleName(u"URL");
    field->SetPreferredSize(gfx::Size(0, kFieldHeightDp));
    return field;
  }

  void OnAccepted() {
    if (!on_accept_) {
      return;
    }
    std::move(on_accept_).Run(base::UTF16ToUTF8(url_field_->GetText()));
  }

  AcceptCallback on_accept_;
  raw_ptr<views::Textfield> url_field_ = nullptr;
};

}  // namespace

// static
void MahoFavoriteEditDialog::Show(gfx::NativeWindow parent,
                                  const std::string& initial_url,
                                  AcceptCallback on_accept) {
  auto delegate = std::make_unique<FavoriteEditDialogDelegate>(
      initial_url, std::move(on_accept));
  views::Widget* widget = views::DialogDelegate::CreateDialogWidget(
      std::move(delegate), parent, gfx::NativeView());
  if (widget) {
    widget->Show();
  }
}

}  // namespace maho
