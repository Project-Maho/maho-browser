// Copyright 2026 Maho Browser. All rights reserved.

#include "maho_sidebar_media_view.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/i18n/case_conversion.h"
#include "chrome/browser/platform_util.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_downloads_data.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/models/image_model.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
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
#include "ui/views/controls/textfield/textfield.h"
#include "components/vector_icons/vector_icons.h"

namespace maho {
namespace {

constexpr int kPanelCornerRadiusDp = 18;
constexpr int kPanelPaddingDp = 12;
constexpr int kRowSpacingDp = 8;
constexpr int kRowPaddingDp = 10;
constexpr int kSectionSpacingDp = 10;

// MediaKind moved to header

std::u16string MediaKindLabel(MediaKind kind) {
  switch (kind) {
    case MediaKind::kAudio:
      return u"Audio";
    case MediaKind::kVideo:
      return u"Video";
    case MediaKind::kImage:
      return u"Images";
    case MediaKind::kOther:
      return u"Other media";
  }
}

std::u16string MediaKindGlyph(MediaKind kind) {
  switch (kind) {
    case MediaKind::kAudio:
      return u"A";
    case MediaKind::kVideo:
      return u"V";
    case MediaKind::kImage:
      return u"I";
    case MediaKind::kOther:
      return u"F";
  }
}

int MediaKindSortOrder(MediaKind kind) {
  switch (kind) {
    case MediaKind::kAudio:
      return 0;
    case MediaKind::kVideo:
      return 1;
    case MediaKind::kImage:
      return 2;
    case MediaKind::kOther:
      return 3;
  }
}

MediaKind MediaKindForItem(const DownloadItem& item) {
  if (item.mime_type.has_value()) {
    const std::string& mime = *item.mime_type;
    if (base::StartsWith(mime, "audio/", base::CompareCase::SENSITIVE)) {
      return MediaKind::kAudio;
    }
    if (base::StartsWith(mime, "video/", base::CompareCase::SENSITIVE)) {
      return MediaKind::kVideo;
    }
    if (base::StartsWith(mime, "image/", base::CompareCase::SENSITIVE)) {
      return MediaKind::kImage;
    }
  }

  if (item.file_path.has_value()) {
    if (base::EndsWith(*item.file_path, ".mp3",
                       base::CompareCase::INSENSITIVE_ASCII) ||
        base::EndsWith(*item.file_path, ".m4a",
                       base::CompareCase::INSENSITIVE_ASCII) ||
        base::EndsWith(*item.file_path, ".aac",
                       base::CompareCase::INSENSITIVE_ASCII) ||
        base::EndsWith(*item.file_path, ".flac",
                       base::CompareCase::INSENSITIVE_ASCII) ||
        base::EndsWith(*item.file_path, ".wav",
                       base::CompareCase::INSENSITIVE_ASCII)) {
      return MediaKind::kAudio;
    }
    if (base::EndsWith(*item.file_path, ".mp4",
                       base::CompareCase::INSENSITIVE_ASCII) ||
        base::EndsWith(*item.file_path, ".m4v",
                       base::CompareCase::INSENSITIVE_ASCII) ||
        base::EndsWith(*item.file_path, ".mov",
                       base::CompareCase::INSENSITIVE_ASCII) ||
        base::EndsWith(*item.file_path, ".mkv",
                       base::CompareCase::INSENSITIVE_ASCII) ||
        base::EndsWith(*item.file_path, ".webm",
                       base::CompareCase::INSENSITIVE_ASCII)) {
      return MediaKind::kVideo;
    }
    if (base::EndsWith(*item.file_path, ".jpg",
                       base::CompareCase::INSENSITIVE_ASCII) ||
        base::EndsWith(*item.file_path, ".jpeg",
                       base::CompareCase::INSENSITIVE_ASCII) ||
        base::EndsWith(*item.file_path, ".png",
                       base::CompareCase::INSENSITIVE_ASCII) ||
        base::EndsWith(*item.file_path, ".gif",
                       base::CompareCase::INSENSITIVE_ASCII) ||
        base::EndsWith(*item.file_path, ".webp",
                       base::CompareCase::INSENSITIVE_ASCII)) {
      return MediaKind::kImage;
    }
  }

  return MediaKind::kOther;
}

std::u16string MediaGroupDateLabel(const DownloadItem& item) {
  if (!item.completed_at.has_value() || item.completed_at->empty()) {
    if (item.started_at.empty()) {
      return u"Unknown date";
    }
    return base::UTF8ToUTF16(item.started_at.substr(0, std::min<size_t>(10, item.started_at.size())));
  }
  return base::UTF8ToUTF16(item.completed_at->substr(0, std::min<size_t>(10, item.completed_at->size())));
}

std::u16string MediaSubtitleText(const DownloadItem& item) {
  std::u16string subtitle;
  if (item.mime_type.has_value() && !item.mime_type->empty()) {
    subtitle = base::UTF8ToUTF16(*item.mime_type);
  }
  if (item.file_path.has_value() && !item.file_path->empty()) {
    if (!subtitle.empty()) {
      subtitle += u" · ";
    }
    subtitle += base::UTF8ToUTF16(*item.file_path);
  }
  if (subtitle.empty()) {
    subtitle = u"Media item";
  }
  return subtitle;
}

std::u16string MediaActionsDescription(bool has_file_path) {
  return has_file_path ? u"Open or reveal this file"
                       : u"No file path available";
}

struct MediaGroup {
  MediaKind kind;
  std::u16string date_label;
  std::vector<DownloadItem> items;
};

std::vector<MediaGroup> BuildMediaGroups(std::vector<DownloadItem> items) {
  std::sort(items.begin(), items.end(), [](const DownloadItem& lhs,
                                          const DownloadItem& rhs) {
    const int lhs_kind = MediaKindSortOrder(MediaKindForItem(lhs));
    const int rhs_kind = MediaKindSortOrder(MediaKindForItem(rhs));
    if (lhs_kind != rhs_kind) {
      return lhs_kind < rhs_kind;
    }
    const std::u16string lhs_date = MediaGroupDateLabel(lhs);
    const std::u16string rhs_date = MediaGroupDateLabel(rhs);
    if (lhs_date != rhs_date) {
      return lhs_date > rhs_date;
    }
    return lhs.filename < rhs.filename;
  });

  std::vector<MediaGroup> groups;
  for (auto& item : items) {
    const MediaKind kind = MediaKindForItem(item);
    const std::u16string date_label = MediaGroupDateLabel(item);
    auto it = std::find_if(groups.begin(), groups.end(),
                           [&](const MediaGroup& group) {
                             return group.kind == kind &&
                                    group.date_label == date_label;
                           });
    if (it == groups.end()) {
      groups.push_back(MediaGroup{kind, date_label, {std::move(item)}});
      continue;
    }
    it->items.push_back(std::move(item));
  }
  return groups;
}

class MediaSectionHeaderView : public views::View {
  METADATA_HEADER(MediaSectionHeaderView, views::View)

