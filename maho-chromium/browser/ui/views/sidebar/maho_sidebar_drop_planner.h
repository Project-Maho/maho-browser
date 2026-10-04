// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_DROP_PLANNER_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_DROP_PLANNER_H_

#include <string>
#include <vector>

#include "maho/browser/ui/views/sidebar/maho_sidebar_section_policy.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_state_models.h"

namespace maho {

struct SidebarDropStep {
  SidebarDropStep();
  SidebarDropStep(std::string kind,
                  std::string node_id,
                  std::string space_id,
                  std::string folder_id);
  SidebarDropStep(const SidebarDropStep&);
  SidebarDropStep& operator=(const SidebarDropStep&);
  ~SidebarDropStep();

  std::string kind;
  std::string node_id;
  std::string space_id;
  std::string folder_id;
};

struct SidebarDropPlanPinTransition {
  SidebarDropPlanPinTransition();
  SidebarDropPlanPinTransition(const SidebarDropPlanPinTransition&);
  SidebarDropPlanPinTransition& operator=(const SidebarDropPlanPinTransition&);
  ~SidebarDropPlanPinTransition();

  enum class Kind { kNone, kPin, kUnpin } kind = Kind::kNone;
  std::string node_id;
  std::string space_id;
  bool is_folder = false;
};

struct SidebarDropPlanFavoriteTransition {
  SidebarDropPlanFavoriteTransition();
  SidebarDropPlanFavoriteTransition(const SidebarDropPlanFavoriteTransition&);
  SidebarDropPlanFavoriteTransition& operator=(
      const SidebarDropPlanFavoriteTransition&);
  ~SidebarDropPlanFavoriteTransition();

  enum class Kind { kNone, kFavorite, kUnfavorite } kind = Kind::kNone;
  std::string node_id;
};

struct SidebarDropPlan {
  using PinTransition = SidebarDropPlanPinTransition;
  using FavoriteTransition = SidebarDropPlanFavoriteTransition;

  SidebarDropPlan();
  SidebarDropPlan(const SidebarDropPlan&);
  SidebarDropPlan& operator=(const SidebarDropPlan&);
  ~SidebarDropPlan();

  std::vector<SidebarDropStep> steps;
  bool is_valid = false;

  PinTransition pin_transition;
  FavoriteTransition favorite_transition;
};

SidebarDropPlan ResolveDropPlan(SectionKind target,
                                const SidebarDragPayload& payload);

SidebarDropPlan PlanSectionDrop(MahoSidebarTabSection target_section,
                                const SidebarDragPayload& payload);

SidebarDropPlan PlanFavoritesGridDrop(const SidebarDragPayload& payload);

bool ExecuteDropPlan(const SidebarDropPlan& plan);

bool ExecuteDropPlanTransitionsOnly(const SidebarDropPlan& plan);

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_DROP_PLANNER_H_
