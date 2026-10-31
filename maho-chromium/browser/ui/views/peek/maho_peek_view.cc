// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/peek/maho_peek_view.h"

#include <algorithm>
#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"  // nogncheck
#include "chrome/browser/ui/browser_web_contents_delegate/browser_web_contents_delegate.h"
#include "components/input/native_web_keyboard_event.h"
#include "components/web_modal/web_contents_modal_dialog_manager.h"
#include "content/public/browser/file_select_listener.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "third_party/blink/public/mojom/choosers/file_chooser.mojom.h"
#include "third_party/blink/public/mojom/input/pointer_lock_result.mojom.h"
#include "third_party/blink/public/common/input/web_input_event.h"
#include "ui/base/page_transition_types.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/widget/widget.h"

MahoPeekView::MahoPeekView(Browser* browser,
                           ControllerCallback open_as_tab_callback,
                           ControllerCallback close_callback)
    : views::WebView(browser ? browser->GetProfile() : nullptr),
      browser_(browser),
      open_as_tab_callback_(std::move(open_as_tab_callback)),
      close_callback_(std::move(close_callback)) {
  CHECK(browser_);

  open_as_tab_button_ = AddChildView(std::make_unique<views::MdTextButton>(
      base::BindRepeating(
          [](MahoPeekView* view) {
            if (view->open_as_tab_callback_) {
              view->open_as_tab_callback_.Run();
            }
          },
          base::Unretained(this))));
  open_as_tab_button_->SetText(u"Open as tab");
  open_as_tab_button_->SetAccessibleName(u"Open in a new tab");
}

MahoPeekView::~MahoPeekView() {
  DetachContents();
}

content::WebContents* MahoPeekView::CreateAndLoad(Profile* profile,
                                                  const GURL& url) {
  CHECK(profile);
  CHECK_EQ(profile, browser_->GetProfile());

  content::WebContents::CreateParams create_params(profile);
  auto contents = content::WebContents::Create(create_params);
  content::WebContents* contents_ptr = contents.get();
  AttachContents(std::move(contents));

  content::NavigationController::LoadURLParams load_params(url);
  load_params.transition_type = ui::PAGE_TRANSITION_GENERATED;
  contents_ptr->GetController().LoadURLWithParams(load_params);
  return contents_ptr;
}

content::WebContents* MahoPeekView::AdoptContents(
    std::unique_ptr<content::WebContents> contents) {
  // Characterization contract: AdoptContents(nullptr) must CHECK-fail. A
  // runtime death test belongs with the Peek interactive tests in Todo 5.
  CHECK(contents);
  content::WebContents* contents_ptr = contents.get();
  AttachContents(std::move(contents));
  return contents_ptr;
}

std::unique_ptr<content::WebContents> MahoPeekView::ReleaseContents() {
  DetachContents();
  return std::move(owned_contents_);
}

content::WebContents* MahoPeekView::AddNewContents(
    content::WebContents* source,
    std::unique_ptr<content::WebContents> new_contents,
    const GURL& target_url,
    WindowOpenDisposition disposition,
    const blink::mojom::WindowFeatures& window_features,
    bool user_gesture,
    bool* was_blocked) {
  return BrowserDelegate()->AddNewContents(
      source, std::move(new_contents), target_url,
      WindowOpenDisposition::NEW_FOREGROUND_TAB, window_features, user_gesture,
      was_blocked);
}

void MahoPeekView::CloseContents(content::WebContents* source) {
  if (close_callback_) {
    close_callback_.Run();
  }
}

content::KeyboardEventProcessingResult MahoPeekView::PreHandleKeyboardEvent(
    content::WebContents* source,
    const input::NativeWebKeyboardEvent& event) {
  if (event.GetType() == blink::WebInputEvent::Type::kRawKeyDown &&
      event.windows_key_code == ui::VKEY_ESCAPE && close_callback_) {
    close_callback_.Run();
    return content::KeyboardEventProcessingResult::HANDLED;
  }
  return content::KeyboardEventProcessingResult::NOT_HANDLED;
}

void MahoPeekView::SetContentsBounds(content::WebContents* source,
                                     const gfx::Rect& bounds) {
  // Peek is a fixed card; renderer-requested window move/resize is ignored.
}

void MahoPeekView::CanDownload(
    const GURL& url,
    const std::string& request_method,
    base::OnceCallback<void(bool)> callback) {
  content::WebContentsDelegate::CanDownload(url, request_method,
                                            std::move(callback));
}

content::JavaScriptDialogManager*
MahoPeekView::GetJavaScriptDialogManager(content::WebContents* source) {
  return BrowserDelegate()->GetJavaScriptDialogManager(source);
}

