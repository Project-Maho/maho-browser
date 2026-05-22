// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ai/maho_opencode_acp_adapter.h"

#include <cstring>

#include "base/compiler_specific.h"
#include "base/containers/span.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/values.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "maho/browser/ai/maho_acp_event_decoder.h"
#include "maho/browser/ui/webui/maho_ai/maho_ai.mojom.h"
#include "maho/third_party/maho/maho_ffi.h"

MahoOpenCodeAcpAdapter::FfiUserData::FfiUserData() = default;
MahoOpenCodeAcpAdapter::FfiUserData::~FfiUserData() = default;

MahoOpenCodeAcpAdapter::MahoOpenCodeAcpAdapter(
    PrefService* prefs,
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory)
    : prefs_(prefs), url_loader_factory_(std::move(url_loader_factory)) {
  DETACH_FROM_SEQUENCE(sequence_checker_);
}

MahoOpenCodeAcpAdapter::~MahoOpenCodeAcpAdapter() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // Lifecycle contract: shutdown (closes stdin → child sees EOF and self-
  // terminates) then destroy (frees Rust-side resources). On abnormal Maho
  // exit the OS closes the pipe fd; the child detects EOF and exits. The Rust
  // crate's kill_on_drop(true) is the final backstop via SIGKILL.
  if (acp_handle_) {
    maho_acp_shutdown(acp_handle_);
    maho_acp_destroy(acp_handle_);
    acp_handle_ = nullptr;
  }
  // ffi_user_data_ freed here AFTER destroy returns.
}

std::string MahoOpenCodeAcpAdapter::GetAdapterName() const {
  return "opencode-acp";
}

bool MahoOpenCodeAcpAdapter::IsAvailable() const {
  return true;
}

void MahoOpenCodeAcpAdapter::SubmitMessage(
    const std::string& message,
    maho_ai::mojom::ChatIntent chat_intent,
    bool attach_browser_context,
    maho_ai::mojom::InteractionMode mode,
    RuntimeEventCallback on_event) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  current_on_event_ = on_event;
  accumulated_text_.clear();

  if (!attach_browser_context) {
    MahoAiRuntimeEvent ctx_event;
    ctx_event.type = MahoAiRuntimeEventType::kBrowserContextInjected;
    ctx_event.payload.Set("status", "not_requested");
    ctx_event.text = "Browser context not requested.";
    EmitEvent(std::move(ctx_event));
  }
  // TODO(Phase 5+): When attach_browser_context is true, inject browser
  // context. This is an AI-behavior concern that stays in C++.

  if (state_ == State::kReady) {
    state_ = State::kSubmitting;
    maho_acp_submit_prompt(acp_handle_, session_id_.c_str(), message.c_str());
    state_ = State::kAwaitingTurn;
    return;
  }

  if (state_ == State::kIdle) {
    pending_message_ = message;
    state_ = State::kStarting;

    acp_handle_ = maho_acp_create();
    if (!acp_handle_) {
      TransitionToError("Failed to create ACP handle.");
      return;
    }

    ffi_user_data_ = std::make_unique<FfiUserData>();
    ffi_user_data_->weak_adapter = weak_factory_.GetWeakPtr();
    ffi_user_data_->task_runner =
        base::SequencedTaskRunner::GetCurrentDefault();

    int32_t rc = maho_acp_set_event_callback(
        acp_handle_, &MahoOpenCodeAcpAdapter::OnFfiEvent,
        ffi_user_data_.get());
    if (rc != MAHO_ACP_OK) {
      TransitionToError("Failed to set event callback.");
      return;
    }

    rc = maho_acp_start(acp_handle_, nullptr);
    if (rc != MAHO_ACP_OK) {
      TransitionToError("Failed to start ACP process.");
      return;
    }

    state_ = State::kInitializing;
    maho_acp_initialize(acp_handle_);
    return;
  }

  if (state_ == State::kAwaitingTurn) {
    pending_message_ = message;
    return;
  }

  pending_message_ = message;
}

void MahoOpenCodeAcpAdapter::CancelCurrentTurn() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (state_ != State::kAwaitingTurn || session_id_.empty()) {
    return;
  }

  maho_acp_cancel_turn(acp_handle_, session_id_.c_str());
}

void MahoOpenCodeAcpAdapter::RespondToApproval(const std::string& approval_id,
                                                bool approved) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = approval_to_rpc_id_.find(approval_id);
  if (it == approval_to_rpc_id_.end()) {
    LOG(WARNING) << "MahoOpenCodeAcpAdapter: unknown approval_id "
                 << approval_id;
    return;
  }

  int64_t json_rpc_id = it->second;
  approval_to_rpc_id_.erase(it);

  uint32_t outcome_kind = approved ? 1 : 0;
  const char* option_id = approved ? "allow" : nullptr;

  maho_acp_reply_permission(acp_handle_, json_rpc_id, outcome_kind, option_id);
}

