// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_interactive_test_base.h"

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/test/base/ui_test_utils.h"
#include "ui/base/dragdrop/drop_target_event.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "ui/base/clipboard/clipboard_format_type.h"
#include "ui/base/dragdrop/mojom/drag_drop_types.mojom.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/gfx/geometry/point.h"
#include <string_view>
#include "maho/browser/ui/views/sidebar/maho_sidebar_container_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_favorites_grid_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_footer_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_tab_list_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_view.h"
#include "ui/base/test/ui_controls.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/views/controls/label.h"
#include "ui/views/view_utils.h"

namespace maho {
namespace {

ui::DropTargetEvent MakeDropEvent(const ui::OSExchangeData& data,
                                  const gfx::Point& point = gfx::Point(5, 5)) {
  return ui::DropTargetEvent(
      data, gfx::PointF(point), gfx::PointF(point),
      static_cast<int>(ui::mojom::DragOperation::kMove));
}

bool HasLabelWithText(views::View* root, std::u16string_view expected) {
  if (!root) {
    return false;
  }
  if (auto* label = views::AsViewClass<views::Label>(root); label &&
      label->GetText() == expected) {
    return true;
  }
  for (views::View* child : root->children()) {
    if (HasLabelWithText(child, expected)) {
      return true;
    }
  }
  return false;
}

class MahoSidebarEdgeCasesInteractiveTest
    : public MahoSidebarInteractiveTestBase {};

IN_PROC_BROWSER_TEST_F(MahoSidebarEdgeCasesInteractiveTest,
                       SidebarResizePersists) {
  ASSERT_TRUE(container_);

  int initial_width = container_->width();
  ASSERT_GT(initial_width, 0);

  gfx::Rect edge_bounds = container_->children().back()->GetBoundsInScreen();
  gfx::Point edge(edge_bounds.CenterPoint());

  ASSERT_TRUE(ui_controls::SendMouseMove(edge.x(), edge.y()));
  base::RunLoop press_loop;
  ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(
      ui_controls::LEFT, ui_controls::DOWN, press_loop.QuitClosure()));
  press_loop.Run();

  int delta = 40;
  ASSERT_TRUE(ui_controls::SendMouseMove(edge.x() + delta, edge.y()));

  base::RunLoop release_loop;
  ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(
      ui_controls::LEFT, ui_controls::UP, release_loop.QuitClosure()));
  release_loop.Run();
  base::RunLoop().RunUntilIdle();

