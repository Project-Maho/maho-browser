// Copyright 2026 Maho Browser. All rights reserved.

#include "maho_sidebar_tab_list_view.h"

#include "maho/browser/ui/views/sidebar/maho_sidebar_view.h"

#include <functional>
#include <memory>
#include <set>
#include <utility>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/functional/callback_helpers.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/pickle.h"
#include "base/strings/string_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "base/values.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_navigator.h"
#include "chrome/browser/ui/browser_navigator_params.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/location_bar/location_bar_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_container_view.h"
#include "content/public/browser/page_navigator.h"
#include "content/public/common/referrer.h"
#include "chrome/browser/ui/tab_ui_helper.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/split_tab_metrics.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/web_contents.h"
#include "components/tabs/public/tab_interface.h"
#include "maho/browser/ui/tab_preview/maho_tab_preview_controller.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_folder_hover_controller.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "maho/browser/ui/context_menu/maho_folder_context_menu.h"
#include "maho/browser/ui/context_menu/maho_space_context_menu.h"
#include "maho/browser/ui/context_menu/maho_tab_context_menu.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/browser/maho_tab_id_helper.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_dnd_events.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_state_adapter.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/base/clipboard/clipboard_format_type.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/dragdrop/mojom/drag_drop_types.mojom.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/base/models/image_model.h"
#include "ui/compositor/layer.h"
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
#include "maho/browser/ui/views/maho_lucide_icons/vector_icons.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/box_layout_view.h"
#include "ui/views/style/typography.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/view_utils.h"
#include "ui/views/vector_icons.h"
#include "url/gurl.h"
#include "base/timer/timer.h"
#include "ui/views/controls/scroll_view.h"

#include "maho/browser/ui/views/sidebar/maho_sidebar_drag_util.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_drop_planner.h"

namespace maho {
void ForwardDragToAutoScroller(views::View* child,
                               const ui::DropTargetEvent& event);
void ForwardDragEnded(views::View* child);
}  // namespace maho

namespace {

using namespace maho;  // NOLINT(build/namespaces)

constexpr int kRowSpacingDp = 0;
constexpr int kRowHeightDp = 32;
constexpr int kRowCornerRadiusDp = 6;
constexpr int kRowIconSizeDp = 16;
constexpr int kCloseButtonSizeDp = 24;
constexpr int kCloseIconSizeDp = 14;
constexpr int kRowIconTextSpacingDp = 6;
constexpr int kRowTrailingSpacingDp = 0;
constexpr int kSectionSpacingDp = 4;
constexpr int kFolderIndentDp = 13;
constexpr int kFolderRowHeightDp = 33;
constexpr int kSectionSeparatorHeightDp = 8;
constexpr int kSectionSeparatorLineHeightDp = 2;
constexpr int kFolderGlyphSizeDp = 13;
constexpr int kAutoScrollEdgeDp = 24;
constexpr int kAutoScrollStepDp = 4;
constexpr int kAutoScrollHz = 30;
constexpr int kFolderGlyphContainerSizeDp = 16;
constexpr int kChevronSizeDp = 12;
constexpr int kDragStartThresholdDp = 5;
constexpr int kDropIndicatorThicknessDp = 2;
constexpr int kDropIndicatorOutlineThicknessDp = 1;

const gfx::Insets kRootInsets = gfx::Insets::TLBR(0, 0, 0, 0);
const gfx::Insets kInlineRowInsets = gfx::Insets::TLBR(0, 10, 0, 8);
const gfx::Insets kPlaceholderInsets = gfx::Insets::TLBR(1, 12, 0, 9);
const gfx::Insets kNewTabRowMargins = gfx::Insets::TLBR(1, 0, 0, 0);
const gfx::Insets kSectionInsets = gfx::Insets::TLBR(0, 0, 0, 0);
const gfx::Insets kSectionSeparatorInsets = gfx::Insets::TLBR(3, 8, 3, 8);

ui::DropTargetEvent ForwardDropTargetEventToView(const ui::DropTargetEvent& event,
                                                 views::View* source,
                                                 views::View* target) {
  gfx::Point target_location = event.location();
  views::View::ConvertPointToTarget(source, target, &target_location);
  return ui::DropTargetEvent(event.data(), gfx::PointF(target_location),
                             event.root_location_f(),
                             event.source_operations());
}

void NotifySidebarDragEnded(views::View* start_view) {
  views::View* v = start_view;
  while (v) {
    if (v->GetClassName() == std::string("MahoSidebarView")) {
      MahoSidebarView* sidebar_view = static_cast<MahoSidebarView*>(v);
      sidebar_view->OnDragEnded();
      break;
    }
    v = v->parent();
  }
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

  void OnDragDone() override {
    views::LabelButton::OnDragDone();
    NotifySidebarDragEnded(this);
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
  METADATA_HEADER(SidebarSeparatorRow, views::View)

 public:
  SidebarSeparatorRow() {
    SetPreferredSize(gfx::Size(1, kSectionSeparatorHeightDp));
    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, kSectionSeparatorInsets,
        0));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);

    auto divider = std::make_unique<views::View>();
    divider->SetPreferredSize(gfx::Size(0, kSectionSeparatorLineHeightDp));
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
  explicit SidebarSectionDropTarget(MahoSidebarTabSection section)
      : section_(section) {
    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, kSectionInsets,
        kRowSpacingDp));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);
    SetBackground(nullptr);
    SetBorder(nullptr);
  }

  void SetFavoritesDropAcceptedCallback(
      base::RepeatingCallback<void(const std::string&)> cb) {
    on_favorites_drop_accepted_ = std::move(cb);
  }

  void SetPopulated(bool populated) {
    populated_ = populated;
  }

  bool GetDropFormats(int* formats,
                      std::set<ui::ClipboardFormatType>* format_types) override {
    format_types->insert(GetMahoDragFormatType());
    return true;
  }

  // CanDrop() stays permissive even when the section is populated so that drag
  // events keep propagating through SidebarDropForwardingView to row and
  // insertion-lane descendants. The populated-section rejection is applied
  // later in OnDragUpdated() / GetDropCallback() once the final target is known.
  bool CanDrop(const ui::OSExchangeData& data) override {
    SidebarDragPayload payload;
    return ReadMahoDragData(data, payload);
  }

  int OnDragUpdated(const ui::DropTargetEvent& event) override {
    ForwardDragToAutoScroller(this, event);
    if (populated_) {
      return static_cast<int>(ui::mojom::DragOperation::kNone);
    }
    drag_over_drop_target_ = true;
    UpdateAppearance();
    return static_cast<int>(ui::mojom::DragOperation::kMove);
  }

  DropCallback GetDropCallback(const ui::DropTargetEvent& event) override {
    ForwardDragEnded(this);
    if (populated_) {
      return base::BindOnce(
          [](const ui::DropTargetEvent&,
             ui::mojom::DragOperation& output_drag_op,
             std::unique_ptr<ui::LayerTreeOwner>) {
            output_drag_op = ui::mojom::DragOperation::kNone;
          });
    }
    return base::BindOnce(
        [](MahoSidebarTabSection section,
           base::RepeatingCallback<void(const std::string&)>
               on_favorites_drop_accepted,
           const ui::DropTargetEvent& event,
           ui::mojom::DragOperation& output_drag_op,
           std::unique_ptr<ui::LayerTreeOwner> drag_image_layer_owner) {
          SidebarDragPayload payload;
          if (!ReadMahoDragData(event.data(), payload)) {
            output_drag_op = ui::mojom::DragOperation::kNone;
            return;
          }
          output_drag_op = ui::mojom::DragOperation::kMove;

          const SidebarDropPlan plan = PlanSectionDrop(section, payload);
          ExecuteDropPlan(plan);

          if (plan.favorite_transition.kind ==
                  SidebarDropPlan::FavoriteTransition::Kind::kUnfavorite &&
              on_favorites_drop_accepted) {
            on_favorites_drop_accepted.Run(payload.node_id);
          }
        },
        section_, on_favorites_drop_accepted_);
  }

  void OnDragExited() override {
    ForwardDragEnded(this);
    drag_over_drop_target_ = false;
    UpdateAppearance();
  }

  void SetMinDropTargetHeight(int height_dp) {
    SetPreferredSize(gfx::Size(0, height_dp));
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
  bool populated_ = false;
  base::RepeatingCallback<void(const std::string&)> on_favorites_drop_accepted_;
};

bool IsSidebarEligibleUrl(const GURL& url) {
  if (!url.is_valid() || url.is_empty()) {
    return false;
  }
  if (url.SchemeIs("about")) {
    return false;
  }
  if (url.SchemeIs("chrome")) {
    if (url.host() == "newtab" || url.host() == "downloads" ||
        url.host() == "history") {
      return false;
    }
  }
  return true;
}

const GURL& GetVisibleOrCommittedSidebarUrl(content::WebContents* contents) {
  CHECK(contents);
  return contents->GetVisibleURL().is_empty() ? contents->GetLastCommittedURL()
                                              : contents->GetVisibleURL();
}

MahoSidebarTabListModel BuildLiveStripFallbackTabListModel(
    Browser* browser,
    const std::string& active_space_id) {
  MahoSidebarTabListModel fallback_model;
  fallback_model.active_space_id = active_space_id;

  TabStripModel* strip = browser ? browser->tab_strip_model() : nullptr;
  if (!strip) {
    return fallback_model;
  }

  fallback_model.active_tab.index = strip->active_index();
  if (strip->ContainsIndex(fallback_model.active_tab.index)) {
    if (content::WebContents* active_contents =
            strip->GetWebContentsAt(fallback_model.active_tab.index)) {
      fallback_model.active_tab.title = active_contents->GetTitle();
      fallback_model.active_tab.host = base::UTF8ToUTF16(
          GetVisibleOrCommittedSidebarUrl(active_contents).host());
      if (auto* helper = MahoTabIdHelper::FromWebContents(active_contents);
          helper && !helper->stable_tab_id().empty()) {
        fallback_model.active_tab.tab_id = helper->stable_tab_id();
      }
    }
  }

  // This fallback model intentionally flattens live eligible tabs into pinned
  // and normal root rows. It is only used to preserve the scaffold during the
  // brief window where the core-driven sidebar tree is transiently empty.
  for (int i = 0; i < strip->count(); ++i) {
    content::WebContents* contents = strip->GetWebContentsAt(i);
    if (!contents) {
      continue;
    }

    const GURL& url = GetVisibleOrCommittedSidebarUrl(contents);
    if (!IsSidebarEligibleUrl(url)) {
      continue;
    }

    auto* helper = MahoTabIdHelper::FromWebContents(contents);
    if (!helper || helper->stable_tab_id().empty()) {
      continue;
    }

    SidebarTreeNode node;
    node.kind = SidebarNodeKind::kTab;
    node.tab_id = helper->stable_tab_id();
    node.tab_strip_index = i;
    node.is_active = strip->active_index() == i;
    node.is_pinned = strip->IsTabPinned(i);
    node.title = contents->GetTitle();
    node.host = base::UTF8ToUTF16(url.host());
    node.is_loading = contents->IsLoading();
    node.is_audible = contents->IsCurrentlyAudible();
    node.is_muted = contents->IsAudioMuted();

    if (node.is_pinned) {
      fallback_model.pinned_tree.push_back(std::move(node));
    } else {
      fallback_model.normal_tree.push_back(std::move(node));
    }
  }

  return fallback_model;
}

views::View* AddSectionContainer(views::View* parent,
                                 MahoSidebarTabSection section) {
  auto container = std::make_unique<SidebarSectionDropTarget>(section);
  auto* raw = container.get();
  parent->AddChildView(
      std::unique_ptr<views::View>(container.release()));
  return raw;
}

bool HasTabStripContext(Browser* browser) {
  return browser && browser->tab_strip_model();
}

BEGIN_METADATA(SidebarSeparatorRow)
END_METADATA

}  // namespace

