// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_tab_list_view.h"

#include <functional>
#include <memory>
#include <set>
#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/pickle.h"
#include "base/strings/string_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "base/values.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "content/public/browser/page_navigator.h"
#include "content/public/common/referrer.h"
#include "chrome/browser/ui/tab_ui_helper.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/web_contents.h"
#include "components/tabs/public/tab_interface.h"
#include "maho/browser/ui/context_menu/maho_tab_context_menu.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_state_adapter.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/base/clipboard/clipboard_format_type.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/dragdrop/mojom/drag_drop_types.mojom.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/base/models/image_model.h"
#include "ui/compositor/layer_tree_owner.h"
#include "ui/base/window_open_disposition.h"
#include "ui/base/dragdrop/drop_target_event.h"
#include "ui/gfx/geometry/point.h"
#include "ui/color/color_id.h"
#include "ui/events/event.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/image_button_factory.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/style/typography.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/view_utils.h"
#include "ui/views/vector_icons.h"
#include "url/gurl.h"

#include "maho/browser/ui/views/sidebar/maho_sidebar_drag_util.h"

namespace maho {

constexpr int kRowSpacingDp = 2;
constexpr int kRowHeightDp = 26;
constexpr int kRowCornerRadiusDp = 9;
constexpr int kRowIconSizeDp = 16;
constexpr int kRowIconTextSpacingDp = 6;
constexpr int kRowTrailingSpacingDp = 0;
constexpr int kCloseButtonSizeDp = 16;
constexpr int kSectionSpacingDp = 5;
constexpr int kFolderIndentDp = 13;
constexpr int kFolderRowHeightDp = 26;
constexpr int kSectionSeparatorHeightDp = 8;
constexpr int kFolderGlyphSizeDp = 13;
constexpr int kFolderGlyphContainerSizeDp = 16;
constexpr int kChevronSizeDp = 12;
constexpr int kDragStartThresholdDp = 5;
constexpr int kDropIndicatorThicknessDp = 3;
constexpr int kDropIndicatorOutlineThicknessDp = 1;
constexpr int kDropIndicatorInsetDp = 4;

const gfx::Insets kRootInsets = gfx::Insets::TLBR(0, 0, 0, 0);
const gfx::Insets kInlineRowInsets = gfx::Insets::TLBR(0, 9, 0, 8);
const gfx::Insets kPlaceholderInsets = gfx::Insets::TLBR(6, 9, 6, 9);
const gfx::Insets kNewTabRowMargins = gfx::Insets::TLBR(0, 0, 0, 0);
const gfx::Insets kSectionInsets = gfx::Insets::TLBR(0, 0, 0, 0);

ui::DropTargetEvent ForwardDropTargetEventToView(const ui::DropTargetEvent& event,
                                                 views::View* source,
                                                 views::View* target) {
  gfx::Point target_location = event.location();
  views::View::ConvertPointToTarget(source, target, &target_location);
  return ui::DropTargetEvent(event.data(), gfx::PointF(target_location),
                             event.root_location_f(),
                             event.source_operations());
}

class SidebarDropForwardingLabelButton : public views::LabelButton {
 public:
  SidebarDropForwardingLabelButton(PressedCallback callback,
                                   std::u16string text,
                                   raw_ptr<views::View> drop_target)
      : views::LabelButton(std::move(callback), std::move(text)),
        drop_target_(drop_target) {}

  SidebarDropForwardingLabelButton(const SidebarDropForwardingLabelButton&) =
      delete;
  SidebarDropForwardingLabelButton& operator=(
      const SidebarDropForwardingLabelButton&) = delete;
  ~SidebarDropForwardingLabelButton() override = default;

  bool GetDropFormats(int* formats,
                      std::set<ui::ClipboardFormatType>* format_types) override {
    return drop_target_ && drop_target_->GetDropFormats(formats, format_types);
  }

  bool CanDrop(const ui::OSExchangeData& data) override {
    return drop_target_ && drop_target_->CanDrop(data);
  }

  int OnDragUpdated(const ui::DropTargetEvent& event) override {
    if (!drop_target_) {
      return static_cast<int>(ui::mojom::DragOperation::kNone);
    }
    return drop_target_->OnDragUpdated(
        ForwardDropTargetEventToView(event, this, drop_target_));
  }

  DropCallback GetDropCallback(const ui::DropTargetEvent& event) override {
    if (!drop_target_) {
      return views::LabelButton::GetDropCallback(event);
    }
    return drop_target_->GetDropCallback(
        ForwardDropTargetEventToView(event, this, drop_target_));
  }

  void OnDragExited() override {
    if (drop_target_) {
      drop_target_->OnDragExited();
    }
  }

 private:
  raw_ptr<views::View> drop_target_ = nullptr;
};

class SidebarDropForwardingView : public views::View {
 public:
  explicit SidebarDropForwardingView(raw_ptr<views::View> drop_target)
      : drop_target_(drop_target) {}

  SidebarDropForwardingView(const SidebarDropForwardingView&) = delete;
  SidebarDropForwardingView& operator=(const SidebarDropForwardingView&) =
      delete;
  ~SidebarDropForwardingView() override = default;

  bool GetDropFormats(int* formats,
                      std::set<ui::ClipboardFormatType>* format_types) override {
    return drop_target_ && drop_target_->GetDropFormats(formats, format_types);
  }

  bool CanDrop(const ui::OSExchangeData& data) override {
    return drop_target_ && drop_target_->CanDrop(data);
  }

  int OnDragUpdated(const ui::DropTargetEvent& event) override {
    if (!drop_target_) {
      return static_cast<int>(ui::mojom::DragOperation::kNone);
    }
    return drop_target_->OnDragUpdated(
        ForwardDropTargetEventToView(event, this, drop_target_));
  }

  DropCallback GetDropCallback(const ui::DropTargetEvent& event) override {
    if (!drop_target_) {
      return views::View::GetDropCallback(event);
    }
    return drop_target_->GetDropCallback(
        ForwardDropTargetEventToView(event, this, drop_target_));
  }

  void OnDragExited() override {
    if (drop_target_) {
      drop_target_->OnDragExited();
    }
  }

 private:
  raw_ptr<views::View> drop_target_ = nullptr;
};

class SidebarSeparatorRow : public views::View {
 public:
   SidebarSeparatorRow() {
     SetPreferredSize(gfx::Size(1, kSectionSeparatorHeightDp));
     auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
         views::BoxLayout::Orientation::kVertical,
         gfx::Insets::TLBR(kSectionSeparatorHeightDp / 2, 8,
                           kSectionSeparatorHeightDp / 2, 8),
         0));
     layout->set_cross_axis_alignment(
         views::BoxLayout::CrossAxisAlignment::kStretch);
      auto divider = std::make_unique<views::View>();
      divider->SetPreferredSize(gfx::Size(1, 2));
      divider->SetBackground(
          views::CreateSolidBackground(ui::kColorSysNeutralOutline));
     AddChildView(std::move(divider));
   }

  SidebarSeparatorRow(const SidebarSeparatorRow&) = delete;
  SidebarSeparatorRow& operator=(const SidebarSeparatorRow&) = delete;
  ~SidebarSeparatorRow() override = default;
};

