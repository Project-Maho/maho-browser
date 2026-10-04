// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/importer/maho_migration_dialog_view.h"

#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/files/file_path.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/memory/raw_ref.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/single_thread_task_runner.h"
#include "base/values.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/grit/generated_resources.h"
#include "content/public/browser/browser_thread.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/components/maho_importer/maho_importer_constants.mojom.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/models/combobox_model.h"
#include "ui/gfx/canvas.h"
#include "ui/shell_dialogs/select_file_policy.h"
#include "ui/shell_dialogs/selected_file_info.h"

// These string ids were referenced before a grit target ever defined them, so
// the translation unit never compiled. Define them locally as a distinct enum:
// GetMahoLocalizedString() switches on them for the ko locale and falls back to
// l10n_util for everything else, so the values only need to be unique and
// stable within this file.
// Newer Chromium revisions define IDS_MAHO_MIGRATION_* grit macros; skip the
// enum there and let the macros provide the identifiers.
#ifndef IDS_MAHO_MIGRATION_TITLE
enum MahoMigrationStringIds {
  IDS_MAHO_MIGRATION_BROWSER_SELECTION,
  IDS_MAHO_MIGRATION_CANCEL,
  IDS_MAHO_MIGRATION_CHECKBOX_AUTOFILL,
  IDS_MAHO_MIGRATION_CHECKBOX_BOOKMARKS,
  IDS_MAHO_MIGRATION_CHECKBOX_COOKIES,
  IDS_MAHO_MIGRATION_CHECKBOX_FAVICONS,
  IDS_MAHO_MIGRATION_CHECKBOX_HISTORY,
  IDS_MAHO_MIGRATION_CHECKBOX_PASSWORDS,
  IDS_MAHO_MIGRATION_CHECKBOX_SIDEBAR,
  IDS_MAHO_MIGRATION_CHECKBOX_WORKSPACES,
  IDS_MAHO_MIGRATION_CLOSE,
  IDS_MAHO_MIGRATION_CONTINUE,
  IDS_MAHO_MIGRATION_IMPORT,
  IDS_MAHO_MIGRATION_RETRY,
  IDS_MAHO_MIGRATION_STATUS_CANCELLING,
  IDS_MAHO_MIGRATION_STATUS_ERROR,
  IDS_MAHO_MIGRATION_STATUS_IMPORTING,
  IDS_MAHO_MIGRATION_STATUS_PARTIAL_FAIL,
  IDS_MAHO_MIGRATION_STATUS_SUCCESS,
  IDS_MAHO_MIGRATION_TITLE,
};
#endif
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/box_layout_view.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/widget/widget.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_impl_macros.h"