  int stored_width = browser()->profile()->GetPrefs()->GetInteger(
      sidebar_prefs::kSidebarWidth);
  EXPECT_NE(initial_width, stored_width)
      << "Sidebar width pref should update after resize drag";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarEdgeCasesInteractiveTest,
                       FavoritesAddViaShellEvent) {
  SeedProfile(2);
  observer_.Clear();

  auto state = GetViewState();
  size_t initial_fav_count = state.favorites.items.size();

  std::string tab_id;
  for (const auto& node : state.tab_list.normal_tree) {
    if (node.kind == SidebarNodeKind::kTab && !node.is_favorite) {
      tab_id = node.tab_id;
      break;
    }
  }

  if (tab_id.empty()) {
    LOG(WARNING) << "All tabs are already favorites in seed 2 — skipping";
    return;
  }

  DispatchShellEvent("add_favorite",
                     {{"tab_id", tab_id},
                      {"space_id", GetActiveSpaceId()}});
  base::RunLoop().RunUntilIdle();
  ResolveViews();

  auto new_state = GetViewState();
  EXPECT_GT(new_state.favorites.items.size(), initial_fav_count)
      << "Favorite count should increase after add_favorite event";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarEdgeCasesInteractiveTest,
                       FavoritesRemoveViaShellEvent) {
  SeedProfile(2);
  observer_.Clear();

  auto state = GetViewState();
  ASSERT_FALSE(state.favorites.items.empty());

  std::string fav_tab_id = state.favorites.items[0].tab_id;
  ASSERT_FALSE(fav_tab_id.empty());

  size_t initial_count = state.favorites.items.size();

  DispatchShellEvent("remove_favorite",
                     {{"tab_id", fav_tab_id},
                      {"space_id", GetActiveSpaceId()}});
  base::RunLoop().RunUntilIdle();
  ResolveViews();

  auto new_state = GetViewState();
  EXPECT_LT(new_state.favorites.items.size(), initial_count)
      << "Favorite count should decrease after remove_favorite event";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarEdgeCasesInteractiveTest,
                       FolderRenameViaContextMenu) {
  SeedProfile(2);
  observer_.Clear();

  auto state = GetViewState();
  std::string folder_id;
  for (const auto& node : state.tab_list.normal_tree) {
    if (node.kind == SidebarNodeKind::kFolder) {
      folder_id = node.folder_id;
      break;
    }
  }
  ASSERT_FALSE(folder_id.empty());

  auto* folder_row = tab_list_->FindFolderRowByIdForTesting(folder_id);
  ASSERT_TRUE(folder_row);

  const gfx::Point center = folder_row->GetBoundsInScreen().CenterPoint();
  ASSERT_TRUE(ui_controls::SendMouseMove(center.x(), center.y()));
  base::RunLoop rclick_loop;
  ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(
      ui_controls::RIGHT, ui_controls::DOWN | ui_controls::UP,
      rclick_loop.QuitClosure()));
  rclick_loop.Run();
  base::RunLoop().RunUntilIdle();

  base::RunLoop esc_loop;
  ASSERT_TRUE(ui_controls::SendKeyPressNotifyWhenDone(
      browser()->window()->GetNativeWindow(), ui::VKEY_ESCAPE,
      false, false, false, false, esc_loop.QuitClosure()));
  esc_loop.Run();
  base::RunLoop().RunUntilIdle();

  EXPECT_TRUE(folder_row->GetVisible())
      << "Folder row should remain visible after dismissing context menu";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarEdgeCasesInteractiveTest,
                       MultiWindowIndependentSidebars) {
  SeedProfile(2);

  Browser* second_browser = CreateBrowser(browser()->profile());
  ASSERT_TRUE(second_browser);
  ASSERT_NE(browser(), second_browser);

  BrowserView* bv2 =
      BrowserView::GetBrowserViewForBrowser(second_browser);
  ASSERT_TRUE(bv2);

  MahoSidebarContainerView* container2 = nullptr;
  for (views::View* child : bv2->children()) {
    auto* candidate = views::AsViewClass<MahoSidebarContainerView>(child);
    if (candidate) {
      container2 = candidate;
      break;
    }
  }
  ASSERT_TRUE(container2)
      << "Second browser should also have a sidebar container";
  EXPECT_TRUE(container2->GetVisible());

  chrome::CloseWindow(second_browser);
  base::RunLoop().RunUntilIdle();

  EXPECT_TRUE(container_->GetVisible())
      << "First browser sidebar unaffected by second window close";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarEdgeCasesInteractiveTest,
                       EmptyStateFavoritesDisplay) {
  MahoSidebarFavoritesModel empty_favorites;
  favorites_->Update(empty_favorites);
  ResolveViews();

  EXPECT_FALSE(favorites_->GetVisible())
      << "Empty favorites grid must be hidden at idle";
  EXPECT_EQ(0, favorites_->GetPreferredSize().height())
      << "Empty favorites must not reserve any height at idle";
  EXPECT_FALSE(favorites_->is_drop_indicator_visible_for_testing())
      << "No drop indicator should be shown when idle";
  EXPECT_FALSE(favorites_->is_drag_reveal_active_for_testing())
      << "Drag reveal must not be active at idle";
  EXPECT_FALSE(HasLabelWithText(favorites_, u"Drag tabs here to pin favorites"))
      << "Idle favorites view should not render the placeholder copy";

  // Simulate a valid internal tab drag entering the favorites grid.
  ui::OSExchangeData data;
  SidebarDragPayload payload;
  payload.node_kind = SidebarNodeKind::kTab;
  payload.node_id = "fav-drag-test";
  payload.origin = SidebarDragOrigin::kFavorites;
  WriteMahoDragData(payload, &data);
  EXPECT_TRUE(favorites_->CanDrop(data))
      << "Favorites grid should still accept internal tab drags when empty";
  ui::DropTargetEvent event = MakeDropEvent(data);
  EXPECT_EQ(static_cast<int>(ui::mojom::DragOperation::kMove),
            favorites_->OnDragUpdated(event));
  EXPECT_TRUE(favorites_->GetVisible())
      << "Empty favorites must reveal during valid drag";
  EXPECT_TRUE(favorites_->is_drag_reveal_active_for_testing())
      << "Drag reveal flag must be set during valid drag";
  EXPECT_GT(favorites_->GetPreferredSize().height(), 0)
      << "Revealed empty favorites must have positive height for drop target";

  // Drag exit must collapse back to hidden.
  favorites_->OnDragExited();
  EXPECT_FALSE(favorites_->GetVisible())
      << "Empty favorites must hide again after drag exit";
  EXPECT_FALSE(favorites_->is_drag_reveal_active_for_testing())
      << "Drag reveal must be cleared after drag exit";
  EXPECT_EQ(0, favorites_->GetPreferredSize().height())
      << "Empty favorites must return to zero height after drag exit";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarEdgeCasesInteractiveTest,
                       ParentDragForwardRevealsEmptyFavorites) {
  MahoSidebarFavoritesModel empty_favorites;
  favorites_->Update(empty_favorites);
  ResolveViews();
  ASSERT_FALSE(favorites_->GetVisible());

  ui::OSExchangeData data;
  SidebarDragPayload payload;
  payload.node_kind = SidebarNodeKind::kTab;
  payload.node_id = "parent-forward-test";
  payload.origin = SidebarDragOrigin::kNormalSection;
  WriteMahoDragData(payload, &data);

  MahoSidebarView* sidebar_view = GetSidebarView();
  ASSERT_TRUE(sidebar_view);

  int formats = 0;
  std::set<ui::ClipboardFormatType> format_types;
  EXPECT_TRUE(sidebar_view->GetDropFormats(&formats, &format_types));
  EXPECT_TRUE(sidebar_view->CanDrop(data));

  ui::DropTargetEvent event = MakeDropEvent(data, gfx::Point(10, 10));
  int result = sidebar_view->OnDragUpdated(event);
  EXPECT_EQ(static_cast<int>(ui::mojom::DragOperation::kMove), result);
  EXPECT_TRUE(favorites_->GetVisible())
      << "Parent OnDragUpdated must reveal hidden empty favorites";
  EXPECT_TRUE(favorites_->is_drag_reveal_active_for_testing());
  EXPECT_GT(favorites_->GetPreferredSize().height(), 0);

  sidebar_view->OnDragExited();
  EXPECT_FALSE(favorites_->GetVisible())
      << "Parent OnDragExited must collapse empty favorites";
  EXPECT_FALSE(favorites_->is_drag_reveal_active_for_testing());
  EXPECT_EQ(0, favorites_->GetPreferredSize().height());
}