 public:
  MediaSectionHeaderView(std::u16string title,
                         size_t count,
                         const MahoSidebarPalette& palette) {
    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(), 2));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);

    std::u16string count_text = base::UTF8ToUTF16(std::to_string(count));
    auto* label = AddChildView(std::make_unique<views::Label>(
        title + u" · " + count_text + (count == 1 ? u" item" : u" items")));
    label->SetTextStyle(views::style::STYLE_BODY_4_MEDIUM);
    label->SetEnabledColor(palette.primary_text);
    label->SetAutoColorReadabilityEnabled(false);
    label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  }
};

BEGIN_METADATA(MediaSectionHeaderView)
END_METADATA

class MediaRowView : public views::View {
  METADATA_HEADER(MediaRowView, views::View)

 public:
  using ActionCallback =
      base::RepeatingCallback<void(const std::string&, MediaActionKind)>;

  MediaRowView(const DownloadItem& item,
               ActionCallback action_callback,
               const MahoSidebarPalette& palette)
      : item_id_(item.id),
        action_callback_(std::move(action_callback)),
        palette_(palette) {
    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal,
        gfx::Insets::TLBR(kRowPaddingDp, kRowPaddingDp, kRowPaddingDp,
                          kRowPaddingDp),
        8));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);

    auto* icon_tile = AddChildView(std::make_unique<views::View>());
    icon_tile->SetPreferredSize(gfx::Size(24, 24));
    icon_tile->SetBackground(views::CreateRoundedRectBackground(
        palette_.row_selected, 8));
    icon_tile->SetLayoutManager(std::make_unique<views::FillLayout>());
    auto* icon_label = icon_tile->AddChildView(std::make_unique<views::Label>(
        MediaKindGlyph(MediaKindForItem(item))));
    icon_label->SetHorizontalAlignment(gfx::ALIGN_CENTER);
    icon_label->SetTextStyle(views::style::STYLE_BODY_5_BOLD);
    icon_label->SetEnabledColor(palette_.primary_text);
    icon_label->SetAutoColorReadabilityEnabled(false);
    icon_label->GetViewAccessibility().SetIsIgnored(true);

    auto* text_column = AddChildView(std::make_unique<views::View>());
    auto* text_layout = text_column->SetLayoutManager(
        std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kVertical,
                                           gfx::Insets(), 2));
    text_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);
    layout->SetFlexForView(text_column, 1);

    auto* title = text_column->AddChildView(
        std::make_unique<views::Label>(item.filename));
    title->SetTextStyle(views::style::STYLE_BODY_4_MEDIUM);
    title->SetEnabledColor(palette_.primary_text);
    title->SetAutoColorReadabilityEnabled(false);
    title->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    title->SetMultiLine(false);
    title->SetElideBehavior(gfx::ELIDE_TAIL);

    auto* subtitle = text_column->AddChildView(
        std::make_unique<views::Label>(MediaSubtitleText(item)));
    subtitle->SetTextStyle(views::style::STYLE_BODY_5);
    subtitle->SetEnabledColor(palette_.secondary_text);
    subtitle->SetAutoColorReadabilityEnabled(false);
    subtitle->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    subtitle->SetMultiLine(false);
    subtitle->SetElideBehavior(gfx::ELIDE_TAIL);

    if (HasRealFilePath(item)) {
      auto* action_column = AddChildView(std::make_unique<views::View>());
      auto* action_layout = action_column->SetLayoutManager(
          std::make_unique<views::BoxLayout>(
              views::BoxLayout::Orientation::kVertical, gfx::Insets(), 4));
      action_layout->set_cross_axis_alignment(
          views::BoxLayout::CrossAxisAlignment::kEnd);

      AddActionButton(action_column, u"Open", MediaActionKind::kOpen);
      AddActionButton(action_column, u"Reveal", MediaActionKind::kReveal);
    }

    GetViewAccessibility().SetRole(ax::mojom::Role::kListItem);
    GetViewAccessibility().SetName(item.filename);
    GetViewAccessibility().SetDescription(
        MediaActionsDescription(HasRealFilePath(item)));
  }

  bool MatchesItemId(const std::string& item_id) const { return item_id_ == item_id; }

  views::Button* button_for_action_for_testing(size_t action_index) {
    if (action_index >= action_buttons_.size()) {
      return nullptr;
    }
    return action_buttons_[action_index];
  }

 private:
  void AddActionButton(views::View* parent, const std::u16string& label,
                       MediaActionKind action) {
    auto* button = parent->AddChildView(std::make_unique<views::MdTextButton>(
        base::BindRepeating(&MediaRowView::HandleActionPressed,
                            base::Unretained(this), action),
        label));
    button->SetStyle(ui::ButtonStyle::kText);
    button->SetCornerRadius(18);
    button->SetMinSize(gfx::Size(0, 32));
    button->SetMaxSize(gfx::Size(0, 32));
    button->SetRequestFocusOnPress(false);
    button->SetAccessibleName(label);
    // MdTextButton otherwise resolves text from OS-theme ColorIds, and macOS
    // paints STATE_DISABLED for every button in an inactive window.
    button->SetEnabledTextColors(palette_.primary_text);
    button->SetTextColor(views::Button::STATE_DISABLED, palette_.disabled_text);
    action_buttons_.push_back(button);
  }

  void HandleActionPressed(MediaActionKind action) {
    if (action_callback_) {
      action_callback_.Run(item_id_, action);
    }
  }

  std::string item_id_;
  ActionCallback action_callback_;
  std::vector<views::Button*> action_buttons_;
  MahoSidebarPalette palette_;
};

