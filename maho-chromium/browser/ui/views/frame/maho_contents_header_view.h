// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_FRAME_MAHO_CONTENTS_HEADER_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_FRAME_MAHO_CONTENTS_HEADER_VIEW_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/callback_list.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "components/prefs/pref_change_registrar.h"
#include "content/public/browser/web_contents_observer.h"
#include "maho/browser/ui/theme/maho_theme_helper.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/accessible_pane_view.h"
#include "ui/views/view.h"
#include "ui/views/view_tracker.h"

class Browser;
class BrowserView;
class ContentsWebView;
class FullscreenController;
class GURL;
class Profile;

namespace content {
class WebContents;
}  // namespace content

namespace gfx {
struct VectorIcon;
}  // namespace gfx

namespace ui {
class Event;
}  // namespace ui

namespace views {
class BoxLayout;
class ImageButton;
class ImageView;
class LabelButton;
class MenuRunner;
class WebView;
class Widget;
}  // namespace views

namespace maho {

class MahoLocationBarUtilityBubbleCoordinator;
class MahoLocationBarUtilityIconView;
class MahoSidebarContainerView;

// Snapshot of the translate options menu, for tests.
struct MahoTranslateOptionsForTesting {
  MahoTranslateOptionsForTesting();
  MahoTranslateOptionsForTesting(MahoTranslateOptionsForTesting&&);
  MahoTranslateOptionsForTesting& operator=(MahoTranslateOptionsForTesting&&);
  ~MahoTranslateOptionsForTesting();