namespace maho {

namespace {

// Dialog dimensions per spec.
constexpr int kDialogWidth = 720;
constexpr int kDialogHeight = 540;

// Card styling.
constexpr int kCardPaddingVertical = 18;   // ~1.1rem
constexpr int kCardPaddingHorizontal = 24; // ~1.5rem
constexpr int kCardBorderWidth = 2;
constexpr int kCardCornerRadius = 10;
constexpr int kCardFaviconSize = 32;
constexpr int kCardSpacing = 10;

// Maho primary color (#5b4fcf).
constexpr SkColor kMahoPrimaryColor = SkColorSetRGB(0x5b, 0x4f, 0xcf);
constexpr SkColor kCardBorderColor = SkColorSetRGB(0xe0, 0xe0, 0xe0);
constexpr SkColor kCardHoverShadowColor = SkColorSetARGB(0x20, 0x00, 0x00, 0x00);

// Cancel timeout during import (ms).
constexpr int kCancelTimeoutMs = 500;

std::u16string GetMahoLocalizedString(int resource_id) {
  std::string locale = g_browser_process->GetApplicationLocale();
  if (base::StartsWith(locale, "ko", base::CompareCase::INSENSITIVE_ASCII)) {
    switch (resource_id) {
      case IDS_MAHO_MIGRATION_TITLE:
        return u"다른 브라우저에서 데이터 가져오기";
      case IDS_MAHO_MIGRATION_IMPORT:
        return u"가져오기";
      case IDS_MAHO_MIGRATION_CANCEL:
        return u"취소";
      case IDS_MAHO_MIGRATION_CLOSE:
        return u"닫기";
      case IDS_MAHO_MIGRATION_BROWSER_SELECTION:
        return u"가져올 브라우저 선택";
      case IDS_MAHO_MIGRATION_CHECKBOX_BOOKMARKS:
        return u"북마크";
      case IDS_MAHO_MIGRATION_CHECKBOX_HISTORY:
        return u"방문 기록";
      case IDS_MAHO_MIGRATION_CHECKBOX_PASSWORDS:
        return u"비밀번호";
      case IDS_MAHO_MIGRATION_CHECKBOX_COOKIES:
        return u"쿠키";
      case IDS_MAHO_MIGRATION_CHECKBOX_AUTOFILL:
        return u"자동 완성 양식 데이터";
      case IDS_MAHO_MIGRATION_CHECKBOX_FAVICONS:
        return u"사이트 아이콘 (파비콘)";
      case IDS_MAHO_MIGRATION_CHECKBOX_WORKSPACES:
        return u"워크스페이스 및 탭 (Spaces & Tabs)";
      case IDS_MAHO_MIGRATION_CHECKBOX_SIDEBAR:
        return u"사이드바 항목";
      case IDS_MAHO_MIGRATION_STATUS_IMPORTING:
        return u"가져오는 중...";
      case IDS_MAHO_MIGRATION_STATUS_CANCELLING:
        return u"취소하는 중...";
      case IDS_MAHO_MIGRATION_STATUS_SUCCESS:
        return u"가져오기 완료";
      case IDS_MAHO_MIGRATION_STATUS_ERROR:
        return u"가져오기 실패";
      case IDS_MAHO_MIGRATION_STATUS_PARTIAL_FAIL:
        return u"일부 항목 가져오기 실패";
      case IDS_MAHO_MIGRATION_CONTINUE:
        return u"계속";
      case IDS_MAHO_MIGRATION_RETRY:
        return u"다시 시도";
    }
  }
  return l10n_util::GetStringUTF16(resource_id);
}

// Data types and their string IDs.
struct DataTypeInfo {
  uint32_t flag;
  int string_id;
};

const DataTypeInfo kDataTypeInfos[] = {
    {mojom::kImportBookmarks, IDS_MAHO_MIGRATION_CHECKBOX_BOOKMARKS},
    {mojom::kImportPasswords, IDS_MAHO_MIGRATION_CHECKBOX_PASSWORDS},
    {mojom::kImportHistory, IDS_MAHO_MIGRATION_CHECKBOX_HISTORY},
    {mojom::kImportCookies, IDS_MAHO_MIGRATION_CHECKBOX_COOKIES},
    {mojom::kImportAutofill, IDS_MAHO_MIGRATION_CHECKBOX_AUTOFILL},
    {mojom::kImportFavicons, IDS_MAHO_MIGRATION_CHECKBOX_FAVICONS},
    {mojom::kImportWorkspaces, IDS_MAHO_MIGRATION_CHECKBOX_WORKSPACES},
};

}  // namespace

// =============================================================================
// BrowserCard
// =============================================================================

BrowserCard::BrowserCard(size_t index,
                         const DetectedBrowser& browser,
                         SelectCallback on_select)
    : index_(index), on_select_(std::move(on_select)) {
  auto layout = std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal,
      gfx::Insets::VH(kCardPaddingVertical, kCardPaddingHorizontal),
      12);
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  SetLayoutManager(std::move(layout));

  // Favicon placeholder (32×32).
  auto favicon = std::make_unique<views::ImageView>();
  favicon->SetPreferredSize(gfx::Size(kCardFaviconSize, kCardFaviconSize));
  favicon_view_ = AddChildView(std::move(favicon));

  // Name + version column.
  auto text_column = std::make_unique<views::BoxLayoutView>();
  text_column->SetOrientation(views::BoxLayout::Orientation::kVertical);
  text_column->SetBetweenChildSpacing(2);

  auto name = std::make_unique<views::Label>(
      base::UTF8ToUTF16(browser.display_name));
  name->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  name->SetFontList(views::Label::GetDefaultFontList().Derive(
      1, gfx::Font::FontStyle::NORMAL, gfx::Font::Weight::SEMIBOLD));
  name_label_ = text_column->AddChildView(std::move(name));

  auto version = std::make_unique<views::Label>(u"");
  version->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  version->SetEnabledColor(SkColorSetRGB(0x88, 0x88, 0x88));
  version_label_ = text_column->AddChildView(std::move(version));

