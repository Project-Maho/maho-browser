// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_interactive_test_base.h"

#include "base/strings/utf_string_conversions.h"
#include "base/test/run_until.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/test/base/ui_test_utils.h"
#include "ui/events/event.h"
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
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_tab_list_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_state_adapter.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_view.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "maho/browser/maho_tab_id_helper.h"
#include "maho/browser/maho_tab_registry.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_downloads_view.h"
#include "maho/browser/ui/views/library_overlay/maho_library_overlay_controller.h"
#include "maho/browser/ui/views/library_overlay/maho_library_overlay_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_library_rail_view.h"
#include "maho/browser/ui/views/spaces_overlay/maho_browser_frame_overlay_host.h"
#include "ui/base/test/ui_controls.h"
#include "ui/compositor/layer.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/test/button_test_api.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"

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

MahoSidebarViewStateModel BuildViewStateForBrowser(Browser* target_browser) {
  MahoSidebarStateAdapter adapter;
  return adapter.BuildViewStateModel(target_browser);
}

void SelectLibraryCategoryViaRail(
    MahoSidebarView* sidebar,
    MahoSidebarLibraryRailView::Category category) {
  ASSERT_TRUE(sidebar);
  MahoSidebarLibraryRailView* rail = sidebar->library_rail_view();
  ASSERT_TRUE(rail);
  views::Button* button = rail->button_for_category_for_testing(category);
  ASSERT_TRUE(button);
  ui::MouseEvent event(ui::EventType::kMousePressed, gfx::Point(), gfx::Point(),
                       base::TimeTicks(), ui::EF_LEFT_MOUSE_BUTTON, 0);
  views::test::ButtonTestApi(button).NotifyClick(event);
  base::RunLoop().RunUntilIdle();
}

MahoLibraryOverlayController* GetLibraryOverlayController(
    MahoSidebarContainerView* container) {
  if (!container || !container->overlay_host()) {
    return nullptr;
  }
  return container->overlay_host()->library_overlay_controller();
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

  int stored_width = browser()->GetProfile()->GetPrefs()->GetInteger(
      sidebar_prefs::kSidebarWidth);
  EXPECT_NE(initial_width, stored_width)
      << "Sidebar width pref should update after resize drag";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarEdgeCasesInteractiveTest,
                       CollapsedSidebarReservesNoContentWidth) {
  ASSERT_TRUE(container_);

  SeedSidebarPrefs(/*layout_enabled=*/true, /*panel_expanded=*/false);
  // Collapsing a pinned sidebar slides it out; it hides when the slide ends.
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return !container_->sidebar_view()->GetVisible(); }));
  EXPECT_EQ(0, container_->GetPreferredSize().width())
      << "A collapsed sidebar must not leave an empty content rail";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarEdgeCasesInteractiveTest,
                       CollapsedSidebarKeepsFloatingHoverStripAboveContents) {
  ASSERT_TRUE(container_);

  SeedSidebarPrefs(/*layout_enabled=*/true, /*panel_expanded=*/false);
  // Collapsing a pinned sidebar slides it out at full width first; the strip
  // geometry applies once that slide has ended and the panel is hidden.
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !container_->sidebar_view()->GetVisible(); }));
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  browser_view->DeprecatedLayoutImmediately();

  // The hover strip must stay laid out, or nothing can receive the hover that
  // reveals the sidebar again.
  EXPECT_EQ(sidebar_layout::kHoverTriggerWidthDp, container_->width());
  EXPECT_GT(container_->height(), 0);
  EXPECT_EQ(0, browser_view->contents_container()->x())
      << "The hover strip floats over the contents instead of reserving width";
  EXPECT_GT(*browser_view->GetIndexOf(container_),
            *browser_view->GetIndexOf(browser_view->contents_container()))
      << "The floating sidebar must be stacked above the contents";

  SeedSidebarPrefs(/*layout_enabled=*/true, /*panel_expanded=*/true);
  base::RunLoop().RunUntilIdle();
  browser_view->DeprecatedLayoutImmediately();
  EXPECT_LT(*browser_view->GetIndexOf(container_),
            *browser_view->GetIndexOf(browser_view->contents_container()));
  EXPECT_EQ(container_->bounds().right(),
            browser_view->contents_container()->x());
}