namespace maho {

base::DictValue MakeRootItem(SidebarNodeKind kind, const std::string& id);
base::DictValue MakeInsertionPointBefore(SidebarNodeKind target_kind,
                                         const std::string& target_id);
base::DictValue MakeInsertionPointAppend();
void DispatchReorderRootItem(const std::string& space_id,
                             SidebarNodeKind item_kind,
                             const std::string& item_id,
                             base::DictValue insertion_point);

class SidebarInsertionLaneView : public views::View {
   METADATA_HEADER(SidebarInsertionLaneView, views::View)

 public:
  SidebarInsertionLaneView(std::string space_id,
                           std::string item_id,
                           SidebarNodeKind target_kind,
                           std::string target_id,
                           bool append = false,
                           std::string target_parent_folder_id = std::string(),
                           int target_folder_child_index = -1,
                           MahoSidebarTabSection section =
                               MahoSidebarTabSection::kNormal)
      : space_id_(std::move(space_id)),
        item_id_(std::move(item_id)),
        target_kind_(target_kind),
        target_id_(std::move(target_id)),
        append_(append),
        target_parent_folder_id_(std::move(target_parent_folder_id)),
        target_folder_child_index_(target_folder_child_index),
        section_(section) {
    SetPreferredSize(gfx::Size(0, kDropIndicatorThicknessDp));
  }

  void SetFavoritesDropAcceptedCallback(
      base::RepeatingCallback<void(const std::string&)> cb) {
    on_favorites_drop_accepted_ = std::move(cb);
  }

  void SetRevealEmptyPinnedDropLaneCallback(base::RepeatingClosure cb) {
    reveal_empty_pinned_drop_lane_callback_ = std::move(cb);
  }

  SidebarInsertionLaneView(const SidebarInsertionLaneView&) = delete;
  SidebarInsertionLaneView& operator=(const SidebarInsertionLaneView&) = delete;
  ~SidebarInsertionLaneView() override = default;

  const std::string& target_id_for_testing() const { return target_id_; }
  SidebarNodeKind target_kind_for_testing() const { return target_kind_; }
  bool append_for_testing() const { return append_; }

  bool GetDropFormats(int* formats,
                      std::set<ui::ClipboardFormatType>* format_types) override {
    format_types->insert(GetMahoDragFormatType());
    return true;
  }

  bool CanDrop(const ui::OSExchangeData& data) override {
    SidebarDragPayload payload;
    return ReadMahoDragData(data, payload) &&
           (payload.node_kind == SidebarNodeKind::kTab ||
            payload.node_kind == SidebarNodeKind::kFolder);
  }

  int OnDragUpdated(const ui::DropTargetEvent& event) override {
    ForwardDragToAutoScroller(this, event);
    SidebarDragPayload payload;
    if (!ReadMahoDragData(event.data(), payload) || payload.node_id == item_id_) {
      drag_over_drop_target_ = false;
      UpdateAppearance();
      return static_cast<int>(ui::mojom::DragOperation::kNone);
    }
    if (section_ == MahoSidebarTabSection::kNormal &&
        reveal_empty_pinned_drop_lane_callback_ &&
        (payload.origin == SidebarDragOrigin::kPinnedSection ||
         payload.origin == SidebarDragOrigin::kNormalSection ||
         payload.origin == SidebarDragOrigin::kFavorites)) {
      reveal_empty_pinned_drop_lane_callback_.Run();
    }
    drag_over_drop_target_ = true;
    UpdateAppearance();
    return static_cast<int>(ui::mojom::DragOperation::kMove);
  }

  DropCallback GetDropCallback(const ui::DropTargetEvent& event) override {
    ForwardDragEnded(this);
    return base::BindOnce(
        [](std::string space_id, std::string item_id,
           SidebarNodeKind target_kind, std::string target_id, bool append,
           std::string target_parent_folder_id, int target_folder_child_index,
           MahoSidebarTabSection section,
           base::RepeatingCallback<void(const std::string&)>
               on_favorites_drop_accepted,
           const ui::DropTargetEvent& event,
           ui::mojom::DragOperation& output_drag_op,
           std::unique_ptr<ui::LayerTreeOwner> drag_image_layer_owner) {
          SidebarDragPayload payload;
          if (!ReadMahoDragData(event.data(), payload) || payload.node_id == item_id) {
            output_drag_op = ui::mojom::DragOperation::kNone;
            return;
          }
          output_drag_op = ui::mojom::DragOperation::kNone;
          if (payload.node_kind == SidebarNodeKind::kTab) {
            if (target_kind == SidebarNodeKind::kTab) {
              if (!target_parent_folder_id.empty() &&
                  payload.source_parent_folder_id == target_parent_folder_id) {
                output_drag_op = ui::mojom::DragOperation::kMove;
                DispatchShellEventEx("reorder_tab_in_folder", {
                    ShellEventField("space_id", space_id),
                    ShellEventField("folder_id", target_parent_folder_id),
                    ShellEventField("tab_id", payload.node_id),
                    ShellEventField("to", target_folder_child_index)});
                return;
              }
              const SidebarDropPlan plan =
                  ResolveDropPlan(SectionKindFromTabSection(section), payload);
              if (!plan.is_valid) {
                output_drag_op = ui::mojom::DragOperation::kNone;
                return;
              }
              if (plan.favorite_transition.kind ==
                  SidebarDropPlan::FavoriteTransition::Kind::kUnfavorite) {
                if (on_favorites_drop_accepted) {
                  on_favorites_drop_accepted.Run(payload.node_id);
                }
              }
              ExecuteDropPlan(plan);
              output_drag_op = ui::mojom::DragOperation::kMove;
              DispatchReorderRootItem(space_id, SidebarNodeKind::kTab,
                                     payload.node_id,
                                     append ? MakeInsertionPointAppend()
                                            : MakeInsertionPointBefore(
                                                  SidebarNodeKind::kTab,
                                                  target_id));
              return;
            }
            if (target_kind == SidebarNodeKind::kFolder) {
              const SidebarDropPlan plan =
                  ResolveDropPlan(SectionKindFromTabSection(section), payload);
              if (!plan.is_valid) {
                output_drag_op = ui::mojom::DragOperation::kNone;
                return;
              }
              if (plan.favorite_transition.kind ==
                  SidebarDropPlan::FavoriteTransition::Kind::kUnfavorite) {
                if (on_favorites_drop_accepted) {
                  on_favorites_drop_accepted.Run(payload.node_id);
                }
              }
              ExecuteDropPlan(plan);
              output_drag_op = ui::mojom::DragOperation::kMove;
              DispatchReorderRootItem(space_id, SidebarNodeKind::kTab,
                                     payload.node_id,
                                     append ? MakeInsertionPointAppend()
                                            : MakeInsertionPointBefore(
                                                  SidebarNodeKind::kFolder,
                                                  target_id));
              return;
            }
          } else if (payload.node_kind == SidebarNodeKind::kFolder) {
            const SidebarDropPlan plan =
                ResolveDropPlan(SectionKindFromTabSection(section), payload);
            if (!plan.is_valid) {
              output_drag_op = ui::mojom::DragOperation::kNone;
              return;
            }
            ExecuteDropPlan(plan);
            output_drag_op = ui::mojom::DragOperation::kMove;
            if (target_kind == SidebarNodeKind::kTab) {
              DispatchReorderRootItem(space_id, SidebarNodeKind::kFolder,
                                     payload.node_id,
                                     append ? MakeInsertionPointAppend()
                                            : MakeInsertionPointBefore(
                                                  SidebarNodeKind::kTab,
                                                  target_id));
              return;
            }
            if (target_kind == SidebarNodeKind::kFolder) {
              if (append) {
                std::vector<std::pair<std::string, std::string>> fields = {
                    {"space_id", space_id},
                    {"folder_id", payload.node_id}};
                if (!target_parent_folder_id.empty()) {
                  fields.push_back({"parent_folder_id",
                                    target_parent_folder_id});
                }
                DispatchShellEvent("reorder_folder", fields);
              } else {
                if (!target_parent_folder_id.empty()) {
                  DispatchShellEvent("reorder_folder", {
                      {"space_id", space_id},
                      {"folder_id", payload.node_id},
                      {"parent_folder_id", target_parent_folder_id},
                      {"before_folder_id", target_id}});
                } else {
                  DispatchShellEvent("reorder_folder", {
                      {"space_id", space_id},
                      {"folder_id", payload.node_id},
                      {"before_folder_id", target_id}});
                }
              }
            }
          }
        },
        space_id_, item_id_, target_kind_, target_id_, append_,
        target_parent_folder_id_, target_folder_child_index_, section_,
        on_favorites_drop_accepted_);
  }

  void OnDragExited() override {
    ForwardDragEnded(this);
    drag_over_drop_target_ = false;
    UpdateAppearance();
  }

 private:
  void UpdateAppearance() {
    SetBackground(drag_over_drop_target_
                      ? views::CreateSolidBackground(ui::kColorSysPrimary)
                      : nullptr);
  }

