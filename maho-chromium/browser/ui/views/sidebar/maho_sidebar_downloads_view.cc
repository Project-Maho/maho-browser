// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_downloads_view.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/byte_size.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/task/thread_pool.h"
#include "url/gurl.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "base/i18n/case_conversion.h"
#include "chrome/browser/platform_util.h"
#include "chrome/browser/ui/browser.h"
#include "components/vector_icons/vector_icons.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/maho_private_context_policy.h"
#include "maho/browser/ui/downloads/maho_download_bridge_service.h"
#include "maho/browser/ui/downloads/maho_download_bridge_service_factory.h"
#include "maho/browser/ui/theme/maho_color_id.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_downloads_data.h"
#include "maho/browser/ui/views/maho_lucide_icons/vector_icons.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_download_visual_loader.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_library_tile_helpers.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/dragdrop/mojom/drag_drop_types.mojom.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/views/drag_controller.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/base/text/bytes_formatting.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/font.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/text_constants.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/image_button_factory.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/style/typography.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/view_utils.h"
#include "third_party/skia/include/core/SkColor.h"

namespace maho {

namespace {

constexpr int kPaneSpacingDp = 0;
constexpr int kPaneHorizontalInsetDp = 0;
constexpr int kHeaderHorizontalInsetDp = 16;
constexpr int kHeaderTopInsetDp = 16;
constexpr int kHeaderBottomInsetDp = 12;
constexpr int kHeaderSearchHeightDp = 36;
constexpr int kHeaderSearchCornerRadiusDp = 8;
constexpr int kHeaderSearchHorizontalInsetDp = 12;
constexpr int kHeaderSearchIconSpacingDp = 8;
constexpr int kHeaderSearchIconSizeDp = 14;
constexpr int kRowCornerRadiusDp = 10;
constexpr int kRowSpacingDp = 2;
constexpr int kRowHorizontalSpacingDp = 8;
constexpr int kRowVerticalPaddingDp = 6;
constexpr int kRowHorizontalPaddingDp = 8;
constexpr int kBadgeSizeDp = 28;
constexpr int kActionSpacingDp = 6;
constexpr int kMetaSpacingDp = 3;
constexpr int kProgressBarHeightDp = 3;
constexpr int kProgressBarWidthDp = 48;
constexpr int kProgressBarCornerRadiusDp = 2;
constexpr int kSectionHeaderTopInsetDp = 12;
constexpr int kSectionHeaderBottomInsetDp = 6;
constexpr int kSectionHeaderHorizontalInsetDp = 6;
constexpr int kSectionSpacingDp = 0;
constexpr int kEmptyStateTextMaxWidthDp = 188;
constexpr int kEmptyStateHorizontalPaddingDp = 24;
constexpr int kEmptyStateItemSpacingDp =
    sidebar_layout::kArchiveEmptyStateItemSpacingDp;
constexpr int kListBottomInsetDp = 10;

struct DownloadActionSpec {
  std::u16string label;
  std::u16string accessible_name;
  DownloadActionKind kind;
};

struct DownloadSection {
  std::u16string title;
  int sort_key = 0;
  std::vector<DownloadItem> items;
};

std::u16string FormatByteCount(uint64_t bytes) {
  return ui::FormatBytes(base::ByteSize(bytes));
}

bool EndsWithAny(std::string_view str, std::initializer_list<std::string_view> suffixes) {
  for (const auto& suffix : suffixes) {
    if (base::EndsWith(str, suffix, base::CompareCase::INSENSITIVE_ASCII)) {
      return true;
    }
  }
  return false;
}

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

bool IsInProgressState(const std::string& state) {
  return state == "downloading" || state == "paused";
}

bool HasProgressBar(const DownloadItem& item) {
  return IsInProgressState(item.state) && item.total_bytes > 0;
}

double ProgressFraction(const DownloadItem& item) {
  if (item.total_bytes == 0) {
    return 0.0;
  }
  return std::clamp(static_cast<double>(item.received_bytes) /
                        static_cast<double>(item.total_bytes),
                    0.0, 1.0);
}

base::Time ParseDownloadTimeString(const std::string& value) {
  if (value.empty()) {
    return base::Time();
  }

  base::Time parsed;
  if (base::Time::FromString(value.c_str(), &parsed) ||
      base::Time::FromUTCString(value.c_str(), &parsed)) {
    return parsed;
  }

  return base::Time();
}

base::Time TimestampForDownload(const DownloadItem& item) {
  if (item.completed_at.has_value()) {
    base::Time completed = ParseDownloadTimeString(*item.completed_at);
    if (!completed.is_null()) {
      return completed;
    }
  }

  return ParseDownloadTimeString(item.started_at);
}

int DayOffset(base::Time timestamp) {
  if (timestamp.is_null()) {
    return -1;
  }

  const base::Time today_start = base::Time::Now().LocalMidnight();
  const base::Time item_day = timestamp.LocalMidnight();
  return std::max(0, (today_start - item_day).InDays());
}

std::pair<std::u16string, int> SectionMetadataForDownload(const DownloadItem& item) {
  const int day_offset = DayOffset(TimestampForDownload(item));
  if (day_offset == 0) {
    return {u"Today", 0};
  }
  if (day_offset == 1) {
    return {u"Yesterday", 1};
  }
  if (day_offset >= 2 && day_offset <= 7) {
    return {u"Last Week", 2};
  }
  if (day_offset > 7) {
    return {u"Older", 3};
  }
  if (IsInProgressState(item.state)) {
    return {u"In Progress", -1};
  }
  return {u"Older", 3};
}

std::vector<DownloadSection> BuildDownloadSections(std::vector<DownloadItem> downloads) {
  std::sort(downloads.begin(), downloads.end(),
            [](const DownloadItem& lhs, const DownloadItem& rhs) {
              const int lhs_sort_key = SectionMetadataForDownload(lhs).second;
              const int rhs_sort_key = SectionMetadataForDownload(rhs).second;
              if (lhs_sort_key != rhs_sort_key) {
                return lhs_sort_key < rhs_sort_key;
              }

              const base::Time lhs_time = TimestampForDownload(lhs);
              const base::Time rhs_time = TimestampForDownload(rhs);
              if (lhs_time != rhs_time) {
                return lhs_time > rhs_time;
              }

              if (lhs.state != rhs.state) {
                return lhs.state < rhs.state;
              }

              return lhs.filename < rhs.filename;
            });

  std::vector<DownloadSection> sections;
  for (auto& item : downloads) {
    const auto [title, sort_key] = SectionMetadataForDownload(item);
    auto it = std::find_if(sections.begin(), sections.end(),
                           [&](const DownloadSection& section) {
                             return section.sort_key == sort_key &&
                                    section.title == title;
                           });
    if (it == sections.end()) {
      DownloadSection section;
      section.title = title;
      section.sort_key = sort_key;
      section.items.push_back(std::move(item));
      sections.push_back(std::move(section));
      continue;
    }
    it->items.push_back(std::move(item));
  }

  return sections;
}

std::u16string DownloadCountText(size_t count) {
  return base::UTF8ToUTF16(std::to_string(count)) +
         (count == 1 ? u" download" : u" downloads");
}

std::u16string MonthDayText(base::Time timestamp) {
  static constexpr std::array<std::u16string_view, 12> kMonths = {
      u"Jan", u"Feb", u"Mar", u"Apr", u"May", u"Jun",
      u"Jul", u"Aug", u"Sep", u"Oct", u"Nov", u"Dec",
  };

  base::Time::Exploded exploded;
  timestamp.LocalExplode(&exploded);
  if (exploded.month < 1 || exploded.month > 12) {
    return std::u16string();
  }

  return base::StrCat(
      {std::u16string(kMonths[exploded.month - 1]), u" ",
       base::UTF8ToUTF16(std::to_string(exploded.day_of_month))});
}

std::u16string TimeOfDayText(base::Time timestamp) {
  base::Time::Exploded exploded;
  timestamp.LocalExplode(&exploded);

  int hour = exploded.hour;
  const bool is_pm = hour >= 12;
  if (hour == 0) {
    hour = 12;
  } else if (hour > 12) {
    hour -= 12;
  }

  std::string minute = exploded.minute < 10
                           ? "0" + std::to_string(exploded.minute)
                           : std::to_string(exploded.minute);
  return base::UTF8ToUTF16(std::to_string(hour) + ":" + minute +
                           (is_pm ? " PM" : " AM"));
}

std::u16string TimestampText(const DownloadItem& item) {
  const base::Time timestamp = TimestampForDownload(item);
  if (timestamp.is_null()) {
    return std::u16string();
  }

  const int day_offset = DayOffset(timestamp);
  if (day_offset <= 1) {
    return TimeOfDayText(timestamp);
  }

  return MonthDayText(timestamp);
}

class DownloadProgressBarView : public views::View {
  METADATA_HEADER(DownloadProgressBarView, views::View)

