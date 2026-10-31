// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_archive_view.h"

#include "maho/browser/ui/views/sidebar/maho_sidebar_library_rail_view.h"

#include <algorithm>
#include <array>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "base/i18n/case_conversion.h"
#include "base/json/json_reader.h"
#include "base/json/string_escape.h"
#include "base/logging.h"
#include "base/memory/raw_ptr.h"
#include "base/task/thread_pool.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "base/values.h"
#include "build/buildflag.h"
#include "cc/paint/paint_flags.h"
#include "components/vector_icons/vector_icons.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/ui/theme/maho_color_id.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/browser/ui/views/maho_lucide_icons/vector_icons.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_library_tile_helpers.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_state_adapter.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/base/mojom/menu_source_type.mojom-shared.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/events/event.h"
#include "ui/events/event_constants.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/vector_icon_types.h"
#include "ui/views/background.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/border.h"
#include "ui/views/context_menu_controller.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/focus_ring.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/style/typography.h"
#include "ui/views/view_class_properties.h"
#include "url/gurl.h"

#include "third_party/skia/include/core/SkColor.h"

namespace maho {

// ---- ArchivedTabItem ----
ArchivedTabItem::ArchivedTabItem() = default;
ArchivedTabItem::ArchivedTabItem(const ArchivedTabItem&) = default;
ArchivedTabItem::ArchivedTabItem(ArchivedTabItem&&) = default;
ArchivedTabItem& ArchivedTabItem::operator=(const ArchivedTabItem&) = default;
ArchivedTabItem& ArchivedTabItem::operator=(ArchivedTabItem&&) = default;
ArchivedTabItem::~ArchivedTabItem() = default;

// ---- ArchiveSection ----
ArchiveSection::ArchiveSection() = default;
ArchiveSection::ArchiveSection(const ArchiveSection&) = default;
ArchiveSection::ArchiveSection(ArchiveSection&&) = default;
ArchiveSection& ArchiveSection::operator=(const ArchiveSection&) = default;
ArchiveSection& ArchiveSection::operator=(ArchiveSection&&) = default;
ArchiveSection::~ArchiveSection() = default;

namespace {

std::u16string GetFilterModeTitle(ArchiveFilterMode mode) {
  switch (mode) {
    case ArchiveFilterMode::kAll:
      return u"Any time";
    case ArchiveFilterMode::kToday:
      return u"Today";
    case ArchiveFilterMode::kPastWeek:
      return u"Past week";
    case ArchiveFilterMode::kOlder:
      return u"Older";
  }
}

std::u16string GetArchiveResultsSummaryText(int visible_count, int total_count) {
  if (total_count == 0) {
    return u"No tabs";
  }
  if (visible_count == total_count) {
    return base::UTF8ToUTF16(std::to_string(visible_count)) +
           (visible_count == 1 ? u" tab" : u" tabs");
  }
  return base::UTF8ToUTF16(std::to_string(visible_count) + " of " +
                           std::to_string(total_count));
}

std::u16string GetArchivedTabsCountText(int count) {
  return base::UTF8ToUTF16(std::to_string(count)) +
         (count == 1 ? u" archived tab" : u" archived tabs");
}

enum FilterCommand {
  kFilterAll = 0,
  kFilterToday = 1,
  kFilterPastWeek = 2,
  kFilterOlder = 3,
};

enum RowCommand { kRowRestore = 100, kRowDelete = 101 };

constexpr int kSearchFieldHeight = 36;
constexpr int kSearchFieldCornerRadius = 8;
constexpr int kSearchFieldHorizontalInset = 12;
constexpr int kSearchFieldIconSpacing = 8;
constexpr int kSearchFieldIconSize = 14;
constexpr int kFilterButtonIconSize = 12;
constexpr int kFilterButtonHeight = 24;
constexpr int kFilterButtonCornerRadius = 6;
constexpr int kRowCornerRadius = 10;
constexpr int kRowTileSize = 28;
constexpr int kRowTileContentSize = 18;
constexpr int kRowFallbackGlyphSize = 16;
constexpr int kRowHorizontalSpacing = 8;
constexpr int kRowVerticalPadding = 6;
constexpr int kRowHorizontalPadding = 8;
constexpr int kHeaderBottomSpacing = 0;
constexpr int kSectionSpacing = 0;
constexpr int kSectionHeaderSpacing = 0;
constexpr int kRowSpacing = 2;
constexpr int kArchiveEmptyStateSubtitleWidthDp =
    static_cast<int>(sidebar_layout::kArchiveEmptyStateContentMaxWidthDp);

const gfx::Insets kArchiveInsets = gfx::Insets();
const gfx::Insets kHeaderInsets = gfx::Insets::TLBR(16, 16, 12, 16);
const gfx::Insets kListInsets = gfx::Insets::TLBR(0, 10, 10, 18);

// Callers pass resolved palette colors; taking ui::ColorId (an int) here
// silently turned those SkColors into meaningless color ids.
ui::ImageModel CreateArchivePaneIcon(const gfx::VectorIcon& icon,
                                     SkColor color,
                                     int size) {
  return ui::ImageModel::FromVectorIcon(icon, color, size);
}

base::Time ParseArchivedAt(const std::string& date_str) {
  base::Time result;
  if (base::Time::FromString(date_str.c_str(), &result)) {
    return result;
  }
  if (base::Time::FromUTCString(date_str.c_str(), &result)) {
    return result;
  }
  return base::Time();
}

std::u16string CompactLocation(const std::string& url_str) {
  GURL url(url_str);
  if (!url.is_valid() || url.host().empty()) {
    return base::UTF8ToUTF16(url_str);
  }
  std::string host(url.host());
  std::string path(url.path());
  while (!path.empty() && path.front() == '/') {
    path.erase(0, 1);
  }
  while (!path.empty() && path.back() == '/') {
    path.pop_back();
  }
  if (path.empty()) {
    return base::UTF8ToUTF16(host);
  }
  if (path.length() > 28) {
    path = path.substr(0, 25) + "\xe2\x80\xa6";
  }
  return base::UTF8ToUTF16(host + " / " + path);
}

std::u16string HostnameFromUrl(const std::string& url_str) {
  GURL url(url_str);
  if (!url.is_valid() || url.host().empty()) {
    return std::u16string();
  }
  return base::UTF8ToUTF16(url.host());
}

std::u16string GetArchiveRowTitleText(const ArchivedTabItem& item) {
  if (!item.title.empty()) {
    return item.title;
  }

  std::u16string hostname = HostnameFromUrl(item.url);
  if (!hostname.empty()) {
    return hostname;
  }

  if (!item.host.empty()) {
    return item.host;
  }

  return base::UTF8ToUTF16(item.url);
}

std::u16string GetArchiveRowSubtitleText(const ArchivedTabItem& item) {
  if (!item.host.empty()) {
    return item.host;
  }
  return CompactLocation(item.url);
}

std::u16string TrimWhitespace16(const std::u16string& str) {
  size_t start = str.find_first_not_of(u" \t\n\r");
  if (start == std::u16string::npos) {
    return {};
  }
  size_t end = str.find_last_not_of(u" \t\n\r");
  return str.substr(start, end - start + 1);
}

std::u16string ArchiveTileLetter(const ArchivedTabItem& item) {
  return MahoSidebarTileLetterForText(GetArchiveRowTitleText(item));
}

class SearchFieldShellView : public views::View {
  METADATA_HEADER(SearchFieldShellView, views::View)