  std::vector<std::u16string> labels;
  int first_language_command_id = 0;
  std::string first_language_code;
  int never_translate_command_id = 0;
  int language_settings_command_id = 0;
};

// Height of the per-pane header strip that sits above every contents pane.
inline constexpr int kMahoContentsHeaderHeightDp = 24;

// Per-pane address strip: back / forward / reload, security state, host, the
// translate affordance, the utility panel toggle, and copy URL. One instance
// lives in each ContentsContainerView, so every control acts on that pane's
// WebContents rather than on the browser's active tab.
//
// Inherits views::AccessiblePaneView (rather than plain views::View) so it
// participates in F6 / Ctrl+Back/Forward pane traversal the same way the
// upstream toolbar and mini toolbar do: BrowserView::GetAccessiblePanes()
// (chrome/browser/ui/views/frame/browser_view.cc) collects panes in F6 order
// and WidgetDelegate::RotatePaneFocusFromView() calls SetPaneFocus() on
// whichever one AccessiblePaneView subclass is next, which handles Escape-to-
// restore and arrow-key traversal internally.
class MahoContentsHeaderView : public views::AccessiblePaneView,
                               public content::WebContentsObserver,
                               public ui::SimpleMenuModel::Delegate {
  METADATA_HEADER(MahoContentsHeaderView, views::AccessiblePaneView)

 public:
  MahoContentsHeaderView(BrowserView* browser_view, ContentsWebView* web_view);
  MahoContentsHeaderView(const MahoContentsHeaderView&) = delete;
  MahoContentsHeaderView& operator=(const MahoContentsHeaderView&) = delete;
  ~MahoContentsHeaderView() override;

  // Host text for |url|: the host with a leading "www." removed, or the
  // search placeholder when the URL has no host.
  static std::u16string BuildHostText(const GURL& url);
  // Translate affordance text; empty when neither state applies.
  static std::u16string BuildTranslateText(bool translated, bool available);
  // Only web pages carry a connection-security indicator; internal pages
  // (chrome://, about:, etc.) show none.
  static bool ShouldShowSecurityIcon(const GURL& url);

  // Inactive panes draw secondary text, dimmed glyphs, and a recessed
  // (selected-row tinted) palette surface; the active pane draws the plain
  // palette surface at full contrast.
  void SetActive(bool active);
  bool is_active_for_testing() const { return active_; }

  void SetPalette(const MahoSidebarPalette& palette);

  // Bubble anchors for the active pane. The translate anchor is the translate
  // label while it is visible and drawn, otherwise the utility icon.
  views::View* translate_anchor();
  views::View* utility_anchor();
  views::View* security_anchor();

  // Requests focus on the host button. Used as the sidebar's focus-restore
  // fallback when the sidebar itself cannot take focus (see
  // MahoSidebarView::RestoreLastFocusedBodyView()): the active pane's host
  // button is a keyboard-reachable substitute for the removed search pill.
  void FocusHostButton();

  views::ImageButton* back_button_for_testing() { return back_button_; }
  views::ImageButton* forward_button_for_testing() { return forward_button_; }
  views::ImageButton* reload_button_for_testing() { return reload_button_; }
  views::ImageButton* security_icon_for_testing() { return security_icon_; }
  // The page info bubble opened from the security icon, or null.
  views::Widget* page_info_widget_for_testing() {
    return page_info_widget_.get();
  }
  // Replaces the page info bubble with |opener| (unit tests have no
  // BrowserView to host it). Receives the pane WebContents and the anchor.
  using PageInfoOpenerForTesting =
      base::RepeatingCallback<void(content::WebContents*, views::View*)>;
  void set_page_info_opener_for_testing(PageInfoOpenerForTesting opener) {
    page_info_opener_for_testing_ = std::move(opener);
  }
  // Overrides the split-view state read from MultiContentsView.
  void set_in_split_for_testing(bool in_split) {
    in_split_for_testing_ = in_split;
  }
  views::LabelButton* host_button_for_testing() { return host_button_; }
  views::LabelButton* translate_label_for_testing() { return translate_label_; }
  MahoLocationBarUtilityIconView* utility_icon_for_testing() {
    return utility_icon_;
  }
  views::ImageButton* copy_url_button_for_testing() { return copy_url_button_; }
  views::Widget* translate_popover_widget_for_testing() {
    return translate_popover_widget_.get();
  }
  const MahoSidebarPalette& palette_for_testing() const { return palette_; }
  // Builds the translate options menu models without showing the menu.
  MahoTranslateOptionsForTesting PrepareTranslateOptionsForTesting();

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void OnThemeChanged() override;
  void OnPaintBackground(gfx::Canvas* canvas) override;
  void Layout(PassKey) override;
  void AddedToWidget() override;
  void RemovedFromWidget() override;

  // Fullscreen:
  void OnFullscreenStateChanged();

  // content::WebContentsObserver:
  void DidFinishNavigation(
      content::NavigationHandle* navigation_handle) override;
  void DidStartLoading() override;
  void DidStopLoading() override;
  void DidChangeVisibleSecurityState() override;
  void WebContentsDestroyed() override;

  // ui::SimpleMenuModel::Delegate:
  void ExecuteCommand(int command_id, int event_flags) override;
  bool IsCommandIdChecked(int command_id) const override;

 private:
  Browser* GetBrowser() const;
  // The Maho sidebar container among BrowserView's children, or null when
  // this browser has none (popup / app / devtools windows, unit tests).
  MahoSidebarContainerView* GetSidebarContainer() const;
  Profile* GetProfile() const;
  GURL GetCurrentUrl() const;

  void OnWebContentsAttached(views::WebView* web_view);
  void OnWebContentsDetached(views::WebView* web_view);

  void OnBackPressed(const ui::Event& event);
  void OnForwardPressed(const ui::Event& event);
  void OnReloadPressed(const ui::Event& event);
  void OnHostPressed(const ui::Event& event);
  void OnCopyUrlPressed(const ui::Event& event);
  void OnTranslatePressed(const ui::Event& event);
  // Opens the page info bubble for this pane, anchored to the security icon.
  void OnSecurityPressed(const ui::Event& event);

  // True while the browser shows more than one contents pane.
  bool IsInSplit() const;

  // Activates this pane's tab in the tab strip if it is not already active.
  // When that activation changes the active tab, keyboard focus moves to this
  // pane's web view too (see the .cc for why), so a later focus restore
  // (command overlay dismiss, bubble close) cannot flip the split back.
  void ActivatePaneTab();

  // Translate flow (moved from the sidebar search pill).
  std::u16string ResolveTranslateTargetDisplayName() const;
  void OnTranslatePopoverTranslate();
  void OnTranslatePopoverOptions(views::View* anchor,
                                 base::RepeatingClosure close_popover);
  void BuildTranslateOptionsMenuModels();
  void OnTranslateOptionsMenuClosed();
  void OnTranslateLanguageSelected(const std::string& language_code);
  void OnNeverTranslateThisSite();
  void OnOpenTranslateLanguageSettings();
  void CloseTranslatePopover();

  // Utility panel (moved from the sidebar search pill).
  void ToggleUtilityPanel();
  void ShowUtilityPanel();
  void CloseUtilityPanel();
  void OnUtilityPanelClosed();

  // Subscribes to the sidebar palette once the sidebar container exists.
  void MaybeSubscribeToSidebarPalette();
  // Starts observing fullscreen once the browser's exclusive access manager
  // exists. Returns true when the observation was newly established.
  bool MaybeObserveFullscreen();
  void StopObservingFullscreen();
  // Same predicate as BrowserView::IsMahoArcLayoutActive(): a normal browser
  // window with the sidebar-layout pref on. Sidebar collapse is not an input.
  bool IsArcLayoutActive() const;
  // Applies the Arc-layout and fullscreen visibility policy.
  void UpdateVisibility();
  // Only the active pane's translate label carries
  // kTranslatePageActionElementId, so the translate bubble resolves a unique
  // anchor element when several panes are shown.
  void UpdateTranslateElementId();

  // Recomputes every child from the pane WebContents.
  void UpdateState();
  void UpdateNavigationButtons();
  void UpdateSecurityIcon();
  void UpdateTranslateLabel();
  // Re-binds text and glyph colors from the palette and active state.
  void UpdateChrome();
  void SetButtonIcon(views::ImageButton* button, const gfx::VectorIcon& icon);

  raw_ptr<BrowserView> browser_view_;
  // This pane's ContentsWebView. A sibling in the owning ContentsContainerView
  // that is added (and therefore deleted) before this header, so it is held
  // by a ViewTracker, which clears itself on deletion, instead of a raw_ptr
  // that would dangle for the rest of the container's teardown.
  views::ViewTracker web_view_tracker_;
  raw_ptr<views::BoxLayout> layout_ = nullptr;
  raw_ptr<views::ImageButton> back_button_ = nullptr;
  raw_ptr<views::ImageButton> forward_button_ = nullptr;
  raw_ptr<views::ImageButton> reload_button_ = nullptr;
  raw_ptr<views::ImageButton> security_icon_ = nullptr;
  raw_ptr<views::LabelButton> host_button_ = nullptr;
  raw_ptr<views::LabelButton> translate_label_ = nullptr;
  raw_ptr<MahoLocationBarUtilityIconView> utility_icon_ = nullptr;
  raw_ptr<views::ImageButton> copy_url_button_ = nullptr;

  std::unique_ptr<MahoLocationBarUtilityBubbleCoordinator>
      utility_bubble_coordinator_;
  base::CallbackListSubscription utility_panel_close_subscription_;
  base::CallbackListSubscription web_contents_attached_subscription_;
  base::CallbackListSubscription web_contents_detached_subscription_;

  base::CallbackListSubscription sidebar_palette_subscription_;
  base::CallbackListSubscription fullscreen_subscription_;
  // Observes the sidebar-layout pref so toggling it shows or hides the header.
  PrefChangeRegistrar sidebar_layout_pref_registrar_;
  // FullscreenController is torn down before BrowserView's children, so the
  // observation is released through a weak pointer instead of a
  // ScopedObservation.
  base::WeakPtr<FullscreenController> observed_fullscreen_controller_;

  std::unique_ptr<ui::SimpleMenuModel> translate_options_menu_model_;
  std::unique_ptr<ui::SimpleMenuModel> translate_languages_menu_model_;
  std::unique_ptr<views::MenuRunner> translate_options_menu_runner_;
  std::vector<std::string> translate_option_language_codes_;
  base::RepeatingClosure close_translate_popover_;
  base::WeakPtr<views::Widget> translate_popover_widget_;
  base::WeakPtr<views::Widget> page_info_widget_;
  PageInfoOpenerForTesting page_info_opener_for_testing_;
  std::optional<bool> in_split_for_testing_;

  MahoSidebarPalette palette_;
  bool active_ = true;
  bool is_loading_ = false;
  bool is_translated_ = false;
  bool translation_available_ = false;
  // Tooltip of the current security glyph, empty when hidden.
  std::u16string security_description_;
  raw_ptr<const gfx::VectorIcon> security_vector_icon_ = nullptr;
  bool security_dangerous_ = false;

  base::WeakPtrFactory<MahoContentsHeaderView> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_FRAME_MAHO_CONTENTS_HEADER_VIEW_H_
