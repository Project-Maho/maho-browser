// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/maho_tab_preview_capture.h"

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/utf_string_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/functional/callback_helpers.h"
#include "base/logging.h"
#include "base/task/thread_pool.h"
#include "base/timer/elapsed_timer.h"
#include "base/trace_event/trace_event.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/common/isolated_world_ids.h"
#include "components/viz/common/frame_sinks/copy_output_result.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/gfx/codec/jpeg_codec.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/views/view_observer.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "maho/browser/maho_tab_id_helper.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/maho_private_context_policy.h"
#include "chrome/browser/profiles/profile.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_state_adapter.h"
#include "maho/third_party/maho/maho_bridge.h"
#include "maho/third_party/maho/maho_ffi.h"

namespace maho {

namespace {
MahoTabPreviewCapture* g_instance = nullptr;
}  // namespace

class MahoTabWakeOverlay : public views::View, public views::ViewObserver {
  METADATA_HEADER(MahoTabWakeOverlay, views::View)
 public:
  MahoTabWakeOverlay(const SkBitmap& bitmap, views::View* parent)
      : bitmap_(bitmap), parent_(parent) {
    if (parent_) {
      parent_->AddObserver(this);
      SetBoundsRect(gfx::Rect(parent_->bounds().size()));
    }
  }

  ~MahoTabWakeOverlay() override {
    if (parent_) {
      parent_->RemoveObserver(this);
    }
  }

  // views::ViewObserver:
  void OnViewBoundsChanged(views::View* observed_view) override {
    if (observed_view == parent_) {
      SetBoundsRect(gfx::Rect(parent_->bounds().size()));
    }
  }

  void OnPaint(gfx::Canvas* canvas) override {
    if (!bitmap_.isNull()) {
      canvas->DrawImageInt(gfx::ImageSkia::CreateFrom1xBitmap(bitmap_),
                           0, 0, bitmap_.width(), bitmap_.height(),
                           0, 0, width(), height(),
                           true);
    } else {
      canvas->FillRect(GetLocalBounds(), SK_ColorWHITE);
    }
  }