 public:
  DownloadProgressBarView(double progress, const MahoSidebarPalette& palette)
      : progress_(progress) {
    SetPreferredSize(gfx::Size(kProgressBarWidthDp, kProgressBarHeightDp));
    SetBackground(views::CreateRoundedRectBackground(
        palette.row_selected, kProgressBarCornerRadiusDp));
    fill_view_ = AddChildView(std::make_unique<views::View>());
    fill_view_->SetBackground(views::CreateRoundedRectBackground(
        palette.focus_ring, kProgressBarCornerRadiusDp));
    GetViewAccessibility().SetIsIgnored(true);
  }

  void Layout(PassKey) override {
    LayoutSuperclass<views::View>(this);
    const int fill_width = std::clamp(
        static_cast<int>(std::round(width() * progress_)), 0, width());
    fill_view_->SetBounds(0, 0, fill_width, height());
  }

 private:
  const double progress_ = 0.0;
  raw_ptr<views::View> fill_view_ = nullptr;
};

BEGIN_METADATA(DownloadProgressBarView)
END_METADATA

class DownloadSectionHeaderView : public views::View {
  METADATA_HEADER(DownloadSectionHeaderView, views::View)

 public:
  DownloadSectionHeaderView(const std::u16string& title,
                            size_t count,
                            const MahoSidebarPalette& palette) {
    GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
    GetViewAccessibility().SetName(title + u", " + DownloadCountText(count));

    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal,
        gfx::Insets::TLBR(kSectionHeaderTopInsetDp,
                          kSectionHeaderHorizontalInsetDp,
                          kSectionHeaderBottomInsetDp,
                          kSectionHeaderHorizontalInsetDp),
        6));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);

    auto* title_label =
        AddChildView(std::make_unique<views::Label>(title));
    title_label->SetSkipSubpixelRenderingOpacityCheck(true);
    title_label->SetTextStyle(views::style::STYLE_BODY_5_MEDIUM);
    title_label->SetEnabledColor(palette.primary_text);
    title_label->SetAutoColorReadabilityEnabled(false);
    title_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    title_label->GetViewAccessibility().SetIsIgnored(true);