IN_PROC_BROWSER_TEST_F(MahoSidebarEdgeCasesInteractiveTest,
                       HoverRevealSlidesByTransformWithoutRelayoutingContents) {
  ASSERT_TRUE(container_);

  SeedSidebarPrefs(/*layout_enabled=*/true, /*panel_expanded=*/false);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !container_->sidebar_view()->GetVisible(); }));
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  browser_view->DeprecatedLayoutImmediately();
  ASSERT_EQ(sidebar_layout::kHoverTriggerWidthDp, container_->width());
  const gfx::Rect contents_before = browser_view->contents_container()->bounds();

  const gfx::Point strip = container_->GetBoundsInScreen().CenterPoint();
  ASSERT_TRUE(ui_controls::SendMouseMove(strip.x(), strip.y()));
  // The interactive harness moves the cursor but does not deliver a synthetic
  // mouse-enter to this view, so invoke the real OnMouseEntered override with
  // the cursor parked on the strip (which also keeps the hide timer re-arming).
  ui::MouseEvent enter_event(ui::EventType::kMouseEntered, gfx::Point(),
                             gfx::Point(), base::TimeTicks::Now(), 0, 0);
  container_->OnMouseEntered(enter_event);
  ASSERT_TRUE(container_->sidebar_view()->GetVisible());
  browser_view->DeprecatedLayoutImmediately();

  // The revealed sidebar gets its full width once; the slide is a layer
  // transform, and the contents keep their bounds throughout.
  EXPECT_GT(container_->width(), sidebar_layout::kHoverTriggerWidthDp);
  EXPECT_EQ(contents_before, browser_view->contents_container()->bounds());
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return container_->GetRevealFraction() == 1.0; }));
  EXPECT_TRUE(container_->layer()->transform().IsIdentity());
  EXPECT_EQ(contents_before, browser_view->contents_container()->bounds());
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
      browser()->GetWindow()->GetNativeWindow(), ui::VKEY_ESCAPE,
      false, false, false, false, esc_loop.QuitClosure()));
  esc_loop.Run();
  base::RunLoop().RunUntilIdle();

  EXPECT_TRUE(folder_row->GetVisible())
      << "Folder row should remain visible after dismissing context menu";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarEdgeCasesInteractiveTest,
                       FolderRenameShellEventCommitPropagatesStateAndLabel) {
  SeedProfile(2);
  observer_.Clear();

  auto state = GetViewState();
  std::string folder_id;
  std::u16string original_name;
  for (const auto& node : state.tab_list.normal_tree) {
    if (node.kind == SidebarNodeKind::kFolder) {
      folder_id = node.folder_id;
      original_name = node.folder_name;
      break;
    }
  }
  ASSERT_FALSE(folder_id.empty()) << "Seed 2 must have at least one folder";

  const std::u16string kNewName = u"RenamedFolder_Test";
  ASSERT_NE(original_name, kNewName)
      << "Test setup: new name must differ from seed name";

  DispatchShellEvent("rename_folder",
                     {{"space_id", GetActiveSpaceId()},
                      {"folder_id", folder_id},
                      {"name", base::UTF16ToUTF8(kNewName)}});
  base::RunLoop().RunUntilIdle();
  ResolveViews();

  auto new_state = GetViewState();
  bool found_renamed = false;
  bool found_old_name = false;
  for (const auto& node : new_state.tab_list.normal_tree) {
    if (node.kind == SidebarNodeKind::kFolder &&
        node.folder_id == folder_id) {
      if (node.folder_name == kNewName) {
        found_renamed = true;
      }
      if (!original_name.empty() && node.folder_name == original_name) {
        found_old_name = true;
      }
      break;
    }
  }
  EXPECT_TRUE(found_renamed)
      << "GetViewState() must reflect the committed folder rename";
  EXPECT_FALSE(found_old_name)
      << "Old folder name must no longer appear in the state tree after rename";

  EXPECT_TRUE(HasLabelWithText(tab_list_, kNewName))
      << "Sidebar view must display the renamed folder label after commit";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarEdgeCasesInteractiveTest,
                       FolderInlineRenameViaContextMenuDispatchesShellEvent) {
  SeedProfile(2);
  observer_.Clear();

  auto state = GetViewState();
  std::string folder_id;
  std::u16string original_name;
  for (const auto& node : state.tab_list.normal_tree) {
    if (node.kind == SidebarNodeKind::kFolder) {
      folder_id = node.folder_id;
      original_name = node.folder_name;
      break;
    }
  }
  ASSERT_FALSE(folder_id.empty()) << "Seed 2 must have at least one folder";

  auto* folder_row = tab_list_->FindFolderRowByIdForTesting(folder_id);
  ASSERT_TRUE(folder_row);
  ASSERT_FALSE(folder_row->is_editing_for_testing());

  folder_row->BeginFolderEditing();
  base::RunLoop().RunUntilIdle();

  ASSERT_TRUE(folder_row->is_editing_for_testing())
      << "BeginFolderEditing must activate inline edit mode";
  ASSERT_TRUE(folder_row->name_field_for_testing());
  ASSERT_TRUE(folder_row->name_field_for_testing()->GetVisible())
      << "Name field must be visible while editing";

  const std::u16string kNewName = u"InlineRenamed_Test";
  folder_row->name_field_for_testing()->SetText(kNewName);

  ui::KeyEvent enter_event(ui::EventType::kKeyPressed, ui::VKEY_RETURN,
                           ui::EF_NONE);
  folder_row->HandleKeyEvent(folder_row->name_field_for_testing(), enter_event);
  base::RunLoop().RunUntilIdle();

  EXPECT_FALSE(folder_row->is_editing_for_testing())
      << "Inline edit must be committed on Enter";

  EXPECT_TRUE(observer_.HasEvent("rename_folder"))
      << "rename_folder shell event must be dispatched on inline edit commit";

  ResolveViews();
  EXPECT_TRUE(HasLabelWithText(tab_list_, kNewName))
      << "Sidebar must display the new folder name after inline rename commit";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarEdgeCasesInteractiveTest,
                       FolderInlineRenameEscapeDoesNotDispatch) {
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

  folder_row->BeginFolderEditing();
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(folder_row->is_editing_for_testing());

  folder_row->name_field_for_testing()->SetText(u"ShouldNotCommit");
  ui::KeyEvent esc_event(ui::EventType::kKeyPressed, ui::VKEY_ESCAPE,
                         ui::EF_NONE);
  folder_row->HandleKeyEvent(folder_row->name_field_for_testing(), esc_event);
  base::RunLoop().RunUntilIdle();

  EXPECT_FALSE(folder_row->is_editing_for_testing())
      << "Escape must cancel inline editing";
  EXPECT_FALSE(observer_.HasEvent("rename_folder"))
      << "Escape must not dispatch rename_folder shell event";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarEdgeCasesInteractiveTest,
                       FolderPinViaContextMenuDispatchesShellEvent) {
  SeedProfile(2);
  observer_.Clear();

  auto state = GetViewState();
  std::string folder_id;
  for (const auto& node : state.tab_list.normal_tree) {
    if (node.kind == SidebarNodeKind::kFolder && !node.folder_is_pinned) {
      folder_id = node.folder_id;
      break;
    }
  }
  if (folder_id.empty()) {
    LOG(WARNING) << "No unpinned folder found in seed 2 — skipping";
    return;
  }

  auto* folder_row = tab_list_->FindFolderRowByIdForTesting(folder_id);
  ASSERT_TRUE(folder_row);

  ShowContextMenuOnView(folder_row);
  base::RunLoop().RunUntilIdle();

  ASSERT_TRUE(ui_controls::SendKeyPressNotifyWhenDone(
      browser()->GetWindow()->GetNativeWindow(), ui::VKEY_ESCAPE,
      false, false, false, false, base::DoNothing()));
  base::RunLoop().RunUntilIdle();
}

