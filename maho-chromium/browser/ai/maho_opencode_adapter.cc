// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ai/maho_opencode_adapter.h"

#include <memory>
#include <optional>
#include <string_view>
#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/browser.h"  // nogncheck
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "maho/browser/ai/maho_browser_tool_executor.h"
#include "maho/browser/ai/maho_browser_tool_registry.h"
#include "maho/browser/ai/maho_opencode_runtime_service.h"
#include "maho/browser/ui/webui/maho_ai/maho_ai.mojom.h"
#include "maho/browser/ui/webui/maho_ai_prefs.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/cpp/simple_url_loader_stream_consumer.h"

namespace {

constexpr net::NetworkTrafficAnnotationTag kOpenCodeTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("maho_opencode_runtime",
        R"maho(semantics {
          sender: "Maho AI (OpenCode Runtime)"
          description:
            "Communicates with a local or remote OpenCode server "
            "for AI agent sessions."
          trigger:
            "User interacts with Maho AI when OpenCode runtime is "
            "enabled."
          data:
            "Prompt content, session identifiers, permission "
            "decisions."
          destination: OTHER
          internal { contacts { email: "maho@example.com" } }
          user_data { type: OTHER }
          last_reviewed: "2026-05-06"
        }
        policy {
          cookies_allowed: NO
          setting:
            "User must configure an OpenCode runtime endpoint."
        })maho");

}  // namespace

class MahoOpenCodeSseConsumer final
    : public network::SimpleURLLoaderStreamConsumer {
 public:
  explicit MahoOpenCodeSseConsumer(MahoOpenCodeAdapter* adapter)
      : adapter_(adapter) {}

  void OnDataReceived(std::string_view string_piece,
                      base::OnceClosure resume) override {
    if (adapter_) {
      adapter_->OnSseDataReceived(std::string(string_piece));
    }
    std::move(resume).Run();
  }

  void OnComplete(bool success) override {
    if (adapter_) {
      adapter_->OnSseComplete(success);
    }
  }

  void OnRetry(base::OnceClosure start_retry) override {
    std::move(start_retry).Run();
  }

 private:
  raw_ptr<MahoOpenCodeAdapter> adapter_;
};

MahoOpenCodeAdapter::MahoOpenCodeAdapter(
    Browser* browser,
    PrefService* prefs,
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory)
    : browser_(browser),
      prefs_(prefs),
      url_loader_factory_(std::move(url_loader_factory)),
      tool_executor_(std::make_unique<MahoBrowserToolExecutor>(browser)),
      page_context_extractor_(browser) {}

MahoOpenCodeAdapter::~MahoOpenCodeAdapter() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

std::string MahoOpenCodeAdapter::GetAdapterName() const {
  return "opencode";
}

GURL MahoOpenCodeAdapter::GetServerBaseUrl() const {
  std::string endpoint =
      prefs_->GetString(maho::ai_prefs::kRuntimeEndpoint);
  if (endpoint.empty()) {
    auto* service = MahoOpenCodeRuntimeService::GetInstance();
    if (service &&
        service->state() == MahoOpenCodeRuntimeService::State::kReady) {
      return service->GetManagedEndpoint();
    }
    return GURL();
  }
  if (!base::EndsWith(endpoint, "/", base::CompareCase::SENSITIVE)) {
    endpoint.push_back('/');
  }
  return GURL(endpoint);
}

bool MahoOpenCodeAdapter::IsAvailable() const {
  GURL base_url = GetServerBaseUrl();
  return base_url.is_valid() &&
         (base_url.SchemeIs("http") || base_url.SchemeIs("https"));
}

void MahoOpenCodeAdapter::SubmitMessage(
    const std::string& message,
    maho_ai::mojom::ChatIntent chat_intent,
    RuntimeEventCallback on_event) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  current_on_event_ = on_event;
  accumulated_text_.clear();
  local_tool_round_count_ = 0;

  if (!IsAvailable()) {
    MahoAiRuntimeEvent event;
    event.type = MahoAiRuntimeEventType::kError;
    event.text = "OpenCode runtime endpoint is not configured.";
    on_event.Run(std::move(event));
    return;
  }

  EnsureEventStreamConnected();

  if (IsPageIntent(chat_intent)) {
    page_context_extractor_.GetPageContext(base::BindOnce(
        &MahoOpenCodeAdapter::OnPageContextForIntent,
        weak_factory_.GetWeakPtr(), message, chat_intent, on_event));
    return;
  }

  if (session_id_.empty()) {
    std::string context_prefix = BuildBrowserContextPrefix();
    if (!context_prefix.empty()) {
      pending_prompt_ = context_prefix + "\n\n" + message;
    } else {
      pending_prompt_ = message;
    }
    CreateSession();
  } else {
    SubmitPromptToSession(message);
  }
}