void MahoPeekView::RunFileChooser(
    content::RenderFrameHost* render_frame_host,
    scoped_refptr<content::FileSelectListener> listener,
    const blink::mojom::FileChooserParams& params) {
  BrowserDelegate()->RunFileChooser(render_frame_host, std::move(listener),
                                    params);
}

bool MahoPeekView::CanEnterFullscreenModeForTab(
    content::RenderFrameHost* requesting_frame) {
  return false;
}

void MahoPeekView::EnterFullscreenModeForTab(
    content::RenderFrameHost* requesting_frame,
    const blink::mojom::FullscreenOptions& options) {
  // Peek never enters content fullscreen.
}

void MahoPeekView::RequestPointerLock(content::WebContents* web_contents,
                                      bool user_gesture,
                                      bool last_unlocked_by_target) {
  web_contents->GotResponseToPointerLockRequest(
      blink::mojom::PointerLockResult::kPermissionDenied);
}

bool MahoPeekView::AllowKeyboardLockForInnerContents(
    content::WebContents* web_contents) {
  return false;
}

void MahoPeekView::RequestKeyboardLock(content::WebContents* web_contents,
                                      bool esc_key_locked) {
  web_contents->GotResponseToKeyboardLockRequest(false);
}

void MahoPeekView::RequestMediaAccessPermission(
    content::WebContents* web_contents,
    const content::MediaStreamRequest& request,
    content::MediaResponseCallback callback) {
  BrowserDelegate()->RequestMediaAccessPermission(web_contents, request,
                                                  std::move(callback));
}

#if BUILDFLAG(ENABLE_PRINTING)
void MahoPeekView::PrintCrossProcessSubframe(
    content::WebContents* web_contents,
    const gfx::Rect& rect,
    int document_cookie,
    content::RenderFrameHost* subframe_host) const {
  BrowserDelegate()->PrintCrossProcessSubframe(web_contents, rect,
                                               document_cookie, subframe_host);
}
#endif

void MahoPeekView::Layout(PassKey pass_key) {
  LayoutSuperclass<views::WebView>(this);
  if (!open_as_tab_button_) {
    return;
  }

  const gfx::Size button_size = open_as_tab_button_->GetPreferredSize();
  open_as_tab_button_->SetBounds(
      std::max(0, width() - button_size.width() - 12), 12,
      button_size.width(), button_size.height());
}

content::WebContentsDelegate* MahoPeekView::BrowserDelegate() const {
  return BrowserWebContentsDelegate::From(browser_.get());
}

web_modal::WebContentsModalDialogHost*
MahoPeekView::GetWebContentsModalDialogHost(
    content::WebContents* web_contents) {
  // Host peek dialogs in this view. Returning nullptr here (the base default)
  // makes constrained_window::CreateWebModalDialogViews() fail its
  // CHECK(dialog_host), which crashes the browser whenever peeked content opens
  // a web-modal dialog - notably the WebAuthn passkey dialog (including the
  // caBLE cross-device QR) raised while Google sign-in runs inside peek.
  return this;
}

gfx::NativeView MahoPeekView::GetHostView() const {
  return GetWidget() ? GetWidget()->GetNativeView() : gfx::NativeView();
}

gfx::Point MahoPeekView::GetDialogPosition(const gfx::Size& size) {
  const gfx::Size host_size = this->size();
  return gfx::Point(host_size.width() / 2 - size.width() / 2,
                    host_size.height() / 2 - size.height() / 2);
}

gfx::Size MahoPeekView::GetMaximumDialogSize() {
  return size();
}

void MahoPeekView::AddObserver(
    web_modal::ModalDialogHostObserver* observer) {
  observers_.AddObserver(observer);
}

void MahoPeekView::RemoveObserver(
    web_modal::ModalDialogHostObserver* observer) {
  observers_.RemoveObserver(observer);
}

void MahoPeekView::AttachContents(
    std::unique_ptr<content::WebContents> contents) {
  CHECK(contents);
  CHECK(!owned_contents_);
  CHECK_EQ(contents->GetBrowserContext(), browser_->GetProfile());

  owned_contents_ = std::move(contents);
  owned_contents_->SetDelegate(this);
  web_modal::WebContentsModalDialogManager::CreateForWebContents(
      owned_contents_.get());
  web_modal::WebContentsModalDialogManager::FromWebContents(
      owned_contents_.get())
      ->SetDelegate(this);
  SetWebContents(owned_contents_.get());
}

void MahoPeekView::DetachContents() {
  if (!owned_contents_) {
    return;
  }

  owned_contents_->SetDelegate(nullptr);
  web_modal::WebContentsModalDialogManager::FromWebContents(
      owned_contents_.get())
      ->SetDelegate(nullptr);
  SetWebContents(nullptr);
}
