// Copyright 2026 Maho Browser. All rights reserved.

#include "maho_sidebar_downloads_view.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/platform_util.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_downloads_data.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_id.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/text_constants.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/style/typography.h"

namespace maho {

namespace {

constexpr int kPanelCornerRadiusDp = 18;
constexpr int kPanelPaddingDp = 12;
constexpr int kRowSpacingDp = 8;
constexpr int kRowPaddingDp = 10;
constexpr int kSectionSpacingDp = 10;

struct DownloadActionSpec {
  std::u16string label;
  std::u16string accessible_name;
  DownloadActionKind kind;
};

std::u16string StateLabel(const std::string& state) {
  if (state == "downloading") {
    return u"Downloading";
  }
  if (state == "paused") {
    return u"Paused";
  }
  if (state == "completed") {
    return u"Completed";
  }
  if (state == "failed") {
    return u"Failed";
  }
  if (state == "cancelled") {
    return u"Cancelled";
  }
  return base::UTF8ToUTF16(state);
}

std::u16string ProgressText(const DownloadItem& item) {
  if (item.total_bytes == 0) {
    return item.received_bytes == 0 ? u"0 bytes" :
                                      base::UTF8ToUTF16(std::to_string(item.received_bytes) + " bytes");
  }
  return base::UTF8ToUTF16(std::to_string(item.received_bytes) + " / " +
                           std::to_string(item.total_bytes) + " bytes");
}

std::u16string SubtitleText(const DownloadItem& item) {
  std::u16string text = StateLabel(item.state);
  if (!item.error.has_value() && item.state == "downloading") {
    text += u" · ";
    text += ProgressText(item);
  } else if (item.error.has_value() && !item.error->empty()) {
    text += u" · ";
    text += base::UTF8ToUTF16(*item.error);
  }
  return text;
}

std::vector<DownloadActionSpec> BuildActionsForDownload(const DownloadItem& item) {
  if (item.state == "downloading") {
    return {{u"Pause", u"Pause download", DownloadActionKind::kPause},
            {u"Cancel", u"Cancel download", DownloadActionKind::kCancel}};
  }
  if (item.state == "paused") {
    return {{u"Resume", u"Resume download", DownloadActionKind::kResume},
            {u"Cancel", u"Cancel download", DownloadActionKind::kCancel}};
  }
  if (HasRealFilePath(item)) {
    return {{u"Open", u"Open download", DownloadActionKind::kOpen},
            {u"Reveal", u"Reveal download in folder",
             DownloadActionKind::kReveal},
            {u"Remove", u"Remove download", DownloadActionKind::kRemove}};
  }
  return {{u"Remove", u"Remove download", DownloadActionKind::kRemove}};
}

class DownloadRowView : public views::View {
  METADATA_HEADER(DownloadRowView, views::View)

 public:
  using ActionCallback =
      base::RepeatingCallback<void(const std::string&, DownloadActionKind)>;

  DownloadRowView(const DownloadItem& item,
                  std::vector<DownloadActionSpec> actions,
                  ActionCallback action_callback)
      : download_id_(item.id), action_callback_(std::move(action_callback)) {
    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical,
        gfx::Insets::TLBR(kRowPaddingDp, kRowPaddingDp, kRowPaddingDp,
                          kRowPaddingDp),
        4));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);

    auto* title = AddChildView(std::make_unique<views::Label>(item.filename));
    title->SetTextStyle(views::style::STYLE_BODY_4_MEDIUM);
    title->SetEnabledColor(ui::kColorSysOnSurface);
    title->SetAutoColorReadabilityEnabled(false);
    title->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    title->SetMultiLine(false);
    title->SetElideBehavior(gfx::ELIDE_TAIL);

    auto* subtitle =
        AddChildView(std::make_unique<views::Label>(SubtitleText(item)));
    subtitle->SetTextStyle(views::style::STYLE_BODY_5);
    subtitle->SetEnabledColor(ui::kColorSysOnSurfaceSubtle);
    subtitle->SetAutoColorReadabilityEnabled(false);
    subtitle->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    subtitle->SetMultiLine(false);
    subtitle->SetElideBehavior(gfx::ELIDE_TAIL);