  const std::string space_id_;
  const std::string item_id_;
  const SidebarNodeKind target_kind_;
  const std::string target_id_;
  const bool append_;
  const std::string target_parent_folder_id_;
  const int target_folder_child_index_;
  const MahoSidebarTabSection section_;
  bool drag_over_drop_target_ = false;
  base::RepeatingCallback<void(const std::string&)> on_favorites_drop_accepted_;
  base::RepeatingClosure reveal_empty_pinned_drop_lane_callback_;
};

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
  SetTextSubpixelRenderingEnabled(false);
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
  label->SetSubpixelRenderingEnabled(false);
  label->SetTextStyle(views::style::STYLE_BODY_5);
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

BEGIN_METADATA(SidebarInsertionLaneView)
END_METADATA

BEGIN_METADATA(MahoSidebarTabListView)
END_METADATA
BEGIN_METADATA(SidebarTabRowView)
END_METADATA
BEGIN_METADATA(SidebarFolderRowView)
END_METADATA

views::View::DropCallback GetSectionDropCallbackForTesting(
    MahoSidebarTabSection section,
    const ui::DropTargetEvent& event,
    bool populated) {
  SidebarSectionDropTarget target(section);
  target.SetPopulated(populated);
  return target.GetDropCallback(event);
}

SidebarSectionDropIndicatorStateForTesting
GetSectionDropIndicatorStateForTesting(MahoSidebarTabSection section,
                                             const ui::DropTargetEvent& event,
                                             bool populated) {
  SidebarSectionDropTarget target(section);
  target.SetPopulated(populated);
  SidebarSectionDropIndicatorStateForTesting state;
  target.OnDragUpdated(event);
  state.visible_after_update = target.is_drop_indicator_visible_for_testing();
  target.OnDragExited();
  state.visible_after_exit = target.is_drop_indicator_visible_for_testing();
  return state;
}

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

SidebarTabRowView* MahoSidebarTabListView::FindTabRowByTabIndexForTesting(
    int tab_index) {
  return RecursiveFindView<SidebarTabRowView>(
      tab_rows_, [tab_index](SidebarTabRowView* row) {
        return row->live_tab_index_for_testing() == tab_index;
      });
}

SidebarTabRowView* MahoSidebarTabListView::FindActiveTabRowForTesting() {
  TabStripModel* strip = browser_ ? browser_->tab_strip_model() : nullptr;
  if (strip) {
    const int active_index = strip->active_index();
    if (auto* row = RecursiveFindView<SidebarTabRowView>(
            tab_rows_, [active_index](SidebarTabRowView* r) {
              return r->live_tab_index_for_testing() == active_index;
            })) {
      return row;
    }
  }
  return RecursiveFindView<SidebarTabRowView>(
      tab_rows_, [](SidebarTabRowView* row) {
        return row->is_active_for_testing();
      });
}

SidebarTabRowView* MahoSidebarTabListView::FindFirstInactiveTabRowForTesting() {
  TabStripModel* strip = browser_ ? browser_->tab_strip_model() : nullptr;
  if (!strip) {
    return nullptr;
  }
  const int active_index = strip->active_index();
  return RecursiveFindView<SidebarTabRowView>(
      tab_rows_, [active_index](SidebarTabRowView* row) {
        const int live_index = row->live_tab_index_for_testing();
        return live_index >= 0 && live_index != active_index;
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

views::View* MahoSidebarTabListView::FindInsertionLaneBeforeTabByIdForTesting(
    const std::string& tab_id) {
  return RecursiveFindView<SidebarInsertionLaneView>(
      tab_rows_, [&tab_id](SidebarInsertionLaneView* lane) {
        return lane && lane->GetVisible() &&
               lane->target_kind_for_testing() == SidebarNodeKind::kTab &&
               lane->target_id_for_testing() == tab_id &&
               !lane->append_for_testing();
      });
}

views::View* MahoSidebarTabListView::FindInsertionLaneBeforeFolderByIdForTesting(
    const std::string& folder_id) {
  return RecursiveFindView<SidebarInsertionLaneView>(
      tab_rows_, [&folder_id](SidebarInsertionLaneView* lane) {
        return lane && lane->GetVisible() &&
               lane->target_kind_for_testing() == SidebarNodeKind::kFolder &&
               lane->target_id_for_testing() == folder_id &&
               !lane->append_for_testing();
      });
}

void MahoSidebarTabListView::ShowTabRowContextMenu(
    views::View* source,
    int live_index,
    const gfx::Point& point,
    ui::mojom::MenuSourceType source_type,
    MahoTabContextMenu::Delegate* delegate) {
  active_context_menu_ =
      std::make_unique<MahoTabContextMenu>(browser_, live_index, delegate);
  active_menu_model_ = active_context_menu_->BuildMenuModel();
  active_menu_runner_ = std::make_unique<views::MenuRunner>(
      active_menu_model_.get(),
      views::MenuRunner::CONTEXT_MENU);
  active_menu_runner_->RunMenuAt(source->GetWidget(), nullptr,
                                 gfx::Rect(point, gfx::Size()),
                                 views::MenuAnchorPosition::kTopLeft,
                                 source_type);
}

void MahoSidebarTabListView::ShowContextMenuForViewImpl(
    views::View* source,
    const gfx::Point& point,
    ui::mojom::MenuSourceType source_type) {
  if (!browser_) {
    return;
  }
  MahoSpaceContextMenu context_menu(browser_, "sidebar-spaces");
  auto menu_model = context_menu.BuildMenuModel();
  views::MenuRunner menu_runner(menu_model.get(),
                                views::MenuRunner::CONTEXT_MENU);
  menu_runner.RunMenuAt(source->GetWidget(), nullptr,
                        gfx::Rect(point, gfx::Size()),
                        views::MenuAnchorPosition::kTopLeft, source_type);
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

base::DictValue MakeRootItem(SidebarNodeKind kind, const std::string& id) {
  base::DictValue item;
  item.Set("kind", kind == SidebarNodeKind::kTab ? "tab" : "folder");
  item.Set("id", id);
  return item;
}

base::DictValue MakeInsertionPointBefore(SidebarNodeKind target_kind,
                                         const std::string& target_id) {
  base::DictValue point;
  point.Set("kind", "before");
  point.Set("target", MakeRootItem(target_kind, target_id));
  return point;
}

base::DictValue MakeInsertionPointAppend() {
  base::DictValue point;
  point.Set("kind", "append");
  return point;
}

void DispatchReorderRootItem(const std::string& space_id,
                             SidebarNodeKind item_kind,
                             const std::string& item_id,
                             base::DictValue insertion_point) {
  base::DictValue dict;
  dict.Set("space_id", space_id);
  dict.Set("item", MakeRootItem(item_kind, item_id));
  dict.Set("insertion_point", std::move(insertion_point));
  DispatchShellEventDict("reorder_root_item", std::move(dict));
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
                                         rename_callback,
                                     base::RepeatingCallback<int(const std::string&)>
                                         resolve_tab_index,
                                     base::RepeatingCallback<void(views::View*, int)>
                                         preview_show_callback,
                                     base::RepeatingClosure preview_hide_callback)
    : active_(node.is_active),
      pinned_(node.is_pinned),
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
      rename_callback_(std::move(rename_callback)),
      resolve_tab_index_(std::move(resolve_tab_index)),
      preview_show_callback_(std::move(preview_show_callback)),
      preview_hide_callback_(std::move(preview_hide_callback)),
      activate_callback_(std::move(activate_callback)) {
  SetNotifyEnterExitOnChild(true);
  const int indent = node.depth * kFolderIndentDp;
  auto row_insets = gfx::Insets::TLBR(0, 9 + indent, 0, 7);

  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, row_insets,
      kRowIconTextSpacingDp));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  favicon_view_ = AddChildView(std::make_unique<views::ImageView>());
  favicon_view_->SetImage(std::move(favicon));
  favicon_view_->SetImageSize(gfx::Size(kRowIconSizeDp, kRowIconSizeDp));

  std::u16string display_text = committed_title_.empty() ? node.host : committed_title_;
  title_button_ = static_cast<views::LabelButton*>(AddChildView(
      std::unique_ptr<views::View>(
          std::make_unique<SidebarDropForwardingLabelButton>(
              base::BindRepeating(&SidebarTabRowView::ActivateTabFromEvent,
                                  base::Unretained(this)),
              std::move(display_text), this)
              .release())));
  title_button_->SetBorder(nullptr);
  title_button_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  title_button_->SetMinSize(gfx::Size(0, kRowHeightDp));
  title_button_->SetMaxSize(
      gfx::Size(sidebar_layout::kRuntimeRailWidthMaxDp, kRowHeightDp));
  title_button_->SetFocusRingCornerRadius(kRowCornerRadiusDp);
  title_button_->SetRequestFocusOnPress(false);
  title_button_->SetImageLabelSpacing(0);
  title_button_->SetTextColor(views::Button::STATE_NORMAL,
                              ui::kColorSysOnSurface);
  title_button_->SetTextColor(views::Button::STATE_HOVERED,
                              ui::kColorSysOnSurface);
  title_button_->SetTextColor(views::Button::STATE_PRESSED,
                              ui::kColorSysOnSurface);
  title_button_->SetTextColor(views::Button::STATE_DISABLED,
                              ui::kColorSysOnSurfaceSubtle);
  title_button_->SetTextSubpixelRenderingEnabled(false);
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
      std::move(close_callback), maho_lucide_icons::kXIcon,
      kCloseIconSizeDp, ui::kColorSysOnSurfaceSubtle, ui::kColorSysOnSurface,
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

  title_button_->set_context_menu_controller(this);
  title_button_->set_drag_controller(this);

  set_context_menu_controller(this);
  set_drag_controller(this);

  UpdateAppearance();
}

SidebarTabRowView::~SidebarTabRowView() = default;

void SidebarTabRowView::SetSuspended(bool suspended) {
  if (is_suspended_ == suspended) {
    return;
  }
  is_suspended_ = suspended;
  UpdateAppearance();
}

int SidebarTabRowView::live_tab_index_for_testing() const {
  if (resolve_tab_index_) {
    const int resolved_index = resolve_tab_index_.Run(tab_id_);
    if (resolved_index >= 0) {
      return resolved_index;
    }
  }
  return tab_index_;
}

void SidebarTabRowView::RenameTab(int /*tab_index*/) {
  BeginTitleEditing();
}

void SidebarTabRowView::OnThemeChanged() {
  views::View::OnThemeChanged();
  UpdateAppearance();
}

bool SidebarTabRowView::OnMousePressed(const ui::MouseEvent& event) {
  activate_pending_ = event.IsOnlyLeftMouseButton() && !is_editing_title_;
  activation_handled_for_current_press_ = false;
  if ((event.flags() & ui::EF_IS_DOUBLE_CLICK) && !is_editing_title_) {
    activate_pending_ = false;
    BeginTitleEditing();
    return true;
  }
  return views::View::OnMousePressed(event);
}

void SidebarTabRowView::OnMouseReleased(const ui::MouseEvent& event) {
  const bool should_activate = activate_pending_;
  activate_pending_ = false;
  views::View::OnMouseReleased(event);
  if (should_activate) {
    ActivateFromRowClick(event);
  }
}

void SidebarTabRowView::OnMouseEntered(const ui::MouseEvent& event) {
  views::View::OnMouseEntered(event);
  int live_index = tab_index_;
  if (resolve_tab_index_) {
    const int resolved_index = resolve_tab_index_.Run(tab_id_);
    if (resolved_index >= 0) {
      live_index = resolved_index;
    }
  }
  if (!active_ && live_index >= 0 && preview_show_callback_) {
    preview_show_callback_.Run(this, live_index);
  }
  UpdateAppearance();
}

void SidebarTabRowView::OnMouseExited(const ui::MouseEvent& event) {
  views::View::OnMouseExited(event);
  if (preview_hide_callback_) {
    preview_hide_callback_.Run();
  }
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
  TabStripModel* tab_strip_model = browser_ ? browser_->tab_strip_model() : nullptr;
  const int live_index = resolve_tab_index_ ? resolve_tab_index_.Run(tab_id_) : tab_index_;
  if (!tab_strip_model || !tab_strip_model->ContainsIndex(live_index)) {
    return;
  }

  views::View* v = parent();
  MahoSidebarTabListView* list_view = nullptr;
  while (v) {
    list_view = views::AsViewClass<MahoSidebarTabListView>(v);
    if (list_view) {
      break;
    }
    v = v->parent();
  }
  if (!list_view) {
    return;
  }
  list_view->ShowTabRowContextMenu(list_view, live_index, point, source_type, this);
}

bool SidebarTabRowView::GetDropFormats(
    int* formats,
    std::set<ui::ClipboardFormatType>* format_types) {
  *formats = 0;
  format_types->insert(GetMahoDragFormatType());
  return true;
}

bool SidebarTabRowView::CanDrop(const ui::OSExchangeData& data) {
  SidebarDragPayload payload;
  return ReadMahoDragData(data, payload);
}

int SidebarTabRowView::OnDragUpdated(const ui::DropTargetEvent& event) {
  ForwardDragToAutoScroller(this, event);
  SidebarDragPayload payload;
  if (!ReadMahoDragData(event.data(), payload)) {
    active_drop_zone_ = SidebarTabDropZone::kNone;
    drag_over_drop_target_ = false;
    UpdateAppearance();
    return static_cast<int>(ui::mojom::DragOperation::kNone);
  }
  active_drop_zone_ = ResolveDropZone(event, payload);
  if (active_drop_zone_ == SidebarTabDropZone::kNone) {
    drag_over_drop_target_ = false;
    UpdateAppearance();
    return static_cast<int>(ui::mojom::DragOperation::kNone);
  }
  if (section_ == MahoSidebarTabSection::kNormal &&
      reveal_empty_pinned_drop_lane_callback_ &&
      (payload.origin == SidebarDragOrigin::kPinnedSection ||
       payload.origin == SidebarDragOrigin::kNormalSection ||
       payload.origin == SidebarDragOrigin::kFavorites)) {
    reveal_empty_pinned_drop_lane_callback_.Run();
  }
  drag_over_drop_target_ = true;
  UpdateAppearance();
  return static_cast<int>(ui::mojom::DragOperation::kMove);
}

views::View::DropCallback SidebarTabRowView::GetDropCallback(
    const ui::DropTargetEvent& event) {
  ForwardDragEnded(this);
  SidebarDragPayload drag_payload;
  const bool has_drag_payload = ReadMahoDragData(event.data(), drag_payload);
  const SidebarTabDropZone drop_zone =
      has_drag_payload ? ResolveDropZone(event, drag_payload)
                       : SidebarTabDropZone::kNone;
  return base::BindOnce(
      [](Browser* browser, std::string tab_id,
          MahoSidebarTabSection section, std::string space_id,
          std::string source_parent_folder_id,
          base::RepeatingCallback<int(const std::string&)> resolve_tab_index,
          base::RepeatingCallback<void(const std::string&)>
              favorites_drop_accepted,
          SidebarTabDropZone drop_zone,
          const ui::DropTargetEvent& event,
          ui::mojom::DragOperation& output_drag_op,
          std::unique_ptr<ui::LayerTreeOwner> drag_image_layer_owner) {
        SidebarDragPayload payload;
        if (!ReadMahoDragData(event.data(), payload)) {
          output_drag_op = ui::mojom::DragOperation::kNone;
          return;
        }
        output_drag_op = ui::mojom::DragOperation::kNone;

        if (payload.node_kind == SidebarNodeKind::kTab &&
            !payload.node_id.empty() && !tab_id.empty() &&
            payload.node_id != tab_id) {
          const SidebarDropPlan plan =
              ResolveDropPlan(SectionKindFromTabSection(section), payload);
          if (!plan.is_valid) {
            output_drag_op = ui::mojom::DragOperation::kNone;
            return;
          }

          if (drop_zone == SidebarTabDropZone::kSplit && plan.steps.empty()) {
            const int target_tab_strip_index =
                resolve_tab_index ? resolve_tab_index.Run(tab_id) : -1;
            const int source_tab_strip_index =
                resolve_tab_index ? resolve_tab_index.Run(payload.node_id) : -1;
            if (target_tab_strip_index >= 0 &&
                source_tab_strip_index >= 0 &&
                target_tab_strip_index != source_tab_strip_index) {
              TabStripModel* model =
                  browser ? browser->tab_strip_model() : nullptr;
              if (model && model->ContainsIndex(source_tab_strip_index) &&
                  model->ContainsIndex(target_tab_strip_index) &&
                  !model->GetSplitForTab(source_tab_strip_index).has_value() &&
                  !model->GetSplitForTab(target_tab_strip_index).has_value()) {
                output_drag_op = ui::mojom::DragOperation::kMove;
                split_tabs::SplitTabVisualData visual_data(
                    split_tabs::SplitTabLayout::kVertical, 0.5);
                model->ActivateTabAt(
                    target_tab_strip_index,
                    TabStripUserGestureDetails(
                        TabStripUserGestureDetails::GestureType::kOther));
                model->AddToNewSplit(
                    {source_tab_strip_index}, visual_data,
                    split_tabs::SplitTabCreatedSource::kDragAndDropTab);
                return;
              }
            }
          }

          if (plan.favorite_transition.kind ==
              SidebarDropPlan::FavoriteTransition::Kind::kUnfavorite) {
            if (favorites_drop_accepted) {
              favorites_drop_accepted.Run(payload.node_id);
            }
          }
          if (drop_zone == SidebarTabDropZone::kBefore &&
              !payload.source_parent_folder_id.empty()) {
            DispatchShellEvent("move_tab_to_root", {
                {"space_id", space_id},
                {"folder_id", payload.source_parent_folder_id},
                {"tab_id", payload.node_id},
                {"before_tab_id", tab_id}});
          }

          ExecuteDropPlanTransitionsOnly(plan);
          output_drag_op = ui::mojom::DragOperation::kMove;

          if (drop_zone == SidebarTabDropZone::kBefore) {
            if (payload.source_parent_folder_id.empty()) {
              DispatchReorderRootItem(space_id, SidebarNodeKind::kTab,
                                      payload.node_id,
                                      MakeInsertionPointBefore(
                                          SidebarNodeKind::kTab, tab_id));
            }
          }
          return;
        }

        if (payload.node_kind == SidebarNodeKind::kFolder &&
            !payload.node_id.empty() && !tab_id.empty()) {
          const SidebarDropPlan plan =
              ResolveDropPlan(SectionKindFromTabSection(section), payload);
          if (!plan.is_valid) {
            output_drag_op = ui::mojom::DragOperation::kNone;
            return;
          }
          ExecuteDropPlanTransitionsOnly(plan);
          if (drop_zone == SidebarTabDropZone::kBefore) {
            output_drag_op = ui::mojom::DragOperation::kMove;
            if (!payload.source_parent_folder_id.empty()) {
              DispatchShellEvent(maho::sidebar::kMoveFolderToRoot,
                                 {{"space_id", space_id},
                                  {"folder_id", payload.node_id}});
            }
            DispatchReorderRootItem(space_id, SidebarNodeKind::kFolder,
                                    payload.node_id,
                                    MakeInsertionPointBefore(
                                        SidebarNodeKind::kTab, tab_id));
          }
        }
      },
      browser_, tab_id_, section_, space_id_,
      source_parent_folder_id_, resolve_tab_index_,
      favorites_drop_accepted_callback_, drop_zone);
}

void SidebarTabRowView::OnDragExited() {
  ForwardDragEnded(this);
  active_drop_zone_ = SidebarTabDropZone::kNone;
  drag_over_drop_target_ = false;
  UpdateAppearance();
}

void SidebarTabRowView::OnDragDone() {
  views::View::OnDragDone();
  NotifySidebarDragEnded(this);
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

void SidebarTabRowView::ActivateFromRowClick(const ui::MouseEvent& event) {
  if (is_editing_title_ ||
      activation_handled_for_current_press_ ||
      !(event.changed_button_flags() & ui::EF_LEFT_MOUSE_BUTTON)) {
    return;
  }
  if (!HitTestPoint(event.location())) {
    return;
  }
  if (close_button_ && close_button_->GetVisible()) {
    gfx::Point close_button_point = event.location();
    views::View::ConvertPointToTarget(this, close_button_, &close_button_point);
    if (close_button_->HitTestPoint(close_button_point)) {
      return;
    }
  }
  const int live_index =
      resolve_tab_index_ ? resolve_tab_index_.Run(tab_id_) : tab_index_;
  if (live_index < 0) {
    if (activate_callback_) {
      activation_handled_for_current_press_ = true;
      activate_callback_.Run(event);
    }
    return;
  }
  if (!browser_ || !browser_->tab_strip_model() ||
      !browser_->tab_strip_model()->ContainsIndex(live_index)) {
    return;
  }
  activation_handled_for_current_press_ = true;
  browser_->tab_strip_model()->ActivateTabAt(live_index);
}

void SidebarTabRowView::ActivateTabFromEvent(const ui::Event& event) {
  const int live_index =
      resolve_tab_index_ ? resolve_tab_index_.Run(tab_id_) : tab_index_;
  if (live_index < 0) {
    if (activate_callback_) {
      activation_handled_for_current_press_ = true;
      activate_callback_.Run(event);
    }
    return;
  }
  if (!browser_ || !browser_->tab_strip_model() ||
      !browser_->tab_strip_model()->ContainsIndex(live_index)) {
    return;
  }
  activation_handled_for_current_press_ = true;
  browser_->tab_strip_model()->ActivateTabAt(live_index);
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
  payload.tab_strip_index = resolve_tab_index_ ? resolve_tab_index_.Run(tab_id_) : tab_index_;
  payload.source_parent_folder_id = source_parent_folder_id_;
  payload.source_folder_child_index = folder_child_index_;
  WriteMahoDragData(payload, data);
  data->provider().SetDragImage(CreateSidebarDragImage(sender),
                                gfx::Vector2d(0, 0));
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
    SetBorder(views::CreateRoundedRectBorder(
        kDropIndicatorOutlineThicknessDp, kRowCornerRadiusDp,
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
  if (favicon_view_ && favicon_view_->layer()) {
    favicon_view_->DestroyLayer();
  }
  // Pinned + active (loaded WebContents) shows minus = unload affordance.
  // Pinned + suspended OR normal tab shows X = full close affordance.
  const bool show_minus =
      (section_ == MahoSidebarTabSection::kPinned) && !is_suspended_;
  const gfx::VectorIcon& close_icon =
      show_minus ? maho_lucide_icons::kMinusIcon
                 : maho_lucide_icons::kXIcon;
  views::SetImageFromVectorIconWithColor(
      close_button_, close_icon, kCloseIconSizeDp,
      views::IconColors(ui::kColorSysOnSurfaceSubtle, ui::kColorSysOnSurface,
                        ui::kColorSysOnSurface));
  const std::u16string close_tooltip =
      show_minus ? u"Unload tab" : u"Close tab";
  close_button_->SetTooltipText(close_tooltip);
  close_button_->SetAccessibleName(close_tooltip);
  if (title_field_) {
    title_field_->SetVisible(is_editing_title_);
  }
  title_button_->SetVisible(!is_editing_title_);
  const bool close_was_visible = close_button_->GetVisible();
  close_button_->SetVisible(show_close_button);
  if (audio_indicator_view_) {
    audio_indicator_view_->SetVisible(!show_close_button);
  }
  // If the close button's visibility just changed, the BoxLayout bounds for
  // it (and the audio indicator) are stale until the next layout pass.
  // Force an immediate relayout so hit-testing on the revealed close button
  // works correctly on the very first click after hover reveals it.
  if (close_was_visible != show_close_button) {
    DeprecatedLayoutImmediately();
  }
}

SidebarTabRowView::SidebarTabDropZone SidebarTabRowView::ResolveDropZone(
    const ui::DropTargetEvent& event,
    const SidebarDragPayload& payload) const {
  if (payload.node_id.empty() || payload.node_id == tab_id_) {
    return SidebarTabDropZone::kNone;
  }

  if (payload.node_kind == SidebarNodeKind::kTab &&
      !source_parent_folder_id_.empty() &&
      !payload.source_parent_folder_id.empty() &&
      payload.source_parent_folder_id == source_parent_folder_id_) {
    const int src = payload.source_folder_child_index;
    const int tgt = folder_child_index_;
    if (src == tgt - 1 || src == tgt + 1) {
      return SidebarTabDropZone::kNone;
    }
  }

  const int row_height = std::max(height(), 1);
  const int y = event.location().y();
  const int third = row_height / 3;

  if (y < third) {
    return SidebarTabDropZone::kBefore;
  }

  if (payload.node_kind == SidebarNodeKind::kTab) {
    if (y >= third && y < row_height - third &&
        (resolve_tab_index_ ? resolve_tab_index_.Run(tab_id_) : tab_index_) >= 0) {
      return SidebarTabDropZone::kSplit;
    }
  }

  return SidebarTabDropZone::kNone;
}

// --- SidebarFolderRowView ---

SidebarFolderRowView::SidebarFolderRowView(
    const SidebarTreeNode& node,
    MahoSidebarTabSection /*section*/,
    std::string active_space_id,
    Browser* browser,
    views::Button::PressedCallback toggle_callback,
    base::RepeatingCallback<void(const std::string&, const std::u16string&)>
        rename_callback,
    std::string next_sibling_folder_id,
    std::string parent_folder_id)
    : folder_id_(node.folder_id),
      space_id_(std::move(active_space_id)),
      folder_is_pinned_(node.folder_is_pinned),
      next_sibling_folder_id_(std::move(next_sibling_folder_id)),
      parent_folder_id_(std::move(parent_folder_id)),
      is_expanded_(node.is_expanded),
      browser_(browser),
      rename_callback_(std::move(rename_callback)),
      committed_name_(node.folder_name.empty() ? u"Folder" : node.folder_name) {
  SetNotifyEnterExitOnChild(true);
  const int indent = node.depth * kFolderIndentDp;
  auto row_insets = gfx::Insets::TLBR(0, 9 + indent, 0, 7);

  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, row_insets,
      kRowIconTextSpacingDp));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

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
              std::move(toggle_callback), committed_name_, this)
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
  label_button_->SetTextSubpixelRenderingEnabled(false);
  layout->SetFlexForView(label_button_, 1);

  auto name_field = std::make_unique<views::Textfield>();
  name_field->SetVisible(false);
  name_field->SetBorder(nullptr);
  name_field->SetBackgroundColor(SK_ColorTRANSPARENT);
  name_field->SetTextColor(ui::kColorSysOnSurface);
  name_field->SetController(this);
  name_field->SetAccessibleName(u"Rename folder");
  name_field_ = AddChildView(std::move(name_field));
  layout->SetFlexForView(name_field_, 1);

  label_button_->set_context_menu_controller(this);
  label_button_->set_drag_controller(this);

  set_context_menu_controller(this);
  set_drag_controller(this);

  UpdateAppearance();
}

SidebarFolderRowView::~SidebarFolderRowView() = default;

void SidebarFolderRowView::ShowContextMenuForViewImpl(
    views::View* source,
    const gfx::Point& point,
    ui::mojom::MenuSourceType source_type) {
  if (is_editing_name_) {
    EndFolderEditing(false);
  }
  active_folder_context_menu_ = std::make_unique<MahoFolderContextMenu>(
      browser_, space_id_, folder_id_, parent_folder_id_,
      base::BindOnce(&SidebarFolderRowView::BeginFolderEditing,
                     weak_factory_.GetWeakPtr()));
  active_folder_menu_model_ = active_folder_context_menu_->BuildMenuModel();
  active_folder_menu_runner_ = std::make_unique<views::MenuRunner>(
      active_folder_menu_model_.get(),
      views::MenuRunner::CONTEXT_MENU);
  active_folder_menu_runner_->RunMenuAt(
      GetWidget(), nullptr, gfx::Rect(point, gfx::Size()),
      views::MenuAnchorPosition::kTopLeft, source_type);
}

bool SidebarFolderRowView::HandleKeyEvent(views::Textfield* sender,
                                           const ui::KeyEvent& event) {
  if (event.type() != ui::EventType::kKeyPressed || sender != name_field_) {
    return false;
  }
  switch (event.key_code()) {
    case ui::VKEY_RETURN:
      EndFolderEditing(true);
      return true;
    case ui::VKEY_ESCAPE:
      EndFolderEditing(false);
      return true;
    default:
      return false;
  }
}

void SidebarFolderRowView::BeginFolderEditing() {
  if (folder_id_.empty() || is_editing_name_ || !name_field_) {
    return;
  }
  is_editing_name_ = true;
  label_button_->SetVisible(false);
  name_field_->SetVisible(true);
  name_field_->SetText(committed_name_);
  name_field_->RequestFocus();
  name_field_->SelectAll(false);
  UpdateAppearance();
}

void SidebarFolderRowView::EndFolderEditing(bool commit) {
  if (!is_editing_name_ || !name_field_) {
    return;
  }
  if (commit) {
    std::u16string next_name =
        NormalizeRenamedTitle(std::u16string(name_field_->GetText()));
    if (next_name.empty()) {
      next_name = committed_name_;
    }
    committed_name_ = next_name;
    if (label_button_) {
      label_button_->SetText(committed_name_);
    }
    if (rename_callback_) {
      rename_callback_.Run(folder_id_, committed_name_);
    }
  } else {
    name_field_->SetText(committed_name_);
    if (label_button_) {
      label_button_->SetText(committed_name_);
    }
  }
  is_editing_name_ = false;
  name_field_->SetVisible(false);
  label_button_->SetVisible(true);
  RequestFocus();
  UpdateAppearance();
}

void SidebarFolderRowView::OnThemeChanged() {
  views::View::OnThemeChanged();
  UpdateAppearance();
}

void SidebarFolderRowView::OnMouseEntered(const ui::MouseEvent& event) {
  views::View::OnMouseEntered(event);
  UpdateAppearance();
  if (show_popup_callback_ && !children_snapshot_.empty()) {
    show_popup_callback_.Run();
  }
}

void SidebarFolderRowView::OnMouseExited(const ui::MouseEvent& event) {
  views::View::OnMouseExited(event);
  UpdateAppearance();
  if (hide_popup_callback_) {
    hide_popup_callback_.Run();
  }
}

bool SidebarFolderRowView::GetDropFormats(
    int* formats,
    std::set<ui::ClipboardFormatType>* format_types) {
  *formats = 0;
  format_types->insert(GetMahoDragFormatType());
  return true;
}

bool SidebarFolderRowView::CanDrop(const ui::OSExchangeData& data) {
  SidebarDragPayload payload;
  return ReadMahoDragData(data, payload);
}

int SidebarFolderRowView::OnDragUpdated(const ui::DropTargetEvent& event) {
  ForwardDragToAutoScroller(this, event);
  SidebarDragPayload payload;
  if (!ReadMahoDragData(event.data(), payload)) {
    active_drop_zone_ = SidebarFolderDropZone::kNone;
    drag_over_drop_target_ = false;
    UpdateAppearance();
    return static_cast<int>(ui::mojom::DragOperation::kNone);
  }
  active_drop_zone_ = ResolveDropZone(event, payload);
  if (active_drop_zone_ == SidebarFolderDropZone::kNone) {
    drag_over_drop_target_ = false;
    UpdateAppearance();
    return static_cast<int>(ui::mojom::DragOperation::kNone);
  }
  if (reveal_empty_pinned_drop_lane_callback_ &&
      (payload.origin == SidebarDragOrigin::kPinnedSection ||
       payload.origin == SidebarDragOrigin::kNormalSection ||
       payload.origin == SidebarDragOrigin::kFavorites)) {
    reveal_empty_pinned_drop_lane_callback_.Run();
  }
  drag_over_drop_target_ = true;
  UpdateAppearance();

  if (active_drop_zone_ == SidebarFolderDropZone::kInto && !is_expanded_ &&
      spring_expand_callback_ && !spring_load_timer_.IsRunning()) {
    spring_load_timer_.Start(
        FROM_HERE, base::Milliseconds(700),
        base::BindOnce(
            [](base::WeakPtr<SidebarFolderRowView> self) {
              if (self && self->spring_expand_callback_) {
                self->spring_expand_callback_.Run();
              }
            },
            weak_factory_.GetWeakPtr()));
  } else if (active_drop_zone_ != SidebarFolderDropZone::kInto) {
    spring_load_timer_.Stop();
  }

  return static_cast<int>(ui::mojom::DragOperation::kMove);
}

views::View::DropCallback SidebarFolderRowView::GetDropCallback(
    const ui::DropTargetEvent& event) {
  ForwardDragEnded(this);
  SidebarDragPayload payload;
  SidebarFolderDropZone drop_zone = SidebarFolderDropZone::kNone;
  if (ReadMahoDragData(event.data(), payload)) {
    drop_zone = ResolveDropZone(event, payload);
  }

  return base::BindOnce(
      [](std::string folder_id, std::string space_id, bool folder_is_pinned,
         SidebarFolderDropZone drop_zone, std::string next_sibling_folder_id,
         std::string parent_folder_id,
         base::RepeatingCallback<void(const std::string&)>
             favorites_drop_accepted,
         const ui::DropTargetEvent& event,
         ui::mojom::DragOperation& output_drag_op,
         std::unique_ptr<ui::LayerTreeOwner> drag_image_layer_owner) {
        SidebarDragPayload payload;
        if (!ReadMahoDragData(event.data(), payload)) {
          output_drag_op = ui::mojom::DragOperation::kNone;
          return;
        }
        if (payload.node_id == folder_id) {
          output_drag_op = ui::mojom::DragOperation::kNone;
          return;
        }
        const SectionKind section_kind =
            folder_is_pinned ? SectionKind::kPinned : SectionKind::kNormal;
        const SidebarDropPlan plan = ResolveDropPlan(section_kind, payload);
        if (!plan.is_valid) {
          output_drag_op = ui::mojom::DragOperation::kNone;
          return;
        }
        if (drop_zone == SidebarFolderDropZone::kNone ||
            (drop_zone == SidebarFolderDropZone::kAfter &&
             payload.node_kind == SidebarNodeKind::kFolder &&
             payload.node_id == next_sibling_folder_id)) {
          output_drag_op = ui::mojom::DragOperation::kNone;
          return;
        }
        if (plan.favorite_transition.kind ==
            SidebarDropPlan::FavoriteTransition::Kind::kUnfavorite) {
          if (favorites_drop_accepted) {
            favorites_drop_accepted.Run(payload.node_id);
          }
        }
        if (payload.node_kind == SidebarNodeKind::kTab &&
            drop_zone == SidebarFolderDropZone::kInto) {
          ExecuteDropPlanTransitionsOnly(plan);
        } else {
          ExecuteDropPlan(plan);
        }
        output_drag_op = ui::mojom::DragOperation::kMove;
        if (payload.node_kind == SidebarNodeKind::kTab) {
          if (drop_zone == SidebarFolderDropZone::kBefore) {
            DispatchReorderRootItem(space_id, SidebarNodeKind::kTab,
                                    payload.node_id,
                                    MakeInsertionPointBefore(
                                        SidebarNodeKind::kFolder, folder_id));
            return;
          }
          if (drop_zone == SidebarFolderDropZone::kInto) {
            DispatchShellEvent("move_tab_to_folder", {
                {"space_id", space_id},
                {"folder_id", folder_id},
                {"tab_id", payload.node_id}});
            return;
          }
          DispatchReorderRootItem(
              space_id, SidebarNodeKind::kTab, payload.node_id,
              next_sibling_folder_id.empty()
                  ? MakeInsertionPointAppend()
                  : MakeInsertionPointBefore(SidebarNodeKind::kFolder,
                                             next_sibling_folder_id));
          return;
        }

        if (payload.node_kind == SidebarNodeKind::kFolder) {
          if (drop_zone == SidebarFolderDropZone::kBefore) {
            if (!parent_folder_id.empty()) {
              DispatchShellEvent("reorder_folder", {
                  {"space_id", space_id},
                  {"folder_id", payload.node_id},
                  {"parent_folder_id", parent_folder_id},
                  {"before_folder_id", folder_id}});
            } else {
              DispatchShellEvent("reorder_folder", {
                  {"space_id", space_id},
                  {"folder_id", payload.node_id},
                  {"before_folder_id", folder_id}});
            }
            return;
          }
          if (drop_zone == SidebarFolderDropZone::kInto) {
            DispatchShellEvent("move_folder_into_folder", {
                {"space_id", space_id},
                {"folder_id", payload.node_id},
                {"target_folder_id", folder_id}});
            return;
          }
          if (!parent_folder_id.empty()) {
            std::vector<std::pair<std::string, std::string>> fields = {
                {"space_id", space_id},
                {"folder_id", payload.node_id},
                {"parent_folder_id", parent_folder_id}};
            if (!next_sibling_folder_id.empty()) {
              fields.push_back({"before_folder_id", next_sibling_folder_id});
            }
            DispatchShellEvent("reorder_folder", fields);
            return;
          }
          if (!next_sibling_folder_id.empty()) {
            DispatchShellEvent("reorder_folder", {
                {"space_id", space_id},
                {"folder_id", payload.node_id},
                {"before_folder_id", next_sibling_folder_id}});
            return;
          }
          DispatchShellEvent("reorder_folder", {
              {"space_id", space_id},
              {"folder_id", payload.node_id}});
        }
       },
       folder_id_, space_id_, folder_is_pinned_, drop_zone,
       next_sibling_folder_id_, parent_folder_id_,
       favorites_drop_accepted_callback_);
}

void SidebarFolderRowView::OnDragExited() {
  spring_load_timer_.Stop();
  ForwardDragEnded(this);
  active_drop_zone_ = SidebarFolderDropZone::kNone;
  drag_over_drop_target_ = false;
  UpdateAppearance();
}

void SidebarFolderRowView::OnDragDone() {
  spring_load_timer_.Stop();
  views::View::OnDragDone();
  NotifySidebarDragEnded(this);
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
  payload.source_parent_folder_id = parent_folder_id_;
  WriteMahoDragData(payload, data);
  data->provider().SetDragImage(CreateSidebarDragImage(sender),
                                gfx::Vector2d(0, 0));
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
        vector_icons::kFolderChromeRefreshIcon, ui::kColorSysPrimary,
        kFolderGlyphSizeDp));
  }
  if (label_button_) {
    label_button_->SetVisible(!is_editing_name_);
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
  if (name_field_) {
    name_field_->SetVisible(is_editing_name_);
  }
  if (show_drop_target) {
    SetBackground(views::CreateRoundedRectBackground(ui::kColorSysSurface4,
                                                     kRowCornerRadiusDp));
    SetBorder(views::CreateRoundedRectBorder(
        kDropIndicatorOutlineThicknessDp, kRowCornerRadiusDp,
        ui::kColorSysPrimary));
    return;
  }
  SetBackground(hovered ? views::CreateRoundedRectBackground(
                              ui::kColorSysSurface2, kRowCornerRadiusDp)
                        : nullptr);
  SetBorder(nullptr);
}

SidebarFolderRowView::SidebarFolderDropZone SidebarFolderRowView::ResolveDropZone(
    const ui::DropTargetEvent& event,
    const SidebarDragPayload& payload) const {
  if (payload.node_id.empty() || payload.node_id == folder_id_) {
    return SidebarFolderDropZone::kNone;
  }

  if (payload.node_kind == SidebarNodeKind::kTab ||
      payload.node_kind == SidebarNodeKind::kFolder) {
    const int row_height = std::max(height(), 1);
    const int y = event.location().y();
    const int third = row_height / 3;

    SidebarFolderDropZone zone = SidebarFolderDropZone::kAfter;
    if (y < third) {
      zone = SidebarFolderDropZone::kBefore;
    } else if (y >= third && y < row_height - third) {
      zone = SidebarFolderDropZone::kInto;
    }

    if (zone == SidebarFolderDropZone::kAfter &&
        payload.node_kind == SidebarNodeKind::kFolder &&
        payload.node_id == next_sibling_folder_id_) {
      return SidebarFolderDropZone::kNone;
    }

    return zone;
  }

  return SidebarFolderDropZone::kNone;
}

class SidebarSpaceHeaderRow : public views::View {
  METADATA_HEADER(SidebarSpaceHeaderRow, views::View)

