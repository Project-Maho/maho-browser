// Copyright 2026 Maho Browser. All rights reserved.

#include "components/password_manager/core/browser/password_store/password_store_interface.h"
#include "components/password_manager/core/browser/password_form.h"
#define ShowBubble                                                             \
  ShowBubble(content::WebContents *web_contents, DisplayReason reason);        \
  static void ShowBubble_ChromiumImpl
#include "maho/chromium_src/chrome/browser/ui/views/passwords/password_bubble_view_base.h"
#undef ShowBubble

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/actions/chrome_action_id.h"
#include "chrome/browser/ui/browser_actions.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/passwords/passwords_model_delegate.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "content/public/browser/web_contents.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/browser/ui/views/passwords/maho_password_save_update_view.h"

namespace views {

// Renaming the upstream factory also reaches the unrelated widget-creation
// call in its ShowBubble() body. Keep that call unchanged through a narrow
// passthrough whose method already carries the renamed token.
class BubbleDialogDelegateViewChromiumPassthrough {
public:
  static Widget *CreateBubble_ChromiumImpl(BubbleDialogDelegateView *bubble) {
    return BubbleDialogDelegateView::CreateBubble(bubble);
  }
};

} // namespace views

#define CreateBubble CreateBubble_ChromiumImpl
#define ShowBubble ShowBubble_ChromiumImpl
#define BubbleDialogDelegateView BubbleDialogDelegateViewChromiumPassthrough
#include "../src/chrome/browser/ui/views/passwords/password_bubble_view_base.cc"
#undef BubbleDialogDelegateView
#undef ShowBubble
#undef CreateBubble

namespace {

bool ShouldUseMahoPasswordSaveUpdateView(content::WebContents *web_contents) {
  if (!web_contents) {
    return false;
  }

  Profile *profile =
      Profile::FromBrowserContext(web_contents->GetBrowserContext());
  if (!profile || !maho::IsPasswordManagerAllowedForProfile(profile)) {
    return false;
  }

  const std::string profile_key =
      maho::MahoSpaceProfileBridge::GetInstance()->GetSpaceIdForProfile(
          profile);
  return maho::passwords::ShouldUseMahoPasswordSaveUpdateView(
      maho::passwords::GetEffectivePasswordProviderForProfileKey(profile_key),
      maho::passwords::GetNativePasswordWritePrefs());
}

} // namespace

// static
void PasswordBubbleViewBase::ShowBubble(content::WebContents *web_contents,
                                        DisplayReason reason) {
  BrowserWindowInterface *browser =
      GlobalBrowserCollection::GetInstance()->FindBrowserWithTab(web_contents);
  if (!browser) {
    return;
  }
  DCHECK(browser->GetWindow());
  DCHECK(!g_manage_passwords_bubble_ ||
         !g_manage_passwords_bubble_->GetWidget()->IsVisible());

  BrowserView *browser_view = BrowserView::GetBrowserViewForBrowser(browser);
  ToolbarButtonProvider *button_provider =
      browser_view->toolbar_button_provider();
  views::BubbleAnchor anchor =
      button_provider->GetBubbleAnchor(kActionShowPasswordsBubbleOrPage);

  // This public call is intentionally outside the macro-renamed upstream
  // implementation so production ShowBubble() dispatches through Maho's gate.
  // CreateBubble() returning null is a legitimate suppression path (the
  // upstream Chromium save/update prompt must never surface in Maho), so a
  // DCHECK on the bubble pointer would crash debug builds by design.
  PasswordBubbleViewBase *bubble = CreateBubble(web_contents, anchor, reason);
  DCHECK_EQ(bubble, g_manage_passwords_bubble_);
  if (!g_manage_passwords_bubble_) {
    return;
  }

  if (!bubble_anchor_util::IsHighlightable(anchor)) {
    g_manage_passwords_bubble_->SetHighlightedElement(
        kPasswordsOmniboxKeyIconElementId);
  }

  views::BubbleDialogDelegateView::CreateBubble(g_manage_passwords_bubble_);

  g_manage_passwords_bubble_->ShowForReason(reason);
  g_manage_passwords_bubble_->RegisterWindowClosingCallback(base::BindOnce(
      [](PasswordBubbleViewBase *closing_bubble) {
        if (closing_bubble == g_manage_passwords_bubble_) {
          g_manage_passwords_bubble_ = nullptr;
        }
      },
      bubble));

  auto *browser_actions = BrowserActions::From(browser);
  if (browser_actions && browser_actions->root_action_item()) {
    auto *passwords_action_item = actions::ActionManager::Get().FindAction(
        kActionShowPasswordsBubbleOrPage,
        browser_actions->root_action_item());
    if (passwords_action_item) {
      bool should_suppress_next_button_trigger =
          g_manage_passwords_bubble_->ShouldCloseOnDeactivate();
      passwords_action_item->SetIsShowingBubble(
          should_suppress_next_button_trigger);
    }
  }
}

// static
PasswordBubbleViewBase *
PasswordBubbleViewBase::CreateBubble(content::WebContents *web_contents,
                                     views::BubbleAnchor anchor_view,
                                     DisplayReason reason) {
  base::WeakPtr<PasswordsModelDelegate> delegate =
      PasswordsModelDelegateFromWebContents(web_contents);
  const password_manager::ui::State model_state = delegate->GetState();
  if (model_state == password_manager::ui::PENDING_PASSWORD_UPDATE_STATE ||
      model_state == password_manager::ui::PENDING_PASSWORD_STATE) {
    // Maho never surfaces the upstream save/update prompt. When the effective
    // provider is Maho Native the Maho bubble owns the save; under an external
    // provider (Bitwarden/1Password) the extension's own UI owns it, and under
    // a disabled provider nothing is saved at all — in both latter cases no
    // browser prompt may appear.
    if (!ShouldUseMahoPasswordSaveUpdateView(web_contents)) {
      g_manage_passwords_bubble_ = nullptr;
      return nullptr;
    }
    g_manage_passwords_bubble_ =
        new maho::passwords::MahoPasswordSaveUpdateView(web_contents,
                                                        anchor_view, reason);
    return g_manage_passwords_bubble_;
  }

  return CreateBubble_ChromiumImpl(web_contents, anchor_view, reason);
}

#if defined(UNIT_TEST)
// static
void PasswordBubbleViewBase::DestroyManagePasswordsBubbleForTesting() {
  PasswordBubbleViewBase *bubble = g_manage_passwords_bubble_;
  g_manage_passwords_bubble_ = nullptr;
  delete bubble;
}
#endif
