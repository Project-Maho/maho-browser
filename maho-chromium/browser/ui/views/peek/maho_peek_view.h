// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_PEEK_MAHO_PEEK_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_PEEK_MAHO_PEEK_VIEW_H_

#include <memory>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/observer_list.h"
#include "build/build_config.h"
#include "content/public/browser/keyboard_event_processing_result.h"
#include "content/public/browser/web_contents_delegate.h"
#include "content/public/browser/web_contents_observer.h"
#include "components/web_modal/modal_dialog_host.h"
#include "components/web_modal/web_contents_modal_dialog_host.h"
#include "components/web_modal/web_contents_modal_dialog_manager_delegate.h"
#include "printing/buildflags/buildflags.h"
#include "ui/views/controls/webview/webview.h"

class Browser;
class GURL;
class Profile;

namespace views {
class MdTextButton;
}

// Hosts a transient Peek WebContents. Ownership deliberately remains here
// rather than in views::WebView so ReleaseContents() can transfer the live page
// into a tab without destroying it.
class MahoPeekView : public views::WebView,
                      public web_modal::WebContentsModalDialogManagerDelegate,
                      public web_modal::WebContentsModalDialogHost {
 public:
  using ControllerCallback = base::RepeatingClosure;

  MahoPeekView(Browser* browser,
               ControllerCallback open_as_tab_callback,
               ControllerCallback close_callback);
  MahoPeekView(const MahoPeekView&) = delete;
  MahoPeekView& operator=(const MahoPeekView&) = delete;
  ~MahoPeekView() override;

  content::WebContents* CreateAndLoad(Profile* profile, const GURL& url);
  content::WebContents* AdoptContents(
      std::unique_ptr<content::WebContents> contents);
  std::unique_ptr<content::WebContents> ReleaseContents();

  content::WebContents* hosted_contents() const { return owned_contents_.get(); }
  views::MdTextButton* open_as_tab_button_for_testing() const {
    return open_as_tab_button_;
  }

  // content::WebContentsDelegate:
  content::WebContents* AddNewContents(
      content::WebContents* source,
      std::unique_ptr<content::WebContents> new_contents,
      const GURL& target_url,
      WindowOpenDisposition disposition,
      const blink::mojom::WindowFeatures& window_features,
      bool user_gesture,
      bool* was_blocked) override;
  void CloseContents(content::WebContents* source) override;
  content::KeyboardEventProcessingResult PreHandleKeyboardEvent(
      content::WebContents* source,
      const input::NativeWebKeyboardEvent& event) override;
  void SetContentsBounds(content::WebContents* source,
                         const gfx::Rect& bounds) override;
  void CanDownload(const GURL& url,
                   const std::string& request_method,
                   base::OnceCallback<void(bool)> callback) override;
  content::JavaScriptDialogManager* GetJavaScriptDialogManager(
      content::WebContents* source) override;
  void RunFileChooser(content::RenderFrameHost* render_frame_host,
                      scoped_refptr<content::FileSelectListener> listener,
                      const blink::mojom::FileChooserParams& params) override;
  bool CanEnterFullscreenModeForTab(
      content::RenderFrameHost* requesting_frame) override;
  void EnterFullscreenModeForTab(
      content::RenderFrameHost* requesting_frame,
      const blink::mojom::FullscreenOptions& options) override;
  void RequestPointerLock(content::WebContents* web_contents,
                          bool user_gesture,
                          bool last_unlocked_by_target) override;
  bool AllowKeyboardLockForInnerContents(
      content::WebContents* web_contents) override;
  void RequestKeyboardLock(content::WebContents* web_contents,
                           bool esc_key_locked) override;
  void RequestMediaAccessPermission(
      content::WebContents* web_contents,
      const content::MediaStreamRequest& request,
      content::MediaResponseCallback callback) override;
#if BUILDFLAG(ENABLE_PRINTING)
  void PrintCrossProcessSubframe(
      content::WebContents* web_contents,
      const gfx::Rect& rect,
      int document_cookie,
      content::RenderFrameHost* subframe_host) const override;
#endif

  // web_modal::WebContentsModalDialogManagerDelegate:
  // Peek contents live in this view instead of a Browser window, so the modal
  // dialog manager needs this view to serve as the dialog host. The base
  // implementation returns nullptr, and constrained_window's
  // CreateWebModalDialogViews() CHECKs the host, so any web-modal dialog raised
  // by peeked content (WebAuthn passkey/caBLE, permission prompts) would kill
  // the browser. Mirrors chrome's SimpleWebViewDialog, which hosts its own
  // dialogs the same way.
  web_modal::WebContentsModalDialogHost* GetWebContentsModalDialogHost(
      content::WebContents* web_contents) override;

  // web_modal::WebContentsModalDialogHost:
  gfx::NativeView GetHostView() const override;
  gfx::Point GetDialogPosition(const gfx::Size& size) override;
  gfx::Size GetMaximumDialogSize() override;
  void AddObserver(web_modal::ModalDialogHostObserver* observer) override;
  void RemoveObserver(web_modal::ModalDialogHostObserver* observer) override;

  // views::View:
  void Layout(PassKey) override;

 private:
  content::WebContentsDelegate* BrowserDelegate() const;
  void AttachContents(std::unique_ptr<content::WebContents> contents);
  void DetachContents();

  const raw_ptr<Browser> browser_;
  ControllerCallback open_as_tab_callback_;
  ControllerCallback close_callback_;
  raw_ptr<views::MdTextButton> open_as_tab_button_ = nullptr;
  std::unique_ptr<content::WebContents> owned_contents_;
  base::ObserverList<web_modal::ModalDialogHostObserver> observers_;
};

#endif  // MAHO_BROWSER_UI_VIEWS_PEEK_MAHO_PEEK_VIEW_H_