 public:
  SidebarSpaceHeaderRow() {
    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal,
        gfx::Insets::TLBR(4, 12, 4, 12),
        8));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);

    icon_label_ = AddChildView(std::make_unique<views::Label>());
    icon_label_->SetFontList(
        views::Label::GetDefaultFontList().Derive(4, gfx::Font::NORMAL, gfx::Font::Weight::NORMAL));
    icon_label_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
    icon_label_->SetVerticalAlignment(gfx::ALIGN_MIDDLE);
    icon_label_->SetVisible(false);

    name_label_ = AddChildView(std::make_unique<views::Label>());
    name_label_->SetTextStyle(views::style::STYLE_BODY_4_MEDIUM);
    name_label_->SetEnabledColor(ui::kColorSysOnSurfaceSubtle);
    name_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    name_label_->SetVerticalAlignment(gfx::ALIGN_MIDDLE);

    SetBackground(nullptr);
    SetBorder(nullptr);
  }

  SidebarSpaceHeaderRow(const SidebarSpaceHeaderRow&) = delete;
  SidebarSpaceHeaderRow& operator=(const SidebarSpaceHeaderRow&) = delete;
  ~SidebarSpaceHeaderRow() override = default;

  void Configure(const std::string& icon, const std::u16string& name) {
    icon_label_->SetText(base::UTF8ToUTF16(icon));
    icon_label_->SetVisible(!icon.empty());
    name_label_->SetText(name);
    InvalidateLayout();
  }

 private:
  raw_ptr<views::Label> icon_label_ = nullptr;
  raw_ptr<views::Label> name_label_ = nullptr;
};