  AddChildView(std::move(text_column));

  SetFocusBehavior(FocusBehavior::ALWAYS);
}

BrowserCard::~BrowserCard() = default;

void BrowserCard::SetSelected(bool selected) {
  if (selected_ == selected) return;
  selected_ = selected;
  SchedulePaint();
}

std::optional<size_t> BrowserCard::GetSelectedProfileIndex() const {
  if (profile_combobox_) {
    return profile_combobox_->GetSelectedIndex();
  }
  return std::nullopt;
}

gfx::Size BrowserCard::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  // Cards fill the width of the scroll area; height is content-based.
  int height = kCardPaddingVertical * 2 + kCardFaviconSize;
  return gfx::Size(0, height);
}

void BrowserCard::OnPaint(gfx::Canvas* canvas) {
  views::View::OnPaint(canvas);

  const gfx::Rect bounds = GetLocalBounds();

  if (hovered_ && !selected_) {
    cc::PaintFlags shadow_flags;
    shadow_flags.setColor(kCardHoverShadowColor);
    shadow_flags.setStyle(cc::PaintFlags::kFill_Style);
    shadow_flags.setAntiAlias(true);
    canvas->DrawRoundRect(bounds, kCardCornerRadius, shadow_flags);
  }

  cc::PaintFlags border_flags;
  border_flags.setStyle(cc::PaintFlags::kStroke_Style);
  border_flags.setStrokeWidth(kCardBorderWidth);
  border_flags.setAntiAlias(true);

  if (selected_) {
    border_flags.setColor(kMahoPrimaryColor);
  } else {
    border_flags.setColor(kCardBorderColor);
  }

  gfx::Rect inset_bounds = bounds;
  inset_bounds.Inset(kCardBorderWidth / 2);
  canvas->DrawRoundRect(inset_bounds, kCardCornerRadius, border_flags);
}

void BrowserCard::OnMouseEntered(const ui::MouseEvent& event) {
  hovered_ = true;
  SchedulePaint();
}

void BrowserCard::OnMouseExited(const ui::MouseEvent& event) {
  hovered_ = false;
  SchedulePaint();
}

bool BrowserCard::OnMousePressed(const ui::MouseEvent& event) {
  if (on_select_) {
    on_select_.Run(index_);
  }
  return true;
}

BEGIN_METADATA(BrowserCard)
END_METADATA

// =============================================================================
// MahoMigrationDialogView
// =============================================================================

MahoMigrationDialogView::MahoMigrationDialogView(
    Profile* profile,
    const std::vector<DetectedBrowser>& browsers,
    CloseCallback callback)
    : profile_(profile), browsers_(browsers), callback_(std::move(callback)) {
  SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  InitLayout();
}

MahoMigrationDialogView::~MahoMigrationDialogView() = default;

// static
void MahoMigrationDialogView::Show(
    views::Widget* parent,
    Profile* profile,
    const std::vector<DetectedBrowser>& browsers,
    CloseCallback callback) {
  auto dialog = std::make_unique<MahoMigrationDialogView>(
      profile, browsers, std::move(callback));
  gfx::NativeWindow parent_window =
      parent ? parent->GetNativeWindow() : gfx::NativeWindow();
  views::Widget* widget = views::DialogDelegate::CreateDialogWidget(
      std::move(dialog), parent_window, gfx::NativeView());
  if (widget) {
    widget->Show();
  }
}

bool MahoMigrationDialogView::ShouldShowCloseButton() const {
  return state_ != DialogState::kImporting;
}

std::u16string MahoMigrationDialogView::GetWindowTitle() const {
  return GetMahoLocalizedString(IDS_MAHO_MIGRATION_TITLE);
}

void MahoMigrationDialogView::WindowClosing() {
  // X-close during import routes through the cancel path (Row 9).
  if (state_ == DialogState::kImporting) {
    // Already handled by OnCancelClicked triggering close.
  }
  if (callback_) {
    std::move(callback_).Run(was_cancelled_, current_import_bitmask_);
  }
}

