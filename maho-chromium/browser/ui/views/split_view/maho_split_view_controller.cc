// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/split_view/maho_split_view_controller.h"

#include <memory>

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/multi_contents_view.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "url/gurl.h"

MahoSplitViewController::MahoSplitViewController(Browser* browser)
    : browser_(browser) {}

MahoSplitViewController::~MahoSplitViewController() {
  if (IsSplitActive()) {
    RemoveSplit();
  }
}

void MahoSplitViewController::AddSplit() {
  AddSplitWithURL(GURL("about:blank"));
}

void MahoSplitViewController::AddSplitWithURL(const GURL& url) {
  if (IsSplitActive()) {
    return;
  }

  BrowserView* browser_view =
      BrowserView::GetBrowserViewForBrowser(browser_);
  if (!browser_view) {
    return;
  }

  MultiContentsView* multi_contents_view =
      browser_view->multi_contents_view();
  if (!multi_contents_view) {
    return;
  }

  secondary_web_contents_ = CreateSecondaryWebContents();
  if (!secondary_web_contents_) {
    return;
  }

  content::NavigationController::LoadURLParams params(url);
  params.transition_type = ui::PAGE_TRANSITION_GENERATED;
  secondary_web_contents_->GetController().LoadURLWithParams(params);

  multi_contents_view->ShowSplitView(0.5);
  multi_contents_view->SetWebContentsAtIndex(
      secondary_web_contents_.get(), 1);
  multi_contents_view->SetActiveIndex(0);
}

void MahoSplitViewController::RemoveSplit() {
  BrowserView* browser_view =
      BrowserView::GetBrowserViewForBrowser(browser_);
  if (!browser_view) {
    return;
  }

  MultiContentsView* multi_contents_view =
      browser_view->multi_contents_view();
  if (!multi_contents_view) {
    return;
  }

  multi_contents_view->SetWebContentsAtIndex(nullptr, 1);
  multi_contents_view->CloseSplitView();
  secondary_web_contents_.reset();
}

bool MahoSplitViewController::IsSplitActive() const {
  BrowserView* browser_view =
      BrowserView::GetBrowserViewForBrowser(browser_);
  if (!browser_view) {
    return false;
  }

  MultiContentsView* multi_contents_view =
      browser_view->multi_contents_view();
  if (!multi_contents_view) {
    return false;
  }

  return multi_contents_view->IsInSplitView();
}

std::unique_ptr<content::WebContents>
MahoSplitViewController::CreateSecondaryWebContents() {
  content::WebContents* primary =
      browser_->tab_strip_model()->GetActiveWebContents();
  if (!primary) {
    return nullptr;
  }

  content::WebContents::CreateParams create_params(browser_->profile());
  return content::WebContents::Create(create_params);
}
