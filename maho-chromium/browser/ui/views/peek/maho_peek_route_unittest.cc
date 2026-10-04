// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/peek/maho_peek_route.h"

#include <string>
#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

namespace maho {
namespace {

struct RouteCase {
  std::string name;
  PeekRouteInput input;
  PeekRoute expected;
};

PeekRouteInput EligibleInput(PeekSeam seam) {
  return PeekRouteInput{
      .master_enabled = true,
      .popup_routing_enabled = true,
      .link_routing_enabled = true,
      .seam = seam,
      .disposition = seam == PeekSeam::kAddNewContents
                         ? content::WindowOpenDisposition::NEW_POPUP
                     : seam == PeekSeam::kOpenUrlFromTab
                         ? content::WindowOpenDisposition::NEW_FOREGROUND_TAB
                         : content::WindowOpenDisposition::CURRENT_TAB,
      .source_role = seam == PeekSeam::kAddNewContents
                         ? PeekSourceRole::kNormal
                         : PeekSourceRole::kPinned,
      .is_user_initiated = true,
      .force_tab = false,
      .is_maho_mini_gesture = false,
      .atc_has_cross_space_target = false,
      .peek_slot_state = PeekSlotState::kClosed,
  };
}

void ExpectCases(const std::vector<RouteCase>& cases) {
  for (const auto& test_case : cases) {
    EXPECT_EQ(test_case.expected, DecidePeekRoute(test_case.input))
        << test_case.name;
  }
}

std::vector<RouteCase> CommonPrecedenceCases(PeekSeam seam) {
  const PeekRouteInput eligible = EligibleInput(seam);
  std::vector<RouteCase> cases;

  auto master_off = eligible;
  master_off.master_enabled = false;
  cases.push_back({"master disabled", master_off, PeekRoute::kFallThrough});

  auto mini_gesture = eligible;
  mini_gesture.is_maho_mini_gesture = true;
  cases.push_back({"Maho Mini gesture", mini_gesture, PeekRoute::kFallThrough});

  auto cross_space = eligible;
  cross_space.atc_has_cross_space_target = true;
  cases.push_back(
      {"ATC cross-space target", cross_space, PeekRoute::kFallThrough});

  auto unresolved = eligible;
  unresolved.source_role = PeekSourceRole::kUnresolved;
  cases.push_back(
      {"unresolved source role", unresolved, PeekRoute::kFallThrough});

  auto busy = eligible;
  busy.peek_slot_state = PeekSlotState::kBusy;
  cases.push_back({"busy Peek slot", busy, PeekRoute::kOpenInForegroundTab});

  cases.push_back({"eligible closed Peek slot", eligible,
                   seam == PeekSeam::kAddNewContents
                       ? PeekRoute::kOpenInForegroundTab
                       : PeekRoute::kOpenInPeek});
  return cases;
}

class MahoPeekRouteTest : public testing::Test {};

TEST_F(MahoPeekRouteTest, AddNewContentsBranches) {
  std::vector<RouteCase> cases =
      CommonPrecedenceCases(PeekSeam::kAddNewContents);
  const PeekRouteInput eligible = EligibleInput(PeekSeam::kAddNewContents);

  auto popup_disabled = eligible;
  popup_disabled.popup_routing_enabled = false;
  cases.push_back(
      {"popup routing disabled", popup_disabled, PeekRoute::kFallThrough});

  auto force_tab = eligible;
  force_tab.force_tab = true;
  cases.push_back({"force tab does not affect popup routing", force_tab,
                   PeekRoute::kOpenInForegroundTab});

  for (const auto disposition : {
           content::WindowOpenDisposition::UNKNOWN,
           content::WindowOpenDisposition::CURRENT_TAB,
           content::WindowOpenDisposition::SINGLETON_TAB,
           content::WindowOpenDisposition::NEW_FOREGROUND_TAB,
           content::WindowOpenDisposition::NEW_BACKGROUND_TAB,
           content::WindowOpenDisposition::NEW_WINDOW,
           content::WindowOpenDisposition::SAVE_TO_DISK,
           content::WindowOpenDisposition::OFF_THE_RECORD,
           content::WindowOpenDisposition::IGNORE_ACTION,
           content::WindowOpenDisposition::SWITCH_TO_TAB,
           content::WindowOpenDisposition::NEW_PICTURE_IN_PICTURE,
           content::WindowOpenDisposition::NEW_SPLIT_VIEW,
       }) {
    auto wrong_disposition = eligible;
    wrong_disposition.disposition = disposition;
    cases.push_back({"non-NEW_POPUP disposition", wrong_disposition,
                     PeekRoute::kFallThrough});
  }

  ExpectCases(cases);
}

TEST_F(MahoPeekRouteTest, OpenUrlFromTabBranches) {
  std::vector<RouteCase> cases =
      CommonPrecedenceCases(PeekSeam::kOpenUrlFromTab);
  const PeekRouteInput eligible = EligibleInput(PeekSeam::kOpenUrlFromTab);

  auto background_tab = eligible;
  background_tab.disposition =
      content::WindowOpenDisposition::NEW_BACKGROUND_TAB;
  cases.push_back(
      {"background-tab disposition", background_tab, PeekRoute::kOpenInPeek});

  auto new_window = eligible;
  new_window.disposition = content::WindowOpenDisposition::NEW_WINDOW;
  cases.push_back(
      {"new-window disposition", new_window, PeekRoute::kOpenInPeek});

  auto link_disabled = eligible;
  link_disabled.link_routing_enabled = false;
  cases.push_back(
      {"link routing disabled", link_disabled, PeekRoute::kFallThrough});

  for (const PeekSourceRole source_role : {PeekSourceRole::kPinned,
                                           PeekSourceRole::kFavorite}) {
    auto force_tab = eligible;
    force_tab.force_tab = true;
    force_tab.source_role = source_role;
    cases.push_back({"force tab bypasses OpenURLFromTab Peek", force_tab,
                     PeekRoute::kFallThrough});
  }

  auto normal_source = eligible;
  normal_source.source_role = PeekSourceRole::kNormal;
  cases.push_back({"normal source", normal_source, PeekRoute::kFallThrough});

  auto favorite_source = eligible;
  favorite_source.source_role = PeekSourceRole::kFavorite;
  cases.push_back({"favorite source", favorite_source, PeekRoute::kOpenInPeek});

  auto not_user_initiated = eligible;
  not_user_initiated.is_user_initiated = false;
  cases.push_back(
      {"not user initiated", not_user_initiated, PeekRoute::kFallThrough});

  for (const auto disposition : {
           content::WindowOpenDisposition::UNKNOWN,
           content::WindowOpenDisposition::CURRENT_TAB,
           content::WindowOpenDisposition::SINGLETON_TAB,
           content::WindowOpenDisposition::NEW_POPUP,
           content::WindowOpenDisposition::SAVE_TO_DISK,
           content::WindowOpenDisposition::OFF_THE_RECORD,
           content::WindowOpenDisposition::IGNORE_ACTION,
           content::WindowOpenDisposition::SWITCH_TO_TAB,
           content::WindowOpenDisposition::NEW_PICTURE_IN_PICTURE,
           content::WindowOpenDisposition::NEW_SPLIT_VIEW,
       }) {
    auto wrong_disposition = eligible;
    wrong_disposition.disposition = disposition;
    cases.push_back({"disposition outside the OpenURLFromTab set",
                     wrong_disposition, PeekRoute::kFallThrough});
  }

  ExpectCases(cases);
}

TEST_F(MahoPeekRouteTest, SameTabThrottleBranches) {
  std::vector<RouteCase> cases =
      CommonPrecedenceCases(PeekSeam::kSameTabThrottle);
  const PeekRouteInput eligible = EligibleInput(PeekSeam::kSameTabThrottle);

  auto link_disabled = eligible;
  link_disabled.link_routing_enabled = false;
  cases.push_back(
      {"link routing disabled", link_disabled, PeekRoute::kFallThrough});

  for (const PeekSourceRole source_role : {PeekSourceRole::kPinned,
                                           PeekSourceRole::kFavorite}) {
    auto force_tab = eligible;
    force_tab.force_tab = true;
    force_tab.source_role = source_role;
    cases.push_back({"force tab bypasses same-tab Peek", force_tab,
                     PeekRoute::kFallThrough});
  }

  auto normal_source = eligible;
  normal_source.source_role = PeekSourceRole::kNormal;
  cases.push_back({"normal source", normal_source, PeekRoute::kFallThrough});

  auto favorite_source = eligible;
  favorite_source.source_role = PeekSourceRole::kFavorite;
  cases.push_back({"favorite source", favorite_source, PeekRoute::kOpenInPeek});

  auto not_user_initiated = eligible;
  not_user_initiated.is_user_initiated = false;
  cases.push_back(
      {"not user initiated", not_user_initiated, PeekRoute::kFallThrough});

  for (const auto disposition : {
           content::WindowOpenDisposition::UNKNOWN,
           content::WindowOpenDisposition::SINGLETON_TAB,
           content::WindowOpenDisposition::NEW_FOREGROUND_TAB,
           content::WindowOpenDisposition::NEW_BACKGROUND_TAB,
           content::WindowOpenDisposition::NEW_POPUP,
           content::WindowOpenDisposition::NEW_WINDOW,
           content::WindowOpenDisposition::SAVE_TO_DISK,
           content::WindowOpenDisposition::OFF_THE_RECORD,
           content::WindowOpenDisposition::IGNORE_ACTION,
           content::WindowOpenDisposition::SWITCH_TO_TAB,
           content::WindowOpenDisposition::NEW_PICTURE_IN_PICTURE,
           content::WindowOpenDisposition::NEW_SPLIT_VIEW,
       }) {
    auto wrong_disposition = eligible;
    wrong_disposition.disposition = disposition;
    cases.push_back({"non-CURRENT_TAB disposition", wrong_disposition,
                     PeekRoute::kFallThrough});
  }

  ExpectCases(cases);
}

TEST_F(MahoPeekRouteTest, ForcePeekModifierBranches) {
  const PeekRouteInput link_eligible = EligibleInput(PeekSeam::kOpenUrlFromTab);

  // Shift-click (force_peek) arrives as a renderer-initiated NEW_WINDOW and
  // opens Peek from ANY tab, bypassing the pinned/favorite source requirement.
  auto normal_force_peek = link_eligible;
  normal_force_peek.source_role = PeekSourceRole::kNormal;
  normal_force_peek.disposition = content::WindowOpenDisposition::NEW_WINDOW;
  normal_force_peek.force_peek = true;

  std::vector<RouteCase> cases;
  cases.push_back({"force peek from normal tab opens Peek", normal_force_peek,
                   PeekRoute::kOpenInPeek});

  auto unresolved_force_peek = normal_force_peek;
  unresolved_force_peek.source_role = PeekSourceRole::kUnresolved;
  cases.push_back({"force peek from unresolved source opens Peek",
                   unresolved_force_peek, PeekRoute::kOpenInPeek});

  auto busy_force_peek = normal_force_peek;
  busy_force_peek.peek_slot_state = PeekSlotState::kBusy;
  cases.push_back({"force peek with busy slot routes to foreground tab",
                   busy_force_peek, PeekRoute::kOpenInForegroundTab});

  auto master_off = normal_force_peek;
  master_off.master_enabled = false;
  cases.push_back(
      {"force peek respects master disabled", master_off, PeekRoute::kFallThrough});

  auto link_off = normal_force_peek;
  link_off.link_routing_enabled = false;
  cases.push_back({"force peek respects link routing disabled", link_off,
                   PeekRoute::kFallThrough});

  auto mini_force_peek = normal_force_peek;
  mini_force_peek.is_maho_mini_gesture = true;
  cases.push_back({"Maho Mini gesture beats force peek", mini_force_peek,
                   PeekRoute::kFallThrough});

  auto atc_force_peek = normal_force_peek;
  atc_force_peek.atc_has_cross_space_target = true;
  cases.push_back({"ATC cross-space beats force peek", atc_force_peek,
                   PeekRoute::kFallThrough});

  // force_peek must NOT rescue a popup whose popup routing is disabled.
  auto popup_force_peek = EligibleInput(PeekSeam::kAddNewContents);
  popup_force_peek.popup_routing_enabled = false;
  popup_force_peek.force_peek = true;
  cases.push_back({"force peek does not affect popup routing", popup_force_peek,
                   PeekRoute::kFallThrough});

  ExpectCases(cases);
}

}  // namespace
}  // namespace maho
