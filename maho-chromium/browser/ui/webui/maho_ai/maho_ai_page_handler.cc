#include "maho/browser/ui/webui/maho_ai/maho_ai_page_handler.h"

#include <string>
#include <unordered_set>
#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/rand_util.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/time/time.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/side_panel/side_panel_entry.h"
#include "chrome/browser/ui/side_panel/side_panel_entry_id.h"
#include "chrome/browser/ui/side_panel/side_panel_ui.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "content/public/browser/page_navigator.h"
#include "ui/base/window_open_disposition.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "maho/browser/ui/webui/maho_ai/maho_ai_prefs_registration.h"
#include "maho/browser/ui/webui/maho_ai_prefs.h"

namespace {

namespace ai = maho::ai_prefs;

static constexpr size_t kMaxPageContextChars = 8000;

constexpr size_t kMaxPersistedEvents = 500;
// Capped at 100 sessions to keep profile prefs loading fast on startup
// while providing sufficient history size for regular usage.
constexpr size_t kMaxPersistedSessions = 100;

std::string GetRuntimeUnavailableMessage() {
  return "OpenCode runtime is not available. "
         "Please configure the runtime endpoint in settings.";
}

std::string GenerateSessionId() {
  return "ses_" + base::NumberToString(
      static_cast<uint64_t>(base::Time::Now().InSecondsFSinceUnixEpoch() *
                            1000)) +
      "_" + base::NumberToString(base::RandUint64() % 100000);
}

// UTF-8 continuation bytes match 10xxxxxx (0x80–0xBF); walking back past them
// lands on the lead byte of the interrupted sequence, giving a safe cut point.
std::string UTF8SafeTruncate(const std::string& text, size_t max_bytes) {
  if (text.size() <= max_bytes) {
    return text;
  }
  size_t cut = max_bytes;
  while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) {
    --cut;
  }
  return text.substr(0, cut);
}

std::string DeriveTitle(const std::string& text) {
  constexpr size_t kMaxTitleLen = 80;
  std::string title = UTF8SafeTruncate(text, kMaxTitleLen);
  size_t newline = title.find('\n');
  if (newline != std::string::npos) {
    title = title.substr(0, newline);
  }
  if (text.size() > kMaxTitleLen && title.size() < text.size()) {
    title += "...";
  }
  return title;
}

std::string SerializeValueToJson(base::Value value) {
  std::string json;
  base::JSONWriter::Write(value, &json);
  return json;
}

base::DictValue RuntimeEventToDict(
    const maho_ai::mojom::RuntimeEvent& event) {
  base::DictValue dict;
  dict.Set("kind", static_cast<int>(event.kind));
  dict.Set("sequence", static_cast<double>(event.sequence));
  dict.Set("timestamp", event.timestamp);
  if (event.text.has_value()) {
    dict.Set("text", *event.text);
  }

  if (event.tool_call) {
    base::DictValue tc;
    tc.Set("call_id", event.tool_call->call_id);
    tc.Set("tool_name", event.tool_call->tool_name);
    tc.Set("arguments_json", event.tool_call->arguments_json);
    tc.Set("status", static_cast<int>(event.tool_call->status));
    dict.Set("tool_call", std::move(tc));
  }
  if (event.tool_result) {
    base::DictValue tr;
    tr.Set("call_id", event.tool_result->call_id);
    tr.Set("success", event.tool_result->success);
    tr.Set("output", event.tool_result->output);
    if (event.tool_result->error_message.has_value()) {
      tr.Set("error_message", *event.tool_result->error_message);
    }
    dict.Set("tool_result", std::move(tr));
  }
  if (event.approval_request) {
    base::DictValue ar;
    ar.Set("approval_id", event.approval_request->approval_id);
    ar.Set("description", event.approval_request->description);
    if (event.approval_request->related_tool_call) {
      base::DictValue rtc;
      rtc.Set("tool_name",
              event.approval_request->related_tool_call->tool_name);
      rtc.Set("arguments_json",
              event.approval_request->related_tool_call->arguments_json);
      ar.Set("related_tool_call", std::move(rtc));
    }
    dict.Set("approval_request", std::move(ar));
  }
  if (event.approval_result) {
    base::DictValue ares;
    ares.Set("approval_id", event.approval_result->approval_id);
    ares.Set("approved", event.approval_result->approved);
    if (event.approval_result->reason.has_value()) {
      ares.Set("reason", *event.approval_result->reason);
    }
    dict.Set("approval_result", std::move(ares));
  }
  if (event.browser_context) {
    base::DictValue bc;
    bc.Set("status", static_cast<int>(event.browser_context->status));
    bc.Set("url", event.browser_context->url);
    bc.Set("title", event.browser_context->title);
    bc.Set("content_snippet", event.browser_context->content_snippet);
    bc.Set("content_length",
           static_cast<int>(event.browser_context->content_length));
    base::ListValue warns;
    for (const auto& w : event.browser_context->warnings) {
      warns.Append(w);
    }
    bc.Set("warnings", std::move(warns));
    dict.Set("browser_context", std::move(bc));
  }
  return dict;
}

