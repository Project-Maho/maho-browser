// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_drop_planner.h"

#include "maho/browser/ui/views/sidebar/maho_sidebar_section_policy.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_state_adapter.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_dnd_events.h"

namespace maho {

SidebarDropStep::SidebarDropStep() = default;

SidebarDropStep::SidebarDropStep(std::string k,
                                 std::string n,
                                 std::string s,
                                 std::string f)
    : kind(std::move(k)),
      node_id(std::move(n)),
      space_id(std::move(s)),
      folder_id(std::move(f)) {}

SidebarDropStep::SidebarDropStep(const SidebarDropStep&) = default;
SidebarDropStep& SidebarDropStep::operator=(const SidebarDropStep&) = default;
SidebarDropStep::~SidebarDropStep() = default;

SidebarDropPlan::SidebarDropPlan() = default;
SidebarDropPlan::SidebarDropPlan(const SidebarDropPlan&) = default;
SidebarDropPlan& SidebarDropPlan::operator=(const SidebarDropPlan&) = default;
SidebarDropPlan::~SidebarDropPlan() = default;

SidebarDropPlanPinTransition::SidebarDropPlanPinTransition() = default;
SidebarDropPlanPinTransition::SidebarDropPlanPinTransition(
    const SidebarDropPlanPinTransition&) = default;
SidebarDropPlanPinTransition& SidebarDropPlanPinTransition::operator=(
    const SidebarDropPlanPinTransition&) = default;
SidebarDropPlanPinTransition::~SidebarDropPlanPinTransition() = default;

SidebarDropPlanFavoriteTransition::SidebarDropPlanFavoriteTransition() =
    default;
SidebarDropPlanFavoriteTransition::SidebarDropPlanFavoriteTransition(
    const SidebarDropPlanFavoriteTransition&) = default;
SidebarDropPlanFavoriteTransition&
SidebarDropPlanFavoriteTransition::operator=(
    const SidebarDropPlanFavoriteTransition&) = default;
SidebarDropPlanFavoriteTransition::~SidebarDropPlanFavoriteTransition() =
    default;

SidebarDropPlan ResolveDropPlan(SectionKind target,
                                const SidebarDragPayload& payload) {
  SidebarDropPlan plan;
  plan.is_valid = true;

  const bool is_tab = (payload.node_kind == SidebarNodeKind::kTab);
  const bool source_is_pinned =
      (payload.origin == SidebarDragOrigin::kPinnedSection);

  if (!is_tab) {
    // Folders can never be dropped into the Favorites strip.
    if (target == SectionKind::kFavorites) {
      plan.is_valid = false;
      return plan;
    }
    // Folder drops to Pinned/Normal are valid. A whole-folder move that crosses
    // the Pinned/Normal boundary flips the folder's pin state; a same-section
    // folder drop is a pure positional reorder that carries no pin transition.
    const bool to_pinned_folder = (target == SectionKind::kPinned);
    if (to_pinned_folder && !source_is_pinned) {
      plan.pin_transition.kind = SidebarDropPlan::PinTransition::Kind::kPin;
      plan.pin_transition.is_folder = true;
      plan.pin_transition.node_id = payload.node_id;
      plan.pin_transition.space_id = payload.space_id;
    } else if (!to_pinned_folder && source_is_pinned) {
      plan.pin_transition.kind = SidebarDropPlan::PinTransition::Kind::kUnpin;
      plan.pin_transition.is_folder = true;
      plan.pin_transition.node_id = payload.node_id;
      plan.pin_transition.space_id = payload.space_id;
    }
    return plan;
  }

  const bool needs_folder_escape = NeedsFolderEscape(payload);
  // origin is read here only to detect the kFavorites provenance case —
  // distinguishing "tab being dragged out of the favorites strip" from
  // "tab being dragged within the normal/pinned section".  source_is_pinned
  // cannot encode this: a favorites tab has source_is_pinned=false but is
  // semantically different from a normal-section tab.  This is the one
  // sanctioned use of origin; all pin-state decisions use source_is_pinned.
  const bool from_favorites = (payload.origin == SidebarDragOrigin::kFavorites);

  if (target == SectionKind::kFavorites) {
    // favorites -> favorites: positional reorder is handled separately by the
    // grid; the state-transition plan is empty (tab is already a favorite).
    if (from_favorites) {
      return plan;
    }
    if (needs_folder_escape) {
      plan.steps.push_back({maho::sidebar::kMoveTabToRoot, payload.node_id,
                             payload.space_id,
                             payload.source_parent_folder_id});
    }
    if (source_is_pinned) {
      plan.pin_transition.kind =
          SidebarDropPlan::PinTransition::Kind::kUnpin;
      plan.pin_transition.node_id = payload.node_id;
    }
    plan.favorite_transition.kind =
        SidebarDropPlan::FavoriteTransition::Kind::kFavorite;
    plan.favorite_transition.node_id = payload.node_id;
    plan.steps.push_back({"favorite_tab", payload.node_id, {}, {}});
    return plan;
  }

  const bool to_pinned = (target == SectionKind::kPinned);

  if (from_favorites) {
    plan.favorite_transition.kind =
        SidebarDropPlan::FavoriteTransition::Kind::kUnfavorite;
    plan.favorite_transition.node_id = payload.node_id;
    if (to_pinned) {
      plan.steps.push_back({"change_tab_role_pinned", payload.node_id, {}, {}});
      plan.pin_transition.kind = SidebarDropPlan::PinTransition::Kind::kPin;
      plan.pin_transition.node_id = payload.node_id;
    } else {
      plan.steps.push_back({"change_tab_role_normal", payload.node_id, {}, {}});
    }
    return plan;
  }

  if (needs_folder_escape) {
    plan.steps.push_back({maho::sidebar::kMoveTabToRoot, payload.node_id,
                           payload.space_id,
                           payload.source_parent_folder_id});
    if (to_pinned && !source_is_pinned) {
      plan.pin_transition.kind = SidebarDropPlan::PinTransition::Kind::kPin;
      plan.pin_transition.node_id = payload.node_id;
      plan.steps.push_back({"pin_tab", payload.node_id, {}, {}});
    } else if (!to_pinned && source_is_pinned) {
      plan.pin_transition.kind = SidebarDropPlan::PinTransition::Kind::kUnpin;
      plan.pin_transition.node_id = payload.node_id;
      plan.steps.push_back({"unpin_tab", payload.node_id, {}, {}});
    }
    return plan;
  }

  if (IsSameSectionRootDrop(target, payload)) {
    return plan;
  }

  if (to_pinned) {
    plan.pin_transition.kind = SidebarDropPlan::PinTransition::Kind::kPin;
    plan.pin_transition.node_id = payload.node_id;
    plan.steps.push_back({"pin_tab", payload.node_id, {}, {}});
  } else if (source_is_pinned) {
    plan.pin_transition.kind = SidebarDropPlan::PinTransition::Kind::kUnpin;
    plan.pin_transition.node_id = payload.node_id;
    plan.steps.push_back({"unpin_tab", payload.node_id, {}, {}});
  }
  return plan;
}

SidebarDropPlan PlanSectionDrop(MahoSidebarTabSection target_section,
                                const SidebarDragPayload& payload) {
  return ResolveDropPlan(SectionKindFromTabSection(target_section), payload);
}

SidebarDropPlan PlanFavoritesGridDrop(const SidebarDragPayload& payload) {
  return ResolveDropPlan(SectionKind::kFavorites, payload);
}

namespace {
void DispatchRoleTransitionEvent(const std::string& node_id, const std::string& role_type) {
  base::DictValue event;
  event.Set("tab_id", node_id);
  base::DictValue new_role;
  new_role.Set("type", role_type);
  event.Set("new_role", std::move(new_role));
  DispatchShellEventDict("change_tab_role", std::move(event));
}
}  // namespace

bool ExecuteDropPlan(const SidebarDropPlan& plan) {
  if (!plan.is_valid || plan.steps.empty()) {
    return false;
  }
  for (const SidebarDropStep& step : plan.steps) {
    if (step.kind == maho::sidebar::kMoveTabToRoot) {
      DispatchShellEvent(step.kind, {
          {"space_id", step.space_id},
          {"folder_id", step.folder_id},
          {"tab_id", step.node_id}});
    } else if (step.kind == "change_tab_role_normal") {
      DispatchRoleTransitionEvent(step.node_id, "normal");
    } else if (step.kind == "change_tab_role_pinned") {
      DispatchRoleTransitionEvent(step.node_id, "pinned");
    } else {
      DispatchShellEvent(step.kind, {{"tab_id", step.node_id}});
    }
  }
  return true;
}

bool ExecuteDropPlanTransitionsOnly(const SidebarDropPlan& plan) {
  if (!plan.is_valid) {
    return false;
  }
  bool executed = false;
  for (const SidebarDropStep& step : plan.steps) {
    if (step.kind == maho::sidebar::kMoveTabToRoot) {
      continue;
    }
    if (step.kind == "change_tab_role_normal") {
      DispatchRoleTransitionEvent(step.node_id, "normal");
    } else if (step.kind == "change_tab_role_pinned") {
      DispatchRoleTransitionEvent(step.node_id, "pinned");
    } else {
      DispatchShellEvent(step.kind, {{"tab_id", step.node_id}});
    }
    executed = true;
  }
  return executed;
}

}  // namespace maho
