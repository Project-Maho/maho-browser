// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_routines/maho_trace_recorder_client.h"

#include "base/strings/utf_string_conversions.h"
#include "content/public/browser/render_frame_host.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "third_party/blink/public/common/input/web_keyboard_event.h"
#include "third_party/blink/public/common/input/web_mouse_event.h"

namespace maho {

namespace {

class MahoTraceRecorderClientSingleton {
 public:
  static MahoTraceRecorderClientSingleton* GetInstance() {
    return base::Singleton<MahoTraceRecorderClientSingleton>::get();
  }

  MahoTraceRecorderClient client;
};

}  // namespace

MahoTraceRecorderClient* MahoTraceRecorderClient::GetInstance() {
  return &MahoTraceRecorderClientSingleton::GetInstance()->client;
}

MahoTraceRecorderClient::MahoTraceRecorderClient() = default;

MahoTraceRecorderClient::~MahoTraceRecorderClient() {
  StopAndExportJson();
}

bool MahoTraceRecorderClient::Start(content::WebContents* contents,
                                    int64_t tab_id) {
  if (!contents) {
    return false;
  }

  if (recorder_) {
    StopAndExportJson();
  }

  recorder_ = maho_trace_recorder_create();
  if (!recorder_) {
    return false;
  }

  tab_id_ = tab_id;
  Observe(contents);
  AttachInputObserver();

  if (contents->GetLastCommittedURL().is_valid()) {
    RecordNavigate(contents->GetLastCommittedURL().spec());
  }

  return true;
}

std::string MahoTraceRecorderClient::StopAndExportJson() {
  DetachInputObserver();
  Observe(nullptr);
  tab_id_ = 0;

  if (!recorder_) {
    return "{}";
  }

  char* json = maho_trace_recorder_finish_json(recorder_);
  recorder_ = nullptr;
  if (!json) {
    return "{}";
  }

  std::string result(json);
  maho_string_free(json);
  return result;
}

void MahoTraceRecorderClient::RecordClick(
    const std::optional<std::string>& target_ref,
    double x,
    double y) {
  if (!recorder_) {
    return;
  }
  maho_trace_recorder_record_click(
      recorder_, target_ref ? target_ref->c_str() : nullptr, x, y);
}

void MahoTraceRecorderClient::RecordFill(
    const std::string& text,
    const std::optional<std::string>& selector) {
  if (!recorder_) {
    return;
  }
  maho_trace_recorder_record_fill(
      recorder_, text.c_str(), selector ? selector->c_str() : nullptr);
}

void MahoTraceRecorderClient::RecordNavigate(const std::string& url) {
  if (!recorder_) {
    return;
  }
  maho_trace_recorder_record_navigate(recorder_, url.c_str());
}

void MahoTraceRecorderClient::RecordHover(
    const std::optional<std::string>& target_ref) {
  if (!recorder_) {
    return;
  }
  maho_trace_recorder_record_hover(
      recorder_, target_ref ? target_ref->c_str() : nullptr);
}

void MahoTraceRecorderClient::DidFinishNavigation(
    content::NavigationHandle* navigation_handle) {
  if (navigation_handle && navigation_handle->IsInPrimaryMainFrame() &&
      navigation_handle->HasCommitted() && recorder_) {
    RecordNavigate(navigation_handle->GetURL().spec());
  }
}

void MahoTraceRecorderClient::WebContentsDestroyed() {
  StopAndExportJson();
}

void MahoTraceRecorderClient::OnInputEvent(
    const content::RenderWidgetHost& host,
    const blink::WebInputEvent& event,
    input::InputEventSource source) {
  if (!recorder_) {
    return;
  }

  if (blink::WebInputEvent::IsMouseEventType(event.GetType())) {
    if (event.GetType() == blink::WebInputEvent::Type::kMouseDown ||
        event.GetType() == blink::WebInputEvent::Type::kMouseUp) {
      const auto& mouse = static_cast<const blink::WebMouseEvent&>(event);
      RecordClick(std::nullopt, mouse.PositionInWidget().x(),
                  mouse.PositionInWidget().y());
    }
  } else if (blink::WebInputEvent::IsKeyboardEventType(event.GetType())) {
    if (event.GetType() == blink::WebInputEvent::Type::kChar) {
      const auto& key = static_cast<const blink::WebKeyboardEvent&>(event);
      if (!key.text.empty() && key.text[0] != 0) {
        std::string text;
        base::UTF16ToUTF8(key.text.data(),
                          std::char_traits<char16_t>::length(key.text.data()),
                          &text);
        if (!text.empty()) {
          RecordFill(text, std::nullopt);
        }
      }
    }
  }
}

void MahoTraceRecorderClient::AttachInputObserver() {
  if (!web_contents()) {
    return;
  }
  content::RenderFrameHost* main_frame = web_contents()->GetPrimaryMainFrame();
  if (!main_frame) {
    return;
  }
  content::RenderWidgetHost* rwh = main_frame->GetRenderWidgetHost();
  if (rwh && rwh != observed_rwh_) {
    DetachInputObserver();
    observed_rwh_ = rwh;
    observed_rwh_->AddInputEventObserver(this);
  }
}

void MahoTraceRecorderClient::DetachInputObserver() {
  if (observed_rwh_) {
    observed_rwh_->RemoveInputEventObserver(this);
    observed_rwh_ = nullptr;
  }
}

}  // namespace maho