void MahoOpenCodeAdapter::CancelCurrentTurn() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (session_id_.empty()) {
    return;
  }

  GURL url = GetServerBaseUrl().Resolve("session/" + session_id_ + "/abort");

  auto request = std::make_unique<network::ResourceRequest>();
  request->url = url;
  request->method = "POST";
  request->headers.SetHeader("Content-Type", "application/json");

  cancel_loader_ = network::SimpleURLLoader::Create(
      std::move(request), kOpenCodeTrafficAnnotation);
  cancel_loader_->AttachStringForUpload("{}", "application/json");

  cancel_loader_->DownloadToStringOfUnboundedSizeUntilCrashAndDie(
      url_loader_factory_.get(),
      base::BindOnce([](std::optional<std::string> response) {}));
}

void MahoOpenCodeAdapter::RespondToApproval(const std::string& approval_id,
                                              bool approved) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (session_id_.empty() || approval_id.empty()) {
    return;
  }

  pending_approval_id_.clear();
  approval_timeout_timer_.Stop();

  GURL url = GetServerBaseUrl().Resolve(
      "session/" + session_id_ + "/permissions/" + approval_id);

  auto request = std::make_unique<network::ResourceRequest>();
  request->url = url;
  request->method = "POST";
  request->headers.SetHeader("Content-Type", "application/json");

  base::DictValue body;
  body.Set("response", approved ? "once" : "reject");
  std::string body_json;
  base::JSONWriter::Write(body, &body_json);

  permission_loader_ = network::SimpleURLLoader::Create(
      std::move(request), kOpenCodeTrafficAnnotation);
  permission_loader_->AttachStringForUpload(body_json, "application/json");

  permission_loader_->DownloadToStringOfUnboundedSizeUntilCrashAndDie(
      url_loader_factory_.get(),
      base::BindOnce([](std::optional<std::string> response) {}));
}

std::string MahoOpenCodeAdapter::GetRuntimeSessionId() const {
  return session_id_;
}

void MahoOpenCodeAdapter::Reset() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  session_id_.clear();
  accumulated_text_.clear();
  part_types_.clear();
  pending_prompt_.clear();
  pending_is_direct_message_ = false;
  awaiting_direct_response_ = false;
  current_on_event_.Reset();
  local_tool_round_count_ = 0;
  pending_approval_id_.clear();
  approval_timeout_timer_.Stop();

  prompt_loader_.reset();
  direct_message_loader_.reset();
  session_loader_.reset();
  cancel_loader_.reset();
  permission_loader_.reset();

  sse_loader_.reset();
  sse_consumer_.reset();
  sse_buffer_.clear();
  sse_connected_ = false;
}

void MahoOpenCodeAdapter::EnsureEventStreamConnected() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (sse_loader_) {
    return;
  }

  GURL url = GetServerBaseUrl().Resolve("event");
  if (!url.is_valid()) {
    return;
  }

  auto request = std::make_unique<network::ResourceRequest>();
  request->url = url;
  request->method = "GET";
  request->headers.SetHeader("Accept", "text/event-stream");
  request->headers.SetHeader("Cache-Control", "no-cache");

  sse_loader_ = network::SimpleURLLoader::Create(
      std::move(request), kOpenCodeTrafficAnnotation);
  sse_loader_->SetAllowHttpErrorResults(true);

  sse_consumer_ = std::make_unique<MahoOpenCodeSseConsumer>(this);
  sse_loader_->DownloadAsStream(url_loader_factory_.get(),
                                sse_consumer_.get());
}