    auto* count_label = AddChildView(
        std::make_unique<views::Label>(
            base::UTF8ToUTF16(std::to_string(count))));
    count_label->SetSkipSubpixelRenderingOpacityCheck(true);
    count_label->SetTextStyle(views::style::STYLE_BODY_5);
    count_label->SetEnabledColor(palette.primary_text);
    count_label->SetAutoColorReadabilityEnabled(false);
    count_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    count_label->GetViewAccessibility().SetIsIgnored(true);
  }
};

BEGIN_METADATA(DownloadSectionHeaderView)
END_METADATA

std::u16string GetDownloadFileType(const DownloadItem& item) {
  std::string mime = item.mime_type.value_or("");
  std::string filename = base::UTF16ToUTF8(item.filename);
  if (filename.empty() && item.file_path.has_value()) {
    filename = *item.file_path;
  }

  auto match_ext = [&](std::initializer_list<std::string_view> suffixes) {
    return EndsWithAny(filename, suffixes);
  };

  if (base::StartsWith(mime, "image/", base::CompareCase::SENSITIVE) ||
      match_ext({".jpg", ".jpeg", ".png", ".gif", ".webp", ".bmp", ".tiff", ".svg", ".ico"})) {
    return u"Image";
  }
  if (base::StartsWith(mime, "video/", base::CompareCase::SENSITIVE) ||
      match_ext({".mp4", ".m4v", ".mov", ".mkv", ".webm", ".avi", ".flv", ".wmv"})) {
    return u"Movie";
  }
  if (base::StartsWith(mime, "audio/", base::CompareCase::SENSITIVE) ||
      match_ext({".mp3", ".m4a", ".aac", ".flac", ".wav", ".ogg", ".wma"})) {
    return u"Audio";
  }
  if (mime == "application/pdf" || match_ext({".pdf"})) {
    return u"PDF";
  }
  if (mime == "application/x-apple-diskimage" || mime == "application/isoimage" ||
      mime == "application/x-iso9660-image" || match_ext({".dmg", ".iso", ".img"})) {
    return u"Disk Image";
  }
  if (mime == "application/zip" || mime == "application/x-zip-compressed" ||
      mime == "application/x-tar" || mime == "application/x-gzip" ||
      mime == "application/x-bzip2" || mime == "application/x-7z-compressed" ||
      mime == "application/x-rar-compressed" ||
      match_ext({".zip", ".tar", ".gz", ".tgz", ".bz2", ".7z", ".rar"})) {
    return u"Zip Archive";
  }
  if (mime == "application/msword" ||
      mime == "application/vnd.openxmlformats-officedocument.wordprocessingml.document" ||
      mime == "application/rtf" || mime == "text/plain" ||
      match_ext({".doc", ".docx", ".rtf", ".txt", ".pages"})) {
    return u"Document";
  }
  if (mime == "application/vnd.ms-excel" ||
      mime == "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet" ||
      match_ext({".xls", ".xlsx", ".csv", ".numbers"})) {
    return u"Spreadsheet";
  }
  if (mime == "application/vnd.ms-powerpoint" ||
      mime == "application/vnd.openxmlformats-officedocument.presentationml.presentation" ||
      match_ext({".ppt", ".pptx", ".key"})) {
    return u"Presentation";
  }

  if (!filename.empty()) {
    base::FilePath file_path = base::FilePath::FromUTF8Unsafe(filename);
    // FilePath::Extension() is StringType, which is wstring on Windows.
    std::string ext =
        base::FilePath(file_path.Extension()).AsUTF8Unsafe();
    if (!ext.empty() && ext.front() == '.') {
      ext.erase(0, 1);
    }
    ext = base::ToUpperASCII(ext);
    static constexpr std::string_view kSafeExts[] = {
        "7Z", "AVI", "CSS", "CSV", "DMG", "DOC", "DOCX", "EXE",
        "GIF", "GZ", "HTML", "ISO", "JPEG", "JPG", "JS", "JSON",
        "MKV", "MOV", "MP3", "MP4", "PDF", "PNG", "PPT", "PPTX",
        "RAR", "SVG", "TAR", "TXT", "WAV", "WEBP", "XLS", "XLSX",
        "XML", "ZIP"
    };
    if (std::binary_search(std::begin(kSafeExts), std::end(kSafeExts), ext)) {
      return base::UTF8ToUTF16(ext);
    }
  }

  return u"";
}

std::u16string GetDownloadStatusDescription(const DownloadItem& item) {
  std::u16string host;
  GURL url(item.url);
  if (url.is_valid() && !url.host().empty()) {
    host = base::UTF8ToUTF16(url.host());
  }

  if (item.state == "completed") {
    std::u16string type = GetDownloadFileType(item);
    if (!type.empty() && !host.empty()) {
      return type + u" from " + host;
    } else if (type.empty() && !host.empty()) {
      return u"Download from " + host;
    } else if (!type.empty() && host.empty()) {
      return type;
    } else {
      return u"Download";
    }
  }

  if (item.state == "downloading" || item.state == "paused") {
    std::u16string base_desc;
    if (item.total_bytes > 0) {
      base_desc = FormatByteCount(item.received_bytes) + u" of " + FormatByteCount(item.total_bytes);
      if (!host.empty()) {
        base_desc += u" from " + host;
      }
    } else {
      if (!host.empty()) {
        base_desc = u"Downloading from " + host;
      } else {
        base_desc = u"Downloading";
      }
    }

    if (item.state == "paused") {
      return u"Paused - " + base_desc;
    }
    return base_desc;
  }

  if (item.state == "failed" || item.state == "interrupted") {
    if (item.error.has_value() && !item.error->empty()) {
      return base::UTF8ToUTF16(*item.error);
    }
    return u"Failed";
  }

  if (item.state == "cancelled") {
    return u"Cancelled";
  }

  return StateLabel(item.state);
}