 public:
  explicit SearchFieldShellView(views::Textfield* textfield)
      : textfield_(textfield) {}

  SearchFieldShellView(const SearchFieldShellView&) = delete;
  SearchFieldShellView& operator=(const SearchFieldShellView&) = delete;
  ~SearchFieldShellView() override = default;

  bool OnMousePressed(const ui::MouseEvent& event) override {
    if (textfield_ && textfield_->GetVisible()) {
      textfield_->RequestFocus();
      return true;
    }
    return views::View::OnMousePressed(event);
  }

 private:
  raw_ptr<views::Textfield> textfield_ = nullptr;
};

BEGIN_METADATA(SearchFieldShellView)
END_METADATA

}  // namespace

// ---- ArchiveRowView ----

class ArchiveRowView : public views::View,
                       public views::ContextMenuController,
                       public ui::SimpleMenuModel::Delegate {
  METADATA_HEADER(ArchiveRowView, views::View)

 public:
  ArchiveRowView(const ArchivedTabItem& item,
                 base::RepeatingCallback<void()> on_restore,
                 base::RepeatingCallback<void()> on_delete)
      : item_(item),
        accessible_name_(u"Restore archived tab " +
                           GetArchiveRowTitleText(item)),
        tab_id_(item.tab_id),
        on_restore_(std::move(on_restore)),
        on_delete_(std::move(on_delete)) {
    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal,
        gfx::Insets::TLBR(kRowVerticalPadding, kRowHorizontalPadding,
                          kRowVerticalPadding, kRowHorizontalPadding),
        kRowHorizontalSpacing));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);

    tile_container_ = AddChildView(std::make_unique<views::View>());
    tile_container_->SetPreferredSize(gfx::Size(kRowTileSize, kRowTileSize));
    tile_container_->SetLayoutManager(std::make_unique<views::FillLayout>());
    tile_container_->GetViewAccessibility().SetIsIgnored(true);

    auto* favicon_view = tile_container_->AddChildView(
        std::make_unique<views::ImageView>());
    favicon_view->GetViewAccessibility().SetIsIgnored(true);
    favicon_view->SetImageSize(
        gfx::Size(kRowTileContentSize, kRowTileContentSize));
    if (!item.favicon_png_data.empty()) {
      gfx::Image image =
          gfx::Image::CreateFrom1xPNGBytes(item.favicon_png_data);
      if (!image.IsEmpty()) {
        favicon_view->SetImage(
            ui::ImageModel::FromImageSkia(image.AsImageSkia()));
      } else {
        favicon_view->SetVisible(false);
      }
    } else {
      favicon_view->SetVisible(false);
    }

    if (!favicon_view->GetVisible()) {
      const std::u16string fallback_letter = ArchiveTileLetter(item);
      if (!fallback_letter.empty()) {
        tile_label_ = tile_container_->AddChildView(
            std::make_unique<views::Label>(fallback_letter));
        tile_label_->SetSkipSubpixelRenderingOpacityCheck(true);
        tile_label_->SetTextStyle(views::style::STYLE_BODY_5_BOLD);
        tile_label_->SetFontList(tile_label_->font_list().Derive(
            1, gfx::Font::NORMAL, gfx::Font::Weight::BOLD));
        tile_label_->SetEnabledColor(palette_.primary_text);
        tile_label_->SetAutoColorReadabilityEnabled(false);
        tile_label_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
        tile_label_->GetViewAccessibility().SetIsIgnored(true);
      } else {
        tile_icon_ = tile_container_->AddChildView(
            std::make_unique<views::ImageView>());
        tile_icon_->SetImage(CreateArchivePaneIcon(
            maho_lucide_icons::kGlobeIcon,
            palette_.primary_text, kRowFallbackGlyphSize));
        tile_icon_->SetPreferredSize(
            gfx::Size(kRowFallbackGlyphSize, kRowFallbackGlyphSize));
        tile_icon_->GetViewAccessibility().SetIsIgnored(true);
      }
    }

    text_container_ = AddChildView(std::make_unique<views::View>());
    auto* text_layout = text_container_->SetLayoutManager(
        std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kVertical, gfx::Insets(), 2));
    text_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);
    layout->SetFlexForView(text_container_, 1);

    title_label_ = text_container_->AddChildView(
        std::make_unique<views::Label>(GetArchiveRowTitleText(item)));
    title_label_->SetSkipSubpixelRenderingOpacityCheck(true);
    title_label_->GetViewAccessibility().SetIsIgnored(true);
    title_label_->SetTextStyle(views::style::STYLE_BODY_4_MEDIUM);
    title_label_->SetEnabledColor(palette_.primary_text);
    title_label_->SetAutoColorReadabilityEnabled(false);
    title_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    title_label_->SetMultiLine(false);
    title_label_->SetElideBehavior(gfx::ELIDE_TAIL);

    subtitle_label_ = text_container_->AddChildView(
        std::make_unique<views::Label>(GetArchiveRowSubtitleText(item)));
    subtitle_label_->SetSkipSubpixelRenderingOpacityCheck(true);
    subtitle_label_->GetViewAccessibility().SetIsIgnored(true);
    subtitle_label_->SetTextStyle(views::style::STYLE_BODY_5);
    subtitle_label_->SetEnabledColor(palette_.secondary_text);
    subtitle_label_->SetAutoColorReadabilityEnabled(false);
    subtitle_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    subtitle_label_->SetMultiLine(false);
    subtitle_label_->SetElideBehavior(gfx::ELIDE_TAIL);

    SetAccessibleRole(ax::mojom::Role::kButton);
    SetAccessibleName(accessible_name_);
    GetViewAccessibility().SetDescription(
        u"Restores this archived tab to the sidebar");
    SetFocusBehavior(FocusBehavior::ALWAYS);

    views::FocusRing::Install(this);
    views::InstallRoundRectHighlightPathGenerator(this, gfx::Insets(),
                                                  kRowCornerRadius);
    set_context_menu_controller(this);
    UpdateAppearance();
  }

  ArchiveRowView(const ArchiveRowView&) = delete;
  ArchiveRowView& operator=(const ArchiveRowView&) = delete;
  ~ArchiveRowView() override = default;

  void SetSidebarPalette(const MahoSidebarPalette& palette) {
    palette_ = palette;
    if (tile_label_) {
      tile_label_->SetEnabledColor(palette_.primary_text);
    }
    if (tile_icon_) {
      tile_icon_->SetImage(ui::ImageModel::FromVectorIcon(
          maho_lucide_icons::kGlobeIcon, palette_.primary_text,
          kRowFallbackGlyphSize));
    }
    if (title_label_) {
      title_label_->SetEnabledColor(palette_.primary_text);
    }
    if (subtitle_label_) {
      subtitle_label_->SetEnabledColor(palette_.secondary_text);
    }
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
    if (!event.IsOnlyLeftMouseButton()) {
      return views::View::OnMousePressed(event);
    }
    RequestFocus();
    restore_pending_ = true;
    return true;
  }

  void OnMouseReleased(const ui::MouseEvent& event) override {
    if (restore_pending_ &&
        (event.changed_button_flags() & ui::EF_LEFT_MOUSE_BUTTON) &&
        HitTestPoint(event.location())) {
      on_restore_.Run();
    }
    restore_pending_ = false;
    views::View::OnMouseReleased(event);
  }

  bool OnKeyPressed(const ui::KeyEvent& event) override {
    if (event.key_code() == ui::VKEY_RETURN ||
        event.key_code() == ui::VKEY_SPACE) {
      on_restore_.Run();
      return true;
    }
    if (event.key_code() == ui::VKEY_DELETE ||
        event.key_code() == ui::VKEY_BACK) {
      on_delete_.Run();
      return true;
    }
    return false;
  }

  // views::ContextMenuController:
  void ShowContextMenuForViewImpl(
      views::View* source,
      const gfx::Point& point,
      ui::mojom::MenuSourceType source_type) override {
    context_menu_model_ = std::make_unique<ui::SimpleMenuModel>(this);
    context_menu_model_->AddItem(kRowRestore, u"Restore");
    context_menu_model_->AddItem(kRowDelete, u"Delete");

    context_menu_runner_ = std::make_unique<views::MenuRunner>(
        context_menu_model_.get(), views::MenuRunner::CONTEXT_MENU);
    context_menu_runner_->RunMenuAt(
        GetWidget(), nullptr, gfx::Rect(point, gfx::Size()),
        views::MenuAnchorPosition::kTopLeft, source_type);
  }

  // ui::SimpleMenuModel::Delegate:
  void ExecuteCommand(int command_id, int event_flags) override {
    if (command_id == kRowRestore) {
      on_restore_.Run();
    } else if (command_id == kRowDelete) {
      on_delete_.Run();
    }
  }

  bool IsCommandIdChecked(int command_id) const override { return false; }

  void SetShowsDivider(bool show) {
    shows_divider_ = show;
    SchedulePaint();
  }

  void OnPaint(gfx::Canvas* canvas) override {
    View::OnPaint(canvas);
  }

 private:
  void UpdateAppearance() {
    if (tile_container_) {
      tile_container_->SetBackground(nullptr);
      tile_container_->SetBorder(nullptr);
    }

    if (HasFocus()) {
      SetBackground(views::CreateRoundedRectBackground(
          palette_.row_selected, kRowCornerRadius));
      SetBorder(views::CreateRoundedRectBorder(
          1, kRowCornerRadius, palette_.outline));
    } else if (IsMouseHovered()) {
      SetBackground(views::CreateRoundedRectBackground(
          palette_.row_hover, kRowCornerRadius));
      SetBorder(views::CreateRoundedRectBorder(
          1, kRowCornerRadius, palette_.outline));
    } else {
      SetBackground(nullptr);
      SetBorder(nullptr);
    }
    SchedulePaint();
  }

  const ArchivedTabItem item_;
  const std::u16string accessible_name_;
  std::string tab_id_;
  base::RepeatingClosure on_restore_;
  base::RepeatingClosure on_delete_;
  bool restore_pending_ = false;
  bool shows_divider_ = false;
  raw_ptr<views::View> tile_container_ = nullptr;
  raw_ptr<views::Label> tile_label_ = nullptr;
  raw_ptr<views::ImageView> tile_icon_ = nullptr;
  raw_ptr<views::View> text_container_ = nullptr;
  raw_ptr<views::Label> title_label_ = nullptr;
  raw_ptr<views::Label> subtitle_label_ = nullptr;
  std::unique_ptr<ui::SimpleMenuModel> context_menu_model_;
  std::unique_ptr<views::MenuRunner> context_menu_runner_;
  MahoSidebarPalette palette_;
};