void MahoOpenCodeAdapter::CreateSession() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  GURL url = GetServerBaseUrl().Resolve("session");

  auto request = std::make_unique<network::ResourceRequest>();
  request->url = url;
  request->method = "POST";
  request->headers.SetHeader("Content-Type", "application/json");

  base::DictValue body;
  body.Set("title", "Maho AI Session");
  std::string body_json;
  base::JSONWriter::Write(body, &body_json);

  session_loader_ = network::SimpleURLLoader::Create(
      std::move(request), kOpenCodeTrafficAnnotation);
  session_loader_->AttachStringForUpload(body_json, "application/json");

  session_loader_->DownloadToStringOfUnboundedSizeUntilCrashAndDie(
      url_loader_factory_.get(),
      base::BindOnce(&MahoOpenCodeAdapter::OnCreateSessionResponse,
                     weak_factory_.GetWeakPtr()));
}

void MahoOpenCodeAdapter::OnCreateSessionResponse(
    std::optional<std::string> body) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!body || body->empty()) {
    if (current_on_event_) {
      MahoAiRuntimeEvent event;
      event.type = MahoAiRuntimeEventType::kError;
      event.text = "Failed to create OpenCode session: no response.";
      current_on_event_.Run(std::move(event));
    }
    return;
  }

  auto parsed = base::JSONReader::Read(*body, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    if (current_on_event_) {
      MahoAiRuntimeEvent event;
      event.type = MahoAiRuntimeEventType::kError;
      event.text = "Failed to parse OpenCode session response.";
      current_on_event_.Run(std::move(event));
    }
    return;
  }

  const std::string* id = parsed->GetDict().FindString("id");
  if (!id || id->empty()) {
    const std::string* sid = parsed->GetDict().FindString("sessionId");
    if (sid && !sid->empty()) {
      session_id_ = *sid;
    } else {
      if (current_on_event_) {
        MahoAiRuntimeEvent event;
        event.type = MahoAiRuntimeEventType::kError;
        event.text = "OpenCode session created but no session ID returned.";
        current_on_event_.Run(std::move(event));
      }
      return;
    }
  } else {
    session_id_ = *id;
  }

  EmitConnectionState("session_created");

  if (!pending_prompt_.empty()) {
    std::string prompt = std::move(pending_prompt_);
    pending_prompt_.clear();
    bool is_direct = pending_is_direct_message_;
    pending_is_direct_message_ = false;
    if (is_direct) {
      SubmitDirectMessage(prompt);
    } else {
      SubmitPromptToSession(prompt);
    }
  }
}

void MahoOpenCodeAdapter::SubmitPromptToSession(const std::string& prompt) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  GURL url = GetServerBaseUrl().Resolve(
      "session/" + session_id_ + "/prompt_async");

  auto request = std::make_unique<network::ResourceRequest>();
  request->url = url;
  request->method = "POST";
  request->headers.SetHeader("Content-Type", "application/json");

  base::DictValue body;
  body.Set("agent", "build");

  std::string runtime_model =
      prefs_->GetString(maho::ai_prefs::kRuntimeModel);
  if (!runtime_model.empty()) {
    const auto pieces = base::SplitStringPiece(
        runtime_model, "/", base::TRIM_WHITESPACE,
        base::SPLIT_WANT_NONEMPTY);
    if (pieces.size() >= 2) {
      base::DictValue model;
      model.Set("providerID", std::string(pieces.front()));
      model.Set("modelID", std::string(runtime_model.substr(
          std::string(pieces.front()).size() + 1)));
      body.Set("model", std::move(model));
    }
  }

  base::ListValue parts;
  base::DictValue text_part;
  text_part.Set("type", "text");
  text_part.Set("text", prompt);
  parts.Append(std::move(text_part));
  body.Set("parts", std::move(parts));

  std::string body_json;
  base::JSONWriter::Write(body, &body_json);

  prompt_loader_ = network::SimpleURLLoader::Create(
      std::move(request), kOpenCodeTrafficAnnotation);
  prompt_loader_->AttachStringForUpload(body_json, "application/json");

  prompt_loader_->DownloadToStringOfUnboundedSizeUntilCrashAndDie(
      url_loader_factory_.get(),
      base::BindOnce(&MahoOpenCodeAdapter::OnPromptResponse,
                     weak_factory_.GetWeakPtr()));
}

void MahoOpenCodeAdapter::OnPromptResponse(
    std::optional<std::string> body) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