BEGIN_METADATA(SidebarSpaceHeaderRow)
END_METADATA

void ForwardDragToAutoScroller(views::View* child, const ui::DropTargetEvent& event) {
  for (views::View* v = child; v; v = v->parent()) {
    if (auto* list = views::AsViewClass<MahoSidebarTabListView>(v)) {
      gfx::Point screen_point = event.location();
      views::View::ConvertPointToScreen(child, &screen_point);
      list->OnAutoScrollDragOver(screen_point);
      return;
    }
  }
}

void ForwardDragEnded(views::View* child) {
  for (views::View* v = child; v; v = v->parent()) {
    if (auto* list = views::AsViewClass<MahoSidebarTabListView>(v)) {
      list->OnAutoScrollDragEnded();
      return;
    }
  }
}

// --- public SidebarSpaceHeaderRow APIs ---
std::unique_ptr<views::View> CreateSidebarSpaceHeaderRow() {
  return std::make_unique<SidebarSpaceHeaderRow>();
}

void UpdateSidebarSpaceHeaderRow(views::View* row,
                                 const std::string& icon,
                                 const std::u16string& name) {
  if (!row) {
    return;
  }
  auto* header = views::AsViewClass<SidebarSpaceHeaderRow>(row);
  CHECK(header) << "UpdateSidebarSpaceHeaderRow received non-SidebarSpaceHeaderRow";
  header->Configure(icon, name);
}