BEGIN_METADATA(ArchiveRowView)
END_METADATA

// ---- ArchiveSectionHeaderView ----

class ArchiveSectionHeaderView : public views::View {
  METADATA_HEADER(ArchiveSectionHeaderView, views::View)

  public:
  ArchiveSectionHeaderView(const std::u16string& title,
                           int count,
                           const MahoSidebarPalette& palette)
      : palette_(palette) {
    GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
    GetViewAccessibility().SetName(title + u", " +
                                   GetArchivedTabsCountText(count));

    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal,
        gfx::Insets::TLBR(12, 6, 6, 6), 5));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStart);

    title_label_ = AddChildView(std::make_unique<views::Label>(title));
    // Labels sit over an opaque ancestor background; the nearest compositor
    // layer is the macOS composited ScrollView/Textfield layer, so skip the
    // subpixel opacity DCHECK (same as row labels).
    title_label_->SetSkipSubpixelRenderingOpacityCheck(true);
    title_label_->GetViewAccessibility().SetIsIgnored(true);
    title_label_->SetTextStyle(views::style::STYLE_BODY_5_MEDIUM);
    title_label_->SetEnabledColor(palette_.primary_text);
    title_label_->SetAutoColorReadabilityEnabled(false);
    title_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);

    count_label_ = AddChildView(
        std::make_unique<views::Label>(
            base::UTF8ToUTF16(std::to_string(count))));
    count_label_->SetSkipSubpixelRenderingOpacityCheck(true);
    count_label_->GetViewAccessibility().SetIsIgnored(true);
    count_label_->SetTextStyle(views::style::STYLE_BODY_5);
    count_label_->SetEnabledColor(palette_.primary_text);
    count_label_->SetAutoColorReadabilityEnabled(false);
    count_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    count_label_->SetBorder(
        views::CreateEmptyBorder(gfx::Insets::TLBR(2, 0, 0, 0)));

    layout->SetFlexForView(title_label_, 0);
  }

  ArchiveSectionHeaderView(const ArchiveSectionHeaderView&) = delete;
  ArchiveSectionHeaderView& operator=(const ArchiveSectionHeaderView&) = delete;
  ~ArchiveSectionHeaderView() override = default;

  void SetSidebarPalette(const MahoSidebarPalette& palette) {
    palette_ = palette;
    title_label_->SetEnabledColor(palette_.primary_text);
    count_label_->SetEnabledColor(palette_.primary_text);
  }

  void OnThemeChanged() override {
    views::View::OnThemeChanged();
    SetSidebarPalette(palette_);
  }

 private:
  raw_ptr<views::Label> title_label_ = nullptr;
  raw_ptr<views::Label> count_label_ = nullptr;
  MahoSidebarPalette palette_;
};