void MahoOpenCodeAdapter::SubmitDirectMessage(const std::string& prompt) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  awaiting_direct_response_ = true;

  GURL url = GetServerBaseUrl().Resolve(
      "session/" + session_id_ + "/message");

  auto request = std::make_unique<network::ResourceRequest>();
  request->url = url;
  request->method = "POST";
  request->headers.SetHeader("Content-Type", "application/json");

  base::DictValue body;

  std::string runtime_model =
      prefs_->GetString(maho::ai_prefs::kRuntimeModel);
  if (!runtime_model.empty()) {
    const auto pieces = base::SplitStringPiece(
        runtime_model, "/", base::TRIM_WHITESPACE,
        base::SPLIT_WANT_NONEMPTY);
    if (pieces.size() >= 2) {
      base::DictValue model;
      model.Set("providerID", std::string(pieces.front()));
      model.Set("modelID", std::string(runtime_model.substr(
          std::string(pieces.front()).size() + 1)));
      body.Set("model", std::move(model));
    }
  }

  base::ListValue parts;
  base::DictValue text_part;
  text_part.Set("type", "text");
  text_part.Set("text", prompt);
  parts.Append(std::move(text_part));
  body.Set("parts", std::move(parts));

  std::string body_json;
  base::JSONWriter::Write(body, &body_json);

  direct_message_loader_ = network::SimpleURLLoader::Create(
      std::move(request), kOpenCodeTrafficAnnotation);
  direct_message_loader_->AttachStringForUpload(body_json, "application/json");

  direct_message_loader_->DownloadToStringOfUnboundedSizeUntilCrashAndDie(
      url_loader_factory_.get(),
      base::BindOnce(&MahoOpenCodeAdapter::OnDirectMessageResponse,
                     weak_factory_.GetWeakPtr()));
}

void MahoOpenCodeAdapter::OnDirectMessageResponse(
    std::optional<std::string> body) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  awaiting_direct_response_ = false;

  if (!current_on_event_) {
    return;
  }

  if (!body || body->empty()) {
    MahoAiRuntimeEvent event;
    event.type = MahoAiRuntimeEventType::kError;
    event.text = "No response from the AI model.";
    current_on_event_.Run(std::move(event));
    return;
  }

  auto parsed = base::JSONReader::Read(*body, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    MahoAiRuntimeEvent event;
    event.type = MahoAiRuntimeEventType::kError;
    event.text = "Failed to parse AI response.";
    current_on_event_.Run(std::move(event));
    return;
  }

  const base::ListValue* parts = parsed->GetDict().FindList("parts");
  if (!parts) {
    MahoAiRuntimeEvent event;
    event.type = MahoAiRuntimeEventType::kError;
    event.text = "AI response contained no content.";
    current_on_event_.Run(std::move(event));
    return;
  }

  std::string assembled_text;
  for (const auto& part_val : *parts) {
    if (!part_val.is_dict()) {
      continue;
    }
    const base::DictValue& part = part_val.GetDict();
    const std::string* type = part.FindString("type");
    if (!type || *type != "text") {
      continue;
    }
    const std::string* text = part.FindString("text");
    if (text && !text->empty()) {
      if (!assembled_text.empty()) {
        assembled_text += "\n\n";
      }
      assembled_text += *text;
    }
  }

  if (assembled_text.empty()) {
    MahoAiRuntimeEvent event;
    event.type = MahoAiRuntimeEventType::kError;
    event.text = "AI returned no readable text.";
    current_on_event_.Run(std::move(event));
    return;
  }

  {
    MahoAiRuntimeEvent event;
    event.type = MahoAiRuntimeEventType::kAssistantToken;
    event.text = assembled_text;
    current_on_event_.Run(std::move(event));
  }
  {
    MahoAiRuntimeEvent event;
    event.type = MahoAiRuntimeEventType::kTurnComplete;
    event.text = assembled_text;
    current_on_event_.Run(std::move(event));
  }
}

