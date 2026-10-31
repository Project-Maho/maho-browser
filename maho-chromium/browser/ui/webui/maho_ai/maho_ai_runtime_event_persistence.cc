// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_ai/maho_ai_runtime_event_persistence.h"

#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>

#include "base/check.h"
#include "base/json/json_reader.h"
#include "maho/browser/ai/maho_ai_security_utils.h"
#include "maho/browser/ai/maho_credential_redaction.h"

namespace maho::ai {
namespace {

constexpr char kSafeCredentialFailureText[] =
    "Your saved AI credential could not be used.";

constexpr size_t kReplayReserveBytes = 8192;
constexpr size_t kMergedTextBytes = 64 * 1024;
constexpr char kReplayCapacityExceeded[] = "replay_capacity_exceeded";

size_t TextBytes(std::string_view text) {
  size_t bytes = 0;
  for (size_t i = 0; i < text.size(); ++i) {
    const auto c = static_cast<unsigned char>(text[i]);
    if (c == 0xe2 && i + 2 < text.size() &&
        static_cast<unsigned char>(text[i + 1]) == 0x80 &&
        (static_cast<unsigned char>(text[i + 2]) == 0xa8 ||
         static_cast<unsigned char>(text[i + 2]) == 0xa9)) {
      bytes += 6;
      i += 2;
    } else if (c == '"' || c == '\\' || c == '\n' || c == '\r' ||
               c == '\t' || c == '\b' || c == '\f') {
      bytes += 2;
    } else {
      bytes += c < 0x20 || c == '<' ? 6 : 1;
    }
  }
  return bytes;
}

size_t OptionalTextBytes(const std::optional<std::string>& text) {
  return text ? TextBytes(*text) : 0;
}

size_t ToolBytes(const maho_ai::mojom::ToolCallInfo& tool) {
  return sizeof(tool) + 128 + TextBytes(tool.call_id) +
         TextBytes(tool.tool_name) + TextBytes(tool.arguments_json);
}

size_t EventBytes(const maho_ai::mojom::RuntimeEvent& event) {
  size_t bytes = sizeof(event) + 128 + TextBytes(event.session_id) +
                 OptionalTextBytes(event.request_id) +
                 OptionalTextBytes(event.text);
  if (event.tool_call) {
    bytes += ToolBytes(*event.tool_call);
  }
  if (event.tool_result) {
    const auto& result = *event.tool_result;
    bytes += sizeof(result) + 128 + TextBytes(result.call_id) +
             TextBytes(result.output) + OptionalTextBytes(result.error_message);
  }
  if (event.approval_request) {
    const auto& approval = *event.approval_request;
    bytes += sizeof(approval) + 128 + TextBytes(approval.approval_id) +
             TextBytes(approval.description);
    if (approval.related_tool_call) {
      bytes += ToolBytes(*approval.related_tool_call);
    }
  }
  if (event.approval_result) {
    const auto& approval = *event.approval_result;
    bytes += sizeof(approval) + 128 + TextBytes(approval.approval_id) +
             OptionalTextBytes(approval.reason);
  }
  if (event.interaction_request) {
    const auto& request = *event.interaction_request;
    bytes += sizeof(request) + 128 + TextBytes(request.request_id) +
             TextBytes(request.question) + OptionalTextBytes(request.artifact_ref) +
             OptionalTextBytes(request.state);
    for (const auto& option : request.options) {
      bytes += sizeof(*option) + 128 + TextBytes(option->id) +
               TextBytes(option->label) + OptionalTextBytes(option->description);
    }
  }
  if (event.browser_context) {
    const auto& context = *event.browser_context;
    bytes += sizeof(context) + 128 + TextBytes(context.url) +
             TextBytes(context.title) + TextBytes(context.content_snippet) +
             TextBytes(context.label);
    for (const auto& warning : context.warnings) {
      bytes += sizeof(warning) + 16 + TextBytes(warning);
    }
  }
  if (event.artifact) {
    const auto& artifact = *event.artifact;
    bytes += sizeof(artifact) + 128 + TextBytes(artifact.artifact_id) +
             TextBytes(artifact.session_id) + TextBytes(artifact.display_name) +
             TextBytes(artifact.mime_type);
  }
  if (event.replay_window) {
    bytes += sizeof(*event.replay_window) + 128;
  }
  return bytes;
}

bool IsPlainDelta(const maho_ai::mojom::RuntimeEvent& event) {
  using Kind = maho_ai::mojom::RuntimeEventKind;
  return (event.kind == Kind::kAssistantToken ||
          event.kind == Kind::kAssistantThinking) &&
         event.text.has_value() && !event.tool_call && !event.tool_result &&
         !event.approval_request && !event.approval_result &&
         !event.interaction_request && !event.browser_context &&
         !event.artifact && !event.credential_error_code.has_value();
}

const char* CredentialErrorCodeToPersistedSymbol(
    maho_ai::mojom::CredentialErrorCode code) {
  switch (code) {
    case maho_ai::mojom::CredentialErrorCode::kProviderNotConfigured:
      return "provider_not_configured";
    case maho_ai::mojom::CredentialErrorCode::kCredentialUnusable:
      return "credential_unusable";
    case maho_ai::mojom::CredentialErrorCode::kSecureStoreUnavailable:
      return "secure_store_unavailable";
    case maho_ai::mojom::CredentialErrorCode::kCredentialDecryptFailed:
      return "credential_decrypt_failed";
    case maho_ai::mojom::CredentialErrorCode::kManagedAuthUnavailable:
      return "managed_auth_unavailable";
    case maho_ai::mojom::CredentialErrorCode::kUnsupportedProvider:
      return "unsupported_provider";
    case maho_ai::mojom::CredentialErrorCode::kGeneric:
      return "generic";
  }
  return "generic";
}

}  // namespace

RuntimeReplayBudget::RuntimeReplayBudget() = default;
RuntimeReplayBudget::~RuntimeReplayBudget() = default;

void RuntimeReplayBudget::Reset() {
  bytes = 0;
  event_bytes.clear();
  omitted_turns = 0;
  omitted_through_sequence = 0;
  exhausted = false;
}

ReplayAdmission AppendBoundedReplayEvent(
    std::vector<maho_ai::mojom::RuntimeEventPtr>& events,
    RuntimeReplayBudget& budget,
    maho_ai::mojom::RuntimeEventPtr& event) {
  using Kind = maho_ai::mojom::RuntimeEventKind;
  CHECK_EQ(events.size(), budget.event_bytes.size());
  if (event->sequence_start == 0) {
    event->sequence_start = event->sequence;
  }
  if (event->kind == Kind::kUserPrompt) {
    budget.exhausted = false;
  } else if (budget.exhausted) {
    return ReplayAdmission::kIgnored;
  }
  const bool merge =
      !events.empty() && IsPlainDelta(*event) && IsPlainDelta(*events.back()) &&
      events.back()->kind == event->kind &&
      events.back()->session_id == event->session_id &&
      events.back()->request_id == event->request_id &&
      events.back()->sequence + 1 == event->sequence_start &&
      events.back()->text->size() <= kMergedTextBytes &&
      event->text->size() <= kMergedTextBytes - events.back()->text->size();
  const size_t added_bytes = merge ? TextBytes(*event->text) : EventBytes(*event);
  while (added_bytes > kReplayBytes - kReplayReserveBytes ||
         budget.bytes > kReplayBytes - kReplayReserveBytes - added_bytes ||
         events.size() + (merge ? 0 : 1) > kReplayRecords - 2) {
    size_t end = 0;
    std::set<std::string> pending_tools;
    std::set<std::string> pending_approvals;
    for (size_t i = budget.omitted_turns ? 1 : 0; i < events.size(); ++i) {
      const auto& item = *events[i];
      if (item.tool_call) {
        pending_tools.insert(item.tool_call->call_id);
      }
      if (item.tool_result) {
        pending_tools.erase(item.tool_result->call_id);
      }
      if (item.approval_request) {
        pending_approvals.insert(item.approval_request->approval_id);
      }
      if (item.approval_result) {
        pending_approvals.erase(item.approval_result->approval_id);
      }
      if (item.kind == Kind::kError ||
          (item.kind == Kind::kTurnComplete && pending_tools.empty() &&
           pending_approvals.empty())) {
        end = i + 1;
        break;
      }
    }
    if (end == 0) {
      auto failure = maho_ai::mojom::RuntimeEvent::New();
      failure->kind = Kind::kError;
      failure->session_id = event->session_id;
      failure->request_id = event->request_id;
      failure->sequence = event->sequence;
      failure->sequence_start = event->sequence;
      failure->timestamp = event->timestamp;
      failure->text = kReplayCapacityExceeded;
      if (budget.omitted_turns) {
        failure->replay_window = maho_ai::mojom::ReplayWindowInfo::New(
            budget.omitted_turns, budget.omitted_through_sequence);
      }
      const size_t bytes = EventBytes(*failure);
      CHECK_LE(bytes, kReplayReserveBytes);
      budget.bytes += bytes;
      budget.event_bytes.push_back(bytes);
      events.push_back(failure->Clone());
      event = std::move(failure);
      budget.exhausted = true;
      return ReplayAdmission::kExhausted;
    }
    const uint64_t removed_sequence = events[end - 1]->sequence;
    for (size_t i = 0; i < end; ++i) {
      budget.bytes -= budget.event_bytes.front();
      budget.event_bytes.pop_front();
    }
    events.erase(events.begin(), events.begin() + end);
    ++budget.omitted_turns;
    budget.omitted_through_sequence = removed_sequence;
    auto window = maho_ai::mojom::RuntimeEvent::New();
    window->kind = Kind::kSessionStatus;
    window->session_id = event->session_id;
    window->sequence = removed_sequence;
    window->sequence_start = removed_sequence;
    window->timestamp = event->timestamp;
    window->replay_window = maho_ai::mojom::ReplayWindowInfo::New(
        budget.omitted_turns, removed_sequence);
    window->text =
        "{\"history_window\":{\"omitted_turns\":" +
        std::to_string(budget.omitted_turns) + ",\"omitted_through_sequence\":" +
        std::to_string(removed_sequence) + "}}";
    const size_t bytes = EventBytes(*window);
    budget.bytes += bytes;
    budget.event_bytes.push_front(bytes);
    events.insert(events.begin(), std::move(window));
  }
  budget.bytes += added_bytes;
  if (merge) {
    events.back()->text->append(*event->text);
    events.back()->sequence = event->sequence;
    events.back()->timestamp = event->timestamp;
    budget.event_bytes.back() += added_bytes;
  } else {
    budget.event_bytes.push_back(added_bytes);
    events.push_back(event->Clone());
  }
  if (event->kind == Kind::kError &&
      event->text == kReplayCapacityExceeded) {
    budget.exhausted = true;
  }
  if (budget.omitted_turns) {
    event->replay_window = maho_ai::mojom::ReplayWindowInfo::New(
        budget.omitted_turns, budget.omitted_through_sequence);
  }
  return ReplayAdmission::kAccepted;
}

void NormalizeReplayWindow(
    std::vector<maho_ai::mojom::RuntimeEventPtr>& events,
    RuntimeReplayBudget& budget) {
  auto previous = std::move(events);
  events.clear();
  budget.Reset();
  for (auto& event : previous) {
    AppendBoundedReplayEvent(events, budget, event);
    if (events.size() == 1 &&
        event->kind == maho_ai::mojom::RuntimeEventKind::kSessionStatus &&
        event->text) {
      const auto metadata =
          base::JSONReader::ReadDict(*event->text, base::JSON_PARSE_RFC);
      const auto* window = metadata ? metadata->FindDict("history_window") : nullptr;
      const int omitted =
          window ? window->FindInt("omitted_turns").value_or(0) : 0;
      if (omitted > 0) {
        budget.omitted_turns = omitted;
        budget.omitted_through_sequence = event->sequence;
      }
    }
  }
}

base::DictValue SerializeRuntimeEventForPersistence(
    const maho_ai::mojom::RuntimeEvent& event) {
  base::DictValue dict;
  dict.Set("kind", static_cast<int>(event.kind));
  dict.Set("session_id", event.session_id);
  if (event.request_id.has_value()) {
    dict.Set("request_id", *event.request_id);
  }
  dict.Set("sequence", static_cast<double>(event.sequence));
  dict.Set("sequence_start",
           static_cast<double>(event.sequence_start ? event.sequence_start
                                                    : event.sequence));
  dict.Set("timestamp", event.timestamp);
  if (event.replay_window) {
    base::DictValue window;
    window.Set("omitted_turns",
               static_cast<double>(event.replay_window->omitted_turns));
    window.Set("omitted_through_sequence", static_cast<double>(
        event.replay_window->omitted_through_sequence));
    dict.Set("replay_window", std::move(window));
  }
  if (event.credential_error_code.has_value()) {
    dict.Set("credential_error_code",
             CredentialErrorCodeToPersistedSymbol(
                 *event.credential_error_code));
    dict.Set("text", kSafeCredentialFailureText);
  } else if (event.text.has_value()) {
    dict.Set("text", credential_redaction::RedactCredentialText(*event.text));
  }
  if (event.tool_call) {
    base::DictValue tool_call;
    tool_call.Set("call_id", event.tool_call->call_id);
    tool_call.Set("tool_name", event.tool_call->tool_name);
    tool_call.Set("arguments_json", credential_redaction::RedactJsonOrText(
                                        event.tool_call->arguments_json));
    tool_call.Set("status", static_cast<int>(event.tool_call->status));
    dict.Set("tool_call", std::move(tool_call));
  }
  if (event.tool_result) {
    base::DictValue tool_result;
    tool_result.Set("call_id", event.tool_result->call_id);
    tool_result.Set("success", event.tool_result->success);
    tool_result.Set("output", credential_redaction::RedactJsonOrText(
                                  event.tool_result->output));
    if (event.tool_result->error_message.has_value()) {
      tool_result.Set("error_message", credential_redaction::RedactCredentialText(
                                           *event.tool_result->error_message));
    }
    dict.Set("tool_result", std::move(tool_result));
  }
  if (event.approval_request) {
    base::DictValue approval_request;
    approval_request.Set("approval_id", event.approval_request->approval_id);
    approval_request.Set(
        "description", credential_redaction::RedactCredentialText(
                           event.approval_request->description));
    approval_request.Set(
        "approval_policy",
        static_cast<int>(event.approval_request->approval_policy));
    approval_request.Set("sensitivity",
                         static_cast<int>(event.approval_request->sensitivity));
    approval_request.Set("state",
                         static_cast<int>(event.approval_request->state));
    approval_request.Set("page_derived_justification",
                         event.approval_request->page_derived_justification);
    if (event.approval_request->related_tool_call) {
      base::DictValue related_tool_call;
      related_tool_call.Set(
          "tool_name", event.approval_request->related_tool_call->tool_name);
      related_tool_call.Set("arguments_json", "");
      approval_request.Set("related_tool_call", std::move(related_tool_call));
    }
    dict.Set("approval_request", std::move(approval_request));
  }
  if (event.approval_result) {
    base::DictValue approval_result;
    approval_result.Set("approval_id", event.approval_result->approval_id);
    approval_result.Set("approved", event.approval_result->approved);
    if (event.approval_result->reason.has_value()) {
      approval_result.Set("reason", credential_redaction::RedactCredentialText(
                                        *event.approval_result->reason));
    }
    approval_result.Set(
        "approval_policy",
        static_cast<int>(event.approval_result->approval_policy));
    approval_result.Set("sensitivity",
                        static_cast<int>(event.approval_result->sensitivity));
    approval_result.Set("state", static_cast<int>(event.approval_result->state));
    approval_result.Set("decision",
                        static_cast<int>(event.approval_result->decision));
    approval_result.Set("page_derived_justification",
                        event.approval_result->page_derived_justification);
    dict.Set("approval_result", std::move(approval_result));
  }
  if (event.interaction_request) {
    base::DictValue interaction_request;
    interaction_request.Set("request_id",
                            event.interaction_request->request_id);
    interaction_request.Set(
        "kind", static_cast<int>(event.interaction_request->kind));
    interaction_request.Set(
        "question", credential_redaction::RedactCredentialText(
                        event.interaction_request->question));
    base::ListValue options;
    for (const auto& option : event.interaction_request->options) {
      base::DictValue option_dict;
      option_dict.Set("id", option->id);
      // Same sanitizer as the question: agent-echoed secrets in option text
      // must not persist to disk either.
      option_dict.Set("label", credential_redaction::RedactCredentialText(
                                   option->label));
      if (option->description.has_value()) {
        option_dict.Set("description",
                        credential_redaction::RedactCredentialText(
                            *option->description));
      }
      options.Append(std::move(option_dict));
    }
    interaction_request.Set("options", std::move(options));
    if (event.interaction_request->artifact_ref.has_value()) {
      interaction_request.Set("artifact_ref",
                              *event.interaction_request->artifact_ref);
    }
    if (event.interaction_request->state.has_value()) {
      interaction_request.Set("state", *event.interaction_request->state);
    }
    dict.Set("interaction_request", std::move(interaction_request));
  }
  if (event.browser_context) {
    base::DictValue browser_context;
    browser_context.Set("status", static_cast<int>(event.browser_context->status));
    browser_context.Set("url",
                        ai_security::RedactUrlForAi(event.browser_context->url));
    browser_context.Set(
        "title", credential_redaction::RedactCredentialText(
                     event.browser_context->title));
    browser_context.Set(
        "content_snippet", credential_redaction::RedactCredentialText(
                               event.browser_context->content_snippet));
    browser_context.Set("content_length",
                        static_cast<int>(event.browser_context->content_length));
    browser_context.Set("label", credential_redaction::RedactCredentialText(
                                     event.browser_context->label));
    base::ListValue warnings;
    for (const std::string& warning : event.browser_context->warnings) {
      warnings.Append(credential_redaction::RedactCredentialText(warning));
    }
    browser_context.Set("warnings", std::move(warnings));
    dict.Set("browser_context", std::move(browser_context));
  }
  return dict;
}

}  // namespace maho::ai