maho_ai::mojom::RuntimeEventPtr DictToRuntimeEvent(
    const base::DictValue& dict) {
  auto event = maho_ai::mojom::RuntimeEvent::New();
  event->kind = static_cast<maho_ai::mojom::RuntimeEventKind>(
      dict.FindInt("kind").value_or(0));
  event->sequence =
      static_cast<uint64_t>(dict.FindDouble("sequence").value_or(0));
  event->timestamp = dict.FindDouble("timestamp").value_or(0);
  const std::string* text = dict.FindString("text");
  if (text) {
    event->text = *text;
  }

  const base::DictValue* tc = dict.FindDict("tool_call");
  if (tc) {
    auto tool_call = maho_ai::mojom::ToolCallInfo::New();
    const std::string* cid = tc->FindString("call_id");
    tool_call->call_id = cid ? *cid : "";
    const std::string* tn = tc->FindString("tool_name");
    tool_call->tool_name = tn ? *tn : "";
    const std::string* aj = tc->FindString("arguments_json");
    tool_call->arguments_json = aj ? *aj : "";
    tool_call->status = static_cast<maho_ai::mojom::ToolCallStatus>(
        tc->FindInt("status").value_or(0));
    event->tool_call = std::move(tool_call);
  }
  const base::DictValue* tr = dict.FindDict("tool_result");
  if (tr) {
    auto tool_result = maho_ai::mojom::ToolResultInfo::New();
    const std::string* cid = tr->FindString("call_id");
    tool_result->call_id = cid ? *cid : "";
    tool_result->success = tr->FindBool("success").value_or(false);
    const std::string* out = tr->FindString("output");
    tool_result->output = out ? *out : "";
    const std::string* em = tr->FindString("error_message");
    if (em) {
      tool_result->error_message = *em;
    }
    event->tool_result = std::move(tool_result);
  }
  const base::DictValue* ar = dict.FindDict("approval_request");
  if (ar) {
    auto approval = maho_ai::mojom::ApprovalRequestInfo::New();
    const std::string* aid = ar->FindString("approval_id");
    approval->approval_id = aid ? *aid : "";
    const std::string* desc = ar->FindString("description");
    approval->description = desc ? *desc : "";
    const base::DictValue* rtc = ar->FindDict("related_tool_call");
    if (rtc) {
      auto related = maho_ai::mojom::ToolCallInfo::New();
      const std::string* rtn = rtc->FindString("tool_name");
      related->tool_name = rtn ? *rtn : "";
      const std::string* raj = rtc->FindString("arguments_json");
      related->arguments_json = raj ? *raj : "";
      related->call_id = "";
      related->status = maho_ai::mojom::ToolCallStatus::kPending;
      approval->related_tool_call = std::move(related);
    }
    event->approval_request = std::move(approval);
  }
  const base::DictValue* ares = dict.FindDict("approval_result");
  if (ares) {
    auto result = maho_ai::mojom::ApprovalResultInfo::New();
    const std::string* aid = ares->FindString("approval_id");
    result->approval_id = aid ? *aid : "";
    result->approved = ares->FindBool("approved").value_or(false);
    const std::string* reason = ares->FindString("reason");
    if (reason) {
      result->reason = *reason;
    }
    event->approval_result = std::move(result);
  }
  const base::DictValue* bc = dict.FindDict("browser_context");
  if (bc) {
    auto ctx = maho_ai::mojom::BrowserContextPayload::New();
    ctx->status = static_cast<maho_ai::mojom::BrowserContextStatus>(
        bc->FindInt("status").value_or(
            static_cast<int>(maho_ai::mojom::BrowserContextStatus::kNotRequested)));
    const std::string* bc_url = bc->FindString("url");
    ctx->url = bc_url ? *bc_url : "";
    const std::string* bc_title = bc->FindString("title");
    ctx->title = bc_title ? *bc_title : "";
    const std::string* bc_snippet = bc->FindString("content_snippet");
    ctx->content_snippet = bc_snippet ? *bc_snippet : "";
    ctx->content_length = static_cast<uint32_t>(
        bc->FindInt("content_length").value_or(0));
    const base::ListValue* bc_warns = bc->FindList("warnings");
    if (bc_warns) {
      for (const auto& w : *bc_warns) {
        if (w.is_string()) {
          ctx->warnings.push_back(w.GetString());
        }
      }
    }
    event->browser_context = std::move(ctx);
  }
  return event;
}

maho_ai::mojom::RuntimeEventKind InternalToMojoEventKind(
    MahoAiRuntimeEventType type) {
  switch (type) {
    case MahoAiRuntimeEventType::kAssistantToken:
      return maho_ai::mojom::RuntimeEventKind::kAssistantToken;
    case MahoAiRuntimeEventType::kAssistantThinking:
      return maho_ai::mojom::RuntimeEventKind::kAssistantThinking;
    case MahoAiRuntimeEventType::kTurnComplete:
      return maho_ai::mojom::RuntimeEventKind::kTurnComplete;
    case MahoAiRuntimeEventType::kError:
      return maho_ai::mojom::RuntimeEventKind::kError;
    case MahoAiRuntimeEventType::kToolRequest:
      return maho_ai::mojom::RuntimeEventKind::kToolRequest;
    case MahoAiRuntimeEventType::kToolResult:
      return maho_ai::mojom::RuntimeEventKind::kToolResult;
    case MahoAiRuntimeEventType::kApprovalRequest:
      return maho_ai::mojom::RuntimeEventKind::kApprovalRequest;
    case MahoAiRuntimeEventType::kApprovalResult:
      return maho_ai::mojom::RuntimeEventKind::kApprovalResult;
    case MahoAiRuntimeEventType::kConnectionStateChanged:
      return maho_ai::mojom::RuntimeEventKind::kConnectionStateChanged;
    case MahoAiRuntimeEventType::kBrowserContextInjected:
      return maho_ai::mojom::RuntimeEventKind::kBrowserContextInjected;
  }
}

}  // namespace

MahoAIPageHandler::MahoAIPageHandler(
    mojo::PendingReceiver<maho_ai::mojom::PageHandler> receiver,
    mojo::PendingRemote<maho_ai::mojom::Page> page,
    Browser* browser,
    PrefService* prefs,
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory)
    : receiver_(this, std::move(receiver)),
      page_(std::move(page)),
      browser_(browser),
      prefs_(prefs),
      runtime_router_(prefs, url_loader_factory),
      page_context_extractor_(browser) {
  DCHECK(prefs_);
  if (prefs_->GetString(maho::ai_prefs::kAiViewMode) == "floating" &&
      browser_ && !browser_->is_type_popup()) {
    prefs_->SetString(maho::ai_prefs::kAiViewMode, "sidebar");
  }
  LoadPersistedSessions();
}