void MahoOpenCodeAdapter::OnSseDataReceived(std::string chunk) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  sse_buffer_ += chunk;

  size_t pos;
  while ((pos = sse_buffer_.find("\n\n")) != std::string::npos) {
    std::string event_block = sse_buffer_.substr(0, pos);
    sse_buffer_.erase(0, pos + 2);

    std::string event_type;
    std::string data;

    for (const auto& line : base::SplitStringPiece(
             event_block, "\n", base::KEEP_WHITESPACE,
             base::SPLIT_WANT_NONEMPTY)) {
      if (base::StartsWith(line, "event:")) {
        event_type = std::string(line.substr(6));
        base::TrimWhitespaceASCII(event_type, base::TRIM_ALL, &event_type);
      } else if (base::StartsWith(line, "data:")) {
        std::string d = std::string(line.substr(5));
        base::TrimWhitespaceASCII(d, base::TRIM_ALL, &d);
        if (!data.empty()) {
          data += "\n";
        }
        data += d;
      }
    }

    if (!data.empty() || !event_type.empty()) {
      ProcessSseEvent(event_type, data);
    }
  }
}

void MahoOpenCodeAdapter::OnSseComplete(bool success) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  sse_loader_.reset();
  sse_consumer_.reset();
  sse_buffer_.clear();
  sse_connected_ = false;

  EmitConnectionState("disconnected");
}