    if (!actions.empty()) {
      auto* action_row = AddChildView(std::make_unique<views::View>());
      auto* action_layout = action_row->SetLayoutManager(
          std::make_unique<views::BoxLayout>(
              views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 6));
      action_layout->set_cross_axis_alignment(
          views::BoxLayout::CrossAxisAlignment::kCenter);

      for (const auto& action : actions) {
        auto* button = action_row->AddChildView(std::make_unique<views::MdTextButton>(
            base::BindRepeating(&DownloadRowView::HandleActionPressed,
                                base::Unretained(this), action.kind),
            action.label));
        button->SetStyle(ui::ButtonStyle::kText);
        button->SetCornerRadius(18);
        button->SetMinSize(gfx::Size(0, 32));
        button->SetMaxSize(gfx::Size(0, 32));
        button->SetRequestFocusOnPress(false);
        button->SetAccessibleName(action.accessible_name.empty()
                                      ? action.label
                                      : action.accessible_name);
        action_buttons_.push_back(button);
      }
    }

    GetViewAccessibility().SetRole(ax::mojom::Role::kListItem);
    GetViewAccessibility().SetName(item.filename);
  }

  bool MatchesDownloadId(const std::string& download_id) const {
    return download_id_ == download_id;
  }

  views::Button* button_for_action_for_testing(size_t action_index) {
    if (action_index >= action_buttons_.size()) {
      return nullptr;
    }
    return action_buttons_[action_index];
  }

 private:
  void HandleActionPressed(DownloadActionKind kind) {
    if (!action_callback_) {
      return;
    }
    action_callback_.Run(download_id_, kind);
  }

  std::string download_id_;
  ActionCallback action_callback_;
  std::vector<views::Button*> action_buttons_;
};

BEGIN_METADATA(DownloadRowView)
END_METADATA

}  // namespace

BEGIN_METADATA(MahoSidebarDownloadsView)
END_METADATA

MahoSidebarDownloadsView::MahoSidebarDownloadsView() {
  BuildUi();
}

MahoSidebarDownloadsView::~MahoSidebarDownloadsView() = default;

views::View* MahoSidebarDownloadsView::download_row_for_download_id_for_testing(
    const std::string& download_id) {
  if (!list_container_) {
    return nullptr;
  }

  for (const auto& child : list_container_->children()) {
    auto* row = static_cast<DownloadRowView*>(child.get());
    if (row->MatchesDownloadId(download_id)) {
      return row;
    }
  }

  return nullptr;
}

views::Button* MahoSidebarDownloadsView::download_action_button_for_testing(
    const std::string& download_id,
    size_t action_index) {
  auto* row = download_row_for_download_id_for_testing(download_id);
  if (!row) {
    return nullptr;
  }
  return static_cast<DownloadRowView*>(row)->button_for_action_for_testing(
      action_index);
}