MahoAIPageHandler::~MahoAIPageHandler() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!active_session_id_.empty()) {
    if (session_status_ == maho_ai::mojom::SessionStatus::kActive ||
        session_status_ == maho_ai::mojom::SessionStatus::kIdle ||
        session_status_ == maho_ai::mojom::SessionStatus::kPausedForApproval) {
      SetSessionStatus(maho_ai::mojom::SessionStatus::kCompleted);
    }
    PersistEventLog(active_session_id_, /*synchronous=*/true);
    PersistSessionList(/*synchronous=*/true);
  }
}

// static
void MahoAIPageHandler::RegisterProfilePrefs(PrefRegistrySimple* registry) {
  maho::ai::RegisterProfilePrefs(registry);
}



void MahoAIPageHandler::OnRuntimeEvent(MahoAiRuntimeEvent event) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  session_updated_at_ = base::Time::Now().InSecondsFSinceUnixEpoch();

  // Capture the runtime session ID if the adapter now has one.
  std::string rid = runtime_router_.GetActiveAdapter()->GetRuntimeSessionId();
  if (!rid.empty() && runtime_session_id_ != rid) {
    runtime_session_id_ = rid;
    PersistSessionList();
  }

  auto mojo_event = ToMojoEvent(event);
  session_event_log_.push_back(mojo_event->Clone());
  page_->OnRuntimeEvent(std::move(mojo_event));
  switch (event.type) {
    case MahoAiRuntimeEventType::kTurnComplete:
      SetSessionStatus(maho_ai::mojom::SessionStatus::kIdle);
      PersistEventLog(active_session_id_);
      PersistSessionList();
      EmitSessionUpdated();
      break;

    case MahoAiRuntimeEventType::kError:
      SetSessionStatus(maho_ai::mojom::SessionStatus::kError);
      PersistEventLog(active_session_id_);
      PersistSessionList();
      break;

    case MahoAiRuntimeEventType::kToolRequest:
      ++tool_call_count_;
      break;

    case MahoAiRuntimeEventType::kApprovalRequest:
      SetSessionStatus(maho_ai::mojom::SessionStatus::kPausedForApproval);
      break;

    case MahoAiRuntimeEventType::kApprovalResult:
      SetSessionStatus(maho_ai::mojom::SessionStatus::kActive);
      break;

    case MahoAiRuntimeEventType::kConnectionStateChanged:
      if (!event.text.empty()) {
        last_runtime_state_ = event.text;
      }
      MaybeEmitConnectionStateChanged();
      break;

    case MahoAiRuntimeEventType::kBrowserContextInjected:
      break;

    case MahoAiRuntimeEventType::kAssistantThinking:
      break;

    case MahoAiRuntimeEventType::kAssistantToken:
    case MahoAiRuntimeEventType::kToolResult:
      break;
  }
}

maho_ai::mojom::RuntimeEventPtr MahoAIPageHandler::ToMojoEvent(
    const MahoAiRuntimeEvent& event) {
  auto mojo_event = maho_ai::mojom::RuntimeEvent::New();
  mojo_event->kind = InternalToMojoEventKind(event.type);
  mojo_event->sequence = ++event_sequence_;
  mojo_event->timestamp = base::Time::Now().InSecondsFSinceUnixEpoch();
  mojo_event->text = event.text;

  if (event.type == MahoAiRuntimeEventType::kToolRequest) {
    auto tool_call = maho_ai::mojom::ToolCallInfo::New();
    const std::string* call_id = event.payload.FindString("call_id");
    const std::string* tool_name = event.payload.FindString("tool_name");
    const std::string* args = event.payload.FindString("arguments_json");
    tool_call->call_id = call_id ? *call_id : "";
    tool_call->tool_name = tool_name ? *tool_name : "";
    tool_call->arguments_json = args ? *args : "";
    tool_call->status = maho_ai::mojom::ToolCallStatus::kPending;
    mojo_event->tool_call = std::move(tool_call);
  }

  if (event.type == MahoAiRuntimeEventType::kToolResult) {
    auto tool_result = maho_ai::mojom::ToolResultInfo::New();
    const std::string* call_id = event.payload.FindString("call_id");
    const std::string* output = event.payload.FindString("output");
    tool_result->call_id = call_id ? *call_id : "";
    tool_result->success =
        event.payload.FindBool("success").value_or(false);
    tool_result->output = output ? *output : "";
    const std::string* error = event.payload.FindString("error");
    if (error) {
      tool_result->error_message = *error;
    }
    mojo_event->tool_result = std::move(tool_result);
  }

  if (event.type == MahoAiRuntimeEventType::kApprovalRequest) {
    auto approval = maho_ai::mojom::ApprovalRequestInfo::New();
    const std::string* aid = event.payload.FindString("approval_id");
    const std::string* desc = event.payload.FindString("description");
    approval->approval_id = aid ? *aid : "";
    approval->description = desc ? *desc : "";

    const std::string* related_tool = event.payload.FindString("related_tool_name");
    if (related_tool && !related_tool->empty()) {
      auto tool_call = maho_ai::mojom::ToolCallInfo::New();
      tool_call->tool_name = *related_tool;
      const std::string* tool_args =
          event.payload.FindString("related_tool_arguments");
      tool_call->arguments_json = tool_args ? *tool_args : "";
      tool_call->call_id = "";
      tool_call->status = maho_ai::mojom::ToolCallStatus::kPending;
      approval->related_tool_call = std::move(tool_call);
    }

    const std::string* method = event.payload.FindString("method");
    if (method && !method->empty()) {
      approval->description =
          approval->description + " [" + *method + "]";
    }
    const std::string* risk = event.payload.FindString("risk");
    if (risk && !risk->empty()) {
      approval->description =
          approval->description + " (risk: " + *risk + ")";
    }

    mojo_event->approval_request = std::move(approval);
  } else if (event.type == MahoAiRuntimeEventType::kApprovalResult) {
    auto result = maho_ai::mojom::ApprovalResultInfo::New();
    const std::string* aid = event.payload.FindString("approval_id");
    result->approval_id = aid ? *aid : "";
    result->approved = event.payload.FindBool("approved").value_or(false);
    const std::string* reason = event.payload.FindString("reason");
    if (reason) {
      result->reason = *reason;
    }
    mojo_event->approval_result = std::move(result);
  }

  if (event.type == MahoAiRuntimeEventType::kBrowserContextInjected) {
    auto ctx = maho_ai::mojom::BrowserContextPayload::New();
    const std::string* status_str = event.payload.FindString("status");
    std::string s = status_str ? *status_str : "not_requested";
    if (s == "injected") {
      ctx->status = maho_ai::mojom::BrowserContextStatus::kInjected;
    } else if (s == "denied_internal_page") {
      ctx->status = maho_ai::mojom::BrowserContextStatus::kDeniedInternalPage;
    } else if (s == "no_active_tab") {
      ctx->status = maho_ai::mojom::BrowserContextStatus::kNoActiveTab;
    } else if (s == "cannot_access") {
      ctx->status = maho_ai::mojom::BrowserContextStatus::kCannotAccess;
    } else if (s == "extraction_failed") {
      ctx->status = maho_ai::mojom::BrowserContextStatus::kExtractionFailed;
    } else {
      ctx->status = maho_ai::mojom::BrowserContextStatus::kNotRequested;
    }
    const std::string* url = event.payload.FindString("url");
    ctx->url = url ? *url : "";
    const std::string* title = event.payload.FindString("title");
    ctx->title = title ? *title : "";
    const std::string* snippet = event.payload.FindString("content_snippet");
    ctx->content_snippet = snippet ? *snippet : "";
    ctx->content_length = static_cast<uint32_t>(
        event.payload.FindInt("content_length").value_or(0));
    const base::ListValue* warns = event.payload.FindList("warnings");
    if (warns) {
      for (const auto& w : *warns) {
        if (w.is_string()) {
          ctx->warnings.push_back(w.GetString());
        }
      }
    }
    mojo_event->browser_context = std::move(ctx);
  }

  return mojo_event;
}