BEGIN_METADATA(MediaRowView)
END_METADATA

}  // namespace

BEGIN_METADATA(MahoSidebarMediaView)
END_METADATA

MahoSidebarMediaView::MahoSidebarMediaView() {
  BuildUi();
}

MahoSidebarMediaView::~MahoSidebarMediaView() = default;

void MahoSidebarMediaView::OnThemeChanged() {
  views::View::OnThemeChanged();
  UpdateAppearance();
}

void MahoSidebarMediaView::SetSidebarPalette(
    const MahoSidebarPalette& palette) {
  palette_ = palette;
  UpdateAppearance();
  // Re-applies the chip palette; also reloads the rows with the new palette.
  SetActiveFilter(active_filter_);
}

void MahoSidebarMediaView::BuildUi() {
  SetLayoutManager(std::make_unique<views::FillLayout>());

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
  surface->GetViewAccessibility().SetName(u"Media panel");

  header_label_ =
      surface->AddChildView(std::make_unique<views::Label>(u"Media"));
  header_label_->SetTextStyle(views::style::STYLE_BODY_3_MEDIUM);
  header_label_->SetAutoColorReadabilityEnabled(false);
  header_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);

  // 1. Search textfield shell
  search_shell_ = surface->AddChildView(std::make_unique<views::View>());
  search_shell_->SetPreferredSize(gfx::Size(0, 36));
  auto* search_layout = search_shell_->SetLayoutManager(
      std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal,
          gfx::Insets::VH(0, 12),
          8));
  search_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  search_icon_ =
      search_shell_->AddChildView(std::make_unique<views::ImageView>());
  search_icon_->GetViewAccessibility().SetIsIgnored(true);
  search_icon_->SetPreferredSize(gfx::Size(14, 14));

  auto search_field = std::make_unique<views::Textfield>();
  search_field_ = search_shell_->AddChildView(std::move(search_field));
  search_field_->SetPlaceholderText(u"Search media...");
  search_field_->set_controller(this);
  search_field_->SetPreferredSize(gfx::Size(0, 36));
  search_field_->SetBackgroundColor(SK_ColorTRANSPARENT);
  search_field_->SetBorder(nullptr);
  search_field_->SetAccessibleName(u"Search Media");
  search_layout->SetFlexForView(search_field_, 1);
  search_shell_->GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
  search_shell_->GetViewAccessibility().SetName(u"Search media header");

  // 2. Filter chips container
  filters_container_ = surface->AddChildView(std::make_unique<views::View>());
  auto* filters_layout = filters_container_->SetLayoutManager(
      std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal,
          gfx::Insets::VH(0, 0),
          6));
  filters_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  auto make_filter_chip = [this](std::u16string label, std::optional<MediaKind> kind) {
    auto* chip = filters_container_->AddChildView(
        std::make_unique<views::MdTextButton>(
            base::BindRepeating(&MahoSidebarMediaView::SetActiveFilter,
                                weak_factory_.GetWeakPtr(), kind),
            std::move(label)));
    chip->SetStyle(ui::ButtonStyle::kText);
    chip->SetCornerRadius(14);
    return chip;
  };

  make_filter_chip(u"All", std::nullopt);
  make_filter_chip(u"Audio", MediaKind::kAudio);
  make_filter_chip(u"Video", MediaKind::kVideo);
  make_filter_chip(u"Images", MediaKind::kImage);

  // Initialize filter styles
  SetActiveFilter(std::nullopt);

  scroll_view_ = surface->AddChildView(std::make_unique<views::ScrollView>());
  scroll_view_->SetBackgroundColor(std::nullopt);
  scroll_view_->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  scroll_view_->GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
  scroll_view_->GetViewAccessibility().SetName(u"Media results container");

  list_container_ = scroll_view_->SetContents(std::make_unique<views::View>());
  auto* list_layout = list_container_->SetLayoutManager(
      std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kVertical,
                                         gfx::Insets::VH(0, 0),
                                         kRowSpacingDp));
  list_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  list_container_->GetViewAccessibility().SetRole(ax::mojom::Role::kList);
  list_container_->GetViewAccessibility().SetName(u"Media results");

  empty_state_view_ = surface->AddChildView(
      std::make_unique<views::Label>(u"No media items"));
  empty_state_view_->SetTextStyle(views::style::STYLE_BODY_4);
  empty_state_view_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  empty_state_view_->SetMultiLine(true);
  empty_state_view_->SetAutoColorReadabilityEnabled(false);

  empty_state_view_->SetVisible(true);
  scroll_view_->SetVisible(false);
  scroll_view_->GetViewAccessibility().SetValue(u"No media items");
  UpdateAppearance();
}