std::string MahoOpenCodeAcpAdapter::GetRuntimeSessionId() const {
  return session_id_;
}

void MahoOpenCodeAcpAdapter::Reset() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (acp_handle_) {
    maho_acp_shutdown(acp_handle_);
    maho_acp_destroy(acp_handle_);
    acp_handle_ = nullptr;
  }
  ffi_user_data_.reset();

  state_ = State::kIdle;
  session_id_.clear();
  accumulated_text_.clear();
  pending_message_.clear();
  current_on_event_.Reset();
  approval_to_rpc_id_.clear();
  next_approval_id_ = 1;
}

// static
void MahoOpenCodeAcpAdapter::OnFfiEvent(void* user_data,
                                         const uint8_t* blob,
                                         size_t len) {
  // SAFETY: user_data points to FfiUserData which outlives the FFI handle.
  auto* ud = static_cast<FfiUserData*>(user_data);

  // SAFETY: blob/len come from the FFI callback contract — the Rust side
  // guarantees blob points to len valid bytes for the duration of this call.
  auto blob_span = UNSAFE_BUFFERS(base::span<const uint8_t>(blob, len));
  std::vector<uint8_t> blob_copy(blob_span.begin(), blob_span.end());

  ud->task_runner->PostTask(
      FROM_HERE,
      base::BindOnce(&MahoOpenCodeAcpAdapter::OnEventOnSequence,
                     ud->weak_adapter, std::move(blob_copy)));
}

void MahoOpenCodeAcpAdapter::OnEventOnSequence(std::vector<uint8_t> blob) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto decoded = MahoAcpEventDecoder::Decode(blob);
  if (!decoded) {
    LOG(WARNING) << "MahoOpenCodeAcpAdapter: failed to decode FFI event blob";
    return;
  }

  const std::string* kind = decoded->FindString("kind");
  if (!kind) {
    LOG(WARNING) << "MahoOpenCodeAcpAdapter: event missing 'kind' field";
    return;
  }

  const base::DictValue* data = decoded->FindDict("data");
  base::DictValue empty_data;
  DriveStateMachine(*kind, data ? *data : empty_data);
}