maho_ai::mojom::SessionInfoPtr MahoAIPageHandler::MakeActiveSessionInfo()
    const {
  auto info = maho_ai::mojom::SessionInfo::New();
  info->session_id = active_session_id_;
  info->created_at = session_created_at_;
  info->adapter_name = runtime_router_.GetActiveAdapterName();
  info->is_active = !active_session_id_.empty();
  info->is_read_only = false;
  info->status = session_status_;
  info->event_count = static_cast<uint32_t>(session_event_log_.size());
  info->tool_call_count = tool_call_count_;
  if (!session_title_.empty()) {
    info->title = session_title_;
  }
  if (session_updated_at_ > 0) {
    info->updated_at = session_updated_at_;
  }
  if (!runtime_session_id_.empty()) {
    info->runtime_session_id = runtime_session_id_;
  }
  if (!last_runtime_state_.empty()) {
    info->last_runtime_state = last_runtime_state_;
  } else if (runtime_router_.IsAvailable()) {
    info->last_runtime_state = "connected";
  } else {
    info->last_runtime_state = "disconnected";
  }
  return info;
}

void MahoAIPageHandler::GetConnectionState(
    GetConnectionStateCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto state = ResolveConnectionState();

  std::move(callback).Run(state, runtime_router_.GetActiveAdapterName());
}

void MahoAIPageHandler::GetSessionList(GetSessionListCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  std::vector<maho_ai::mojom::SessionInfoPtr> sessions;
  if (!active_session_id_.empty()) {
    sessions.push_back(MakeActiveSessionInfo());
  }
  for (const auto& s : persisted_sessions_) {
    if (s->session_id != active_session_id_) {
      sessions.push_back(s->Clone());
    }
  }
  std::move(callback).Run(std::move(sessions));
}

void MahoAIPageHandler::StartSession(
    const std::optional<std::string>& initial_prompt,
    maho_ai::mojom::InteractionMode mode,
    StartSessionCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!active_session_id_.empty()) {
    // Finalize the outgoing session: persist events and snapshot metadata
    // into persisted_sessions_ so historical sessions retain truthful state.
    if (session_status_ == maho_ai::mojom::SessionStatus::kActive ||
        session_status_ == maho_ai::mojom::SessionStatus::kIdle ||
        session_status_ == maho_ai::mojom::SessionStatus::kPausedForApproval) {
      SetSessionStatus(maho_ai::mojom::SessionStatus::kCompleted);
    }
    session_updated_at_ = base::Time::Now().InSecondsFSinceUnixEpoch();
    PersistEventLog(active_session_id_);

    auto old_info = MakeActiveSessionInfo();
    old_info->is_active = false;
    old_info->is_read_only = true;
    // Insert at front so most recent historical session comes first.
    persisted_sessions_.insert(persisted_sessions_.begin(),
                               std::move(old_info));
    if (persisted_sessions_.size() > kMaxPersistedSessions) {
      persisted_sessions_.resize(kMaxPersistedSessions);
    }
  }

  active_session_id_ = GenerateSessionId();
  session_created_at_ = base::Time::Now().InSecondsFSinceUnixEpoch();
  session_updated_at_ = session_created_at_;
  event_sequence_ = 0;
  tool_call_count_ = 0;
  session_event_log_.clear();
  session_title_.clear();
  runtime_session_id_.clear();
  last_runtime_state_.clear();
  session_status_ = maho_ai::mojom::SessionStatus::kActive;

  if (initial_prompt.has_value() && !initial_prompt->empty()) {
    UpdateSessionTitle(*initial_prompt);
  }

  auto session_info = MakeActiveSessionInfo();
  PersistSessionList();

  auto status_event = maho_ai::mojom::RuntimeEvent::New();
  status_event->kind = maho_ai::mojom::RuntimeEventKind::kSessionStatus;
  status_event->sequence = ++event_sequence_;
  status_event->timestamp = base::Time::Now().InSecondsFSinceUnixEpoch();
  status_event->text = "started";
  session_event_log_.push_back(status_event->Clone());
  page_->OnRuntimeEvent(std::move(status_event));

  std::move(callback).Run(session_info->Clone());

  if (initial_prompt.has_value() && !initial_prompt->empty()) {
    SubmitPrompt(active_session_id_, *initial_prompt,
                 /*attach_browser_context=*/true,
                 mode);
  }
}

