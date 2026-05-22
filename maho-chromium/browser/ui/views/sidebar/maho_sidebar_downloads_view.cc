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
#include "base/memory/weak_ptr.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "chrome/browser/platform_util.h"
#include "components/vector_icons/vector_icons.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/ui/theme/maho_color_id.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_downloads_data.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_library_tile_helpers.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "ui/accessibility/ax_enums.mojom.h"
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
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
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
constexpr int kHeaderSummaryTopInsetDp = 8;
constexpr int kRowCornerRadiusDp = 10;
constexpr int kRowSpacingDp = 2;
constexpr int kRowHorizontalSpacingDp = 10;
constexpr int kRowVerticalSpacingDp = 4;
constexpr int kRowVerticalPaddingDp = 10;
constexpr int kRowHorizontalPaddingDp = 10;
constexpr int kBadgeSizeDp = 28;
constexpr int kBadgeCornerRadiusDp = 7;
constexpr int kBadgeLabelMaxChars = 4;
constexpr int kActionSpacingDp = 6;
constexpr int kActionButtonHeightDp = 28;
constexpr int kMetaSpacingDp = 3;
constexpr int kProgressBarHeightDp = 3;
constexpr int kProgressBarWidthDp = 48;
constexpr int kProgressBarCornerRadiusDp = 2;
constexpr int kSectionHeaderTopInsetDp = 12;
constexpr int kSectionHeaderBottomInsetDp = 6;
constexpr int kSectionHeaderHorizontalInsetDp = 6;
constexpr int kSectionSpacingDp = 0;
constexpr int kEmptyStateIconSizeDp = 24;
constexpr int kEmptyStateIconTileSizeDp = 40;
constexpr int kEmptyStateIconTileCornerRadiusDp = 12;
constexpr int kEmptyStateTextMaxWidthDp = 188;
constexpr int kEmptyStateHorizontalPaddingDp = 24;
constexpr int kEmptyStateItemSpacingDp =
    sidebar_layout::kArchiveEmptyStateItemSpacingDp;
constexpr int kListBottomInsetDp = 10;
constexpr int kListHorizontalInsetDp = 10;

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
                                      FormatByteCount(item.received_bytes);
  }
  return FormatByteCount(item.received_bytes) + u" of " +
         FormatByteCount(item.total_bytes);
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

