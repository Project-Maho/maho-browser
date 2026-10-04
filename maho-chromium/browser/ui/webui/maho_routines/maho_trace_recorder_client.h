// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_CHROMIUM_BROWSER_UI_WEBUI_MAHO_ROUTINES_MAHO_TRACE_RECORDER_CLIENT_H_
#define MAHO_CHROMIUM_BROWSER_UI_WEBUI_MAHO_ROUTINES_MAHO_TRACE_RECORDER_CLIENT_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/singleton.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "third_party/blink/public/common/input/web_input_event.h"

struct MahoTraceRecorder;

namespace maho {

class MahoTraceRecorderClient
    : public content::WebContentsObserver,
      public content::RenderWidgetHost::InputEventObserver {
 public:
  static MahoTraceRecorderClient* GetInstance();

  MahoTraceRecorderClient();
  ~MahoTraceRecorderClient() override;

  MahoTraceRecorderClient(const MahoTraceRecorderClient&) = delete;
  MahoTraceRecorderClient& operator=(const MahoTraceRecorderClient&) = delete;

  bool Start(content::WebContents* contents, int64_t tab_id = 0);
  std::string StopAndExportJson();

  void RecordClick(const std::optional<std::string>& target_ref,
                   double x,
                   double y);
  void RecordFill(const std::string& text,
                  const std::optional<std::string>& selector);
  void RecordNavigate(const std::string& url);
  void RecordHover(const std::optional<std::string>& target_ref);

  bool is_recording() const { return recorder_ != nullptr; }
  int64_t tab_id() const { return tab_id_; }

  void DidFinishNavigation(
      content::NavigationHandle* navigation_handle) override;
  void WebContentsDestroyed() override;

  void OnInputEvent(const content::RenderWidgetHost& host,
                    const blink::WebInputEvent& event,
                    input::InputEventSource source) override;

 private:
  void AttachInputObserver();
  void DetachInputObserver();

  struct MahoTraceRecorder* recorder_{nullptr};
  int64_t tab_id_{0};
  raw_ptr<content::RenderWidgetHost> observed_rwh_{nullptr};
};

}  // namespace maho

#endif  // MAHO_CHROMIUM_BROWSER_UI_WEBUI_MAHO_ROUTINES_MAHO_TRACE_RECORDER_CLIENT_H_