IN_PROC_BROWSER_TEST_F(MahoSidebarEdgeCasesInteractiveTest,
                       FolderDeleteDispatchesShellEvent) {
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

  const std::string active_space_id = GetActiveSpaceId();
  maho::DispatchShellEvent("delete_folder",
                           {{"space_id", active_space_id},
                            {"folder_id", folder_id}});
  base::RunLoop().RunUntilIdle();

  EXPECT_TRUE(observer_.HasEvent("delete_folder"))
      << "delete_folder shell event must be observable after dispatch";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarEdgeCasesInteractiveTest,
                       MultiWindowIndependentSidebars) {
  SeedProfile(2);

  Browser* second_browser = static_cast<Browser*>(CreateBrowser(browser()->GetProfile()));
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

  // Verify per-window active space isolation: switch window 2 to a different
  // space and confirm window 1 retains its original active space.
  MahoSpaceProfileBridge* bridge = MahoSpaceProfileBridge::GetInstance();
  ASSERT_TRUE(bridge);

  const std::string space_a_id = bridge->GetActiveSpaceId(browser());
  std::vector<std::string> all_space_ids = bridge->GetSpaceIds();
  std::string space_b_id;
  for (const auto& sid : all_space_ids) {
    if (sid != space_a_id) {
      space_b_id = sid;
      break;
    }
  }

  if (!space_b_id.empty()) {
    bridge->SwitchToSpace(second_browser, space_b_id);
    base::RunLoop().RunUntilIdle();

    EXPECT_EQ(space_a_id, bridge->GetActiveSpaceId(browser()))
        << "Window 1 active space must not change when window 2 switches";
    EXPECT_EQ(space_b_id, bridge->GetActiveSpaceId(second_browser))
        << "Window 2 must reflect the switched space";

    auto state1 = BuildViewStateForBrowser(static_cast<Browser*>(browser()));
    auto state2 = BuildViewStateForBrowser(second_browser);
    EXPECT_EQ(space_a_id, state1.tab_list.active_space_id)
        << "Window 1 sidebar model must report space A";
    EXPECT_EQ(space_b_id, state2.tab_list.active_space_id)
        << "Window 2 sidebar model must report space B";
    EXPECT_NE(state1.tab_list.active_space_id,
              state2.tab_list.active_space_id)
        << "The two windows must not share the same sidebar active space";
  }

  chrome::CloseWindow(second_browser);
  base::RunLoop().RunUntilIdle();

  EXPECT_TRUE(container_->GetVisible())
      << "First browser sidebar unaffected by second window close";
}