BEGIN_METADATA(ArchiveSectionHeaderView)
END_METADATA

// ---- ArchiveEmptyStateView ----

class ArchiveEmptyStateView : public views::View {
  METADATA_HEADER(ArchiveEmptyStateView, views::View)

 public:
  ArchiveEmptyStateView() {
    GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
    GetViewAccessibility().SetName(u"Archive empty state");

    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(), 0));
    layout->set_main_axis_alignment(
        views::BoxLayout::MainAxisAlignment::kCenter);
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);

    auto* content = AddChildView(std::make_unique<views::View>());
    auto* content_layout = content->SetLayoutManager(
        std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kVertical, gfx::Insets(),
            sidebar_layout::kArchiveEmptyStateItemSpacingDp));
    content_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);

    auto* icon_view = content->AddChildView(std::make_unique<views::ImageView>());
    icon_view->GetViewAccessibility().SetIsIgnored(true);
    icon_view->SetPreferredSize(gfx::Size(
        sidebar_layout::kArchiveEmptyStateIconSizeDp,
        sidebar_layout::kArchiveEmptyStateIconSizeDp));
    icon_view_ = icon_view;
    icon_view_->SetImage(ui::ImageModel::FromVectorIcon(
        maho_lucide_icons::kArchiveIcon, palette_.secondary_text,
        sidebar_layout::kArchiveEmptyStateIconSizeDp));

    title_label_ = content->AddChildView(
        std::make_unique<views::Label>(u"Nothing archived yet"));
    title_label_->SetSkipSubpixelRenderingOpacityCheck(true);
    title_label_->GetViewAccessibility().SetIsIgnored(true);
    title_label_->SetTextStyle(views::style::STYLE_BODY_3_MEDIUM);
    title_label_->SetEnabledColor(palette_.primary_text);
    title_label_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
    title_label_->SetAutoColorReadabilityEnabled(false);

    subtitle_label_ = content->AddChildView(std::make_unique<views::Label>(u""));
    subtitle_label_->SetSkipSubpixelRenderingOpacityCheck(true);
    subtitle_label_->GetViewAccessibility().SetIsIgnored(true);
    subtitle_label_->SetTextStyle(views::style::STYLE_BODY_4);
    subtitle_label_->SetEnabledColor(palette_.secondary_text);
    subtitle_label_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
    subtitle_label_->SetAutoColorReadabilityEnabled(false);
    subtitle_label_->SetMultiLine(true);
    subtitle_label_->SizeToFit(kArchiveEmptyStateSubtitleWidthDp);

    Configure(/*has_archived_tabs=*/false, /*has_active_filters=*/false);
  }

  ArchiveEmptyStateView(const ArchiveEmptyStateView&) = delete;
  ArchiveEmptyStateView& operator=(const ArchiveEmptyStateView&) = delete;
  ~ArchiveEmptyStateView() override = default;

  void SetSidebarPalette(const MahoSidebarPalette& palette) {
    palette_ = palette;
    icon_view_->SetImage(ui::ImageModel::FromVectorIcon(
        maho_lucide_icons::kArchiveIcon, palette_.secondary_text,
        sidebar_layout::kArchiveEmptyStateIconSizeDp));
    title_label_->SetEnabledColor(palette_.primary_text);
    subtitle_label_->SetEnabledColor(palette_.secondary_text);
  }

  void OnThemeChanged() override {
    views::View::OnThemeChanged();
    SetSidebarPalette(palette_);
  }

  void Configure(bool has_archived_tabs, bool has_active_filters) {
    if (has_archived_tabs && has_active_filters) {
      title_label_->SetText(u"No matching tabs");
      subtitle_label_->SetText(
          u"Clear the search or adjust the filter to see archived tabs.");
      subtitle_label_->SetVisible(true);
      GetViewAccessibility().SetName(u"Archive empty state, no matching tabs");
    } else {
      title_label_->SetText(u"Nothing archived yet");
      subtitle_label_->SetText(
          u"Tabs you archive will stay here until you bring them back.");
      subtitle_label_->SetVisible(true);
      GetViewAccessibility().SetName(
          u"Archive empty state, no archived tabs yet");
    }
  }

 private:
  raw_ptr<views::ImageView> icon_view_ = nullptr;
  raw_ptr<views::Label> title_label_ = nullptr;
  raw_ptr<views::Label> subtitle_label_ = nullptr;
  MahoSidebarPalette palette_;
};

