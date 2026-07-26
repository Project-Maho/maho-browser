// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_MAHO_MINI_MAHO_MINI_TOP_BAR_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_MAHO_MINI_MAHO_MINI_TOP_BAR_VIEW_H_

#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "content/public/browser/web_contents_observer.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget_observer.h"

class Browser;
class BrowserView;

namespace views {
class Label;
class ImageButton;
class ImageView;
class Textfield;
class Widget;
}  // namespace views

namespace maho {

class MahoMiniPromoteButton;

class MahoMiniTopBarView : public views::View,
                           public views::TextfieldController,
                           public views::WidgetObserver,
                           public TabStripModelObserver,
                           public content::WebContentsObserver,
                           public MahoSpaceProfileBridge::Observer {
  METADATA_HEADER(MahoMiniTopBarView, views::View)

 public:
  MahoMiniTopBarView(Browser* browser, BrowserView* browser_view);
  MahoMiniTopBarView(const MahoMiniTopBarView&) = delete;
  MahoMiniTopBarView& operator=(const MahoMiniTopBarView&) = delete;
  ~MahoMiniTopBarView() override;

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override;
  void AddedToWidget() override;
  void RemovedFromWidget() override;
  bool AcceleratorPressed(const ui::Accelerator& accelerator) override;
  void Layout(PassKey) override;
  void OnThemeChanged() override;

  // Returns true if `point` (in this view's coordinates) is not over any
  // interactive control (address field, copy button, promote button) and should
  // therefore be treated as window-draggable caption area. Consulted by
  // BrowserView::NonClientHitTest so clicks on the controls reach them instead
  // of being swallowed by the macOS titlebar drag region.
  bool IsPositionInWindowCaption(const gfx::Point& point) const;

  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& key_event) override;

  void OnWidgetBoundsChanged(views::Widget* widget,
                             const gfx::Rect& new_bounds) override;
  void OnWidgetVisibilityChanged(views::Widget* widget, bool visible) override;
  void OnWidgetDestroying(views::Widget* widget) override;

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;

  // content::WebContentsObserver:
  void DidFinishNavigation(
      content::NavigationHandle* navigation_handle) override;

  // MahoSpaceProfileBridge::Observer:
  void OnSpaceProfileBridgeChanged() override;
  void OnSpaceProfileBridgeChanged(bool is_structural) override;

  void PromoteToSpace(const std::string& space_id);
  SkColor space_color() const { return space_color_; }
  Browser* browser() { return browser_; }

 private:
  void UpdateHostname();
  void UpdateSpacePill();
  void OnCopyPressed();
  void AcceptAddressInput();
  void FocusAddressFieldIfBlank();
  SkColor ResolveSpaceColorWithAlpha();

  const raw_ptr<Browser> browser_;
  const raw_ptr<BrowserView> browser_view_;

  raw_ptr<views::View> space_pill_ = nullptr;
  raw_ptr<views::ImageView> space_pill_icon_ = nullptr;
  raw_ptr<views::Label> space_pill_label_ = nullptr;
  raw_ptr<views::Textfield> address_field_ = nullptr;
  raw_ptr<views::ImageButton> copy_button_ = nullptr;
  raw_ptr<MahoMiniPromoteButton> promote_button_ = nullptr;

  gfx::Rect integrated_surface_bounds_;

  SkColor space_color_ = SkColorSetARGB(40, 128, 128, 128);
  gfx::Size last_widget_size_;
  bool can_persist_widget_size_ = false;

  base::ScopedObservation<MahoSpaceProfileBridge,
                          MahoSpaceProfileBridge::Observer>
      space_observation_{this};
  base::ScopedObservation<views::Widget, views::WidgetObserver>
      widget_observation_{this};

  // TabStripModel is observed via TabStripModel::AddObserver(this) rather than
  // base::ScopedObservation: that specialization is deleted in
  // tab_strip_model.h, since TabStripModelObserver unregisters itself natively.
  base::WeakPtrFactory<MahoMiniTopBarView> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_MAHO_MINI_MAHO_MINI_TOP_BAR_VIEW_H_