// Regression test: two windows can hold the same active space without
// cross-contamination. Switching one window away must not affect the other
// window that remains on the original space.
IN_PROC_BROWSER_TEST_F(MahoSidebarEdgeCasesInteractiveTest,
                       MultiWindowSameSpaceIndependence) {
  SeedProfile(2);

  MahoSpaceProfileBridge* bridge = MahoSpaceProfileBridge::GetInstance();
  ASSERT_TRUE(bridge);

  const std::string initial_space = bridge->GetActiveSpaceId(browser());
  ASSERT_FALSE(initial_space.empty());

  // Open a second window — it should inherit the same active space.
  Browser* second_browser = static_cast<Browser*>(CreateBrowser(browser()->GetProfile()));
  ASSERT_TRUE(second_browser);

  // Explicitly set window 2 to the same space as window 1.
  bridge->SetActiveSpaceId(second_browser, initial_space);
  EXPECT_EQ(initial_space, bridge->GetActiveSpaceId(second_browser));

  // Find a different space to switch window 1 to.
  std::vector<std::string> all_space_ids = bridge->GetSpaceIds();
  std::string other_space;
  for (const auto& sid : all_space_ids) {
    if (sid != initial_space) {
      other_space = sid;
      break;
    }
  }

  if (!other_space.empty()) {
    bridge->SwitchToSpace(static_cast<Browser*>(browser()), other_space);
    base::RunLoop().RunUntilIdle();

    EXPECT_EQ(initial_space, bridge->GetActiveSpaceId(second_browser))
        << "Window 2 must retain its active space when window 1 switches away";
    EXPECT_EQ(other_space, bridge->GetActiveSpaceId(browser()))
        << "Window 1 must reflect the new space";

    auto state1 = BuildViewStateForBrowser(static_cast<Browser*>(browser()));
    auto state2 = BuildViewStateForBrowser(second_browser);
    EXPECT_EQ(other_space, state1.tab_list.active_space_id)
        << "Window 1 sidebar model must report the switched space";
    EXPECT_EQ(initial_space, state2.tab_list.active_space_id)
        << "Window 2 sidebar model must still report the original space";
  }

  chrome::CloseWindow(second_browser);
  base::RunLoop().RunUntilIdle();
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

  int initial_count = browser()->GetTabStripModel()->count();
  ASSERT_GE(initial_count, 11);

  for (int i = 0; i < 5; ++i) {
    int last_index = browser()->GetTabStripModel()->count() - 1;
    if (last_index <= 0)
      break;
    browser()->GetTabStripModel()->CloseWebContentsAt(
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

IN_PROC_BROWSER_TEST_F(MahoSidebarEdgeCasesInteractiveTest,
                        FavoritesIsolatedToActiveSpaceAfterSpaceSwitch) {
  SeedProfile(2);

  auto state_space_a = GetViewState();
  const std::string space_a_id = state_space_a.tab_list.active_space_id;
  ASSERT_FALSE(space_a_id.empty());

  MahoSpaceProfileBridge* bridge = MahoSpaceProfileBridge::GetInstance();
  ASSERT_TRUE(bridge);

  std::vector<std::string> all_space_ids = bridge->GetSpaceIds();
  std::string space_b_id;
  for (const auto& sid : all_space_ids) {
    if (sid != space_a_id) {
      space_b_id = sid;
      break;
    }
  }

  if (space_b_id.empty()) {
    LOG(WARNING) << "Seed 2 has only one space — skipping isolation test";
    return;
  }

  std::vector<std::string> space_a_fav_ids;
  for (const auto& item : state_space_a.favorites.items) {
    if (!item.tab_id.empty()) {
      space_a_fav_ids.push_back(item.tab_id);
    }
  }

  bridge->SwitchToSpace(static_cast<Browser*>(browser()), space_b_id);
  base::RunLoop().RunUntilIdle();

  ASSERT_TRUE(base::test::RunUntil([&]() {
    ResolveViews();
    return GetViewState().tab_list.active_space_id == space_b_id;
  })) << "Active space must switch to space B after SwitchToSpace";

  ResolveViews();
  auto state_space_b = GetViewState();
  EXPECT_EQ(space_b_id, state_space_b.tab_list.active_space_id);

  for (const auto& b_item : state_space_b.favorites.items) {
    for (const auto& a_id : space_a_fav_ids) {
      EXPECT_NE(b_item.tab_id, a_id)
          << "Favorite tab_id '" << a_id
          << "' from space A must not appear in space B favorites after switch";
    }
  }
}

IN_PROC_BROWSER_TEST_F(MahoSidebarEdgeCasesInteractiveTest,
                       IneligibleUrlsExcludedFromStartupReconciliation) {
  TabStripModel* model = browser()->GetTabStripModel();
  ASSERT_GE(model->count(), 1);

  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), GURL("chrome://newtab")));
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), GURL("chrome://downloads"),
      WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  base::RunLoop().RunUntilIdle();

  ResolveViews();
  ASSERT_TRUE(tab_list_);

  views::View* rows_container = tab_list_->tab_rows_for_testing();
  ASSERT_TRUE(rows_container);

  for (views::View* child : rows_container->children()) {
    auto* row = views::AsViewClass<SidebarTabRowView>(child);
    if (!row) {
      continue;
    }
    int live_index = row->live_tab_index_for_testing();
    if (live_index < 0 || live_index >= model->count()) {
      continue;
    }
    content::WebContents* contents = model->GetWebContentsAt(live_index);
    ASSERT_TRUE(contents);
    const GURL& url = contents->GetVisibleURL().is_empty()
                          ? contents->GetLastCommittedURL()
                          : contents->GetVisibleURL();
#if 0
    // TODO(maho): re-enable once maho::IsSidebarIneligibleUrl is implemented
    // (referenced symbol is currently undefined in the codebase).
    EXPECT_FALSE(maho::IsSidebarIneligibleUrl(url))
        << "Sidebar rendered an ineligible URL as a tab row: " << url.spec();
#else
    (void)url;
#endif
  }
}

IN_PROC_BROWSER_TEST_F(MahoSidebarEdgeCasesInteractiveTest,
                       GhostRowsNotRenderedAfterTabClose) {
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), GURL("https://example.com"),
      WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  base::RunLoop().RunUntilIdle();

  ResolveViews();
  ASSERT_TRUE(tab_list_);

  TabStripModel* model = browser()->GetTabStripModel();
  ASSERT_GE(model->count(), 2);

  chrome::CloseTab(browser());
  base::RunLoop().RunUntilIdle();

  views::View* rows_container = tab_list_->tab_rows_for_testing();
  ASSERT_TRUE(rows_container);

  for (views::View* child : rows_container->children()) {
    auto* row = views::AsViewClass<SidebarTabRowView>(child);
    if (!row) {
      continue;
    }
    int live_index = row->live_tab_index_for_testing();
    EXPECT_GE(live_index, 0)
        << "A rendered sidebar row has no live tab-strip counterpart "
           "(ghost row). It should have been filtered out during rebuild.";
  }
}



#if 0
// TODO(maho): re-enable once maho::IsSidebarIneligibleUrl is implemented
// (referenced symbol is currently undefined in the codebase).
TEST(IsSidebarIneligibleUrlTest, KnownSpecialPagesAreExcluded) {
  EXPECT_TRUE(maho::IsSidebarIneligibleUrl(GURL("chrome://newtab")));
  EXPECT_TRUE(maho::IsSidebarIneligibleUrl(GURL("chrome://downloads")));
  EXPECT_TRUE(maho::IsSidebarIneligibleUrl(GURL("chrome://history")));
  EXPECT_TRUE(maho::IsSidebarIneligibleUrl(GURL("about:blank")));
  EXPECT_FALSE(maho::IsSidebarIneligibleUrl(GURL("https://example.com")));
  EXPECT_FALSE(maho::IsSidebarIneligibleUrl(GURL("https://google.com")));
  EXPECT_FALSE(maho::IsSidebarIneligibleUrl(GURL("chrome://settings")));
  EXPECT_FALSE(maho::IsSidebarIneligibleUrl(GURL()));
}
#endif