// --- MahoSidebarTabListView::SidebarAutoScroller implementation ---
class MahoSidebarTabListView::SidebarAutoScroller {
 public:
  explicit SidebarAutoScroller(views::ScrollView* scroll_view)
      : scroll_view_(scroll_view) {}

  SidebarAutoScroller(const SidebarAutoScroller&) = delete;
  SidebarAutoScroller& operator=(const SidebarAutoScroller&) = delete;
  ~SidebarAutoScroller() {
    timer_.Stop();
  }

  void OnDragOver(const gfx::Point& screen_point) {
    if (!scroll_view_) {
      return;
    }
    gfx::Point local_point = screen_point;
    views::View::ConvertPointFromScreen(scroll_view_, &local_point);

    const int height = scroll_view_->height();

    if (local_point.y() >= 0 && local_point.y() < kAutoScrollEdgeDp) {
      direction_ = -1;
      if (!timer_.IsRunning()) {
        timer_.Start(FROM_HERE, base::Hertz(kAutoScrollHz), this, &SidebarAutoScroller::ScrollTick);
      }
    } else if (local_point.y() > height - kAutoScrollEdgeDp && local_point.y() <= height) {
      direction_ = 1;
      if (!timer_.IsRunning()) {
        timer_.Start(FROM_HERE, base::Hertz(kAutoScrollHz), this, &SidebarAutoScroller::ScrollTick);
      }
    } else {
      timer_.Stop();
    }
  }

  void OnDragEnded() {
    timer_.Stop();
  }

 private:
  void ScrollTick() {
    if (!scroll_view_) {
      return;
    }
    scroll_view_->ScrollByOffset(gfx::PointF(0, kAutoScrollStepDp * direction_));
  }

  const raw_ptr<views::ScrollView> scroll_view_;
  base::RepeatingTimer timer_;
  int direction_ = 0;
};

void MahoSidebarTabListView::SetAutoScrollScrollView(views::ScrollView* scroll_view) {
  if (scroll_view) {
    auto_scroller_ = std::make_unique<SidebarAutoScroller>(scroll_view);
  } else {
    auto_scroller_.reset();
  }
}

void MahoSidebarTabListView::OnAutoScrollDragOver(const gfx::Point& screen_point) {
  if (auto_scroller_) {
    auto_scroller_->OnDragOver(screen_point);
  }
}

void MahoSidebarTabListView::OnAutoScrollDragEnded() {
  if (auto_scroller_) {
    auto_scroller_->OnDragEnded();
  }
}

// --- MahoSidebarTabListView ---

MahoSidebarTabListView::MahoSidebarTabListView(Browser* browser)
    : browser_(browser),
      tab_preview_controller_(std::make_unique<MahoTabPreviewController>(
          browser)),
      folder_hover_controller_(std::make_unique<MahoSidebarFolderHoverController>(
          browser)) {
  auto* root_layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, kRootInsets, kRowSpacingDp));
  root_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  auto rows = std::make_unique<views::View>();
  auto* rows_layout = rows->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), kSectionSpacingDp));
  rows_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  tab_rows_ = rows.get();
  tab_rows_->set_context_menu_controller(this);
  AddChildView(std::move(rows));
  set_context_menu_controller(this);
}

MahoSidebarTabListView::~MahoSidebarTabListView() = default;

void MahoSidebarTabListView::SetFavoritesDropAcceptedCallback(
    base::RepeatingCallback<void(const std::string&)> callback) {
  favorites_drop_accepted_callback_ = std::move(callback);
}

bool MahoSidebarTabListView::IsTabPreviewShowingForTesting() const {
  return tab_preview_controller_ && tab_preview_controller_->IsShowingForTesting();
}

views::Widget* MahoSidebarTabListView::tab_preview_widget_for_testing() const {
  return tab_preview_controller_
             ? tab_preview_controller_->preview_widget_for_testing()
             : nullptr;
}

const std::u16string& MahoSidebarTabListView::tab_preview_title_for_testing()
    const {
  static const std::u16string kEmpty;
  return tab_preview_controller_
             ? tab_preview_controller_->preview_title_for_testing()
             : kEmpty;
}

const std::u16string& MahoSidebarTabListView::tab_preview_url_for_testing()
    const {
  static const std::u16string kEmpty;
  return tab_preview_controller_
             ? tab_preview_controller_->preview_url_for_testing()
             : kEmpty;
}

void MahoSidebarTabListView::SetTabPreviewShowDelayForTesting(
    base::TimeDelta delay) {
  if (tab_preview_controller_) {
    tab_preview_controller_->SetShowDelayForTesting(delay);
  }
}

