// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_UPDATE_NOTIFICATION_MODEL_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_UPDATE_NOTIFICATION_MODEL_H_

#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "ui/base/models/image_model.h"

namespace maho {

struct MahoSidebarUpdateActionModel {
  MahoSidebarUpdateActionModel();
  MahoSidebarUpdateActionModel(const MahoSidebarUpdateActionModel&);
  MahoSidebarUpdateActionModel(MahoSidebarUpdateActionModel&&);
  MahoSidebarUpdateActionModel& operator=(const MahoSidebarUpdateActionModel&);
  MahoSidebarUpdateActionModel& operator=(MahoSidebarUpdateActionModel&&);
  ~MahoSidebarUpdateActionModel();

  std::u16string label;
  std::u16string accessible_label;
  ui::ImageModel icon;
  bool special = false;
  base::RepeatingClosure on_activate;
};

struct MahoSidebarUpdateCheckboxModel {
  MahoSidebarUpdateCheckboxModel();
  MahoSidebarUpdateCheckboxModel(const MahoSidebarUpdateCheckboxModel&);
  MahoSidebarUpdateCheckboxModel(MahoSidebarUpdateCheckboxModel&&);
  MahoSidebarUpdateCheckboxModel& operator=(
      const MahoSidebarUpdateCheckboxModel&);
  MahoSidebarUpdateCheckboxModel& operator=(MahoSidebarUpdateCheckboxModel&&);
  ~MahoSidebarUpdateCheckboxModel();

  std::u16string label;
  std::u16string accessible_label;
  bool checked = false;
  base::RepeatingCallback<void(bool)> on_toggle;
};

struct MahoSidebarUpdateNotificationModel {
  MahoSidebarUpdateNotificationModel();
  MahoSidebarUpdateNotificationModel(const MahoSidebarUpdateNotificationModel&);
  MahoSidebarUpdateNotificationModel(MahoSidebarUpdateNotificationModel&&);
  MahoSidebarUpdateNotificationModel& operator=(
      const MahoSidebarUpdateNotificationModel&);
  MahoSidebarUpdateNotificationModel& operator=(
      MahoSidebarUpdateNotificationModel&&);
  ~MahoSidebarUpdateNotificationModel();

  bool visible = false;
  std::u16string heading;
  std::u16string accessible_heading;
  std::optional<MahoSidebarUpdateCheckboxModel> checkbox;
  std::vector<MahoSidebarUpdateActionModel> actions;
  base::RepeatingClosure on_dismiss;
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_UPDATE_NOTIFICATION_MODEL_H_
