// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_toolbar_button_provider.h"

#include <type_traits>

#include "testing/gtest/include/gtest/gtest.h"
#include "ui/views/view.h"

namespace maho {
namespace {

static_assert(!std::is_abstract_v<MahoToolbarButtonProvider>);
static_assert(std::is_same_v<
              decltype(&MahoToolbarButtonProvider::GetPageActionBubbleAnchor),
              views::BubbleAnchor (MahoToolbarButtonProvider::*)(actions::ActionId)>);

class MahoToolbarButtonProviderAnchorTest : public testing::Test {
 protected:
  views::View* Resolve(MahoHeaderAnchorKind kind,
                       bool translate_drawn,
                       bool utility_drawn,
                       bool security_drawn) {
    return MahoToolbarButtonProvider::ResolveHeaderAnchor(
        kind, &translate_, translate_drawn, &utility_, utility_drawn,
        &security_, security_drawn);
  }

  views::View translate_;
  views::View utility_;
  views::View security_;
};

TEST_F(MahoToolbarButtonProviderAnchorTest, EachKindPrefersItsOwnControl) {
  EXPECT_EQ(&translate_,
            Resolve(MahoHeaderAnchorKind::kTranslate, true, true, true));
  EXPECT_EQ(&security_,
            Resolve(MahoHeaderAnchorKind::kSecurity, true, true, true));
  EXPECT_EQ(&utility_,
            Resolve(MahoHeaderAnchorKind::kUtility, true, true, true));
}

TEST_F(MahoToolbarButtonProviderAnchorTest, UndrawnControlFallsBackToUtility) {
  EXPECT_EQ(&utility_,
            Resolve(MahoHeaderAnchorKind::kTranslate, false, true, true));
  EXPECT_EQ(&utility_,
            Resolve(MahoHeaderAnchorKind::kSecurity, true, true, false));
}

TEST_F(MahoToolbarButtonProviderAnchorTest, HiddenHeaderYieldsNoAnchor) {
  // Fullscreen hides the header: nothing is drawn, so the provider must use
  // its last-resort anchor.
  for (MahoHeaderAnchorKind kind :
       {MahoHeaderAnchorKind::kTranslate, MahoHeaderAnchorKind::kUtility,
        MahoHeaderAnchorKind::kSecurity}) {
    EXPECT_EQ(nullptr, Resolve(kind, false, false, false));
  }
}

TEST_F(MahoToolbarButtonProviderAnchorTest, MissingControlsYieldNoAnchor) {
  EXPECT_EQ(nullptr, MahoToolbarButtonProvider::ResolveHeaderAnchor(
                         MahoHeaderAnchorKind::kTranslate, nullptr, true,
                         nullptr, true, nullptr, true));
}

}  // namespace
}  // namespace maho
