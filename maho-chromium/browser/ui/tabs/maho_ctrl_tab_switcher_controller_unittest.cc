// Copyright 2026 Maho Browser. All rights reserved.

#include <memory>

#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/testing_pref_service.h"
#include "maho/browser/ui/tabs/maho_ctrl_tab_prefs.h"
#include "maho/browser/ui/tabs/maho_ctrl_tab_slot_model.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho {

// Pref and slot-model tests here.  The full controller behavioral tests
// (fast-flip, blank/kNoTab early-return, space-scope fallthrough, cursor
// wraparound including the "+N more" affordance slot, Ctrl-release commit,
// Escape cancel, snapshot invalidation on tab close) require a live BrowserView
// with a real Widget, TabStripModel and MahoCore; they belong under
// browser_tests / interactive_ui_tests rather than pref-only unit_tests.
// See BrowserView-based coverage in
// maho/browser/ui/views/tab_switcher/... (Phase 6 follow-up).

TEST(MahoCtrlTabPrefsTest, RegistersProfilePrefsWithDefaults) {
  TestingPrefServiceSimple prefs;
  ctrl_tab_prefs::RegisterProfilePrefs(prefs.registry());

  EXPECT_TRUE(prefs.GetBoolean(ctrl_tab_prefs::kMruOrder));
  EXPECT_EQ(std::string(ctrl_tab_prefs::kScopeCurrentSpace),
            prefs.GetString(ctrl_tab_prefs::kScope));
  EXPECT_EQ(7, prefs.GetInteger(ctrl_tab_prefs::kMaxVisible));
}

TEST(MahoCtrlTabPrefsTest, PrefsAreMutable) {
  TestingPrefServiceSimple prefs;
  ctrl_tab_prefs::RegisterProfilePrefs(prefs.registry());

  prefs.SetBoolean(ctrl_tab_prefs::kMruOrder, false);
  prefs.SetString(ctrl_tab_prefs::kScope, ctrl_tab_prefs::kScopeGlobal);
  prefs.SetInteger(ctrl_tab_prefs::kMaxVisible, 5);

  EXPECT_FALSE(prefs.GetBoolean(ctrl_tab_prefs::kMruOrder));
  EXPECT_EQ(std::string(ctrl_tab_prefs::kScopeGlobal),
            prefs.GetString(ctrl_tab_prefs::kScope));
  EXPECT_EQ(5, prefs.GetInteger(ctrl_tab_prefs::kMaxVisible));
}

// R2-2: backward is the MRU wrap-to-end direction, so a quick Ctrl+Shift+Tab
// commits the least-recently-used tab even when more tabs are open than the
// overlay can show cards for.
TEST(MahoCtrlTabSlotModelTest, BackwardStartsOnLeastRecentlyUsedTab) {
  EXPECT_EQ(19u, ctrl_tab_slots::InitialCursor(/*forward=*/false,
                                               /*snapshot_size=*/20));
  EXPECT_EQ(1u, ctrl_tab_slots::InitialCursor(/*forward=*/true,
                                              /*snapshot_size=*/20));
  EXPECT_EQ(0u, ctrl_tab_slots::InitialCursor(/*forward=*/false,
                                              /*snapshot_size=*/0));
}

// R2-3: a refit must never park the cursor on the "+N more" slot, because the
// next Ctrl release would open the command overlay instead of a tab.
TEST(MahoCtrlTabSlotModelTest, RefitClampsToLastVisibleCardNotTheMoreSlot) {
  const ctrl_tab_slots::SlotModel shrunk{/*visible_cards=*/2,
                                         /*has_more=*/true};
  EXPECT_EQ(1u, ctrl_tab_slots::ResolveCursorAfterRefit(
                    /*cursor=*/5, /*snapshot_size=*/20, shrunk,
                    /*was_on_more_slot=*/false, /*was_wrapped=*/false));
}

TEST(MahoCtrlTabSlotModelTest, RefitPreservesAnOnMoreSlotCursor) {
  const ctrl_tab_slots::SlotModel shrunk{/*visible_cards=*/2,
                                         /*has_more=*/true};
  EXPECT_EQ(2u, ctrl_tab_slots::ResolveCursorAfterRefit(
                    /*cursor=*/2, /*snapshot_size=*/20, shrunk,
                    /*was_on_more_slot=*/true, /*was_wrapped=*/false));
  // The more slot disappeared, so the same index is now a real card.
  const ctrl_tab_slots::SlotModel all_visible{/*visible_cards=*/20,
                                              /*has_more=*/false};
  EXPECT_EQ(2u, ctrl_tab_slots::ResolveCursorAfterRefit(
                     /*cursor=*/2, /*snapshot_size=*/20, all_visible,
                     /*was_on_more_slot=*/true, /*was_wrapped=*/false));
}

TEST(MahoCtrlTabSlotModelTest, RefitKeepsAPendingWrappedCommitTarget) {
  const ctrl_tab_slots::SlotModel shrunk{/*visible_cards=*/2,
                                         /*has_more=*/true};
  EXPECT_EQ(19u, ctrl_tab_slots::ResolveCursorAfterRefit(
                     /*cursor=*/19, /*snapshot_size=*/20, shrunk,
                     /*was_on_more_slot=*/false, /*was_wrapped=*/true));
  // Tabs closed meanwhile: the wrapped target follows the shrunken snapshot.
  EXPECT_EQ(11u, ctrl_tab_slots::ResolveCursorAfterRefit(
                     /*cursor=*/19, /*snapshot_size=*/12, shrunk,
                     /*was_on_more_slot=*/false, /*was_wrapped=*/true));
}

