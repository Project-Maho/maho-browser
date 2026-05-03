// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_CONTAINER_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_CONTAINER_VIEW_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "components/prefs/pref_change_registrar.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"

class Browser;

namespace maho {

class MahoCommandOverlayController;

class MahoSidebarContainerView : public views::View {
  METADATA_HEADER(MahoSidebarContainerView, views::View)

 public:
  DECLARE_CLASS_ELEMENT_IDENTIFIER_VALUE(kMahoSidebarContainerElementId);

  explicit MahoSidebarContainerView(Browser* browser);
  ~MahoSidebarContainerView() override;

  MahoSidebarContainerView(const MahoSidebarContainerView&) = delete;
  MahoSidebarContainerView& operator=(const MahoSidebarContainerView&) = delete;

  views::View* sidebar_view() { return sidebar_view_; }
  MahoCommandOverlayController* command_overlay_controller() {
    return command_overlay_controller_.get();
  }

  void ShowCreateSpace();
  void ToggleCommandOverlay();

 private:
  void OnSidebarVisibilityChanged();
  void OnSidebarWidthChanged();
  void ApplyPreferredWidthFromPrefs();
  void MaybePersistSidebarWidth(int width);
  void OnBoundsChanged(const gfx::Rect& previous_bounds) override;

  raw_ptr<Browser> browser_;
  raw_ptr<views::View> sidebar_view_ = nullptr;
  std::unique_ptr<MahoCommandOverlayController> command_overlay_controller_;
  PrefChangeRegistrar pref_registrar_;
  base::WeakPtrFactory<MahoSidebarContainerView> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_CONTAINER_VIEW_H_