void MahoSidebarDownloadsView::BuildUi() {
  SetLayoutManager(std::make_unique<views::FillLayout>());
  SetBackground(views::CreateRoundedRectBackground(ui::kColorSysSurface1,
                                                   kPanelCornerRadiusDp));

  auto* surface = AddChildView(std::make_unique<views::View>());
  auto* surface_layout = surface->SetLayoutManager(
      std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kVertical,
                                         gfx::Insets::TLBR(kPanelPaddingDp,
                                                           kPanelPaddingDp,
                                                           kPanelPaddingDp,
                                                           kPanelPaddingDp),
                                         kSectionSpacingDp));
  surface_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  surface->SetBackground(nullptr);
  surface->SetBorder(nullptr);
  surface->GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
  surface->GetViewAccessibility().SetName(u"Downloads panel");

  auto* header = surface->AddChildView(std::make_unique<views::Label>(u"Downloads"));
  header->SetTextStyle(views::style::STYLE_BODY_3_MEDIUM);
  header->SetEnabledColor(ui::kColorSysOnSurface);
  header->SetAutoColorReadabilityEnabled(false);
  header->SetHorizontalAlignment(gfx::ALIGN_LEFT);

  scroll_view_ = surface->AddChildView(std::make_unique<views::ScrollView>());
  scroll_view_->SetBackgroundColor(std::nullopt);
  scroll_view_->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  scroll_view_->GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
  scroll_view_->GetViewAccessibility().SetName(u"Downloads results container");

  list_container_ = scroll_view_->SetContents(std::make_unique<views::View>());
  auto* list_layout = list_container_->SetLayoutManager(
      std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kVertical,
                                         gfx::Insets::VH(0, 0),
                                         kRowSpacingDp));
  list_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  list_container_->GetViewAccessibility().SetRole(ax::mojom::Role::kList);
  list_container_->GetViewAccessibility().SetName(u"Downloads results");

  empty_state_view_ = surface->AddChildView(std::make_unique<views::Label>(u"No downloads"));
  empty_state_view_->SetTextStyle(views::style::STYLE_BODY_4);
  empty_state_view_->SetEnabledColor(ui::kColorSysOnSurfaceSubtle);
  empty_state_view_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  empty_state_view_->SetMultiLine(true);
  empty_state_view_->SetAutoColorReadabilityEnabled(false);

  empty_state_view_->SetVisible(true);
  scroll_view_->SetVisible(false);
}

void MahoSidebarDownloadsView::ReloadDownloads() {
  std::vector<DownloadItem> downloads = ParseDownloads();

  list_container_->RemoveAllChildViews();
  if (downloads.empty()) {
    empty_state_view_->SetVisible(true);
    scroll_view_->SetVisible(false);
    return;
  }

  for (const auto& item : downloads) {
    list_container_->AddChildView(std::make_unique<DownloadRowView>(
        item, BuildActionsForDownload(item),
        base::BindRepeating(&MahoSidebarDownloadsView::HandleDownloadAction,
                            base::Unretained(this))));
  }

  empty_state_view_->SetVisible(false);
  scroll_view_->SetVisible(true);
}

void MahoSidebarDownloadsView::SetDownloadFileActionCallbackForTesting(
    base::RepeatingCallback<void(const std::string&, DownloadActionKind)>
        callback) {
  download_file_action_callback_for_testing_ = std::move(callback);
}

void MahoSidebarDownloadsView::HandleDownloadAction(
    const std::string& download_id,
    DownloadActionKind action) {
  if (action == DownloadActionKind::kOpen ||
      action == DownloadActionKind::kReveal) {
    const std::vector<DownloadItem> downloads = ParseDownloads();
    const auto it = std::find_if(downloads.begin(), downloads.end(),
                                 [&](const DownloadItem& item) {
                                   return item.id == download_id;
                                 });
    if (it == downloads.end() || !HasRealFilePath(*it)) {
      return;
    }

    const base::FilePath path =
        base::FilePath::FromUTF8Unsafe(*it->file_path);
    if (download_file_action_callback_for_testing_) {
      download_file_action_callback_for_testing_.Run(path.value(), action);
      return;
    }

    if (action == DownloadActionKind::kOpen) {
      platform_util::OpenItem(nullptr, path, platform_util::OPEN_FILE,
                              platform_util::OpenOperationCallback());
    } else {
      platform_util::ShowItemInFolder(nullptr, path);
    }
    return;
  }

  MahoCore* core = maho::GetCore();
  if (!core) {
    return;
  }

  switch (action) {
    case DownloadActionKind::kPause:
      maho_core_pause_download(core, download_id.c_str());
      break;
    case DownloadActionKind::kResume:
      maho_core_resume_download(core, download_id.c_str());
      break;
    case DownloadActionKind::kCancel:
      maho_core_cancel_download(core, download_id.c_str());
      break;
    case DownloadActionKind::kRemove:
      maho_core_remove_download(core, download_id.c_str());
      break;
    case DownloadActionKind::kOpen:
    case DownloadActionKind::kReveal:
      break;
  }

  ReloadDownloads();
}

}  // namespace maho