TEST(MahoCtrlTabSlotModelTest, RefitOntoTheMoreSlotKeepsHighlightAndCommitTogether) {
  // One visible card plus the more slot: index 1 IS the more slot. A pending
  // forward cursor of 1 must not be left painting that slot, because the Ctrl
  // release commits a tab - the highlight would advertise the command palette.
  const ctrl_tab_slots::SlotModel one_card{/*visible_cards=*/1,
                                           /*has_more=*/true};
  EXPECT_EQ(0u, ctrl_tab_slots::ResolveCursorAfterRefit(
                    /*cursor=*/1, /*snapshot_size=*/5, one_card,
                    /*was_on_more_slot=*/false, /*was_wrapped=*/true));
}

TEST(MahoCtrlTabSlotModelTest, RefitWithEmptySnapshotYieldsZero) {
  const ctrl_tab_slots::SlotModel empty;
  EXPECT_EQ(0u, ctrl_tab_slots::ResolveCursorAfterRefit(
                    /*cursor=*/7, /*snapshot_size=*/0, empty,
                    /*was_on_more_slot=*/false, /*was_wrapped=*/false));
}

// Exactly one tab more than the visible cards: the wrap-to-end index equals the
// "+N more" index, so the pending commit must be an explicit state, not an
// index comparison.
TEST(MahoCtrlTabSlotModelTest, WrappedIndexCollidingWithMoreSlotCommitsATab) {
  const ctrl_tab_slots::SlotModel eight_cards{/*visible_cards=*/7,
                                              /*has_more=*/true};
  EXPECT_EQ(7u, ctrl_tab_slots::ResolveCursorAfterRefit(
                    /*cursor=*/7, /*snapshot_size=*/8, eight_cards,
                    /*was_on_more_slot=*/false, /*was_wrapped=*/true));
  // The same index as a real "+N more" selection still opens the overlay.
  EXPECT_EQ(7u, ctrl_tab_slots::ResolveCursorAfterRefit(
                    /*cursor=*/7, /*snapshot_size=*/8, eight_cards,
                    /*was_on_more_slot=*/true, /*was_wrapped=*/false));
  EXPECT_TRUE(eight_cards.is_more_slot(7));
}

// R3-1: with a single visible card the forward initial cursor IS the "+N
// more" slot index; a quick Ctrl release must still commit MRU[1] as a tab,
// so the commit goes through the pending MRU index, not the slot ring.
TEST(MahoCtrlTabSlotModelTest, ForwardPendingCommitAtOneCardTargetsATab) {
  const ctrl_tab_slots::SlotModel one_card{/*visible_cards=*/1,
                                           /*has_more=*/true};
  const size_t initial =
      ctrl_tab_slots::InitialCursor(/*forward=*/true, /*snapshot_size=*/8);
  ASSERT_EQ(1u, initial);
  // The collision that used to open the command palette on a quick release.
  ASSERT_TRUE(one_card.is_more_slot(initial));
  // The pending commit bypasses the slot ring and names the real tab.
  EXPECT_EQ(1u, ctrl_tab_slots::PendingCommitIndex(initial,
                                                   /*snapshot_size=*/8));
  // If the overlay opens instead, the cursor cannot stay on the more slot:
  // it enters the ring at the last visible card so the highlight and the
  // commit agree.
  EXPECT_TRUE(ctrl_tab_slots::NeedsRingNormalization(initial, one_card));
  // With two or more cards the same cursor is a real card: no normalization.
  const ctrl_tab_slots::SlotModel two_cards{/*visible_cards=*/2,
                                            /*has_more=*/true};
  EXPECT_FALSE(ctrl_tab_slots::NeedsRingNormalization(initial, two_cards));
}

// A pending commit follows the snapshot when tabs close mid-session.
TEST(MahoCtrlTabSlotModelTest, PendingCommitIndexFollowsTheShrunkenSnapshot) {
  EXPECT_EQ(19u, ctrl_tab_slots::PendingCommitIndex(19, /*snapshot_size=*/20));
  EXPECT_EQ(11u, ctrl_tab_slots::PendingCommitIndex(19, /*snapshot_size=*/12));
  EXPECT_EQ(0u, ctrl_tab_slots::PendingCommitIndex(19, /*snapshot_size=*/0));
}

// The backward wrap-to-end cursor needs normalization only while it sits past
// the slot ring; when every tab fits a card it already names the last card.
TEST(MahoCtrlTabSlotModelTest, BackwardWrapNeedsNormalizationOnlyOffRing) {
  const ctrl_tab_slots::SlotModel shrunk{/*visible_cards=*/2,
                                         /*has_more=*/true};
  EXPECT_TRUE(ctrl_tab_slots::NeedsRingNormalization(19, shrunk));
  const ctrl_tab_slots::SlotModel all_visible{/*visible_cards=*/20,
                                              /*has_more=*/false};
  EXPECT_FALSE(ctrl_tab_slots::NeedsRingNormalization(19, all_visible));
}

}  // namespace maho