class SidebarSectionDropTarget : public views::View {
 public:
  SidebarSectionDropTarget(bool emphasized, MahoSidebarTabSection section)
      : section_(section) {
    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, kSectionInsets,
        kRowSpacingDp));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);
    SetBackground(nullptr);
    SetBorder(nullptr);
  }

  bool GetDropFormats(int* formats,
                      std::set<ui::ClipboardFormatType>* format_types) override {
    format_types->insert(GetMahoDragFormatType());
    return true;
  }

  bool CanDrop(const ui::OSExchangeData& data) override {
    SidebarDragPayload payload;
    return ReadMahoDragData(data, payload);
  }

  int OnDragUpdated(const ui::DropTargetEvent& event) override {
    drag_over_drop_target_ = true;
    UpdateAppearance();
    return static_cast<int>(ui::mojom::DragOperation::kMove);
  }

  DropCallback GetDropCallback(const ui::DropTargetEvent& event) override {
    return base::BindOnce(
        [](MahoSidebarTabSection section,
           const ui::DropTargetEvent& event,
           ui::mojom::DragOperation& output_drag_op,
           std::unique_ptr<ui::LayerTreeOwner> drag_image_layer_owner) {
          SidebarDragPayload payload;
          if (!ReadMahoDragData(event.data(), payload)) {
            output_drag_op = ui::mojom::DragOperation::kNone;
            return;
          }
          output_drag_op = ui::mojom::DragOperation::kMove;

          if (payload.origin == SidebarDragOrigin::kFavorites &&
              payload.node_kind == SidebarNodeKind::kTab) {
            DispatchShellEvent("unfavorite_tab", {{"tab_id", payload.node_id}});
          }

          if (section == MahoSidebarTabSection::kPinned) {
            if (payload.node_kind == SidebarNodeKind::kTab) {
              if (!payload.source_parent_folder_id.empty()) {
                DispatchShellEvent("move_tab_to_root", {
                    {"space_id", payload.space_id},
                    {"folder_id", payload.source_parent_folder_id},
                    {"tab_id", payload.node_id}});
                DispatchShellEvent("pin_tab", {{"tab_id", payload.node_id}});
              } else {
                DispatchShellEvent("pin_tab", {{"tab_id", payload.node_id}});
              }
            } else {
              DispatchShellEvent("pin_folder", {
                  {"space_id", payload.space_id},
                  {"folder_id", payload.node_id}});
            }
          } else {
            if (payload.node_kind == SidebarNodeKind::kTab) {
              if (!payload.source_parent_folder_id.empty()) {
                DispatchShellEvent("move_tab_to_root", {
                    {"space_id", payload.space_id},
                    {"folder_id", payload.source_parent_folder_id},
                    {"tab_id", payload.node_id}});
              } else {
                DispatchShellEvent("unpin_tab", {{"tab_id", payload.node_id}});
              }
            } else {
              DispatchShellEvent("unpin_folder", {
                  {"space_id", payload.space_id},
                  {"folder_id", payload.node_id}});
            }
          }
        },
        section_);
  }

  void OnDragExited() override {
    drag_over_drop_target_ = false;
    UpdateAppearance();
  }

  bool is_drop_indicator_visible_for_testing() const {
    return drag_over_drop_target_;
  }

  MahoSidebarTabSection section_for_testing() const { return section_; }

 private:
  void UpdateAppearance() {
    SetBackground(drag_over_drop_target_
                      ? views::CreateRoundedRectBackground(
                            ui::kColorSysSurface4, kRowCornerRadiusDp)
                      : nullptr);
    SetBorder(drag_over_drop_target_
                  ? views::CreateRoundedRectBorder(
                        kDropIndicatorOutlineThicknessDp,
                        kRowCornerRadiusDp, ui::kColorSysPrimary)
                  : nullptr);
  }

  const MahoSidebarTabSection section_;
  bool drag_over_drop_target_ = false;
};

views::View* AddSectionContainer(views::View* parent, bool emphasized,
                                 MahoSidebarTabSection section) {
  auto container = std::make_unique<SidebarSectionDropTarget>(emphasized, section);
  auto* raw = container.get();
  parent->AddChildView(
      std::unique_ptr<views::View>(container.release()));
  return raw;
}

class SidebarInlineRow : public views::LabelButton {

 public:
  SidebarInlineRow(PressedCallback callback,
                   std::u16string text,
                   bool active,
                   bool subtle)
      : views::LabelButton(std::move(callback), text),
        active_(active),
        subtle_(subtle) {
    SetHorizontalAlignment(gfx::ALIGN_LEFT);
    SetBorder(views::CreateEmptyBorder(kInlineRowInsets));
    SetMinSize(gfx::Size(0, kRowHeightDp));
    SetMaxSize(
        gfx::Size(sidebar_layout::kRuntimeRailWidthMaxDp, kRowHeightDp));
    SetFocusRingCornerRadius(kRowCornerRadiusDp);
    SetImageLabelSpacing(0);
    SetRequestFocusOnPress(false);
    label()->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    label()->SetMultiLine(false);
    label()->SetElideBehavior(gfx::ELIDE_TAIL);
    label()->SetTextStyle(views::style::STYLE_BODY_4);
    label()->SetAutoColorReadabilityEnabled(false);
    UpdateAppearance();
  }

  SidebarInlineRow(const SidebarInlineRow&) = delete;
  SidebarInlineRow& operator=(const SidebarInlineRow&) = delete;
  ~SidebarInlineRow() override = default;

  void OnThemeChanged() override {
    views::LabelButton::OnThemeChanged();
    UpdateAppearance();
  }

  void StateChanged(ButtonState old_state) override {
    views::LabelButton::StateChanged(old_state);
    UpdateAppearance();
  }

 private:
  void UpdateAppearance() {
    const bool hovered = GetState() == STATE_HOVERED ||
                         GetState() == STATE_PRESSED;
    if (active_) {
      SetBackground(views::CreateRoundedRectBackground(
          hovered ? ui::kColorSysSurface3 : ui::kColorSysSurface1,
          kRowCornerRadiusDp));
      SetBorder(views::CreatePaddedBorder(
          views::CreateRoundedRectBorder(
              1, kRowCornerRadiusDp, ui::kColorSysSurfaceVariant),
          kInlineRowInsets));
      SetEnabledTextColors(ui::kColorSysOnSurface);
      label()->SetTextStyle(views::style::STYLE_BODY_4_MEDIUM);
      return;
    }

    SetBackground(hovered ? views::CreateRoundedRectBackground(
                                ui::kColorSysSurface2, kRowCornerRadiusDp)
                          : nullptr);
    SetBorder(views::CreateEmptyBorder(kInlineRowInsets));
    SetEnabledTextColors(subtle_ ? ui::kColorSysOnSurfaceSubtle
                                 : ui::kColorSysOnSurface);
    label()->SetTextStyle(subtle_ ? views::style::STYLE_BODY_4
                                  : views::style::STYLE_BODY_4_MEDIUM);
  }

  const bool active_;
  const bool subtle_;
};

std::u16string NormalizeRenamedTitle(const std::u16string& text) {
  std::u16string trimmed = text;
  base::TrimWhitespace(trimmed, base::TRIM_ALL, &trimmed);
  return trimmed;
}

views::Label* AddPlaceholderLabel(views::View* parent, std::u16string text) {
  auto label = std::make_unique<views::Label>(std::move(text));
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label->SetAutoColorReadabilityEnabled(false);
  label->SetEnabledColor(ui::kColorSysOnSurfaceSubtle);
  label->SetTextStyle(views::style::STYLE_BODY_4);
  label->SetBorder(views::CreateEmptyBorder(kPlaceholderInsets));
  return parent->AddChildView(std::move(label));
}

SidebarInlineRow* AddInlineRow(views::View* parent,
                               std::u16string text,
                               views::Button::PressedCallback callback,
                               bool active,
                               bool subtle) {
  auto button = std::make_unique<SidebarInlineRow>(std::move(callback), text,
                                                   active, subtle);
  auto* button_ptr = button.get();
  button_ptr->SetProperty(views::kMarginsKey, kNewTabRowMargins);
  parent->AddChildView(
      std::unique_ptr<views::View>(button.release()));
  return button_ptr;
}

}  // namespace