void MahoOpenCodeAdapter::ProcessSseEvent(const std::string& event_type,
                                            const std::string& data) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (event_type == "server.connected") {
    sse_connected_ = true;
    if (current_on_event_) {
      EmitConnectionState("connected");
    }
    return;
  }

  auto parsed = base::JSONReader::Read(data, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return;
  }

  std::string resolved_type = event_type;
  const base::DictValue* payload = &parsed->GetDict();
  if (resolved_type.empty()) {
    const std::string* wrapped_type = payload->FindString("type");
    const base::DictValue* wrapped_properties = payload->FindDict("properties");
    if (!wrapped_type || !wrapped_properties) {
      return;
    }
    resolved_type = *wrapped_type;
    payload = wrapped_properties;
  }

  const std::string* payload_session_id = payload->FindString("sessionID");
  if (!session_id_.empty() && payload_session_id && *payload_session_id != session_id_) {
    return;
  }

  if (!current_on_event_) {
    return;
  }

  if (awaiting_direct_response_ && resolved_type != "session.error") {
    return;
  }

  // Handle streaming text deltas — these carry incremental token content.
  if (resolved_type == "message.part.delta") {
    const std::string* field = payload->FindString("field");
    const std::string* delta = payload->FindString("delta");
    const std::string* part_id = payload->FindString("partID");
    if (field && *field == "text" && delta && !delta->empty()) {
      if (part_id) {
        auto it = part_types_.find(*part_id);
        if (it != part_types_.end() && it->second != "text") {
          return;
        }
      }
      accumulated_text_ += *delta;
      MahoAiRuntimeEvent event;
      event.type = MahoAiRuntimeEventType::kAssistantToken;
      event.text = *delta;
      current_on_event_.Run(std::move(event));
    }
    return;
  }

  if (resolved_type == "message.part.updated") {
    const base::DictValue* part = payload->FindDict("part");
    if (!part) {
      return;
    }

    const std::string* part_type = part->FindString("type");
    if (!part_type) {
      return;
    }

    const std::string* part_id = part->FindString("id");
    if (part_id && part_type) {
      part_types_[*part_id] = *part_type;
    }

    if (*part_type == "text") {
      // The full text is available in part.text. We use it to reconcile
      // accumulated_text_ in case any deltas were missed, but do NOT emit
      // a token event here — streaming is handled by message.part.delta.
      const std::string* text = part->FindString("text");
      if (text && !text->empty()) {
        accumulated_text_ = *text;
      }
      return;
    }

    if (*part_type == "tool") {
      const std::string* tool_name = part->FindString("tool");
      const std::string* call_id = part->FindString("callID");
      const base::DictValue* state = part->FindDict("state");
      const std::string* status = state ? state->FindString("status") : nullptr;

      if (!status) {
        return;
      }

      if ((*status == "pending" || *status == "running") &&
          tool_name && call_id && IsBrowserLocalTool(*tool_name)) {
        MahoAiRuntimeEvent request_event;
        request_event.type = MahoAiRuntimeEventType::kToolRequest;
        if (tool_name) {
          request_event.payload.Set("tool_name", *tool_name);
        }
        if (call_id) {
          request_event.payload.Set("call_id", *call_id);
        }
        request_event.payload.Set("source", "browser_local");
        current_on_event_.Run(std::move(request_event));

        base::DictValue arguments;
        if (state) {
          const base::DictValue* input = state->FindDict("input");
          if (input) {
            arguments = input->Clone();
          }
        }
        ExecuteLocalBrowserTool(*tool_name, *call_id, arguments);
        return;
      }

      MahoAiRuntimeEvent event;
      if (*status == "pending" || *status == "running") {
        event.type = MahoAiRuntimeEventType::kToolRequest;
      } else if (*status == "completed" || *status == "error") {
        event.type = MahoAiRuntimeEventType::kToolResult;
      } else {
        return;
      }

      if (tool_name) {
        event.payload.Set("tool_name", *tool_name);
      }
      if (call_id) {
        event.payload.Set("call_id", *call_id);
      }
      if (state) {
        if (const base::DictValue* input = state->FindDict("input")) {
          std::string input_json;
          base::JSONWriter::Write(base::Value(input->Clone()), &input_json);
          event.payload.Set("arguments_json", input_json);
        }
        if (const std::string* output = state->FindString("output")) {
          event.payload.Set("output", *output);
          event.text = *output;
        }
        if (const std::string* error = state->FindString("error")) {
          event.payload.Set("error", *error);
          event.text = *error;
        }
        event.payload.Set("success", *status == "completed");
      }

      current_on_event_.Run(std::move(event));
      return;
    }
  }

  if (resolved_type == "session.status") {
    const base::DictValue* status = payload->FindDict("status");
    const std::string* status_type = status ? status->FindString("type") : nullptr;
    if (status_type) {
      EmitConnectionState(*status_type);
      if (*status_type == "idle") {
        MahoAiRuntimeEvent event;
        event.type = MahoAiRuntimeEventType::kTurnComplete;
        event.text = accumulated_text_;
        current_on_event_.Run(std::move(event));
        accumulated_text_.clear();
        part_types_.clear();
      }
    }
    return;
  }

  if (resolved_type == "session.completed" ||
      resolved_type == "message.completed" ||
      resolved_type == "session.idle") {
    MahoAiRuntimeEvent event;
    event.type = MahoAiRuntimeEventType::kTurnComplete;
    event.text = accumulated_text_;
    current_on_event_.Run(std::move(event));
    accumulated_text_.clear();
    part_types_.clear();
    return;
  }

  if (resolved_type == "session.error") {
    const base::DictValue* error_dict = payload->FindDict("error");
    const std::string* error = error_dict ? error_dict->FindString("message") : nullptr;
    const std::string* message = payload->FindString("message");
    MahoAiRuntimeEvent event;
    event.type = MahoAiRuntimeEventType::kError;
    event.text = error ? *error : (message ? *message : "OpenCode session error.");
    current_on_event_.Run(std::move(event));
    return;
  }

  if (resolved_type == "permission.asked") {
    const std::string* pid = payload->FindString("id");
    const std::string* desc = payload->FindString("description");
    const std::string* title = payload->FindString("title");

    std::string approval_id = pid ? *pid : "";
    pending_approval_id_ = approval_id;

    EmitConnectionState("paused_for_approval");

    approval_timeout_timer_.Start(
        FROM_HERE, kApprovalTimeout,
        base::BindOnce(&MahoOpenCodeAdapter::OnApprovalTimeout,
                       weak_factory_.GetWeakPtr()));

    MahoAiRuntimeEvent event;
    event.type = MahoAiRuntimeEventType::kApprovalRequest;
    event.text = desc ? *desc : (title ? *title : "Permission requested.");
    event.payload.Set("approval_id", approval_id);
    event.payload.Set("description", event.text);

    const std::string* method = payload->FindString("method");
    if (method) {
      event.payload.Set("method", *method);
    }
    const std::string* risk = payload->FindString("risk");
    if (risk) {
      event.payload.Set("risk", *risk);
    }
    const base::DictValue* input = payload->FindDict("input");
    if (input) {
      const std::string* tool_name = input->FindString("tool");
      if (tool_name) {
        event.payload.Set("related_tool_name", *tool_name);
      }
      const std::string* tool_args = input->FindString("arguments");
      if (tool_args) {
        event.payload.Set("related_tool_arguments", *tool_args);
      }
    }

    current_on_event_.Run(std::move(event));
    return;
  }

  if (resolved_type == "permission.replied") {
    const std::string* request_id = payload->FindString("requestID");
    const std::string* reply = payload->FindString("reply");

    pending_approval_id_.clear();
    approval_timeout_timer_.Stop();

    EmitConnectionState("resumed");

    bool was_approved = reply && (*reply == "once" || *reply == "always" ||
                                  *reply == "yes" || *reply == "approve");

    MahoAiRuntimeEvent event;
    event.type = MahoAiRuntimeEventType::kApprovalResult;
    event.text = reply ? *reply : "permission_replied";
    event.payload.Set("approval_id", request_id ? *request_id : "");
    event.payload.Set("approved", was_approved);
    event.payload.Set("description", event.text);
    current_on_event_.Run(std::move(event));
    return;
  }
}

