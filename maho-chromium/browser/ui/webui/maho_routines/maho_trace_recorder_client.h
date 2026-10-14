// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_CHROMIUM_BROWSER_UI_WEBUI_MAHO_ROUTINES_MAHO_TRACE_RECORDER_CLIENT_H_
#define MAHO_CHROMIUM_BROWSER_UI_WEBUI_MAHO_ROUTINES_MAHO_TRACE_RECORDER_CLIENT_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/raw_ptr_exclusion.h"
#include "base/memory/singleton.h"
#include "content/public/browser/focused_node_details.h"
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

  // Called for every typed character. The browser process has no synchronous
  // signal for whether the focused element is a password / OTP / card field
  // (OnFocusChangedInPage only reports the editable level), so this fails
  // closed: the typed text is never stored. One placeholder Fill step with
  // kRedactedFillText is recorded per typing burst (reset on focus change).
  static constexpr char kRedactedFillText[] = "<redacted>";
  void RecordTypedText(const std::string& text);

  bool is_recording() const { return recorder_ != nullptr; }
  int64_t tab_id() const { return tab_id_; }

  void DidFinishNavigation(
      content::NavigationHandle* navigation_handle) override;
  void WebContentsDestroyed() override;
  void OnFocusChangedInPage(
      const content::FocusedNodeDetails& details) override;

  void OnInputEvent(const content::RenderWidgetHost& host,
                    const blink::WebInputEvent& event,
                    input::InputEventSource source) override;

 private:
  void AttachInputObserver();
  void DetachInputObserver();

  // RAW_PTR_EXCLUSION: Rust-heap handle owned by maho-ffi
  // (maho_trace_recorder_create/finish_json); not PartitionAlloc memory.
  RAW_PTR_EXCLUSION struct MahoTraceRecorder* recorder_{nullptr};
  int64_t tab_id_{0};
  // True once a placeholder Fill was recorded for the current focus run, so a
  // typing burst yields one step instead of one per character.
  bool redacted_fill_recorded_{false};
  raw_ptr<content::RenderWidgetHost> observed_rwh_{nullptr};
};

}  // namespace maho

#endif  // MAHO_CHROMIUM_BROWSER_UI_WEBUI_MAHO_ROUTINES_MAHO_TRACE_RECORDER_CLIENT_H_
