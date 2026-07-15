// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_drop_planner.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_dnd_events.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_section_policy.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_state_models.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace maho {
namespace {

SidebarDragPayload MakeTab(const std::string& id,
                            SidebarDragOrigin origin,
                            const std::string& folder_id = "") {
  SidebarDragPayload p;
  p.node_kind = SidebarNodeKind::kTab;
  p.node_id = id;
  p.origin = origin;
  p.space_id = "space-1";
  p.source_parent_folder_id = folder_id;
  return p;
}

SidebarDragPayload MakeFolder(const std::string& id,
                               SidebarDragOrigin origin) {
  SidebarDragPayload p;
  p.node_kind = SidebarNodeKind::kFolder;
  p.node_id = id;
  p.origin = origin;
  p.space_id = "space-1";
  return p;
}

class SidebarPlannerTest : public ::testing::Test {};

TEST_F(SidebarPlannerTest, NormalToNormalRootDropIsNoOp) {
  SidebarDragPayload payload =
      MakeTab("tab-1", SidebarDragOrigin::kNormalSection);
  SidebarDropPlan plan = ResolveDropPlan(SectionKind::kNormal, payload);
  EXPECT_TRUE(plan.is_valid);
  EXPECT_TRUE(plan.steps.empty())
      << "normal -> normal root drop must produce no steps";
  EXPECT_EQ(plan.pin_transition.kind,
            SidebarDropPlan::PinTransition::Kind::kNone);
  EXPECT_EQ(plan.favorite_transition.kind,
            SidebarDropPlan::FavoriteTransition::Kind::kNone);
}

TEST_F(SidebarPlannerTest, NormalToPinnedEmitsPin) {
  SidebarDragPayload payload =
      MakeTab("tab-1", SidebarDragOrigin::kNormalSection);
  SidebarDropPlan plan = ResolveDropPlan(SectionKind::kPinned, payload);
  EXPECT_TRUE(plan.is_valid);
  ASSERT_EQ(plan.steps.size(), 1u);
  EXPECT_EQ(plan.steps[0].kind, "pin_tab");
  EXPECT_EQ(plan.steps[0].node_id, "tab-1");
  EXPECT_EQ(plan.pin_transition.kind,
            SidebarDropPlan::PinTransition::Kind::kPin);
}

TEST_F(SidebarPlannerTest, PinnedToNormalEmitsUnpin) {
  SidebarDragPayload payload =
      MakeTab("tab-p", SidebarDragOrigin::kPinnedSection);
  SidebarDropPlan plan = ResolveDropPlan(SectionKind::kNormal, payload);
  EXPECT_TRUE(plan.is_valid);
  ASSERT_EQ(plan.steps.size(), 1u);
  EXPECT_EQ(plan.steps[0].kind, "unpin_tab");
  EXPECT_EQ(plan.steps[0].node_id, "tab-p");
  EXPECT_EQ(plan.pin_transition.kind,
            SidebarDropPlan::PinTransition::Kind::kUnpin);
}

TEST_F(SidebarPlannerTest, PinnedToFavoritesEmitsFavoriteOnly) {
  SidebarDragPayload payload =
      MakeTab("tab-p", SidebarDragOrigin::kPinnedSection);
  SidebarDropPlan plan = ResolveDropPlan(SectionKind::kFavorites, payload);
  EXPECT_TRUE(plan.is_valid);
  ASSERT_EQ(plan.steps.size(), 1u)
      << "pinned -> favorites must produce exactly [favorite_tab]";
  EXPECT_EQ(plan.steps[0].kind, "favorite_tab");
  EXPECT_EQ(plan.pin_transition.kind,
            SidebarDropPlan::PinTransition::Kind::kUnpin);
  EXPECT_EQ(plan.favorite_transition.kind,
            SidebarDropPlan::FavoriteTransition::Kind::kFavorite);
}

TEST_F(SidebarPlannerTest, FavoritesToNormalEmitsUnfavoriteOnly) {
  SidebarDragPayload payload =
      MakeTab("fav-1", SidebarDragOrigin::kFavorites);
  SidebarDropPlan plan = ResolveDropPlan(SectionKind::kNormal, payload);
  EXPECT_TRUE(plan.is_valid);
  ASSERT_EQ(plan.steps.size(), 1u)
      << "favorites -> normal must produce exactly [change_tab_role_normal]";
  EXPECT_EQ(plan.steps[0].kind, "change_tab_role_normal");
  EXPECT_EQ(plan.favorite_transition.kind,
            SidebarDropPlan::FavoriteTransition::Kind::kUnfavorite);
  EXPECT_EQ(plan.pin_transition.kind,
            SidebarDropPlan::PinTransition::Kind::kNone);
}

TEST_F(SidebarPlannerTest, FavoritesToPinnedEmitsUnfavoriteThenPin) {
  SidebarDragPayload payload =
      MakeTab("fav-2", SidebarDragOrigin::kFavorites);
  SidebarDropPlan plan = ResolveDropPlan(SectionKind::kPinned, payload);
  EXPECT_TRUE(plan.is_valid);
  ASSERT_EQ(plan.steps.size(), 1u)
      << "favorites -> pinned must produce [change_tab_role_pinned]";
  EXPECT_EQ(plan.steps[0].kind, "change_tab_role_pinned");
  EXPECT_EQ(plan.favorite_transition.kind,
            SidebarDropPlan::FavoriteTransition::Kind::kUnfavorite);
  EXPECT_EQ(plan.pin_transition.kind,
            SidebarDropPlan::PinTransition::Kind::kPin);
}

TEST_F(SidebarPlannerTest, FolderChildToNormalEmitsMoveToRootOnly) {
  SidebarDragPayload payload = MakeTab("child-tab", SidebarDragOrigin::kNormalSection, "folder-1");
  SidebarDropPlan plan = ResolveDropPlan(SectionKind::kNormal, payload);
  EXPECT_TRUE(plan.is_valid);
  ASSERT_EQ(plan.steps.size(), 1u)
      << "folder-child -> normal must produce [move_tab_to_root] only";
  EXPECT_EQ(plan.steps[0].kind, maho::sidebar::kMoveTabToRoot);
  EXPECT_EQ(plan.steps[0].folder_id, "folder-1");
  EXPECT_EQ(plan.pin_transition.kind,
            SidebarDropPlan::PinTransition::Kind::kNone);
}

TEST_F(SidebarPlannerTest, FolderChildToPinnedEmitsMoveToRootThenPin) {
  SidebarDragPayload payload = MakeTab("child-tab", SidebarDragOrigin::kNormalSection, "folder-1");
  SidebarDropPlan plan = ResolveDropPlan(SectionKind::kPinned, payload);
  EXPECT_TRUE(plan.is_valid);
  ASSERT_EQ(plan.steps.size(), 2u)
      << "folder-child -> pinned must produce [move_tab_to_root, pin_tab]";
  EXPECT_EQ(plan.steps[0].kind, maho::sidebar::kMoveTabToRoot);
  EXPECT_EQ(plan.steps[1].kind, "pin_tab");
  EXPECT_EQ(plan.pin_transition.kind,
            SidebarDropPlan::PinTransition::Kind::kPin);
}

TEST_F(SidebarPlannerTest, FolderChildToFavoritesEmitsMoveToRootThenFavorite) {
  SidebarDragPayload payload = MakeTab("child-tab", SidebarDragOrigin::kNormalSection, "folder-1");
  SidebarDropPlan plan = ResolveDropPlan(SectionKind::kFavorites, payload);
  EXPECT_TRUE(plan.is_valid);
  ASSERT_EQ(plan.steps.size(), 2u)
      << "folder-child -> favorites must produce [move_tab_to_root, favorite_tab]";
  EXPECT_EQ(plan.steps[0].kind, maho::sidebar::kMoveTabToRoot);
  EXPECT_EQ(plan.steps[1].kind, "favorite_tab");
  EXPECT_EQ(plan.favorite_transition.kind,
            SidebarDropPlan::FavoriteTransition::Kind::kFavorite);
}

TEST_F(SidebarPlannerTest, NormalToFavoritesEmitsFavoriteOnly) {
  SidebarDragPayload payload =
      MakeTab("tab-n", SidebarDragOrigin::kNormalSection);
  SidebarDropPlan plan = ResolveDropPlan(SectionKind::kFavorites, payload);
  EXPECT_TRUE(plan.is_valid);
  ASSERT_EQ(plan.steps.size(), 1u)
      << "normal -> favorites must produce [favorite_tab] only";
  EXPECT_EQ(plan.steps[0].kind, "favorite_tab");
  EXPECT_EQ(plan.favorite_transition.kind,
            SidebarDropPlan::FavoriteTransition::Kind::kFavorite);
  EXPECT_EQ(plan.pin_transition.kind,
            SidebarDropPlan::PinTransition::Kind::kNone);
}

TEST_F(SidebarPlannerTest, FolderDropToNormalIsInvalid) {
  SidebarDragPayload payload =
      MakeFolder("folder-1", SidebarDragOrigin::kPinnedSection);
  SidebarDropPlan plan = ResolveDropPlan(SectionKind::kNormal, payload);
  EXPECT_FALSE(plan.is_valid);
}

TEST_F(SidebarPlannerTest, FolderDropToPinnedHasNoSteps) {
  SidebarDragPayload payload =
      MakeFolder("folder-1", SidebarDragOrigin::kPinnedSection);
  SidebarDropPlan plan = ResolveDropPlan(SectionKind::kPinned, payload);
  EXPECT_TRUE(plan.is_valid);
  EXPECT_TRUE(plan.steps.empty());
}

TEST_F(SidebarPlannerTest, PlanSectionDropDelegatesToResolveDropPlan) {
  SidebarDragPayload payload =
      MakeTab("tab-x", SidebarDragOrigin::kNormalSection);
  SidebarDropPlan via_section =
      PlanSectionDrop(MahoSidebarTabSection::kPinned, payload);
  SidebarDropPlan via_resolve =
      ResolveDropPlan(SectionKind::kPinned, payload);
  ASSERT_EQ(via_section.steps.size(), via_resolve.steps.size());
  for (size_t i = 0; i < via_section.steps.size(); ++i) {
    EXPECT_EQ(via_section.steps[i].kind, via_resolve.steps[i].kind);
  }
}

TEST_F(SidebarPlannerTest, PlanFavoritesGridDropDelegatesToResolveDropPlan) {
  SidebarDragPayload payload =
      MakeTab("tab-y", SidebarDragOrigin::kNormalSection);
  SidebarDropPlan via_fav = PlanFavoritesGridDrop(payload);
  SidebarDropPlan via_resolve = ResolveDropPlan(SectionKind::kFavorites, payload);
  ASSERT_EQ(via_fav.steps.size(), via_resolve.steps.size());
  for (size_t i = 0; i < via_fav.steps.size(); ++i) {
    EXPECT_EQ(via_fav.steps[i].kind, via_resolve.steps[i].kind);
  }
}

TEST_F(SidebarPlannerTest, SectionKindFromTabSectionRoundTrips) {
  EXPECT_EQ(SectionKindFromTabSection(MahoSidebarTabSection::kPinned),
            SectionKind::kPinned);
  EXPECT_EQ(SectionKindFromTabSection(MahoSidebarTabSection::kNormal),
            SectionKind::kNormal);
}

TEST_F(SidebarPlannerTest, IsSameSectionRootDropTrueForNormalToNormal) {
  SidebarDragPayload payload =
      MakeTab("tab-1", SidebarDragOrigin::kNormalSection);
  EXPECT_TRUE(IsSameSectionRootDrop(SectionKind::kNormal, payload));
}

TEST_F(SidebarPlannerTest, IsSameSectionRootDropFalseForFolderChild) {
  SidebarDragPayload payload = MakeTab("tab-c", SidebarDragOrigin::kNormalSection, "folder-1");
  EXPECT_FALSE(IsSameSectionRootDrop(SectionKind::kNormal, payload));
}

TEST_F(SidebarPlannerTest, IsSameSectionRootDropFalseForFavorites) {
  SidebarDragPayload payload =
      MakeTab("fav-1", SidebarDragOrigin::kFavorites);
  EXPECT_FALSE(IsSameSectionRootDrop(SectionKind::kFavorites, payload));
}

TEST_F(SidebarPlannerTest, NeedsFolderEscapeTrueWhenFolderIdSet) {
  SidebarDragPayload payload = MakeTab("tab-c", SidebarDragOrigin::kNormalSection, "folder-1");
  EXPECT_TRUE(NeedsFolderEscape(payload));
}

TEST_F(SidebarPlannerTest, NeedsFolderEscapeFalseWhenNoFolder) {
  SidebarDragPayload payload =
      MakeTab("tab-1", SidebarDragOrigin::kNormalSection);
  EXPECT_FALSE(NeedsFolderEscape(payload));
}

TEST_F(SidebarPlannerTest, PinnedToPinnedRootDropIsNoOp) {
  SidebarDragPayload payload =
      MakeTab("tab-p", SidebarDragOrigin::kPinnedSection);
  SidebarDropPlan plan = ResolveDropPlan(SectionKind::kPinned, payload);
  EXPECT_TRUE(plan.is_valid);
  EXPECT_TRUE(plan.steps.empty())
      << "pinned -> pinned same-section root drop must produce no steps";
  EXPECT_EQ(plan.pin_transition.kind,
            SidebarDropPlan::PinTransition::Kind::kNone);
  EXPECT_EQ(plan.favorite_transition.kind,
            SidebarDropPlan::FavoriteTransition::Kind::kNone);
}

TEST_F(SidebarPlannerTest, FolderToFavoritesIsRejected) {
  SidebarDragPayload folder_payload =
      MakeFolder("folder-1", SidebarDragOrigin::kNormalSection);
  SidebarDropPlan plan = ResolveDropPlan(SectionKind::kFavorites, folder_payload);
  EXPECT_FALSE(plan.is_valid)
      << "folder -> favorites must be rejected (is_valid == false)";
  EXPECT_TRUE(plan.steps.empty())
      << "rejected plan must produce no steps";

  SidebarDragPayload pinned_folder_payload =
      MakeFolder("folder-p", SidebarDragOrigin::kPinnedSection);
  SidebarDropPlan pinned_plan =
      ResolveDropPlan(SectionKind::kFavorites, pinned_folder_payload);
  EXPECT_FALSE(pinned_plan.is_valid)
      << "pinned folder -> favorites must also be rejected";
  EXPECT_TRUE(pinned_plan.steps.empty());
}

// ---------------------------------------------------------------------------
// Phase 5 — Pinned/Normal symmetry lock tests
// These tests lock the intended parity contract before the production refactor.
// They document the required invariants and will fail if production logic
// diverges from the symmetric architecture described in the plan.
// ---------------------------------------------------------------------------

// Contract: source_is_pinned is the canonical pin-state signal.
// A tab from kNormalSection with source_is_pinned=false dropped onto kPinned
// must produce pin_tab (just like origin=kNormalSection does).
// A tab from kPinnedSection with source_is_pinned=true dropped onto kNormal
// must produce unpin_tab (mirror of the above).
TEST_F(SidebarPlannerTest,
       SourceIsPinnedIsCanonicalSignalForTabPinTransition) {
  // Normal → Pinned: source_is_pinned=false must emit pin_tab.
  {
    SidebarDragPayload payload =
        MakeTab("tab-a", SidebarDragOrigin::kNormalSection);
    SidebarDropPlan plan = ResolveDropPlan(SectionKind::kPinned, payload);
    EXPECT_TRUE(plan.is_valid);
    ASSERT_EQ(plan.steps.size(), 1u)
        << "normal(source_is_pinned=false) -> pinned must produce [pin_tab]";
    EXPECT_EQ(plan.steps[0].kind, "pin_tab");
    EXPECT_EQ(plan.pin_transition.kind,
              SidebarDropPlan::PinTransition::Kind::kPin);
  }

  // Pinned → Normal: source_is_pinned=true must emit unpin_tab.
  {
    SidebarDragPayload payload =
        MakeTab("tab-b", SidebarDragOrigin::kPinnedSection);
    SidebarDropPlan plan = ResolveDropPlan(SectionKind::kNormal, payload);
    EXPECT_TRUE(plan.is_valid);
    ASSERT_EQ(plan.steps.size(), 1u)
        << "pinned(source_is_pinned=true) -> normal must produce [unpin_tab]";
    EXPECT_EQ(plan.steps[0].kind, "unpin_tab");
    EXPECT_EQ(plan.pin_transition.kind,
              SidebarDropPlan::PinTransition::Kind::kUnpin);
  }
}

// Contract: pin-state parity for folders (Zen Model alignment).
// Folders are type-pinned in the Zen model. Dropping a folder to normal/favorites is invalid.
// Dropping a folder to the pinned section is valid but yields no transitions/steps.


// Contract: same-section same-section root drop is a no-op for BOTH sections.
// This is symmetric by construction: neither section is special-cased.
TEST_F(SidebarPlannerTest,
       SameSectionRootDropIsNoOpForBothSections) {
  // Normal → Normal: no steps.
  {
    SidebarDragPayload payload =
        MakeTab("tab-n", SidebarDragOrigin::kNormalSection);
    SidebarDropPlan plan = ResolveDropPlan(SectionKind::kNormal, payload);
    EXPECT_TRUE(plan.is_valid);
    EXPECT_TRUE(plan.steps.empty())
        << "normal -> normal same-section root drop must produce no steps";
    EXPECT_EQ(plan.pin_transition.kind,
              SidebarDropPlan::PinTransition::Kind::kNone);
  }

  // Pinned → Pinned: no steps.
  {
    SidebarDragPayload payload =
        MakeTab("tab-p", SidebarDragOrigin::kPinnedSection);
    SidebarDropPlan plan = ResolveDropPlan(SectionKind::kPinned, payload);
    EXPECT_TRUE(plan.is_valid);
    EXPECT_TRUE(plan.steps.empty())
        << "pinned -> pinned same-section root drop must produce no steps";
    EXPECT_EQ(plan.pin_transition.kind,
              SidebarDropPlan::PinTransition::Kind::kNone);
  }
}

// Contract: folder-child tab cross-section drops produce the correct
// move_tab_to_root + pin/unpin prelude regardless of which section is the
// target.  This is the "shared prelude" requirement from §3.4 of the plan.
TEST_F(SidebarPlannerTest,
       FolderChildCrossSectionDropProducesCorrectPlanPrelude) {
  // Folder child (normal) → pinned section must produce
  // [move_tab_to_root, pin_tab] in that order.
  {
    SidebarDragPayload payload = MakeTab("child-tab", SidebarDragOrigin::kNormalSection, "folder-1");
    SidebarDropPlan plan = ResolveDropPlan(SectionKind::kPinned, payload);
    EXPECT_TRUE(plan.is_valid);
    ASSERT_EQ(plan.steps.size(), 2u)
        << "folder-child(normal) -> pinned must produce "
           "[move_tab_to_root, pin_tab]";
    EXPECT_EQ(plan.steps[0].kind, maho::sidebar::kMoveTabToRoot);
    EXPECT_EQ(plan.steps[1].kind, "pin_tab");
    EXPECT_EQ(plan.pin_transition.kind,
              SidebarDropPlan::PinTransition::Kind::kPin);
  }

  // Folder child (pinned) → normal section must produce
  // [move_tab_to_root, unpin_tab] — the mirror case (currently a known gap
  // in production; this test documents the required outcome).
  {
    SidebarDragPayload payload = MakeTab("pinned-child-tab", SidebarDragOrigin::kPinnedSection, "folder-1");
    SidebarDropPlan plan = ResolveDropPlan(SectionKind::kNormal, payload);
    EXPECT_TRUE(plan.is_valid);
    ASSERT_EQ(plan.steps.size(), 2u)
        << "folder-child(pinned) -> normal must produce "
           "[move_tab_to_root, unpin_tab] — required parity outcome";
    EXPECT_EQ(plan.steps[0].kind, maho::sidebar::kMoveTabToRoot);
    EXPECT_EQ(plan.steps[1].kind, "unpin_tab");
    EXPECT_EQ(plan.pin_transition.kind,
              SidebarDropPlan::PinTransition::Kind::kUnpin);
  }
}

// Contract: equivalent transition produced by both ResolveDropPlan and
// PlanSectionDrop for cross-section cases (not just same-section no-ops).
// This covers the §3.4 "all visible drop targets share the same planner"
// requirement for section-level targets.
TEST_F(SidebarPlannerTest,
       PlanSectionDropMatchesResolveDropPlanForCrossSectionCases) {
  // Normal → Pinned via both entry points must agree.
  {
    SidebarDragPayload payload =
        MakeTab("tab-1", SidebarDragOrigin::kNormalSection);
    SidebarDropPlan via_section =
        PlanSectionDrop(MahoSidebarTabSection::kPinned, payload);
    SidebarDropPlan via_resolve =
        ResolveDropPlan(SectionKind::kPinned, payload);
    ASSERT_EQ(via_section.steps.size(), via_resolve.steps.size())
        << "PlanSectionDrop and ResolveDropPlan must agree on step count";
    for (size_t i = 0; i < via_section.steps.size(); ++i) {
      EXPECT_EQ(via_section.steps[i].kind, via_resolve.steps[i].kind)
          << "PlanSectionDrop and ResolveDropPlan must produce identical steps "
             "for the same payload/target";
    }
    EXPECT_EQ(via_section.pin_transition.kind, via_resolve.pin_transition.kind);
  }

  // Pinned → Normal via both entry points.
  {
    SidebarDragPayload payload =
        MakeTab("tab-p", SidebarDragOrigin::kPinnedSection);
    SidebarDropPlan via_section =
        PlanSectionDrop(MahoSidebarTabSection::kNormal, payload);
    SidebarDropPlan via_resolve =
        ResolveDropPlan(SectionKind::kNormal, payload);
    ASSERT_EQ(via_section.steps.size(), via_resolve.steps.size());
    for (size_t i = 0; i < via_section.steps.size(); ++i) {
      EXPECT_EQ(via_section.steps[i].kind, via_resolve.steps[i].kind);
    }
    EXPECT_EQ(via_section.pin_transition.kind, via_resolve.pin_transition.kind);
  }

  // Pinned folder → Normal via both entry points.
  {
    SidebarDragPayload payload =
        MakeFolder("folder-p", SidebarDragOrigin::kPinnedSection);
    SidebarDropPlan via_section =
        PlanSectionDrop(MahoSidebarTabSection::kNormal, payload);
    SidebarDropPlan via_resolve =
        ResolveDropPlan(SectionKind::kNormal, payload);
    ASSERT_EQ(via_section.steps.size(), via_resolve.steps.size());
    for (size_t i = 0; i < via_section.steps.size(); ++i) {
      EXPECT_EQ(via_section.steps[i].kind, via_resolve.steps[i].kind);
    }
    EXPECT_EQ(via_section.pin_transition.kind, via_resolve.pin_transition.kind);
  }
}

// Contract: IsSameSectionRootDrop is symmetric.
// It must return false for folder-child payloads regardless of which section.
TEST_F(SidebarPlannerTest,
       IsSameSectionRootDropIsSymmetricAcrossSections) {
  // Root tab in same section → true for both sections.
  {
    SidebarDragPayload n_payload =
        MakeTab("tab-n", SidebarDragOrigin::kNormalSection);
    EXPECT_TRUE(IsSameSectionRootDrop(SectionKind::kNormal, n_payload));

    SidebarDragPayload p_payload =
        MakeTab("tab-p", SidebarDragOrigin::kPinnedSection);
    EXPECT_TRUE(IsSameSectionRootDrop(SectionKind::kPinned, p_payload));
  }

  // Folder-child in same section → false for both sections.
  {
    SidebarDragPayload n_child =
        MakeTab("c-n", SidebarDragOrigin::kNormalSection, "folder-1");
    EXPECT_FALSE(IsSameSectionRootDrop(SectionKind::kNormal, n_child));

    SidebarDragPayload p_child =
        MakeTab("c-p", SidebarDragOrigin::kPinnedSection, "folder-1");
    EXPECT_FALSE(IsSameSectionRootDrop(SectionKind::kPinned, p_child));
  }
}

// Contract: NeedsFolderEscape is section-agnostic (only depends on
// source_parent_folder_id presence).
TEST_F(SidebarPlannerTest,
       NeedsFolderEscapeIsIndependentOfSection) {
  // With folder id — true regardless of section origin.
  {
    SidebarDragPayload normal_child =
        MakeTab("t", SidebarDragOrigin::kNormalSection, "f");
    SidebarDragPayload pinned_child =
        MakeTab("t", SidebarDragOrigin::kPinnedSection, "f");
    EXPECT_TRUE(NeedsFolderEscape(normal_child));
    EXPECT_TRUE(NeedsFolderEscape(pinned_child));
  }

  // Without folder id — false regardless of section origin.
  {
    SidebarDragPayload normal_root =
        MakeTab("t", SidebarDragOrigin::kNormalSection);
    SidebarDragPayload pinned_root =
        MakeTab("t", SidebarDragOrigin::kPinnedSection);
    EXPECT_FALSE(NeedsFolderEscape(normal_root));
    EXPECT_FALSE(NeedsFolderEscape(pinned_root));
  }
}

TEST_F(SidebarPlannerTest,
       PinnedFolderChildSameSectionDropIsNoOpForPinnedSection) {
  SidebarDragPayload payload = MakeTab("pinned-child", SidebarDragOrigin::kPinnedSection, "pinned-folder-1");
  SidebarDropPlan plan = ResolveDropPlan(SectionKind::kPinned, payload);
  EXPECT_TRUE(plan.is_valid);
  EXPECT_EQ(plan.pin_transition.kind,
            SidebarDropPlan::PinTransition::Kind::kNone)
      << "Pinned folder-child to pinned section: no pin transition needed";
  ASSERT_EQ(plan.steps.size(), 1u)
      << "Pinned folder-child to pinned section: [move_tab_to_root] only";
  EXPECT_EQ(plan.steps[0].kind, maho::sidebar::kMoveTabToRoot);
}

TEST_F(SidebarPlannerTest,
       NormalFolderChildSameSectionDropEmitsMoveToRootOnlyNoUnpin) {
  SidebarDragPayload payload = MakeTab("normal-child", SidebarDragOrigin::kNormalSection, "normal-folder-1");
  SidebarDropPlan plan = ResolveDropPlan(SectionKind::kNormal, payload);
  EXPECT_TRUE(plan.is_valid);
  ASSERT_EQ(plan.steps.size(), 1u)
      << "Normal folder-child to normal section: [move_tab_to_root] only";
  EXPECT_EQ(plan.steps[0].kind, maho::sidebar::kMoveTabToRoot);
  EXPECT_EQ(plan.pin_transition.kind,
            SidebarDropPlan::PinTransition::Kind::kNone);
  EXPECT_EQ(plan.favorite_transition.kind,
            SidebarDropPlan::FavoriteTransition::Kind::kNone);
}

TEST_F(SidebarPlannerTest,
       FolderEscapeStepCarriesSourceFolderIdFromPayload) {
  const std::string kSourceFolder = "escape-source-folder-99";
  SidebarDragPayload payload = MakeTab("child-tab-escape", SidebarDragOrigin::kNormalSection, kSourceFolder);
  SidebarDropPlan plan = ResolveDropPlan(SectionKind::kNormal, payload);
  EXPECT_TRUE(plan.is_valid);
  ASSERT_GE(plan.steps.size(), 1u);
  EXPECT_EQ(plan.steps[0].kind, maho::sidebar::kMoveTabToRoot);
  EXPECT_EQ(plan.steps[0].folder_id, kSourceFolder)
      << "move_tab_to_root must carry the source folder_id";
}

TEST_F(SidebarPlannerTest,
       PinnedFolderChildToNormalSectionProducesMoveToRootThenUnpin) {
  SidebarDragPayload payload = MakeTab("pinned-child-to-normal", SidebarDragOrigin::kPinnedSection, "pinned-folder-1");
  SidebarDropPlan plan = ResolveDropPlan(SectionKind::kNormal, payload);
  EXPECT_TRUE(plan.is_valid);
  ASSERT_EQ(plan.steps.size(), 2u)
      << "Pinned folder-child to normal: [move_tab_to_root, unpin_tab]";
  EXPECT_EQ(plan.steps[0].kind, maho::sidebar::kMoveTabToRoot);
  EXPECT_EQ(plan.steps[1].kind, "unpin_tab");
  EXPECT_EQ(plan.pin_transition.kind,
            SidebarDropPlan::PinTransition::Kind::kUnpin);
}

TEST_F(SidebarPlannerTest,
       PinnedFolderToFavoritesIsAlsoRejected) {
  SidebarDragPayload payload =
      MakeFolder("pinned-folder-fav", SidebarDragOrigin::kPinnedSection);
  SidebarDropPlan plan = ResolveDropPlan(SectionKind::kFavorites, payload);
  EXPECT_FALSE(plan.is_valid)
      << "Pinned folder to favorites must be rejected (symmetric with normal)";
  EXPECT_TRUE(plan.steps.empty());
}

TEST_F(SidebarPlannerTest,
       PinnedFolderToPinnedSectionIsNoOp) {
  SidebarDragPayload payload =
      MakeFolder("pinned-folder-same", SidebarDragOrigin::kPinnedSection);
  SidebarDropPlan plan = ResolveDropPlan(SectionKind::kPinned, payload);
  EXPECT_TRUE(plan.is_valid);
  EXPECT_TRUE(plan.steps.empty())
      << "Pinned folder to pinned section: same-section root drop, no steps";
  EXPECT_EQ(plan.pin_transition.kind,
            SidebarDropPlan::PinTransition::Kind::kNone);
}

TEST_F(SidebarPlannerTest,
       IsSameSectionRootDropFalseForPinnedFolderChild) {
  SidebarDragPayload payload = MakeTab("pinned-child-check", SidebarDragOrigin::kPinnedSection, "pf-1");
  EXPECT_FALSE(IsSameSectionRootDrop(SectionKind::kPinned, payload))
      << "A pinned folder-child is not a root drop";
}

TEST_F(SidebarPlannerTest,
       NeedsFolderEscapeTrueForPinnedFolderChild) {
  SidebarDragPayload payload = MakeTab("pinned-child-escape", SidebarDragOrigin::kPinnedSection, "pf-escape-1");
  EXPECT_TRUE(NeedsFolderEscape(payload));
}

TEST_F(SidebarPlannerTest, FavoritesToFavoritesProducesEmptyValidPlan) {
  SidebarDragPayload payload =
      MakeTab("fav-self", SidebarDragOrigin::kFavorites);
  SidebarDropPlan plan = ResolveDropPlan(SectionKind::kFavorites, payload);
  EXPECT_TRUE(plan.is_valid)
      << "favorites -> favorites must produce a valid plan";
  EXPECT_TRUE(plan.steps.empty())
      << "favorites -> favorites state-transition plan must be empty; "
         "positional reorder is handled separately by the grid";
  EXPECT_EQ(plan.favorite_transition.kind,
            SidebarDropPlan::FavoriteTransition::Kind::kNone)
      << "No favorite_transition step for a same-surface favorites drag";
  EXPECT_EQ(plan.pin_transition.kind,
            SidebarDropPlan::PinTransition::Kind::kNone);
}

}  // namespace
}  // namespace maho