void MahoSidebarTabListView::Update(const MahoSidebarTabListModel& model,
                                    Browser* browser) {
  browser_ = browser;
  active_space_id_ = model.active_space_id;
  pinned_drop_lane_revealed_ = false;
  PruneLocalTitleCache(model);

  const TabStripModel* strip = browser ? browser->tab_strip_model() : nullptr;
  const bool live_tab_strip_is_empty = !strip || strip->empty();

  auto model_has_tab_ids = [](const MahoSidebarTabListModel& candidate) {
    auto has_tree_tab_id = [&](const auto& self,
                               const std::vector<SidebarTreeNode>& nodes) -> bool {
      for (const auto& node : nodes) {
        if (node.kind == SidebarNodeKind::kTab && !node.tab_id.empty()) {
          return true;
        }
        if (!node.children.empty() && self(self, node.children)) {
          return true;
        }
      }
      return false;
    };
    return has_tree_tab_id(has_tree_tab_id, candidate.pinned_tree) ||
           has_tree_tab_id(has_tree_tab_id, candidate.normal_tree);
  };

  if (live_tab_strip_is_empty || model_has_tab_ids(model)) {
    last_model_ = model;
    has_last_model_ = true;
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
  ForwardDragToAutoScroller(this, event);
  SidebarDragPayload payload;
  if (ReadMahoDragData(event.data(), payload)) {
    if (payload.origin == SidebarDragOrigin::kPinnedSection ||
        payload.origin == SidebarDragOrigin::kNormalSection ||
        payload.origin == SidebarDragOrigin::kFavorites) {
      RevealEmptyPinnedDropLane();
    }
  }
  return static_cast<int>(ui::mojom::DragOperation::kCopy);
}

views::View::DropCallback MahoSidebarTabListView::GetDropCallback(
    const ui::DropTargetEvent& event) {
  ForwardDragEnded(this);
  return base::BindOnce(
      [](Browser* browser, std::string space_id,
         base::RepeatingCallback<void(const std::string&)>
             favorites_drop_accepted,
         const ui::DropTargetEvent& event,
         ui::mojom::DragOperation& output_drag_op,
         std::unique_ptr<ui::LayerTreeOwner> drag_image_layer_owner) {
        SidebarDragPayload payload;
        if (ReadMahoDragData(event.data(), payload)) {
          const SidebarDropPlan plan =
              ResolveDropPlan(SectionKind::kNormal, payload);
          if (!plan.is_valid) {
            output_drag_op = ui::mojom::DragOperation::kNone;
            return;
          }
          if (plan.favorite_transition.kind ==
              SidebarDropPlan::FavoriteTransition::Kind::kUnfavorite) {
            if (favorites_drop_accepted) {
              favorites_drop_accepted.Run(payload.node_id);
            }
          }
          ExecuteDropPlan(plan);
          output_drag_op = ui::mojom::DragOperation::kMove;
          if (payload.node_kind == SidebarNodeKind::kTab &&
              !payload.node_id.empty()) {
            DispatchReorderRootItem(space_id, SidebarNodeKind::kTab,
                                   payload.node_id,
                                   MakeInsertionPointAppend());
          } else if (payload.node_kind == SidebarNodeKind::kFolder &&
                     !payload.node_id.empty()) {
            DispatchReorderRootItem(space_id, SidebarNodeKind::kFolder,
                                   payload.node_id,
                                   MakeInsertionPointAppend());
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
      browser_, active_space_id_, favorites_drop_accepted_callback_);
}

void MahoSidebarTabListView::OnDragExited() {
  ForwardDragEnded(this);
  HideEmptyPinnedDropLane();
}

void MahoSidebarTabListView::RevealEmptyPinnedDropLane() {
  if (pinned_drop_lane_revealed_) {
    return;
  }
  if (!has_last_model_) {
    return;
  }
  const bool pinned_already_has_content =
      !last_model_.pinned_tree.empty();
  if (pinned_already_has_content) {
    return;
  }
  pinned_drop_lane_revealed_ = true;
  UpdatePinnedDropLaneVisibility(true);
}

void MahoSidebarTabListView::HideEmptyPinnedDropLane() {
  if (!pinned_drop_lane_revealed_) {
    return;
  }
  pinned_drop_lane_revealed_ = false;
  UpdatePinnedDropLaneVisibility(false);
}

void MahoSidebarTabListView::UpdatePinnedDropLaneVisibility(bool visible) {
  if (pinned_section_target_) {
    auto* drop_target = static_cast<SidebarSectionDropTarget*>(pinned_section_target_);
    drop_target->SetMinDropTargetHeight(visible ? kRowHeightDp : 0);
    pinned_section_target_->SetVisible(visible);
  }
  if (pinned_separator_) {
    // Separator stays visible regardless of pinned-section drop-lane state.
    pinned_separator_->SetVisible(true);
  }
}

void MahoSidebarTabListView::RebuildRows(const MahoSidebarTabListModel& model,
                                            Browser* browser) {
  HandleTabRowHoverEnd();
  // Tear down active context-menu state before destroying row views.
  // The menu runner, menu model, and delegate chain may still reference row
  // views, so preserve this order before RemoveAllChildViews() clears them.
  active_menu_runner_.reset();
  active_menu_model_.reset();
  active_context_menu_.reset();
  tab_rows_->RemoveAllChildViews();
  pinned_section_target_ = nullptr;
  normal_section_target_ = nullptr;
  pinned_separator_ = nullptr;

  const bool has_tree = !model.pinned_tree.empty() ||
                        !model.normal_tree.empty();
  // True zero-context: no browser/tab-strip AND no model content.
  if (!HasTabStripContext(browser) && !has_tree) {
    new_tab_button_ = AddInlineRow(
        tab_rows_, u"+ New Tab",
        base::BindRepeating(&MahoSidebarTabListView::OpenNewTab,
                            weak_factory_.GetWeakPtr()),
        /*active=*/false, /*subtle=*/true);
    return;
  }

  const MahoSidebarTabListModel* effective_model = &model;
  MahoSidebarTabListModel fallback_model;
  // Only fall back to the live strip when there is genuinely no space
  // context yet (bridge not initialized). An empty per-space tree for a
  // valid active_space_id is a legitimate state — that space simply has no
  // tabs of its own. Falling back in that case would leak every other
  // space's strip tabs into this space's sidebar.
  if (!has_tree && active_space_id_.empty()) {
    fallback_model =
        BuildLiveStripFallbackTabListModel(browser, active_space_id_);
    effective_model = &fallback_model;
  }

  RebuildTreeSection(tab_rows_, effective_model->pinned_tree,
                     MahoSidebarTabSection::kPinned, browser);
  auto separator = std::make_unique<SidebarSeparatorRow>();
  pinned_separator_ = tab_rows_->AddChildView(std::move(separator));
  const bool pinned_visible = !effective_model->pinned_tree.empty() ||
                              pinned_drop_lane_revealed_;
  auto* pinned_drop_target =
      static_cast<SidebarSectionDropTarget*>(pinned_section_target_);
  if (effective_model->pinned_tree.empty()) {
    pinned_drop_target->SetMinDropTargetHeight(kRowHeightDp);
  }
  pinned_section_target_->SetVisible(pinned_visible);
  pinned_separator_->SetVisible(true);
  new_tab_button_ = AddInlineRow(
      tab_rows_, u"+ New Tab",
      base::BindRepeating(&MahoSidebarTabListView::OpenNewTab,
                          weak_factory_.GetWeakPtr()),
      false, true);
  RebuildTreeSection(tab_rows_, effective_model->normal_tree,
                     MahoSidebarTabSection::kNormal, browser);

  // After rebuilding, propagate layout through the full ancestor chain so the
  // outer scroll view owned by MahoSidebarView commits the new content bounds
  // before we synthesize hover updates. Then fire an immediate synthesize for
  // the common case, plus a deferred follow-up on the next UI task to catch
  // native tracking-area state that settles slightly later on macOS.
  if (GetWidget() && GetWidget()->IsVisible()) {
    for (views::View* current = this; current; current = current->parent()) {
      current->DeprecatedLayoutImmediately();
    }
    if (GetWidget()->HasCapture()) {
      // Active mouse-press in progress; synthesizing a mouse move would reset
      // the pressed button's STATE_PRESSED and kill NotifyClick.
      return;
    }
    GetWidget()->SynthesizeMouseMoveEvent();
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(
            [](base::WeakPtr<MahoSidebarTabListView> weak_self) {
              if (!weak_self) {
                return;
              }
              views::Widget* widget = weak_self->GetWidget();
              if (widget && widget->IsVisible() && !widget->HasCapture()) {
                widget->SynthesizeMouseMoveEvent();
              }
            },
            weak_factory_.GetWeakPtr()));
  }
}

void MahoSidebarTabListView::RebuildTreeSection(
    views::View* parent,
    const std::vector<SidebarTreeNode>& nodes,
    MahoSidebarTabSection section,
    Browser* browser) {
  auto* section_container = AddSectionContainer(parent, section);

  if (section == MahoSidebarTabSection::kPinned) {
    pinned_section_target_ = section_container;
  } else if (section == MahoSidebarTabSection::kNormal) {
    normal_section_target_ = section_container;
  }

  auto* section_drop_target =
      static_cast<SidebarSectionDropTarget*>(section_container);
  section_drop_target->SetPopulated(!nodes.empty());

  if (favorites_drop_accepted_callback_) {
    section_drop_target->SetFavoritesDropAcceptedCallback(
        favorites_drop_accepted_callback_);
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
    RebuildTreeNodes(rows_container, nodes, section, browser, std::string());
  } else {
    auto* drop_target =
        static_cast<SidebarSectionDropTarget*>(section_container);
    drop_target->SetMinDropTargetHeight(kRowHeightDp);
  }
}

void MahoSidebarTabListView::RebuildTreeNodes(
    views::View* parent,
    const std::vector<SidebarTreeNode>& nodes,
    MahoSidebarTabSection section,
    Browser* browser,
    const std::string& parent_folder_id) {
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

      auto* before_folder_lane =
          parent->AddChildView(std::make_unique<SidebarInsertionLaneView>(
              active_space_id_, node.folder_id, SidebarNodeKind::kFolder,
              node.folder_id, false, sibling_parent_folder_id, -1, section));
      if (favorites_drop_accepted_callback_) {
        before_folder_lane->SetFavoritesDropAcceptedCallback(
            favorites_drop_accepted_callback_);
      }
      before_folder_lane->SetRevealEmptyPinnedDropLaneCallback(
          base::BindRepeating(&MahoSidebarTabListView::RevealEmptyPinnedDropLane,
                              base::Unretained(this)));
      auto* folder_row = parent->AddChildView(std::make_unique<SidebarFolderRowView>(
          node, section, active_space_id_, browser,
          base::BindRepeating(&MahoSidebarTabListView::ToggleFolderExpanded,
                              weak_factory_.GetWeakPtr(), node.folder_id),
          base::BindRepeating(&MahoSidebarTabListView::CommitFolderNameChange,
                              weak_factory_.GetWeakPtr()),
          next_sibling_folder_id, sibling_parent_folder_id));
      if (favorites_drop_accepted_callback_) {
        folder_row->SetFavoritesDropAcceptedCallback(
            favorites_drop_accepted_callback_);
      }
      folder_row->SetRevealEmptyPinnedDropLaneCallback(
          base::BindRepeating(&MahoSidebarTabListView::RevealEmptyPinnedDropLane,
                              base::Unretained(this)));

      folder_row->children_snapshot_ = node.children;

      folder_row->SetHoverPopupCallbacks(
          base::BindRepeating(&MahoSidebarFolderHoverController::ScheduleShow,
                              folder_hover_controller_->AsWeakPtr(),
                              folder_row, node.folder_id, node.folder_name),
          base::BindRepeating(&MahoSidebarFolderHoverController::OnRowMouseExited,
                              folder_hover_controller_->AsWeakPtr()));

      folder_row->SetSpringExpandCallback(
          base::BindRepeating(&MahoSidebarTabListView::ToggleFolderExpanded,
                              weak_factory_.GetWeakPtr(), node.folder_id));

      if (node.is_expanded && !node.children.empty()) {
        RebuildTreeNodes(parent, node.children, section, browser,
                         node.folder_id);
      }

      if (index + 1 == nodes.size()) {
        auto* append_folder_lane =
            parent->AddChildView(std::make_unique<SidebarInsertionLaneView>(
                active_space_id_, node.folder_id, SidebarNodeKind::kFolder,
                next_sibling_folder_id.empty() ? node.folder_id
                                               : next_sibling_folder_id,
                true, sibling_parent_folder_id, -1, section));
        if (favorites_drop_accepted_callback_) {
          append_folder_lane->SetFavoritesDropAcceptedCallback(
              favorites_drop_accepted_callback_);
        }
        append_folder_lane->SetRevealEmptyPinnedDropLaneCallback(
            base::BindRepeating(&MahoSidebarTabListView::RevealEmptyPinnedDropLane,
                                base::Unretained(this)));
      }
    } else if (node.kind == SidebarNodeKind::kSplitGroup) {
      auto split_container = std::make_unique<views::BoxLayoutView>();
      split_container->SetOrientation(views::LayoutOrientation::kVertical);
      split_container->SetInsideBorderInsets(gfx::Insets::TLBR(0, 4, 0, 0));
      split_container->SetBorder(views::CreateSolidSidedBorder(
          gfx::Insets::TLBR(0, 2, 0, 0),
          SkColorSetA(SK_ColorGRAY, 0x60)));
      auto* container_ptr = parent->AddChildView(std::move(split_container));
      RebuildTreeNodes(container_ptr, node.children, section, browser,
                       parent_folder_id);
    } else {
      const int live_index = node.tab_strip_index >= 0
                                 ? node.tab_strip_index
                                 : ResolveTabStripIndexForTabId(node.tab_id);
      const bool is_suspended = (live_index < 0);
      content::WebContents* contents = nullptr;
      tabs::TabInterface* tab_iface = nullptr;
      if (browser && browser->tab_strip_model() && live_index >= 0) {
        contents = browser->tab_strip_model()->GetWebContentsAt(live_index);
        if (contents) {
          tab_iface = tabs::TabInterface::GetFromContents(contents);
        }
      }
      TabUIHelper* tab_ui =
          tab_iface ? TabUIHelper::From(tab_iface) : nullptr;
      ui::ImageModel fav = tab_ui ? tab_ui->GetFavicon() : ui::ImageModel();
      if (fav.IsEmpty()) {
        fav = ui::ImageModel::FromVectorIcon(vector_icons::kGlobeIcon,
                                             ui::kColorSysOnSurfaceSubtle,
                                             kRowIconSizeDp);
      }

      auto* before_tab_lane =
          parent->AddChildView(std::make_unique<SidebarInsertionLaneView>(
              active_space_id_, node.tab_id, SidebarNodeKind::kTab, node.tab_id,
              false, parent_folder_id, static_cast<int>(index), section));
      if (favorites_drop_accepted_callback_) {
        before_tab_lane->SetFavoritesDropAcceptedCallback(
            favorites_drop_accepted_callback_);
      }
      before_tab_lane->SetRevealEmptyPinnedDropLaneCallback(
          base::BindRepeating(&MahoSidebarTabListView::RevealEmptyPinnedDropLane,
                              base::Unretained(this)));
      auto* row_view = parent->AddChildView(std::make_unique<SidebarTabRowView>(
          node, std::move(fav), browser, section, active_space_id_,
          ResolveTabDisplayText(node),
          base::BindRepeating(&MahoSidebarTabListView::ActivateTabById,
                              weak_factory_.GetWeakPtr(),
                              node.tab_id),
          base::BindRepeating(&MahoSidebarTabListView::CloseTabById,
                              weak_factory_.GetWeakPtr(),
                              node.tab_id),
          base::BindRepeating(&MahoSidebarTabListView::CommitTabTitleChange,
                              weak_factory_.GetWeakPtr()),
          base::BindRepeating(&MahoSidebarTabListView::ResolveTabStripIndexForTabId,
                              base::Unretained(this)),
          base::BindRepeating(&MahoSidebarTabListView::HandleTabRowHoverStart,
                              base::Unretained(this)),
          base::BindRepeating(&MahoSidebarTabListView::HandleTabRowHoverEnd,
                              base::Unretained(this))));
      row_view->SetSuspended(is_suspended);
      if (favorites_drop_accepted_callback_) {
        row_view->SetFavoritesDropAcceptedCallback(
            favorites_drop_accepted_callback_);
      }
      row_view->SetRevealEmptyPinnedDropLaneCallback(
          base::BindRepeating(&MahoSidebarTabListView::RevealEmptyPinnedDropLane,
                              base::Unretained(this)));
      row_view->SetTooltipText(GetNodeTooltipText(node));

      if (index + 1 == nodes.size()) {
        auto* append_tab_lane =
            parent->AddChildView(std::make_unique<SidebarInsertionLaneView>(
                active_space_id_, node.tab_id, SidebarNodeKind::kTab, node.tab_id,
                true, parent_folder_id, static_cast<int>(index + 1), section));
        if (favorites_drop_accepted_callback_) {
          append_tab_lane->SetFavoritesDropAcceptedCallback(
              favorites_drop_accepted_callback_);
        }
        append_tab_lane->SetRevealEmptyPinnedDropLaneCallback(
            base::BindRepeating(&MahoSidebarTabListView::RevealEmptyPinnedDropLane,
                                base::Unretained(this)));
      }
    }
  }
}

std::u16string MahoSidebarTabListView::GetNodeDisplayText(
    const SidebarTreeNode& node) const {
  return ResolveTabDisplayText(node);
}

std::u16string MahoSidebarTabListView::GetNodeTooltipText(
    const SidebarTreeNode& node) const {
  const std::u16string display = ResolveTabDisplayText(node);
  if (node.host.empty() || node.host == display) {
    return display;
  }
  if (display.empty()) {
    return node.host;
  }
  return display + u"\n" + node.host;
}

void MahoSidebarTabListView::ActivateTab(int tab_index) {
  TabStripModel* model = browser_ ? browser_->tab_strip_model() : nullptr;
  if (!model || !model->ContainsIndex(tab_index)) {
    return;
  }

  model->ActivateTabAt(tab_index);
}

namespace {

struct PersistedTabInfo {
  std::string url;
  std::string title;
  bool is_pinned = false;
};

std::optional<PersistedTabInfo> QueryTabFromCore(const std::string& tab_id) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return std::nullopt;
  }
  char* json_str = maho_core_get_tab_view_models(core);
  if (!json_str) {
    return std::nullopt;
  }
  std::string json(json_str);
  maho_string_free(json_str);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    return std::nullopt;
  }

  for (const auto& item : parsed->GetList()) {
    const auto* dict = item.GetIfDict();
    if (!dict) {
      continue;
    }
    const std::string* id = dict->FindString("id");
    if (id && *id == tab_id) {
      const std::string* url = dict->FindString("url");
      const std::string* title = dict->FindString("title");
      std::optional<bool> is_pinned = dict->FindBool("isPinned");
      PersistedTabInfo info;
      if (url) info.url = *url;
      if (title) info.title = *title;
      if (is_pinned) info.is_pinned = *is_pinned;
      return info;
    }
  }
  return std::nullopt;
}

}  // namespace