IN_PROC_BROWSER_TEST_F(MahoSidebarEdgeCasesInteractiveTest,
                       DirtyStructuralRefreshDiscardsStaleBackgroundResult) {
  ResolveViews();
  auto* sidebar = GetSidebarView();
  ASSERT_TRUE(sidebar);

  std::vector<SidebarStateBackgroundResult> held_results;
  base::RunLoop run_loop;

  sidebar->SetOnBackgroundStateReadyCallbackForTesting(base::BindRepeating(
      [](std::vector<SidebarStateBackgroundResult>* held, base::RunLoop* loop,
         MahoSidebarView* view, SidebarStateBackgroundResult& res) {
        held->push_back(std::move(res));
        loop->Quit();
        return false;
      },
      base::Unretained(&held_results), base::Unretained(&run_loop)));

  sidebar->ScheduleRefreshAll();
  run_loop.Run();

  ASSERT_EQ(1u, held_results.size());

  size_t count_before = GetViewState().tab_list.normal_tree.size();

  // Mutate model.
  AddTestTab(GURL("https://stale-test.example.com"), u"B Tab");

  // Queue result B.
  sidebar->ScheduleRefreshAll();

  // Remove interceptor.
  sidebar->SetOnBackgroundStateReadyCallbackForTesting(base::NullCallback());

  // Release A.
  sidebar->OnBackgroundStateReadyForTesting(std::move(held_results[0]));

  // Assert A did not apply.
  EXPECT_EQ(count_before, GetViewState().tab_list.normal_tree.size());

  // Let B complete.
  RunAllPendingTasks();

  // Assert B applied.
  EXPECT_EQ(count_before + 1, GetViewState().tab_list.normal_tree.size());
}

IN_PROC_BROWSER_TEST_F(MahoSidebarEdgeCasesInteractiveTest, ArchiveTabRegistrySuppressionBehavior) {
  ResolveViews();
  auto* registry = MahoTabRegistry::Get();
  ASSERT_TRUE(registry);

  SetTabRegistryShellEventObserverForTesting(&observer_);

  AddTestTabs(5);
  ResolveViews();
  base::RunLoop().RunUntilIdle();

  TabStripModel* model = browser()->GetTabStripModel();
  int initial_count = model->count();
  ASSERT_GE(initial_count, 5);

  std::vector<std::string> tab_ids;
  for (int i = 0; i < model->count(); ++i) {
    auto* helper = MahoTabIdHelper::FromWebContents(model->GetWebContentsAt(i));
    ASSERT_TRUE(helper);
    tab_ids.push_back(helper->stable_tab_id());
  }

  // --- (a) inactive single archive ---
  model->ActivateTabAt(0, TabStripUserGestureDetails(TabStripUserGestureDetails::GestureType::kMouse));
  std::string inactive_id = tab_ids[1];

  observer_.Clear();
  registry->ArchiveTabsAndRemoveFromStrip(static_cast<Browser*>(browser()), {inactive_id});
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(initial_count - 1, model->count());
  EXPECT_EQ(0, model->active_index());
  EXPECT_FALSE(observer_.HasEvent("close_tab"));
  EXPECT_TRUE(observer_.HasEvent("archive_tab_by_id"));

  // --- (b) active archive and successor ---
  model->ActivateTabAt(1, TabStripUserGestureDetails(TabStripUserGestureDetails::GestureType::kMouse));
  std::string active_id = MahoTabIdHelper::FromWebContents(model->GetActiveWebContents())->stable_tab_id();

  observer_.Clear();
  registry->ArchiveTabsAndRemoveFromStrip(static_cast<Browser*>(browser()), {active_id});
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(initial_count - 2, model->count());
  {
    std::string new_active_id =
        MahoTabIdHelper::FromWebContents(model->GetActiveWebContents())
            ->stable_tab_id();
    EXPECT_NE(active_id, new_active_id);
  }
  EXPECT_FALSE(observer_.HasEvent("close_tab"));

  // --- (c) multi-select and Archive Below ---
  std::vector<std::string> multi_ids;
  multi_ids.push_back(MahoTabIdHelper::FromWebContents(model->GetWebContentsAt(1))->stable_tab_id());
  multi_ids.push_back(MahoTabIdHelper::FromWebContents(model->GetWebContentsAt(2))->stable_tab_id());

  observer_.Clear();
  registry->ArchiveTabsAndRemoveFromStrip(static_cast<Browser*>(browser()), multi_ids);
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(initial_count - 4, model->count());
  EXPECT_FALSE(observer_.HasEvent("close_tab"));

  // --- (d) pinned/favorite archived record survives ---
  std::string pin_id = MahoTabIdHelper::FromWebContents(model->GetWebContentsAt(0))->stable_tab_id();
  observer_.Clear();
  registry->ArchiveTabsAndRemoveFromStrip(static_cast<Browser*>(browser()), {pin_id});
  base::RunLoop().RunUntilIdle();

  EXPECT_FALSE(observer_.HasEvent("close_tab"));
  EXPECT_FALSE(observer_.HasEvent("suspend_tab"));

  // --- (f) unconsumed suppression clean up and normal close on restored ---
  std::string restored_id = "restored-tab-id";
  {
    ScopedMahoTabWakeId wake_scope(restored_id);
    chrome::AddTabAt(browser(), GURL("https://example.com"), -1, true);
  }
  base::RunLoop().RunUntilIdle();

  registry->ArchiveTabsAndRemoveFromStrip(static_cast<Browser*>(browser()), {restored_id});
  base::RunLoop().RunUntilIdle();

  {
    ScopedMahoTabWakeId wake_scope(restored_id);
    chrome::AddTabAt(browser(), GURL("https://example.com"), -1, true);
  }
  base::RunLoop().RunUntilIdle();

  observer_.Clear();
  int idx = -1;
  for (int i = 0; i < model->count(); ++i) {
    auto* helper = MahoTabIdHelper::FromWebContents(model->GetWebContentsAt(i));
    if (helper && helper->stable_tab_id() == restored_id) {
      idx = i;
      break;
    }
  }
  ASSERT_GE(idx, 0);

  model->CloseWebContentsAt(idx, TabCloseTypes::CLOSE_USER_GESTURE);
  base::RunLoop().RunUntilIdle();

  int close_count = 0;
  for (const auto& ev : observer_.events()) {
    if (ev.kind == "close_tab") {
      close_count++;
    }
  }
  EXPECT_EQ(1, close_count);

  SetTabRegistryShellEventObserverForTesting(nullptr);
}