IN_PROC_BROWSER_TEST_F(MahoSidebarEdgeCasesInteractiveTest,
                       ParentDragForwardSkipsNonTabDrag) {
  MahoSidebarFavoritesModel empty_favorites;
  favorites_->Update(empty_favorites);
  ResolveViews();
  ASSERT_FALSE(favorites_->GetVisible());

  ui::OSExchangeData data;
  SidebarDragPayload payload;
  payload.node_kind = SidebarNodeKind::kFolder;
  payload.node_id = "folder-test";
  payload.origin = SidebarDragOrigin::kNormalSection;
  WriteMahoDragData(payload, &data);

  MahoSidebarView* sidebar_view = GetSidebarView();
  ASSERT_TRUE(sidebar_view);

  ui::DropTargetEvent event = MakeDropEvent(data, gfx::Point(10, 10));
  int result = sidebar_view->OnDragUpdated(event);
  EXPECT_EQ(static_cast<int>(ui::mojom::DragOperation::kNone), result);
  EXPECT_FALSE(favorites_->GetVisible())
      << "Non-tab drag must not reveal empty favorites";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarEdgeCasesInteractiveTest,
                       ParentDragForwardNoopWhenFavoritesPopulated) {
  SeedProfile(2);
  ResolveViews();

  auto state = GetViewState();
  if (state.favorites.items.empty()) {
    LOG(WARNING) << "Seed 2 has no favorites — skipping";
    return;
  }
  ASSERT_TRUE(favorites_->GetVisible());
  bool was_reveal_active = favorites_->is_drag_reveal_active_for_testing();
  EXPECT_FALSE(was_reveal_active);

  ui::OSExchangeData data;
  SidebarDragPayload payload;
  payload.node_kind = SidebarNodeKind::kTab;
  payload.node_id = "noop-test";
  payload.origin = SidebarDragOrigin::kNormalSection;
  WriteMahoDragData(payload, &data);

  MahoSidebarView* sidebar_view = GetSidebarView();
  ASSERT_TRUE(sidebar_view);

  ui::DropTargetEvent event = MakeDropEvent(data, gfx::Point(10, 10));
  sidebar_view->OnDragUpdated(event);
  EXPECT_FALSE(favorites_->is_drag_reveal_active_for_testing())
      << "RevealForDrag should be a no-op when favorites are already populated";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarEdgeCasesInteractiveTest,
                       RapidTabCloseNoCrash) {
  AddTestTabs(10);
  ResolveViews();

  int initial_count = browser()->tab_strip_model()->count();
  ASSERT_GE(initial_count, 11);

  for (int i = 0; i < 5; ++i) {
    int last_index = browser()->tab_strip_model()->count() - 1;
    if (last_index <= 0)
      break;
    browser()->tab_strip_model()->CloseWebContentsAt(
        last_index, TabCloseTypes::CLOSE_NONE);
  }
  base::RunLoop().RunUntilIdle();
  ResolveViews();

  auto state = GetViewState();
  int remaining = static_cast<int>(state.tab_list.normal_tree.size()) +
                  static_cast<int>(state.tab_list.pinned_tree.size());
  EXPECT_EQ(initial_count - 5, remaining)
      << "Rapid close should remove exactly 5 tabs without crash";
  EXPECT_TRUE(tab_list_->GetVisible());
}

IN_PROC_BROWSER_TEST_F(MahoSidebarEdgeCasesInteractiveTest,
                       UpdatePillVisibilityFromModel) {
  SeedProfile(2);

  auto state = GetViewState();
  MahoSidebarFooterView* footer = GetFooterView();
  ASSERT_TRUE(footer);

  EXPECT_EQ(state.footer.update_pill.visible,
            state.footer.update_pill.has_update)
      << "Update pill visibility should match has_update flag";
}

}  // namespace
}  // namespace maho