void MahoOpenCodeAcpAdapter::DriveStateMachine(const std::string& kind,
                                                const base::DictValue& data) {
  if (kind == "InitializeOk") {
    if (state_ == State::kInitializing) {
      state_ = State::kSessionPending;
      maho_acp_create_session(acp_handle_);
    }
    return;
  }

  if (kind == "InitializeErr") {
    const std::string* msg = data.FindString("message");
    TransitionToError(msg ? *msg : "ACP initialization failed.");
    return;
  }

  if (kind == "SessionCreated") {
    const std::string* sid = data.FindString("session_id");
    if (sid) {
      session_id_ = *sid;
    }
    state_ = State::kReady;
    EmitConnectionState("connected");

    if (!pending_message_.empty()) {
      std::string msg = std::move(pending_message_);
      pending_message_.clear();
      state_ = State::kSubmitting;
      maho_acp_submit_prompt(acp_handle_, session_id_.c_str(), msg.c_str());
      state_ = State::kAwaitingTurn;
    }
    return;
  }

  // The Rust FfiEvent enum tags streaming updates under
  // FfiEvent::Update(FfiAcpUpdate), which serializes as
  //   {"kind":"Update","data":{"update_kind":"<variant>","update_data":{...}}}
  // Re-dispatch using the inner discriminator so the per-variant branches
  // below see the same shape they would see for top-level FfiEvent variants.
  if (kind == "Update") {
    const std::string* update_kind = data.FindString("update_kind");
    if (!update_kind) {
      DVLOG(1) << "MahoOpenCodeAcpAdapter: Update event missing 'update_kind'";
      return;
    }
    const base::DictValue* update_data = data.FindDict("update_data");
    base::DictValue empty_data;
    DriveStateMachine(*update_kind, update_data ? *update_data : empty_data);
    return;
  }

  if (kind == "AgentMessageChunk") {
    const std::string* text = data.FindString("text");
    if (text && !text->empty()) {
      accumulated_text_ += *text;
      MahoAiRuntimeEvent event;
      event.type = MahoAiRuntimeEventType::kAssistantToken;
      event.text = *text;
      EmitEvent(std::move(event));
    }
    return;
  }

  if (kind == "AgentThoughtChunk") {
    const std::string* text = data.FindString("text");
    if (text && !text->empty()) {
      MahoAiRuntimeEvent event;
      event.type = MahoAiRuntimeEventType::kAssistantToken;
      event.text = *text;
      EmitEvent(std::move(event));
    }
    return;
  }

  if (kind == "ToolCall") {
    MahoAiRuntimeEvent event;
    event.type = MahoAiRuntimeEventType::kToolRequest;
    const std::string* title = data.FindString("title");
    const std::string* id = data.FindString("id");
    const std::string* tool_kind = data.FindString("kind");
    const std::string* tool_status = data.FindString("status");
    if (title) event.payload.Set("tool_name", *title);
    if (id) event.payload.Set("call_id", *id);
    if (tool_kind) event.payload.Set("tool_kind", *tool_kind);
    if (tool_status) event.payload.Set("status", *tool_status);
    if (const base::Value* raw_input = data.Find("raw_input")) {
      std::string args_json;
      if (base::JSONWriter::Write(*raw_input, &args_json)) {
        event.payload.Set("arguments_json", args_json);
      }
    }
    EmitEvent(std::move(event));
    return;
  }

  if (kind == "ToolCallUpdate") {
    MahoAiRuntimeEvent event;
    event.type = MahoAiRuntimeEventType::kToolResult;
    const std::string* id = data.FindString("id");
    const std::string* status = data.FindString("status");
    const std::string* content_text = data.FindString("content_text");
    if (id) event.payload.Set("call_id", *id);
    if (content_text) {
      event.payload.Set("output", *content_text);
      event.text = *content_text;
    }
    if (const base::Value* raw_output = data.Find("raw_output")) {
      std::string out_json;
      if (base::JSONWriter::Write(*raw_output, &out_json)) {
        event.payload.Set("raw_output_json", out_json);
      }
    }
    bool success = status && (*status == "completed" || *status == "success");
    bool failed = status && *status == "failed";
    event.payload.Set("success", success);
    if (failed && event.text.empty()) {
      event.text = "Tool call failed.";
    }
    if (status) event.payload.Set("status", *status);
    EmitEvent(std::move(event));
    return;
  }

  if (kind == "PermissionRequest") {
    std::optional<int> rpc_id_opt = data.FindInt("json_rpc_id");
    int64_t json_rpc_id = rpc_id_opt.value_or(0);

    std::string approval_id =
        "acp_approval_" + base::NumberToString(next_approval_id_++);
    approval_to_rpc_id_[approval_id] = json_rpc_id;

    MahoAiRuntimeEvent event;
    event.type = MahoAiRuntimeEventType::kApprovalRequest;
    event.payload.Set("approval_id", approval_id);
    const std::string* desc = data.FindString("description");
    const std::string* title = data.FindString("title");
    event.text = desc ? *desc : (title ? *title : "Permission requested.");
    event.payload.Set("description", event.text);

    const std::string* method = data.FindString("method");
    if (method) event.payload.Set("method", *method);

    EmitEvent(std::move(event));
    return;
  }

  if (kind == "PromptComplete") {
    const std::string* stop_reason = data.FindString("stop_reason");
    bool cancelled = stop_reason && *stop_reason == "cancelled";

    MahoAiRuntimeEvent event;
    event.type = MahoAiRuntimeEventType::kTurnComplete;
    event.text = accumulated_text_;
    event.payload.Set("cancelled", cancelled);
    accumulated_text_.clear();
    state_ = State::kReady;
    EmitEvent(std::move(event));
    return;
  }

  if (kind == "PromptErr") {
    const std::string* msg = data.FindString("message");
    MahoAiRuntimeEvent event;
    event.type = MahoAiRuntimeEventType::kError;
    event.text = msg ? *msg : "Prompt error.";
    state_ = State::kReady;
    EmitEvent(std::move(event));
    return;
  }

  if (kind == "TransportError" || kind == "ChildExited") {
    const std::string* msg = data.FindString("message");
    MahoAiRuntimeEvent err_event;
    err_event.type = MahoAiRuntimeEventType::kError;
    err_event.text = msg ? *msg : (kind + " occurred.");
    EmitEvent(std::move(err_event));
    EmitConnectionState("disconnected");
    state_ = State::kError;
    return;
  }

  DVLOG(1) << "MahoOpenCodeAcpAdapter: unhandled event kind: " << kind;
}

void MahoOpenCodeAcpAdapter::EmitEvent(MahoAiRuntimeEvent event) {
  if (current_on_event_) {
    current_on_event_.Run(std::move(event));
  }
}

void MahoOpenCodeAcpAdapter::EmitConnectionState(
    const std::string& state_str) {
  MahoAiRuntimeEvent event;
  event.type = MahoAiRuntimeEventType::kConnectionStateChanged;
  event.text = state_str;
  EmitEvent(std::move(event));
}

void MahoOpenCodeAcpAdapter::TransitionToError(const std::string& message) {
  state_ = State::kError;
  MahoAiRuntimeEvent event;
  event.type = MahoAiRuntimeEventType::kError;
  event.text = message;
  EmitEvent(std::move(event));
}