namespace maho {

views::View::DropCallback GetSectionDropCallbackForTesting(
    MahoSidebarTabSection section,
    const ui::DropTargetEvent& event) {
  SidebarSectionDropTarget target(/*emphasized=*/section ==
                                      MahoSidebarTabSection::kPinned,
                                  section);
  return target.GetDropCallback(event);
}

SidebarSectionDropIndicatorStateForTesting
GetSectionDropIndicatorStateForTesting(MahoSidebarTabSection section,
                                             const ui::DropTargetEvent& event) {
  SidebarSectionDropTarget target(/*emphasized=*/section ==
                                      MahoSidebarTabSection::kPinned,
                                  section);
  SidebarSectionDropIndicatorStateForTesting state;
  target.OnDragUpdated(event);
  state.visible_after_update = target.is_drop_indicator_visible_for_testing();
  target.OnDragExited();
  state.visible_after_exit = target.is_drop_indicator_visible_for_testing();
  return state;
}

BEGIN_METADATA(MahoSidebarTabListView)
END_METADATA
BEGIN_METADATA(SidebarTabRowView)
END_METADATA
BEGIN_METADATA(SidebarFolderRowView)
END_METADATA

namespace {

template <typename ViewType>
ViewType* RecursiveFindView(views::View* root,
                            std::function<bool(ViewType*)> predicate) {
  if (!root) return nullptr;
  if (auto* typed = views::AsViewClass<ViewType>(root)) {
    if (predicate(typed)) return typed;
  }
  for (views::View* child : root->children()) {
    if (auto* found = RecursiveFindView<ViewType>(child, predicate)) {
      return found;
    }
  }
  return nullptr;
}

}  // namespace

views::View* MahoSidebarTabListView::FindSectionDropTargetForTesting(
    MahoSidebarTabSection section) {
  if (section == MahoSidebarTabSection::kPinned) return pinned_section_target_;
  if (section == MahoSidebarTabSection::kNormal) return normal_section_target_;
  return nullptr;
}

SidebarTabRowView* MahoSidebarTabListView::FindTabRowByIdForTesting(
    const std::string& tab_id) {
  return RecursiveFindView<SidebarTabRowView>(
      tab_rows_,
      [&tab_id](SidebarTabRowView* row) {
        return row->tab_id_for_testing() == tab_id;
      });
}

SidebarFolderRowView* MahoSidebarTabListView::FindFolderRowByIdForTesting(
    const std::string& folder_id) {
  return RecursiveFindView<SidebarFolderRowView>(
      tab_rows_,
      [&folder_id](SidebarFolderRowView* row) {
        return row->folder_id_for_testing() == folder_id;
      });
}

// --- Custom clipboard format for Maho sidebar drag ---

const ui::ClipboardFormatType& GetMahoDragFormatType() {
  static const base::NoDestructor<ui::ClipboardFormatType> kFormat(
      ui::ClipboardFormatType::Deserialize("application/x-maho-sidebar-drag"));
  return *kFormat;
}

std::string SerializeDragPayload(const SidebarDragPayload& payload) {
  base::DictValue dict = base::DictValue()
      .Set("node_kind", payload.node_kind == SidebarNodeKind::kTab ? "tab" : "folder")
      .Set("node_id", payload.node_id)
      .Set("origin", static_cast<int>(payload.origin))
      .Set("space_id", payload.space_id)
      .Set("source_parent_folder_id", payload.source_parent_folder_id)
      .Set("source_folder_child_index", payload.source_folder_child_index);
  std::string json;
  base::JSONWriter::Write(dict, &json);
  return json;
}

bool DeserializeDragPayload(const std::string& data, SidebarDragPayload& out) {
  auto parsed = base::JSONReader::Read(data, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return false;
  }
  const auto& dict = parsed->GetDict();
  const std::string* kind_str = dict.FindString("node_kind");
  if (!kind_str) {
    return false;
  }
  out.node_kind = (*kind_str == "folder") ? SidebarNodeKind::kFolder : SidebarNodeKind::kTab;
  const std::string* nid = dict.FindString("node_id");
  if (nid) out.node_id = *nid;
  std::optional<int> origin = dict.FindInt("origin");
  if (origin.has_value()) out.origin = static_cast<SidebarDragOrigin>(*origin);
  const std::string* sid = dict.FindString("space_id");
  if (sid) out.space_id = *sid;
  const std::string* source_parent_folder_id =
      dict.FindString("source_parent_folder_id");
  if (source_parent_folder_id) {
    out.source_parent_folder_id = *source_parent_folder_id;
  }
  std::optional<int> source_folder_child_index =
      dict.FindInt("source_folder_child_index");
  if (source_folder_child_index.has_value()) {
    out.source_folder_child_index = *source_folder_child_index;
  }
  return true;
}

void WriteMahoDragData(const SidebarDragPayload& payload,
                       ui::OSExchangeData* data) {
  std::string serialized = SerializeDragPayload(payload);
  base::Pickle pickle;
  pickle.WriteString(serialized);
  data->SetPickledData(GetMahoDragFormatType(), pickle);
}

bool ReadMahoDragData(const ui::OSExchangeData& data,
                      SidebarDragPayload& out) {
  std::optional<base::Pickle> pickle = data.GetPickledData(GetMahoDragFormatType());
  if (!pickle.has_value()) {
    return false;
  }
  base::PickleIterator iter(pickle.value());
  std::string serialized;
  if (!iter.ReadString(&serialized)) {
    return false;
  }
  return DeserializeDragPayload(serialized, out);
}

// --- SidebarTabRowView ---

SidebarTabRowView::SidebarTabRowView(const SidebarTreeNode& node,
                                     ui::ImageModel favicon,
                                     Browser* browser,
                                     MahoSidebarTabSection section,
                                     std::string active_space_id,
                                     std::u16string display_title,
                                     views::Button::PressedCallback activate_callback,
                                     views::Button::PressedCallback close_callback,
                                     base::RepeatingCallback<void(const std::string&, const std::u16string&)>
                                         rename_callback)
    : active_(node.is_active),
      pinned_(node.is_pinned),
      loading_(node.is_loading),
      audible_(node.is_audible),
      muted_(node.is_muted),
      tab_id_(node.tab_id),
      section_(section),
      space_id_(std::move(active_space_id)),
      source_parent_folder_id_(node.parent_folder_id),
      folder_child_index_(node.folder_child_index),
      browser_(browser),
      tab_index_(node.tab_strip_index),
      committed_title_(std::move(display_title)),
      rename_callback_(std::move(rename_callback)) {
  const int indent = node.depth * kFolderIndentDp;
  auto row_insets = gfx::Insets::TLBR(0, 9 + indent, 0, 7);

  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, row_insets,
      kRowIconTextSpacingDp));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  favicon_view_ = AddChildView(std::make_unique<views::ImageView>());
  if (loading_) {
    favicon_view_->SetImage(ui::ImageModel::FromVectorIcon(
        vector_icons::kSyncChromeRefreshIcon, ui::kColorSysOnSurfaceSubtle,
        kRowIconSizeDp));
  } else {
    favicon_view_->SetImage(std::move(favicon));
  }
  favicon_view_->SetImageSize(gfx::Size(kRowIconSizeDp, kRowIconSizeDp));

  std::u16string display_text = committed_title_.empty() ? node.host : committed_title_;
  title_button_ = static_cast<views::LabelButton*>(AddChildView(
      std::unique_ptr<views::View>(
          std::make_unique<SidebarDropForwardingLabelButton>(
              std::move(activate_callback), std::move(display_text), this)
              .release())));
  title_button_->SetBorder(nullptr);
  title_button_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  title_button_->SetMinSize(gfx::Size(0, kRowHeightDp));
  title_button_->SetMaxSize(
      gfx::Size(sidebar_layout::kRuntimeRailWidthMaxDp, kRowHeightDp));
  title_button_->SetFocusRingCornerRadius(kRowCornerRadiusDp);
  title_button_->SetRequestFocusOnPress(false);
  title_button_->SetImageLabelSpacing(0);
  title_button_->set_drag_controller(this);
  title_button_->SetTextColor(views::Button::STATE_NORMAL,
                              ui::kColorSysOnSurface);
  title_button_->SetTextColor(views::Button::STATE_HOVERED,
                              ui::kColorSysOnSurface);
  title_button_->SetTextColor(views::Button::STATE_PRESSED,
                              ui::kColorSysOnSurface);
  title_button_->SetTextColor(views::Button::STATE_DISABLED,
                              ui::kColorSysOnSurfaceSubtle);
  layout->SetFlexForView(title_button_, 1);

  auto title_field = std::make_unique<views::Textfield>();
  title_field->SetVisible(false);
  title_field->SetBorder(nullptr);
  title_field->SetBackgroundColor(SK_ColorTRANSPARENT);
  title_field->SetTextColor(ui::kColorSysOnSurface);
  title_field->SetController(this);
  title_field->SetAccessibleName(u"Rename tab");
  title_field_ = AddChildView(std::move(title_field));
  layout->SetFlexForView(title_field_, 1);

  if (muted_) {
    audio_indicator_view_ = AddChildView(std::make_unique<views::ImageView>());
    audio_indicator_view_->SetImage(ui::ImageModel::FromVectorIcon(
        vector_icons::kVolumeOffChromeRefreshIcon, ui::kColorSysOnSurfaceSubtle,
        12));
    audio_indicator_view_->SetImageSize(gfx::Size(12, 12));
    audio_indicator_view_->SetTooltipText(u"Tab muted");
  } else if (audible_) {
    audio_indicator_view_ = AddChildView(std::make_unique<views::ImageView>());
    audio_indicator_view_->SetImage(ui::ImageModel::FromVectorIcon(
        vector_icons::kVolumeUpChromeRefreshIcon, ui::kColorSysOnSurfaceSubtle,
        12));
    audio_indicator_view_->SetImageSize(gfx::Size(12, 12));
    audio_indicator_view_->SetTooltipText(u"Tab playing audio");
  }

  close_button_ = AddChildView(views::CreateVectorImageButtonWithNativeTheme(
      std::move(close_callback), vector_icons::kCloseChromeRefreshIcon,
      10, ui::kColorSysOnSurfaceSubtle, ui::kColorSysOnSurface,
      ui::kColorSysOnSurface));
  close_button_->SetBorder(nullptr);
  close_button_->SetPreferredSize(
      gfx::Size(kCloseButtonSizeDp, kCloseButtonSizeDp));
  close_button_->SetImageHorizontalAlignment(views::ImageButton::ALIGN_CENTER);
  close_button_->SetImageVerticalAlignment(views::ImageButton::ALIGN_MIDDLE);
  close_button_->SetTooltipText(u"Close tab");
  close_button_->SetAccessibleName(u"Close tab");
  close_button_->SetProperty(
      views::kMarginsKey,
      gfx::Insets::TLBR(0, kRowTrailingSpacingDp, 0, 0));
  close_button_->SetVisible(false);
  views::InstallCircleHighlightPathGenerator(close_button_);

  set_context_menu_controller(this);
  title_button_->set_context_menu_controller(this);

  set_drag_controller(this);

  UpdateAppearance();
}

SidebarTabRowView::~SidebarTabRowView() = default;

void SidebarTabRowView::OnThemeChanged() {
  views::View::OnThemeChanged();
  UpdateAppearance();
}

