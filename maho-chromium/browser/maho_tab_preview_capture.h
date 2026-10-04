// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_MAHO_TAB_PREVIEW_CAPTURE_H_
#define MAHO_BROWSER_MAHO_TAB_PREVIEW_CAPTURE_H_

#include "chrome/browser/ui/browser_window/public/browser_collection_observer.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/time/time.h"
#include "base/timer/timer.h"

#include <optional>
#include <string>
#include <vector>

class BrowserWindowInterface;

namespace views {
class View;
}

namespace maho {

class MahoTabPreviewCapture : public BrowserCollectionObserver,
                              public TabStripModelObserver {
 public:
  static MahoTabPreviewCapture* GetInstance();

  MahoTabPreviewCapture();
  ~MahoTabPreviewCapture() override;

  MahoTabPreviewCapture(const MahoTabPreviewCapture&) = delete;
  MahoTabPreviewCapture& operator=(const MahoTabPreviewCapture&) = delete;

  // BrowserCollectionObserver:
  void OnBrowserCreated(BrowserWindowInterface* browser) override;
  void OnBrowserClosed(BrowserWindowInterface* browser) override;

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;

  void CaptureScrollPosition(content::WebContents* web_contents,
                             const std::string& tab_id);

 private:
  void CaptureViewport(content::WebContents* web_contents,
                       const std::string& tab_id);
  // R-13: delayed callbacks carry a weak WebContents pointer and revalidate the
  // kPreview token before touching core/storage so a context that became OTR
  // (or was torn down) between scheduling and completion is denied.
  void OnCaptureDone(const std::string& tab_id,
                     base::WeakPtr<content::WebContents> web_contents,
                     const content::CopyFromSurfaceResult& result);
  // Kicks the JPEG encode onto the thread pool. Encoding a 640x400 bitmap ran
  // on the UI thread on every tab switch; only the core write has to stay here.
  void StoreAsJpeg(const std::string& tab_id,
                   base::WeakPtr<content::WebContents> web_contents,
                   const SkBitmap& bitmap);
  // R-13: revalidates after the thread-pool hop, like every other async path in
  // this file, then writes the encoded preview to core on the UI thread.
  void OnJpegEncoded(const std::string& tab_id,
                     base::WeakPtr<content::WebContents> web_contents,
                     std::optional<std::vector<uint8_t>> jpeg_data);
  void OnScrollPositionCaptured(const std::string& tab_id,
                                base::WeakPtr<content::WebContents> web_contents,
                                base::Value result);

  base::ScopedObservation<GlobalBrowserCollection, BrowserCollectionObserver>
      browser_collection_observation_{this};
  base::WeakPtrFactory<MahoTabPreviewCapture> weak_factory_{this};
};

class MahoTabWakeHelper : public content::WebContentsObserver,
                          public content::WebContentsUserData<MahoTabWakeHelper> {
 public:
  ~MahoTabWakeHelper() override;

  // WebContentsObserver:
  void DidFinishNavigation(content::NavigationHandle* navigation_handle) override;
  void DidFinishLoad(content::RenderFrameHost* render_frame_host, const GURL& validated_url) override;
  void DidFirstVisuallyNonEmptyPaint() override;
  void WebContentsDestroyed() override;

 private:
  friend class content::WebContentsUserData<MahoTabWakeHelper>;
  MahoTabWakeHelper(content::WebContents* web_contents,
                    const std::string& tab_id,
                    double scroll_x,
                    double scroll_y);

  void RemoveOverlay();

  std::string tab_id_;
  double scroll_x_ = 0.0;
  double scroll_y_ = 0.0;
  raw_ptr<views::View> overlay_ = nullptr;
  base::OneShotTimer safety_timeout_;
  base::TimeTicks creation_time_;

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

}  // namespace maho

#endif  // MAHO_BROWSER_MAHO_TAB_PREVIEW_CAPTURE_H_