void MahoSidebarMediaView::UpdateAppearance() {
  const SkColor surface = palette_.content_surface_stops.empty()
                              ? palette_.row_active
                              : palette_.content_surface_stops.back();
  SetBackground(views::CreateRoundedRectBackground(surface,
                                                    kPanelCornerRadiusDp));
  if (header_label_) {
    header_label_->SetEnabledColor(palette_.primary_text);
  }
  if (search_shell_) {
    search_shell_->SetBackground(
        views::CreateRoundedRectBackground(palette_.row_active, 8));
    search_shell_->SetBorder(
        views::CreateRoundedRectBorder(1, 8, palette_.outline));
  }
  if (search_icon_) {
    search_icon_->SetImage(ui::ImageModel::FromVectorIcon(
        vector_icons::kSearchIcon, palette_.secondary_text, 14));
  }
  if (search_field_) {
    search_field_->SetMahoResolvedTextColor(palette_.primary_text);
    search_field_->SetMahoResolvedPlaceholderTextColor(palette_.secondary_text);
  }
  if (empty_state_view_) {
    empty_state_view_->SetEnabledColor(palette_.secondary_text);
  }
}

views::View* MahoSidebarMediaView::media_row_for_item_id_for_testing(
    const std::string& item_id) {
  if (!list_container_) {
    return nullptr;
  }

  for (const auto& section : list_container_->children()) {
    if (section->children().size() < 2) {
      continue;
    }
    auto* rows_container = section->children()[1].get();
    for (const auto& row : rows_container->children()) {
      auto* media_row = static_cast<MediaRowView*>(row.get());
      if (media_row->MatchesItemId(item_id)) {
        return media_row;
      }
    }
  }

  return nullptr;
}