void MahoOpenCodeAdapter::EmitConnectionState(const std::string& state) {
  if (!current_on_event_) {
    return;
  }
  MahoAiRuntimeEvent event;
  event.type = MahoAiRuntimeEventType::kConnectionStateChanged;
  event.text = state;
  current_on_event_.Run(std::move(event));
}

bool MahoOpenCodeAdapter::IsPageIntent(
    maho_ai::mojom::ChatIntent intent) const {
  return intent == maho_ai::mojom::ChatIntent::kAnalyzeCurrentPage ||
         intent == maho_ai::mojom::ChatIntent::kExplainCurrentPage ||
         intent == maho_ai::mojom::ChatIntent::kSummarizeCurrentPage;
}

std::string MahoOpenCodeAdapter::BuildIntentInstruction(
    maho_ai::mojom::ChatIntent intent) {
  switch (intent) {
    case maho_ai::mojom::ChatIntent::kAnalyzeCurrentPage:
      return "Analyze the following web page content. Identify key themes, "
             "structure, and notable details.";
    case maho_ai::mojom::ChatIntent::kExplainCurrentPage:
      return "Explain the following web page content in clear, simple terms. "
             "Break down any complex concepts.";
    case maho_ai::mojom::ChatIntent::kSummarizeCurrentPage:
      return "Summarize the following web page content concisely. "
             "Highlight the most important points.";
    case maho_ai::mojom::ChatIntent::kFreeform:
      return std::string();
  }
  return std::string();
}

void MahoOpenCodeAdapter::OnPageContextForIntent(
    const std::string& message,
    maho_ai::mojom::ChatIntent chat_intent,
    RuntimeEventCallback on_event,
    MahoAiPageContextExtractor::PageContextResult page_context) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!page_context.HasUsableContent()) {
    MahoAiRuntimeEvent event;
    event.type = MahoAiRuntimeEventType::kError;
    switch (page_context.extraction_status) {
      case MahoAiPageContextExtractor::PageContextResult::Status::kNoActiveTab:
        event.text = "There is no active page to analyze right now.";
        break;
      case MahoAiPageContextExtractor::PageContextResult::Status::kCannotAccess:
        if (!page_context.url.empty() &&
            (base::StartsWith(page_context.url, "chrome://") ||
             base::StartsWith(page_context.url, "chrome-untrusted://") ||
             base::StartsWith(page_context.url, "devtools://") ||
             base::StartsWith(page_context.url, "about:"))) {
          event.text =
              "Maho AI cannot analyze internal browser pages. "
              "Navigate to a web page and try again.";
        } else {
          event.text =
              "Maho AI couldn't access the current page content. "
              "Try again once the page finishes loading.";
        }
        break;
      default:
        event.text =
            "Maho AI couldn't extract readable content from the current page.";
        break;
    }
    on_event.Run(std::move(event));
    return;
  }

  std::string instruction = BuildIntentInstruction(chat_intent);
  std::string prompt = instruction + "\n\n"
      "[Page Context]\n"
      "Title: " + page_context.title + "\n"
      "URL: " + page_context.url + "\n"
      "Content:\n" + page_context.GetLegacyText();

  if (!message.empty()) {
    prompt += "\n\n[User Message]\n" + message;
  }

  if (session_id_.empty()) {
    pending_prompt_ = prompt;
    CreateSession();
  } else {
    SubmitPromptToSession(prompt);
  }
}