 private:
  SkBitmap bitmap_;
  raw_ptr<views::View> parent_ = nullptr;
};

BEGIN_METADATA(MahoTabWakeOverlay)
END_METADATA

// static
MahoTabPreviewCapture* MahoTabPreviewCapture::GetInstance() {
  return g_instance;
}

MahoTabPreviewCapture::MahoTabPreviewCapture() {
  g_instance = this;
  auto* collection = GlobalBrowserCollection::GetInstance();
  if (collection) {
    browser_collection_observation_.Observe(collection);
    collection->ForEach([this](BrowserWindowInterface* browser) {
      if (browser->GetTabStripModel()) {
        browser->GetTabStripModel()->AddObserver(this);
      }
      return true;
    });
  }
}

MahoTabPreviewCapture::~MahoTabPreviewCapture() {
  auto* collection = GlobalBrowserCollection::GetInstance();
  if (collection) {
    collection->ForEach([this](BrowserWindowInterface* browser) {
      if (browser->GetTabStripModel()) {
        browser->GetTabStripModel()->RemoveObserver(this);
      }
      return true;
    });
  }
  if (g_instance == this) {
    g_instance = nullptr;
  }
}

void MahoTabPreviewCapture::OnBrowserCreated(BrowserWindowInterface* browser) {
  // R-13-b: Never observe OTR tab strips.
  if (!browser) return;
  MahoPrivateContextToken token(browser, nullptr);
  if (!token.Revalidate(MahoPrivateCapability::kPreview)) return;
  if (browser->GetTabStripModel()) {
    browser->GetTabStripModel()->AddObserver(this);
  }
}

void MahoTabPreviewCapture::OnBrowserClosed(BrowserWindowInterface* browser) {
  if (browser->GetTabStripModel()) {
    browser->GetTabStripModel()->RemoveObserver(this);
  }
}

void MahoTabPreviewCapture::OnTabStripModelChanged(
    TabStripModel* model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  if (selection.active_tab_changed() && selection.old_contents) {
    // R-13-b: Skip OTR WebContents.
    MahoPrivateContextToken tsm_token(nullptr, selection.old_contents);
    if (!tsm_token.Revalidate(MahoPrivateCapability::kPreview)) return;
    auto* helper = MahoTabIdHelper::FromWebContents(selection.old_contents);
    if (helper) {
      CaptureViewport(selection.old_contents, helper->stable_tab_id());
      CaptureScrollPosition(selection.old_contents, helper->stable_tab_id());
    }
  }
}

void MahoTabPreviewCapture::CaptureViewport(content::WebContents* web_contents,
                                           const std::string& tab_id) {
  // R-13-b: Deny capture for OTR WebContents.
  MahoPrivateContextToken cap_token(nullptr, web_contents);
  if (!cap_token.Revalidate(MahoPrivateCapability::kPreview)) return;
  content::RenderWidgetHostView* view = web_contents->GetRenderWidgetHostView();
  if (!view || !view->IsSurfaceAvailableForCopy()) {
    return;
  }

  if (!web_contents->GetPrimaryMainFrame() || !web_contents->GetPrimaryMainFrame()->GetView()) {
    return;
  }

  gfx::Size target_size(640, 400);
  gfx::Rect src_rect(view->GetViewBounds().size());

  view->CopyFromSurface(
      src_rect, target_size, base::TimeDelta(),
      base::BindOnce(&MahoTabPreviewCapture::OnCaptureDone,
                     weak_factory_.GetWeakPtr(), tab_id,
                     web_contents->GetWeakPtr()));
}

void MahoTabPreviewCapture::OnCaptureDone(
    const std::string& tab_id,
    base::WeakPtr<content::WebContents> web_contents,
    const content::CopyFromSurfaceResult& result) {
  if (!result.has_value()) {
    return;
  }
  // R-13-b: Revalidate after the async CopyFromSurface hop; a torn-down or
  // reclassified (OTR) WebContents is denied before any core write.
  if (!web_contents) {
    return;
  }
  MahoPrivateContextToken done_token(nullptr, web_contents.get());
  if (!done_token.Revalidate(MahoPrivateCapability::kPreview)) {
    return;
  }
  const SkBitmap& bitmap = result->bitmap;
  if (bitmap.isNull()) {
    return;
  }
  StoreAsJpeg(tab_id, web_contents, bitmap);
}

void MahoTabPreviewCapture::StoreAsJpeg(
    const std::string& tab_id,
    base::WeakPtr<content::WebContents> web_contents,
    const SkBitmap& bitmap) {
  // JPEG encoding is pure CPU work with no UI-thread affinity, but it ran here
  // on every tab switch. Only the core write below has to stay on the UI thread
  // (MahoCore is single-threaded), so just the encode moves off.
  //
  // BEST_EFFORT: a stale preview thumbnail is never worth competing with
  // user-visible work.
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::TaskPriority::BEST_EFFORT},
      base::BindOnce(
          [](const SkBitmap& bitmap) {
            return gfx::JPEGCodec::Encode(bitmap, 70);
          },
          bitmap),
      base::BindOnce(&MahoTabPreviewCapture::OnJpegEncoded,
                     weak_factory_.GetWeakPtr(), tab_id, web_contents));
}