void MahoAIPageHandler::ResumeSession(const std::string& session_id,
                                       ResumeSessionCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (session_id == active_session_id_ && !active_session_id_.empty()) {
    auto session_info = MakeActiveSessionInfo();
    std::vector<maho_ai::mojom::RuntimeEventPtr> replay;
    for (const auto& evt : session_event_log_) {
      replay.push_back(evt->Clone());
    }
    std::move(callback).Run(std::move(session_info), std::move(replay));
    return;
  }

  for (const auto& s : persisted_sessions_) {
    if (s->session_id == session_id) {
      auto replay = LoadPersistedEventLog(session_id);
      auto info = s->Clone();
      // Historical sessions are read-only: no runtime connection exists,
      // so SubmitPrompt/CancelTurn/RespondToApproval will be rejected.
      // The frontend should present these as immutable history views.
      info->is_active = false;
      info->is_read_only = true;
      info->last_runtime_state = "disconnected";
      info->status = s->status;
      std::move(callback).Run(std::move(info), std::move(replay));
      return;
    }
  }

  std::move(callback).Run(nullptr,
                          std::vector<maho_ai::mojom::RuntimeEventPtr>());
}

void MahoAIPageHandler::SubmitPrompt(const std::string& session_id,
                                      const std::string& prompt,
                                      bool attach_browser_context,
                                      maho_ai::mojom::InteractionMode mode) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (session_id != active_session_id_) {
    return;
  }

  // Persist the user prompt as a first-class event so history replay
  // includes it rather than relying on client-side synthetic entries.
  auto prompt_event = maho_ai::mojom::RuntimeEvent::New();
  prompt_event->kind = maho_ai::mojom::RuntimeEventKind::kUserPrompt;
  prompt_event->sequence = ++event_sequence_;
  prompt_event->timestamp = base::Time::Now().InSecondsFSinceUnixEpoch();
  prompt_event->text = prompt;
  session_event_log_.push_back(prompt_event->Clone());
  page_->OnRuntimeEvent(std::move(prompt_event));

  // Ensure session is active when a prompt is submitted.
  if (session_status_ != maho_ai::mojom::SessionStatus::kActive) {
    SetSessionStatus(maho_ai::mojom::SessionStatus::kActive);
  }

  UpdateSessionTitle(prompt);

  MahoAiRuntimeAdapter* adapter = runtime_router_.GetActiveAdapter();

  if (!adapter->IsAvailable()) {
    MahoAiRuntimeEvent error_event;
    error_event.type = MahoAiRuntimeEventType::kError;
    error_event.text = GetRuntimeUnavailableMessage();
    OnRuntimeEvent(std::move(error_event));
    return;
  }

  if (attach_browser_context) {
    page_context_extractor_.GetPageContext(
        base::BindOnce(&MahoAIPageHandler::OnContextExtractedThenSubmit,
                       weak_factory_.GetWeakPtr(), prompt, mode));
    return;
  }

  adapter->SubmitMessage(
      prompt, maho_ai::mojom::ChatIntent::kFreeform,
      attach_browser_context, mode,
      base::BindRepeating(&MahoAIPageHandler::OnRuntimeEvent,
                          weak_factory_.GetWeakPtr()));
}

void MahoAIPageHandler::OnContextExtractedThenSubmit(
    std::string user_prompt,
    maho_ai::mojom::InteractionMode mode,
    MahoAiPageContextExtractor::PageContextResult ctx) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  MahoAiRuntimeAdapter* adapter = runtime_router_.GetActiveAdapter();
  MahoAiRuntimeEvent ctx_event;
  ctx_event.type = MahoAiRuntimeEventType::kBrowserContextInjected;

  std::string final_prompt;
  if (!ctx.HasUsableContent()) {
    DVLOG(1) << "MahoAIPageHandler: page context extraction failed ("
             << ctx.GetExtractionStatusString() << "); sending original prompt";
    final_prompt = user_prompt;
    ctx_event.payload.Set("status", "extraction_failed");
    ctx_event.text =
        "Page content could not be extracted; sending prompt without page "
        "context.";
  } else {
    std::string clipped_text = ctx.main_text;
    if (clipped_text.size() > kMaxPageContextChars) {
      clipped_text = clipped_text.substr(0, kMaxPageContextChars) + "...";
    }
    final_prompt = base::StrCat({"[Current Page Context]\nTitle: ", ctx.title,
                                 "\nURL: ", ctx.url, "\nContent: ", clipped_text,
                                 "\n\nUser Question:\n", user_prompt});
    ctx_event.payload.Set("status", "ok");
    ctx_event.payload.Set("url", ctx.url);
    ctx_event.payload.Set("title", ctx.title);
    ctx_event.payload.Set("content_length",
                          static_cast<int>(ctx.main_text.size()));
    ctx_event.text =
        base::StrCat({"Attached page context: ", ctx.title});
  }

  OnRuntimeEvent(std::move(ctx_event));

  adapter->SubmitMessage(
      final_prompt, maho_ai::mojom::ChatIntent::kFreeform,
      /*attach_browser_context=*/false, mode,
      base::BindRepeating(&MahoAIPageHandler::OnRuntimeEvent,
                          weak_factory_.GetWeakPtr()));
}