void MahoMigrationDialogView::InitLayout() {
  auto contents = std::make_unique<views::BoxLayoutView>();
  contents->SetOrientation(views::BoxLayout::Orientation::kVertical);
  contents->SetInsideBorderInsets(gfx::Insets(24));
  contents->SetBetweenChildSpacing(18);

  contents->SetBackground(
      views::CreateRoundedRectBackground(ui::kColorDialogBackground, 16));

  // 1. Section label.
  auto* select_label = contents->AddChildView(std::make_unique<views::Label>(
      GetMahoLocalizedString(IDS_MAHO_MIGRATION_BROWSER_SELECTION)));
  select_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  select_label->SetFontList(views::Label::GetDefaultFontList().Derive(
      1, gfx::Font::FontStyle::NORMAL, gfx::Font::Weight::SEMIBOLD));

  // 2. Browser card list inside ScrollView.
  auto scroll_view = std::make_unique<views::ScrollView>();
  scroll_view->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  scroll_view->SetVerticalScrollBarMode(
      views::ScrollView::ScrollBarMode::kHiddenButEnabled);

  auto cards_list = std::make_unique<views::BoxLayoutView>();
  cards_list->SetOrientation(views::BoxLayout::Orientation::kVertical);
  cards_list->SetBetweenChildSpacing(kCardSpacing);

  for (size_t i = 0; i < browsers_.size(); ++i) {
    auto card = std::make_unique<BrowserCard>(
        i, browsers_[i],
        base::BindRepeating(
            &MahoMigrationDialogView::OnBrowserCardSelected,
            base::Unretained(this)));
    browser_cards_.push_back(card.get());
    cards_list->AddChildView(std::move(card));
  }

  cards_container_ = scroll_view->SetContents(std::move(cards_list));
  browser_scroll_view_ = contents->AddChildView(std::move(scroll_view));

  // Constrain scroll view height to allow scrolling.
  browser_scroll_view_->SetPreferredSize(gfx::Size(0, 180));

  // 3. Checkboxes container (rebuilt when selection changes).
  checkboxes_container_ =
      contents->AddChildView(std::make_unique<views::BoxLayoutView>());
  checkboxes_container_->SetOrientation(
      views::BoxLayout::Orientation::kVertical);
  checkboxes_container_->SetBetweenChildSpacing(8);

  // 4. Progress container (hidden by default).
  progress_container_ =
      contents->AddChildView(std::make_unique<views::BoxLayoutView>());
  progress_container_->SetOrientation(
      views::BoxLayout::Orientation::kVertical);
  progress_container_->SetBetweenChildSpacing(12);
  progress_container_->SetVisible(false);

  // 5. Status label (for state feedback).
  status_label_ = contents->AddChildView(std::make_unique<views::Label>(u""));
  status_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  status_label_->SetVisible(false);

  // 6. Actions row.
  auto* actions_row =
      contents->AddChildView(std::make_unique<views::BoxLayoutView>());
  actions_row->SetOrientation(views::BoxLayout::Orientation::kHorizontal);
  actions_row->SetBetweenChildSpacing(12);
  actions_row->SetMainAxisAlignment(views::BoxLayout::MainAxisAlignment::kEnd);

  cancel_button_ = actions_row->AddChildView(
      std::make_unique<views::MdTextButton>(
          base::BindRepeating(
              &MahoMigrationDialogView::OnCancelClicked,
              base::Unretained(this)),
          GetMahoLocalizedString(IDS_MAHO_MIGRATION_CANCEL)));

  import_button_ = actions_row->AddChildView(
      std::make_unique<views::MdTextButton>(
          base::BindRepeating(
              &MahoMigrationDialogView::OnImportClicked,
              base::Unretained(this)),
          GetMahoLocalizedString(IDS_MAHO_MIGRATION_IMPORT)));
  import_button_->SetStyle(ui::ButtonStyle::kProminent);
  import_button_->SetEnabled(false);

  contents->SetPreferredSize(gfx::Size(kDialogWidth, kDialogHeight));

  SetContentsView(std::move(contents));
}

void MahoMigrationDialogView::OnBrowserCardSelected(size_t index) {
  if (state_ != DialogState::kIdle) return;
  if (index >= browsers_.size()) return;

  // Deselect previous.
  if (selected_browser_index_.has_value()) {
    size_t prev = selected_browser_index_.value();
    if (prev < browser_cards_.size()) {
      browser_cards_[prev]->SetSelected(false);
    }
  }

  // Select new.
  selected_browser_index_ = index;
  browser_cards_[index]->SetSelected(true);
  import_button_->SetEnabled(true);

  // Rebuild checkboxes based on selected browser capabilities (W5-T4).
  RebuildCheckboxes();
}