BEGIN_METADATA(ArchiveEmptyStateView)
END_METADATA

// ---- MahoSidebarArchiveView ----

BEGIN_METADATA(MahoSidebarArchiveView)
END_METADATA

MahoSidebarArchiveView::MahoSidebarArchiveView(Browser* browser)
    : browser_(browser) {
  BuildUi();
}

MahoSidebarArchiveView::~MahoSidebarArchiveView() = default;

void MahoSidebarArchiveView::SetSidebarPalette(
    const MahoSidebarPalette& palette) {
  palette_ = palette;
  if (search_shell_) {
    search_shell_->SetBackground(views::CreateRoundedRectBackground(
        palette_.row_active, kSearchFieldCornerRadius));
    search_shell_->SetBorder(views::CreateRoundedRectBorder(
        1, kSearchFieldCornerRadius, palette_.outline));
  }
  if (search_icon_) {
    search_icon_->SetImage(ui::ImageModel::FromVectorIcon(
        maho_lucide_icons::kSearchIcon, palette_.secondary_text,
        kSearchFieldIconSize));
  }
  if (search_field_) {
    search_field_->SetMahoResolvedTextColor(palette_.primary_text);
    search_field_->SetMahoResolvedPlaceholderTextColor(palette_.secondary_text);
  }
  if (empty_state_view_) {
    static_cast<ArchiveEmptyStateView*>(empty_state_view_.get())
        ->SetSidebarPalette(palette_);
  }
  UpdateFilterButtonAppearance();
  ApplyFilters();
}

void MahoSidebarArchiveView::SetArchivedTabsForTesting(
    std::vector<ArchivedTabItem> tabs) {
  test_injection_active_ = true;
  all_archived_tabs_ = std::move(tabs);
  ApplyFilters();
}

std::u16string MahoSidebarArchiveView::filter_accessibility_value_for_testing()
    const {
  if (!filter_button_) {
    return std::u16string();
  }

  ui::AXNodeData data;
  filter_button_->GetViewAccessibility().GetAccessibleNodeData(&data);
  return data.GetString16Attribute(ax::mojom::StringAttribute::kValue);
}

void MahoSidebarArchiveView::BuildUi() {
  auto* root_layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, kArchiveInsets, 0));
  root_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
  GetViewAccessibility().SetName(u"Archived tabs panel");

  // --- Flat header strip with separated search pill + filter chip ---
  header_surface_ = AddChildView(std::make_unique<views::View>());
  header_surface_->SetBackground(nullptr);
  header_surface_->SetBorder(nullptr);
  auto* header_layout = header_surface_->SetLayoutManager(
      std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal,
          kHeaderInsets, 8));
  header_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  auto search_field = std::make_unique<views::Textfield>();

  search_shell_ = header_surface_->AddChildView(
      std::make_unique<SearchFieldShellView>(search_field.get()));
  search_shell_->SetPreferredSize(gfx::Size(0, kSearchFieldHeight));
  search_shell_->SetBackground(views::CreateRoundedRectBackground(
      palette_.row_active, kSearchFieldCornerRadius));
  search_shell_->SetBorder(views::CreateRoundedRectBorder(
      1, kSearchFieldCornerRadius, palette_.outline));
  auto* search_layout = search_shell_->SetLayoutManager(
      std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal,
          gfx::Insets::VH(0, kSearchFieldHorizontalInset),
          kSearchFieldIconSpacing));
  search_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  search_icon_ = search_shell_->AddChildView(std::make_unique<views::ImageView>());
  search_icon_->GetViewAccessibility().SetIsIgnored(true);
  search_icon_->SetPreferredSize(gfx::Size(kSearchFieldIconSize,
                                           kSearchFieldIconSize));
  search_icon_->SetImage(CreateArchivePaneIcon(
      maho_lucide_icons::kSearchIcon, palette_.secondary_text,
      kSearchFieldIconSize));

  search_field_ = search_shell_->AddChildView(std::move(search_field));
  search_field_->SetPlaceholderText(u"Search archived tabs...");
  search_field_->set_controller(this);
  search_field_->SetPreferredSize(gfx::Size(0, kSearchFieldHeight));
  search_field_->SetBackgroundColor(SK_ColorTRANSPARENT);
  search_field_->SetBorder(nullptr);
  search_field_->SetMahoResolvedTextColor(palette_.primary_text);
  search_field_->SetAccessibleName(u"Search Archive");
  search_layout->SetFlexForView(search_field_, 1);

  filter_button_ = header_surface_->AddChildView(
      std::make_unique<views::LabelButton>(
          base::BindRepeating(&MahoSidebarArchiveView::OnFilterPressed,
                              weak_factory_.GetWeakPtr()),
          u""));
  filter_button_->SetImageModel(
      views::Button::STATE_NORMAL,
      CreateArchivePaneIcon(maho_lucide_icons::kSlidersHorizontalIcon,
                            palette_.secondary_text,
                            kFilterButtonIconSize));
  filter_button_->SetMinSize(
      gfx::Size(kFilterButtonHeight, kFilterButtonHeight));
  filter_button_->SetMaxSize(
      gfx::Size(kFilterButtonHeight, kFilterButtonHeight));
  filter_button_->SetAccessibleName(u"Filter archived tabs");
  filter_button_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  filter_button_->SetImageLabelSpacing(4);
  filter_button_->SetTextSubpixelRenderingEnabled(false);
  UpdateFilterButtonAppearance();
  header_layout->SetFlexForView(search_shell_, 1);

  auto* header_spacer = AddChildView(std::make_unique<views::View>());
  header_spacer->SetPreferredSize(gfx::Size(0, kHeaderBottomSpacing));

  // --- Flat body container for scroll + empty overlay ---
  body_container_ = AddChildView(std::make_unique<views::View>());
  body_container_->SetLayoutManager(std::make_unique<views::FillLayout>());
  body_container_->SetBackground(nullptr);
  body_container_->SetBorder(nullptr);
  root_layout->SetFlexForView(body_container_, 1, true);

  scroll_view_ = body_container_->AddChildView(
      std::make_unique<views::ScrollView>());
  scroll_view_->SetBackgroundColor(std::nullopt);
  scroll_view_->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  scroll_view_->SetVerticalScrollBarMode(
      views::ScrollView::ScrollBarMode::kHiddenButEnabled);
  scroll_view_->GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
  scroll_view_->GetViewAccessibility().SetName(
      u"Archived tabs results container");

  list_container_ = scroll_view_->SetContents(std::make_unique<views::View>());
  auto* list_layout = list_container_->SetLayoutManager(
      std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical,
          kListInsets, 0));
  list_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  list_container_->GetViewAccessibility().SetRole(ax::mojom::Role::kList);
  list_container_->GetViewAccessibility().SetName(u"Archived tabs results");

  empty_state_view_ = body_container_->AddChildView(
      std::make_unique<ArchiveEmptyStateView>());
  static_cast<ArchiveEmptyStateView*>(empty_state_view_.get())
      ->SetSidebarPalette(palette_);

  empty_state_view_->SetVisible(true);
  scroll_view_->SetVisible(false);
}