bool SidebarTabRowView::OnMousePressed(const ui::MouseEvent& event) {
  if ((event.flags() & ui::EF_IS_DOUBLE_CLICK) && !is_editing_title_) {
    BeginTitleEditing();
    return true;
  }
  return views::View::OnMousePressed(event);
}

void SidebarTabRowView::OnMouseEntered(const ui::MouseEvent& event) {
  views::View::OnMouseEntered(event);
  UpdateAppearance();
}

void SidebarTabRowView::OnMouseExited(const ui::MouseEvent& event) {
  views::View::OnMouseExited(event);
  UpdateAppearance();
}

bool SidebarTabRowView::HandleKeyEvent(views::Textfield* sender,
                                       const ui::KeyEvent& event) {
  if (event.type() != ui::EventType::kKeyPressed || sender != title_field_) {
    return false;
  }

  switch (event.key_code()) {
    case ui::VKEY_RETURN:
      EndTitleEditing(true);
      return true;
    case ui::VKEY_ESCAPE:
      EndTitleEditing(false);
      return true;
    default:
      return false;
  }
}

void SidebarTabRowView::ShowContextMenuForViewImpl(
    views::View* source,
    const gfx::Point& point,
    ui::mojom::MenuSourceType source_type) {
  if (is_editing_title_) {
    EndTitleEditing(false);
  }
  if (!browser_) {
    return;
  }

  context_menu_ = std::make_unique<MahoTabContextMenu>(browser_, tab_index_);
  menu_model_ = context_menu_->BuildMenuModel();
  menu_runner_ = std::make_unique<views::MenuRunner>(menu_model_.get(),
                                                      views::MenuRunner::CONTEXT_MENU);
  menu_runner_->RunMenuAt(source->GetWidget(), nullptr,
                          gfx::Rect(point, gfx::Size()),
                          views::MenuAnchorPosition::kTopLeft, source_type);
}

bool SidebarTabRowView::GetDropFormats(
    int* formats,
    std::set<ui::ClipboardFormatType>* format_types) {
  *formats = ui::OSExchangeData::URL;
  format_types->insert(GetMahoDragFormatType());
  return true;
}

bool SidebarTabRowView::CanDrop(const ui::OSExchangeData& data) {
  SidebarDragPayload payload;
  if (ReadMahoDragData(data, payload)) {
    return true;
  }
  drop_url_ = GURL();
  drop_title_.clear();
  const auto urls = data.GetURLs(ui::FilenameToURLPolicy::CONVERT_FILENAMES);
  if (urls.empty()) {
    return false;
  }
  drop_url_ = urls.front().url;
  drop_title_ = urls.front().title;
  return drop_url_.is_valid();
}

int SidebarTabRowView::OnDragUpdated(const ui::DropTargetEvent& event) {
  SidebarDragPayload payload;
  if (ReadMahoDragData(event.data(), payload)) {
    active_drop_zone_ = ResolveDropZone(event, payload);
    if (active_drop_zone_ == DropZoneForTesting::kNone) {
      drag_over_drop_target_ = false;
      UpdateAppearance();
      return static_cast<int>(ui::mojom::DragOperation::kNone);
    }
  } else {
    active_drop_zone_ = DropZoneForTesting::kBefore;
  }
  drag_over_drop_target_ = true;
  UpdateAppearance();
  return static_cast<int>(ui::mojom::DragOperation::kMove);
}

views::View::DropCallback SidebarTabRowView::GetDropCallback(
    const ui::DropTargetEvent& event) {
  SidebarDragPayload drag_payload;
  const bool has_drag_payload = ReadMahoDragData(event.data(), drag_payload);
  const DropZoneForTesting drop_zone =
      has_drag_payload ? ResolveDropZone(event, drag_payload)
                       : DropZoneForTesting::kBefore;
  return base::BindOnce(
      [](Browser* browser, GURL url, std::string tab_id,
         MahoSidebarTabSection section, std::string space_id,
         std::string target_parent_folder_id, int target_folder_child_index,
         DropZoneForTesting drop_zone, const ui::DropTargetEvent& event,
         ui::mojom::DragOperation& output_drag_op,
         std::unique_ptr<ui::LayerTreeOwner> drag_image_layer_owner) {
        SidebarDragPayload payload;
        if (ReadMahoDragData(event.data(), payload)) {
          output_drag_op = ui::mojom::DragOperation::kMove;
          if (payload.node_kind == SidebarNodeKind::kTab &&
              !payload.node_id.empty() && !tab_id.empty() &&
              payload.node_id != tab_id) {
            if (payload.origin == SidebarDragOrigin::kFavorites) {
              DispatchShellEvent("unfavorite_tab",
                                {{"tab_id", payload.node_id}});
            }
            const bool same_folder_drop =
                !target_parent_folder_id.empty() &&
                payload.source_parent_folder_id == target_parent_folder_id &&
                payload.source_folder_child_index >= 0 &&
                target_folder_child_index >= 0;
            if (same_folder_drop) {
              const bool is_no_op_before =
                  drop_zone == DropZoneForTesting::kBefore &&
                  payload.source_folder_child_index ==
                      target_folder_child_index - 1;
              const bool is_no_op_after =
                  drop_zone == DropZoneForTesting::kAfter &&
                  payload.source_folder_child_index ==
                      target_folder_child_index + 1;
              if (is_no_op_before || is_no_op_after) {
                output_drag_op = ui::mojom::DragOperation::kNone;
                return;
              }

              int to = target_folder_child_index;
              if (drop_zone == DropZoneForTesting::kAfter) {
                to = target_folder_child_index + 1;
              }
              if (payload.source_folder_child_index < target_folder_child_index) {
                to -= 1;
              }
              DispatchShellEventEx("reorder_tab_in_folder", {
                  ShellEventField("space_id", space_id),
                  ShellEventField("folder_id", target_parent_folder_id),
                  ShellEventField("tab_id", payload.node_id),
                  ShellEventField("to", to)});
            } else if (!payload.source_parent_folder_id.empty()) {
              DispatchShellEvent("move_tab_to_root", {
                  {"space_id", space_id},
                  {"folder_id", payload.source_parent_folder_id},
                  {"tab_id", payload.node_id},
                  {"before_tab_id", tab_id}});
            } else {
              DispatchShellEvent("reorder_tab", {
                  {"tab_id", payload.node_id},
                  {"before_tab_id", tab_id}});
            }
          }
          return;
        }
        if (!browser || !url.is_valid()) {
          output_drag_op = ui::mojom::DragOperation::kNone;
          return;
        }
        output_drag_op = ui::mojom::DragOperation::kCopy;
        browser->OpenURL(content::OpenURLParams(
                             url, content::Referrer(),
                             WindowOpenDisposition::CURRENT_TAB,
                             ui::PAGE_TRANSITION_LINK, false),
                         base::NullCallback());
      },
      browser_, drop_url_, tab_id_, section_, space_id_,
      source_parent_folder_id_, folder_child_index_, drop_zone);
}

void SidebarTabRowView::OnDragExited() {
  drop_url_ = GURL();
  active_drop_zone_ = DropZoneForTesting::kNone;
  drag_over_drop_target_ = false;
  UpdateAppearance();
}

void SidebarTabRowView::BeginTitleEditing() {
  if (tab_id_.empty() || is_editing_title_ || !title_field_) {
    return;
  }

  is_editing_title_ = true;
  title_button_->SetVisible(false);
  title_field_->SetVisible(true);
  title_field_->SetText(committed_title_);
  title_field_->RequestFocus();
  title_field_->SelectAll(false);
  UpdateAppearance();
}

void SidebarTabRowView::EndTitleEditing(bool commit) {
  if (!is_editing_title_ || !title_field_) {
    return;
  }

  std::u16string next_title = committed_title_;
  if (commit) {
    next_title =
        NormalizeRenamedTitle(std::u16string(title_field_->GetText()));
    committed_title_ = next_title;
    ApplyDisplayedTitle(committed_title_);
    if (rename_callback_) {
      rename_callback_.Run(tab_id_, committed_title_);
    }
  } else {
    title_field_->SetText(committed_title_);
    ApplyDisplayedTitle(committed_title_);
  }

  is_editing_title_ = false;
  title_field_->SetVisible(false);
  title_button_->SetVisible(true);
  RequestFocus();
  UpdateAppearance();
}

void SidebarTabRowView::ApplyDisplayedTitle(const std::u16string& title) {
  if (title_button_) {
    title_button_->SetText(title);
  }
}

void SidebarTabRowView::WriteDragDataForView(views::View* sender,
                                             const gfx::Point& press_pt,
                                             ui::OSExchangeData* data) {
  SidebarDragPayload payload;
  payload.node_kind = SidebarNodeKind::kTab;
  payload.node_id = tab_id_;
  payload.origin = pinned_ ? SidebarDragOrigin::kPinnedSection
                           : SidebarDragOrigin::kNormalSection;
  payload.space_id = space_id_;
  payload.source_parent_folder_id = source_parent_folder_id_;
  payload.source_folder_child_index = folder_child_index_;
  WriteMahoDragData(payload, data);
  data->provider().SetDragImage(CreateSidebarDragImage(sender),
                                press_pt.OffsetFromOrigin());
}