views::Button* MahoSidebarMediaView::media_action_button_for_item_id_for_testing(
    const std::string& item_id,
    size_t action_index) {
  auto* row = media_row_for_item_id_for_testing(item_id);
  if (!row) {
    return nullptr;
  }
  return static_cast<MediaRowView*>(row)->button_for_action_for_testing(
      action_index);
}

void MahoSidebarMediaView::ReloadMedia() {
  cached_media_items_.clear();
  for (const auto& item : ParseDownloads()) {
    if (IsMediaLikeDownload(item)) {
      cached_media_items_.push_back(item);
    }
  }

  std::vector<DownloadItem> media_items;
  std::u16string query;
  if (search_field_) {
    query = base::i18n::FoldCase(search_field_->GetText());
  }

  for (const auto& item : cached_media_items_) {
    // 1. MediaKind filtering
    if (active_filter_.has_value()) {
      if (MediaKindForItem(item) != *active_filter_) {
        continue;
      }
    }
    // 2. Search query filtering
    if (!query.empty()) {
      std::u16string name = base::i18n::FoldCase(item.filename);
      std::u16string url = base::i18n::FoldCase(base::UTF8ToUTF16(item.url));
      if (name.find(query) == std::u16string::npos &&
          url.find(query) == std::u16string::npos) {
        continue;
      }
    }
    media_items.push_back(item);
  }

  list_container_->RemoveAllChildViews();
  if (media_items.empty()) {
    empty_state_view_->SetVisible(true);
    scroll_view_->SetVisible(false);
    scroll_view_->GetViewAccessibility().SetValue(u"No media items");
    return;
  }

  const size_t media_item_count = media_items.size();
  const std::vector<MediaGroup> groups = BuildMediaGroups(std::move(media_items));
  for (const auto& group : groups) {
    auto* section = list_container_->AddChildView(std::make_unique<views::View>());
    auto* section_layout = section->SetLayoutManager(
        std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kVertical,
                                           gfx::Insets(), 6));
    section_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);

    section->AddChildView(std::make_unique<MediaSectionHeaderView>(
        MediaKindLabel(group.kind) + u" · " + group.date_label,
        group.items.size(), palette_));

    auto* rows_container = section->AddChildView(std::make_unique<views::View>());
    auto* rows_layout = rows_container->SetLayoutManager(
        std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kVertical,
                                           gfx::Insets(), kRowSpacingDp));
    rows_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);

    for (const auto& item : group.items) {
      rows_container->AddChildView(std::make_unique<MediaRowView>(
          item, base::BindRepeating(&MahoSidebarMediaView::HandleMediaAction,
                                     weak_factory_.GetWeakPtr()),
          palette_));
    }
  }

  empty_state_view_->SetVisible(false);
  scroll_view_->SetVisible(true);
  scroll_view_->GetViewAccessibility().SetValue(
      base::UTF8ToUTF16(std::to_string(media_item_count)) + u" items");
}