std::u16string HeaderSummaryText(size_t count) {
  if (count == 0) {
    return u"Ready for your next file";
  }
  return base::StrCat({DownloadCountText(count), u" in your library"});
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
  explicit DownloadProgressBarView(double progress) : progress_(progress) {
    SetPreferredSize(gfx::Size(kProgressBarWidthDp, kProgressBarHeightDp));
    SetBackground(views::CreateRoundedRectBackground(
        kMahoColorLibraryListRowBackground, kProgressBarCornerRadiusDp));
    fill_view_ = AddChildView(std::make_unique<views::View>());
    fill_view_->SetBackground(views::CreateRoundedRectBackground(
        kMahoColorLibraryProgressFill, kProgressBarCornerRadiusDp));
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
  DownloadSectionHeaderView(const std::u16string& title, size_t count) {
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
    title_label->SetTextStyle(views::style::STYLE_BODY_5_MEDIUM);
    title_label->SetEnabledColor(kMahoColorLibrarySectionHeaderText);
    title_label->SetAutoColorReadabilityEnabled(false);
    title_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    title_label->GetViewAccessibility().SetIsIgnored(true);

    auto* count_label = AddChildView(
        std::make_unique<views::Label>(
            base::UTF8ToUTF16(std::to_string(count))));
    count_label->SetTextStyle(views::style::STYLE_BODY_5);
    count_label->SetEnabledColor(kMahoColorLibrarySectionHeaderText);
    count_label->SetAutoColorReadabilityEnabled(false);
    count_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    count_label->GetViewAccessibility().SetIsIgnored(true);
    if (GetColorProvider()) {
      count_label->SetEnabledColor(SkColorSetA(
          GetColorProvider()->GetColor(kMahoColorLibrarySectionHeaderText),
          0xB8));
    }

    count_label_ = count_label;
  }

  void OnThemeChanged() override {
    views::View::OnThemeChanged();
    if (count_label_ && GetColorProvider()) {
      count_label_->SetEnabledColor(SkColorSetA(
          GetColorProvider()->GetColor(kMahoColorLibrarySectionHeaderText),
          0xB8));
    }
  }

 private:
  raw_ptr<views::Label> count_label_ = nullptr;
};

BEGIN_METADATA(DownloadSectionHeaderView)
END_METADATA

std::u16string DetailText(const DownloadItem& item) {
  if (item.error.has_value() && !item.error->empty()) {
    return base::UTF8ToUTF16(*item.error);
  }
  if (item.state == "downloading" || item.state == "paused") {
    return ProgressText(item);
  }
  if (item.total_bytes > 0) {
    return FormatByteCount(item.total_bytes);
  }
  if (item.file_path.has_value() && !item.file_path->empty()) {
    return base::FilePath::FromUTF8Unsafe(*item.file_path).BaseName().LossyDisplayName();
  }
  return StateLabel(item.state);
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

std::u16string StatusText(const DownloadItem& item) {
  if (item.state == "downloading") {
    if (item.total_bytes > 0) {
      const uint64_t percent = std::min<uint64_t>(
          100, (item.received_bytes * 100) / item.total_bytes);
      return base::UTF8ToUTF16(std::to_string(percent) + "%");
    }
    return u"Live";
  }
  if (item.state == "completed") {
    return u"Done";
  }
  return StateLabel(item.state);
}

std::string DownloadBadgeToken(const DownloadItem& item) {
  std::string token;
  std::string source_path;
  if (item.file_path.has_value() && !item.file_path->empty()) {
    source_path = *item.file_path;
  } else {
    source_path = base::UTF16ToUTF8(item.filename);
  }

  if (!source_path.empty()) {
    token = base::FilePath::FromUTF8Unsafe(source_path).Extension();
    if (!token.empty() && token.front() == '.') {
      token.erase(0, 1);
    }
  }

  if (token.empty() && item.mime_type.has_value() && !item.mime_type->empty()) {
    const size_t slash_index = item.mime_type->find('/');
    token = slash_index == std::string::npos
                ? *item.mime_type
                : item.mime_type->substr(slash_index + 1);
  }

  if (token.empty()) {
    token = "FILE";
  }

  token = base::ToUpperASCII(token);
  if (token.length() > kBadgeLabelMaxChars) {
    token.resize(kBadgeLabelMaxChars);
  }
  return token;
}

std::u16string DownloadBadgeText(const DownloadItem& item) {
  return base::UTF8ToUTF16(DownloadBadgeToken(item));
}

MahoSidebarTileAccent DownloadBadgeAccentForToken(std::string_view token) {
  if (token == "PDF") {
    return MahoSidebarTileAccent::kRed;
  }
  if (token == "ZIP" || token == "RAR" || token == "7Z" ||
      token == "TAR" || token == "GZ") {
    return MahoSidebarTileAccent::kPurple;
  }
  if (token == "PNG" || token == "JPG" || token == "JPEG" ||
      token == "GIF" || token == "WEBP" || token == "SVG" ||
      token == "SVG+" || token == "HEIC" || token == "AVIF") {
    return MahoSidebarTileAccent::kCyan;
  }
  if (token == "MOV" || token == "MP4" || token == "M4V" ||
      token == "MKV" || token == "AVI" || token == "WEBM") {
    return MahoSidebarTileAccent::kBlue;
  }
  if (token == "FIG" || token == "AI" || token == "PSD" ||
      token == "SKET" || token == "XD") {
    return MahoSidebarTileAccent::kOrange;
  }
  if (token == "DOC" || token == "DOCX" || token == "TXT" ||
      token == "RTF" || token == "MD" || token == "PAGE") {
    return MahoSidebarTileAccent::kGreen;
  }

  return MahoSidebarTileAccentForSeed(token);
}

void StyleDownloadActionButton(views::MdTextButton* button, bool primary_action) {
  if (!button) {
    return;
  }

  button->SetStyle(ui::ButtonStyle::kText);
  button->SetCornerRadius(14);
  button->SetMinSize(gfx::Size(0, kActionButtonHeightDp));
  button->SetMaxSize(gfx::Size(0, kActionButtonHeightDp));
  button->SetRequestFocusOnPress(false);
  button->SetEnabledTextColors(primary_action ? kMahoColorPrimaryText
                                              : kMahoColorSecondaryText);
  button->SetBackground(views::CreateRoundedRectBackground(
      primary_action ? kMahoColorLibraryRowSelected
                     : kMahoColorLibrarySearchPillBackground,
      14));
  button->SetBorder(views::CreateRoundedRectBorder(
      1, 14, kMahoColorLibraryGlyphBorder));
}

class DownloadsEmptyStateView : public views::View {
  METADATA_HEADER(DownloadsEmptyStateView, views::View)

 public:
  DownloadsEmptyStateView() {
    GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
    GetViewAccessibility().SetName(u"Downloads empty state");

    tile_ = AddChildView(std::make_unique<views::View>());
    tile_->SetPreferredSize(
        gfx::Size(kEmptyStateIconTileSizeDp, kEmptyStateIconTileSizeDp));
    tile_->SetBackground(views::CreateRoundedRectBackground(
        kMahoColorLibraryListRowBackground, kEmptyStateIconTileCornerRadiusDp));
    tile_->SetBorder(views::CreateRoundedRectBorder(
        1, kEmptyStateIconTileCornerRadiusDp, kMahoColorLibraryGlyphBorder));
    auto* tile_layout = tile_->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal));
    tile_layout->set_main_axis_alignment(
        views::BoxLayout::MainAxisAlignment::kCenter);
    tile_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);
    tile_->GetViewAccessibility().SetIsIgnored(true);

    auto* icon = tile_->AddChildView(std::make_unique<views::ImageView>());
    icon->SetPreferredSize(
        gfx::Size(kEmptyStateIconSizeDp, kEmptyStateIconSizeDp));
    icon->SetImage(ui::ImageModel::FromVectorIcon(
        vector_icons::kFileDownloadOffChromeRefreshIcon,
        kMahoColorSecondaryText, kEmptyStateIconSizeDp));
    icon->GetViewAccessibility().SetIsIgnored(true);

    title_label_ = AddChildView(
        std::make_unique<views::Label>(u"No downloads yet"));
    title_label_->SetTextStyle(views::style::STYLE_BODY_3_MEDIUM);
    title_label_->SetEnabledColor(kMahoColorPrimaryText);
    title_label_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
    title_label_->SetAutoColorReadabilityEnabled(false);
    title_label_->GetViewAccessibility().SetIsIgnored(true);

    subtitle_label_ = AddChildView(std::make_unique<views::Label>(
        u"Files you save will collect here so you can open, reveal, or remove them later."));
    subtitle_label_->SetTextStyle(views::style::STYLE_BODY_4);
    subtitle_label_->SetEnabledColor(kMahoColorSecondaryText);
    subtitle_label_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
    subtitle_label_->SetAutoColorReadabilityEnabled(false);
    subtitle_label_->SetMultiLine(true);
    subtitle_label_->GetViewAccessibility().SetIsIgnored(true);
  }

  void Layout(PassKey) override {
    LayoutSuperclass<views::View>(this);
    if (!tile_ || !title_label_ || !subtitle_label_) {
      return;
    }

    const gfx::Size tile_size = tile_->GetPreferredSize();
    const gfx::Size title_size = title_label_->GetPreferredSize();
    const int subtitle_width = std::max(
        0, std::min(kEmptyStateTextMaxWidthDp,
                    width() - (kEmptyStateHorizontalPaddingDp * 2)));
    const int subtitle_height =
        subtitle_width > 0 ? subtitle_label_->GetHeightForWidth(subtitle_width)
                           : subtitle_label_->GetPreferredSize().height();
    const int content_width = std::max(
        {tile_size.width(), title_size.width(), subtitle_width});
    const int content_height = tile_size.height() + kEmptyStateItemSpacingDp +
                               title_size.height() + kEmptyStateItemSpacingDp +
                               subtitle_height;
    const int content_top = std::max(0, (height() - content_height) / 2);
    const int content_left = (width() - content_width) / 2;

    tile_->SetBounds(content_left + ((content_width - tile_size.width()) / 2),
                     content_top, tile_size.width(), tile_size.height());
    title_label_->SetBounds(
        content_left + ((content_width - title_size.width()) / 2),
        tile_->bounds().bottom() + kEmptyStateItemSpacingDp, title_size.width(),
        title_size.height());
    subtitle_label_->SetBounds(
        content_left + ((content_width - subtitle_width) / 2),
        title_label_->bounds().bottom() + kEmptyStateItemSpacingDp,
        subtitle_width, subtitle_height);
  }

 private:
  raw_ptr<views::View> tile_ = nullptr;
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