void MahoSidebarTabListView::ActivateTabById(const std::string& tab_id) {
  const int index = ResolveTabStripIndexForTabId(tab_id);
  if (index >= 0) {
    ActivateTab(index);
  } else {
    WakeSuspendedTab(tab_id);
  }
}

void MahoSidebarTabListView::WakeSuspendedTab(const std::string& tab_id) {
  auto persisted = QueryTabFromCore(tab_id);
  if (!persisted) {
    return;
  }

  MahoTabIdHelper::SetPendingRestoredTabId(tab_id);

  NavigateParams params(browser_, GURL(persisted->url),
                        ui::PAGE_TRANSITION_AUTO_BOOKMARK);
  params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  Navigate(&params);

  // If the persisted tab was pinned, re-pin in the new WebContents' tab strip slot
  if (persisted->is_pinned && params.navigated_or_inserted_contents) {
    TabStripModel* model = browser_->tab_strip_model();
    int new_idx = model->GetIndexOfWebContents(params.navigated_or_inserted_contents);
    if (new_idx != TabStripModel::kNoTab && !model->IsTabPinned(new_idx)) {
      model->SetTabPinned(new_idx, true);
    }
  }
}

void MahoSidebarTabListView::CloseTab(int tab_index) {
  TabStripModel* model = browser_ ? browser_->tab_strip_model() : nullptr;
  if (!model || !model->ContainsIndex(tab_index)) {
    return;
  }

  if (content::WebContents* wc = model->GetWebContentsAt(tab_index)) {
    if (auto* helper = MahoTabIdHelper::FromWebContents(wc);
        helper && !helper->stable_tab_id().empty()) {
      auto* bridge = MahoSpaceProfileBridge::GetInstance();
      std::string active_space_id = bridge ? bridge->GetActiveSpaceId(browser_) : std::string();
      if (!active_space_id.empty()) {
        maho::DispatchShellEvent("close_tab", {
            {"tab_id", helper->stable_tab_id()},
            {"expected_space_id", active_space_id}
        });
      } else {
        // expected_space_id is intentionally omitted when active_space_id is empty
        // (bridge not yet initialized — startup window). Rust core's CloseTab handler
        // treats None as "no validation"; the close is processed against tab.space_id
        // directly, which is the correct fallback during startup before the bridge
        // has a space context.
        maho::DispatchShellEvent("close_tab", {{"tab_id", helper->stable_tab_id()}});
      }
    }
  }
  model->CloseWebContentsAt(tab_index, TabCloseTypes::CLOSE_USER_GESTURE |
                                           TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB);
}

void MahoSidebarTabListView::CloseTabById(const std::string& tab_id) {
  const int index = ResolveTabStripIndexForTabId(tab_id);
  if (index >= 0) {
    CloseTab(index);
  } else {
    // If it's a suspended tab (index < 0), close it directly in rust core
    auto* bridge = MahoSpaceProfileBridge::GetInstance();
    std::string active_space_id = bridge ? bridge->GetActiveSpaceId(browser_) : std::string();
    if (!active_space_id.empty()) {
      maho::DispatchShellEvent("close_tab", {
          {"tab_id", tab_id},
          {"expected_space_id", active_space_id}
      });
    } else {
      // expected_space_id is intentionally omitted when active_space_id is empty
      // (bridge not yet initialized — startup window). Rust core's CloseTab handler
      // treats None as "no validation"; the close is processed against tab.space_id
      // directly, which is the correct fallback during startup before the bridge
      // has a space context.
      maho::DispatchShellEvent("close_tab", {{"tab_id", tab_id}});
    }
  }
}

int MahoSidebarTabListView::ResolveTabStripIndexForTabId(
    const std::string& tab_id) const {
  if (tab_id.empty()) {
    return -1;
  }
  TabStripModel* strip = browser_ ? browser_->tab_strip_model() : nullptr;
  if (!strip) {
    return -1;
  }
  // Single authoritative path: MahoTabIdHelper::stable_tab_id() is the sole
  // identity source for mapping core tab IDs to live WebContents.
  for (int i = 0; i < strip->count(); ++i) {
    auto* contents = strip->GetWebContentsAt(i);
    if (!contents) {
      continue;
    }
    auto* helper = MahoTabIdHelper::FromWebContents(contents);
    if (helper && helper->stable_tab_id() == tab_id) {
      return i;
    }
  }
  return -1;
}

void MahoSidebarTabListView::OpenNewTab(const ui::Event& event) {
  if (!browser_) {
    return;
  }

  // Open the command palette in new-tab mode. The tab is only created once the
  // user enters a URL; dismissing without input creates no tab.
  auto* browser_view = BrowserView::GetBrowserViewForBrowser(browser_);
  if (!browser_view) {
    return;
  }
  for (views::View* child : browser_view->children()) {
    auto* container =
        views::AsViewClass<MahoSidebarContainerView>(child);
    if (container) {
      container->ShowCommandOverlayForNewTab();
      return;
    }
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

void MahoSidebarTabListView::CommitFolderNameChange(
    const std::string& folder_id,
    const std::u16string& new_name) {
  if (active_space_id_.empty() || folder_id.empty()) {
    return;
  }
  DispatchShellEvent("rename_folder",
                     {{"space_id", active_space_id_},
                      {"folder_id", folder_id},
                      {"name", base::UTF16ToUTF8(new_name)}});
}

std::u16string MahoSidebarTabListView::ResolveTabDisplayText(
    const SidebarTreeNode& node) const {
  auto local_it = local_custom_titles_.find(node.tab_id);
  if (local_it != local_custom_titles_.end()) {
    return local_it->second;
  }
  const std::u16string resolved = MahoDisplayPolicy::ResolveTabDisplayText(
      GURL(node.url), node.title, node.custom_title);
  if (!resolved.empty()) {
    return resolved;
  }
  if (!node.host.empty()) {
    return node.host;
  }
  return node.title;
}

void MahoSidebarTabListView::SetLastFavoritesModel(
    const MahoSidebarFavoritesModel& model) {
  last_favorites_model_ = model;
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
  for (const auto& item : last_favorites_model_.items) {
    if (!item.tab_id.empty()) {
      live_tab_ids.insert(item.tab_id);
    }
  }

  for (auto it = local_custom_titles_.begin(); it != local_custom_titles_.end();) {
    if (!live_tab_ids.contains(it->first)) {
      it = local_custom_titles_.erase(it);
    } else {
      ++it;
    }
  }
}

void MahoSidebarTabListView::HandleTabRowHoverStart(views::View* anchor_view,
                                                    int tab_index) {
  if (tab_preview_controller_) {
    tab_preview_controller_->ShowPreview(anchor_view, tab_index);
  }
}

void MahoSidebarTabListView::HandleTabRowHoverEnd() {
  if (tab_preview_controller_) {
    tab_preview_controller_->HidePreview();
  }
}

std::string MahoSidebarTabListView::FindCoreTabIdByWebContents(
    content::WebContents* contents) const {
  if (!contents) {
    return std::string();
  }
  auto* helper = MahoTabIdHelper::FromWebContents(contents);
  if (helper && !helper->stable_tab_id().empty()) {
    return helper->stable_tab_id();
  }
  return std::string();
}

}  // namespace maho