int SidebarTabRowView::GetDragOperationsForView(views::View* sender,
                                                const gfx::Point& p) {
  return static_cast<int>(ui::mojom::DragOperation::kMove);
}

bool SidebarTabRowView::CanStartDragForView(views::View* sender,
                                            const gfx::Point& press_pt,
                                            const gfx::Point& current_pt) {
  return (press_pt - current_pt).Length() >= kDragStartThresholdDp;
}

void SidebarTabRowView::UpdateAppearance() {
  const bool hovered = IsMouseHovered();
  const bool show_close_button = hovered && !is_editing_title_;
  if (drag_over_drop_target_) {
    SetBackground(views::CreateRoundedRectBackground(ui::kColorSysSurface4,
                                                     kRowCornerRadiusDp));
    SetBorder(views::CreateSolidSidedBorder(
        active_drop_zone_ == DropZoneForTesting::kAfter
            ? gfx::Insets::TLBR(0, kDropIndicatorInsetDp,
                                kDropIndicatorThicknessDp,
                                kDropIndicatorInsetDp)
            : gfx::Insets::TLBR(kDropIndicatorThicknessDp,
                                kDropIndicatorInsetDp, 0,
                                kDropIndicatorInsetDp),
        ui::kColorSysPrimary));
  } else if (active_) {
    SetBackground(views::CreateRoundedRectBackground(
        hovered ? ui::kColorSysSurface3 : ui::kColorSysSurface1,
        kRowCornerRadiusDp));
    SetBorder(views::CreateRoundedRectBorder(
        1, kRowCornerRadiusDp, ui::kColorSysSurfaceVariant));
  } else if (hovered) {
    SetBackground(views::CreateRoundedRectBackground(ui::kColorSysSurface2,
                                                     kRowCornerRadiusDp));
    SetBorder(nullptr);
  } else {
    SetBackground(nullptr);
    SetBorder(nullptr);
  }

  title_button_->SetTextColor(views::Button::STATE_NORMAL,
                              ui::kColorSysOnSurface);
  title_button_->SetTextColor(views::Button::STATE_HOVERED,
                              ui::kColorSysOnSurface);
  title_button_->SetTextColor(views::Button::STATE_PRESSED,
                              ui::kColorSysOnSurface);
  if (title_field_) {
    title_field_->SetVisible(is_editing_title_);
  }
  title_button_->SetVisible(!is_editing_title_);
  close_button_->SetVisible(show_close_button);
  if (audio_indicator_view_) {
    audio_indicator_view_->SetVisible(!show_close_button);
  }
}

SidebarTabRowView::DropZoneForTesting SidebarTabRowView::ResolveDropZone(
    const ui::DropTargetEvent& event,
    const SidebarDragPayload& payload) const {
  if (payload.node_kind != SidebarNodeKind::kTab || payload.node_id.empty() ||
      payload.node_id == tab_id_) {
    return DropZoneForTesting::kNone;
  }

  const bool same_folder_drop = !source_parent_folder_id_.empty() &&
                                payload.source_parent_folder_id ==
                                    source_parent_folder_id_ &&
                                payload.source_folder_child_index >= 0 &&
                                folder_child_index_ >= 0;
  if (!same_folder_drop) {
    return DropZoneForTesting::kBefore;
  }

  const int row_height = std::max(height(), 1);
  const int midpoint = std::max(row_height / 2, 1);
  return event.location().y() < midpoint ? DropZoneForTesting::kBefore
                                         : DropZoneForTesting::kAfter;
}

// --- SidebarFolderRowView ---

SidebarFolderRowView::SidebarFolderRowView(
    const SidebarTreeNode& node,
    MahoSidebarTabSection /*section*/,
    std::string active_space_id,
    views::Button::PressedCallback toggle_callback,
    std::string next_sibling_folder_id,
    std::string parent_folder_id)
    : folder_id_(node.folder_id),
      space_id_(std::move(active_space_id)),
      folder_is_pinned_(node.folder_is_pinned),
      depth_(node.depth),
      next_sibling_folder_id_(std::move(next_sibling_folder_id)),
      parent_folder_id_(std::move(parent_folder_id)),
      is_expanded_(node.is_expanded) {
  const int indent = node.depth * kFolderIndentDp;
  auto row_insets = gfx::Insets::TLBR(0, 9 + indent, 0, 7);

  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, row_insets,
      kRowIconTextSpacingDp));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  std::u16string folder_text = node.folder_name;
  if (folder_text.empty()) {
    folder_text = u"Folder";
  }

  chevron_ = AddChildView(std::make_unique<views::ImageView>());
  chevron_->SetImageSize(gfx::Size(kChevronSizeDp, kChevronSizeDp));

  auto* folder_glyph_container = AddChildView(std::make_unique<views::View>());
  folder_glyph_container->SetPreferredSize(
      gfx::Size(kFolderGlyphContainerSizeDp, kFolderGlyphContainerSizeDp));
  auto* folder_glyph_layout = folder_glyph_container->SetLayoutManager(
      std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kHorizontal,
                                         gfx::Insets(), 0));
  folder_glyph_layout->set_main_axis_alignment(
      views::BoxLayout::MainAxisAlignment::kCenter);
  folder_glyph_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  folder_icon_ = folder_glyph_container->AddChildView(
      std::make_unique<views::ImageView>());
  folder_icon_->SetImageSize(gfx::Size(kFolderGlyphSizeDp, kFolderGlyphSizeDp));

  label_button_ = static_cast<views::LabelButton*>(AddChildView(
      std::unique_ptr<views::View>(
          std::make_unique<SidebarDropForwardingLabelButton>(
              std::move(toggle_callback), folder_text, this)
              .release())));
  label_button_->SetBorder(nullptr);
  label_button_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label_button_->SetMinSize(gfx::Size(0, kFolderRowHeightDp));
  label_button_->SetMaxSize(
      gfx::Size(sidebar_layout::kRuntimeRailWidthMaxDp, kFolderRowHeightDp));
  label_button_->SetFocusRingCornerRadius(kRowCornerRadiusDp);
  label_button_->SetRequestFocusOnPress(false);
  label_button_->SetTextColor(views::Button::STATE_NORMAL,
                              ui::kColorSysOnSurface);
  label_button_->SetTextColor(views::Button::STATE_HOVERED,
                              ui::kColorSysOnSurface);
  label_button_->SetTextColor(views::Button::STATE_PRESSED,
                              ui::kColorSysOnSurface);
  layout->SetFlexForView(label_button_, 1);

  set_drag_controller(this);

  UpdateAppearance();
}

SidebarFolderRowView::~SidebarFolderRowView() = default;

void SidebarFolderRowView::OnThemeChanged() {
  views::View::OnThemeChanged();
  UpdateAppearance();
}

void SidebarFolderRowView::OnMouseEntered(const ui::MouseEvent& event) {
  views::View::OnMouseEntered(event);
  UpdateAppearance();
}

void SidebarFolderRowView::OnMouseExited(const ui::MouseEvent& event) {
  views::View::OnMouseExited(event);
  UpdateAppearance();
}

bool SidebarFolderRowView::GetDropFormats(
    int* formats,
    std::set<ui::ClipboardFormatType>* format_types) {
  *formats = ui::OSExchangeData::URL;
  format_types->insert(GetMahoDragFormatType());
  return true;
}

bool SidebarFolderRowView::CanDrop(const ui::OSExchangeData& data) {
  SidebarDragPayload payload;
  if (ReadMahoDragData(data, payload)) {
    return true;
  }
  const auto urls = data.GetURLs(ui::FilenameToURLPolicy::CONVERT_FILENAMES);
  return !urls.empty() && urls.front().url.is_valid();
}

int SidebarFolderRowView::OnDragUpdated(const ui::DropTargetEvent& event) {
  SidebarDragPayload payload;
  if (!ReadMahoDragData(event.data(), payload)) {
    active_drop_zone_ = DropZoneForTesting::kNone;
    drag_over_drop_target_ = false;
    UpdateAppearance();
    return static_cast<int>(ui::mojom::DragOperation::kNone);
  }
  active_drop_zone_ = ResolveDropZone(event, payload);
  if (active_drop_zone_ == DropZoneForTesting::kNone) {
    drag_over_drop_target_ = false;
    UpdateAppearance();
    return static_cast<int>(ui::mojom::DragOperation::kNone);
  }
  drag_over_drop_target_ = true;
  UpdateAppearance();
  return static_cast<int>(ui::mojom::DragOperation::kMove);
}