void MahoMigrationDialogView::RebuildCheckboxes() {
  checkboxes_.clear();
  checkboxes_container_->RemoveAllChildViews();

  if (!selected_browser_index_.has_value()) return;

  const auto& browser = browsers_[selected_browser_index_.value()];

  for (const auto& info : kDataTypeInfos) {
    // W5-T4: Hide "Spaces & Tabs" for browsers that don't support workspaces.
    if (info.flag == mojom::kImportWorkspaces) {
      if (!(browser.services_supported & mojom::kImportWorkspaces)) {
        continue;  // Don't add the row at all.
      }
    }

    bool supported = (browser.services_supported & info.flag) != 0;
    auto checkbox = std::make_unique<views::Checkbox>(
        GetMahoLocalizedString(info.string_id));
    checkbox->SetChecked(supported);
    checkbox->SetEnabled(supported);
    checkboxes_.push_back(
        {info.flag, checkboxes_container_->AddChildView(std::move(checkbox))});
  }

  checkboxes_container_->InvalidateLayout();
}

void MahoMigrationDialogView::OnImportClicked() {
  if (state_ != DialogState::kIdle) return;
  if (!selected_browser_index_.has_value()) return;

  const auto& browser = browsers_[selected_browser_index_.value()];
  uint32_t mask = 0;
  for (const auto& row : checkboxes_) {
    if (row.checkbox->GetChecked() && row.checkbox->GetEnabled()) {
      mask |= row.flag;
    }
  }

  if (mask == 0) return;

  // Safari passwords must come from a CSV the user exports manually
  // (Passwords app → Export). Collect that file before starting; the path is
  // forwarded to the orchestrator in FileSelected().
  if (browser.type == maho::BrowserType::kSafari &&
      (mask & mojom::kImportPasswords)) {
    pending_import_bitmask_ = mask;
    import_button_->SetEnabled(false);  // Guard against re-entry.

    const std::string locale = g_browser_process->GetApplicationLocale();
    const bool is_ko = base::StartsWith(
        locale, "ko", base::CompareCase::INSENSITIVE_ASCII);
    const std::u16string title =
        is_ko ? u"Safari에서 내보낸 비밀번호 CSV 선택"
              : u"Choose the passwords CSV exported from Safari";

    select_file_dialog_ = ui::SelectFileDialog::Create(this, nullptr);
    ui::SelectFileDialog::FileTypeInfo file_type_info;
    file_type_info.extensions.push_back({FILE_PATH_LITERAL("csv")});
    file_type_info.extension_description_overrides.push_back(
        is_ko ? u"CSV 파일 (.csv)" : u"CSV file (.csv)");

    select_file_dialog_->SelectFile(
        ui::SelectFileDialog::SELECT_OPEN_FILE, title, base::FilePath(),
        &file_type_info, 1, FILE_PATH_LITERAL("csv"),
        GetWidget() ? GetWidget()->GetNativeWindow() : gfx::NativeWindow());
    return;
  }

  StartImport(mask);
}

void MahoMigrationDialogView::FileSelected(const ui::SelectedFileInfo& file,
                                           int index) {
  select_file_dialog_.reset();
  const uint32_t mask = pending_import_bitmask_;
  pending_import_bitmask_ = 0;
  if (mask == 0 || state_ != DialogState::kIdle) {
    return;
  }

  // Forward the CSV path to the orchestrator (consumed by the next session
  // start). Never log the path or its contents.
  const std::string path = file.path().AsUTF8Unsafe();
  last_password_csv_path_for_testing_ = path;
  maho_import_orchestrator_set_password_csv(path.c_str());

  StartImport(mask);
}

void MahoMigrationDialogView::FileSelectionCanceled() {
  select_file_dialog_.reset();
  uint32_t mask = pending_import_bitmask_;
  pending_import_bitmask_ = 0;

  // User cancelled the CSV picker: skip passwords but still import any other
  // selected data types.
  mask &= ~mojom::kImportPasswords;
  if (mask == 0) {
    // Nothing else was selected — stay idle so the user can retry.
    import_button_->SetEnabled(selected_browser_index_.has_value());
    return;
  }

  StartImport(mask);
}