std::string MahoOpenCodeAdapter::BuildBrowserContextPrefix() {
  if (!browser_) {
    return std::string();
  }

  content::WebContents* web_contents =
      browser_->tab_strip_model()->GetActiveWebContents();
  if (!web_contents) {
    return std::string();
  }

  std::string url = web_contents->GetLastCommittedURL().spec();
  std::u16string title16 = web_contents->GetTitle();
  std::string title = base::UTF16ToUTF8(title16);

  if (url.empty() || url == "about:blank" || url == "chrome://newtab/") {
    return std::string();
  }

  return "[Browser Context]\nActive tab: " + title + "\nURL: " + url;
}

bool MahoOpenCodeAdapter::IsBrowserLocalTool(
    const std::string& tool_name) const {
  return MahoBrowserToolRegistry::FindToolSchema(tool_name) != nullptr;
}

void MahoOpenCodeAdapter::ExecuteLocalBrowserTool(
    const std::string& tool_name,
    const std::string& call_id,
    const base::DictValue& arguments) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (local_tool_round_count_ >= kMaxLocalToolRounds) {
    LOG(WARNING) << "MahoOpenCodeAdapter: max local tool rounds ("
                 << kMaxLocalToolRounds << ") reached, stopping loop.";
    MahoAiRuntimeEvent event;
    event.type = MahoAiRuntimeEventType::kToolResult;
    event.payload.Set("tool_name", tool_name);
    event.payload.Set("call_id", call_id);
    event.payload.Set("success", false);
    event.payload.Set("error", "Max local tool execution rounds exceeded.");
    event.text = "Max local tool execution rounds exceeded.";
    if (current_on_event_) {
      current_on_event_.Run(std::move(event));
    }
    return;
  }

  ++local_tool_round_count_;

  tool_executor_->Execute(
      tool_name, arguments,
      base::BindOnce(&MahoOpenCodeAdapter::OnLocalToolResult,
                     weak_factory_.GetWeakPtr(), tool_name, call_id));
}

void MahoOpenCodeAdapter::OnLocalToolResult(
    const std::string& tool_name,
    const std::string& call_id,
    base::DictValue result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  MahoAiRuntimeEvent event;
  event.type = MahoAiRuntimeEventType::kToolResult;
  event.payload.Set("tool_name", tool_name);
  event.payload.Set("call_id", call_id);
  event.payload.Set("source", "browser_local");

  const std::string* error = result.FindString("error");
  if (error && !error->empty()) {
    event.payload.Set("success", false);
    event.payload.Set("error", *error);
    event.text = *error;
  } else {
    event.payload.Set("success", true);
    std::string result_json;
    base::JSONWriter::Write(base::Value(result.Clone()), &result_json);
    event.payload.Set("output", result_json);
    event.text = result_json;
  }

  if (current_on_event_) {
    current_on_event_.Run(std::move(event));
  }

  SubmitToolResultFollowUp(tool_name, call_id, result);
}

void MahoOpenCodeAdapter::SubmitToolResultFollowUp(
    const std::string& tool_name,
    const std::string& call_id,
    const base::DictValue& result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (session_id_.empty()) {
    return;
  }

  std::string result_json;
  base::JSONWriter::Write(base::Value(result.Clone()), &result_json);

  std::string follow_up =
      "[Maho Browser Tool Result]\n"
      "tool: " + tool_name + "\n"
      "call_id: " + call_id + "\n"
      "result:\n" + result_json;

  SubmitPromptToSession(follow_up);
}

void MahoOpenCodeAdapter::OnApprovalTimeout() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (pending_approval_id_.empty()) {
    return;
  }

  std::string timed_out_id = std::move(pending_approval_id_);
  pending_approval_id_.clear();

  RespondToApproval(timed_out_id, false);

  if (current_on_event_) {
    MahoAiRuntimeEvent event;
    event.type = MahoAiRuntimeEventType::kError;
    event.text = "Approval timed out after " +
        base::NumberToString(kApprovalTimeout.InSeconds()) +
        " seconds. Automatically denied.";
    current_on_event_.Run(std::move(event));
  }
}