views::View::DropCallback SidebarFolderRowView::GetDropCallback(
    const ui::DropTargetEvent& event) {
  SidebarDragPayload payload;
  DropZoneForTesting drop_zone = DropZoneForTesting::kNone;
  if (ReadMahoDragData(event.data(), payload)) {
    drop_zone = ResolveDropZone(event, payload);
  }

  return base::BindOnce(
      [](std::string folder_id, std::string space_id, Browser* browser_unused,
         DropZoneForTesting drop_zone, std::string next_sibling_folder_id,
         std::string parent_folder_id,
         const ui::DropTargetEvent& event,
         ui::mojom::DragOperation& output_drag_op,
         std::unique_ptr<ui::LayerTreeOwner> drag_image_layer_owner) {
        SidebarDragPayload payload;
        if (!ReadMahoDragData(event.data(), payload)) {
          output_drag_op = ui::mojom::DragOperation::kNone;
          return;
        }
        output_drag_op = ui::mojom::DragOperation::kMove;
        if (payload.node_kind == SidebarNodeKind::kTab) {
          if (payload.origin == SidebarDragOrigin::kFavorites) {
            DispatchShellEvent("unfavorite_tab", {{"tab_id", payload.node_id}});
          }
          DispatchShellEvent("move_tab_to_folder", {
              {"space_id", space_id},
              {"folder_id", folder_id},
              {"tab_id", payload.node_id}});
        } else if (payload.node_kind == SidebarNodeKind::kFolder) {
          const bool dragging_self = payload.node_id == folder_id;
          const bool is_root_folder_target = parent_folder_id.empty();

          if (!dragging_self && drop_zone == DropZoneForTesting::kBefore) {
            if (is_root_folder_target) {
              DispatchShellEvent("reorder_folder", {
                  {"space_id", space_id},
                  {"folder_id", payload.node_id},
                  {"before_folder_id", folder_id}});
              return;
            }

            if (!payload.source_parent_folder_id.empty() &&
                payload.source_parent_folder_id == parent_folder_id) {
              DispatchShellEvent("reorder_folder", {
                  {"space_id", space_id},
                  {"folder_id", payload.node_id},
                  {"parent_folder_id", parent_folder_id},
                  {"before_folder_id", folder_id}});
              return;
            }
          }

          if (!dragging_self && drop_zone == DropZoneForTesting::kAfter) {
            if (is_root_folder_target) {
              std::vector<std::pair<std::string, std::string>> fields = {
                  {"space_id", space_id},
                  {"folder_id", payload.node_id}};
              if (!next_sibling_folder_id.empty() &&
                  next_sibling_folder_id != payload.node_id) {
                fields.push_back({"before_folder_id", next_sibling_folder_id});
              }
              DispatchShellEvent("reorder_folder", fields);
              return;
            }

            if (!payload.source_parent_folder_id.empty() &&
                payload.source_parent_folder_id == parent_folder_id) {
              std::vector<std::pair<std::string, std::string>> fields = {
                  {"space_id", space_id},
                  {"folder_id", payload.node_id},
                  {"parent_folder_id", parent_folder_id}};
              if (!next_sibling_folder_id.empty() &&
                  next_sibling_folder_id != payload.node_id) {
                fields.push_back({"before_folder_id", next_sibling_folder_id});
              }
              DispatchShellEvent("reorder_folder", fields);
              return;
            }
          }

          if (dragging_self) {
            output_drag_op = ui::mojom::DragOperation::kNone;
            return;
          }

          DispatchShellEvent("move_folder_into_folder", {
              {"space_id", space_id},
              {"folder_id", payload.node_id},
              {"target_folder_id", folder_id}});
        }
      },
      folder_id_, space_id_, static_cast<Browser*>(nullptr), drop_zone,
      next_sibling_folder_id_, parent_folder_id_);
}

void SidebarFolderRowView::OnDragExited() {
  active_drop_zone_ = DropZoneForTesting::kNone;
  drag_over_drop_target_ = false;
  UpdateAppearance();
}

void SidebarFolderRowView::WriteDragDataForView(views::View* sender,
                                                const gfx::Point& press_pt,
                                                ui::OSExchangeData* data) {
  SidebarDragPayload payload;
  payload.node_kind = SidebarNodeKind::kFolder;
  payload.node_id = folder_id_;
  payload.origin = folder_is_pinned_ ? SidebarDragOrigin::kPinnedSection
                                     : SidebarDragOrigin::kNormalSection;
  payload.space_id = space_id_;
  WriteMahoDragData(payload, data);
  data->provider().SetDragImage(CreateSidebarDragImage(sender),
                                press_pt.OffsetFromOrigin());
}

int SidebarFolderRowView::GetDragOperationsForView(views::View* sender,
                                                   const gfx::Point& p) {
  return static_cast<int>(ui::mojom::DragOperation::kMove);
}

bool SidebarFolderRowView::CanStartDragForView(views::View* sender,
                                               const gfx::Point& press_pt,
                                               const gfx::Point& current_pt) {
  return (press_pt - current_pt).Length() >= kDragStartThresholdDp;
}

void SidebarFolderRowView::UpdateAppearance() {
  const bool hovered = IsMouseHovered();
  const bool show_drop_target = drag_over_drop_target_;
  if (chevron_) {
    chevron_->SetImage(ui::ImageModel::FromVectorIcon(
        is_expanded_ ? vector_icons::kExpandMoreIcon
                     : vector_icons::kSubmenuArrowChromeRefreshIcon,
        show_drop_target ? ui::kColorSysPrimary : ui::kColorSysOnSurfaceSubtle,
        kChevronSizeDp));
  }
  if (folder_icon_) {
    folder_icon_->SetImage(ui::ImageModel::FromVectorIcon(
        vector_icons::kFolderChromeRefreshIcon,
        ui::kColorSysPrimary,
        kFolderGlyphSizeDp));
  }
  if (label_button_) {
    label_button_->SetTextColor(views::Button::STATE_NORMAL,
                                show_drop_target ? ui::kColorSysPrimary
                                                 : ui::kColorSysOnSurface);
    label_button_->SetTextColor(views::Button::STATE_HOVERED,
                                show_drop_target ? ui::kColorSysPrimary
                                                 : ui::kColorSysOnSurface);
    label_button_->SetTextColor(views::Button::STATE_PRESSED,
                                show_drop_target ? ui::kColorSysPrimary
                                                 : ui::kColorSysOnSurface);
  }
  if (show_drop_target) {
    SetBackground(views::CreateRoundedRectBackground(ui::kColorSysSurface4,
                                                     kRowCornerRadiusDp));
    SetBorder(active_drop_zone_ == DropZoneForTesting::kInto
                  ? views::CreateRoundedRectBorder(
                        kDropIndicatorOutlineThicknessDp,
                        kRowCornerRadiusDp, ui::kColorSysPrimary)
                  : views::CreateSolidSidedBorder(
                        active_drop_zone_ == DropZoneForTesting::kAfter
                            ? gfx::Insets::TLBR(0, kDropIndicatorInsetDp,
                                                kDropIndicatorThicknessDp,
                                                kDropIndicatorInsetDp)
                            : gfx::Insets::TLBR(kDropIndicatorThicknessDp,
                                                kDropIndicatorInsetDp, 0,
                                                kDropIndicatorInsetDp),
                        ui::kColorSysPrimary));
    return;
  }
  SetBackground(hovered ? views::CreateRoundedRectBackground(
                              ui::kColorSysSurface2, kRowCornerRadiusDp)
                        : nullptr);
  SetBorder(nullptr);
}

SidebarFolderRowView::DropZoneForTesting SidebarFolderRowView::ResolveDropZone(
    const ui::DropTargetEvent& event,
    const SidebarDragPayload& payload) const {
  if (payload.node_kind != SidebarNodeKind::kFolder) {
    return DropZoneForTesting::kInto;
  }

  if (payload.node_id.empty() || payload.node_id == folder_id_) {
    return DropZoneForTesting::kNone;
  }

  const bool is_root_folder_target = depth_ == 0;
  if (!is_root_folder_target && parent_folder_id_.empty()) {
    return DropZoneForTesting::kInto;
  }

  const int row_height = std::max(height(), 1);
  const int top_threshold = std::max(row_height / 4, 1);
  const int bottom_threshold = std::max((row_height * 3) / 4, top_threshold);
  const int y = event.location().y();

  if (y < top_threshold) {
    return DropZoneForTesting::kBefore;
  }
  if (y >= bottom_threshold) {
    return DropZoneForTesting::kAfter;
  }
  return DropZoneForTesting::kInto;
}

// --- MahoSidebarTabListView ---

MahoSidebarTabListView::MahoSidebarTabListView(Browser* browser)
    : browser_(browser) {
  auto* root_layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, kRootInsets, kRowSpacingDp));
  root_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  auto scroll_view = std::make_unique<views::ScrollView>();
  scroll_view->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  scroll_view->SetVerticalScrollBarMode(
      views::ScrollView::ScrollBarMode::kHiddenButEnabled);

  auto rows = std::make_unique<views::View>();
  auto* rows_layout = rows->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), kSectionSpacingDp));
  rows_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  tab_rows_ = rows.get();

  scroll_view->SetContents(std::move(rows));
  scroll_view_ = AddChildView(std::move(scroll_view));
  root_layout->SetFlexForView(scroll_view_, 1, true);
}