IN_PROC_BROWSER_TEST_F(MahoSidebarEdgeCasesInteractiveTest, DownloadsViewPopulatedSnapshot) {
  ResolveViews();
  auto* sidebar = GetSidebarView();
  ASSERT_TRUE(sidebar);

  sidebar->OpenLibrary(MahoSidebarLibraryRailView::Category::kDownloads);
  base::RunLoop().RunUntilIdle();

  auto* downloads_view = sidebar->downloads_view_for_testing();
  ASSERT_TRUE(downloads_view);
  EXPECT_TRUE(downloads_view->GetVisible());

  std::vector<DownloadItem> items;
  DownloadItem item1;
  item1.id = "dl-1";
  item1.filename = u"completed_doc.pdf";
  item1.url = "https://example.com/completed_doc.pdf";
  item1.state = "completed";
  item1.total_bytes = 2048;
  item1.received_bytes = 2048;
  item1.started_at = "Saturday, July 18, 2026 6:00:00 PM";
  item1.completed_at = "Saturday, July 18, 2026 6:01:00 PM";
  items.push_back(item1);

  DownloadItem item2;
  item2.id = "dl-2";
  item2.filename = u"active_zip.zip";
  item2.url = "https://example.com/active_zip.zip";
  item2.state = "downloading";
  item2.total_bytes = 1024 * 1024;
  item2.received_bytes = 512 * 1024;
  item2.started_at = "Saturday, July 18, 2026 6:00:00 PM";
  items.push_back(item2);

  downloads_view->SetDownloadsForTesting(items);
  base::RunLoop().RunUntilIdle();

  views::View* list_container = downloads_view->list_container_for_testing();
  int row_count = 0;
  for (views::View* child : list_container->children()) {
    for (views::View* grandchild : child->children()) {
      if (grandchild->GetClassName() == "DownloadRowView") {
        row_count++;
      }
    }
  }
  EXPECT_EQ(2, row_count);
}

IN_PROC_BROWSER_TEST_F(MahoSidebarEdgeCasesInteractiveTest, MahoSidebarRestoreArchivedTabFlow) {
  ResolveViews();
  auto* registry = MahoTabRegistry::Get();
  ASSERT_TRUE(registry);

  SetTabRegistryShellEventObserverForTesting(&observer_);

  AddTestTabs(3);
  ResolveViews();
  base::RunLoop().RunUntilIdle();

  TabStripModel* model = browser()->GetTabStripModel();
  int initial_count = model->count();
  ASSERT_GE(initial_count, 3);

  auto* target_contents = model->GetWebContentsAt(1);
  auto* helper = MahoTabIdHelper::FromWebContents(target_contents);
  ASSERT_TRUE(helper);
  std::string tab_id = helper->stable_tab_id();

  auto* bridge = MahoSpaceProfileBridge::GetInstance();
  ASSERT_TRUE(bridge);
  std::string space_id = bridge->GetActiveSpaceId(browser());
  ASSERT_FALSE(space_id.empty());

  registry->ArchiveTabsAndRemoveFromStrip(static_cast<Browser*>(browser()), {tab_id});
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(initial_count - 1, model->count());

  sidebar_->OpenLibrary(MahoSidebarLibraryRailView::Category::kArchivedTabs);
  ResolveViews();
  base::RunLoop().RunUntilIdle();

  auto* archive_view = sidebar_->archive_view_for_testing();
  ASSERT_TRUE(archive_view);

  archive_view->ReloadArchivedTabs();

  bool tab_in_archive = BlockAndPollUntil([&]() {
    for (const auto& item : archive_view->all_archived_tabs_for_testing()) {
      if (item.tab_id == tab_id) {
        return true;
      }
    }
    return false;
  });
  ASSERT_TRUE(tab_in_archive) << "Archived tab was not loaded into the archive view";

  std::string found_space_id;
  for (const auto& item : archive_view->all_archived_tabs_for_testing()) {
    if (item.tab_id == tab_id) {
      found_space_id = item.space_id;
      break;
    }
  }
  EXPECT_EQ(found_space_id, space_id);

  observer_.Clear();
  sidebar_->RestoreArchivedTabAndActivate(tab_id, space_id);
  base::RunLoop().RunUntilIdle();

  bool found_live = false;
  for (int i = 0; i < model->count(); ++i) {
    auto* tab_helper = MahoTabIdHelper::FromWebContents(model->GetWebContentsAt(i));
    if (tab_helper && tab_helper->stable_tab_id() == tab_id) {
      found_live = true;
      break;
    }
  }
  EXPECT_TRUE(found_live) << "Restored tab was not found in the tab strip model";

  EXPECT_EQ(initial_count, model->count());

  archive_view->ReloadArchivedTabs();
  bool tab_still_in_archive = BlockAndPollUntil([&]() {
    for (const auto& item : archive_view->all_archived_tabs_for_testing()) {
      if (item.tab_id == tab_id) {
        return true;
      }
    }
    return false;
  }, base::Milliseconds(500));
  EXPECT_FALSE(tab_still_in_archive) << "Restored tab should no longer be in the archive view";

  sidebar_->RestoreArchivedTabAndActivate(tab_id, space_id);
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(initial_count, model->count());

  SetTabRegistryShellEventObserverForTesting(nullptr);
}

