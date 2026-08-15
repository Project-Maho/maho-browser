// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_favorites_grid_view.h"

#include "components/prefs/testing_pref_service.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/views/test/views_test_base.h"

namespace maho {

class MahoSidebarFavoritesPrefTest : public views::ViewsTestBase {
 protected:
  void SetUp() override {
    views::ViewsTestBase::SetUp();
    sidebar_prefs::RegisterProfilePrefs(prefs_.registry());
  }

  TestingPrefServiceSimple prefs_;
};

TEST_F(MahoSidebarFavoritesPrefTest, MailBetaDefaultsOff) {
  EXPECT_FALSE(prefs_.GetBoolean(sidebar_prefs::kMahoMailEnabled));
}

TEST_F(MahoSidebarFavoritesPrefTest,
       DismissHintPersistsPrefAndCollapsesEmptyState) {
  MahoSidebarFavoritesGridView view(nullptr);
  view.SetPrefsForTesting(&prefs_);

  view.Update(MahoSidebarFavoritesModel());
  ASSERT_TRUE(view.empty_state_view_for_testing());
  EXPECT_TRUE(view.empty_state_view_for_testing()->GetVisible());
  EXPECT_FALSE(prefs_.GetBoolean(sidebar_prefs::kFavoritesDragHintDismissed));

  view.DismissHintForTesting();

  EXPECT_TRUE(prefs_.GetBoolean(sidebar_prefs::kFavoritesDragHintDismissed));
  EXPECT_FALSE(view.empty_state_view_for_testing()->GetVisible());
  EXPECT_EQ(view.GetPreferredSize().height(), 0);
}

TEST_F(MahoSidebarFavoritesPrefTest,
       DismissedHintStaysCollapsedUntilDragReveal) {
  prefs_.SetBoolean(sidebar_prefs::kFavoritesDragHintDismissed, true);
  MahoSidebarFavoritesGridView view(nullptr);
  view.SetPrefsForTesting(&prefs_);

  view.Update(MahoSidebarFavoritesModel());
  ASSERT_TRUE(view.empty_state_view_for_testing());
  EXPECT_FALSE(view.empty_state_view_for_testing()->GetVisible());
  EXPECT_EQ(view.GetPreferredSize().height(), 0);

  view.RevealForDrag();

  EXPECT_TRUE(view.is_drag_reveal_active_for_testing());
  EXPECT_TRUE(view.empty_state_view_for_testing()->GetVisible());
  EXPECT_GT(view.GetPreferredSize().height(), 0);

  view.CollapseFromDrag();

  EXPECT_FALSE(view.is_drag_reveal_active_for_testing());
  EXPECT_FALSE(view.empty_state_view_for_testing()->GetVisible());
  EXPECT_EQ(view.GetPreferredSize().height(), 0);
}

}
