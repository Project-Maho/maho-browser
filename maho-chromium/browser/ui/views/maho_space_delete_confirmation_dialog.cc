#include "maho_space_delete_confirmation_dialog.h"

#include <optional>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/thread_pool.h"
#include "base/values.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_state_adapter.h"  // nogncheck
#include "maho/third_party/maho/maho_ffi.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/text_constants.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/window/dialog_delegate.h"
#include "ui/views/widget/widget.h"

namespace {

constexpr char kDefaultSpaceName[] = "Space";
constexpr char16_t kDeleteBodyText[] =
    u"All tabs in this space will be closed.";

std::string ResolveSpaceName(const std::string& space_id) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return kDefaultSpaceName;
  }

  char* json_str = maho_core_get_space_view_models(core);
  if (!json_str) {
    return kDefaultSpaceName;
  }

  std::string json(json_str);
  maho_string_free(json_str);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    return kDefaultSpaceName;
  }

  for (const auto& item : parsed->GetList()) {
    const auto* dict = item.GetIfDict();
    if (!dict) {
      continue;
    }
    const std::string* id = dict->FindString("id");
    const std::string* name = dict->FindString("name");
    if (id && *id == space_id && name && !name->empty()) {
      return *name;
    }
  }

  return kDefaultSpaceName;
}

void DeleteSpaceOnPool(const std::string& space_id) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return;
  }

  maho_core_delete_space(core, space_id.c_str());
  maho::SidebarCacheInvalidation invalidation;
  invalidation.fragments = maho::SidebarCoreFragment::kAll;
  invalidation.space_ids.push_back(space_id);
  maho::InvalidateSidebarCoreCache(invalidation);
}

class MahoSpaceDeleteConfirmationDialogView : public views::DialogDelegate {
 public:
  MahoSpaceDeleteConfirmationDialogView(std::string space_id,
                                        std::u16string space_name)
      : space_id_(std::move(space_id)),
        space_name_(std::move(space_name)) {
    SetButtons(static_cast<int>(ui::mojom::DialogButton::kOk) |
               static_cast<int>(ui::mojom::DialogButton::kCancel));
    SetModalType(ui::mojom::ModalType::kWindow);
    SetDefaultButton(static_cast<int>(ui::mojom::DialogButton::kOk));
    SetButtonLabel(ui::mojom::DialogButton::kOk, u"Delete");
    SetButtonLabel(ui::mojom::DialogButton::kCancel, u"Cancel");

    auto contents = std::make_unique<views::View>();
    contents->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(16), 8));
    auto* label = contents->AddChildView(std::make_unique<views::Label>(
        std::u16string(kDeleteBodyText)));
    label->SetMultiLine(true);
    label->SetHorizontalAlignment(gfx::HorizontalAlignment::ALIGN_LEFT);
    SetContentsView(std::move(contents));
  }

  MahoSpaceDeleteConfirmationDialogView(
      const MahoSpaceDeleteConfirmationDialogView&) = delete;
  MahoSpaceDeleteConfirmationDialogView& operator=(
      const MahoSpaceDeleteConfirmationDialogView&) = delete;

  std::u16string GetWindowTitle() const override {
    return u"Delete \"" + space_name_ + u"\"?";
  }

  bool Accept() override {
    base::ThreadPool::PostTask(
        FROM_HERE, {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
        base::BindOnce(&DeleteSpaceOnPool, space_id_));
    if (auto* bridge = maho::MahoSpaceProfileBridge::GetInstance()) {
      bridge->UnregisterSpace(space_id_);
    }
    return true;
  }

  bool Cancel() override { return true; }

 private:
  std::string space_id_;
  std::u16string space_name_;
};

}  // namespace

void MahoSpaceDeleteConfirmationDialog::Show(Browser* browser,
                                             const std::string& space_id) {
  if (!browser || !browser->GetWindow()) {
    return;
  }

  const std::string space_name = ResolveSpaceName(space_id);
  auto delegate = std::make_unique<MahoSpaceDeleteConfirmationDialogView>(
      space_id, base::UTF8ToUTF16(space_name));
  views::Widget* widget = views::DialogDelegate::CreateDialogWidget(
      std::move(delegate), browser->GetWindow()->GetNativeWindow(),
      gfx::NativeView());
  if (widget) {
    widget->Show();
  }
}