MahoSidebarTabListView::~MahoSidebarTabListView() = default;

void MahoSidebarTabListView::Update(const MahoSidebarTabListModel& model,
                                    Browser* browser) {
  browser_ = browser;
  active_space_id_ = model.active_space_id;
  PruneLocalTitleCache(model);
  last_model_ = model;
  has_last_model_ = true;

  // Rebuild stable WebContents* → core tab_id map for observer lookups.
  contents_to_core_id_.clear();
  if (browser) {
    TabStripModel* strip = browser->tab_strip_model();
    auto populate = [&](const std::vector<SidebarTreeNode>& nodes) {
      for (const auto& node : nodes) {
        if (node.kind == SidebarNodeKind::kTab &&
            node.tab_strip_index >= 0 && !node.tab_id.empty() &&
            strip->ContainsIndex(node.tab_strip_index)) {
          content::WebContents* wc =
              strip->GetWebContentsAt(node.tab_strip_index);
          if (wc) {
            contents_to_core_id_[wc] = node.tab_id;
          }
        }
        for (const auto& child : node.children) {
          if (child.kind == SidebarNodeKind::kTab &&
              child.tab_strip_index >= 0 && !child.tab_id.empty() &&
              strip->ContainsIndex(child.tab_strip_index)) {
            content::WebContents* wc =
                strip->GetWebContentsAt(child.tab_strip_index);
            if (wc) {
              contents_to_core_id_[wc] = child.tab_id;
            }
          }
        }
      }
    };
    populate(model.pinned_tree);
    populate(model.normal_tree);
  }

  RebuildRows(model, browser);
}

bool MahoSidebarTabListView::GetDropFormats(
    int* formats,
    std::set<ui::ClipboardFormatType>* format_types) {
  *formats = ui::OSExchangeData::URL;
  format_types->insert(GetMahoDragFormatType());
  return true;
}

bool MahoSidebarTabListView::CanDrop(const ui::OSExchangeData& data) {
  SidebarDragPayload payload;
  if (ReadMahoDragData(data, payload)) {
    return true;
  }
  const auto urls = data.GetURLs(ui::FilenameToURLPolicy::CONVERT_FILENAMES);
  return !urls.empty() && urls.front().url.is_valid();
}

int MahoSidebarTabListView::OnDragUpdated(const ui::DropTargetEvent& event) {
  return static_cast<int>(ui::mojom::DragOperation::kCopy);
}

views::View::DropCallback MahoSidebarTabListView::GetDropCallback(
    const ui::DropTargetEvent& event) {
  return base::BindOnce(
      [](Browser* browser,
         const ui::DropTargetEvent& event,
         ui::mojom::DragOperation& output_drag_op,
         std::unique_ptr<ui::LayerTreeOwner> drag_image_layer_owner) {
        SidebarDragPayload payload;
        if (ReadMahoDragData(event.data(), payload)) {
          output_drag_op = ui::mojom::DragOperation::kMove;
          if (payload.origin == SidebarDragOrigin::kFavorites &&
              payload.node_kind == SidebarNodeKind::kTab) {
            DispatchShellEvent("unfavorite_tab", {{"tab_id", payload.node_id}});
          }
          return;
        }
        const auto urls = event.data().GetURLs(
            ui::FilenameToURLPolicy::CONVERT_FILENAMES);
        if (!browser || urls.empty() || !urls.front().url.is_valid()) {
          output_drag_op = ui::mojom::DragOperation::kNone;
          return;
        }
        output_drag_op = ui::mojom::DragOperation::kCopy;
        browser->OpenURL(content::OpenURLParams(
                             urls.front().url, content::Referrer(),
                             WindowOpenDisposition::NEW_FOREGROUND_TAB,
                             ui::PAGE_TRANSITION_LINK, false),
                         base::NullCallback());
      },
      browser_);
}

void MahoSidebarTabListView::OnDragExited() {}

void MahoSidebarTabListView::RebuildRows(const MahoSidebarTabListModel& model,
                                         Browser* browser) {
  tab_rows_->RemoveAllChildViews();
  pinned_section_target_ = nullptr;
  normal_section_target_ = nullptr;

  const bool has_tree = !active_space_id_.empty() ||
                        !model.pinned_tree.empty() || !model.normal_tree.empty();
  const bool has_flat = !model.pinned_tabs.empty() || !model.tabs.empty();

  if (!has_tree && !has_flat) {
    new_tab_button_ = AddInlineRow(
        tab_rows_, u"+ New Tab",
        base::BindRepeating(&MahoSidebarTabListView::OpenNewTab,
                            weak_factory_.GetWeakPtr()),
        false, true);
    AddPlaceholderLabel(tab_rows_, u"No tabs open");
    return;
  }

  if (has_tree) {
    RebuildTreeSection(tab_rows_, model.pinned_tree,
                       MahoSidebarTabSection::kPinned, browser);
    new_tab_button_ = AddInlineRow(
        tab_rows_, u"+ New Tab",
        base::BindRepeating(&MahoSidebarTabListView::OpenNewTab,
                            weak_factory_.GetWeakPtr()),
        false, true);
    if (!model.normal_tree.empty()) {
      tab_rows_->AddChildView(
          std::unique_ptr<views::View>(std::make_unique<SidebarSeparatorRow>()
                                           .release()));
    }
    RebuildTreeSection(tab_rows_, model.normal_tree,
                       MahoSidebarTabSection::kNormal, browser);
  } else {
    // Fallback flat rendering when no core tree available.
    if (!model.pinned_tabs.empty()) {
      auto* section = AddSectionContainer(tab_rows_, true, MahoSidebarTabSection::kPinned);
      auto* rows_container = section->AddChildView(
          std::unique_ptr<views::View>(
              std::make_unique<SidebarDropForwardingView>(section).release()));
      auto* rows_layout = rows_container->SetLayoutManager(
          std::make_unique<views::BoxLayout>(
              views::BoxLayout::Orientation::kVertical, gfx::Insets(), kRowSpacingDp));
      rows_layout->set_cross_axis_alignment(
          views::BoxLayout::CrossAxisAlignment::kStretch);
      for (const auto& row : model.pinned_tabs) {
        SidebarTreeNode node;
        node.kind = SidebarNodeKind::kTab;
        node.tab_id = row.tab_id;
        node.tab_strip_index = row.index;
        node.is_active = row.is_active;
        node.is_pinned = row.is_pinned;
        node.is_loading = row.is_loading;
        node.is_audible = row.is_audible;
        node.is_muted = row.is_muted;
        node.title = row.title;
        node.host = row.host;
        content::WebContents* contents =
            browser && browser->tab_strip_model()
                ? browser->tab_strip_model()->GetWebContentsAt(row.index)
                : nullptr;
        tabs::TabInterface* tab_iface =
            contents ? tabs::TabInterface::GetFromContents(contents) : nullptr;
        TabUIHelper* tab_ui =
            tab_iface ? TabUIHelper::From(tab_iface) : nullptr;
        ui::ImageModel fav = tab_ui ? tab_ui->GetFavicon() : ui::ImageModel();
        rows_container->AddChildView(std::make_unique<SidebarTabRowView>(
            node, std::move(fav), browser, MahoSidebarTabSection::kPinned,
            active_space_id_, ResolveTabDisplayText(node),
            base::BindRepeating(&MahoSidebarTabListView::ActivateTab,
                                weak_factory_.GetWeakPtr(), row.index),
            base::BindRepeating(&MahoSidebarTabListView::CloseTab,
                                weak_factory_.GetWeakPtr(), row.index),
            base::BindRepeating(&MahoSidebarTabListView::CommitTabTitleChange,
                                weak_factory_.GetWeakPtr())));
      }
    }
    new_tab_button_ = AddInlineRow(
        tab_rows_, u"+ New Tab",
        base::BindRepeating(&MahoSidebarTabListView::OpenNewTab,
                            weak_factory_.GetWeakPtr()),
        false, true);
    if (!model.tabs.empty()) {
      tab_rows_->AddChildView(
          std::unique_ptr<views::View>(std::make_unique<SidebarSeparatorRow>()
                                           .release()));
    }
    if (!model.tabs.empty()) {
      auto* section = AddSectionContainer(tab_rows_, false, MahoSidebarTabSection::kNormal);
      auto* rows_container = section->AddChildView(
          std::unique_ptr<views::View>(
              std::make_unique<SidebarDropForwardingView>(section).release()));
      auto* rows_layout = rows_container->SetLayoutManager(
          std::make_unique<views::BoxLayout>(
              views::BoxLayout::Orientation::kVertical, gfx::Insets(), kRowSpacingDp));
      rows_layout->set_cross_axis_alignment(
          views::BoxLayout::CrossAxisAlignment::kStretch);
      for (const auto& row : model.tabs) {
        SidebarTreeNode node;
        node.kind = SidebarNodeKind::kTab;
        node.tab_id = row.tab_id;
        node.tab_strip_index = row.index;
        node.is_active = row.is_active;
        node.is_pinned = row.is_pinned;
        node.is_loading = row.is_loading;
        node.is_audible = row.is_audible;
        node.is_muted = row.is_muted;
        node.title = row.title;
        node.host = row.host;
        content::WebContents* contents =
            browser && browser->tab_strip_model()
                ? browser->tab_strip_model()->GetWebContentsAt(row.index)
                : nullptr;
        tabs::TabInterface* tab_iface =
            contents ? tabs::TabInterface::GetFromContents(contents) : nullptr;
        TabUIHelper* tab_ui =
            tab_iface ? TabUIHelper::From(tab_iface) : nullptr;
        ui::ImageModel fav = tab_ui ? tab_ui->GetFavicon() : ui::ImageModel();
        rows_container->AddChildView(std::make_unique<SidebarTabRowView>(
            node, std::move(fav), browser, MahoSidebarTabSection::kNormal,
            active_space_id_, ResolveTabDisplayText(node),
            base::BindRepeating(&MahoSidebarTabListView::ActivateTab,
                                weak_factory_.GetWeakPtr(), row.index),
            base::BindRepeating(&MahoSidebarTabListView::CloseTab,
                                weak_factory_.GetWeakPtr(), row.index),
            base::BindRepeating(&MahoSidebarTabListView::CommitTabTitleChange,
                                weak_factory_.GetWeakPtr())));
      }
    }
  }

}