void MahoTabPreviewCapture::OnJpegEncoded(
    const std::string& tab_id,
    base::WeakPtr<content::WebContents> web_contents,
    std::optional<std::vector<uint8_t>> jpeg_data) {
  if (!jpeg_data) {
    return;
  }
  // R-13-b: Revalidate after the thread-pool hop. The WebContents may have been
  // torn down or reclassified OTR while the encode was queued, and a preview
  // must never be written for a context that is no longer eligible.
  if (!web_contents) {
    return;
  }
  MahoPrivateContextToken encoded_token(nullptr, web_contents.get());
  if (!encoded_token.Revalidate(MahoPrivateCapability::kPreview)) {
    return;
  }
  auto* core = maho::GetCore();
  if (!core) {
    return;
  }
  // R-13-b: Desktop capture only reaches here after kPreview revalidation,
  // so the additive privacy ABI is always called with is_private=false;
  // the legacy ABI is never used from desktop capture.
  maho_core_update_tab_preview_with_privacy(
      core, tab_id.c_str(), /*is_private=*/false, jpeg_data->data(),
      jpeg_data->size());
}

void MahoTabPreviewCapture::CaptureScrollPosition(
    content::WebContents* web_contents, const std::string& tab_id) {
  // R-13-b: Deny scroll capture for OTR WebContents.
  MahoPrivateContextToken scroll_token(nullptr, web_contents);
  if (!scroll_token.Revalidate(MahoPrivateCapability::kPreview)) return;
  if (!web_contents->GetPrimaryMainFrame()) {
    return;
  }
  web_contents->GetPrimaryMainFrame()->ExecuteJavaScript(
      u"JSON.stringify({x: window.scrollX, y: window.scrollY})",
      base::BindOnce(&MahoTabPreviewCapture::OnScrollPositionCaptured,
                     weak_factory_.GetWeakPtr(), tab_id,
                     web_contents->GetWeakPtr()));
}

void MahoTabPreviewCapture::OnScrollPositionCaptured(
    const std::string& tab_id,
    base::WeakPtr<content::WebContents> web_contents,
    base::Value result) {
  if (!result.is_string()) {
    return;
  }
  // R-13-b: Revalidate after the async JS hop; a torn-down or reclassified
  // (OTR) WebContents dispatches no scroll event to core.
  if (!web_contents) {
    return;
  }
  MahoPrivateContextToken scroll_done_token(nullptr, web_contents.get());
  if (!scroll_done_token.Revalidate(MahoPrivateCapability::kPreview)) {
    return;
  }
  std::optional<base::Value> parsed =
      base::JSONReader::Read(result.GetString(), base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return;
  }
  const auto& dict = parsed->GetDict();
  double scroll_x = dict.FindDouble("x").value_or(0.0);
  double scroll_y = dict.FindDouble("y").value_or(0.0);

  base::DictValue event_data;
  event_data.Set("tab_id", tab_id);
  event_data.Set("x", scroll_x);
  event_data.Set("y", scroll_y);

  maho::DispatchShellEventDict("update_tab_scroll_position", std::move(event_data));
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(MahoTabWakeHelper);

MahoTabWakeHelper::MahoTabWakeHelper(content::WebContents* web_contents,
                                     const std::string& tab_id,
                                     double scroll_x,
                                     double scroll_y)
    : content::WebContentsObserver(web_contents),
      content::WebContentsUserData<MahoTabWakeHelper>(*web_contents),
      tab_id_(tab_id),
      scroll_x_(scroll_x),
      scroll_y_(scroll_y),
      creation_time_(base::TimeTicks::Now()) {
  // R-13-b: WakeHelper must not restore previews for OTR tabs.
  MahoPrivateContextToken wake_token(nullptr, web_contents);
  if (!wake_token.Revalidate(MahoPrivateCapability::kPreview)) return;
  TRACE_EVENT1("browser", "MahoTabWakeHelper::Create", "tab_id", tab_id);
  ::MahoCore* core = maho::GetCore();
  SkBitmap bitmap;
  if (core) {
    size_t preview_len = 0;
    uint8_t* preview_data = maho_core_get_tab_preview(core, tab_id_.c_str(), &preview_len);
    if (preview_data && preview_len > 0) {
      UNSAFE_BUFFERS(
        bitmap = gfx::JPEGCodec::Decode(base::span<const uint8_t>(preview_data, preview_len));
      );
      maho_core_free_preview_data(preview_data, preview_len);
    }
  }

  Browser* browser = static_cast<Browser*>(
      GlobalBrowserCollection::GetInstance()->FindBrowserWithTab(web_contents));
  if (browser) {
    BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser);
    if (browser_view && browser_view->contents_web_view()) {
      auto overlay = std::make_unique<MahoTabWakeOverlay>(bitmap, browser_view->contents_web_view());
      overlay_ = browser_view->contents_web_view()->AddChildView(std::move(overlay));
    }
  }

  safety_timeout_.Start(
      FROM_HERE, base::Seconds(10),
      base::BindOnce(&MahoTabWakeHelper::RemoveOverlay, base::Unretained(this)));
}