void MahoAIPageHandler::CancelTurn(const std::string& session_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (session_id != active_session_id_) {
    return;
  }

  runtime_router_.GetActiveAdapter()->CancelCurrentTurn();
  SetSessionStatus(maho_ai::mojom::SessionStatus::kCancelled);
  PersistEventLog(active_session_id_);
  PersistSessionList();
}

void MahoAIPageHandler::RespondToApproval(const std::string& session_id,
                                           const std::string& approval_id,
                                           bool approved) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (session_id != active_session_id_) {
    return;
  }

  // Do NOT flip status to kActive here — wait for the runtime to confirm
  // via permission.replied (which arrives as kApprovalResult event).
  runtime_router_.GetActiveAdapter()->RespondToApproval(approval_id, approved);
}

void MahoAIPageHandler::ClosePanel() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!browser_) {
    return;
  }

  if (browser_->is_type_popup()) {
    browser_->window()->Close();
    return;
  }

  auto* side_panel_ui = browser_->GetFeatures().side_panel_ui();
  if (!side_panel_ui) {
    return;
  }

  if (side_panel_ui->GetCurrentEntryId(SidePanelType::kContent) !=
      SidePanelEntryId::kMahoAiPanel) {
    return;
  }

  side_panel_ui->Close(SidePanelType::kContent,
                       SidePanelEntryHideReason::kSidePanelClosed,
                       /*suppress_animations=*/true);
}

void MahoAIPageHandler::OpenSettings() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!browser_) {
    return;
  }
  content::OpenURLParams params(
      GURL("chrome://maho-settings/"),
      content::Referrer(),
      WindowOpenDisposition::SINGLETON_TAB,
      ui::PAGE_TRANSITION_AUTO_TOPLEVEL,
      /*is_renderer_initiated=*/false);
  browser_->OpenURL(params, /*navigation_handle_callback=*/{});
}

void MahoAIPageHandler::GetViewMode(GetViewModeCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::string pref_val = prefs_->GetString(maho::ai_prefs::kAiViewMode);
  maho_ai::mojom::ViewMode mode = (pref_val == "floating" ?
      maho_ai::mojom::ViewMode::kFloating : maho_ai::mojom::ViewMode::kSidebar);
  std::move(callback).Run(mode);
}

void MahoAIPageHandler::SetViewMode(maho_ai::mojom::ViewMode mode) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::string new_pref = (mode == maho_ai::mojom::ViewMode::kFloating ? "floating" : "sidebar");
  std::string current_pref = prefs_->GetString(maho::ai_prefs::kAiViewMode);
  if (new_pref == current_pref) {
    return;
  }

  prefs_->SetString(maho::ai_prefs::kAiViewMode, new_pref);

  if (mode == maho_ai::mojom::ViewMode::kFloating) {
    if (browser_ && !browser_->is_type_popup()) {
      content::OpenURLParams params(
          GURL("chrome://maho-ai/"),
          content::Referrer(),
          WindowOpenDisposition::NEW_POPUP,
          ui::PAGE_TRANSITION_AUTO_TOPLEVEL,
          /*is_renderer_initiated=*/false);
      browser_->OpenURL(params, /*navigation_handle_callback=*/{});
      ClosePanel();
    }
  } else {
    if (browser_ && browser_->is_type_popup()) {
      Browser* normal_browser = chrome::FindBrowserWithProfile(browser_->profile());
      if (normal_browser && normal_browser->GetFeatures().side_panel_ui()) {
        normal_browser->GetFeatures().side_panel_ui()->Show(SidePanelEntryId::kMahoAiPanel);
      }
      browser_->window()->Close();
    }
  }
}

void MahoAIPageHandler::GetSessionHistory(
    const std::string& session_id,
    uint32_t offset,
    uint32_t limit,
    GetSessionHistoryCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  std::vector<maho_ai::mojom::RuntimeEventPtr> events;
  uint32_t total_count = 0;

  if (session_id == active_session_id_ && !active_session_id_.empty()) {
    total_count = static_cast<uint32_t>(session_event_log_.size());
    uint32_t start = std::min(offset, total_count);
    uint32_t end = std::min(start + limit, total_count);
    for (uint32_t i = start; i < end; ++i) {
      events.push_back(session_event_log_[i]->Clone());
    }
  } else {
    auto all_events = LoadPersistedEventLog(session_id);
    total_count = static_cast<uint32_t>(all_events.size());
    uint32_t start = std::min(offset, total_count);
    uint32_t end = std::min(start + limit, total_count);
    for (uint32_t i = start; i < end; ++i) {
      events.push_back(std::move(all_events[i]));
    }
  }

  std::move(callback).Run(std::move(events), total_count);
}

void MahoAIPageHandler::MaybeEmitConnectionStateChanged() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto current = ResolveConnectionState();

  if (current != last_emitted_connection_state_) {
    last_emitted_connection_state_ = current;
    page_->OnConnectionStateChanged(current);
  }
}

maho_ai::mojom::RuntimeConnectionState
MahoAIPageHandler::ResolveConnectionState() const {
  if (!runtime_router_.IsAvailable()) {
    return maho_ai::mojom::RuntimeConnectionState::kDisconnected;
  }
  if (last_runtime_state_ == "paused_for_approval") {
    return maho_ai::mojom::RuntimeConnectionState::kPausedForApproval;
  }
  if (last_runtime_state_ == "resumed") {
    return maho_ai::mojom::RuntimeConnectionState::kResumed;
  }
  return maho_ai::mojom::RuntimeConnectionState::kConnected;
}

void MahoAIPageHandler::EmitSessionUpdated() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!active_session_id_.empty()) {
    page_->OnSessionUpdated(MakeActiveSessionInfo());
  }
}