void MahoSidebarTabListView::RebuildTreeSection(
    views::View* parent,
    const std::vector<SidebarTreeNode>& nodes,
    MahoSidebarTabSection section,
    Browser* browser) {
  const bool emphasized = (section == MahoSidebarTabSection::kPinned);
  auto* section_container = AddSectionContainer(parent, emphasized, section);

  if (section == MahoSidebarTabSection::kPinned) {
    pinned_section_target_ = section_container;
  } else if (section == MahoSidebarTabSection::kNormal) {
    normal_section_target_ = section_container;
  }

  auto* rows_container = section_container->AddChildView(
      std::unique_ptr<views::View>(
          std::make_unique<SidebarDropForwardingView>(section_container)
              .release()));
  auto* rows_layout = rows_container->SetLayoutManager(
      std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical, gfx::Insets(), kRowSpacingDp));
  rows_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  if (!nodes.empty()) {
    RebuildTreeNodes(rows_container, nodes, section, browser);
  }
}

void MahoSidebarTabListView::RebuildTreeNodes(
    views::View* parent,
    const std::vector<SidebarTreeNode>& nodes,
    MahoSidebarTabSection section,
    Browser* browser) {
  for (size_t index = 0; index < nodes.size(); ++index) {
    const auto& node = nodes[index];
    if (node.kind == SidebarNodeKind::kFolder) {
      std::string next_sibling_folder_id;
      const std::string sibling_parent_folder_id = node.parent_of_folder_id;
      for (size_t next_index = index + 1; next_index < nodes.size();
           ++next_index) {
        const auto& next_node = nodes[next_index];
        if (next_node.kind == SidebarNodeKind::kFolder &&
            next_node.parent_of_folder_id == sibling_parent_folder_id &&
            !next_node.folder_id.empty()) {
          next_sibling_folder_id = next_node.folder_id;
          break;
        }
      }

      parent->AddChildView(std::make_unique<SidebarFolderRowView>(
          node, section, active_space_id_,
          base::BindRepeating(&MahoSidebarTabListView::ToggleFolderExpanded,
                              weak_factory_.GetWeakPtr(), node.folder_id),
          next_sibling_folder_id, sibling_parent_folder_id));

      if (node.is_expanded && !node.children.empty()) {
        RebuildTreeNodes(parent, node.children, section, browser);
      }
    } else {
      content::WebContents* contents = nullptr;
      tabs::TabInterface* tab_iface = nullptr;
      if (browser && browser->tab_strip_model() && node.tab_strip_index >= 0) {
        contents = browser->tab_strip_model()->GetWebContentsAt(
            node.tab_strip_index);
        if (contents) {
          tab_iface = tabs::TabInterface::GetFromContents(contents);
        }
      }
      TabUIHelper* tab_ui =
          tab_iface ? TabUIHelper::From(tab_iface) : nullptr;
      ui::ImageModel fav = tab_ui ? tab_ui->GetFavicon() : ui::ImageModel();

      auto* row_view = parent->AddChildView(std::make_unique<SidebarTabRowView>(
          node, std::move(fav), browser, section, active_space_id_,
          ResolveTabDisplayText(node),
          base::BindRepeating(&MahoSidebarTabListView::ActivateTab,
                              weak_factory_.GetWeakPtr(),
                              node.tab_strip_index),
          base::BindRepeating(&MahoSidebarTabListView::CloseTab,
                              weak_factory_.GetWeakPtr(),
                              node.tab_strip_index),
          base::BindRepeating(&MahoSidebarTabListView::CommitTabTitleChange,
                              weak_factory_.GetWeakPtr())));
      row_view->SetTooltipText(GetNodeTooltipText(node));
    }
  }
}

std::u16string MahoSidebarTabListView::GetNodeDisplayText(
    const SidebarTreeNode& node) const {
  return ResolveTabDisplayText(node);
}

std::u16string MahoSidebarTabListView::GetNodeTooltipText(
    const SidebarTreeNode& node) const {
  if (node.host.empty() || node.host == node.title) {
    return node.title;
  }
  return node.title + u"\n" + node.host;
}

void MahoSidebarTabListView::ActivateTab(int tab_index) {
  TabStripModel* model = browser_ ? browser_->tab_strip_model() : nullptr;
  if (!model || !model->ContainsIndex(tab_index)) {
    return;
  }

  model->ActivateTabAt(tab_index);
}

void MahoSidebarTabListView::CloseTab(int tab_index) {
  TabStripModel* model = browser_ ? browser_->tab_strip_model() : nullptr;
  if (!model || !model->ContainsIndex(tab_index)) {
    return;
  }

  model->CloseWebContentsAt(tab_index, TabCloseTypes::CLOSE_USER_GESTURE);
}

void MahoSidebarTabListView::OpenNewTab(const ui::Event& event) {
  if (browser_) {
    chrome::NewTab(browser_);
  }
}

void MahoSidebarTabListView::ToggleFolderExpanded(
    const std::string& folder_id) {
  if (!active_space_id_.empty() && !folder_id.empty()) {
    DispatchShellEvent("toggle_folder_expanded", {
        {"space_id", active_space_id_},
        {"folder_id", folder_id}});
  }
}

void MahoSidebarTabListView::CommitTabTitleChange(
    const std::string& tab_id,
    const std::u16string& new_title) {
  local_custom_titles_[tab_id] = new_title;
  MahoCore* core = maho::GetCore();
  if (!core || tab_id.empty()) {
    return;
  }

  std::string utf8_title = base::UTF16ToUTF8(new_title);
  maho_core_set_tab_custom_title(core, tab_id.c_str(),
                                 utf8_title.empty() ? nullptr
                                                    : utf8_title.c_str());
}

std::u16string MahoSidebarTabListView::ResolveTabDisplayText(
    const SidebarTreeNode& node) const {
  auto local_it = local_custom_titles_.find(node.tab_id);
  if (local_it != local_custom_titles_.end()) {
    return local_it->second;
  }
  if (!node.custom_title.empty()) {
    return node.custom_title;
  }
  if (!node.title.empty() && node.title != u"New Tab") {
    return node.title;
  }
  if (!node.host.empty()) {
    return node.host;
  }
  return node.title;
}

void MahoSidebarTabListView::PruneLocalTitleCache(
    const MahoSidebarTabListModel& model) {
  std::set<std::string> live_tab_ids;
  auto collect_tree = [&](const auto& self,
                          const std::vector<SidebarTreeNode>& nodes) -> void {
    for (const auto& node : nodes) {
      if (node.kind == SidebarNodeKind::kTab) {
        live_tab_ids.insert(node.tab_id);
      } else {
        self(self, node.children);
      }
    }
  };
  collect_tree(collect_tree, model.pinned_tree);
  collect_tree(collect_tree, model.normal_tree);
  for (const auto& row : model.pinned_tabs) {
    live_tab_ids.insert(row.tab_id);
  }
  for (const auto& row : model.tabs) {
    live_tab_ids.insert(row.tab_id);
  }

  for (auto it = local_custom_titles_.begin(); it != local_custom_titles_.end();) {
    if (!live_tab_ids.contains(it->first)) {
      it = local_custom_titles_.erase(it);
    } else {
      ++it;
    }
  }
}

std::string MahoSidebarTabListView::FindCoreTabIdByWebContents(
    content::WebContents* contents) const {
  if (!contents) {
    return std::string();
  }
  auto it = contents_to_core_id_.find(contents);
  if (it != contents_to_core_id_.end()) {
    return it->second;
  }
  return std::string();
}

}  // namespace maho