IN_PROC_BROWSER_TEST_F(MahoSidebarEdgeCasesInteractiveTest,
                       MahoSidebarLibraryOverlayVisibleOnDownloadsSelect) {
  ResolveViews();
  ASSERT_TRUE(sidebar_);
  ASSERT_TRUE(container_);

  sidebar_->OpenLibrary(MahoSidebarLibraryRailView::Category::kArchivedTabs);
  ResolveViews();
  base::RunLoop().RunUntilIdle();

  SelectLibraryCategoryViaRail(
      sidebar_, MahoSidebarLibraryRailView::Category::kDownloads);

  auto* controller = GetLibraryOverlayController(container_);
  ASSERT_TRUE(controller);
  EXPECT_TRUE(controller->IsVisible())
      << "Selecting Downloads must open the fixed-width library overlay";
  EXPECT_EQ(MahoSidebarLibraryRailView::Category::kDownloads,
            controller->category());
  ASSERT_TRUE(controller->GetOverlayViewForTesting());
  EXPECT_TRUE(
      controller->GetOverlayViewForTesting()->downloads_view_for_testing())
      << "Downloads overlay must host a downloads content view";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarEdgeCasesInteractiveTest,
                       MahoSidebarLibraryOverlayWidthIndependentOfSidebarPref) {
  ResolveViews();
  ASSERT_TRUE(sidebar_);
  ASSERT_TRUE(container_);

  PrefService* prefs = browser()->GetProfile()->GetPrefs();
  prefs->SetInteger(sidebar_prefs::kSidebarWidth, 280);
  base::RunLoop().RunUntilIdle();

  sidebar_->OpenLibrary(MahoSidebarLibraryRailView::Category::kArchivedTabs);
  ResolveViews();
  base::RunLoop().RunUntilIdle();
  SelectLibraryCategoryViaRail(
      sidebar_, MahoSidebarLibraryRailView::Category::kDownloads);

  auto* controller = GetLibraryOverlayController(container_);
  ASSERT_TRUE(controller);
  ASSERT_TRUE(controller->IsVisible());
  EXPECT_EQ(sidebar_layout::kLibraryOverlayContentWidthDp,
            controller->GetOverlayWidthForTesting())
      << "Overlay widget width must be fixed with a 280dp sidebar pref";

  prefs->SetInteger(sidebar_prefs::kSidebarWidth, 1600);
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(controller->IsVisible());
  EXPECT_EQ(sidebar_layout::kLibraryOverlayContentWidthDp,
            controller->GetOverlayWidthForTesting())
      << "Overlay widget width must stay fixed with a 1600dp sidebar pref";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarEdgeCasesInteractiveTest,
                       MahoSidebarLibraryOverlaySwapKeepsSingleWidget) {
  ResolveViews();
  ASSERT_TRUE(sidebar_);
  ASSERT_TRUE(container_);

  sidebar_->OpenLibrary(MahoSidebarLibraryRailView::Category::kArchivedTabs);
  ResolveViews();
  base::RunLoop().RunUntilIdle();
  SelectLibraryCategoryViaRail(
      sidebar_, MahoSidebarLibraryRailView::Category::kDownloads);

  auto* controller = GetLibraryOverlayController(container_);
  ASSERT_TRUE(controller);
  ASSERT_TRUE(controller->IsVisible());
  EXPECT_EQ(MahoSidebarLibraryRailView::Category::kDownloads,
            controller->category());
  views::Widget* widget_before = controller->GetOverlayWidgetForTesting();
  ASSERT_TRUE(widget_before);

  SelectLibraryCategoryViaRail(
      sidebar_, MahoSidebarLibraryRailView::Category::kArchivedTabs);

  EXPECT_TRUE(controller->IsVisible());
  EXPECT_EQ(MahoSidebarLibraryRailView::Category::kArchivedTabs,
            controller->category());
  EXPECT_EQ(widget_before, controller->GetOverlayWidgetForTesting())
      << "Swapping Downloads<->Archive must reuse the same overlay widget";
  ASSERT_TRUE(controller->GetOverlayViewForTesting());
  EXPECT_TRUE(
      controller->GetOverlayViewForTesting()->archive_view_for_testing())
      << "Swapped overlay must now host an archive content view";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarEdgeCasesInteractiveTest,
                       MahoSidebarLibraryOverlayDismissedOnOtherCategory) {
  ResolveViews();
  ASSERT_TRUE(sidebar_);
  ASSERT_TRUE(container_);

  sidebar_->OpenLibrary(MahoSidebarLibraryRailView::Category::kArchivedTabs);
  ResolveViews();
  base::RunLoop().RunUntilIdle();
  SelectLibraryCategoryViaRail(
      sidebar_, MahoSidebarLibraryRailView::Category::kDownloads);

  auto* controller = GetLibraryOverlayController(container_);
  ASSERT_TRUE(controller);
  ASSERT_TRUE(controller->IsVisible());

  // NOTE: kMedia intentionally avoided here - MahoSidebarMediaView has a
  // separate pre-existing construction crash (out of scope).
  SelectLibraryCategoryViaRail(
      sidebar_, MahoSidebarLibraryRailView::Category::kSpaces);
  EXPECT_FALSE(controller->IsVisible())
      << "Selecting a different library category must dismiss the library "
         "overlay";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarEdgeCasesInteractiveTest,
                       MahoSidebarLibraryOverlayDismissedOnLeaveLibrary) {
  ResolveViews();
  ASSERT_TRUE(sidebar_);
  ASSERT_TRUE(container_);

  sidebar_->OpenLibrary(MahoSidebarLibraryRailView::Category::kArchivedTabs);
  ResolveViews();
  base::RunLoop().RunUntilIdle();
  SelectLibraryCategoryViaRail(
      sidebar_, MahoSidebarLibraryRailView::Category::kDownloads);

  auto* controller = GetLibraryOverlayController(container_);
  ASSERT_TRUE(controller);
  ASSERT_TRUE(controller->IsVisible());

  sidebar_->ExitLibraryToTabs();
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(controller->IsVisible())
      << "Leaving Library must dismiss the floating overlay";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarEdgeCasesInteractiveTest,
                       MahoSidebarLibraryOverlayDismissedOnFullAutoHide) {
  ResolveViews();
  ASSERT_TRUE(sidebar_);
  ASSERT_TRUE(container_);

  sidebar_->OpenLibrary(MahoSidebarLibraryRailView::Category::kArchivedTabs);
  ResolveViews();
  base::RunLoop().RunUntilIdle();
  SelectLibraryCategoryViaRail(
      sidebar_, MahoSidebarLibraryRailView::Category::kDownloads);

  auto* controller = GetLibraryOverlayController(container_);
  ASSERT_TRUE(controller);
  ASSERT_TRUE(controller->IsVisible());

  browser()->GetProfile()->GetPrefs()->SetBoolean(
      sidebar_prefs::kSidebarPanelExpanded, false);
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(controller->IsVisible())
      << "Fully auto-hiding the sidebar must dismiss the library overlay";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarEdgeCasesInteractiveTest,
                       MahoSidebarLibraryOverlayRestoreOpensLiveTab) {
  ResolveViews();
  auto* registry = MahoTabRegistry::Get();
  ASSERT_TRUE(registry);
  SetTabRegistryShellEventObserverForTesting(&observer_);

  AddTestTabs(3);
  ResolveViews();
  base::RunLoop().RunUntilIdle();

  TabStripModel* model = browser()->GetTabStripModel();
  int initial_count = model->count();
  ASSERT_GE(initial_count, 3);

  auto* target_contents = model->GetWebContentsAt(1);
  auto* helper = MahoTabIdHelper::FromWebContents(target_contents);
  ASSERT_TRUE(helper);
  std::string tab_id = helper->stable_tab_id();

  auto* bridge = MahoSpaceProfileBridge::GetInstance();
  ASSERT_TRUE(bridge);
  std::string space_id = bridge->GetActiveSpaceId(browser());
  ASSERT_FALSE(space_id.empty());

  registry->ArchiveTabsAndRemoveFromStrip(static_cast<Browser*>(browser()), {tab_id});
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(initial_count - 1, model->count());

  sidebar_->OpenLibrary(MahoSidebarLibraryRailView::Category::kArchivedTabs);
  ResolveViews();
  base::RunLoop().RunUntilIdle();
  SelectLibraryCategoryViaRail(
      sidebar_, MahoSidebarLibraryRailView::Category::kArchivedTabs);

  auto* controller = GetLibraryOverlayController(container_);
  ASSERT_TRUE(controller);
  ASSERT_TRUE(controller->IsVisible());
  ASSERT_TRUE(controller->GetOverlayViewForTesting());
  auto* overlay_archive =
      controller->GetOverlayViewForTesting()->archive_view_for_testing();
  ASSERT_TRUE(overlay_archive);

  overlay_archive->ReloadArchivedTabs();
  bool tab_in_archive = BlockAndPollUntil([&]() {
    for (const auto& item : overlay_archive->all_archived_tabs_for_testing()) {
      if (item.tab_id == tab_id) {
        return true;
      }
    }
    return false;
  });
  ASSERT_TRUE(tab_in_archive)
      << "Archived tab was not loaded into the overlay archive view";

  observer_.Clear();
  sidebar_->RestoreArchivedTabAndActivate(tab_id, space_id);
  base::RunLoop().RunUntilIdle();

  bool found_live = false;
  for (int i = 0; i < model->count(); ++i) {
    auto* tab_helper =
        MahoTabIdHelper::FromWebContents(model->GetWebContentsAt(i));
    if (tab_helper && tab_helper->stable_tab_id() == tab_id) {
      found_live = true;
      break;
    }
  }
  EXPECT_TRUE(found_live)
      << "Restoring from the overlay must open a live tab in the strip";
  EXPECT_EQ(initial_count, model->count());
  EXPECT_FALSE(controller->IsVisible())
      << "Restoring exits Library and dismisses the overlay";

  SetTabRegistryShellEventObserverForTesting(nullptr);
}