void MahoAIPageHandler::SetSessionStatus(
    maho_ai::mojom::SessionStatus status) {
  if (session_status_ == status) {
    return;
  }
  session_status_ = status;

  // Emit a replayable kSessionStatus event so history replay reflects
  // meaningful lifecycle transitions (not just the initial "started").
  if (!active_session_id_.empty()) {
    auto status_event = maho_ai::mojom::RuntimeEvent::New();
    status_event->kind = maho_ai::mojom::RuntimeEventKind::kSessionStatus;
    status_event->sequence = ++event_sequence_;
    status_event->timestamp = base::Time::Now().InSecondsFSinceUnixEpoch();
    switch (status) {
      case maho_ai::mojom::SessionStatus::kActive:
        status_event->text = "active";
        break;
      case maho_ai::mojom::SessionStatus::kIdle:
        status_event->text = "idle";
        break;
      case maho_ai::mojom::SessionStatus::kPausedForApproval:
        status_event->text = "paused_for_approval";
        break;
      case maho_ai::mojom::SessionStatus::kCompleted:
        status_event->text = "completed";
        break;
      case maho_ai::mojom::SessionStatus::kCancelled:
        status_event->text = "cancelled";
        break;
      case maho_ai::mojom::SessionStatus::kError:
        status_event->text = "error";
        break;
      case maho_ai::mojom::SessionStatus::kCreated:
        status_event->text = "created";
        break;
    }
    session_event_log_.push_back(status_event->Clone());
    page_->OnRuntimeEvent(std::move(status_event));
  }

  EmitSessionUpdated();
}



void MahoAIPageHandler::PersistSessionList(bool synchronous) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!prefs_->GetBoolean(ai::kSessionPersistenceEnabled)) {
    return;
  }

  // Trim `persisted_sessions_` in-memory to prevent unbounded growth.
  // The total number of sessions persisted on disk is capped at kMaxPersistedSessions.
  // If there is an active session, it takes 1 slot, leaving kMaxPersistedSessions - 1 slots for historical sessions.
  size_t max_historical = active_session_id_.empty() ? kMaxPersistedSessions : (kMaxPersistedSessions - 1);
  if (persisted_sessions_.size() > max_historical) {
    persisted_sessions_.resize(max_historical);
  }

  base::ListValue list;
  std::vector<std::string> kept_ids;

  if (!active_session_id_.empty()) {
    kept_ids.push_back(active_session_id_);
    base::DictValue entry;
    entry.Set("session_id", active_session_id_);
    entry.Set("created_at", session_created_at_);
    entry.Set("updated_at", session_updated_at_);
    entry.Set("adapter_name", runtime_router_.GetActiveAdapterName());
    entry.Set("status", static_cast<int>(session_status_));
    entry.Set("event_count",
              static_cast<int>(session_event_log_.size()));
    entry.Set("tool_call_count", static_cast<int>(tool_call_count_));
    if (!session_title_.empty()) {
      entry.Set("title", session_title_);
    }
    if (!runtime_session_id_.empty()) {
      entry.Set("runtime_session_id", runtime_session_id_);
    }
    if (!last_runtime_state_.empty()) {
      entry.Set("last_runtime_state", last_runtime_state_);
    }
    list.Append(std::move(entry));
  }

  for (const auto& s : persisted_sessions_) {
    if (list.size() >= kMaxPersistedSessions) {
      break;
    }
    if (s->session_id == active_session_id_) {
      continue;
    }
    kept_ids.push_back(s->session_id);
    base::DictValue entry;
    entry.Set("session_id", s->session_id);
    entry.Set("created_at", s->created_at);
    entry.Set("adapter_name", s->adapter_name);
    entry.Set("status", static_cast<int>(s->status));
    entry.Set("event_count", static_cast<int>(s->event_count));
    entry.Set("tool_call_count", static_cast<int>(s->tool_call_count));
    if (s->title.has_value()) {
      entry.Set("title", *s->title);
    }
    if (s->updated_at.has_value()) {
      entry.Set("updated_at", *s->updated_at);
    }
    if (s->runtime_session_id.has_value()) {
      entry.Set("runtime_session_id", *s->runtime_session_id);
    }
    if (s->last_runtime_state.has_value()) {
      entry.Set("last_runtime_state", *s->last_runtime_state);
    }
    list.Append(std::move(entry));
  }

  if (synchronous) {
    std::string json = SerializeValueToJson(base::Value(std::move(list)));
    prefs_->SetString(ai::kSessionListJson, json);

    ScopedDictPrefUpdate update(prefs_, ai::kSessionEventLogs);
    std::vector<std::string> orphans_to_remove;
    std::unordered_set<std::string> kept_set(kept_ids.begin(), kept_ids.end());
    for (auto it : update.Get()) {
      if (kept_set.find(it.first) == kept_set.end()) {
        orphans_to_remove.push_back(it.first);
      }
    }
    for (const auto& orphan : orphans_to_remove) {
      update->Remove(orphan);
    }
  } else {
    session_list_weak_factory_.InvalidateWeakPtrs();
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, {base::MayBlock(), base::TaskPriority::BEST_EFFORT},
        base::BindOnce(&SerializeValueToJson, base::Value(std::move(list))),
        base::BindOnce(&MahoAIPageHandler::OnSessionListSerialized,
                       session_list_weak_factory_.GetWeakPtr(), std::move(kept_ids)));
  }
}

void MahoAIPageHandler::OnSessionListSerialized(std::vector<std::string> kept_ids, std::string json) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  prefs_->SetString(ai::kSessionListJson, json);

  ScopedDictPrefUpdate update(prefs_, ai::kSessionEventLogs);
  std::vector<std::string> orphans_to_remove;
  std::unordered_set<std::string> kept_set(kept_ids.begin(), kept_ids.end());
  for (auto it : update.Get()) {
    if (kept_set.find(it.first) == kept_set.end()) {
      orphans_to_remove.push_back(it.first);
    }
  }
  for (const auto& orphan : orphans_to_remove) {
    update->Remove(orphan);
  }
}