void MahoSidebarMediaView::HandleMediaAction(const std::string& item_id,
                                             MediaActionKind action) {
  const auto it = std::find_if(cached_media_items_.begin(), cached_media_items_.end(),
                               [&](const DownloadItem& item) {
                                 return item.id == item_id;
                               });
  if (it == cached_media_items_.end() || !HasRealFilePath(*it)) {
    return;
  }

  const base::FilePath path = base::FilePath::FromUTF8Unsafe(*it->file_path);
  if (!base::PathExists(path)) {
    return;
  }

  if (action == MediaActionKind::kOpen) {
    platform_util::OpenItem(nullptr, path, platform_util::OPEN_FILE,
                            platform_util::OpenOperationCallback());
  } else if (action == MediaActionKind::kReveal) {
    platform_util::ShowItemInFolder(nullptr, path);
  }
}

void MahoSidebarMediaView::ContentsChanged(
    views::Textfield* sender,
    const std::u16string& new_contents) {
  search_debounce_timer_.Start(
      FROM_HERE, base::Milliseconds(200),
      base::BindOnce(&MahoSidebarMediaView::ApplySearchFilter,
                     weak_factory_.GetWeakPtr()));
}

void MahoSidebarMediaView::ApplySearchFilter() {
  ReloadMedia();
}

void MahoSidebarMediaView::SetActiveFilter(std::optional<MediaKind> filter) {
  active_filter_ = filter;

  if (filters_container_) {
    size_t active_idx = 0;
    if (filter == MediaKind::kAudio) active_idx = 1;
    else if (filter == MediaKind::kVideo) active_idx = 2;
    else if (filter == MediaKind::kImage) active_idx = 3;

    for (size_t i = 0; i < filters_container_->children().size(); ++i) {
      auto* button = static_cast<views::MdTextButton*>(filters_container_->children()[i]);
      const bool active = i == active_idx;
      button->SetStyle(active ? ui::ButtonStyle::kProminent
                              : ui::ButtonStyle::kText);
      // Chip colors come from the Space palette, not the OS theme.
      button->SetEnabledTextColors(active ? palette_.primary_text
                                          : palette_.secondary_text);
      button->SetTextColor(views::Button::STATE_DISABLED,
                           palette_.disabled_text);
      button->SetBgColorOverrideDeprecated(
          active ? std::optional<SkColor>(palette_.row_selected)
                 : std::nullopt);
      button->SetStrokeColorOverrideDeprecated(
          active ? std::optional<SkColor>(palette_.outline) : std::nullopt);
    }
  }

  ReloadMedia();
}

}  // namespace maho