std::u16string DetailText(const DownloadItem& item) {
  return GetDownloadStatusDescription(item);
}

std::u16string DownloadSubtitleText(const DownloadItem& item) {
  const std::u16string detail = DetailText(item);
  const std::u16string timestamp = TimestampText(item);
  if (detail.empty()) {
    return timestamp;
  }
  if (timestamp.empty()) {
    return detail;
  }
  return base::StrCat({detail, u" · ", timestamp});
}

class DownloadsEmptyStateView : public views::View {
  METADATA_HEADER(DownloadsEmptyStateView, views::View)

 public:
  explicit DownloadsEmptyStateView(const MahoSidebarPalette& palette) {
    GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
    GetViewAccessibility().SetName(u"Downloads empty state");

    icon_ = AddChildView(std::make_unique<views::ImageView>());
    icon_->SetPreferredSize(gfx::Size(24, 24));
    icon_->GetViewAccessibility().SetIsIgnored(true);

    title_label_ = AddChildView(
        std::make_unique<views::Label>(u"No downloads yet"));
    title_label_->SetSkipSubpixelRenderingOpacityCheck(true);
    title_label_->SetTextStyle(views::style::STYLE_BODY_3_MEDIUM);
    title_label_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
    title_label_->SetAutoColorReadabilityEnabled(false);
    title_label_->GetViewAccessibility().SetIsIgnored(true);

    subtitle_label_ = AddChildView(std::make_unique<views::Label>(
        u"Files you download will show up here."));
    subtitle_label_->SetSkipSubpixelRenderingOpacityCheck(true);
    subtitle_label_->SetTextStyle(views::style::STYLE_BODY_4);
    subtitle_label_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
    subtitle_label_->SetAutoColorReadabilityEnabled(false);
    subtitle_label_->SetMultiLine(true);
    subtitle_label_->GetViewAccessibility().SetIsIgnored(true);

    SetPalette(palette);
  }

  void SetPalette(const MahoSidebarPalette& palette) {
    icon_->SetImage(ui::ImageModel::FromVectorIcon(
        maho_lucide_icons::kArrowDownToLineIcon, palette.secondary_text, 24));
    title_label_->SetEnabledColor(palette.primary_text);
    subtitle_label_->SetEnabledColor(palette.secondary_text);
  }

  void Layout(PassKey) override {
    LayoutSuperclass<views::View>(this);
    if (!icon_ || !title_label_ || !subtitle_label_) {
      return;
    }

    const gfx::Size icon_size = icon_->GetPreferredSize();
    const gfx::Size title_size = title_label_->GetPreferredSize();
    const int subtitle_width = std::max(
        0, std::min(kEmptyStateTextMaxWidthDp,
                    width() - (kEmptyStateHorizontalPaddingDp * 2)));
    const int subtitle_height =
        subtitle_width > 0 ? subtitle_label_->GetHeightForWidth(subtitle_width)
                           : subtitle_label_->GetPreferredSize().height();
    const int content_width = std::max(
        {icon_size.width(), title_size.width(), subtitle_width});
    const int content_height = icon_size.height() + kEmptyStateItemSpacingDp +
                               title_size.height() + kEmptyStateItemSpacingDp +
                               subtitle_height;
    const int content_top = std::max(0, (height() - content_height) / 2);
    const int content_left = (width() - content_width) / 2;

    icon_->SetBounds(content_left + ((content_width - icon_size.width()) / 2),
                     content_top, icon_size.width(), icon_size.height());
    title_label_->SetBounds(
        content_left + ((content_width - title_size.width()) / 2),
        icon_->bounds().bottom() + kEmptyStateItemSpacingDp, title_size.width(),
        title_size.height());
    subtitle_label_->SetBounds(
        content_left + ((content_width - subtitle_width) / 2),
        title_label_->bounds().bottom() + kEmptyStateItemSpacingDp,
        subtitle_width, subtitle_height);
  }

 private:
  raw_ptr<views::ImageView> icon_ = nullptr;
  raw_ptr<views::Label> title_label_ = nullptr;
  raw_ptr<views::Label> subtitle_label_ = nullptr;
};

BEGIN_METADATA(DownloadsEmptyStateView)
END_METADATA

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

const gfx::VectorIcon& GetIconForAction(DownloadActionKind kind) {
  switch (kind) {
    case DownloadActionKind::kOpen:
      return maho_lucide_icons::kArrowUpRightIcon;
    case DownloadActionKind::kReveal:
      return maho_lucide_icons::kFolderOpenIcon;
    case DownloadActionKind::kPause:
      return maho_lucide_icons::kPauseIcon;
    case DownloadActionKind::kResume:
      return maho_lucide_icons::kPlayIcon;
    case DownloadActionKind::kCancel:
      return maho_lucide_icons::kXIcon;
    case DownloadActionKind::kRemove:
      return maho_lucide_icons::kTrash2Icon;
  }
}

std::unique_ptr<views::ImageButton> CreateActionButton(
    const gfx::VectorIcon& icon,
    const std::u16string& tooltip,
    const std::u16string& accessible_name,
    base::RepeatingClosure callback,
    const MahoSidebarPalette& palette) {
  // CreateVectorImageButtonWithNativeTheme takes ui::ColorId (an int); the
  // palette SkColors must be bound per state instead.
  auto button = views::CreateVectorImageButton(std::move(callback));
  const ui::ImageModel normal =
      ui::ImageModel::FromVectorIcon(icon, palette.neutral_glyph, 14);
  button->SetImageModel(views::Button::STATE_NORMAL, normal);
  button->SetImageModel(views::Button::STATE_HOVERED, normal);
  button->SetImageModel(views::Button::STATE_PRESSED, normal);
  button->SetImageModel(
      views::Button::STATE_DISABLED,
      ui::ImageModel::FromVectorIcon(icon, palette.disabled_text, 14));
  button->SetBorder(nullptr);
  button->SetPreferredSize(gfx::Size(24, 24));
  button->SetImageHorizontalAlignment(views::ImageButton::ALIGN_CENTER);
  button->SetImageVerticalAlignment(views::ImageButton::ALIGN_MIDDLE);
  button->SetTooltipText(tooltip);
  button->SetAccessibleName(accessible_name);
  views::InstallCircleHighlightPathGenerator(button.get());
  return button;
}