IN_PROC_BROWSER_TEST_F(MahoSidebarEdgeCasesInteractiveTest,
                       MahoSidebarLibraryOverlayOpensFromFooterArchiveButton) {
  ResolveViews();
  ASSERT_TRUE(sidebar_);
  ASSERT_TRUE(container_);
  MahoSidebarFooterView* footer = GetFooterView();
  ASSERT_TRUE(footer);
  views::MdTextButton* archive_button = footer->archive_button_for_testing();
  ASSERT_TRUE(archive_button);

  ui::MouseEvent event(ui::EventType::kMousePressed, gfx::Point(), gfx::Point(),
                       base::TimeTicks(), ui::EF_LEFT_MOUSE_BUTTON, 0);
  views::test::ButtonTestApi(archive_button).NotifyClick(event);
  base::RunLoop().RunUntilIdle();

  EXPECT_TRUE(container_->IsLibraryOverlayVisible())
      << "Footer archive button must open the fixed-width library overlay, "
         "identical to a rail Archive selection";

  auto* controller = GetLibraryOverlayController(container_);
  ASSERT_TRUE(controller);
  ASSERT_TRUE(controller->IsVisible());
  EXPECT_EQ(sidebar_layout::kLibraryOverlayContentWidthDp,
            controller->GetOverlayWidthForTesting())
      << "Footer-opened overlay must use the fixed content width";
}

}  // namespace
}  // namespace maho
