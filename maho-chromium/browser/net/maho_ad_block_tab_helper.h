// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_NET_MAHO_AD_BLOCK_TAB_HELPER_H_
#define MAHO_BROWSER_NET_MAHO_AD_BLOCK_TAB_HELPER_H_

#include "base/memory/weak_ptr.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"

class MahoAdBlockTabHelper
    : public content::WebContentsObserver,
      public content::WebContentsUserData<MahoAdBlockTabHelper> {
 public:
  ~MahoAdBlockTabHelper() override;

  MahoAdBlockTabHelper(const MahoAdBlockTabHelper&) = delete;
  MahoAdBlockTabHelper& operator=(const MahoAdBlockTabHelper&) = delete;

  uint32_t blocked_count() const { return blocked_count_; }
  void IncrementBlockedCount();
  base::WeakPtr<MahoAdBlockTabHelper> GetWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

  // content::WebContentsObserver:
  void PrimaryPageChanged(content::Page& page) override;

 private:
  friend class content::WebContentsUserData<MahoAdBlockTabHelper>;
  explicit MahoAdBlockTabHelper(content::WebContents* web_contents);

  uint32_t blocked_count_ = 0;
  base::WeakPtrFactory<MahoAdBlockTabHelper> weak_factory_{this};

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

#endif  // MAHO_BROWSER_NET_MAHO_AD_BLOCK_TAB_HELPER_H_