class DownloadRowView : public views::View, public views::DragController {
  METADATA_HEADER(DownloadRowView, views::View)

 public:
  using ActionCallback =
      base::RepeatingCallback<void(const std::string&, DownloadActionKind)>;

  DownloadRowView(const DownloadItem& item,
                  std::vector<DownloadActionSpec> actions,
                  ActionCallback action_callback,
                  const MahoSidebarPalette& palette)
      : download_id_(item.id),
        action_callback_(std::move(action_callback)),
        palette_(palette) {
    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal,
        gfx::Insets::TLBR(kRowVerticalPaddingDp, kRowHorizontalPaddingDp,
                          kRowVerticalPaddingDp, kRowHorizontalPaddingDp),
        kRowHorizontalSpacingDp));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);

    SetFocusBehavior(FocusBehavior::ALWAYS);
    views::FocusRing::Install(this);
    views::InstallRoundRectHighlightPathGenerator(this, gfx::Insets(),
                                                  kRowCornerRadiusDp);

    badge_slot_ = AddChildView(std::make_unique<views::View>());
    badge_slot_->SetPreferredSize(gfx::Size(kBadgeSizeDp, kBadgeSizeDp));
    badge_slot_->SetLayoutManager(std::make_unique<views::FillLayout>());
    badge_slot_->GetViewAccessibility().SetIsIgnored(true);

    icon_view_ = badge_slot_->AddChildView(std::make_unique<views::ImageView>());
    icon_view_->SetImage(ui::ImageModel::FromVectorIcon(
        maho_lucide_icons::kFileIcon, palette_.primary_text, 16));
    icon_view_->GetViewAccessibility().SetIsIgnored(true);

    if (item.state == "completed" && item.file_path.has_value() && !item.file_path->empty()) {
      file_path_ = base::FilePath::FromUTF8Unsafe(*item.file_path);
      set_drag_controller(this);
      visual_loader_ = std::make_unique<MahoSidebarDownloadVisualLoader>();
      visual_loader_->Start(
          file_path_, 28,
          base::BindOnce(&DownloadRowView::OnThumbnailLoaded,
                         weak_factory_.GetWeakPtr()));
    }

    auto* text_block = AddChildView(std::make_unique<views::View>());
    auto* text_layout = text_block->SetLayoutManager(
        std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kVertical, gfx::Insets(),
            kMetaSpacingDp));
    text_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);
    layout->SetFlexForView(text_block, 1);

    auto* title = text_block->AddChildView(
        std::make_unique<views::Label>(item.filename));
    title->SetSkipSubpixelRenderingOpacityCheck(true);
    title->SetTextStyle(views::style::STYLE_BODY_4_MEDIUM);
    title->SetEnabledColor(palette_.primary_text);
    title->SetAutoColorReadabilityEnabled(false);
    title->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    title->SetMultiLine(false);
    title->SetElideBehavior(gfx::ELIDE_TAIL);

    auto* subtitle = text_block->AddChildView(
        std::make_unique<views::Label>(DownloadSubtitleText(item)));
    subtitle->SetSkipSubpixelRenderingOpacityCheck(true);
    subtitle->SetTextStyle(views::style::STYLE_BODY_5);
    subtitle->SetEnabledColor(palette_.secondary_text);
    subtitle->SetAutoColorReadabilityEnabled(false);
    subtitle->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    subtitle->SetMultiLine(false);
    subtitle->SetElideBehavior(gfx::ELIDE_TAIL);

    if (HasProgressBar(item)) {
      text_block->AddChildView(
        std::make_unique<DownloadProgressBarView>(ProgressFraction(item),
                                                  palette_));
    }

    if (!actions.empty()) {
      action_row_ = AddChildView(std::make_unique<views::View>());
      auto* action_layout = action_row_->SetLayoutManager(
          std::make_unique<views::BoxLayout>(
              views::BoxLayout::Orientation::kHorizontal, gfx::Insets(),
              kActionSpacingDp));
      action_layout->set_cross_axis_alignment(
          views::BoxLayout::CrossAxisAlignment::kCenter);
      action_layout->set_main_axis_alignment(
          views::BoxLayout::MainAxisAlignment::kEnd);

      for (const auto& action : actions) {
        auto* button = action_row_->AddChildView(
            CreateActionButton(
                GetIconForAction(action.kind),
                action.label,
                 action.accessible_name.empty() ? action.label : action.accessible_name,
                base::BindRepeating(&DownloadRowView::HandleActionPressed,
                                    weak_factory_.GetWeakPtr(), action.kind),
                palette_));
        action_buttons_.push_back(button);
      }
    }

    GetViewAccessibility().SetRole(ax::mojom::Role::kListItem);
    GetViewAccessibility().SetName(item.filename);
    UpdateAppearance();
  }

  void OnMouseEntered(const ui::MouseEvent& event) override {
    UpdateAppearance();
  }

  void OnMouseExited(const ui::MouseEvent& event) override {
    UpdateAppearance();
  }

  void OnFocus() override {
    views::View::OnFocus();
    UpdateAppearance();
  }

  void OnBlur() override {
    views::View::OnBlur();
    UpdateAppearance();
  }

  void OnThemeChanged() override {
    views::View::OnThemeChanged();
    UpdateAppearance();
  }

  bool OnMousePressed(const ui::MouseEvent& event) override {
    if (event.IsOnlyLeftMouseButton()) {
      drag_start_pos_ = event.location();
      return true;
    }
    return views::View::OnMousePressed(event);
  }

  void OnMouseReleased(const ui::MouseEvent& event) override {
    if (event.IsOnlyLeftMouseButton() && !file_path_.empty()) {
      const int drag_distance = (event.location() - drag_start_pos_).Length();
      if (drag_distance < 4) {
        HandleActionPressed(DownloadActionKind::kOpen);
      }
    }
    views::View::OnMouseReleased(event);
  }

  bool OnKeyPressed(const ui::KeyEvent& event) override {
    if (event.key_code() == ui::VKEY_RETURN || event.key_code() == ui::VKEY_SPACE) {
      if (!file_path_.empty()) {
        HandleActionPressed(DownloadActionKind::kOpen);
        return true;
      }
    }
    return views::View::OnKeyPressed(event);
  }

  // views::DragController:
  void WriteDragDataForView(views::View* sender,
                            const gfx::Point& press_pt,
                            ui::OSExchangeData* data) override {
    if (!file_path_.empty() && data) {
      data->SetFilename(file_path_);
    }
  }

  int GetDragOperationsForView(views::View* sender,
                               const gfx::Point& p) override {
    if (!file_path_.empty()) {
      return static_cast<int>(ui::mojom::DragOperation::kCopy);
    }
    return static_cast<int>(ui::mojom::DragOperation::kNone);
  }

  bool CanStartDragForView(views::View* sender,
                           const gfx::Point& press_pt,
                           const gfx::Point& p) override {
    return !file_path_.empty() && (press_pt - p).Length() >= 4;
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
  void OnThumbnailLoaded(const gfx::Image& image) {
    if (!image.IsEmpty()) {
      icon_view_->SetImage(ui::ImageModel::FromImageSkia(image.AsImageSkia()));
    }
  }

  void UpdateAppearance() {
    const bool active = HasFocus() || IsMouseHovered();
    if (HasFocus()) {
      SetBackground(views::CreateRoundedRectBackground(
          palette_.row_selected, kRowCornerRadiusDp));
      SetBorder(views::CreateRoundedRectBorder(
          1, kRowCornerRadiusDp, palette_.outline));
    } else if (IsMouseHovered()) {
      SetBackground(views::CreateRoundedRectBackground(
          palette_.row_hover, kRowCornerRadiusDp));
      SetBorder(views::CreateRoundedRectBorder(
          1, kRowCornerRadiusDp, palette_.outline));
    } else {
      SetBackground(nullptr);
      SetBorder(nullptr);
    }

    if (action_row_) {
      action_row_->SetVisible(active);
    }
    InvalidateLayout();
    SchedulePaint();
  }

  void HandleActionPressed(DownloadActionKind kind) {
    if (!action_callback_) {
      return;
    }
    action_callback_.Run(download_id_, kind);
  }

  std::string download_id_;
  base::FilePath file_path_;
  gfx::Point drag_start_pos_;
  ActionCallback action_callback_;
  raw_ptr<views::View> badge_slot_ = nullptr;
  raw_ptr<views::ImageView> icon_view_ = nullptr;
  raw_ptr<views::View> action_row_ = nullptr;
  std::vector<views::Button*> action_buttons_;
  std::unique_ptr<MahoSidebarDownloadVisualLoader> visual_loader_;
  MahoSidebarPalette palette_;
  base::WeakPtrFactory<DownloadRowView> weak_factory_{this};
};

