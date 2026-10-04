// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_state_models.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <optional>

#include "base/json/json_writer.h"
#include "base/strings/string_number_conversions.h"
#include "third_party/skia/include/core/SkColor.h"

namespace {

}  // namespace

namespace maho {

MahoSidebarFavoriteItemModel::MahoSidebarFavoriteItemModel() = default;
MahoSidebarFavoriteItemModel::MahoSidebarFavoriteItemModel(
    const MahoSidebarFavoriteItemModel&) = default;
MahoSidebarFavoriteItemModel& MahoSidebarFavoriteItemModel::operator=(
    const MahoSidebarFavoriteItemModel&) = default;
MahoSidebarFavoriteItemModel::~MahoSidebarFavoriteItemModel() = default;

MahoSidebarFavoritesModel::MahoSidebarFavoritesModel() = default;
MahoSidebarFavoritesModel::MahoSidebarFavoritesModel(
    const MahoSidebarFavoritesModel&) = default;
MahoSidebarFavoritesModel::MahoSidebarFavoritesModel(
    MahoSidebarFavoritesModel&&) = default;
MahoSidebarFavoritesModel& MahoSidebarFavoritesModel::operator=(
    const MahoSidebarFavoritesModel&) = default;
MahoSidebarFavoritesModel& MahoSidebarFavoritesModel::operator=(
    MahoSidebarFavoritesModel&&) = default;
MahoSidebarFavoritesModel::~MahoSidebarFavoritesModel() = default;

SidebarTreeNode::SidebarTreeNode() = default;
SidebarTreeNode::SidebarTreeNode(const SidebarTreeNode&) = default;
SidebarTreeNode::SidebarTreeNode(SidebarTreeNode&&) = default;
SidebarTreeNode& SidebarTreeNode::operator=(const SidebarTreeNode&) = default;
SidebarTreeNode& SidebarTreeNode::operator=(SidebarTreeNode&&) = default;
SidebarTreeNode::~SidebarTreeNode() = default;

MahoSidebarActiveTabModel::MahoSidebarActiveTabModel() = default;
MahoSidebarActiveTabModel::MahoSidebarActiveTabModel(
    const MahoSidebarActiveTabModel&) = default;
MahoSidebarActiveTabModel& MahoSidebarActiveTabModel::operator=(
    const MahoSidebarActiveTabModel&) = default;
MahoSidebarActiveTabModel::~MahoSidebarActiveTabModel() = default;

MahoSidebarFooterUpdatePillModel::MahoSidebarFooterUpdatePillModel() = default;
MahoSidebarFooterUpdatePillModel::MahoSidebarFooterUpdatePillModel(
    const MahoSidebarFooterUpdatePillModel&) = default;
MahoSidebarFooterUpdatePillModel& MahoSidebarFooterUpdatePillModel::operator=(
    const MahoSidebarFooterUpdatePillModel&) = default;
MahoSidebarFooterUpdatePillModel::~MahoSidebarFooterUpdatePillModel() = default;

MahoSidebarFooterModel::MahoSidebarFooterModel()
    : status_text(),
      space_count(0),
      active_space_index(-1),
      space_color(),
      can_open_space_controls(true),
      can_create_space(true),
      update_pill() {}

MahoSidebarFooterModel::MahoSidebarFooterModel(
    const MahoSidebarFooterModel&) = default;
MahoSidebarFooterModel::MahoSidebarFooterModel(MahoSidebarFooterModel&&) = default;
MahoSidebarFooterModel& MahoSidebarFooterModel::operator=(
    const MahoSidebarFooterModel&) = default;
MahoSidebarFooterModel& MahoSidebarFooterModel::operator=(
    MahoSidebarFooterModel&&) = default;
MahoSidebarFooterModel::~MahoSidebarFooterModel() = default;

MahoSidebarTabListModel::MahoSidebarTabListModel() = default;
MahoSidebarTabListModel::MahoSidebarTabListModel(
    const MahoSidebarTabListModel&) = default;
MahoSidebarTabListModel::MahoSidebarTabListModel(
    MahoSidebarTabListModel&&) = default;
MahoSidebarTabListModel& MahoSidebarTabListModel::operator=(
    const MahoSidebarTabListModel&) = default;
MahoSidebarTabListModel& MahoSidebarTabListModel::operator=(
    MahoSidebarTabListModel&&) = default;
MahoSidebarTabListModel::~MahoSidebarTabListModel() = default;

SidebarDragPayload::SidebarDragPayload() = default;
SidebarDragPayload::SidebarDragPayload(const SidebarDragPayload&) = default;
SidebarDragPayload& SidebarDragPayload::operator=(
    const SidebarDragPayload&) = default;
SidebarDragPayload::~SidebarDragPayload() = default;

MahoSidebarViewStateModel::MahoSidebarViewStateModel() = default;
MahoSidebarViewStateModel::MahoSidebarViewStateModel(
    const MahoSidebarViewStateModel&) = default;
MahoSidebarViewStateModel::MahoSidebarViewStateModel(
    MahoSidebarViewStateModel&&) = default;
MahoSidebarViewStateModel& MahoSidebarViewStateModel::operator=(
    const MahoSidebarViewStateModel&) = default;
MahoSidebarViewStateModel& MahoSidebarViewStateModel::operator=(
    MahoSidebarViewStateModel&&) = default;
MahoSidebarViewStateModel::~MahoSidebarViewStateModel() = default;

}  // namespace maho