void MahoSidebarArchiveView::ReloadArchivedTabs() {
  if (test_injection_active_) {
    return;
  }

  all_archived_tabs_.clear();

  auto* bridge = MahoSpaceProfileBridge::GetInstance();
  if (!bridge) {
    ApplyFilters();
    return;
  }

  const std::string& active_space_id = bridge->GetActiveSpaceId(browser_);
  if (active_space_id.empty()) {
    ApplyFilters();
    return;
  }

  std::string space_id_json = base::GetQuotedJSONString(active_space_id);
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(
          [](std::string space_id_json) -> std::string {
            MahoCore* core = maho::GetCore();
            if (!core) {
              return "";
            }
            char* json_str = maho_core_get_archived_tabs(core, space_id_json.c_str());
            if (!json_str) {
              return "";
            }
            std::string json(json_str);
            maho_string_free(json_str);
            return json;
          },
          space_id_json),
      base::BindOnce(&MahoSidebarArchiveView::OnArchivedTabsLoaded,
                     weak_factory_.GetWeakPtr()));
}

void MahoSidebarArchiveView::OnArchivedTabsLoaded(std::string json) {
  if (json.empty()) {
    ApplyFilters();
    return;
  }

  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    DLOG(WARNING) << "Failed to parse archived tabs JSON";
    ApplyFilters();
    return;
  }

  for (const auto& item : parsed->GetList()) {
    const auto* dict = item.GetIfDict();
    if (!dict) {
      continue;
    }

    ArchivedTabItem tab;
    const std::string* id = dict->FindString("id");
    if (id) {
      tab.tab_id = *id;
    }
    const std::string* title = dict->FindString("title");
    if (title) {
      tab.title = TrimWhitespace16(base::UTF8ToUTF16(*title));
    }
    const std::string* url = dict->FindString("url");
    if (url) {
      tab.url = *url;
      tab.host = HostnameFromUrl(*url);
    }
    const std::string* archived_at = dict->FindString("archivedAt");
    if (archived_at) {
      tab.archived_at = ParseArchivedAt(*archived_at);
    }
    const std::string* space_id = dict->FindString("spaceId");
    if (space_id) {
      tab.space_id = *space_id;
    }

    const auto* favicon_dict = dict->FindDict("favicon");
    if (favicon_dict) {
      const auto* data_list = favicon_dict->FindList("data");
      if (data_list) {
        std::vector<uint8_t> bytes;
        bytes.reserve(data_list->size());
        for (const auto& v : *data_list) {
          bytes.push_back(static_cast<uint8_t>(v.GetInt()));
        }
        tab.favicon_png_data = std::move(bytes);
      }
    }

    if (!tab.tab_id.empty()) {
      all_archived_tabs_.push_back(std::move(tab));
    }
  }

  ApplyFilters();
}

void MahoSidebarArchiveView::ResetState() {
  // Resetting the pane returns archive loading to the normal production path.
  // Test-injected seed state should persist only for the active seeded session,
  // not across a later pane reopen.
  test_injection_active_ = false;
  if (search_field_) {
    search_field_->SetText(u"");
  }
  filter_mode_ = ArchiveFilterMode::kAll;
  UpdateFilterButtonAppearance();
  ApplyFilters();
}

void MahoSidebarArchiveView::FocusSearchField() {
  if (search_field_) {
    search_field_->RequestFocus();
  }
}

bool MahoSidebarArchiveView::IsPositionInWindowCaption(
    const gfx::Point& point) const {
  if (header_surface_) {
    gfx::Point point_in_header = point;
    views::View::ConvertPointToTarget(this, header_surface_, &point_in_header);
    if (header_surface_->HitTestPoint(point_in_header)) {
      if (search_shell_) {
        gfx::Point point_in_search = point;
        views::View::ConvertPointToTarget(this, search_shell_, &point_in_search);
        if (search_shell_->GetVisible() &&
            search_shell_->HitTestPoint(point_in_search)) {
          return false;
        }
      }
      if (filter_button_) {
        gfx::Point point_in_filter = point;
        views::View::ConvertPointToTarget(this, filter_button_, &point_in_filter);
        if (filter_button_->GetVisible() &&
            filter_button_->HitTestPoint(point_in_filter)) {
          return false;
        }
      }
      return true;
    }
  }

  if (scroll_view_) {
    gfx::Point point_in_scroll = point;
    views::View::ConvertPointToTarget(this, scroll_view_, &point_in_scroll);
    if (scroll_view_->GetVisible() && scroll_view_->HitTestPoint(point_in_scroll)) {
      return false;
    }
  }

  return false;
}