BEGIN_METADATA(DownloadRowView)
END_METADATA

}  // namespace

BEGIN_METADATA(MahoSidebarDownloadsView)
END_METADATA

MahoSidebarDownloadsView::MahoSidebarDownloadsView(Browser* browser)
    : browser_(browser) {
  BuildUi();
  if (browser_ && browser_->GetProfile()) {
    auto* service = maho::MahoDownloadBridgeServiceFactory::GetForProfile(browser_->GetProfile());
    if (service) {
      service->AddObserver(this);
    }
  }
}

MahoSidebarDownloadsView::~MahoSidebarDownloadsView() {
  if (browser_ && browser_->GetProfile()) {
    auto* service = maho::MahoDownloadBridgeServiceFactory::GetForProfileIfExists(browser_->GetProfile());
    if (service) {
      service->RemoveObserver(this);
    }
  }
}

void MahoSidebarDownloadsView::SetSidebarPalette(
    const MahoSidebarPalette& palette) {
  palette_ = palette;
  ApplyPalette();
  ReloadDownloads();
}

void MahoSidebarDownloadsView::OnThemeChanged() {
  views::View::OnThemeChanged();
  ApplyPalette();
}

void MahoSidebarDownloadsView::OnMahoDownloadsChanged() {
  ReloadDownloads();
}