void MahoMigrationDialogView::StartImport(uint32_t mask) {
  if (!selected_browser_index_.has_value()) return;

  const auto& browser = browsers_[selected_browser_index_.value()];

  current_import_bitmask_ = mask;
  was_cancelled_ = false;
  TransitionTo(DialogState::kImporting);

  // Disable cards during import.
  for (const auto& card : browser_cards_) {
    card->SetEnabled(false);
  }
  for (auto& row : checkboxes_) {
    row.checkbox->SetEnabled(false);
  }

  // Setup progress rows.
  checkboxes_container_->SetVisible(false);
  progress_rows_.clear();
  type_completed_.clear();
  type_succeeded_.clear();
  progress_container_->RemoveAllChildViews();

  for (const auto& info : kDataTypeInfos) {
    if (!(mask & info.flag)) continue;

    type_completed_[info.flag] = false;
    type_succeeded_[info.flag] = false;

    auto row = std::make_unique<views::BoxLayoutView>();
    row->SetOrientation(views::BoxLayout::Orientation::kHorizontal);
    row->SetBetweenChildSpacing(8);
    row->SetCrossAxisAlignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);

    // Icon placeholder (spinner initially).
    auto icon = std::make_unique<views::ImageView>();
    icon->SetPreferredSize(gfx::Size(16, 16));
    auto* icon_ptr = row->AddChildView(std::move(icon));

    // Type label.
    auto type_label = std::make_unique<views::Label>(
        GetMahoLocalizedString(info.string_id));
    type_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    auto* type_label_ptr = row->AddChildView(std::move(type_label));

    // Count/status label.
    auto count_label = std::make_unique<views::Label>(
        GetMahoLocalizedString(IDS_MAHO_MIGRATION_STATUS_IMPORTING));
    count_label->SetHorizontalAlignment(gfx::ALIGN_RIGHT);
    count_label->SetEnabledColor(SkColorSetRGB(0x88, 0x88, 0x88));
    auto* count_label_ptr = row->AddChildView(std::move(count_label));

    progress_container_->AddChildView(std::move(row));
    progress_rows_[info.flag] = {icon_ptr, type_label_ptr, count_label_ptr};
  }

  progress_container_->SetVisible(true);
  import_button_->SetEnabled(false);

  // Serialize DetectedBrowser to JSON for the Rust FFI orchestrator.
  const char* type_str = nullptr;
  switch (browser.type) {
    case maho::BrowserType::kChrome:  type_str = "Chrome"; break;
    case maho::BrowserType::kArc:     type_str = "Arc"; break;
    case maho::BrowserType::kBrave:   type_str = "Brave"; break;
    case maho::BrowserType::kEdge:    type_str = "Edge"; break;
    case maho::BrowserType::kVivaldi: type_str = "Vivaldi"; break;
    case maho::BrowserType::kOpera:   type_str = "Opera"; break;
    case maho::BrowserType::kFirefox: type_str = "Firefox"; break;
    case maho::BrowserType::kZen:     type_str = "Zen"; break;
    case maho::BrowserType::kSafari:  type_str = "Safari"; break;
  }

  base::DictValue browser_dict;
  browser_dict.Set("browser_type", type_str);
  browser_dict.Set("display_name", browser.display_name);
  browser_dict.Set("profile_path", browser.profile_path.AsUTF8Unsafe());
  browser_dict.Set("services_supported",
                   static_cast<int>(browser.services_supported));
  browser_dict.Set("requires_full_disk_access",
                   browser.requires_full_disk_access);

  std::string browser_json;
  base::JSONWriter::Write(browser_dict, &browser_json);

  MahoCore* core = maho::GetCore();
  if (!core) {
    LOG(ERROR) << "MahoMigrationDialogView: MahoCore not initialized";
    OnImportProgress(0, 0, "MahoCore not initialized", true);
    return;
  }

  import_session_bridge_ = std::make_unique<MahoImportSessionBridge>(
      base::BindRepeating(&MahoMigrationDialogView::OnImportProgress,
                          weak_factory_.GetWeakPtr()),
      base::SequencedTaskRunner::GetCurrentDefault(),
      profile_);

  if (!import_session_bridge_->Start(core, browser_json, mask, "")) {
    import_session_bridge_.reset();
    OnImportProgress(0, 0, "Failed to start import session", true);
  }
}