void MahoSidebarArchiveView::SetRestoreCallback(RestoreCallback callback) {
  restore_callback_ = std::move(callback);
}

void MahoSidebarArchiveView::SetDeleteCallback(DeleteCallback callback) {
  delete_callback_ = std::move(callback);
}

void MahoSidebarArchiveView::ContentsChanged(
    views::Textfield* sender,
    const std::u16string& new_contents) {
  search_debounce_timer_.Start(
      FROM_HERE, base::Milliseconds(200),
      base::BindOnce(&MahoSidebarArchiveView::ApplyFilters,
                     weak_factory_.GetWeakPtr()));
}

void MahoSidebarArchiveView::ApplyFilters() {
  std::u16string query;
  if (search_field_) {
    query = search_field_->GetText();
  }

  std::u16string normalized_query = base::i18n::FoldCase(query);
  std::u16string collapsed_query;
  for (char16_t c : normalized_query) {
    if (c != ' ' || (!collapsed_query.empty() && collapsed_query.back() != ' ')) {
      collapsed_query += c;
    }
  }
  normalized_query = std::move(collapsed_query);
  while (!normalized_query.empty() && normalized_query.back() == ' ') {
    normalized_query.pop_back();
  }

  std::vector<ArchivedTabItem> filtered;
  for (const auto& tab : all_archived_tabs_) {
    if (!normalized_query.empty()) {
      std::u16string haystack = base::i18n::FoldCase(
          tab.title + u" " + base::UTF8ToUTF16(tab.url));
      if (haystack.find(normalized_query) == std::u16string::npos) {
        continue;
      }
    }

    if (filter_mode_ != ArchiveFilterMode::kAll) {
      int day_off = DayOffset(tab.archived_at);
      switch (filter_mode_) {
        case ArchiveFilterMode::kToday:
          if (day_off != 0) continue;
          break;
        case ArchiveFilterMode::kPastWeek:
          if (day_off < 1 || day_off > 7) continue;
          break;
        case ArchiveFilterMode::kOlder:
          if (day_off >= 0 && day_off <= 7) continue;
          break;
        case ArchiveFilterMode::kAll:
          break;
      }
    }

    filtered.push_back(tab);
  }

  auto sections = GroupIntoSections(filtered);
  visible_result_count_ = static_cast<int>(filtered.size());
  RebuildList(sections, static_cast<int>(filtered.size()));
}

std::vector<ArchiveSection> MahoSidebarArchiveView::GroupIntoSections(
    const std::vector<ArchivedTabItem>& items) const {
  std::vector<const ArchivedTabItem*> sorted;
  sorted.reserve(items.size());
  for (const auto& item : items) {
    sorted.push_back(&item);
  }
  std::sort(sorted.begin(), sorted.end(),
            [](const ArchivedTabItem* a, const ArchivedTabItem* b) {
              return a->archived_at > b->archived_at;
            });

  std::vector<ArchiveSection> sections;
  std::map<std::u16string, size_t> section_index;

  for (const auto* item : sorted) {
    int day_off = DayOffset(item->archived_at);
    auto [title, sort_key] = SectionMetadata(day_off);

    auto it = section_index.find(title);
    if (it != section_index.end()) {
      sections[it->second].tabs.push_back(*item);
    } else {
      section_index[title] = sections.size();
      ArchiveSection section;
      section.title = title;
      section.sort_key = sort_key;
      section.tabs.push_back(*item);
      sections.push_back(std::move(section));
    }
  }

  std::sort(sections.begin(), sections.end(),
            [](const ArchiveSection& a, const ArchiveSection& b) {
              return a.sort_key < b.sort_key;
            });

  return sections;
}

void MahoSidebarArchiveView::RebuildList(
    const std::vector<ArchiveSection>& sections,
    int visible_count) {
  if (!list_container_) {
    return;
  }

  list_container_->RemoveAllChildViews();

  const bool has_results = !sections.empty();
  scroll_view_->SetVisible(has_results);
  empty_state_view_->SetVisible(!has_results);
  const std::u16string results_summary =
      GetArchiveResultsSummaryText(visible_count,
                                   static_cast<int>(all_archived_tabs_.size()));
  if (scroll_view_) {
    scroll_view_->GetViewAccessibility().SetValue(results_summary);
  }

  if (!has_results) {
    const bool has_tabs = !all_archived_tabs_.empty();
    const bool has_search = search_field_ && !search_field_->GetText().empty();
    const bool has_filter = filter_mode_ != ArchiveFilterMode::kAll;
    static_cast<ArchiveEmptyStateView*>(empty_state_view_.get())
        ->Configure(has_tabs, has_search || has_filter);
    return;
  }

  for (size_t section_idx = 0; section_idx < sections.size(); ++section_idx) {
    const auto& section = sections[section_idx];

    if (section_idx > 0) {
      auto* spacer = list_container_->AddChildView(
          std::make_unique<views::View>());
      spacer->SetPreferredSize(gfx::Size(0, kSectionSpacing));
    }

    list_container_->AddChildView(
        std::make_unique<ArchiveSectionHeaderView>(
            section.title, static_cast<int>(section.tabs.size()), palette_));

    auto* header_spacer = list_container_->AddChildView(
        std::make_unique<views::View>());
    header_spacer->SetPreferredSize(gfx::Size(0, kSectionHeaderSpacing));

    auto* section_container = list_container_->AddChildView(
        std::make_unique<views::View>());
    section_container->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(), kRowSpacing));

    for (size_t tab_idx = 0; tab_idx < section.tabs.size(); ++tab_idx) {
      const auto& tab = section.tabs[tab_idx];
      auto* row = section_container->AddChildView(
          std::make_unique<ArchiveRowView>(
              tab,
              base::BindRepeating(&MahoSidebarArchiveView::OnRestorePressed,
                                  weak_factory_.GetWeakPtr(), tab.tab_id, tab.space_id),
              base::BindRepeating(&MahoSidebarArchiveView::OnDeletePressed,
                                   weak_factory_.GetWeakPtr(), tab.tab_id)));
      row->SetSidebarPalette(palette_);
      row->SetShowsDivider(tab_idx < section.tabs.size() - 1);
    }
  }

  scroll_view_->DeprecatedLayoutImmediately();
}