class DownloadRowView : public views::View {
  METADATA_HEADER(DownloadRowView, views::View)

 public:
  using ActionCallback =
      base::RepeatingCallback<void(const std::string&, DownloadActionKind)>;

  DownloadRowView(const DownloadItem& item,
                  std::vector<DownloadActionSpec> actions,
                  ActionCallback action_callback)
      : download_id_(item.id),
        badge_token_(DownloadBadgeToken(item)),
        action_callback_(std::move(action_callback)) {
    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal,
        gfx::Insets::TLBR(kRowVerticalPaddingDp, kRowHorizontalPaddingDp,
                          kRowVerticalPaddingDp, kRowHorizontalPaddingDp),
        kRowHorizontalSpacingDp));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);

    SetBackground(views::CreateRoundedRectBackground(
        kMahoColorLibraryListRowBackground, kRowCornerRadiusDp));
    SetBorder(views::CreateRoundedRectBorder(
        1, kRowCornerRadiusDp, kMahoColorLibraryGlyphBorder));

    badge_slot_ = AddChildView(std::make_unique<views::View>());
    badge_slot_->SetPreferredSize(gfx::Size(kBadgeSizeDp, kBadgeSizeDp));
    badge_slot_->SetLayoutManager(std::make_unique<views::FillLayout>());
    badge_slot_->GetViewAccessibility().SetIsIgnored(true);

    badge_label_ = badge_slot_->AddChildView(
        std::make_unique<views::Label>(DownloadBadgeText(item)));
    badge_label_->SetTextStyle(views::style::STYLE_BODY_5_MEDIUM);
    badge_label_->SetFontList(badge_label_->font_list().Derive(
        0, gfx::Font::NORMAL, gfx::Font::Weight::BOLD));
    badge_label_->SetAutoColorReadabilityEnabled(false);
    badge_label_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
    badge_label_->GetViewAccessibility().SetIsIgnored(true);

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
    title->SetTextStyle(views::style::STYLE_BODY_4_MEDIUM);
    title->SetEnabledColor(kMahoColorPrimaryText);
    title->SetAutoColorReadabilityEnabled(false);
    title->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    title->SetMultiLine(false);
    title->SetElideBehavior(gfx::ELIDE_TAIL);

    auto* subtitle = text_block->AddChildView(
        std::make_unique<views::Label>(DownloadSubtitleText(item)));
    subtitle->SetTextStyle(views::style::STYLE_BODY_5);
    subtitle->SetEnabledColor(kMahoColorSecondaryText);
    subtitle->SetAutoColorReadabilityEnabled(false);
    subtitle->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    subtitle->SetMultiLine(false);
    subtitle->SetElideBehavior(gfx::ELIDE_TAIL);

    auto* trailing_region = AddChildView(std::make_unique<views::View>());
    auto* trailing_layout = trailing_region->SetLayoutManager(
        std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kVertical, gfx::Insets(),
            kRowVerticalSpacingDp));
    trailing_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kEnd);

    auto* status_label = trailing_region->AddChildView(
        std::make_unique<views::Label>(StatusText(item)));
    status_label->SetTextStyle(views::style::STYLE_BODY_5_MEDIUM);
    status_label->SetEnabledColor(kMahoColorSecondaryText);
    status_label->SetAutoColorReadabilityEnabled(false);
    status_label->SetHorizontalAlignment(gfx::ALIGN_RIGHT);
    status_label->SetMultiLine(false);

    if (HasProgressBar(item)) {
      trailing_region->AddChildView(
          std::make_unique<DownloadProgressBarView>(ProgressFraction(item)));
    }

    if (!actions.empty()) {
      auto* action_row = trailing_region->AddChildView(std::make_unique<views::View>());
      auto* action_layout = action_row->SetLayoutManager(
          std::make_unique<views::BoxLayout>(
              views::BoxLayout::Orientation::kHorizontal, gfx::Insets(),
              kActionSpacingDp));
      action_layout->set_cross_axis_alignment(
          views::BoxLayout::CrossAxisAlignment::kCenter);
      action_layout->set_main_axis_alignment(
          views::BoxLayout::MainAxisAlignment::kEnd);

      for (const auto& action : actions) {
        auto* button = action_row->AddChildView(
            std::make_unique<views::MdTextButton>(
                base::BindRepeating(&DownloadRowView::HandleActionPressed,
                                    weak_factory_.GetWeakPtr(), action.kind),
                action.label));
        button->SetAccessibleName(action.accessible_name.empty()
                                      ? action.label
                                      : action.accessible_name);
        StyleDownloadActionButton(button,
                                  action.kind == DownloadActionKind::kOpen ||
                                      action.kind == DownloadActionKind::kResume ||
                                      action.kind == DownloadActionKind::kPause);
        action_buttons_.push_back(button);
      }
    }

    GetViewAccessibility().SetRole(ax::mojom::Role::kListItem);
    GetViewAccessibility().SetName(item.filename);
    UpdateBadgeColors();
  }

  void OnThemeChanged() override {
    views::View::OnThemeChanged();
    UpdateBadgeColors();
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
  void UpdateBadgeColors() {
    if (!badge_slot_ || !badge_label_) {
      return;
    }

    const MahoSidebarTilePalette colors = MahoSidebarTilePaletteForAccent(
        DownloadBadgeAccentForToken(badge_token_),
        MahoSidebarTilePaletteStyle::kTinted);
    badge_slot_->SetBackground(views::CreateRoundedRectBackground(
        colors.background, kBadgeCornerRadiusDp));
    badge_slot_->SetBorder(views::CreateRoundedRectBorder(
        1, kBadgeCornerRadiusDp, kMahoColorLibraryGlyphBorder));
    badge_label_->SetEnabledColor(colors.foreground);
  }

  void HandleActionPressed(DownloadActionKind kind) {
    if (!action_callback_) {
      return;
    }
    action_callback_.Run(download_id_, kind);
  }

  std::string download_id_;
  std::string badge_token_;
  ActionCallback action_callback_;
  raw_ptr<views::View> badge_slot_ = nullptr;
  raw_ptr<views::Label> badge_label_ = nullptr;
  std::vector<views::Button*> action_buttons_;
  base::WeakPtrFactory<DownloadRowView> weak_factory_{this};
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
          views::BoxLayout::Orientation::kVertical,
          gfx::Insets::TLBR(kHeaderTopInsetDp, kHeaderHorizontalInsetDp,
                            kHeaderBottomInsetDp, kHeaderHorizontalInsetDp),
          0));
  header_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  auto* search_shell = header_strip->AddChildView(std::make_unique<views::View>());
  search_shell->SetPreferredSize(gfx::Size(0, kHeaderSearchHeightDp));
  search_shell->SetBackground(views::CreateRoundedRectBackground(
      kMahoColorLibrarySearchPillBackground, kHeaderSearchCornerRadiusDp));
  search_shell->SetBorder(views::CreateRoundedRectBorder(
      1, kHeaderSearchCornerRadiusDp, kMahoColorLibraryGlyphBorder));
  auto* search_layout = search_shell->SetLayoutManager(
      std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal,
          gfx::Insets::VH(0, kHeaderSearchHorizontalInsetDp),
          kHeaderSearchIconSpacingDp));
  search_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  auto* search_icon =
      search_shell->AddChildView(std::make_unique<views::ImageView>());
  search_icon->GetViewAccessibility().SetIsIgnored(true);
  search_icon->SetPreferredSize(
      gfx::Size(kHeaderSearchIconSizeDp, kHeaderSearchIconSizeDp));
  search_icon->SetImage(ui::ImageModel::FromVectorIcon(
      vector_icons::kSearchIcon, kMahoColorSecondaryText,
      kHeaderSearchIconSizeDp));

  auto* placeholder = search_shell->AddChildView(
      std::make_unique<views::Label>(u"Search downloads..."));
  placeholder->SetTextStyle(views::style::STYLE_BODY_5);
  placeholder->SetEnabledColor(kMahoColorSecondaryText);
  placeholder->SetAutoColorReadabilityEnabled(false);
  placeholder->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  placeholder->GetViewAccessibility().SetIsIgnored(true);
  search_shell->GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
  search_shell->GetViewAccessibility().SetName(u"Search downloads header");

  header_summary_label_ = header_strip->AddChildView(
      std::make_unique<views::Label>(HeaderSummaryText(0)));
  header_summary_label_->SetTextStyle(views::style::STYLE_BODY_5);
  header_summary_label_->SetEnabledColor(kMahoColorLibrarySectionHeaderText);
  header_summary_label_->SetAutoColorReadabilityEnabled(false);
  header_summary_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  header_summary_label_->SetBorder(views::CreateEmptyBorder(
      gfx::Insets::TLBR(kHeaderSummaryTopInsetDp, 2, 0, 0)));
  header_summary_label_->GetViewAccessibility().SetIsIgnored(true);
  header_summary_label_->SetVisible(false);

  auto* body_container = AddChildView(std::make_unique<views::View>());
  body_container->SetLayoutManager(std::make_unique<views::FillLayout>());
  root_layout->SetFlexForView(body_container, 1, true);

  scroll_view_ = body_container->AddChildView(std::make_unique<views::ScrollView>());
  scroll_view_->SetBackgroundColor(std::nullopt);
  scroll_view_->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  scroll_view_->GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
  scroll_view_->GetViewAccessibility().SetName(u"Downloads results container");

  list_container_ = scroll_view_->SetContents(std::make_unique<views::View>());
  auto* list_layout = list_container_->SetLayoutManager(
      std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kVertical,
                                         gfx::Insets::TLBR(0, kListHorizontalInsetDp,
                                                           kListBottomInsetDp,
                                                           kListHorizontalInsetDp),
                                         0));
  list_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  list_container_->GetViewAccessibility().SetRole(ax::mojom::Role::kList);
  list_container_->GetViewAccessibility().SetName(u"Downloads results");

  empty_state_view_ = body_container->AddChildView(
      std::make_unique<DownloadsEmptyStateView>());

  empty_state_view_->SetVisible(true);
  scroll_view_->SetVisible(false);
}

void MahoSidebarDownloadsView::ReloadDownloads() {
  std::vector<DownloadItem> downloads = ParseDownloads();

  list_container_->RemoveAllChildViews();
  if (header_summary_label_) {
    header_summary_label_->SetVisible(!downloads.empty());
    if (!downloads.empty()) {
      header_summary_label_->SetText(HeaderSummaryText(downloads.size()));
    }
  }
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
        section.title, section.items.size()));

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
                              weak_factory_.GetWeakPtr())));
    }
  }

  empty_state_view_->SetVisible(false);
  scroll_view_->SetVisible(true);
  scroll_view_->GetViewAccessibility().SetValue(DownloadCountText(download_count));
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