void MahoMigrationDialogView::OnCancelClicked() {
  switch (state_) {
    case DialogState::kImporting: {
      TransitionTo(DialogState::kCancelling);
      status_label_->SetText(
          GetMahoLocalizedString(IDS_MAHO_MIGRATION_STATUS_CANCELLING));
      status_label_->SetVisible(true);

      if (import_session_bridge_) {
        import_session_bridge_->Cancel();
      }

      cancel_timeout_timer_.Start(
          FROM_HERE, base::Milliseconds(kCancelTimeoutMs),
          base::BindOnce(&MahoMigrationDialogView::OnCancelTimeout,
                         weak_factory_.GetWeakPtr()));
      break;
    }
    case DialogState::kCancelling:
      // Already cancelling — ignore double-clicks.
      break;
    case DialogState::kPartialFail:
    case DialogState::kFullFail:
    case DialogState::kIdle:
    case DialogState::kSuccess:
      was_cancelled_ = true;
      GetWidget()->Close();
      break;
  }
}

void MahoMigrationDialogView::OnRetryClicked() {
  // Row 8: Retry after full failure — reset and re-import.
  TransitionTo(DialogState::kIdle);
  status_label_->SetVisible(false);
  OnImportClicked();
}

void MahoMigrationDialogView::OnContinueClicked() {
  // Row 7: Continue after partial failure — close with success signal.
  was_cancelled_ = false;
  GetWidget()->Close();
}

void MahoMigrationDialogView::OnCancelTimeout() {
  // Orchestrator didn't confirm cancellation in time — force close.
  was_cancelled_ = true;
  GetWidget()->Close();
}

void MahoMigrationDialogView::OnAutoCloseAfterSuccess() {
  was_cancelled_ = false;
  GetWidget()->Close();
}

void MahoMigrationDialogView::OnImportProgress(
    uint32_t type,
    int32_t items_imported,
    const std::string& error,
    bool complete) {
  // Handle cancellation acknowledgment from orchestrator.
  if (state_ == DialogState::kCancelling) {
    if (complete && type == 0 && error == "Cancelled") {
      cancel_timeout_timer_.Stop();
      was_cancelled_ = true;
      GetWidget()->Close();
      return;
    }
  }

  if (type == 0 && complete) {
    // Final overall completion signal — evaluate results.
    cancel_timeout_timer_.Stop();

    // Check per-type results.
    bool any_succeeded = false;
    bool any_failed = false;
    for (const auto& [flag, succeeded] : type_succeeded_) {
      if (succeeded) {
        any_succeeded = true;
      } else if (type_completed_[flag]) {
        any_failed = true;
      }
    }

    if (any_failed && !any_succeeded) {
      // Row 8: Full failure.
      TransitionTo(DialogState::kFullFail);
    } else if (any_failed && any_succeeded) {
      // Row 7: Partial failure.
      TransitionTo(DialogState::kPartialFail);
    } else {
      // Row 6: Full success → auto-close in 1s.
      TransitionTo(DialogState::kSuccess);
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE,
          base::BindOnce(&MahoMigrationDialogView::OnAutoCloseAfterSuccess,
                         weak_factory_.GetWeakPtr()),
          base::Milliseconds(1000));
    }
    return;
  }

  // Per-type progress update.
  if (complete && type != 0) {
    type_completed_[type] = true;
    type_succeeded_[type] = error.empty();
  }

  auto it = progress_rows_.find(type);
  if (it == progress_rows_.end()) return;

  auto& row = it->second;
  if (complete) {
    if (error.empty()) {
      row.count_label->SetText(
          GetMahoLocalizedString(IDS_MAHO_MIGRATION_STATUS_SUCCESS));
      row.count_label->SetEnabledColor(SkColorSetRGB(0x22, 0x8b, 0x22));
    } else {
      row.count_label->SetText(
          GetMahoLocalizedString(IDS_MAHO_MIGRATION_STATUS_ERROR));
      row.count_label->SetEnabledColor(SkColorSetRGB(0xdc, 0x26, 0x26));
    }
  } else if (items_imported > 0) {
    // Update count while in progress.
    row.count_label->SetText(base::UTF8ToUTF16(
        std::to_string(items_imported) + " items…"));
  }
}

void MahoMigrationDialogView::TransitionTo(DialogState new_state) {
  state_ = new_state;
  UpdateButtonsForState();
}