// static
std::pair<std::u16string, int> MahoSidebarArchiveView::SectionMetadata(
    int day_offset) {
  if (day_offset < 0) {
    return {u"Earlier", INT_MAX};
  }
  switch (day_offset) {
    case 0:
      return {u"Today", 0};
    case 1:
      return {u"Yesterday", 1};
    default:
      if (day_offset <= 7) {
        return {base::UTF8ToUTF16(std::to_string(day_offset) + " days ago"),
                day_offset};
      }
      if (day_offset <= 14) {
        return {u"Last 2 weeks", 8};
      }
      if (day_offset <= 21) {
        return {u"3 weeks ago", 15};
      }
      if (day_offset <= 28) {
        return {u"4 weeks ago", 22};
      }
      return {u"Earlier", INT_MAX};
  }
}

// static
int MahoSidebarArchiveView::DayOffset(base::Time archived_at) {
  if (archived_at.is_null()) {
    return -1;
  }
  base::Time now = base::Time::Now();
  base::Time today_start = now.LocalMidnight();
  base::Time archived_day = archived_at.LocalMidnight();
  int offset = (today_start - archived_day).InDays();
  return std::max(0, offset);
}

void MahoSidebarArchiveView::OnRestorePressed(const std::string& tab_id, const std::string& space_id) {
  if (restore_callback_) {
    restore_callback_.Run(tab_id, space_id);
  }
}

void MahoSidebarArchiveView::OnDeletePressed(const std::string& tab_id) {
  if (delete_callback_) {
    delete_callback_.Run(tab_id);
  }
}

void MahoSidebarArchiveView::OnFilterPressed() {
  filter_menu_model_ = std::make_unique<ui::SimpleMenuModel>(this);
  filter_menu_model_->AddCheckItem(kFilterAll, u"Any time");
  filter_menu_model_->AddCheckItem(kFilterToday, u"Today");
  filter_menu_model_->AddCheckItem(kFilterPastWeek, u"Past week");
  filter_menu_model_->AddCheckItem(kFilterOlder, u"Older");

  filter_menu_runner_ = std::make_unique<views::MenuRunner>(
      filter_menu_model_.get(), views::MenuRunner::CONTEXT_MENU);

  gfx::Rect bounds = filter_button_->GetBoundsInScreen();
  filter_menu_runner_->RunMenuAt(
      filter_button_->GetWidget(), nullptr, bounds,
      views::MenuAnchorPosition::kTopRight,
      ui::mojom::MenuSourceType::kNone);
}

void MahoSidebarArchiveView::UpdateFilterButtonAppearance() {
  if (!filter_button_) {
    return;
  }
  const bool active = filter_mode_ != ArchiveFilterMode::kAll;
  filter_button_->GetViewAccessibility().SetValue(
      active ? u"Filter Active: " + GetFilterModeTitle(filter_mode_)
             : u"Filter Inactive");
  if (active) {
    filter_button_->SetBackground(views::CreateRoundedRectBackground(
        palette_.row_selected, kFilterButtonCornerRadius));
    filter_button_->SetBorder(views::CreateRoundedRectBorder(
        1, kFilterButtonCornerRadius, palette_.outline));
    filter_button_text_color_ = palette_.primary_text;
    filter_button_->SetEnabledTextColors(filter_button_text_color_);
    filter_button_->SetImageModel(
        views::Button::STATE_NORMAL,
        CreateArchivePaneIcon(maho_lucide_icons::kSlidersHorizontalIcon,
                              palette_.primary_text,
                              kFilterButtonIconSize));
  } else {
    filter_button_->SetBackground(views::CreateRoundedRectBackground(
        palette_.row_active, kFilterButtonCornerRadius));
    filter_button_->SetBorder(views::CreateRoundedRectBorder(
        1, kFilterButtonCornerRadius, palette_.outline));
    filter_button_text_color_ = palette_.secondary_text;
    filter_button_->SetEnabledTextColors(filter_button_text_color_);
    filter_button_->SetImageModel(
        views::Button::STATE_NORMAL,
        CreateArchivePaneIcon(maho_lucide_icons::kSlidersHorizontalIcon,
                              palette_.secondary_text,
                              kFilterButtonIconSize));
  }
}

void MahoSidebarArchiveView::OnThemeChanged() {
  views::View::OnThemeChanged();
  SetSidebarPalette(palette_);
}

void MahoSidebarArchiveView::ExecuteCommand(int command_id, int event_flags) {
  switch (command_id) {
    case kFilterAll:
      filter_mode_ = ArchiveFilterMode::kAll;
      break;
    case kFilterToday:
      filter_mode_ = ArchiveFilterMode::kToday;
      break;
    case kFilterPastWeek:
      filter_mode_ = ArchiveFilterMode::kPastWeek;
      break;
    case kFilterOlder:
      filter_mode_ = ArchiveFilterMode::kOlder;
      break;
  }
  UpdateFilterButtonAppearance();
  ApplyFilters();
}

bool MahoSidebarArchiveView::IsCommandIdChecked(int command_id) const {
  switch (command_id) {
    case kFilterAll:
      return filter_mode_ == ArchiveFilterMode::kAll;
    case kFilterToday:
      return filter_mode_ == ArchiveFilterMode::kToday;
    case kFilterPastWeek:
      return filter_mode_ == ArchiveFilterMode::kPastWeek;
    case kFilterOlder:
      return filter_mode_ == ArchiveFilterMode::kOlder;
    default:
      return false;
  }
}

}  // namespace maho