void MahoAIPageHandler::LoadPersistedSessions() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  persisted_sessions_.clear();

  const std::string& json = prefs_->GetString(ai::kSessionListJson);
  if (json.empty()) {
    return;
  }

  auto parsed = base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    return;
  }

  for (const auto& item : parsed->GetList()) {
    if (persisted_sessions_.size() >= kMaxPersistedSessions) {
      break;
    }
    if (!item.is_dict()) {
      continue;
    }
    const base::DictValue& dict = item.GetDict();
    auto info = maho_ai::mojom::SessionInfo::New();
    const std::string* sid = dict.FindString("session_id");
    if (!sid) {
      continue;
    }
    info->session_id = *sid;
    info->created_at = dict.FindDouble("created_at").value_or(0);
    const std::string* adapter = dict.FindString("adapter_name");
    info->adapter_name = adapter ? *adapter : "";
    info->is_active = false;
    info->is_read_only = true;
    info->status = static_cast<maho_ai::mojom::SessionStatus>(
        dict.FindInt("status").value_or(
            static_cast<int>(maho_ai::mojom::SessionStatus::kCompleted)));
    info->event_count =
        static_cast<uint32_t>(dict.FindInt("event_count").value_or(0));
    info->tool_call_count =
        static_cast<uint32_t>(dict.FindInt("tool_call_count").value_or(0));

    const std::string* title = dict.FindString("title");
    if (title) {
      info->title = *title;
    }
    auto updated_at = dict.FindDouble("updated_at");
    if (updated_at.has_value()) {
      info->updated_at = *updated_at;
    }
    const std::string* rsid = dict.FindString("runtime_session_id");
    if (rsid) {
      info->runtime_session_id = *rsid;
    }
    const std::string* lrs = dict.FindString("last_runtime_state");
    if (lrs) {
      info->last_runtime_state = *lrs;
    } else {
      info->last_runtime_state = "disconnected";
    }

    persisted_sessions_.push_back(std::move(info));
  }

  // Orphan event logs cleanup on startup.
  {
    std::unordered_set<std::string> valid_ids;
    if (!active_session_id_.empty()) {
      valid_ids.insert(active_session_id_);
    }
    for (const auto& s : persisted_sessions_) {
      valid_ids.insert(s->session_id);
    }
    std::vector<std::string> orphans_to_remove;
    {
      const auto& dict = prefs_->GetDict(ai::kSessionEventLogs);
      for (auto it : dict) {
        if (valid_ids.find(it.first) == valid_ids.end()) {
          orphans_to_remove.push_back(it.first);
        }
      }
    }
    if (!orphans_to_remove.empty()) {
      ScopedDictPrefUpdate update(prefs_, ai::kSessionEventLogs);
      for (const auto& orphan : orphans_to_remove) {
        update->Remove(orphan);
      }
    }
  }
}

void MahoAIPageHandler::PersistEventLog(const std::string& session_id, bool synchronous) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (session_id.empty() ||
      !prefs_->GetBoolean(ai::kSessionPersistenceEnabled)) {
    return;
  }

  base::ListValue list;
  size_t start = 0;
  if (session_event_log_.size() > kMaxPersistedEvents) {
    start = session_event_log_.size() - kMaxPersistedEvents;
  }
  for (size_t i = start; i < session_event_log_.size(); ++i) {
    list.Append(RuntimeEventToDict(*session_event_log_[i]));
  }

  if (synchronous) {
    std::string json = SerializeValueToJson(base::Value(std::move(list)));
    ScopedDictPrefUpdate update(prefs_, ai::kSessionEventLogs);
    update->Set(session_id, json);
  } else {
    event_log_weak_factory_.InvalidateWeakPtrs();
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, {base::MayBlock(), base::TaskPriority::BEST_EFFORT},
        base::BindOnce(&SerializeValueToJson, base::Value(std::move(list))),
        base::BindOnce(&MahoAIPageHandler::OnEventLogSerialized,
                       event_log_weak_factory_.GetWeakPtr(), session_id));
  }
}

void MahoAIPageHandler::OnEventLogSerialized(const std::string& session_id, std::string json) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  bool session_still_valid = (session_id == active_session_id_);
  if (!session_still_valid) {
    for (const auto& s : persisted_sessions_) {
      if (s->session_id == session_id) {
        session_still_valid = true;
        break;
      }
    }
  }
  if (!session_still_valid) {
    return;
  }
  ScopedDictPrefUpdate update(prefs_, ai::kSessionEventLogs);
  update->Set(session_id, json);
}

std::vector<maho_ai::mojom::RuntimeEventPtr>
MahoAIPageHandler::LoadPersistedEventLog(const std::string& session_id) {
  std::vector<maho_ai::mojom::RuntimeEventPtr> events;

  const auto& dict = prefs_->GetDict(ai::kSessionEventLogs);
  const std::string* json = dict.FindString(session_id);
  if (!json || json->empty()) {
    return events;
  }

  auto parsed = base::JSONReader::Read(*json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    return events;
  }

  for (const auto& item : parsed->GetList()) {
    if (!item.is_dict()) {
      continue;
    }
    events.push_back(DictToRuntimeEvent(item.GetDict()));
  }
  return events;
}

void MahoAIPageHandler::ClearPersistedEventLog(
    const std::string& session_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!session_id.empty()) {
    ScopedDictPrefUpdate update(prefs_, ai::kSessionEventLogs);
    update->Remove(session_id);
  }
}

void MahoAIPageHandler::UpdateSessionTitle(const std::string& text) {
  if (session_title_.empty() && !text.empty()) {
    session_title_ = DeriveTitle(text);
  }
}