void MahoMigrationDialogView::UpdateButtonsForState() {
  switch (state_) {
    case DialogState::kIdle:
      import_button_->SetText(
          GetMahoLocalizedString(IDS_MAHO_MIGRATION_IMPORT));
      import_button_->SetCallback(base::BindRepeating(
          &MahoMigrationDialogView::OnImportClicked,
          base::Unretained(this)));
      import_button_->SetStyle(ui::ButtonStyle::kProminent);
      import_button_->SetEnabled(selected_browser_index_.has_value());
      import_button_->SetVisible(true);
      cancel_button_->SetText(
          GetMahoLocalizedString(IDS_MAHO_MIGRATION_CANCEL));
      cancel_button_->SetCallback(base::BindRepeating(
          &MahoMigrationDialogView::OnCancelClicked,
          base::Unretained(this)));
      cancel_button_->SetVisible(true);
      status_label_->SetVisible(false);
      break;

    case DialogState::kImporting:
      import_button_->SetEnabled(false);
      cancel_button_->SetEnabled(true);
      status_label_->SetText(
          GetMahoLocalizedString(IDS_MAHO_MIGRATION_STATUS_IMPORTING));
      status_label_->SetVisible(true);
      break;

    case DialogState::kCancelling:
      import_button_->SetEnabled(false);
      cancel_button_->SetEnabled(false);
      status_label_->SetText(
          GetMahoLocalizedString(IDS_MAHO_MIGRATION_STATUS_CANCELLING));
      status_label_->SetVisible(true);
      break;

    case DialogState::kPartialFail:
      // Replace Import button with "Continue" (primary).
      import_button_->SetText(
          GetMahoLocalizedString(IDS_MAHO_MIGRATION_CONTINUE));
      import_button_->SetCallback(base::BindRepeating(
          &MahoMigrationDialogView::OnContinueClicked,
          base::Unretained(this)));
      import_button_->SetStyle(ui::ButtonStyle::kProminent);
      import_button_->SetEnabled(true);
      import_button_->SetVisible(true);
      cancel_button_->SetVisible(true);
      cancel_button_->SetEnabled(true);
      status_label_->SetText(
          GetMahoLocalizedString(IDS_MAHO_MIGRATION_STATUS_PARTIAL_FAIL));
      status_label_->SetVisible(true);
      break;

    case DialogState::kFullFail:
      // Replace buttons with "Retry" (primary) + "Cancel" (secondary).
      import_button_->SetText(
          GetMahoLocalizedString(IDS_MAHO_MIGRATION_RETRY));
      import_button_->SetCallback(base::BindRepeating(
          &MahoMigrationDialogView::OnRetryClicked,
          base::Unretained(this)));
      import_button_->SetStyle(ui::ButtonStyle::kProminent);
      import_button_->SetEnabled(true);
      import_button_->SetVisible(true);
      cancel_button_->SetText(
          GetMahoLocalizedString(IDS_MAHO_MIGRATION_CANCEL));
      cancel_button_->SetCallback(base::BindRepeating(
          &MahoMigrationDialogView::OnCancelClicked,
          base::Unretained(this)));
      cancel_button_->SetEnabled(true);
      cancel_button_->SetVisible(true);
      status_label_->SetText(
          GetMahoLocalizedString(IDS_MAHO_MIGRATION_STATUS_ERROR));
      status_label_->SetVisible(true);
      break;

    case DialogState::kSuccess:
      import_button_->SetVisible(false);
      cancel_button_->SetText(
          GetMahoLocalizedString(IDS_MAHO_MIGRATION_CLOSE));
      cancel_button_->SetEnabled(true);
      status_label_->SetText(
          GetMahoLocalizedString(IDS_MAHO_MIGRATION_STATUS_SUCCESS));
      status_label_->SetVisible(true);
      break;
  }
}

bool MahoMigrationDialogView::import_button_enabled_for_testing() const {
  return import_button_ && import_button_->GetEnabled();
}

bool MahoMigrationDialogView::import_button_visible_for_testing() const {
  return import_button_ && import_button_->GetVisible();
}

bool MahoMigrationDialogView::cancel_button_visible_for_testing() const {
  return cancel_button_ && cancel_button_->GetVisible();
}

std::u16string MahoMigrationDialogView::status_label_text_for_testing() const {
  return status_label_ ? std::u16string(status_label_->GetText()) : u"";
}

std::u16string MahoMigrationDialogView::import_button_text_for_testing() const {
  return import_button_ ? std::u16string(import_button_->GetText()) : u"";
}

}  // namespace maho