MahoTabWakeHelper::~MahoTabWakeHelper() {
  RemoveOverlay();
}

void MahoTabWakeHelper::DidFinishNavigation(content::NavigationHandle* navigation_handle) {
  if (!navigation_handle->HasCommitted() || navigation_handle->IsErrorPage()) {
    RemoveOverlay();
  }
}

void MahoTabWakeHelper::DidFinishLoad(content::RenderFrameHost* render_frame_host, const GURL& validated_url) {
  content::WebContents* wc = content::WebContentsObserver::web_contents();
  if (wc && wc->GetPrimaryMainFrame() == render_frame_host) {
    const double elapsed_ms =
        (base::TimeTicks::Now() - creation_time_).InMillisecondsF();
    TRACE_EVENT_INSTANT2("browser", "WakeTab.DidFinishLoad",
                         TRACE_EVENT_SCOPE_THREAD,
                         "tab_id", tab_id_,
                         "elapsed_ms", elapsed_ms);
    DVLOG(1) << "[MAHO_PERF] WakeTab.DidFinishLoad tab_id=" << tab_id_
             << " elapsed=" << elapsed_ms << "ms";
    // Only inject JS when the frame is live and active. DidFinishLoad can race
    // with a concurrent navigation so the primary main frame may already be in
    // a non-JS-executable lifecycle state (kSpeculative/kPendingCommit), which
    // CHECK-crashes the browser inside ExecuteJavaScriptInIsolatedWorld via
    // AssertFrameWasCommitted().
    if (render_frame_host->IsRenderFrameLive() &&
        render_frame_host->IsActive()) {
      render_frame_host->ExecuteJavaScriptInIsolatedWorld(
          base::UTF8ToUTF16(base::StringPrintf("window.scrollTo(%f, %f)", scroll_x_, scroll_y_)),
          base::NullCallback(), content::ISOLATED_WORLD_ID_CONTENT_END);
    }
    RemoveOverlay();
  }
}

void MahoTabWakeHelper::DidFirstVisuallyNonEmptyPaint() {
  const double elapsed_ms =
      (base::TimeTicks::Now() - creation_time_).InMillisecondsF();
  TRACE_EVENT_INSTANT2("browser", "WakeTab.FirstPaint",
                       TRACE_EVENT_SCOPE_THREAD,
                       "tab_id", tab_id_,
                       "elapsed_ms", elapsed_ms);
  DVLOG(1) << "[MAHO_PERF] WakeTab.FirstPaint tab_id=" << tab_id_
           << " elapsed=" << elapsed_ms << "ms";
}

void MahoTabWakeHelper::WebContentsDestroyed() {
  // Detach the overlay from the contents view rather than only dropping the
  // pointer: the view is owned by contents_web_view(), which outlives this
  // helper. Clearing overlay_ alone made the destructor's RemoveOverlay() a
  // no-op and left the wake screenshot parented to the live contents view.
  RemoveOverlay();
}

void MahoTabWakeHelper::RemoveOverlay() {
  safety_timeout_.Stop();
  if (overlay_) {
    views::View* parent = overlay_->parent();
    if (parent) {
      parent->RemoveChildViewT(overlay_.ExtractAsDangling());
    } else {
      overlay_ = nullptr;
    }
  }
}

}  // namespace maho
