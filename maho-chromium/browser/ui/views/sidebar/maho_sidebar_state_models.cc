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

double NormalizeHueToDegrees(double hue) {
  if (std::abs(hue) <= 1.0) {
    hue *= 360.0;
  }
  hue = std::fmod(hue, 360.0);
  if (hue < 0.0) {
    hue += 360.0;
  }
  return hue;
}

std::string HexColorFromSkColor(SkColor color) {
  std::array<char, 8> buffer = {};
  snprintf(buffer.data(), buffer.size(), "#%02X%02X%02X", SkColorGetR(color),
           SkColorGetG(color), SkColorGetB(color));
  return std::string(buffer.data());
}

std::optional<std::string> TryHexColorFromRgbList(const base::Value* value) {
  const auto* list = value ? value->GetIfList() : nullptr;
  if (!list || list->size() != 3) {
    return std::nullopt;
  }
  auto to_channel = [](const base::Value& channel) -> std::optional<int> {
    if (channel.is_int()) {
      return std::clamp(channel.GetInt(), 0, 255);
    }
    if (channel.is_double()) {
      return std::clamp(static_cast<int>(channel.GetDouble()), 0, 255);
    }
    return std::nullopt;
  };
  const std::optional<int> r = to_channel((*list)[0]);
  const std::optional<int> g = to_channel((*list)[1]);
  const std::optional<int> b = to_channel((*list)[2]);
  if (!r || !g || !b) {
    return std::nullopt;
  }
  return HexColorFromSkColor(SkColorSetRGB(*r, *g, *b));
}

std::optional<std::string> TryHexColorFromColorDict(const base::DictValue* dict) {
  if (!dict) {
    return std::nullopt;
  }
  const std::string* raw_hex = dict->FindString("c");
  if (raw_hex && raw_hex->size() == 7 && (*raw_hex)[0] == '#') {
    return *raw_hex;
  }
  if (const base::Value* raw_rgb = dict->Find("c")) {
    if (std::optional<std::string> from_rgb = TryHexColorFromRgbList(raw_rgb)) {
      return from_rgb;
    }
  }

  const std::optional<double> hue = dict->FindDouble("hue");
  const std::optional<double> saturation = dict->FindDouble("saturation");
  const std::optional<double> brightness = dict->FindDouble("brightness");
  if (!hue || !saturation || !brightness) {
    return std::nullopt;
  }
  SkScalar hsv[3] = {
      static_cast<SkScalar>(NormalizeHueToDegrees(*hue)),
      static_cast<SkScalar>(std::clamp(*saturation, 0.0, 1.0)),
      static_cast<SkScalar>(std::clamp(*brightness, 0.0, 1.0))};
  return HexColorFromSkColor(SkHSVToColor(hsv));
}

void MaybeAppendPaletteColor(std::vector<std::string>* palette,
                             const std::optional<std::string>& color) {
  if (!palette || !color || color->empty()) {
    return;
  }
  if (std::find(palette->begin(), palette->end(), *color) == palette->end()) {
    palette->push_back(*color);
  }
}

}  // namespace

namespace maho {

MahoSidebarSearchModel::MahoSidebarSearchModel() = default;
MahoSidebarSearchModel::MahoSidebarSearchModel(
    const MahoSidebarSearchModel&) = default;
MahoSidebarSearchModel& MahoSidebarSearchModel::operator=(
    const MahoSidebarSearchModel&) = default;
MahoSidebarSearchModel::~MahoSidebarSearchModel() = default;

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

MahoSpaceDisplayThemeModel::MahoSpaceDisplayThemeModel() = default;
MahoSpaceDisplayThemeModel::MahoSpaceDisplayThemeModel(
    const MahoSpaceDisplayThemeModel&) = default;
MahoSpaceDisplayThemeModel::MahoSpaceDisplayThemeModel(
    MahoSpaceDisplayThemeModel&&) = default;
MahoSpaceDisplayThemeModel& MahoSpaceDisplayThemeModel::operator=(
    const MahoSpaceDisplayThemeModel&) = default;
MahoSpaceDisplayThemeModel& MahoSpaceDisplayThemeModel::operator=(
    MahoSpaceDisplayThemeModel&&) = default;
MahoSpaceDisplayThemeModel::~MahoSpaceDisplayThemeModel() = default;

MahoSpaceDisplayThemeModel BuildMahoSpaceDisplayThemeModel(
    const base::DictValue& space_dict) {
  MahoSpaceDisplayThemeModel model;
  const base::DictValue* theme_dict = space_dict.FindDict("theme");
  if (!theme_dict) {
    return model;
  }

  model.has_theme = true;
  if (const std::string* type = theme_dict->FindString("type")) {
    if (*type == "solid") {
      model.kind = MahoSpaceDisplayThemeKind::kSolid;
    } else if (*type == "gradient") {
      model.kind = MahoSpaceDisplayThemeKind::kGradient;
    } else if (*type == "zen") {
      model.kind = MahoSpaceDisplayThemeKind::kZen;
    }
  }
  model.opacity = static_cast<float>(
      std::clamp(theme_dict->FindDouble("opacity").value_or(1.0), 0.0, 1.0));
  model.texture = static_cast<float>(
      std::clamp(theme_dict->FindDouble("texture").value_or(0.0), 0.0, 1.0));

  if (const base::DictValue* color_dict = theme_dict->FindDict("color")) {
    model.grain = static_cast<float>(
        std::clamp(color_dict->FindDouble("grain").value_or(0.0), 0.0, 1.0));
    model.primary_color = TryHexColorFromColorDict(color_dict).value_or(std::string());
    MaybeAppendPaletteColor(&model.palette_colors, model.primary_color);
  }

  if (const base::ListValue* colors = theme_dict->FindList("gradientColors")) {
    for (const auto& entry : *colors) {
      const base::DictValue* color_dict = entry.GetIfDict();
      if (!color_dict) {
        continue;
      }
      std::optional<std::string> hex = TryHexColorFromColorDict(color_dict);
      if (!hex || hex->empty()) {
        continue;
      }
      MaybeAppendPaletteColor(&model.palette_colors, hex);
      if (color_dict->FindBool("isPrimary").value_or(false)) {
        model.primary_color = *hex;
      } else if (model.secondary_color.empty()) {
        model.secondary_color = *hex;
      }
    }
  }

  if (model.primary_color.empty() && !model.palette_colors.empty()) {
    model.primary_color = model.palette_colors.front();
  }
  if (model.secondary_color.empty() && model.palette_colors.size() > 1u) {
    model.secondary_color = model.palette_colors[1];
  }

  base::JSONWriter::Write(base::Value(theme_dict->Clone()), &model.theme_json);
  return model;
}

std::string GetMahoSpaceDisplayPrimaryColor(
    const MahoSpaceDisplayThemeModel& theme,
    const std::string& fallback_color) {
  return theme.primary_color.empty() ? fallback_color : theme.primary_color;
}

std::string GetMahoSpaceDisplaySecondaryColor(
    const MahoSpaceDisplayThemeModel& theme,
    const std::string& fallback_color) {
  if (!theme.secondary_color.empty()) {
    return theme.secondary_color;
  }
  if (!theme.palette_colors.empty()) {
    return theme.palette_colors.back();
  }
  return GetMahoSpaceDisplayPrimaryColor(theme, fallback_color);
}

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