views::View* MahoSidebarDownloadsView::download_row_for_download_id_for_testing(
    const std::string& download_id) {
  if (!list_container_) {
    return nullptr;
  }

  std::vector<views::View*> stack;
  stack.push_back(list_container_);
  while (!stack.empty()) {
    views::View* current = stack.back();
    stack.pop_back();
    if (auto* row = views::AsViewClass<DownloadRowView>(current);
        row && row->MatchesDownloadId(download_id)) {
      return row;
    }
    for (const auto& child : current->children()) {
      stack.push_back(child.get());
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
  auto* root_layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets::TLBR(0, kPaneHorizontalInsetDp, 0, kPaneHorizontalInsetDp),
      kPaneSpacingDp));
  root_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
  GetViewAccessibility().SetName(u"Downloads panel");

  auto* header_strip = AddChildView(std::make_unique<views::View>());
  auto* header_layout = header_strip->SetLayoutManager(
      std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal,
          gfx::Insets::TLBR(kHeaderTopInsetDp, kHeaderHorizontalInsetDp,
                            kHeaderBottomInsetDp, kHeaderHorizontalInsetDp),
          8));
  header_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  search_shell_ =
      header_strip->AddChildView(std::make_unique<views::View>());
  search_shell_->SetPreferredSize(gfx::Size(0, kHeaderSearchHeightDp));
  auto* search_layout = search_shell_->SetLayoutManager(
      std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal,
          gfx::Insets::VH(0, kHeaderSearchHorizontalInsetDp),
          kHeaderSearchIconSpacingDp));
  search_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  search_icon_ =
      search_shell_->AddChildView(std::make_unique<views::ImageView>());
  search_icon_->GetViewAccessibility().SetIsIgnored(true);
  search_icon_->SetPreferredSize(
      gfx::Size(kHeaderSearchIconSizeDp, kHeaderSearchIconSizeDp));

  auto search_field = std::make_unique<views::Textfield>();
  search_field_ = search_shell_->AddChildView(std::move(search_field));
  search_field_->SetPlaceholderText(u"Search downloads...");
  search_field_->set_controller(this);
  search_field_->SetPreferredSize(gfx::Size(0, kHeaderSearchHeightDp));
  search_field_->SetBackgroundColor(SK_ColorTRANSPARENT);
  search_field_->SetBorder(nullptr);
  search_field_->SetAccessibleName(u"Search Downloads");
  search_layout->SetFlexForView(search_field_, 1);
  search_shell_->GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
  search_shell_->GetViewAccessibility().SetName(u"Search downloads header");
  header_layout->SetFlexForView(search_shell_, 1);

  auto* body_container = AddChildView(std::make_unique<views::View>());
  body_container->SetLayoutManager(std::make_unique<views::FillLayout>());
  root_layout->SetFlexForView(body_container, 1, true);

  scroll_view_ = body_container->AddChildView(std::make_unique<views::ScrollView>());
  scroll_view_->SetBackgroundColor(std::nullopt);
  scroll_view_->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  scroll_view_->SetVerticalScrollBarMode(
      views::ScrollView::ScrollBarMode::kHiddenButEnabled);
  scroll_view_->GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
  scroll_view_->GetViewAccessibility().SetName(u"Downloads results container");

  list_container_ = scroll_view_->SetContents(std::make_unique<views::View>());
  auto* list_layout = list_container_->SetLayoutManager(
      std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kVertical,
                                         gfx::Insets::TLBR(0, 10,
                                                           kListBottomInsetDp,
                                                           18),
                                         0));
  list_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  list_container_->GetViewAccessibility().SetRole(ax::mojom::Role::kList);
  list_container_->GetViewAccessibility().SetName(u"Downloads results");

  empty_state_view_ = body_container->AddChildView(
      std::make_unique<DownloadsEmptyStateView>(palette_));

  ApplyPalette();
  empty_state_view_->SetVisible(true);
  scroll_view_->SetVisible(false);
}

void MahoSidebarDownloadsView::ApplyPalette() {
  if (search_shell_) {
    search_shell_->SetBackground(views::CreateRoundedRectBackground(
        palette_.row_active, kHeaderSearchCornerRadiusDp));
    search_shell_->SetBorder(views::CreateRoundedRectBorder(
        1, kHeaderSearchCornerRadiusDp, palette_.outline));
  }
  if (search_icon_) {
    search_icon_->SetImage(ui::ImageModel::FromVectorIcon(
        vector_icons::kSearchIcon, palette_.secondary_text,
        kHeaderSearchIconSizeDp));
  }
  if (search_field_) {
    search_field_->SetMahoResolvedTextColor(palette_.primary_text);
    search_field_->SetMahoResolvedPlaceholderTextColor(palette_.secondary_text);
  }
  if (empty_state_view_) {
    static_cast<DownloadsEmptyStateView*>(empty_state_view_.get())
        ->SetPalette(palette_);
  }
}

void MahoSidebarDownloadsView::ReloadDownloads() {
  std::vector<DownloadItem> downloads;
  if (testing_downloads_) {
    downloads = *testing_downloads_;
  } else if (browser_ &&
             MahoIsCapabilityAllowed(
                 browser_->GetProfile(),
                 MahoPrivateCapability::kMahoDownloadMetadata)) {
    // Defense-in-depth: ParseDownloads() reads the process-global regular
    // MahoCore. The primary OTR boundary is the is_otr_ gate at the sidebar
    // library entry; this policy check fails closed so a future refactor that
    // reaches this view from a non-regular profile cannot leak regular
    // download metadata.
    downloads = ParseDownloads();
  }

  if (search_field_) {
    std::u16string query = base::i18n::FoldCase(search_field_->GetText());
    if (!query.empty()) {
      downloads.erase(
          std::remove_if(downloads.begin(), downloads.end(),
                         [&query](const DownloadItem& item) {
                           std::u16string name = base::i18n::FoldCase(item.filename);
                           std::u16string url = base::i18n::FoldCase(base::UTF8ToUTF16(item.url));
                           return name.find(query) == std::u16string::npos &&
                                  url.find(query) == std::u16string::npos;
                         }),
          downloads.end());
    }
  }

  list_container_->RemoveAllChildViews();
  if (downloads.empty()) {
    empty_state_view_->SetVisible(true);
    scroll_view_->SetVisible(false);
    scroll_view_->GetViewAccessibility().SetValue(u"No downloads");
    return;
  }

  const size_t download_count = downloads.size();
  const std::vector<DownloadSection> sections =
      BuildDownloadSections(std::move(downloads));

  for (size_t index = 0; index < sections.size(); ++index) {
    const auto& section = sections[index];
    if (index > 0) {
      auto* spacer = list_container_->AddChildView(std::make_unique<views::View>());
      spacer->SetPreferredSize(gfx::Size(0, kSectionSpacingDp));
    }

    list_container_->AddChildView(std::make_unique<DownloadSectionHeaderView>(
        section.title, section.items.size(), palette_));

    auto* rows_container = list_container_->AddChildView(std::make_unique<views::View>());
    auto* rows_layout = rows_container->SetLayoutManager(
        std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kVertical,
                                           gfx::Insets(), kRowSpacingDp));
    rows_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);

    for (const auto& item : section.items) {
      rows_container->AddChildView(std::make_unique<DownloadRowView>(
          item, BuildActionsForDownload(item),
          base::BindRepeating(&MahoSidebarDownloadsView::HandleDownloadAction,
                              weak_factory_.GetWeakPtr()),
          palette_));
    }
  }

  empty_state_view_->SetVisible(false);
  scroll_view_->SetVisible(true);
  scroll_view_->GetViewAccessibility().SetValue(DownloadCountText(download_count));
}

void MahoSidebarDownloadsView::SetDownloadsForTesting(std::vector<DownloadItem> downloads) {
  testing_downloads_ = std::move(downloads);
  ReloadDownloads();
}

void MahoSidebarDownloadsView::SetSearchQueryForTesting(const std::u16string& query) {
  if (search_field_) {
    search_field_->SetText(query);
  }
  ReloadDownloads();
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
    // Defense-in-depth: fail closed unless the profile may read regular
    // download metadata (see ReloadDownloads()).
    if (!browser_ ||
        !MahoIsCapabilityAllowed(
            browser_->GetProfile(),
            MahoPrivateCapability::kMahoDownloadMetadata)) {
      return;
    }
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
      download_file_action_callback_for_testing_.Run(path.AsUTF8Unsafe(), action);
      return;
    }

    // Log the resolved path, then probe existence off the UI thread (file I/O
    // may block) and act on the reply: open/reveal only if the file still
    // exists, so a moved/deleted file no longer produces a silent no-op.
    DLOG(WARNING) << "[MahoDL] action="
                  << (action == DownloadActionKind::kOpen ? "open" : "reveal")
                  << " id=" << download_id
                  << " path=" << path.AsUTF8Unsafe();
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, {base::MayBlock()},
        base::BindOnce(&base::PathExists, path),
        base::BindOnce(
            &MahoSidebarDownloadsView::HandleResolvedDownloadFileAction,
            weak_factory_.GetWeakPtr(), action, path));
    return;
  }

  // Drive the maho-core store directly by download id so the action works for
  // historical downloads that have no live download::DownloadItem in this
  // session (the store is authoritative and outlives the browser process).
  if (MahoCore* core = maho::GetCore()) {
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
        // Core-only (historical) removals do not fire the live download
        // observer, so refresh the list directly to drop the removed row.
        ReloadDownloads();
        break;
      case DownloadActionKind::kOpen:
      case DownloadActionKind::kReveal:
        break;
    }
  }

  // Additionally drive the live download::DownloadItem (if one exists in this
  // session) so an in-progress transfer actually pauses/resumes/cancels in
  // Chromium's download system. These no-op safely when there is no live item.
  if (browser_ && browser_->GetProfile()) {
    auto* service = maho::MahoDownloadBridgeServiceFactory::GetForProfile(browser_->GetProfile());
    if (service) {
      switch (action) {
        case DownloadActionKind::kPause:
          service->PauseDownload(download_id);
          break;
        case DownloadActionKind::kResume:
          service->ResumeDownload(download_id);
          break;
        case DownloadActionKind::kCancel:
          service->CancelDownload(download_id);
          break;
        case DownloadActionKind::kRemove:
          service->RemoveDownload(download_id);
          break;
        case DownloadActionKind::kOpen:
        case DownloadActionKind::kReveal:
          break;
      }
    }
  }
}

void MahoSidebarDownloadsView::HandleResolvedDownloadFileAction(
    DownloadActionKind action,
    base::FilePath path,
    bool exists) {
  DLOG(WARNING) << "[MahoDL] exists=" << exists
                << " path=" << path.AsUTF8Unsafe();
  if (!exists) {
    DLOG(WARNING) << "[MahoDL] file missing; skipping "
                  << (action == DownloadActionKind::kOpen ? "open" : "reveal");
    return;
  }
  auto* profile = browser_ ? browser_->GetProfile() : nullptr;
  if (action == DownloadActionKind::kOpen) {
    platform_util::OpenItem(profile, path, platform_util::OPEN_FILE,
                            platform_util::OpenOperationCallback());
  } else {
    platform_util::ShowItemInFolder(profile, path);
  }
}

void MahoSidebarDownloadsView::ContentsChanged(
    views::Textfield* sender,
    const std::u16string& new_contents) {
  search_debounce_timer_.Start(
      FROM_HERE, base::Milliseconds(200),
      base::BindOnce(&MahoSidebarDownloadsView::ApplySearchFilter,
                     weak_factory_.GetWeakPtr()));
}

void MahoSidebarDownloadsView::ApplySearchFilter() {
  ReloadDownloads();
}

}  // namespace maho
