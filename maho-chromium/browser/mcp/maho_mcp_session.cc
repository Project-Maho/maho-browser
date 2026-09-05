// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "maho/browser/mcp/maho_mcp_session.h"

#include "build/build_config.h"
#include "maho/browser/ai/maho_browser_action_contract.h"
#include "maho/browser/ai/maho_browser_tool_registry.h"
#include "maho/browser/mcp/maho_mcp_capability_registry.h"

#if !BUILDFLAG(IS_WIN)
#include <unistd.h>
#endif

#include <atomic>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "base/base64.h"
#include "base/files/file_path.h"
#include "base/functional/callback_helpers.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/memory/scoped_refptr.h"
#include "base/no_destructor.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/synchronization/lock.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "base/uuid.h"
#include "base/values.h"
#include "maho/browser/mcp/maho_mcp_accessibility_handler.h"
#include "maho/browser/mcp/maho_mcp_browser_action_handler.h"
#include "maho/browser/mcp/maho_mcp_firewall.h"
#include "maho/browser/mcp/maho_mcp_lease_registry.h"
#include "url/gurl.h"

#if BUILDFLAG(IS_WIN)
#include "maho/browser/mcp/maho_mcp_session_token_win.h"
#endif

namespace maho {

MahoMcpBrowserActionHandler::Error::Error() = default;
MahoMcpBrowserActionHandler::Error::Error(int code, std::string message)
    : code(code), message(std::move(message)) {}
MahoMcpBrowserActionHandler::Error::Error(const Error&) = default;
MahoMcpBrowserActionHandler::Error&
MahoMcpBrowserActionHandler::Error::operator=(const Error&) = default;
MahoMcpBrowserActionHandler::Error::Error(Error&&) noexcept = default;
MahoMcpBrowserActionHandler::Error&
MahoMcpBrowserActionHandler::Error::operator=(Error&&) noexcept = default;
MahoMcpBrowserActionHandler::Error::~Error() = default;

MahoMcpBrowserActionHandler::Result::Result() = default;
MahoMcpBrowserActionHandler::Result::Result(Result&&) noexcept = default;
MahoMcpBrowserActionHandler::Result&
MahoMcpBrowserActionHandler::Result::operator=(Result&&) noexcept = default;
MahoMcpBrowserActionHandler::Result::~Result() = default;

MahoMcpBrowserDelegate::SameOriginFetchResult::SameOriginFetchResult() =
    default;
MahoMcpBrowserDelegate::SameOriginFetchResult::SameOriginFetchResult(
    const SameOriginFetchResult&) = default;
MahoMcpBrowserDelegate::SameOriginFetchResult&
MahoMcpBrowserDelegate::SameOriginFetchResult::operator=(
    const SameOriginFetchResult&) = default;
MahoMcpBrowserDelegate::SameOriginFetchResult::SameOriginFetchResult(
    SameOriginFetchResult&&) noexcept = default;
MahoMcpBrowserDelegate::SameOriginFetchResult&
MahoMcpBrowserDelegate::SameOriginFetchResult::operator=(
    SameOriginFetchResult&&) noexcept = default;
MahoMcpBrowserDelegate::SameOriginFetchResult::~SameOriginFetchResult() =
    default;

MahoMcpBrowserDelegate::StopNetworkCaptureResult::StopNetworkCaptureResult() =
    default;
MahoMcpBrowserDelegate::StopNetworkCaptureResult::StopNetworkCaptureResult(
    StopNetworkCaptureResult&&) noexcept = default;
MahoMcpBrowserDelegate::StopNetworkCaptureResult&
MahoMcpBrowserDelegate::StopNetworkCaptureResult::operator=(
    StopNetworkCaptureResult&&) noexcept = default;
MahoMcpBrowserDelegate::StopNetworkCaptureResult::~StopNetworkCaptureResult() =
    default;

base::Value MahoMcpBrowserDelegate::GetAccessibilitySnapshot(
    int tab_id,
    MahoMcpSession::RefTable* out_refs,
    uint64_t* out_snapshot_token) {
  if (out_snapshot_token) {
    *out_snapshot_token = 0;
  }
  return GetAccessibilitySnapshot(tab_id, out_refs);
}

MahoMcpBrowserDelegate::AccessibilitySnapshotV2Params::
    AccessibilitySnapshotV2Params() = default;
MahoMcpBrowserDelegate::AccessibilitySnapshotV2Params::
    ~AccessibilitySnapshotV2Params() = default;
MahoMcpBrowserDelegate::AccessibilitySnapshotV2Params::
    AccessibilitySnapshotV2Params(const AccessibilitySnapshotV2Params&) =
        default;
MahoMcpBrowserDelegate::AccessibilitySnapshotV2Params::
    AccessibilitySnapshotV2Params(AccessibilitySnapshotV2Params&&) noexcept =
        default;
MahoMcpBrowserDelegate::AccessibilitySnapshotV2Params&
MahoMcpBrowserDelegate::AccessibilitySnapshotV2Params::operator=(
    const AccessibilitySnapshotV2Params&) = default;
MahoMcpBrowserDelegate::AccessibilitySnapshotV2Params&
MahoMcpBrowserDelegate::AccessibilitySnapshotV2Params::operator=(
    AccessibilitySnapshotV2Params&&) noexcept = default;

MahoMcpBrowserDelegate::AccessibilitySnapshotV2Result::
    AccessibilitySnapshotV2Result() = default;
MahoMcpBrowserDelegate::AccessibilitySnapshotV2Result::
    ~AccessibilitySnapshotV2Result() = default;
MahoMcpBrowserDelegate::AccessibilitySnapshotV2Result::
    AccessibilitySnapshotV2Result(const AccessibilitySnapshotV2Result&) =
        default;
MahoMcpBrowserDelegate::AccessibilitySnapshotV2Result::
    AccessibilitySnapshotV2Result(AccessibilitySnapshotV2Result&&) noexcept =
        default;
MahoMcpBrowserDelegate::AccessibilitySnapshotV2Result&
MahoMcpBrowserDelegate::AccessibilitySnapshotV2Result::operator=(
    const AccessibilitySnapshotV2Result&) = default;
MahoMcpBrowserDelegate::AccessibilitySnapshotV2Result&
MahoMcpBrowserDelegate::AccessibilitySnapshotV2Result::operator=(
    AccessibilitySnapshotV2Result&&) noexcept = default;

MahoMcpBrowserDelegate::AccessibilitySnapshotV2Result
MahoMcpBrowserDelegate::GetAccessibilitySnapshotV2(
    const AccessibilitySnapshotV2Params& params,
    MahoMcpSession::RefTable* out_refs,
    MahoMcpAccessibilityHandler::ObservationCache& observation_cache,
    uint64_t token_sequence) {
  AccessibilitySnapshotV2Result result;
  result.snapshot_token = "s_" + std::to_string(params.tab_id) + "_" +
                          std::to_string(token_sequence);
  result.tab.id = params.tab_id;
  result.tab.url = "https://example.com/";
  result.tab.title = "Page";
  result.tree = "WebArea\n";
  result.bytes = result.tree.size();
  return result;
}

class MahoMcpControllerState
    : public base::RefCountedThreadSafe<MahoMcpControllerState> {
 public:
  void SetRevocationCleanup(
      scoped_refptr<base::SequencedTaskRunner> task_runner,
      base::OnceClosure cleanup) {
    DCHECK(task_runner);
    DCHECK(cleanup);
    base::AutoLock lock(lock_);
    task_runner_ = std::move(task_runner);
    cleanup_ = std::move(cleanup);
  }

  void Revoke() {
    bool expected = false;
    if (!revoked_.compare_exchange_strong(expected, true,
                                          std::memory_order_acq_rel)) {
      return;
    }
    scoped_refptr<base::SequencedTaskRunner> task_runner;
    base::OnceClosure cleanup;
    {
      base::AutoLock lock(lock_);
      task_runner = task_runner_;
      cleanup = std::move(cleanup_);
    }
    DCHECK(task_runner);
    DCHECK(cleanup);
    if (task_runner->RunsTasksInCurrentSequence()) {
      std::move(cleanup).Run();
      return;
    }
    task_runner->PostTask(FROM_HERE, std::move(cleanup));
  }

  bool is_revoked() const { return revoked_.load(std::memory_order_acquire); }

 private:
  friend class base::RefCountedThreadSafe<MahoMcpControllerState>;
  ~MahoMcpControllerState() = default;

  std::atomic_bool revoked_ = false;
  base::Lock lock_;
  scoped_refptr<base::SequencedTaskRunner> task_runner_ GUARDED_BY(lock_);
  base::OnceClosure cleanup_ GUARDED_BY(lock_);
};

namespace {

constexpr char kMcpProtocolVersion[] = "2025-03-26";
constexpr char kServerName[] = "maho-browser";
constexpr char kServerVersion[] = "0.4.1";
constexpr char kMailReadBrokerUnavailable[] = "Mail read broker unavailable";
constexpr char kExternalMcpLabel[] = "External MCP";

struct ControllerIdentity {
  MahoMcpControllerKind kind = MahoMcpControllerKind::kThirdParty;
  const char* wire_kind = "third-party";
  const char* display_label = kExternalMcpLabel;
};

bool IsTrustedMahoExecutablePath(const base::FilePath& executable) {
  if (!executable.IsAbsolute()) {
    return false;
  }
  const std::string normalized =
      base::ToLowerASCII(executable.NormalizePathSeparators().AsUTF8Unsafe());
#if BUILDFLAG(IS_WIN)
  return (base::StartsWith(normalized, "c:\\program files\\maho\\") ||
          base::StartsWith(normalized,
                           "c:\\program files (x86)\\maho\\")) &&
         (base::EndsWith(normalized, "\\maho.exe") ||
          base::EndsWith(normalized, "\\maho-browser-mcp.exe"));
#elif BUILDFLAG(IS_APPLE)
  return base::EndsWith(normalized, "/maho.app/contents/helpers/maho") ||
         base::EndsWith(normalized,
                        "/maho.app/contents/helpers/maho-browser-mcp");
#else
  return normalized == "/usr/bin/maho" ||
         normalized == "/usr/bin/maho-browser-mcp" ||
         normalized == "/opt/maho/bin/maho" ||
         normalized == "/opt/maho/bin/maho-browser-mcp";
#endif
}

ControllerIdentity ClassifyController(std::string_view hint,
                                      std::string_view peer_executable,
                                      bool peer_is_trusted_maho) {
  if (!peer_is_trusted_maho ||
      !IsTrustedMahoExecutablePath(
          base::FilePath::FromUTF8Unsafe(peer_executable))) {
    return {};
  }

  const std::string executable_name = base::ToLowerASCII(
      base::FilePath::FromUTF8Unsafe(peer_executable)
          .BaseName()
          .AsUTF8Unsafe());
#if BUILDFLAG(IS_WIN)
  constexpr char kMahoCliExecutable[] = "maho.exe";
  constexpr char kMahoBrowserMcpExecutable[] = "maho-browser-mcp.exe";
#else
  constexpr char kMahoCliExecutable[] = "maho";
  constexpr char kMahoBrowserMcpExecutable[] = "maho-browser-mcp";
#endif

  if (executable_name == kMahoCliExecutable && hint == "maho-cli-repl") {
    return {MahoMcpControllerKind::kMahoCliRepl, "maho-cli-repl",
            "Maho CLI REPL"};
  }
  if (executable_name == kMahoCliExecutable &&
      (hint == "maho-cli" || hint.empty())) {
    return {MahoMcpControllerKind::kMahoCli, "maho-cli", "Maho CLI"};
  }
  if (executable_name == kMahoBrowserMcpExecutable &&
      hint == "maho-browser-mcp") {
    return {MahoMcpControllerKind::kMahoBrowserMcp, "maho-browser-mcp",
            "Maho Browser MCP"};
  }
  return {};
}

MahoMcpBrowserDelegate* g_browser_delegate = nullptr;
MahoMcpLeaseRegistry* g_browser_action_lease_registry = nullptr;
base::NoDestructor<base::Lock> g_controller_states_lock;
base::NoDestructor<std::map<std::string, scoped_refptr<MahoMcpControllerState>>>
    g_controller_states;

void RegisterControllerState(
    std::string_view session_id,
    scoped_refptr<MahoMcpControllerState> controller_state) {
  base::AutoLock lock(*g_controller_states_lock);
  g_controller_states->insert_or_assign(std::string(session_id),
                                        std::move(controller_state));
}

scoped_refptr<MahoMcpControllerState> GetControllerState(
    std::string_view session_id) {
  base::AutoLock lock(*g_controller_states_lock);
  auto it = g_controller_states->find(std::string(session_id));
  return it == g_controller_states->end() ? nullptr : it->second;
}

void UnregisterControllerState(std::string_view session_id) {
  base::AutoLock lock(*g_controller_states_lock);
  g_controller_states->erase(std::string(session_id));
}

void RevokeControllerSessionOnIo(
    std::string session_id,
    scoped_refptr<MahoMcpNetworkCaptureState> capture_state) {
  if (g_browser_action_lease_registry) {
    g_browser_action_lease_registry->OnSessionDropped(std::move(session_id));
  }
  if (!capture_state || !capture_state->active ||
      capture_state->capture_id.empty()) {
    return;
  }
  const std::string capture_id = capture_state->capture_id;
  capture_state->active = false;
  capture_state->capture_id.clear();
  capture_state->tab_id = 0;
  capture_state->target = ResolvedMahoMcpTarget();
  if (g_browser_delegate) {
    g_browser_delegate->CancelNetworkCapture(capture_id);
  }
}

std::string BuildJsonRpcSuccessLine(std::optional<base::Value> id,
                                    base::Value result) {
  MahoMcpFirewall::RedactAll(result);
  base::DictValue response;
  response.Set("jsonrpc", "2.0");
  response.Set("id", id.has_value() ? id->Clone() : base::Value());
  response.Set("result", std::move(result));
  std::string out;
  base::JSONWriter::Write(base::Value(std::move(response)), &out);
  out.push_back('\n');
  return out;
}

const char* ReceiptControllerType(MahoMcpControllerKind kind) {
  switch (kind) {
    case MahoMcpControllerKind::kMahoCli:
    case MahoMcpControllerKind::kMahoCliRepl:
      return "automation";
    case MahoMcpControllerKind::kMahoBrowserMcp:
    case MahoMcpControllerKind::kThirdParty:
      return "remote_client";
  }
}

std::string AttachExecutionReceipt(
    std::string line,
    const MahoBrowserToolRegistry::CapabilityDescriptor& descriptor,
    std::string_view execution_id,
    std::string_view controller_id,
    std::string_view controller_name,
    std::string_view controller_type,
    const ResolvedMahoMcpTarget& target,
    std::string_view approval,
    double started_at_seconds) {
  std::optional<base::DictValue> response =
      base::JSONReader::ReadDict(line, base::JSON_PARSE_RFC);
  if (!response || response->FindDict("error")) {
    return line;
  }
  base::Value* result = response->Find("result");
  if (!result) {
    return line;
  }
  if (const base::DictValue* typed = result->GetIfDict();
      typed && typed->FindString("outputJson") && typed->FindDict("receipt")) {
    return line;
  }

  std::string output_json;
  if (!base::JSONWriter::Write(result->Clone(), &output_json)) {
    return line;
  }

  MahoBrowserToolRegistry::ExecutionReceiptContext context;
  context.execution_id = std::string(execution_id);
  context.controller_id = std::string(controller_id);
  context.controller_name = std::string(controller_name);
  context.controller_type = std::string(controller_type);
  context.control_plane = "mcp";
  context.approval = std::string(approval);
  context.outcome_status = "completed";
  context.outcome_code = "completed";
  context.started_at_seconds = started_at_seconds;
  context.completed_at_seconds = base::Time::Now().InSecondsFSinceUnixEpoch();
  if (target.valid) {
    context.target_tab_id = target.tab_id;
  }
  response->Set(
      "result", MahoBrowserToolRegistry::SerializeExecutionResult(
                    descriptor, std::move(output_json), context));
  base::Value response_value(std::move(*response));
  MahoMcpFirewall::RedactAll(response_value);
  std::string serialized;
  base::JSONWriter::Write(response_value, &serialized);
  serialized.push_back('\n');
  return serialized;
}

std::string RedactJsonRpcLine(std::string line) {
  std::optional<base::Value> response =
      base::JSONReader::Read(line, base::JSON_PARSE_RFC);
  if (!response) {
    return line;
  }
  if (base::DictValue* response_dict = response->GetIfDict()) {
    if (base::Value* id = response_dict->Find("id"); id && id->is_string()) {
      *id = base::Value(MahoMcpFirewall::RedactString(id->GetString()));
    }
  }
  MahoMcpFirewall::RedactAll(*response);
  std::string out;
  base::JSONWriter::Write(*response, &out);
  out.push_back('\n');
  return out;
}

std::string BuildJsonRpcErrorLine(std::optional<base::Value> id,
                                  int code,
                                  std::string message,
                                  std::optional<base::Value> data =
                                      std::nullopt) {
  base::DictValue error;
  error.Set("code", code);
  error.Set("message", std::move(message));
  if (data.has_value()) {
    error.Set("data", std::move(*data));
  }

  base::DictValue response;
  response.Set("jsonrpc", "2.0");
  response.Set("id", id.has_value() ? id->Clone() : base::Value());
  response.Set("error", std::move(error));
  std::string out;
  base::JSONWriter::Write(base::Value(std::move(response)), &out);
  out.push_back('\n');
  return RedactJsonRpcLine(std::move(out));
}

std::optional<base::Value> CloneMessageId(
    const std::optional<base::Value>& id) {
  if (!id.has_value()) {
    return std::nullopt;
  }
  return id->Clone();
}

std::string BuildLeaseContentionErrorLine(
    std::optional<base::Value> id,
    int tab_id,
    const MahoMcpLeaseRegistry::AcquireResult& result) {
  DCHECK(!result.ok);
  DCHECK(!result.previous_holder.empty());
  base::DictValue data;
  data.Set("previous_holder", result.previous_holder);
  data.Set("tab_id", tab_id);
  return BuildJsonRpcErrorLine(std::move(id), -32000, result.message,
                               base::Value(std::move(data)));
}

std::string BuildRevokedControllerErrorLine(std::string_view line) {
  std::optional<base::DictValue> response =
      base::JSONReader::ReadDict(line, base::JSON_PARSE_RFC);
  const base::Value* id = response ? response->Find("id") : nullptr;
  return BuildJsonRpcErrorLine(
      id ? std::optional<base::Value>(id->Clone()) : std::nullopt, -32004,
      "Controller session was revoked");
}

bool IsJsonRpcErrorResponse(std::string_view line) {
  std::optional<base::Value> response =
      base::JSONReader::Read(line, base::JSON_PARSE_RFC);
  return response && response->is_dict() &&
         response->GetDict().FindDict("error");
}

MahoMcpSession::DeferredResponseSender MakeTerminalAwareSender(
    MahoMcpSession::DeferredResponseSender sender,
    std::string activity_id,
    std::string controller_session_id,
    scoped_refptr<MahoMcpControllerState> controller_state,
    std::string controller_label,
    ResolvedMahoMcpTarget target,
    MahoBrowserToolRegistry::Category category,
    MahoBrowserToolRegistry::Sensitivity sensitivity,
    uint64_t terminal_revision,
    const MahoBrowserToolRegistry::CapabilityDescriptor* descriptor,
    std::string controller_type,
    std::shared_ptr<std::string> approval,
    double started_at_seconds) {
  auto terminal_published = std::make_shared<std::atomic_bool>(false);
  return base::BindRepeating(
      [](MahoMcpSession::DeferredResponseSender sender, std::string activity_id,
         std::string controller_session_id,
         scoped_refptr<MahoMcpControllerState> controller_state,
         std::string controller_label, ResolvedMahoMcpTarget target,
         MahoBrowserToolRegistry::Category category,
         MahoBrowserToolRegistry::Sensitivity sensitivity,
         uint64_t terminal_revision,
         const MahoBrowserToolRegistry::CapabilityDescriptor* descriptor,
         std::string controller_type, std::shared_ptr<std::string> approval,
         double started_at_seconds,
         std::shared_ptr<std::atomic_bool> terminal_published,
         std::string line) {
        if (controller_state->is_revoked()) {
          line = BuildRevokedControllerErrorLine(line);
        } else if (!IsJsonRpcErrorResponse(line) && target.valid &&
                   (!g_browser_delegate ||
                    !g_browser_delegate->RevalidateTarget(target))) {
          std::optional<base::DictValue> response =
              base::JSONReader::ReadDict(line, base::JSON_PARSE_RFC);
          const base::Value* id = response ? response->Find("id") : nullptr;
          line = BuildJsonRpcErrorLine(
              id ? std::optional<base::Value>(id->Clone()) : std::nullopt,
              kMahoMcpErrorTabNotFound, kMahoMcpMessageTabNotFound);
        }
        line = RedactJsonRpcLine(std::move(line));
        const bool failed = IsJsonRpcErrorResponse(line);
        if (!failed && descriptor) {
          line = AttachExecutionReceipt(std::move(line), *descriptor,
                                        activity_id, controller_session_id,
                                        controller_label, controller_type,
                                        target, *approval, started_at_seconds);
        }
        bool expected = false;
        if (terminal_published->compare_exchange_strong(expected, true) &&
            g_browser_delegate) {
          g_browser_delegate->PublishControlActivity(
              activity_id, controller_session_id, controller_label, target,
              category, sensitivity,
              failed ? MahoMcpActivityPhase::kFailed
                     : MahoMcpActivityPhase::kCompleted,
              terminal_revision);
        }
        sender.Run(std::move(line));
      },
      std::move(sender), std::move(activity_id),
      std::move(controller_session_id), std::move(controller_state),
      std::move(controller_label), target, category, sensitivity,
      terminal_revision, descriptor, std::move(controller_type),
      std::move(approval), started_at_seconds, std::move(terminal_published));
}

// T8: build the completion for an async mail read. On broker failure it emits a
// -32000; on success it forwards the durable JSON broker payload verbatim as
// MCP text content so the browser side never has to reparse it.
base::OnceCallback<void(bool, std::string)> MakeMailReadCompletion(
    std::optional<base::Value> msg_id,
    MahoMcpSession::DeferredResponseSender sender,
    std::string tool_name,
    bool recheck_read_consent) {
  return base::BindOnce(
      [](std::optional<base::Value> id,
         MahoMcpSession::DeferredResponseSender sender, std::string tool_name,
         bool recheck_read_consent, bool ok, std::string result_json) {
        if (recheck_read_consent) {
          const ai::MailAuthorizationDecision authorization =
              g_browser_delegate
                  ? ai::AuthorizeMailTool(
                        tool_name,
                        g_browser_delegate->GetMailAuthorizationContext())
                  : ai::MailAuthorizationDecision{};
          if (authorization.action != ai::MailAuthorizationAction::kAllow) {
            sender.Run(BuildJsonRpcErrorLine(
                std::move(id), -32008, std::string(authorization.reason_code)));
            return;
          }
        }
        if (!ok) {
          sender.Run(BuildJsonRpcErrorLine(std::move(id), -32000,
                                           result_json.empty()
                                               ? "Mail read failed"
                                               : std::move(result_json)));
          return;
        }
        base::DictValue text_item;
        text_item.Set("type", "text");
        text_item.Set("text", std::move(result_json));
        base::ListValue content;
        content.Append(std::move(text_item));
        base::DictValue result;
        result.Set("content", std::move(content));
        sender.Run(BuildJsonRpcSuccessLine(std::move(id),
                                           base::Value(std::move(result))));
      },
      std::move(msg_id), std::move(sender), std::move(tool_name),
      recheck_read_consent);
}

const std::string* FindRequiredStringArgument(const base::DictValue* arguments,
                                              const char* name) {
  if (!arguments) {
    return nullptr;
  }
  const std::string* value = arguments->FindString(name);
  if (!value || value->empty()) {
    return nullptr;
  }
  return value;
}

std::string RequiredStringArgumentMessage(const char* name) {
  return std::string("Invalid params: ") + name + " required";
}

bool ReadOptionalNonNegativeIntArgument(const base::DictValue* arguments,
                                        const char* name,
                                        int default_value,
                                        int* out,
                                        std::string* error_message) {
  const base::Value* raw = arguments ? arguments->Find(name) : nullptr;
  if (!raw) {
    *out = default_value;
    return true;
  }
  std::optional<int> value = arguments->FindInt(name);
  if (!value.has_value()) {
    *error_message = std::string("Invalid params: ") + name +
                     " must be a non-negative integer";
    return false;
  }
  if (*value < 0) {
    *error_message = std::string("Invalid params: ") + name +
                     " must be a non-negative integer";
    return false;
  }
  *out = *value;
  return true;
}

bool AddOptionalStringArgument(const base::DictValue* arguments,
                               const char* name,
                               base::DictValue* out,
                               std::string* error_message) {
  const base::Value* raw = arguments ? arguments->Find(name) : nullptr;
  if (!raw) {
    return true;
  }
  const std::string* value = arguments->FindString(name);
  if (!value) {
    *error_message =
        std::string("Invalid params: ") + name + " must be a string";
    return false;
  }
  out->Set(name, *value);
  return true;
}

bool BuildMailSearchQueryJson(const base::DictValue* arguments,
                              std::string* query_json,
                              std::string* error_message) {
  const std::string* query = FindRequiredStringArgument(arguments, "query");
  if (!query) {
    *error_message = RequiredStringArgumentMessage("query");
    return false;
  }

  base::DictValue query_object;
  query_object.Set("query", *query);
  if (!AddOptionalStringArgument(arguments, "account_id", &query_object,
                                 error_message)) {
    return false;
  }
  if (!AddOptionalStringArgument(arguments, "folder_id", &query_object,
                                 error_message)) {
    return false;
  }

  int limit = 50;
  int offset = 0;
  if (!ReadOptionalNonNegativeIntArgument(arguments, "limit", 50, &limit,
                                          error_message) ||
      !ReadOptionalNonNegativeIntArgument(arguments, "offset", 0, &offset,
                                          error_message)) {
    return false;
  }
  query_object.Set("limit", limit);
  query_object.Set("offset", offset);

  base::JSONWriter::Write(base::Value(std::move(query_object)), query_json);
  return true;
}

bool TabExists(int tab_id,
               const std::vector<MahoMcpSession::TabInfo>& fallback_tabs) {
  if (tab_id == 0) {
    return true;
  }
  if (g_browser_delegate) {
    for (const auto& t : g_browser_delegate->GetTabList()) {
      if (t.id == tab_id) {
        return true;
      }
    }
  } else {
    for (const auto& t : fallback_tabs) {
      if (t.id == tab_id) {
        return true;
      }
    }
  }
  return false;
}

// R-8: tools that operate on a specific tab. Their target is resolved once,
// before dispatch, through the private-context boundary so OTR/ineligible
// targets are rejected with a uniform non-revealing error.
bool IsTabTargetTool(std::string_view tool_name) {
  static constexpr std::string_view kTabTargetTools[] = {
      "browser_tab_get",
      "browser_navigate",
      "browser_tab_close",
      "browser_tab_switch",
      "browser_page_content",
      "browser_page_text",
      "browser_page_context",
      "browser_search_in_page",
      "page_query_selector",
      "page_get_text",
      "page_get_attribute",
      "page_wait_for_selector",
      "browser_same_origin_fetch",
      "browser_accessibility_snapshot",
      "page.accessibility_snapshot_v2",
      "page_accessibility_snapshot_v2",
      "browser_locator_click",
      "input.locator_click",
      "input_locator_click",
      "browser_locator_type",
      "input.locator_type",
      "input_locator_type",
      "browser_act_and_observe",
      "browser.act_and_observe",
      "browser_click",
      "browser_type",
      "browser_file_upload_select",
      "browser_select",
      "browser_console_messages",
      "browser_wait_for_navigation",
      "browser_scroll",
      "browser_hover",
      "browser_key_press",
      "browser_set_viewport_size",
      "browser_screenshot_full",
      "browser_screenshot_element",
      "browser_network_start_capture",
      "browser_network_stop_capture",
      "browser_network_get_har",
      "browser_acquire_lease",
      "browser_heartbeat_lease",
      "browser_release_lease",
      "vault_list_credentials_for_active_page",
      "vault_request_credential_use",
      "vault_fill_credential",
      "vault_fill_totp",
  };
  for (std::string_view name : kTabTargetTools) {
    if (name == tool_name) {
      return true;
    }
  }
  return false;
}

// R-8: tools that create or act on a browser/profile rather than an existing
// tab. They require an eligible regular browser (-32006 otherwise).
bool IsProfileTargetTool(std::string_view tool_name) {
  return tool_name == "browser_tab_new";
}

bool IsRefConsumingTool(std::string_view tool_name) {
  return tool_name == "browser_click" || tool_name == "browser_type" ||
         tool_name == "browser_select" ||
         tool_name == "browser_screenshot_element" ||
         tool_name == "browser_scroll" || tool_name == "browser_hover" ||
         tool_name == "vault_fill_credential" ||
         tool_name == "vault_fill_totp";
}

base::DictValue SerializeTabInfo(const MahoMcpSession::TabInfo& tab_info) {
  base::DictValue tab;
  tab.Set("id", tab_info.id);
  tab.Set("title", tab_info.title);
  tab.Set("url", tab_info.url);
  tab.Set("is_active", tab_info.is_active);
  if (!tab_info.stable_id.empty()) {
    tab.Set("stable_id", tab_info.stable_id);
  }
  tab.Set("targetable", tab_info.targetable);
  tab.Set("tab_strip_index", tab_info.tab_strip_index);
  return tab;
}

// Todo 6: input/navigation mutators that require an active tab lease held by
// this session before any delegate dispatch. Lease-control and read-only tools
// are intentionally excluded so they remain available without a lease.
bool IsLeaseGatedMutation(std::string_view tool_name) {
  return tool_name == "browser_click" || tool_name == "browser_type" ||
         tool_name == "browser_locator_click" ||
         tool_name == "input.locator_click" ||
         tool_name == "input_locator_click" ||
         tool_name == "browser_locator_type" ||
         tool_name == "input.locator_type" ||
         tool_name == "input_locator_type" ||
         tool_name == "browser_act_and_observe" ||
         tool_name == "browser.act_and_observe" ||
         tool_name == "browser_file_upload_select" ||
         tool_name == "browser_select" || tool_name == "browser_scroll" ||
         tool_name == "browser_key_press" || tool_name == "browser_navigate" ||
         tool_name == "browser_same_origin_fetch" ||
         tool_name == "vault_fill_credential" || tool_name == "vault_fill_totp";
}

bool IsSensitiveReadTool(std::string_view tool_name) {
  return tool_name == "browser_page_content" ||
         tool_name == "browser_page_text" ||
         tool_name == "browser_page_context" ||
         tool_name == "browser_screenshot_full" ||
         tool_name == "browser_screenshot_element" ||
         tool_name == "browser_accessibility_snapshot" ||
         tool_name == "page.accessibility_snapshot_v2" ||
         tool_name == "page_accessibility_snapshot_v2" ||
         tool_name == "browser_console_messages" ||
         tool_name == "browser_network_get_har" ||
         tool_name == "browser_network_start_capture" ||
         tool_name == "browser_network_stop_capture";
}

[[maybe_unused]] bool RequiresExactOriginGrant(std::string_view tool_name) {
  return false;
}

int TargetErrorCode(MahoMcpTargetError error) {
  switch (error) {
    case MahoMcpTargetError::kNone:
      return 0;
    case MahoMcpTargetError::kTabNotFound:
      return kMahoMcpErrorTabNotFound;
    case MahoMcpTargetError::kNoEligibleActiveTab:
      return kMahoMcpErrorNoEligibleActiveTab;
    case MahoMcpTargetError::kNoEligibleActiveBrowser:
      return kMahoMcpErrorNoEligibleActiveBrowser;
  }
  return kMahoMcpErrorTabNotFound;
}

const char* TargetErrorMessage(MahoMcpTargetError error) {
  switch (error) {
    case MahoMcpTargetError::kNone:
      return "";
    case MahoMcpTargetError::kTabNotFound:
      return kMahoMcpMessageTabNotFound;
    case MahoMcpTargetError::kNoEligibleActiveTab:
      return kMahoMcpMessageNoEligibleActiveTab;
    case MahoMcpTargetError::kNoEligibleActiveBrowser:
      return kMahoMcpMessageNoEligibleActiveBrowser;
  }
  return kMahoMcpMessageTabNotFound;
}

}  // namespace

MahoMcpNetworkCaptureState::MahoMcpNetworkCaptureState() = default;
MahoMcpNetworkCaptureState::~MahoMcpNetworkCaptureState() = default;

void MahoMcpSession::SetBrowserDelegate(MahoMcpBrowserDelegate* delegate) {
  g_browser_delegate = delegate;
}

MahoMcpBrowserDelegate* MahoMcpSession::GetBrowserDelegateForBrowserActions() {
  return g_browser_delegate;
}

void MahoMcpSession::SetLeaseRegistryForBrowserActions(
    MahoMcpLeaseRegistry* lease_registry) {
  g_browser_action_lease_registry = lease_registry;
}

MahoMcpLeaseRegistry* MahoMcpSession::GetLeaseRegistryForBrowserActions() {
  return g_browser_action_lease_registry;
}

void MahoMcpSession::RevokeControllerSession(std::string_view session_id) {
  if (session_id.empty()) {
    return;
  }
  scoped_refptr<MahoMcpControllerState> controller_state =
      GetControllerState(session_id);
  if (controller_state) {
    controller_state->Revoke();
  }
}

#if BUILDFLAG(IS_WIN)
MahoMcpSession::MahoMcpSession(MahoMcpSessionToken* session_token,
                               MahoMcpLeaseRegistry* lease_registry,
                               std::string peer_executable,
                               bool peer_is_trusted_maho)
    : session_token_(session_token),
      lease_registry_(lease_registry),
      session_id_(base::Uuid::GenerateRandomV4().AsLowercaseString()),
      controller_state_(base::MakeRefCounted<MahoMcpControllerState>()),
      peer_executable_(std::move(peer_executable)),
      peer_is_trusted_maho_(peer_is_trusted_maho),
      capability_registry_(std::make_unique<MahoMcpCapabilityRegistry>()),
      network_capture_state_(
          base::MakeRefCounted<MahoMcpNetworkCaptureState>()) {
  DETACH_FROM_SEQUENCE(sequence_checker_);
  TabInfo default_tab;
  default_tab.id = 0;
  default_tab.title = "New Tab";
  default_tab.url = "about:blank";
  default_tab.is_active = true;
  default_tab.targetable = true;
  default_tab.tab_strip_index = 0;
  tabs_.push_back(std::move(default_tab));
  active_tab_id_ = 0;
  controller_state_->SetRevocationCleanup(
      base::SequencedTaskRunner::GetCurrentDefault(),
      base::BindOnce(&RevokeControllerSessionOnIo, session_id_,
                     network_capture_state_));
  RegisterControllerState(session_id_, controller_state_);
}
#else
MahoMcpSession::MahoMcpSession(uid_t peer_uid,
                               MahoMcpLeaseRegistry* lease_registry,
                               std::string peer_executable,
                               bool peer_is_trusted_maho)
    : peer_uid_(peer_uid),
      lease_registry_(lease_registry),
      session_id_(base::Uuid::GenerateRandomV4().AsLowercaseString()),
      controller_state_(base::MakeRefCounted<MahoMcpControllerState>()),
      peer_executable_(std::move(peer_executable)),
      peer_is_trusted_maho_(peer_is_trusted_maho),
      capability_registry_(std::make_unique<MahoMcpCapabilityRegistry>()),
      network_capture_state_(
          base::MakeRefCounted<MahoMcpNetworkCaptureState>()) {
  DETACH_FROM_SEQUENCE(sequence_checker_);
  TabInfo default_tab;
  default_tab.id = 0;
  default_tab.title = "New Tab";
  default_tab.url = "about:blank";
  default_tab.is_active = true;
  default_tab.targetable = true;
  default_tab.tab_strip_index = 0;
  tabs_.push_back(std::move(default_tab));
  active_tab_id_ = 0;
  controller_state_->SetRevocationCleanup(
      base::SequencedTaskRunner::GetCurrentDefault(),
      base::BindOnce(&RevokeControllerSessionOnIo, session_id_,
                     network_capture_state_));
  RegisterControllerState(session_id_, controller_state_);
}
#endif

MahoMcpSession::~MahoMcpSession() {
  controller_state_->Revoke();
  UnregisterControllerState(session_id_);
  if (g_browser_delegate) {
    g_browser_delegate->PublishControlActivity(
        session_id_ + ":connection", session_id_, controller_display_label_,
        last_resolved_target_, MahoBrowserToolRegistry::Category::kUnknown,
        MahoBrowserToolRegistry::Sensitivity::kUnknown,
        MahoMcpActivityPhase::kDisconnected, ++activity_revision_);
  }
}

MahoMcpSession::ConsoleMessage::ConsoleMessage() = default;
MahoMcpSession::ConsoleMessage::~ConsoleMessage() = default;
MahoMcpSession::ConsoleMessage::ConsoleMessage(const ConsoleMessage&) = default;
MahoMcpSession::ConsoleMessage::ConsoleMessage(ConsoleMessage&&) noexcept =
    default;
MahoMcpSession::ConsoleMessage& MahoMcpSession::ConsoleMessage::operator=(
    const ConsoleMessage&) = default;
MahoMcpSession::ConsoleMessage& MahoMcpSession::ConsoleMessage::operator=(
    ConsoleMessage&&) noexcept = default;

MahoMcpSession::TabInfo::TabInfo() = default;
MahoMcpSession::TabInfo::~TabInfo() = default;
MahoMcpSession::TabInfo::TabInfo(const TabInfo&) = default;
MahoMcpSession::TabInfo::TabInfo(TabInfo&&) noexcept = default;
MahoMcpSession::TabInfo& MahoMcpSession::TabInfo::operator=(const TabInfo&) =
    default;
MahoMcpSession::TabInfo& MahoMcpSession::TabInfo::operator=(
    TabInfo&&) noexcept = default;

MahoMcpBrowserDelegate::BookmarkInfo::BookmarkInfo() = default;
MahoMcpBrowserDelegate::BookmarkInfo::~BookmarkInfo() = default;
MahoMcpBrowserDelegate::BookmarkInfo::BookmarkInfo(const BookmarkInfo&) =
    default;
MahoMcpBrowserDelegate::BookmarkInfo::BookmarkInfo(BookmarkInfo&&) noexcept =
    default;
MahoMcpBrowserDelegate::BookmarkInfo&
MahoMcpBrowserDelegate::BookmarkInfo::operator=(const BookmarkInfo&) = default;
MahoMcpBrowserDelegate::BookmarkInfo&
MahoMcpBrowserDelegate::BookmarkInfo::operator=(BookmarkInfo&&) noexcept =
    default;

bool MahoMcpBrowserDelegate::RevalidateRefSnapshot(
    const ResolvedMahoMcpTarget& target,
    uint64_t snapshot_token) {
  return true;
}

void MahoMcpBrowserDelegate::ClickVerified(
    int tab_id,
    ui::AXNodeID ax_id,
    base::OnceCallback<void(InputActionOutcome)> callback) {
  InputActionOutcome outcome;
  outcome.dispatched = Click(tab_id, ax_id);
  outcome.method = "legacy_delegate";
  if (!outcome.dispatched) {
    outcome.reason = "dispatch_failed";
  }
  std::move(callback).Run(std::move(outcome));
}

void MahoMcpBrowserDelegate::TypeVerified(
    int tab_id,
    ui::AXNodeID ax_id,
    const std::string& text,
    base::OnceCallback<void(InputActionOutcome)> callback) {
  InputActionOutcome outcome;
  outcome.dispatched = Type(tab_id, ax_id, text);
  outcome.method = "legacy_delegate";
  if (!outcome.dispatched) {
    outcome.reason = "dispatch_failed";
  }
  std::move(callback).Run(std::move(outcome));
}

MahoMcpBrowserDelegate::LocatorParams::LocatorParams() = default;
MahoMcpBrowserDelegate::LocatorParams::~LocatorParams() = default;
MahoMcpBrowserDelegate::LocatorParams::LocatorParams(
    const LocatorParams&) = default;
MahoMcpBrowserDelegate::LocatorParams&
MahoMcpBrowserDelegate::LocatorParams::operator=(
    const LocatorParams&) = default;
MahoMcpBrowserDelegate::LocatorParams::LocatorParams(
    LocatorParams&&) noexcept = default;
MahoMcpBrowserDelegate::LocatorParams&
MahoMcpBrowserDelegate::LocatorParams::operator=(
    LocatorParams&&) noexcept = default;

MahoMcpBrowserDelegate::LocatorResolution::LocatorResolution() = default;
MahoMcpBrowserDelegate::LocatorResolution::~LocatorResolution() = default;
MahoMcpBrowserDelegate::LocatorResolution::LocatorResolution(
    const LocatorResolution&) = default;
MahoMcpBrowserDelegate::LocatorResolution&
MahoMcpBrowserDelegate::LocatorResolution::operator=(
    const LocatorResolution&) = default;
MahoMcpBrowserDelegate::LocatorResolution::LocatorResolution(
    LocatorResolution&&) noexcept = default;
MahoMcpBrowserDelegate::LocatorResolution&
MahoMcpBrowserDelegate::LocatorResolution::operator=(
    LocatorResolution&&) noexcept = default;

MahoMcpBrowserDelegate::LocatorResolution MahoMcpBrowserDelegate::ResolveLocator(
    int tab_id,
    const LocatorParams& params,
    const MahoMcpSession::RefTable& refs) {
  LocatorResolution res;
  if (params.ref.has_value()) {
    auto it = refs.find(*params.ref);
    if (it != refs.end()) {
      res.success = true;
      res.ax_id = it->second;
      return res;
    }
    res.error_code = "locator_not_found";
    res.hint = "Ref not found in session table";
    return res;
  }
  res.error_code = "locator_not_found";
  res.hint = "Locator not found";
  return res;
}

bool MahoMcpBrowserDelegate::WaitForAutoQuiet(int tab_id, int timeout_ms) {
  return true;
}

bool MahoMcpBrowserDelegate::RequiresDeferredVerifiedInputResponses() const {
  return false;
}


void MahoMcpBrowserDelegate::SameOriginFetch(int tab_id,
                                             const GURL& url,
                                             const std::string& method,
                                             const std::string& body,
                                             const std::string& headers_json,
                                             SameOriginFetchCallback callback) {
  SameOriginFetchResult result;
  result.error = "Same-origin fetch unavailable";
  std::move(callback).Run(std::move(result));
}

void MahoMcpSession::AddConsoleMessageForTesting(int tab_id,
                                                 ConsoleMessage message) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (message.message.size() > 4096) {
    message.message.resize(4096);
  }
  auto& buffer = console_buffers_[tab_id];
  if (buffer.size() >= 500) {
    buffer.erase(buffer.begin());
  }
  buffer.push_back(std::move(message));
}

bool MahoMcpSession::IsHostBlocked(std::string_view host) const {
  if (host.empty() || blocked_domains_.empty()) {
    return false;
  }
  const std::string host_lc = base::ToLowerASCII(host);
  for (const std::string& entry : blocked_domains_) {
    const std::string entry_lc = base::ToLowerASCII(entry);
    // Exact match.
    if (host_lc == entry_lc) {
      return true;
    }
    // Wildcard match: "*.example.com" matches "sub.example.com" and
    // implicitly "example.com" itself.
    static constexpr std::string_view kWildcard = "*.";
    if (base::StartsWith(entry_lc, kWildcard)) {
      const std::string bare = entry_lc.substr(kWildcard.size());
      if (host_lc == bare) {
        return true;
      }
      const std::string suffix = "." + bare;
      if (host_lc.size() > suffix.size() && base::EndsWith(host_lc, suffix)) {
        return true;
      }
    }
  }
  return false;
}

bool MahoMcpSession::HandleAcquireLease(const McpJsonRpcMessage& msg,
                                        const base::DictValue* arguments,
                                        int target_tab_id,
                                        std::vector<std::string>& responses) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!lease_registry_) {
    responses.push_back(
        framer_.BuildErrorResponse(msg.id, -32001, "Lease feature disabled"));
    return true;
  }
  if (!arguments) {
    responses.push_back(framer_.BuildErrorResponse(
        msg.id, -32602, "Invalid params: tab_id required"));
    return true;
  }
  if (!arguments->FindInt("tab_id").has_value()) {
    responses.push_back(framer_.BuildErrorResponse(
        msg.id, -32602, "Invalid params: tab_id must be integer"));
    return true;
  }
  const int ttl_seconds = arguments->FindInt("ttl_seconds").value_or(60);
  // ADR 0016 keeps user-driven acquisition force-stealing. Autonomous
  // sessions explicitly opt into non-stealing admission instead.
  const bool force_steal = !autonomous_;
  auto result = lease_registry_->Acquire(
      static_cast<int64_t>(target_tab_id), session_id_,
      base::Seconds(ttl_seconds), force_steal);
  if (!result.ok) {
    responses.push_back(BuildLeaseContentionErrorLine(
        CloneMessageId(msg.id), target_tab_id, result));
    return true;
  }
  base::DictValue out;
  out.Set("acquired", true);
  out.Set("session_id", session_id_);
  out.Set("tab_id", target_tab_id);
  out.Set("previous_holder", result.previous_holder);
  out.Set("expires_in_seconds",
          static_cast<int>(
              (result.expires_at - base::TimeTicks::Now()).InSeconds()));
  responses.push_back(
      framer_.BuildSuccessResponse(msg.id, base::Value(std::move(out))));
  return true;
}

bool MahoMcpSession::HandleHeartbeatLease(const McpJsonRpcMessage& msg,
                                          const base::DictValue* arguments,
                                          int target_tab_id,
                                          std::vector<std::string>& responses) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!lease_registry_) {
    responses.push_back(
        framer_.BuildErrorResponse(msg.id, -32001, "Lease feature disabled"));
    return true;
  }
  if (!arguments) {
    responses.push_back(framer_.BuildErrorResponse(
        msg.id, -32602, "Invalid params: tab_id required"));
    return true;
  }
  if (!arguments->FindInt("tab_id").has_value()) {
    responses.push_back(framer_.BuildErrorResponse(
        msg.id, -32602, "Invalid params: tab_id must be integer"));
    return true;
  }
  const int ttl_seconds = arguments->FindInt("ttl_seconds").value_or(60);
  base::TimeTicks expires = lease_registry_->Heartbeat(
      static_cast<int64_t>(target_tab_id), session_id_,
      base::Seconds(ttl_seconds));
  if (expires.is_null()) {
    responses.push_back(framer_.BuildErrorResponse(
        msg.id, -32000, "Not lease holder or lease expired"));
    return true;
  }
  base::DictValue out;
  out.Set("heartbeat", true);
  out.Set("expires_in_seconds",
          static_cast<int>((expires - base::TimeTicks::Now()).InSeconds()));
  responses.push_back(
      framer_.BuildSuccessResponse(msg.id, base::Value(std::move(out))));
  return true;
}

bool MahoMcpSession::HandleReleaseLease(const McpJsonRpcMessage& msg,
                                        const base::DictValue* arguments,
                                        int target_tab_id,
                                        std::vector<std::string>& responses) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!lease_registry_) {
    responses.push_back(
        framer_.BuildErrorResponse(msg.id, -32001, "Lease feature disabled"));
    return true;
  }
  if (!arguments) {
    responses.push_back(framer_.BuildErrorResponse(
        msg.id, -32602, "Invalid params: tab_id required"));
    return true;
  }
  if (!arguments->FindInt("tab_id").has_value()) {
    responses.push_back(framer_.BuildErrorResponse(
        msg.id, -32602, "Invalid params: tab_id must be integer"));
    return true;
  }
  const bool released = lease_registry_->Release(
      static_cast<int64_t>(target_tab_id), session_id_);
  base::DictValue out;
  out.Set("released", released);
  responses.push_back(
      framer_.BuildSuccessResponse(msg.id, base::Value(std::move(out))));
  return true;
}

std::vector<std::string> MahoMcpSession::ProcessData(const std::string& data) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  std::vector<std::string> responses;

  if (state_ == State::kClosed) {
    return responses;
  }

#if !BUILDFLAG(IS_WIN)
  // Check peer UID on first data (POSIX only; Windows uses DACL).
  if (peer_uid_ != getuid()) {
    LOG(WARNING) << "MCP: Rejecting connection from UID " << peer_uid_
                 << " (browser UID: " << getuid() << ")";
    state_ = State::kClosed;
    return responses;
  }
#endif

  framer_.AppendData(data);

  // Drain any auto-generated error responses (parse error, invalid request).
  while (auto error_resp = framer_.TakeNextResponse()) {
    responses.push_back(RedactJsonRpcLine(std::move(*error_resp)));
  }

  // Process parsed messages.
  while (auto msg = framer_.TakeNextParsedMessage()) {
    if (state_ == State::kClosed) {
      break;
    }

    if (state_ == State::kAwaitingInitialize) {
      if (msg->method != "initialize") {
        // Any method other than "initialize" in this state is an error.
        std::string err = framer_.BuildErrorResponse(
            msg->id, -32600, "Invalid Request: initialize expected");
        responses.push_back(std::move(err));
        state_ = State::kClosed;
        break;
      }
      HandleInitialize(*msg, responses);
    } else if (state_ == State::kActive) {
      HandleActiveMethod(*msg, responses);
    }
  }

  for (std::string& response : responses) {
    response = RedactJsonRpcLine(std::move(response));
  }
  return responses;
}

void MahoMcpSession::HandleInitialize(const McpJsonRpcMessage& msg,
                                      std::vector<std::string>& responses) {
  if (!msg.params.has_value() || !msg.params->is_dict()) {
    std::string err =
        framer_.BuildErrorResponse(msg.id, -32602, "Invalid params");
    responses.push_back(std::move(err));
    state_ = State::kClosed;
    return;
  }

  const auto& params = msg.params->GetDict();

  const std::string* protocol_version = params.FindString("protocolVersion");
  if (!protocol_version || *protocol_version != kMcpProtocolVersion) {
    std::string err = framer_.BuildErrorResponse(
        msg.id, -32602, "Invalid params: unsupported protocolVersion");
    responses.push_back(std::move(err));
    state_ = State::kClosed;
    return;
  }

  if (const base::Value* autonomous_value = params.Find("autonomous");
      autonomous_value && !autonomous_value->is_bool()) {
    std::string err = framer_.BuildErrorResponse(
        msg.id, -32602, "Invalid params: autonomous must be boolean");
    responses.push_back(std::move(err));
    state_ = State::kClosed;
    return;
  }
  autonomous_ = params.FindBool("autonomous").value_or(false);

#if BUILDFLAG(IS_WIN)
  // Validate DPAPI session token on Windows.
  if (session_token_) {
    const std::string* auth_token_b64 = params.FindString("authToken");
    if (!auth_token_b64) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32600, "Authentication required: authToken missing");
      responses.push_back(std::move(err));
      state_ = State::kClosed;
      return;
    }

    std::optional<std::vector<uint8_t>> decoded =
        base::Base64Decode(*auth_token_b64);
    if (!decoded.has_value()) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32602, "Invalid params: authToken is not valid base64");
      responses.push_back(std::move(err));
      state_ = State::kClosed;
      return;
    }

    if (!session_token_->Matches(base::span(*decoded))) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32600, "Authentication failed: token mismatch");
      responses.push_back(std::move(err));
      state_ = State::kClosed;
      return;
    }
  }
#endif

  const std::string* controller_hint = params.FindString("controllerKind");
  const ControllerIdentity identity =
      ClassifyController(
          controller_hint ? *controller_hint : std::string_view(),
          peer_executable_, peer_is_trusted_maho_);
  controller_kind_ = identity.kind;
  controller_display_label_ = identity.display_label;
  if (controller_kind_ == MahoMcpControllerKind::kMahoCli && autonomous_) {
    controller_display_label_ = "Maho Agent";
  }

  base::DictValue server_info;
  server_info.Set("name", kServerName);
  server_info.Set("version", kServerVersion);
  server_info.Set(
      "catalogDiagnostics",
      MahoBrowserToolRegistry::SerializeDiagnostics(
          MahoBrowserToolRegistry::kPublicMcp));

  base::DictValue tools_cap;
  base::DictValue capabilities;
  capabilities.Set("tools", std::move(tools_cap));

  base::DictValue session_info;
  session_info.Set("id", session_id_);
  session_info.Set("displayLabel", controller_display_label_);
  session_info.Set("controllerKind", identity.wire_kind);
  session_info.Set("autonomous", autonomous_);

  base::DictValue result;
  result.Set("serverInfo", std::move(server_info));
  result.Set("sessionInfo", std::move(session_info));
  result.Set("capabilities", std::move(capabilities));
  result.Set("protocolVersion", kMcpProtocolVersion);

  std::string resp =
      framer_.BuildSuccessResponse(msg.id, base::Value(std::move(result)));
  responses.push_back(std::move(resp));
  state_ = State::kActive;
}

void MahoMcpSession::HandleActiveMethod(const McpJsonRpcMessage& msg,
                                        std::vector<std::string>& responses) {
  if (controller_state_->is_revoked()) {
    if (msg.id.has_value()) {
      responses.push_back(framer_.BuildErrorResponse(
          msg.id, -32004, "Controller session was revoked"));
    }
    return;
  }
  if (msg.method == "maho/control/list") {
    HandleControlList(msg, responses);
    return;
  }
  if (msg.method == "maho/control/call") {
    HandleControlCall(msg, responses);
    return;
  }
  if (msg.method == "tools/list") {
    const MahoMcpFeatureGates gates =
        g_browser_delegate ? g_browser_delegate->GetFeatureGates()
                           : MahoMcpFeatureGates{};
    base::DictValue result;
    result.Set("catalogDiagnostics",
               MahoBrowserToolRegistry::SerializeDiagnostics(
                   MahoBrowserToolRegistry::kPublicMcp, gates.mail_enabled,
                   gates.routines_enabled, gates.vault_enabled));
    result.Set("tools",
               MahoBrowserToolRegistry::SerializePublicMcpCapabilities(
                   gates.mail_enabled, gates.routines_enabled,
                   gates.vault_enabled));

    if (msg.id.has_value()) {
      std::string resp =
          framer_.BuildSuccessResponse(msg.id, base::Value(std::move(result)));
      responses.push_back(std::move(resp));
    }
    return;
  }

  if (msg.method == "tools/call") {
    HandleToolCall(msg, responses);
    return;
  }

  // Unknown method.
  if (msg.id.has_value()) {
    std::string err =
        framer_.BuildErrorResponse(msg.id, -32601, "Method not found");
    responses.push_back(std::move(err));
  }
}

void MahoMcpSession::HandleControlList(
    const McpJsonRpcMessage& msg,
    std::vector<std::string>& responses) {
  if (controller_kind_ == MahoMcpControllerKind::kThirdParty) {
    responses.push_back(framer_.BuildErrorResponse(
        msg.id, -32003,
        "Control discovery requires a trusted Maho controller"));
    return;
  }
  if (!msg.id.has_value()) {
    return;
  }
  base::ListValue controls;
  const MahoMcpFeatureGates gates =
      g_browser_delegate ? g_browser_delegate->GetFeatureGates()
                         : MahoMcpFeatureGates{};
  for (const auto* descriptor :
       MahoBrowserToolRegistry::GetCapabilitiesForSurface(
           MahoBrowserToolRegistry::kControlPlane, gates.mail_enabled,
           gates.routines_enabled, gates.vault_enabled)) {
    if (auto serialized =
            MahoBrowserToolRegistry::SerializeControlPlaneCapability(
                *descriptor)) {
      controls.Append(std::move(*serialized));
    }
  }
  base::DictValue result;
  result.Set("tools", std::move(controls));
  result.Set("surface", "control_plane");
  responses.push_back(
      framer_.BuildSuccessResponse(msg.id, base::Value(std::move(result))));
}

void MahoMcpSession::HandleControlCall(
    const McpJsonRpcMessage& msg,
    std::vector<std::string>& responses) {
  if (controller_kind_ == MahoMcpControllerKind::kThirdParty) {
    responses.push_back(framer_.BuildErrorResponse(
        msg.id, -32003, "Control calls require a trusted Maho controller"));
    return;
  }
  if (!msg.id.has_value() || !msg.params || !msg.params->is_dict()) {
    if (msg.id.has_value()) {
      responses.push_back(
          framer_.BuildErrorResponse(msg.id, -32602, "Invalid params"));
    }
    return;
  }
  const auto& params = msg.params->GetDict();
  const std::string* name = params.FindString("name");
  const auto* descriptor =
      name ? MahoBrowserToolRegistry::FindCapability(*name) : nullptr;
  if (!descriptor ||
      !MahoBrowserToolRegistry::HasSurface(
          *descriptor, MahoBrowserToolRegistry::kControlPlane)) {
    responses.push_back(framer_.BuildErrorResponse(
        msg.id, -32601, "Unknown or unavailable control capability"));
    return;
  }
  const MahoMcpFeatureGates gates =
      g_browser_delegate ? g_browser_delegate->GetFeatureGates()
                         : MahoMcpFeatureGates{};
  const auto enabled_controls =
      MahoBrowserToolRegistry::GetCapabilitiesForSurface(
          MahoBrowserToolRegistry::kControlPlane, gates.mail_enabled,
          gates.routines_enabled, gates.vault_enabled);
  if (std::ranges::find(enabled_controls, descriptor) ==
      enabled_controls.end()) {
    responses.push_back(framer_.BuildErrorResponse(
        msg.id, -32601, "Unknown or unavailable control capability"));
    return;
  }
  HandleToolCall(msg, responses, true);
}

void MahoMcpSession::HandleToolCall(const McpJsonRpcMessage& msg,
                                    std::vector<std::string>& responses,
                                    bool control_plane) {
  if (!msg.id.has_value()) {
    return;
  }

  if (!msg.params.has_value() || !msg.params->is_dict()) {
    std::string err =
        framer_.BuildErrorResponse(msg.id, -32602, "Invalid params");
    responses.push_back(std::move(err));
    return;
  }

  const auto& params = msg.params->GetDict();
  const std::string* tool_name = params.FindString("name");
  if (!tool_name) {
    std::string err = framer_.BuildErrorResponse(
        msg.id, -32602, "Invalid params: missing tool name");
    responses.push_back(std::move(err));
    return;
  }

  // OQ-2 lockdown: explicitly reject evaluate_js.
  if (*tool_name == "evaluate_js") {
    std::string err = framer_.BuildErrorResponse(
        msg.id, -32601, "Method not found: evaluate_js is not available");
    responses.push_back(std::move(err));
    return;
  }

  // Connection health-check only: intentionally functional but absent from
  // the browser capability catalog and every advertised surface.
  if (*tool_name == "ping") {
    base::DictValue result;
    result.Set("pong", true);
    auto redacted = MahoMcpFirewall::Wrap(base::Value(std::move(result)));
    responses.push_back(
        framer_.BuildSuccessResponse(msg.id, redacted.get().Clone()));
    return;
  }

  const base::DictValue* arguments = params.FindDict("arguments");
  const auto* descriptor = MahoBrowserToolRegistry::FindCapability(*tool_name);
  if (!descriptor) {
    responses.push_back(framer_.BuildErrorResponse(
        msg.id, -32601, "Unknown browser capability"));
    return;
  }
  if (!control_plane &&
      !MahoBrowserToolRegistry::HasSurface(
          *descriptor, MahoBrowserToolRegistry::kPublicMcp)) {
    responses.push_back(framer_.BuildErrorResponse(
        msg.id, -32601, "Capability is not available on the public MCP"));
    return;
  }
  const std::string activity_id =
      session_id_ + ":" + base::NumberToString(++activity_revision_);
  const uint64_t initial_revision = activity_revision_;
  const uint64_t terminal_revision = ++activity_revision_;
  const MahoMcpActivityPhase initial_phase =
      descriptor->mutability == MahoBrowserToolRegistry::Mutability::kReadOnly
          ? MahoMcpActivityPhase::kReading
          : MahoMcpActivityPhase::kActing;
  const size_t response_count_before = responses.size();
  const double activity_started_at_seconds =
      base::Time::Now().InSecondsFSinceUnixEpoch();
  auto approval_outcome =
      std::make_shared<std::string>("not_requested");
  auto terminal_activity = base::ScopedClosureRunner(base::BindOnce(
      [](std::string activity_id, std::string controller_session_id,
          std::string controller_label, ResolvedMahoMcpTarget* target,
          MahoBrowserToolRegistry::Category category,
          MahoBrowserToolRegistry::Sensitivity sensitivity,
          const MahoBrowserToolRegistry::CapabilityDescriptor* descriptor,
          std::string controller_type,
          std::shared_ptr<std::string> approval,
          double started_at_seconds,
          uint64_t terminal_revision, std::vector<std::string>* responses,
          size_t response_count_before) {
        if (responses->size() == response_count_before) {
          return;
        }
        const bool failed =
            responses->back().find("\"error\"") != std::string::npos;
        if (!failed) {
          responses->back() = AttachExecutionReceipt(
              std::move(responses->back()), *descriptor, activity_id,
              controller_session_id, controller_label, controller_type,
              *target, *approval, started_at_seconds);
        }
        if (g_browser_delegate) {
          g_browser_delegate->PublishControlActivity(
              activity_id, controller_session_id, controller_label, *target,
              category, sensitivity,
              failed ? MahoMcpActivityPhase::kFailed
                     : MahoMcpActivityPhase::kCompleted,
              terminal_revision);
        }
      },
      activity_id, session_id_, controller_display_label_,
      &last_resolved_target_, descriptor->category, descriptor->sensitivity,
      descriptor, ReceiptControllerType(controller_kind_), approval_outcome,
      activity_started_at_seconds, terminal_revision, &responses,
      response_count_before));

  // R-8: resolve the tool's target once, before any delegate work, and reject
  // OTR/ineligible targets with a uniform non-revealing error. Never fall back
  // to a cached/zero-active target to rescue a denied context.
  last_resolved_target_ = ResolvedMahoMcpTarget();
  if (g_browser_delegate) {
    MahoMcpTargetResolution resolution;
    bool has_resolution = false;
    if (IsTabTargetTool(*tool_name)) {
      const int requested_tab_id = (arguments && arguments->FindInt("tab_id"))
                                       ? arguments->FindInt("tab_id").value()
                                       : 0;
      resolution = g_browser_delegate->ResolveTabTarget(requested_tab_id);
      has_resolution = true;
    } else if (IsProfileTargetTool(*tool_name)) {
      resolution = g_browser_delegate->ResolveProfileTarget();
      has_resolution = true;
    }
    const bool is_internal_cli =
        (controller_kind_ == MahoMcpControllerKind::kMahoCli ||
         controller_kind_ == MahoMcpControllerKind::kMahoCliRepl);
    if (has_resolution && resolution.error != MahoMcpTargetError::kNone) {
      if (!is_internal_cli) {
        responses.push_back(
            framer_.BuildErrorResponse(msg.id, TargetErrorCode(resolution.error),
                                       TargetErrorMessage(resolution.error)));
        return;
      }
    }
    if (has_resolution) {
      if (resolution.target.valid) {
        last_resolved_target_ = resolution.target;
      } else {
        last_resolved_target_.valid = true;
        last_resolved_target_.tab_id = (arguments && arguments->FindInt("tab_id"))
                                           ? arguments->FindInt("tab_id").value()
                                           : 0;
      }
    }
  }
  if (g_browser_delegate) {
    g_browser_delegate->PublishControlActivity(
        activity_id, session_id_, controller_display_label_,
        last_resolved_target_, descriptor->category, descriptor->sensitivity,
        initial_phase, initial_revision);
  }
  DeferredResponseSender terminal_deferred_sender;
  if (deferred_response_sender_) {
    terminal_deferred_sender = MakeTerminalAwareSender(
        deferred_response_sender_, activity_id, session_id_, controller_state_,
        controller_display_label_, last_resolved_target_, descriptor->category,
        descriptor->sensitivity, terminal_revision, descriptor,
        ReceiptControllerType(controller_kind_), approval_outcome,
        activity_started_at_seconds);
  }

  // R-8: once the gate has resolved a tab-target tool against a production
  // delegate, every handler must act on that single stable resolution — never
  // re-resolve an omitted/zero request by passing 0, active_tab_id_, or a raw
  // argument, which could target a different (or OTR) tab after an active
  // switch. Test delegates without a private-context model fall through to the
  // legacy in-memory path below.
  const bool use_resolved_target =
      g_browser_delegate && IsTabTargetTool(*tool_name);
  const int resolved_tab_id =
      use_resolved_target ? last_resolved_target_.tab_id : 0;
  const auto has_stale_ref = [&] {
    return IsRefConsumingTool(*tool_name) && arguments &&
           arguments->FindInt("ref").has_value() &&
           (ref_table_target_.tab_id != last_resolved_target_.tab_id ||
            ref_table_target_.browser_id != last_resolved_target_.browser_id ||
            (g_browser_delegate &&
             (!g_browser_delegate->RevalidateTarget(ref_table_target_) ||
              (ref_snapshot_token_ != 0 &&
               !g_browser_delegate->RevalidateRefSnapshot(
                   last_resolved_target_, ref_snapshot_token_)))));
  };
  if (has_stale_ref()) {
    responses.push_back(framer_.BuildErrorResponse(
        msg.id, kMahoMcpErrorStaleReference,
        kMahoMcpMessageStaleReference));
    return;
  }

  // ----- sandbox ownership gate (B3) -----
  // When the capability registry owns any tabs, every sensitive read must
  // target a sandbox-owned tab. Tab-list metadata remains discoverable.
  if (capability_registry_ && !capability_registry_->ListOwnedTabs().empty() &&
      IsSensitiveReadTool(*tool_name) &&
      !capability_registry_->IsTabOwned(resolved_tab_id)) {
    base::DictValue data;
    data.Set("status", "tab_adoption_required");
    data.Set("tab_id", resolved_tab_id);
    base::DictValue error;
    error.Set("code", -32000);
    error.Set("message", "Tab not owned by this session: adopt it first");
    error.Set("data", std::move(data));
    base::DictValue response;
    response.Set("id", msg.id ? msg.id->Clone() : base::Value());
    response.Set("error", std::move(error));
    std::string out;
    base::JSONWriter::Write(base::Value(std::move(response)), &out);
    out.push_back('\n');
    responses.push_back(std::move(out));
    return;
  }

  const bool is_internal_cli =
      (controller_kind_ == MahoMcpControllerKind::kMahoCli ||
       controller_kind_ == MahoMcpControllerKind::kMahoCliRepl);

  // Todo 6: single mutation gate. After stable concrete target resolution and
  // before any delegate dispatch, input/navigation mutators require an active
  // lease held by THIS session on the resolved tab. A session without a lease
  // registry (test wiring) is not gated; lease-control and read-only tools are
  // never classified as mutations, so they remain available.
  if (lease_registry_ && IsLeaseGatedMutation(*tool_name)) {
    const int gate_tab_id = use_resolved_target
                                ? resolved_tab_id
                                : (arguments && arguments->FindInt("tab_id")
                                       ? arguments->FindInt("tab_id").value()
                                       : active_tab_id_);
    if (!lease_registry_->IsHeldBy(static_cast<int64_t>(gate_tab_id),
                                   session_id_)) {
      const std::string* lease_arg =
          arguments ? arguments->FindString("lease") : nullptr;
      if (lease_arg && *lease_arg == "scoped") {
        // Scoped lease will be acquired and managed during action execution.
      } else if (is_internal_cli && !autonomous_) {
        lease_registry_->Acquire(static_cast<int64_t>(gate_tab_id),
                                 session_id_, base::Seconds(60),
                                 /*force_steal=*/true);
      } else {
        responses.push_back(framer_.BuildErrorResponse(
            msg.id, kMahoMcpErrorLeaseRequired,
            "active tab lease required for this mutation"));
        return;
      }
    }
  }

  const ai::MailToolClass mail_tool_class = ai::ClassifyMailTool(*tool_name);

  const bool credential_typing_requested =
      *tool_name == "browser_type" && arguments &&
      arguments->FindBool("allow_credentials").value_or(false);
  const ai::CredentialTypingAuthorizationDecision credential_authorization =
      ai::AuthorizeCredentialTyping(credential_typing_requested);
  bool credential_typing_approval_satisfied = false;
  if (credential_authorization.action ==
      ai::CredentialTypingAuthorizationAction::kRequireApproval) {
    if (!g_browser_delegate) {
      responses.push_back(framer_.BuildErrorResponse(
          msg.id, -32008,
          std::string(ai::kCredentialTypingApprovalRequired)));
      return;
    }
    if (!g_browser_delegate->ConfirmCredentialTypingApproval(
            *tool_name, last_resolved_target_)) {
      *approval_outcome = "denied";
      responses.push_back(framer_.BuildErrorResponse(
          msg.id, -32008, std::string(ai::kCredentialTypingDenied)));
      return;
    }
    credential_typing_approval_satisfied = true;
    *approval_outcome = "approved";
  }

  if (g_browser_delegate && descriptor->changes_authority &&
      !descriptor->browser_action_contract &&
      mail_tool_class == ai::MailToolClass::kNotMail) {
    if (!is_internal_cli && !g_browser_delegate->ConfirmBrowserActionApproval(
            *tool_name, last_resolved_target_)) {
      *approval_outcome = "denied";
      responses.push_back(framer_.BuildErrorResponse(
          msg.id, kMahoMcpErrorApprovalDenied,
          "authority_change_approval_denied"));
      return;
    }
    *approval_outcome = "approved";
  }

  if (g_browser_delegate && descriptor->browser_action_contract &&
      ai::RequiresApproval(*descriptor->browser_action_contract)) {
    if (!is_internal_cli && !credential_typing_approval_satisfied &&
        !g_browser_delegate->ConfirmBrowserActionApproval(
            *tool_name, last_resolved_target_)) {
      *approval_outcome = "denied";
      responses.push_back(framer_.BuildErrorResponse(
          msg.id, kMahoMcpErrorApprovalDenied,
          "browser_action_approval_denied"));
      return;
    }
    *approval_outcome = "approved";
  }

  auto reject_invalid_mail_params = [&](std::string message) {
    responses.push_back(framer_.BuildErrorResponse(msg.id, -32602, message));
  };
  auto reject_mail_broker_unavailable = [&]() {
    responses.push_back(
        framer_.BuildErrorResponse(msg.id, -32000, kMailReadBrokerUnavailable));
  };
  auto mail_broker_ready = [&]() {
    if (g_browser_delegate && deferred_response_sender_) {
      return true;
    }
    reject_mail_broker_unavailable();
    return false;
  };
  auto make_mail_completion = [&](bool recheck_read_consent) {
    DeferredResponseSender sender = terminal_deferred_sender;
    return MakeMailReadCompletion(CloneMessageId(msg.id), std::move(sender),
                                  *tool_name, recheck_read_consent);
  };
  if (mail_tool_class != ai::MailToolClass::kNotMail) {
    const ai::MailAuthorizationDecision authorization =
        g_browser_delegate
            ? ai::AuthorizeMailTool(
                  *tool_name, g_browser_delegate->GetMailAuthorizationContext())
            : ai::MailAuthorizationDecision{};
    if (authorization.action == ai::MailAuthorizationAction::kDeny) {
      responses.push_back(framer_.BuildErrorResponse(
          msg.id, -32008, std::string(authorization.reason_code)));
      return;
    }
    if (authorization.action == ai::MailAuthorizationAction::kRequireApproval &&
        (!g_browser_delegate || !g_browser_delegate->ConfirmMailToolApproval(
                                    *tool_name, "[REDACTED]"))) {
      *approval_outcome = "denied";
      responses.push_back(framer_.BuildErrorResponse(
          msg.id, -32008, "mail_typed_approval_denied"));
      return;
    }
    if (authorization.action == ai::MailAuthorizationAction::kRequireApproval) {
      *approval_outcome = "approved";
    }
  }

  if (has_stale_ref()) {
    responses.push_back(framer_.BuildErrorResponse(
        msg.id, kMahoMcpErrorStaleReference,
        kMahoMcpMessageStaleReference));
    return;
  }

  if (controller_state_->is_revoked()) {
    responses.push_back(framer_.BuildErrorResponse(
        msg.id, -32004, "Controller session was revoked"));
    return;
  }
  if (g_browser_delegate &&
      descriptor->mutability == MahoBrowserToolRegistry::Mutability::kMutable &&
      last_resolved_target_.valid &&
      !g_browser_delegate->RevalidateTarget(last_resolved_target_)) {
    responses.push_back(framer_.BuildErrorResponse(
        msg.id, kMahoMcpErrorTabNotFound, kMahoMcpMessageTabNotFound));
    return;
  }

  auto append_browser_action_response =
      [this, &msg,
       &responses](MahoMcpBrowserActionHandler::Result action_result) {
        if (action_result.error.has_value()) {
          responses.push_back(framer_.BuildErrorResponse(
              msg.id, action_result.error->code, action_result.error->message));
          return;
        }

        auto redacted =
            MahoMcpFirewall::Wrap(base::Value(std::move(action_result.value)));
        std::string resp =
            framer_.BuildSuccessResponse(msg.id, redacted.get().Clone());
        responses.push_back(std::move(resp));
      };

  auto serialize_verified_action_response =
      [](std::optional<base::Value> message_id, int ref, std::string action,
         InputActionOutcome outcome) {
        if (!outcome.dispatched) {
          return BuildJsonRpcErrorLine(
              std::move(message_id), -32000,
              "Action failed: " + action + " could not be dispatched");
        }
        if (outcome.verified.has_value() && !*outcome.verified) {
          base::DictValue data;
          data.Set("action", action);
          data.Set("ref", ref);
          data.Set("reason", outcome.reason);
          data.Set("remedy", "capture a new accessibility snapshot and retry");
          return BuildJsonRpcErrorLine(
              std::move(message_id), kMahoMcpErrorActionNotApplied,
              kMahoMcpMessageActionNotApplied, base::Value(std::move(data)));
        }
        base::DictValue result;
        result.Set(action == "click" ? "clicked" : "typed", true);
        result.Set("ref", ref);
        result.Set("verified", outcome.verified.value_or(false));
        result.Set("method", outcome.method);
        auto redacted = MahoMcpFirewall::Wrap(base::Value(std::move(result)));
        return BuildJsonRpcSuccessLine(std::move(message_id),
                                       redacted.get().Clone());
      };

  if (*tool_name == "browser_tab_list") {
    base::ListValue list;
    if (g_browser_delegate) {
      for (const auto& t : g_browser_delegate->GetTabList()) {
        list.Append(SerializeTabInfo(t));
      }
    } else {
      for (const auto& t : tabs_) {
        list.Append(SerializeTabInfo(t));
      }
    }
    base::DictValue result;
    result.Set("tabs", std::move(list));
    auto redacted = MahoMcpFirewall::Wrap(base::Value(std::move(result)));
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, redacted.get().Clone());
    responses.push_back(std::move(resp));
    return;
  }

  if (*tool_name == "browser_tab_get") {
    if (!arguments || !arguments->FindInt("tab_id")) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32602, "Invalid params: tab_id required");
      responses.push_back(std::move(err));
      return;
    }
    int target_id = arguments->FindInt("tab_id").value();
    base::DictValue result;
    bool found_real = false;
    if (g_browser_delegate) {
      for (const auto& t : g_browser_delegate->GetTabList()) {
        if (t.id == target_id) {
          result = SerializeTabInfo(t);
          found_real = true;
          break;
        }
      }
    }
    if (!found_real) {
      if (g_browser_delegate) {
        std::string err = framer_.BuildErrorResponse(
            msg.id, kMahoMcpErrorTabNotFound, kMahoMcpMessageTabNotFound);
        responses.push_back(std::move(err));
        return;
      }
      const TabInfo* found = nullptr;
      for (const auto& t : tabs_) {
        if (t.id == target_id) {
          found = &t;
          break;
        }
      }
      if (!found) {
        std::string err =
            framer_.BuildErrorResponse(msg.id, -32000, "Unknown tab_id");
        responses.push_back(std::move(err));
        return;
      }
      result = SerializeTabInfo(*found);
    }
    auto redacted = MahoMcpFirewall::Wrap(base::Value(std::move(result)));
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, redacted.get().Clone());
    responses.push_back(std::move(resp));
    return;
  }

  if (*tool_name == "browser_navigate") {
    if (!arguments || !arguments->FindString("url")) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32602, "Invalid params: url required");
      responses.push_back(std::move(err));
      return;
    }
    // D1: allowed_domains enforcement.
    const std::string& url_str = *arguments->FindString("url");
    GURL nav_url(url_str);
    if (!nav_url.is_valid()) {
      // Agents quote href values verbatim ("/sites", "?page=2"). Resolve
      // relative references against the target tab's current URL instead of
      // rejecting them, matching how a user clicking the link would behave.
      const int base_tab_id =
          use_resolved_target ? resolved_tab_id : active_tab_id_;
      if (g_browser_delegate) {
        for (const auto& t : g_browser_delegate->GetTabList()) {
          if (t.id == base_tab_id) {
            nav_url = GURL(t.url).Resolve(url_str);
            break;
          }
        }
      }
    }
    if (!nav_url.is_valid()) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32602, "Invalid params: url is not a valid GURL");
      responses.push_back(std::move(err));
      return;
    }
    // Reject host-less/dangerous schemes outright. data:, javascript:, file:,
    // chrome: and friends carry no host, so the disallow list can never
    // constrain them; automated (MCP client) navigation must never reach
    // these schemes regardless of the disallow list.
    if (nav_url.SchemeIs("data") || nav_url.SchemeIs("javascript") ||
        nav_url.SchemeIs("file") || nav_url.SchemeIs("chrome") ||
        nav_url.SchemeIs("chrome-untrusted") || nav_url.SchemeIs("blob") ||
        nav_url.SchemeIs("filesystem")) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32000,
          "Navigation blocked: scheme not permitted for automated navigation");
      responses.push_back(std::move(err));
      return;
    }
    // Origin grant check bypassed for browser control
    if (IsHostBlocked(nav_url.host())) {
      base::DictValue data;
      data.Set("host", nav_url.host());
      data.Set("status", "domain_blocked");
      base::DictValue error;
      error.Set("code", -32000);
      error.Set("message", "Navigation blocked: host is in the disallow list");
      error.Set("data", std::move(data));
      base::DictValue response;
      response.Set("id", msg.id ? msg.id->Clone() : base::Value());
      response.Set("error", std::move(error));
      std::string out;
      base::JSONWriter::Write(base::Value(std::move(response)), &out);
      out.push_back('\n');
      responses.push_back(std::move(out));
      return;
    }

    if (g_browser_delegate) {
      g_browser_delegate->Navigate(
          use_resolved_target ? resolved_tab_id : active_tab_id_, nav_url);
    }
    ref_to_ax_id_.clear();
    ref_table_target_ = ResolvedMahoMcpTarget();
    ref_snapshot_token_ = 0;

    // Update active tab URL
    for (auto& t : tabs_) {
      if (t.id == active_tab_id_) {
        t.url = nav_url.spec();
        t.title = nav_url.host();
      }
    }
    base::DictValue result;
    result.Set("navigated", true);
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, base::Value(std::move(result)));
    responses.push_back(std::move(resp));
    return;
  }

  if (*tool_name == "browser_set_blocked_domains") {
    if (!arguments) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32602, "Invalid params: domains list required");
      responses.push_back(std::move(err));
      return;
    }
    const base::ListValue* domains = arguments->FindList("domains");
    if (!domains) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32602, "Invalid params: domains must be a list of strings");
      responses.push_back(std::move(err));
      return;
    }
    std::set<std::string> new_set;
    for (const auto& d : *domains) {
      if (!d.is_string()) {
        std::string err = framer_.BuildErrorResponse(
            msg.id, -32602, "Invalid params: domains entries must be strings");
        responses.push_back(std::move(err));
        return;
      }
      new_set.insert(base::ToLowerASCII(d.GetString()));
    }
    blocked_domains_ = std::move(new_set);
    base::DictValue result;
    result.Set("count", static_cast<int>(blocked_domains_.size()));
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, base::Value(std::move(result)));
    responses.push_back(std::move(resp));
    return;
  }

  if (*tool_name == "browser_get_blocked_domains") {
    base::ListValue domains;
    for (const auto& d : blocked_domains_) {
      domains.Append(d);
    }
    base::DictValue result;
    result.Set("domains", std::move(domains));
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, base::Value(std::move(result)));
    responses.push_back(std::move(resp));
    return;
  }

  // ----- atomic exact-origin grants (sandbox capability) -----

  if (*tool_name == "browser_grant_exact_origin") {
    if (!arguments || !arguments->FindString("origin")) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32602, "Invalid params: origin required");
      responses.push_back(std::move(err));
      return;
    }
    const std::string& origin_str = *arguments->FindString("origin");
    const GURL grant_url(origin_str);
    if (!grant_url.is_valid()) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32602, "Invalid params: url is not a valid GURL");
      responses.push_back(std::move(err));
      return;
    }
    const url::Origin origin = url::Origin::Create(grant_url);
    if (!capability_registry_->IsValidGrantOrigin(origin)) {
      base::DictValue data;
      data.Set("status", "unsupported_origin");
      base::DictValue error;
      error.Set("code", -32000);
      error.Set("message",
                "Origin not grantable: use an exact http/https origin");
      error.Set("data", std::move(data));
      base::DictValue response;
      response.Set("id", msg.id ? msg.id->Clone() : base::Value());
      response.Set("error", std::move(error));
      std::string out;
      base::JSONWriter::Write(base::Value(std::move(response)), &out);
      out.push_back('\n');
      responses.push_back(std::move(out));
      return;
    }
    bool added = capability_registry_->GrantExactOrigin(origin);
    base::DictValue result;
    result.Set("granted", added);
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, base::Value(std::move(result)));
    responses.push_back(std::move(resp));
    return;
  }

  if (*tool_name == "browser_revoke_exact_origin") {
    if (!arguments || !arguments->FindString("origin")) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32602, "Invalid params: origin required");
      responses.push_back(std::move(err));
      return;
    }
    const std::string& origin_str = *arguments->FindString("origin");
    const GURL revoke_url(origin_str);
    const url::Origin origin = url::Origin::Create(revoke_url);
    bool removed = capability_registry_->RevokeExactOrigin(origin);
    base::DictValue result;
    result.Set("revoked", removed);
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, base::Value(std::move(result)));
    responses.push_back(std::move(resp));
    return;
  }

  if (*tool_name == "browser_list_exact_origins") {
    base::ListValue origins;
    for (const url::Origin& o : capability_registry_->ListGrantedOrigins()) {
      origins.Append(o.Serialize());
    }
    base::DictValue result;
    result.Set("origins", std::move(origins));
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, base::Value(std::move(result)));
    responses.push_back(std::move(resp));
    return;
  }

  // ----- sandbox tab ownership -----

  if (*tool_name == "browser_adopt_tab") {
    if (!arguments) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32602, "Invalid params: tab_id required");
      responses.push_back(std::move(err));
      return;
    }
    std::optional<int> tab_id = arguments->FindInt("tab_id");
    if (!tab_id.has_value()) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32602, "Invalid params: tab_id must be an integer");
      responses.push_back(std::move(err));
      return;
    }
    auto r = capability_registry_->AdoptTab(*tab_id);
    base::DictValue result;
    result.Set("adopted", r == MahoMcpCapabilityRegistry::AdoptResult::kOk);
    if (r == MahoMcpCapabilityRegistry::AdoptResult::kConflict) {
      result.Set("status", "conflict");
    }
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, base::Value(std::move(result)));
    responses.push_back(std::move(resp));
    return;
  }

  if (*tool_name == "browser_release_tab") {
    if (!arguments) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32602, "Invalid params: tab_id required");
      responses.push_back(std::move(err));
      return;
    }
    std::optional<int> tab_id = arguments->FindInt("tab_id");
    if (!tab_id.has_value()) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32602, "Invalid params: tab_id must be an integer");
      responses.push_back(std::move(err));
      return;
    }
    capability_registry_->ReleaseTab(*tab_id);
    base::DictValue result;
    result.Set("released", true);
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, base::Value(std::move(result)));
    responses.push_back(std::move(resp));
    return;
  }

  if (*tool_name == "artifact.list") {
    const std::string session_id =
        arguments && arguments->FindString("session_id")
            ? *arguments->FindString("session_id")
            : std::string();
    std::optional<base::Value> parsed =
        g_browser_delegate
            ? base::JSONReader::Read(
                  g_browser_delegate->ListArtifacts(session_id),
                  base::JSON_PARSE_RFC)
            : std::nullopt;
    if (!parsed || !parsed->is_list()) {
      responses.push_back(framer_.BuildErrorResponse(
          msg.id, -32000, "Artifact registry unavailable"));
      return;
    }
    base::DictValue result;
    result.Set("artifacts", std::move(*parsed).TakeList());
    responses.push_back(
        framer_.BuildSuccessResponse(msg.id, base::Value(std::move(result))));
    return;
  }

  if (*tool_name == "artifact.export") {
    const std::string* artifact_id =
        arguments ? arguments->FindString("artifact_id") : nullptr;
    const std::string* destination =
        arguments ? arguments->FindString("destination") : nullptr;
    if (!artifact_id || !destination || !g_browser_delegate ||
        !deferred_response_sender_) {
      responses.push_back(framer_.BuildErrorResponse(
          msg.id, -32602,
          "Invalid params: artifact_id and destination required"));
      return;
    }
    DeferredResponseSender sender = terminal_deferred_sender;
    g_browser_delegate->ExportArtifact(
        *artifact_id, base::FilePath::FromUTF8Unsafe(*destination),
        base::BindOnce(
            [](std::optional<base::Value> message_id,
               DeferredResponseSender sender, bool success,
               std::string error) {
              if (!success) {
                std::move(sender).Run(BuildJsonRpcErrorLine(
                    std::move(message_id), -32000,
                    error.empty() ? "Artifact export failed"
                                  : std::move(error)));
                return;
              }
              base::DictValue result;
              result.Set("exported", true);
              std::move(sender).Run(BuildJsonRpcSuccessLine(
                  std::move(message_id), base::Value(std::move(result))));
            },
            CloneMessageId(msg.id), std::move(sender)));
    return;
  }

  if (*tool_name == "browser_routines_list") {
    base::ListValue routines;
    if (g_browser_delegate) {
      std::optional<base::Value> parsed = base::JSONReader::Read(
          g_browser_delegate->ListRoutines(), base::JSON_PARSE_RFC);
      if (parsed && parsed->is_list()) {
        routines = std::move(*parsed).TakeList();
      }
    }
    base::DictValue result;
    result.Set("routines", std::move(routines));
    auto redacted = MahoMcpFirewall::Wrap(base::Value(std::move(result)));
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, redacted.get().Clone());
    responses.push_back(std::move(resp));
    return;
  }

  if (*tool_name == "browser_routines_run") {
    if (!arguments || !arguments->FindString("id")) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32602, "Invalid params: id required");
      responses.push_back(std::move(err));
      return;
    }
    if (!g_browser_delegate || !deferred_response_sender_) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32000, "Routines feature unavailable");
      responses.push_back(std::move(err));
      return;
    }
    const std::string& id = *arguments->FindString("id");
    DeferredResponseSender sender = terminal_deferred_sender;
    g_browser_delegate->RunRoutine(
        id,
        base::BindOnce(
            [](std::optional<base::Value> msg_id, DeferredResponseSender sender,
               MahoMcpBrowserDelegate::RoutineRunError error,
               std::string content_or_error) {
              if (error != MahoMcpBrowserDelegate::RoutineRunError::kNone) {
                base::DictValue data;
                switch (error) {
                  case MahoMcpBrowserDelegate::RoutineRunError::kTierLocked:
                    data.Set("status", "routine_tier_locked");
                    break;
                  case MahoMcpBrowserDelegate::RoutineRunError::kUnavailable:
                    data.Set("status", "routine_unavailable");
                    break;
                  case MahoMcpBrowserDelegate::RoutineRunError::
                      kExecutionFailed:
                    data.Set("status", "routine_execution_failed");
                    break;
                  case MahoMcpBrowserDelegate::RoutineRunError::kNone:
                    NOTREACHED();
                }
                sender.Run(BuildJsonRpcErrorLine(std::move(msg_id), -32000,
                                                 std::move(content_or_error),
                                                 base::Value(std::move(data))));
                return;
              }
              std::optional<base::Value> parsed = base::JSONReader::Read(
                  content_or_error, base::JSON_PARSE_RFC);
              base::Value result;
              if (parsed) {
                result = std::move(*parsed);
              } else {
                base::DictValue payload;
                payload.Set("content", std::move(content_or_error));
                result = base::Value(std::move(payload));
              }
              sender.Run(BuildJsonRpcSuccessLine(std::move(msg_id),
                                                 std::move(result)));
            },
            CloneMessageId(msg.id), std::move(sender)));
    return;
  }

  if (*tool_name == "mail_extract_otp") {
    std::string account_id = "";
    std::string folder_id = "";
    std::string query = "";
    int64_t max_age = 300;
    if (arguments) {
      if (const std::string* val = arguments->FindString("account_id")) {
        account_id = *val;
      }
      if (const std::string* val = arguments->FindString("folder_id")) {
        folder_id = *val;
      }
      if (const std::string* val = arguments->FindString("query")) {
        query = *val;
      }
      if (std::optional<int> val = arguments->FindInt("max_age_seconds")) {
        max_age = *val;
      }
    }
    if (!mail_broker_ready()) {
      return;
    }
    g_browser_delegate->MailExtractOtp(
        account_id, folder_id, query, max_age,
        make_mail_completion(/*recheck_read_consent=*/true));
    return;
  }

  if (*tool_name == "mail_list_accounts") {
    if (params.Find("arguments") && !arguments) {
      reject_invalid_mail_params("Invalid params: arguments must be an object");
      return;
    }
    if (!mail_broker_ready()) {
      return;
    }
    g_browser_delegate->MailListAccounts(
        make_mail_completion(/*recheck_read_consent=*/true));
    return;
  }

  if (*tool_name == "mail_list_folders") {
    const std::string* account_id =
        FindRequiredStringArgument(arguments, "account_id");
    if (!account_id) {
      reject_invalid_mail_params(RequiredStringArgumentMessage("account_id"));
      return;
    }
    if (!mail_broker_ready()) {
      return;
    }
    g_browser_delegate->MailListFolders(
        *account_id, make_mail_completion(/*recheck_read_consent=*/true));
    return;
  }

  if (*tool_name == "mail_list_emails") {
    const std::string* account_id =
        FindRequiredStringArgument(arguments, "account_id");
    const std::string* folder_id =
        FindRequiredStringArgument(arguments, "folder_id");
    if (!account_id) {
      reject_invalid_mail_params(RequiredStringArgumentMessage("account_id"));
      return;
    }
    if (!folder_id) {
      reject_invalid_mail_params(RequiredStringArgumentMessage("folder_id"));
      return;
    }

    int limit = 50;
    int offset = 0;
    std::string error_message;
    if (!ReadOptionalNonNegativeIntArgument(arguments, "limit", 50, &limit,
                                            &error_message) ||
        !ReadOptionalNonNegativeIntArgument(arguments, "offset", 0, &offset,
                                            &error_message)) {
      reject_invalid_mail_params(std::move(error_message));
      return;
    }
    if (!mail_broker_ready()) {
      return;
    }
    g_browser_delegate->MailListEmails(
        *account_id, *folder_id, limit, offset,
        make_mail_completion(/*recheck_read_consent=*/true));
    return;
  }

  if (*tool_name == "mail_get_email") {
    const std::string* email_id =
        FindRequiredStringArgument(arguments, "email_id");
    if (!email_id) {
      reject_invalid_mail_params(RequiredStringArgumentMessage("email_id"));
      return;
    }
    if (!mail_broker_ready()) {
      return;
    }
    g_browser_delegate->MailGetEmail(
        *email_id, make_mail_completion(/*recheck_read_consent=*/true));
    return;
  }

  if (*tool_name == "mail_search_emails") {
    std::string query_json;
    std::string error_message;
    if (!BuildMailSearchQueryJson(arguments, &query_json, &error_message)) {
      reject_invalid_mail_params(std::move(error_message));
      return;
    }
    if (!mail_broker_ready()) {
      return;
    }
    g_browser_delegate->MailSearchEmails(
        query_json, make_mail_completion(/*recheck_read_consent=*/true));
    return;
  }

  if (*tool_name == "mail_list_thread") {
    const std::string* account_id =
        FindRequiredStringArgument(arguments, "account_id");
    const std::string* message_id =
        FindRequiredStringArgument(arguments, "message_id");
    if (!account_id) {
      reject_invalid_mail_params(RequiredStringArgumentMessage("account_id"));
      return;
    }
    if (!message_id) {
      reject_invalid_mail_params(RequiredStringArgumentMessage("message_id"));
      return;
    }
    if (!mail_broker_ready()) {
      return;
    }
    g_browser_delegate->MailListThread(
        *account_id, *message_id,
        make_mail_completion(/*recheck_read_consent=*/true));
    return;
  }

  if (*tool_name == "mail_add_account") {
    const std::string* request_json =
        FindRequiredStringArgument(arguments, "request_json");
    if (!request_json) {
      reject_invalid_mail_params(RequiredStringArgumentMessage("request_json"));
      return;
    }
    if (!mail_broker_ready()) {
      return;
    }
    g_browser_delegate->MailAddAccount(
        *request_json, make_mail_completion(/*recheck_read_consent=*/false));
    return;
  }

  if (*tool_name == "mail_test_connection") {
    const std::string* params_json =
        FindRequiredStringArgument(arguments, "params_json");
    if (!params_json) {
      reject_invalid_mail_params(RequiredStringArgumentMessage("params_json"));
      return;
    }
    if (!mail_broker_ready()) {
      return;
    }
    g_browser_delegate->MailTestConnection(
        *params_json, make_mail_completion(/*recheck_read_consent=*/false));
    return;
  }

  if (*tool_name == "mail_delete_account") {
    const std::string* account_id =
        FindRequiredStringArgument(arguments, "account_id");
    if (!account_id) {
      reject_invalid_mail_params(RequiredStringArgumentMessage("account_id"));
      return;
    }
    if (!mail_broker_ready()) {
      return;
    }
    g_browser_delegate->MailDeleteAccount(
        *account_id, make_mail_completion(/*recheck_read_consent=*/false));
    return;
  }

  if (*tool_name == "mail_start_oauth") {
    const std::string* provider =
        FindRequiredStringArgument(arguments, "provider");
    const std::string* client_id =
        FindRequiredStringArgument(arguments, "client_id");
    const std::string* redirect_uri =
        FindRequiredStringArgument(arguments, "redirect_uri");
    if (!provider) {
      reject_invalid_mail_params(RequiredStringArgumentMessage("provider"));
      return;
    }
    if (!client_id) {
      reject_invalid_mail_params(RequiredStringArgumentMessage("client_id"));
      return;
    }
    if (!redirect_uri) {
      reject_invalid_mail_params(RequiredStringArgumentMessage("redirect_uri"));
      return;
    }
    if (!mail_broker_ready()) {
      return;
    }
    g_browser_delegate->MailOAuthStartUrl(
        *provider, *client_id, *redirect_uri,
        make_mail_completion(/*recheck_read_consent=*/false));
    return;
  }

  if (*tool_name == "mail_complete_oauth") {
    const std::string* state = FindRequiredStringArgument(arguments, "state");
    const std::string* code = FindRequiredStringArgument(arguments, "code");
    if (!state) {
      reject_invalid_mail_params(RequiredStringArgumentMessage("state"));
      return;
    }
    if (!code) {
      reject_invalid_mail_params(RequiredStringArgumentMessage("code"));
      return;
    }
    if (!mail_broker_ready()) {
      return;
    }
    g_browser_delegate->MailOAuthComplete(
        *state, *code, make_mail_completion(/*recheck_read_consent=*/false));
    return;
  }

  if (*tool_name == "mail_reconnect_account") {
    const std::string* account_id =
        FindRequiredStringArgument(arguments, "account_id");
    if (!account_id) {
      reject_invalid_mail_params(RequiredStringArgumentMessage("account_id"));
      return;
    }
    if (!mail_broker_ready()) {
      return;
    }
    g_browser_delegate->MailReconnectAccount(
        *account_id, make_mail_completion(/*recheck_read_consent=*/false));
    return;
  }

  if (*tool_name == "mail_import_migration_archive") {
    const std::string* archive_json =
        FindRequiredStringArgument(arguments, "archive_json");
    if (!archive_json) {
      reject_invalid_mail_params(RequiredStringArgumentMessage("archive_json"));
      return;
    }
    if (!mail_broker_ready()) {
      return;
    }
    g_browser_delegate->MailImportMigrationArchive(
        *archive_json, make_mail_completion(/*recheck_read_consent=*/false));
    return;
  }

  if (*tool_name == "mail_send") {
    const std::string* request_json =
        FindRequiredStringArgument(arguments, "request_json");
    if (!request_json) {
      reject_invalid_mail_params(RequiredStringArgumentMessage("request_json"));
      return;
    }
    if (!mail_broker_ready()) {
      return;
    }
    g_browser_delegate->MailSendEmail(
        *request_json,
        /*already_authorized=*/true,
        make_mail_completion(/*recheck_read_consent=*/false));
    return;
  }

  if (*tool_name == "mail_save_draft") {
    const std::string* request_json =
        FindRequiredStringArgument(arguments, "request_json");
    if (!request_json) {
      reject_invalid_mail_params(RequiredStringArgumentMessage("request_json"));
      return;
    }
    if (!mail_broker_ready()) {
      return;
    }
    g_browser_delegate->MailSaveDraft(
        *request_json,
        /*already_authorized=*/true,
        make_mail_completion(/*recheck_read_consent=*/false));
    return;
  }

  if (*tool_name == "mail_update_draft") {
    const std::string* draft_id =
        FindRequiredStringArgument(arguments, "draft_id");
    const std::string* request_json =
        FindRequiredStringArgument(arguments, "request_json");
    if (!draft_id || !request_json) {
      reject_invalid_mail_params(
          "Invalid params: draft_id and request_json required");
      return;
    }
    if (!mail_broker_ready()) {
      return;
    }
    g_browser_delegate->MailUpdateDraft(
        *draft_id, *request_json,
        /*already_authorized=*/true,
        make_mail_completion(/*recheck_read_consent=*/false));
    return;
  }

  if (*tool_name == "mail_queue_email") {
    const std::string* request_json =
        FindRequiredStringArgument(arguments, "request_json");
    if (!request_json) {
      reject_invalid_mail_params(RequiredStringArgumentMessage("request_json"));
      return;
    }
    if (!mail_broker_ready()) {
      return;
    }
    g_browser_delegate->MailQueueEmail(
        *request_json, make_mail_completion(/*recheck_read_consent=*/false));
    return;
  }

  if (*tool_name == "mail_flag") {
    const std::string* request_json =
        FindRequiredStringArgument(arguments, "request_json");
    if (!request_json) {
      reject_invalid_mail_params(RequiredStringArgumentMessage("request_json"));
      return;
    }
    if (!mail_broker_ready()) {
      return;
    }
    g_browser_delegate->MailFlag(
        *request_json, make_mail_completion(/*recheck_read_consent=*/false));
    return;
  }

  if (*tool_name == "vault_list_credentials_for_active_page") {
    int tab_id = use_resolved_target
                     ? resolved_tab_id
                     : (arguments && arguments->FindInt("tab_id")
                            ? arguments->FindInt("tab_id").value()
                            : active_tab_id_);
    base::ListValue items;
    if (g_browser_delegate) {
      for (const auto& credential :
           g_browser_delegate->VaultListCredentialsForActivePage(tab_id)) {
        base::DictValue item;
        item.Set("handle", credential.handle);
        item.Set("username_hint", credential.username_hint);
        item.Set("origin", credential.origin);
        items.Append(std::move(item));
      }
    }
    base::DictValue result;
    result.Set("items", std::move(items));
    auto redacted = MahoMcpFirewall::Wrap(base::Value(std::move(result)));
    responses.push_back(
        framer_.BuildSuccessResponse(msg.id, redacted.get().Clone()));
    return;
  }

  if (*tool_name == "vault_request_credential_use") {
    const std::string* handle = FindRequiredStringArgument(arguments, "handle");
    const std::string* origin = FindRequiredStringArgument(arguments, "origin");
    if (!handle) {
      responses.push_back(framer_.BuildErrorResponse(
          msg.id, -32602, RequiredStringArgumentMessage("handle")));
      return;
    }
    if (!origin) {
      responses.push_back(framer_.BuildErrorResponse(
          msg.id, -32602, RequiredStringArgumentMessage("origin")));
      return;
    }
    int tab_id = use_resolved_target
                     ? resolved_tab_id
                     : (arguments && arguments->FindInt("tab_id")
                            ? arguments->FindInt("tab_id").value()
                            : active_tab_id_);
    std::optional<std::string> grant_handle =
        g_browser_delegate ? g_browser_delegate->VaultRequestCredentialUse(
                                 tab_id, *handle, *origin)
                           : std::nullopt;
    if (!grant_handle) {
      responses.push_back(framer_.BuildErrorResponse(
          msg.id, -32000, "Credential grant unavailable"));
      return;
    }
    base::DictValue result;
    result.Set("grant_handle", *grant_handle);
    result.Set("status", "granted");
    auto redacted = MahoMcpFirewall::Wrap(base::Value(std::move(result)));
    responses.push_back(
        framer_.BuildSuccessResponse(msg.id, redacted.get().Clone()));
    return;
  }

  if (*tool_name == "vault_fill_credential" ||
      *tool_name == "vault_fill_totp") {
    const std::string* grant_handle =
        FindRequiredStringArgument(arguments, "grant_handle");
    if (!grant_handle) {
      responses.push_back(framer_.BuildErrorResponse(
          msg.id, -32602, RequiredStringArgumentMessage("grant_handle")));
      return;
    }
    if (!arguments || !arguments->FindInt("ref")) {
      responses.push_back(framer_.BuildErrorResponse(
          msg.id, -32602, "Invalid params: ref required"));
      return;
    }
    int ref = arguments->FindInt("ref").value();
    auto it = ref_to_ax_id_.find(ref);
    if (it == ref_to_ax_id_.end()) {
      responses.push_back(framer_.BuildErrorResponse(
          msg.id, -32000,
          "Unknown ref: call browser_accessibility_snapshot first"));
      return;
    }
    int tab_id = use_resolved_target
                     ? resolved_tab_id
                     : arguments->FindInt("tab_id").value_or(active_tab_id_);
    bool filled = false;
    if (g_browser_delegate) {
      filled = (*tool_name == "vault_fill_totp")
                   ? g_browser_delegate->VaultFillTotp(tab_id, *grant_handle,
                                                       it->second)
                   : g_browser_delegate->VaultFillCredential(
                         tab_id, *grant_handle, it->second);
    }
    if (!filled) {
      responses.push_back(
          framer_.BuildErrorResponse(msg.id, -32000, "Credential fill failed"));
      return;
    }
    base::DictValue result;
    result.Set("filled", true);
    result.Set("ref", ref);
    auto redacted = MahoMcpFirewall::Wrap(base::Value(std::move(result)));
    responses.push_back(
        framer_.BuildSuccessResponse(msg.id, redacted.get().Clone()));
    return;
  }

  if (*tool_name == "browser_acquire_lease") {
    const int target_tab_id = use_resolved_target
                                  ? resolved_tab_id
                                  : (arguments
                                         ? arguments->FindInt("tab_id").value_or(0)
                                         : 0);
    HandleAcquireLease(msg, arguments, target_tab_id, responses);
    return;
  }

  if (*tool_name == "browser_heartbeat_lease") {
    const int target_tab_id = use_resolved_target
                                  ? resolved_tab_id
                                  : (arguments
                                         ? arguments->FindInt("tab_id").value_or(0)
                                         : 0);
    HandleHeartbeatLease(msg, arguments, target_tab_id, responses);
    return;
  }

  if (*tool_name == "browser_release_lease") {
    const int target_tab_id = use_resolved_target
                                  ? resolved_tab_id
                                  : (arguments
                                         ? arguments->FindInt("tab_id").value_or(0)
                                         : 0);
    HandleReleaseLease(msg, arguments, target_tab_id, responses);
    return;
  }

  if (*tool_name == "browser_tab_close") {
    if (!arguments || !arguments->FindInt("tab_id")) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32602, "Invalid params: tab_id required");
      responses.push_back(std::move(err));
      return;
    }
    int target_id = arguments->FindInt("tab_id").value();
    bool closed = false;
    if (g_browser_delegate) {
      closed = g_browser_delegate->CloseTab(
          use_resolved_target ? resolved_tab_id : target_id);
    } else {
      auto it = std::remove_if(
          tabs_.begin(), tabs_.end(),
          [target_id](const TabInfo& t) { return t.id == target_id; });
      if (it != tabs_.end()) {
        tabs_.erase(it, tabs_.end());
        closed = true;
      }
    }
    if (!closed) {
      std::string err =
          framer_.BuildErrorResponse(msg.id, -32000, "Unknown tab_id");
      responses.push_back(std::move(err));
      return;
    }

    // Sync local list as fallback
    auto it = std::remove_if(
        tabs_.begin(), tabs_.end(),
        [target_id](const TabInfo& t) { return t.id == target_id; });
    if (it != tabs_.end()) {
      tabs_.erase(it, tabs_.end());
    }
    if (active_tab_id_ == target_id) {
      if (!tabs_.empty()) {
        tabs_[0].is_active = true;
        active_tab_id_ = tabs_[0].id;
      } else {
        active_tab_id_ = -1;
      }
    }
    base::DictValue result;
    result.Set("closed", true);
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, base::Value(std::move(result)));
    responses.push_back(std::move(resp));
    return;
  }

  if (*tool_name == "browser_tab_new") {
    if (arguments && arguments->FindString("url")) {
      const std::string& url_str = *arguments->FindString("url");
      const GURL nav_url(url_str);
      if (!nav_url.is_valid()) {
        std::string err = framer_.BuildErrorResponse(
            msg.id, -32602, "Invalid params: url is not a valid GURL");
        responses.push_back(std::move(err));
        return;
      }
      // Origin grant check bypassed for browser control
      if (IsHostBlocked(nav_url.host())) {
        std::string err = framer_.BuildErrorResponse(
            msg.id, -32000, "New tab blocked: host is in the disallow list");
        responses.push_back(std::move(err));
        return;
      }
    }
    std::string url_str = arguments && arguments->FindString("url")
                              ? *arguments->FindString("url")
                              : "about:blank";
    const GURL nav_url(url_str);
    int new_id = 0;
    if (g_browser_delegate) {
      new_id = g_browser_delegate->CreateNewTab(nav_url);
    } else {
      for (const auto& t : tabs_) {
        if (t.id >= new_id) {
          new_id = t.id + 1;
        }
      }
    }
    for (auto& t : tabs_) {
      t.is_active = false;
    }
    TabInfo new_tab;
    new_tab.id = new_id;
    new_tab.title = url_str == "about:blank" ? "New Tab" : nav_url.host();
    new_tab.url = url_str;
    new_tab.is_active = true;
    tabs_.push_back(new_tab);
    active_tab_id_ = new_id;

    base::DictValue tab;
    tab.Set("id", new_tab.id);
    tab.Set("title", new_tab.title);
    tab.Set("url", new_tab.url);
    tab.Set("is_active", new_tab.is_active);
    base::DictValue result;
    result.Set("tab", std::move(tab));
    auto redacted = MahoMcpFirewall::Wrap(base::Value(std::move(result)));
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, redacted.get().Clone());
    responses.push_back(std::move(resp));
    return;
  }

  if (*tool_name == "browser_history_search") {
    const std::string query = (arguments && arguments->FindString("query"))
                                  ? *arguments->FindString("query")
                                  : std::string();
    const int max_results = (arguments && arguments->FindInt("max_results"))
                                ? arguments->FindInt("max_results").value()
                                : 20;
    base::ListValue entries_list;
    if (g_browser_delegate) {
      for (const auto& h : g_browser_delegate->SearchHistory(
               query, static_cast<size_t>(max_results))) {
        base::DictValue entry;
        entry.Set("url", h.url);
        entry.Set("title", h.title);
        entry.Set("visited_at", h.visited_at);
        entries_list.Append(std::move(entry));
      }
    }
    base::DictValue result;
    result.Set("entries", std::move(entries_list));
    auto redacted = MahoMcpFirewall::Wrap(base::Value(std::move(result)));
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, redacted.get().Clone());
    if (deferred_response_sender_) {
      terminal_deferred_sender.Run(std::move(resp));
      return;
    }
    responses.push_back(std::move(resp));
    return;
  }

  if (*tool_name == "browser_bookmarks_search") {
    if (!arguments || !arguments->FindString("query")) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32602, "Invalid params: query required");
      responses.push_back(std::move(err));
      return;
    }
    const std::string& query = *arguments->FindString("query");
    base::ListValue bookmarks_list;
    if (g_browser_delegate) {
      for (const auto& b : g_browser_delegate->SearchBookmarks(query)) {
        base::DictValue bookmark;
        bookmark.Set("id", static_cast<double>(b.id));
        bookmark.Set("title", b.title);
        bookmark.Set("url", b.url);
        if (!b.folder.empty()) {
          bookmark.Set("folder", b.folder);
        }
        bookmarks_list.Append(std::move(bookmark));
      }
    }
    base::DictValue result;
    result.Set("bookmarks", std::move(bookmarks_list));
    auto redacted = MahoMcpFirewall::Wrap(base::Value(std::move(result)));
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, redacted.get().Clone());
    responses.push_back(std::move(resp));
    return;
  }

  if (*tool_name == "browser_bookmark_create") {
    if (!arguments || !arguments->FindString("title") ||
        !arguments->FindString("url")) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32602, "Invalid params: title and url required");
      responses.push_back(std::move(err));
      return;
    }
    const std::string& title = *arguments->FindString("title");
    const std::string& url_str = *arguments->FindString("url");
    const GURL url(url_str);
    if (!url.is_valid()) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32602, "Invalid params: url is not a valid GURL");
      responses.push_back(std::move(err));
      return;
    }
    const std::string folder = arguments->FindString("folder")
                                   ? *arguments->FindString("folder")
                                   : std::string();
    base::DictValue result;
    if (g_browser_delegate) {
      auto b = g_browser_delegate->CreateBookmark(title, url, folder);
      base::DictValue bookmark;
      bookmark.Set("id", static_cast<double>(b.id));
      bookmark.Set("title", b.title);
      bookmark.Set("url", b.url);
      if (!b.folder.empty()) {
        bookmark.Set("folder", b.folder);
      }
      result.Set("bookmark", std::move(bookmark));
    } else {
      base::DictValue bookmark;
      bookmark.Set("id", 0);
      bookmark.Set("title", title);
      bookmark.Set("url", url_str);
      if (!folder.empty()) {
        bookmark.Set("folder", folder);
      }
      result.Set("bookmark", std::move(bookmark));
    }
    auto redacted = MahoMcpFirewall::Wrap(base::Value(std::move(result)));
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, redacted.get().Clone());
    responses.push_back(std::move(resp));
    return;
  }

  if (*tool_name == "browser_page_content") {
    int tab_id = use_resolved_target
                     ? resolved_tab_id
                     : (arguments && arguments->FindInt("tab_id")
                            ? arguments->FindInt("tab_id").value()
                            : active_tab_id_);
    base::DictValue result;
    if (g_browser_delegate) {
      auto res = g_browser_delegate->GetPageContent(tab_id);
      result.Set("text", res.text);
      result.Set("url", res.url);
      result.Set("title", res.title);
      result.Set("redacted", false);
    } else {
      result.Set("text", "");
      result.Set("url", "");
      result.Set("title", "");
      result.Set("redacted", false);
    }
    auto redacted = MahoMcpFirewall::Wrap(base::Value(std::move(result)));
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, redacted.get().Clone());
    responses.push_back(std::move(resp));
    return;
  }

  if (*tool_name == "browser_page_text") {
    int tab_id = use_resolved_target
                     ? resolved_tab_id
                     : (arguments && arguments->FindInt("tab_id")
                            ? arguments->FindInt("tab_id").value()
                            : active_tab_id_);
    std::string text;
    if (g_browser_delegate) {
      text = g_browser_delegate->GetPageText(tab_id);
    }
    base::DictValue result;
    result.Set("text", text);
    auto redacted = MahoMcpFirewall::Wrap(base::Value(std::move(result)));
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, redacted.get().Clone());
    responses.push_back(std::move(resp));
    return;
  }

  if (*tool_name == "browser_search_in_page") {
    if (!arguments || !arguments->FindString("query")) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32602, "Invalid params: query required");
      responses.push_back(std::move(err));
      return;
    }
    int tab_id = use_resolved_target
                     ? resolved_tab_id
                     : arguments->FindInt("tab_id").value_or(active_tab_id_);
    const std::string& query = *arguments->FindString("query");
    base::DictValue result;
    if (g_browser_delegate) {
      auto res = g_browser_delegate->SearchInPage(tab_id, query);
      result.Set("match_count", res.match_count);
      result.Set("active_match_index", res.active_match_index);
    } else {
      result.Set("match_count", 0);
      result.Set("active_match_index", -1);
    }
    auto redacted = MahoMcpFirewall::Wrap(base::Value(std::move(result)));
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, redacted.get().Clone());
    responses.push_back(std::move(resp));
    return;
  }

  if (*tool_name == "browser_page_context") {
    int tab_id = use_resolved_target
                     ? resolved_tab_id
                     : (arguments && arguments->FindInt("tab_id")
                            ? arguments->FindInt("tab_id").value()
                            : active_tab_id_);
    base::DictValue result;
    if (g_browser_delegate) {
      auto res = g_browser_delegate->GetPageContext(tab_id);
      result.Set("url", res.url);
      result.Set("title", res.title);
      result.Set("content", res.content);
    } else {
      result.Set("url", "");
      result.Set("title", "");
      result.Set("content", "");
    }
    auto redacted = MahoMcpFirewall::Wrap(base::Value(std::move(result)));
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, redacted.get().Clone());
    responses.push_back(std::move(resp));
    return;
  }

  if (*tool_name == "page_query_selector") {
    if (!arguments || !arguments->FindString("selector")) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32602, "Invalid params: selector required");
      responses.push_back(std::move(err));
      return;
    }
    int tab_id = use_resolved_target
                     ? resolved_tab_id
                     : arguments->FindInt("tab_id").value_or(active_tab_id_);
    const std::string& selector = *arguments->FindString("selector");
    base::DictValue result;
    if (g_browser_delegate) {
      auto res = g_browser_delegate->QuerySelector(tab_id, selector);
      result.Set("ref_id", res.ref_id);
      result.Set("tag", res.tag);
    } else {
      result.Set("ref_id", "");
      result.Set("tag", "");
    }
    auto redacted = MahoMcpFirewall::Wrap(base::Value(std::move(result)));
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, redacted.get().Clone());
    responses.push_back(std::move(resp));
    return;
  }

  if (*tool_name == "page_get_text") {
    if (!arguments || !arguments->FindString("ref_id")) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32602, "Invalid params: ref_id required");
      responses.push_back(std::move(err));
      return;
    }
    int tab_id = use_resolved_target
                     ? resolved_tab_id
                     : arguments->FindInt("tab_id").value_or(active_tab_id_);
    const std::string& ref_id = *arguments->FindString("ref_id");
    base::DictValue result;
    if (g_browser_delegate) {
      result.Set("text", g_browser_delegate->GetElementText(tab_id, ref_id));
    } else {
      result.Set("text", "");
    }
    auto redacted = MahoMcpFirewall::Wrap(base::Value(std::move(result)));
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, redacted.get().Clone());
    responses.push_back(std::move(resp));
    return;
  }

  if (*tool_name == "page_get_attribute") {
    if (!arguments || !arguments->FindString("ref_id") ||
        !arguments->FindString("attribute")) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32602, "Invalid params: ref_id and attribute required");
      responses.push_back(std::move(err));
      return;
    }
    int tab_id = use_resolved_target
                     ? resolved_tab_id
                     : arguments->FindInt("tab_id").value_or(active_tab_id_);
    const std::string& ref_id = *arguments->FindString("ref_id");
    const std::string& attribute = *arguments->FindString("attribute");
    base::DictValue result;
    if (g_browser_delegate) {
      result.Set("value", g_browser_delegate->GetElementAttribute(
                              tab_id, ref_id, attribute));
    } else {
      result.Set("value", "");
    }
    auto redacted = MahoMcpFirewall::Wrap(base::Value(std::move(result)));
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, redacted.get().Clone());
    responses.push_back(std::move(resp));
    return;
  }

  if (*tool_name == "browser_same_origin_fetch") {
    const std::string* url_arg = FindRequiredStringArgument(arguments, "url");
    if (!url_arg) {
      responses.push_back(framer_.BuildErrorResponse(
          msg.id, -32602, RequiredStringArgumentMessage("url")));
      return;
    }
    const GURL url(*url_arg);
    if (!url.is_valid() || !url.SchemeIs("https") || !url.has_host()) {
      responses.push_back(framer_.BuildErrorResponse(
          msg.id, -32602, "Invalid params: url must be an HTTPS URL"));
      return;
    }
    const std::string method =
        arguments && arguments->FindString("method")
            ? base::ToUpperASCII(*arguments->FindString("method"))
            : "GET";
    if (method != "GET" && method != "POST") {
      responses.push_back(framer_.BuildErrorResponse(
          msg.id, -32602, "Invalid params: method must be GET or POST"));
      return;
    }
    const std::string body = arguments && arguments->FindString("body")
                                 ? *arguments->FindString("body")
                                 : std::string();
    base::DictValue headers;
    if (arguments && arguments->Find("headers")) {
      const base::DictValue* requested_headers = arguments->FindDict("headers");
      if (!requested_headers) {
        responses.push_back(framer_.BuildErrorResponse(
            msg.id, -32602,
            "Invalid params: headers must be an object of strings"));
        return;
      }
      for (const auto [name, value] : *requested_headers) {
        if (!value.is_string()) {
          responses.push_back(framer_.BuildErrorResponse(
              msg.id, -32602, "Invalid params: header values must be strings"));
          return;
        }
        headers.Set(name, value.GetString());
      }
    }
    if (method == "POST" && !headers.contains("content-type")) {
      headers.Set("content-type", "application/json");
    }
    std::string headers_json;
    base::JSONWriter::Write(base::Value(std::move(headers)), &headers_json);
    const int tab_id = use_resolved_target ? resolved_tab_id : active_tab_id_;
    if (controller_kind_ != MahoMcpControllerKind::kThirdParty &&
        !capability_registry_->IsOriginGranted(url::Origin::Create(url))) {
      responses.push_back(framer_.BuildErrorResponse(
          msg.id, kMahoMcpErrorApprovalDenied,
          "exact_origin_grant_required"));
      return;
    }
    if (!g_browser_delegate || !deferred_response_sender_) {
      responses.push_back(framer_.BuildErrorResponse(
          msg.id, -32000, "Same-origin fetch unavailable"));
      return;
    }
    const GURL page_url(g_browser_delegate->GetPageContent(tab_id).url);
    if (!page_url.is_valid() ||
        page_url.DeprecatedGetOriginAsURL() != url.DeprecatedGetOriginAsURL()) {
      responses.push_back(framer_.BuildErrorResponse(
          msg.id, -32000,
          "Same-origin fetch blocked: URL origin does not match target tab"));
      return;
    }
    std::optional<base::Value> msg_id_copy = CloneMessageId(msg.id);
    DeferredResponseSender sender = terminal_deferred_sender;
    g_browser_delegate->SameOriginFetch(
        tab_id, url, method, body, headers_json,
        base::BindOnce(
            [](std::optional<base::Value> id, DeferredResponseSender sender,
               MahoMcpBrowserDelegate::SameOriginFetchResult result) {
              if (!result.success) {
                sender.Run(BuildJsonRpcErrorLine(
                    std::move(id), -32000,
                    result.error.empty() ? "Same-origin fetch failed"
                                         : std::move(result.error)));
                return;
              }
              base::DictValue out;
              out.Set("status", result.status);
              out.Set("url", std::move(result.final_url));
              out.Set("content_type", std::move(result.content_type));
              out.Set("text", std::move(result.text));
              out.Set("truncated", result.truncated);
              auto redacted =
                  MahoMcpFirewall::Wrap(base::Value(std::move(out)));
              base::DictValue response;
              response.Set("jsonrpc", "2.0");
              response.Set("id", id.has_value() ? id->Clone() : base::Value());
              response.Set("result", redacted.get().Clone());
              std::string serialized;
              base::JSONWriter::Write(base::Value(std::move(response)),
                                      &serialized);
              serialized.push_back('\n');
              sender.Run(std::move(serialized));
            },
            std::move(msg_id_copy), std::move(sender)));
    return;
  }

  if (*tool_name == "page_wait_for_selector") {
    if (!arguments || !arguments->FindString("selector")) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32602, "Invalid params: selector required");
      responses.push_back(std::move(err));
      return;
    }
    int tab_id = use_resolved_target
                     ? resolved_tab_id
                     : arguments->FindInt("tab_id").value_or(active_tab_id_);
    const std::string& selector = *arguments->FindString("selector");
    int timeout_ms = arguments->FindInt("timeout_ms").value_or(10000);
    base::DictValue result;
    if (g_browser_delegate) {
      result.Set("found", g_browser_delegate->WaitForSelector(tab_id, selector,
                                                              timeout_ms));
    } else {
      result.Set("found", false);
    }
    auto redacted = MahoMcpFirewall::Wrap(base::Value(std::move(result)));
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, redacted.get().Clone());
    responses.push_back(std::move(resp));
    return;
  }

  if (*tool_name == "page.accessibility_snapshot_v2" ||
      *tool_name == "page_accessibility_snapshot_v2") {
    int tab_id = use_resolved_target
                     ? resolved_tab_id
                     : (arguments && arguments->FindInt("tab_id")
                            ? arguments->FindInt("tab_id").value()
                            : active_tab_id_);

    MahoMcpAccessibilityHandler::SnapshotV2Options options;
    if (arguments) {
      if (const std::string* mode_str = arguments->FindString("mode")) {
        if (*mode_str == "compact") {
          options.mode =
              MahoMcpAccessibilityHandler::SnapshotMode::kCompact;
        } else if (*mode_str == "full") {
          options.mode = MahoMcpAccessibilityHandler::SnapshotMode::kFull;
        } else {
          options.mode =
              MahoMcpAccessibilityHandler::SnapshotMode::kInteractive;
        }
      }
      if (std::optional<bool> hidden = arguments->FindBool("include_hidden")) {
        options.include_hidden = *hidden;
      }
      if (const base::DictValue* scope = arguments->FindDict("scope")) {
        if (const std::string* sel = scope->FindString("selector")) {
          options.scope_selector = *sel;
        }
        if (std::optional<int> r = scope->FindInt("ref")) {
          options.scope_ref = *r;
        }
      }
      if (const std::string* since =
              arguments->FindString("since_snapshot_token")) {
        options.since_snapshot_token = *since;
      }
      if (std::optional<int> max_b = arguments->FindInt("max_bytes")) {
        if (*max_b > 0) {
          options.max_bytes = static_cast<size_t>(*max_b);
        }
      }
      if (std::optional<int> depth = arguments->FindInt("max_depth")) {
        options.max_depth = *depth;
      }
    }

    base::TimeTicks start_time = base::TimeTicks::Now();
    ref_to_ax_id_.clear();
    ref_table_target_ = last_resolved_target_;
    ref_snapshot_token_ = 0;

    MahoMcpBrowserDelegate::AccessibilitySnapshotV2Params v2_params;
    v2_params.tab_id = tab_id;
    v2_params.mode =
        (options.mode == MahoMcpAccessibilityHandler::SnapshotMode::kCompact)
            ? "compact"
            : (options.mode ==
               MahoMcpAccessibilityHandler::SnapshotMode::kFull)
                  ? "full"
                  : "interactive";
    v2_params.include_hidden = options.include_hidden;
    v2_params.scope_selector = options.scope_selector;
    v2_params.scope_ref = options.scope_ref;
    v2_params.since_snapshot_token = options.since_snapshot_token;
    v2_params.max_bytes = options.max_bytes;
    v2_params.max_depth = options.max_depth;

    MahoMcpBrowserDelegate::AccessibilitySnapshotV2Result v2_res;
    if (g_browser_delegate) {
      v2_res = g_browser_delegate->GetAccessibilitySnapshotV2(
          v2_params, &ref_to_ax_id_, observation_cache_,
          ++next_v2_snapshot_token_seq_);
    } else {
      v2_res.snapshot_token = "s_" + std::to_string(tab_id) + "_" +
                              std::to_string(++next_v2_snapshot_token_seq_);
      v2_res.tab.id = tab_id;
      v2_res.tab.url = "about:blank";
      v2_res.tab.title = "";
      v2_res.tree = "WebArea\n";
      v2_res.captured_nodes = 0;
      v2_res.serialized_nodes = 0;
      v2_res.bytes = v2_res.tree.size();
      v2_res.truncated = false;
    }

    base::DictValue result;
    result.Set("snapshot_token", v2_res.snapshot_token);

    base::DictValue tab_dict;
    tab_dict.Set("id", v2_res.tab.id != 0 ? v2_res.tab.id : tab_id);
    tab_dict.Set("url", v2_res.tab.url);
    tab_dict.Set("title", v2_res.tab.title);
    result.Set("tab", std::move(tab_dict));

    result.Set("tree", v2_res.tree);
    if (v2_res.diff.has_value()) {
      result.Set("diff", *v2_res.diff);
    } else {
      result.Set("diff", base::Value());
    }

    base::DictValue stats_dict;
    stats_dict.Set("captured_nodes", static_cast<int>(v2_res.captured_nodes));
    stats_dict.Set("serialized_nodes",
                   static_cast<int>(v2_res.serialized_nodes));
    stats_dict.Set("bytes", static_cast<int>(v2_res.bytes));
    stats_dict.Set("truncated", v2_res.truncated);

    const char* perf_env = getenv("MAHO_MCP_PERF_TRACE");
    bool perf_trace_enabled =
        perf_env && (std::string_view(perf_env) == "1" ||
                     std::string_view(perf_env) == "true");
    if (perf_trace_enabled) {
      base::TimeDelta total_elapsed = base::TimeTicks::Now() - start_time;
      double total_ms = total_elapsed.InMillisecondsF();
      double renderer_ms = v2_res.renderer_snapshot_ms.value_or(0.0);
      double serialize_ms =
          v2_res.ax_serialize_ms.value_or(std::max(0.0, total_ms - renderer_ms));
      stats_dict.Set("renderer_snapshot_ms", renderer_ms);
      stats_dict.Set("ax_serialize_ms", serialize_ms);
      stats_dict.Set("wire_bytes", static_cast<int>(v2_res.bytes));
      stats_dict.Set("total_ms", total_ms);
    }
    result.Set("stats", std::move(stats_dict));

    auto redacted = MahoMcpFirewall::Wrap(base::Value(std::move(result)));
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, redacted.get().Clone());
    responses.push_back(std::move(resp));
    return;
  }

  if (*tool_name == "browser_accessibility_snapshot") {
    int tab_id = use_resolved_target
                     ? resolved_tab_id
                     : (arguments && arguments->FindInt("tab_id")
                            ? arguments->FindInt("tab_id").value()
                            : active_tab_id_);
    ref_to_ax_id_.clear();
    ref_table_target_ = last_resolved_target_;
    ref_snapshot_token_ = 0;
    base::Value snapshot;
    if (g_browser_delegate) {
      snapshot = g_browser_delegate->GetAccessibilitySnapshot(
          tab_id, &ref_to_ax_id_, &ref_snapshot_token_);
    } else {
      base::DictValue empty;
      empty.Set("role", "WebArea");
      empty.Set("children", base::ListValue());
      snapshot = base::Value(std::move(empty));
    }
    auto redacted = MahoMcpFirewall::Wrap(std::move(snapshot));
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, redacted.get().Clone());
    responses.push_back(std::move(resp));
    return;
  }

  if (*tool_name == "browser_locator_click" ||
      *tool_name == "input.locator_click" ||
      *tool_name == "input_locator_click" ||
      *tool_name == "browser_locator_type" ||
      *tool_name == "input.locator_type" ||
      *tool_name == "input_locator_type" ||
      *tool_name == "browser_act_and_observe" ||
      *tool_name == "browser.act_and_observe" ||
      ((*tool_name == "browser_click" || *tool_name == "browser_type") &&
       arguments &&
       (arguments->FindDict("locator") || arguments->FindString("css") ||
        (arguments->FindString("role") && arguments->FindString("name")) ||
        arguments->FindDict("wait") || arguments->FindString("observe") ||
        arguments->FindString("lease")))) {
    const int tab_id = use_resolved_target
                           ? resolved_tab_id
                           : (arguments && arguments->FindInt("tab_id")
                                  ? arguments->FindInt("tab_id").value()
                                  : active_tab_id_);

    const std::string* lease_arg =
        arguments ? arguments->FindString("lease") : nullptr;
    bool is_scoped_lease = (lease_arg && *lease_arg == "scoped");
    bool acquired_scoped_lease = false;
    if (is_scoped_lease && lease_registry_) {
      if (!lease_registry_->IsHeldBy(static_cast<int64_t>(tab_id),
                                     session_id_)) {
        auto acq = lease_registry_->Acquire(
            static_cast<int64_t>(tab_id), session_id_, base::Seconds(60),
            /*force_steal=*/is_internal_cli && !autonomous_);
        if (acq.ok) {
          acquired_scoped_lease = true;
        } else {
          responses.push_back(BuildLeaseContentionErrorLine(
              CloneMessageId(msg.id), tab_id, acq));
          return;
        }
      }
    }

    auto cleanup_scoped_lease = [&]() {
      if (acquired_scoped_lease && lease_registry_) {
        lease_registry_->Release(static_cast<int64_t>(tab_id), session_id_);
        acquired_scoped_lease = false;
      }
    };

    std::string observe_mode = "none";
    if (arguments) {
      if (const std::string* obs = arguments->FindString("observe")) {
        observe_mode = *obs;
      } else if (const base::DictValue* obs_dict =
                     arguments->FindDict("observe")) {
        if (const std::string* m = obs_dict->FindString("mode")) {
          observe_mode = *m;
        }
      }
    }

    std::string pre_action_token;
    if (observe_mode == "diff") {
      const auto* cached = observation_cache_.Find(tab_id, "");
      if (cached) {
        pre_action_token = cached->token;
      } else {
        MahoMcpBrowserDelegate::AccessibilitySnapshotV2Params pre_params;
        pre_params.tab_id = tab_id;
        pre_params.mode = "interactive";
        if (g_browser_delegate) {
          auto pre_res = g_browser_delegate->GetAccessibilitySnapshotV2(
              pre_params, &ref_to_ax_id_, observation_cache_,
              ++next_v2_snapshot_token_seq_);
          pre_action_token = pre_res.snapshot_token;
        } else {
          pre_action_token = "s_" + std::to_string(tab_id) + "_" +
                             std::to_string(++next_v2_snapshot_token_seq_);
        }
      }
    }

    MahoMcpBrowserDelegate::LocatorParams locator_params;
    locator_params.tab_id = tab_id;
    const base::DictValue* loc_dict = nullptr;
    if (arguments) {
      loc_dict = arguments->FindDict("locator");
      if (!loc_dict) {
        if (const base::DictValue* act_dict = arguments->FindDict("action")) {
          loc_dict = act_dict->FindDict("locator");
        }
      }
    }

    if (loc_dict) {
      if (std::optional<int> r = loc_dict->FindInt("ref")) {
        locator_params.ref = *r;
      }
      if (const std::string* c = loc_dict->FindString("css")) {
        locator_params.css = *c;
      }
      if (const std::string* ro = loc_dict->FindString("role")) {
        locator_params.role = *ro;
      }
      if (const std::string* na = loc_dict->FindString("name")) {
        locator_params.name = *na;
      }
      if (std::optional<bool> ex = loc_dict->FindBool("exact")) {
        locator_params.exact = *ex;
      }
    } else if (arguments) {
      if (std::optional<int> r = arguments->FindInt("ref")) {
        locator_params.ref = *r;
      }
      if (const std::string* c = arguments->FindString("css")) {
        locator_params.css = *c;
      }
      if (const std::string* ro = arguments->FindString("role")) {
        locator_params.role = *ro;
      }
      if (const std::string* na = arguments->FindString("name")) {
        locator_params.name = *na;
      }
      if (std::optional<bool> ex = arguments->FindBool("exact")) {
        locator_params.exact = *ex;
      }
    }

    MahoMcpBrowserDelegate::LocatorResolution resolution;
    if (g_browser_delegate) {
      resolution = g_browser_delegate->ResolveLocator(tab_id, locator_params,
                                                      ref_to_ax_id_);
    } else {
      resolution.error_code = "locator_not_found";
      resolution.hint = "Browser delegate not available";
    }

    if (!resolution.success) {
      cleanup_scoped_lease();
      base::DictValue error_body;
      error_body.Set("code", resolution.error_code);
      if (resolution.matches > 0) {
        error_body.Set("matches", resolution.matches);
      }
      if (!resolution.hint.empty()) {
        error_body.Set("hint", resolution.hint);
      }

      base::DictValue data_body;
      data_body.Set("code", resolution.error_code);
      if (resolution.matches > 0) {
        data_body.Set("matches", resolution.matches);
      }
      if (!resolution.hint.empty()) {
        data_body.Set("hint", resolution.hint);
      }
      data_body.Set("error", error_body.Clone());

      base::DictValue rpc_error;
      rpc_error.Set("code", -32000);
      rpc_error.Set("message", "Locator error: " + resolution.error_code);
      rpc_error.Set("data", std::move(data_body));
      rpc_error.Set("error", std::move(error_body));

      base::DictValue response;
      response.Set("id", msg.id ? msg.id->Clone() : base::Value());
      response.Set("error", std::move(rpc_error));

      std::string out;
      base::JSONWriter::Write(base::Value(std::move(response)), &out);
      out.push_back('\n');
      responses.push_back(std::move(out));
      return;
    }

    if (g_browser_delegate &&
        !g_browser_delegate->RevalidateTarget(last_resolved_target_)) {
      cleanup_scoped_lease();
      responses.push_back(framer_.BuildErrorResponse(
          msg.id, kMahoMcpErrorTabNotFound, kMahoMcpMessageTabNotFound));
      return;
    }

    bool is_type_action = (*tool_name == "browser_locator_type" ||
                           *tool_name == "input.locator_type" ||
                           *tool_name == "input_locator_type" ||
                           *tool_name == "browser_type");
    if (!is_type_action && arguments) {
      if (const base::DictValue* act_dict = arguments->FindDict("action")) {
        if (const std::string* kind = act_dict->FindString("kind")) {
          if (*kind == "type") {
            is_type_action = true;
          }
        }
      }
    }

    std::string text_to_type;
    if (is_type_action && arguments) {
      if (const std::string* direct_text = arguments->FindString("text")) {
        text_to_type = *direct_text;
      } else if (const base::DictValue* act_dict =
                     arguments->FindDict("action")) {
        if (const std::string* act_text = act_dict->FindString("text")) {
          text_to_type = *act_text;
        }
      }
    }

    InputActionOutcome outcome;
    if (!g_browser_delegate) {
      cleanup_scoped_lease();
      responses.push_back(framer_.BuildErrorResponse(
          msg.id, -32000, "Action failed: delegate not available"));
      return;
    }

    if (is_type_action) {
      outcome.dispatched =
          g_browser_delegate->Type(tab_id, resolution.ax_id, text_to_type);
      outcome.verified = outcome.dispatched;
      outcome.method = "trusted_input";
    } else {
      outcome.dispatched =
          g_browser_delegate->Click(tab_id, resolution.ax_id);
      outcome.verified = outcome.dispatched;
      outcome.method = "trusted_input";
    }

    if (!outcome.dispatched) {
      cleanup_scoped_lease();
      responses.push_back(framer_.BuildErrorResponse(
          msg.id, -32000, "Action failed: dispatch failed"));
      return;
    }

    std::string wait_mode = "none";
    int timeout_ms = 10000;
    std::string wait_selector;
    if (arguments) {
      if (const base::DictValue* wait_dict = arguments->FindDict("wait")) {
        if (const std::string* m = wait_dict->FindString("mode")) {
          wait_mode = *m;
        } else {
          wait_mode = "auto";
        }
        if (std::optional<int> t = wait_dict->FindInt("timeout_ms")) {
          timeout_ms = *t;
        }
        if (const std::string* sel = wait_dict->FindString("selector")) {
          wait_selector = *sel;
        }
      }
    }

    std::string wait_reason = "none";
    int wait_elapsed_ms = 0;
    if (wait_mode == "auto") {
      base::TimeTicks wstart = base::TimeTicks::Now();
      bool quiet = g_browser_delegate->WaitForAutoQuiet(tab_id, timeout_ms);
      wait_elapsed_ms = (base::TimeTicks::Now() - wstart).InMilliseconds();
      wait_reason = quiet ? "auto_quiet" : "timeout";
    } else if (wait_mode == "selector" && !wait_selector.empty()) {
      base::TimeTicks wstart = base::TimeTicks::Now();
      bool present =
          g_browser_delegate->WaitForSelector(tab_id, wait_selector, timeout_ms);
      wait_elapsed_ms = (base::TimeTicks::Now() - wstart).InMilliseconds();
      wait_reason = present ? "selector_present" : "timeout";
    } else if (wait_mode == "navigation") {
      base::TimeTicks wstart = base::TimeTicks::Now();
      bool settled =
          g_browser_delegate->WaitForAutoQuiet(tab_id, timeout_ms);
      wait_elapsed_ms = (base::TimeTicks::Now() - wstart).InMilliseconds();
      wait_reason = settled ? "navigation_settled" : "timeout";
    }

    std::optional<std::string> post_snapshot_token;
    std::optional<std::string> post_diff;
    if (observe_mode == "diff") {
      MahoMcpBrowserDelegate::AccessibilitySnapshotV2Params post_params;
      post_params.tab_id = tab_id;
      post_params.mode = "interactive";
      post_params.since_snapshot_token = pre_action_token;

      auto post_res = g_browser_delegate->GetAccessibilitySnapshotV2(
          post_params, &ref_to_ax_id_, observation_cache_,
          ++next_v2_snapshot_token_seq_);
      post_snapshot_token = post_res.snapshot_token;
      post_diff = post_res.diff;
    }

    base::DictValue result;
    base::DictValue action_dict;
    action_dict.Set("dispatched", outcome.dispatched);
    if (outcome.verified.has_value()) {
      action_dict.Set("verified", *outcome.verified);
    }
    action_dict.Set("method",
                    outcome.method.empty() ? "trusted_input" : outcome.method);
    result.Set("action", std::move(action_dict));

    if (wait_mode != "none" || (arguments && arguments->Find("wait"))) {
      base::DictValue wait_dict;
      wait_dict.Set("reason", wait_reason);
      wait_dict.Set("elapsed_ms", wait_elapsed_ms);
      result.Set("wait", std::move(wait_dict));
    }

    if (observe_mode == "diff") {
      base::DictValue obs_dict;
      obs_dict.Set("snapshot_token", post_snapshot_token.value_or(""));
      if (post_diff.has_value()) {
        obs_dict.Set("diff", *post_diff);
      } else {
        obs_dict.Set("diff", base::Value());
      }
      result.Set("observation", std::move(obs_dict));
    }

    cleanup_scoped_lease();

    auto redacted = MahoMcpFirewall::Wrap(base::Value(std::move(result)));
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, redacted.get().Clone());
    responses.push_back(std::move(resp));
    return;
  }

  if (*tool_name == "browser_click") {
    int tab_id = use_resolved_target ? resolved_tab_id : active_tab_id_;
    auto validation = MahoMcpBrowserActionHandler::ValidateClickRequest(
        ref_to_ax_id_, arguments);
    if (validation.error.has_value()) {
      append_browser_action_response(std::move(validation));
      return;
    }
    const int ref = arguments->FindInt("ref").value();
    if (!g_browser_delegate) {
      responses.push_back(framer_.BuildErrorResponse(
          msg.id, -32000, "Action failed: click could not be dispatched"));
      return;
    }
    const ui::AXNodeID ax_id = validation.value.FindInt("ax_id").value();
    if (!terminal_deferred_sender &&
        g_browser_delegate->RequiresDeferredVerifiedInputResponses()) {
      responses.push_back(framer_.BuildErrorResponse(
          msg.id, -32000,
          "Action failed: click verification requires deferred transport"));
      return;
    }
    if (terminal_deferred_sender) {
      g_browser_delegate->ClickVerified(
          tab_id, ax_id,
          base::BindOnce(
              [](DeferredResponseSender sender,
                 std::optional<base::Value> message_id,
                 decltype(serialize_verified_action_response) serialize,
                 int ref, InputActionOutcome outcome) {
                sender.Run(serialize(std::move(message_id), ref, "click",
                                     std::move(outcome)));
              },
              std::move(terminal_deferred_sender), CloneMessageId(msg.id),
              serialize_verified_action_response, ref));
      return;
    }
    auto immediate_response = std::make_shared<std::optional<std::string>>();
    g_browser_delegate->ClickVerified(
        tab_id, ax_id,
        base::BindOnce(
            [](std::shared_ptr<std::optional<std::string>> response,
               std::optional<base::Value> message_id,
               decltype(serialize_verified_action_response) serialize,
               int ref, InputActionOutcome outcome) {
              *response = serialize(std::move(message_id), ref, "click",
                                    std::move(outcome));
            },
            immediate_response, CloneMessageId(msg.id),
            serialize_verified_action_response, ref));
    if (*immediate_response) {
      responses.push_back(std::move(**immediate_response));
    } else {
      responses.push_back(framer_.BuildErrorResponse(
          msg.id, -32000,
          "Action failed: click verification requires deferred transport"));
    }
    return;
  }

  if (*tool_name == "browser_type") {
    int tab_id = use_resolved_target ? resolved_tab_id : active_tab_id_;
    auto validation = MahoMcpBrowserActionHandler::ValidateTypeRequest(
        g_browser_delegate, ref_to_ax_id_, tab_id, arguments);
    if (validation.error.has_value()) {
      append_browser_action_response(std::move(validation));
      return;
    }
    const int ref = arguments->FindInt("ref").value();
    const std::string text = *arguments->FindString("text");
    if (!g_browser_delegate) {
      responses.push_back(framer_.BuildErrorResponse(
          msg.id, -32000, "Action failed: type could not be dispatched"));
      return;
    }
    const ui::AXNodeID ax_id = validation.value.FindInt("ax_id").value();
    if (!terminal_deferred_sender &&
        g_browser_delegate->RequiresDeferredVerifiedInputResponses()) {
      responses.push_back(framer_.BuildErrorResponse(
          msg.id, -32000,
          "Action failed: type verification requires deferred transport"));
      return;
    }
    if (terminal_deferred_sender) {
      g_browser_delegate->TypeVerified(
          tab_id, ax_id, text,
          base::BindOnce(
              [](DeferredResponseSender sender,
                 std::optional<base::Value> message_id,
                 decltype(serialize_verified_action_response) serialize,
                 int ref, InputActionOutcome outcome) {
                sender.Run(serialize(std::move(message_id), ref, "type",
                                     std::move(outcome)));
              },
              std::move(terminal_deferred_sender), CloneMessageId(msg.id),
              serialize_verified_action_response, ref));
      return;
    }
    auto immediate_response = std::make_shared<std::optional<std::string>>();
    g_browser_delegate->TypeVerified(
        tab_id, ax_id, text,
        base::BindOnce(
            [](std::shared_ptr<std::optional<std::string>> response,
               std::optional<base::Value> message_id,
               decltype(serialize_verified_action_response) serialize,
               int ref, InputActionOutcome outcome) {
              *response = serialize(std::move(message_id), ref, "type",
                                    std::move(outcome));
            },
            immediate_response, CloneMessageId(msg.id),
            serialize_verified_action_response, ref));
    if (*immediate_response) {
      responses.push_back(std::move(**immediate_response));
    } else {
      responses.push_back(framer_.BuildErrorResponse(
          msg.id, -32000,
          "Action failed: type verification requires deferred transport"));
    }
    return;
  }

  if (*tool_name == "browser_select") {
    int tab_id = use_resolved_target ? resolved_tab_id : active_tab_id_;
    append_browser_action_response(MahoMcpBrowserActionHandler::HandleSelect(
        g_browser_delegate, ref_to_ax_id_, tab_id, arguments));
    return;
  }

  if (*tool_name == "browser_screenshot_full") {
    int tab_id = use_resolved_target
                     ? resolved_tab_id
                     : (arguments && arguments->FindInt("tab_id")
                            ? arguments->FindInt("tab_id").value()
                            : active_tab_id_);
    if (g_browser_delegate && deferred_response_sender_) {
      std::optional<base::Value> msg_id_copy;
      if (msg.id.has_value()) {
        msg_id_copy = msg.id->Clone();
      }
      DeferredResponseSender sender = terminal_deferred_sender;
      g_browser_delegate->CaptureFullPagePngBase64(
          tab_id,
          base::BindOnce(
              [](std::optional<base::Value> id, DeferredResponseSender sender,
                 std::string b64) {
                if (b64.empty()) {
                  b64 =
                      "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR4"
                      "2mNk+M9QDwADhgGAWjR9awAAAABJRU5ErkJggg==";
                }
                base::DictValue result;
                result.Set("content_type", "image/png");
                result.Set("data", std::move(b64));
                auto redacted =
                    MahoMcpFirewall::Wrap(base::Value(std::move(result)));
                base::DictValue response;
                response.Set("jsonrpc", "2.0");
                response.Set("id",
                             id.has_value() ? id->Clone() : base::Value());
                response.Set("result", redacted.get().Clone());
                std::string out;
                base::JSONWriter::Write(base::Value(std::move(response)), &out);
                out.push_back('\n');
                sender.Run(std::move(out));
              },
              std::move(msg_id_copy), std::move(sender)));
      return;
    }
    std::string b64 =
        "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNk+"
        "M9QDwADhgGAWjR9awAAAABJRU5ErkJggg==";
    base::DictValue result;
    result.Set("content_type", "image/png");
    result.Set("data", b64);
    auto redacted = MahoMcpFirewall::Wrap(base::Value(std::move(result)));
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, redacted.get().Clone());
    responses.push_back(std::move(resp));
    return;
  }

  if (*tool_name == "browser_screenshot_element") {
    if (!arguments || !arguments->FindInt("ref")) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32602, "Invalid params: ref required");
      responses.push_back(std::move(err));
      return;
    }
    int ref = arguments->FindInt("ref").value();
    auto it = ref_to_ax_id_.find(ref);
    if (it == ref_to_ax_id_.end()) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32000,
          "Unknown ref: call browser_accessibility_snapshot first");
      responses.push_back(std::move(err));
      return;
    }
    int tab_id = use_resolved_target
                     ? resolved_tab_id
                     : arguments->FindInt("tab_id").value_or(active_tab_id_);
    if (g_browser_delegate && deferred_response_sender_) {
      std::optional<base::Value> msg_id_copy;
      if (msg.id.has_value()) {
        msg_id_copy = msg.id->Clone();
      }
      DeferredResponseSender sender = terminal_deferred_sender;
      g_browser_delegate->CaptureElementPngBase64(
          tab_id, it->second,
          base::BindOnce(
              [](std::optional<base::Value> id, DeferredResponseSender sender,
                 int ref_val, std::string b64) {
                if (b64.empty()) {
                  b64 =
                      "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR4"
                      "2mNk+M9QDwADhgGAWjR9awAAAABJRU5ErkJggg==";
                }
                base::DictValue result;
                result.Set("content_type", "image/png");
                result.Set("data", std::move(b64));
                result.Set("ref", ref_val);
                auto redacted =
                    MahoMcpFirewall::Wrap(base::Value(std::move(result)));
                base::DictValue response;
                response.Set("jsonrpc", "2.0");
                response.Set("id",
                             id.has_value() ? id->Clone() : base::Value());
                response.Set("result", redacted.get().Clone());
                std::string out;
                base::JSONWriter::Write(base::Value(std::move(response)), &out);
                out.push_back('\n');
                sender.Run(std::move(out));
              },
              std::move(msg_id_copy), std::move(sender), ref));
      return;
    }
    std::string b64 =
        "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNk+"
        "M9QDwADhgGAWjR9awAAAABJRU5ErkJggg==";
    base::DictValue result;
    result.Set("content_type", "image/png");
    result.Set("data", b64);
    result.Set("ref", ref);
    auto redacted = MahoMcpFirewall::Wrap(base::Value(std::move(result)));
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, redacted.get().Clone());
    responses.push_back(std::move(resp));
    return;
  }

  // Network capture tools (Wave 4.4)
  if (*tool_name == "browser_network_start_capture") {
    if (!g_browser_delegate) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32000, "Network capture unavailable");
      responses.push_back(std::move(err));
      return;
    }
    if (!deferred_response_sender_) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32000,
          "Deferred response sender unavailable for network start capture");
      responses.push_back(std::move(err));
      return;
    }

    DeferredResponseSender sender = terminal_deferred_sender;
    std::optional<base::Value> msg_id = CloneMessageId(msg.id);
    if (network_capture_state_->active) {
      sender.Run(BuildJsonRpcErrorLine(std::move(msg_id), -32000,
                                       "Network capture already active"));
      return;
    }

    // R-8: forward the concrete id resolved by the top-level gate (never a raw
    // 0), so an omitted request binds to the same tab the gate authorized
    // rather than whatever is active when StartNetworkCapture runs. The
    // no-delegate test path keeps the legacy argument/0 behavior.
    const int requested_tab_id =
        use_resolved_target ? resolved_tab_id
                            : ((arguments && arguments->FindInt("tab_id"))
                                   ? arguments->FindInt("tab_id").value()
                                   : 0);

    const std::string capture_id =
        base::Uuid::GenerateRandomV4().AsLowercaseString();
    network_capture_state_->active = true;
    network_capture_state_->capture_id = capture_id;
    network_capture_state_->tab_id = requested_tab_id;
    network_capture_state_->target = last_resolved_target_;
    scoped_refptr<MahoMcpNetworkCaptureState> state = network_capture_state_;

    g_browser_delegate->StartNetworkCapture(
        capture_id, requested_tab_id, network_capture_state_->target,
        base::BindOnce(
            [](std::optional<base::Value> id, DeferredResponseSender sender,
               scoped_refptr<MahoMcpNetworkCaptureState> state,
               scoped_refptr<MahoMcpControllerState> controller_state,
               std::string capture_id,
               MahoMcpBrowserDelegate::StartNetworkCaptureResult result) {
              if (controller_state->is_revoked()) {
                const bool owns_capture =
                    result.success && state->capture_id == capture_id;
                if (owns_capture) {
                  state->active = false;
                  state->capture_id.clear();
                  state->tab_id = 0;
                  state->target = ResolvedMahoMcpTarget();
                }
                if (owns_capture && g_browser_delegate) {
                  g_browser_delegate->CancelNetworkCapture(capture_id);
                }
                sender.Run(BuildJsonRpcErrorLine(
                    std::move(id), -32004, "Controller session was revoked"));
                return;
              }
              if (!result.target_valid) {
                if (state->capture_id == capture_id) {
                  state->active = false;
                  state->capture_id.clear();
                  state->tab_id = 0;
                  state->target = ResolvedMahoMcpTarget();
                }
                sender.Run(BuildJsonRpcErrorLine(std::move(id),
                                                 kMahoMcpErrorTabNotFound,
                                                 kMahoMcpMessageTabNotFound));
                return;
              }
              if (!result.success) {
                // Clear only if this attempt still owns the shared state; a
                // later start may have replaced it. Safe even if the session
                // was destroyed — |state| is refcounted and outlives it.
                if (state->capture_id == capture_id) {
                  state->active = false;
                  state->capture_id.clear();
                  state->tab_id = 0;
                }
                sender.Run(BuildJsonRpcErrorLine(
                    std::move(id), -32000,
                    "No WebContents available for network capture"));
                return;
              }
              if (state->capture_id == capture_id) {
                state->tab_id = result.tab_id;
              }
              base::DictValue payload;
              payload.Set("capturing", true);
              payload.Set("capture_id", std::move(capture_id));
              payload.Set("tab_id", result.tab_id);
              sender.Run(BuildJsonRpcSuccessLine(
                  std::move(id), base::Value(std::move(payload))));
            },
            std::move(msg_id), std::move(sender), std::move(state),
            controller_state_, capture_id));
    return;
  }

  if (*tool_name == "browser_network_stop_capture") {
    if (!network_capture_state_->active ||
        network_capture_state_->capture_id.empty()) {
      std::string err = framer_.BuildErrorResponse(msg.id, -32000,
                                                   "No active capture to stop");
      responses.push_back(std::move(err));
      return;
    }
    if (!g_browser_delegate) {
      network_capture_state_->active = false;
      network_capture_state_->capture_id.clear();
      network_capture_state_->tab_id = 0;
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32000, "Network capture unavailable");
      responses.push_back(std::move(err));
      return;
    }
    if (!deferred_response_sender_) {
      const std::string capture_id = network_capture_state_->capture_id;
      network_capture_state_->active = false;
      network_capture_state_->capture_id.clear();
      network_capture_state_->tab_id = 0;
      g_browser_delegate->CancelNetworkCapture(capture_id);
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32000,
          "Deferred response sender unavailable for network stop capture");
      responses.push_back(std::move(err));
      return;
    }

    const std::string capture_id = network_capture_state_->capture_id;
    network_capture_state_->active = false;
    network_capture_state_->capture_id.clear();
    network_capture_state_->tab_id = 0;
    DeferredResponseSender sender = terminal_deferred_sender;
    g_browser_delegate->StopNetworkCapture(
        capture_id, network_capture_state_->target,
        base::BindOnce(
            [](std::optional<base::Value> id, DeferredResponseSender sender,
               MahoMcpBrowserDelegate::StopNetworkCaptureResult result) {
              if (!result.target_valid) {
                sender.Run(BuildJsonRpcErrorLine(std::move(id),
                                                 kMahoMcpErrorTabNotFound,
                                                 kMahoMcpMessageTabNotFound));
                return;
              }
              if (!result.har.has_value()) {
                sender.Run(BuildJsonRpcErrorLine(
                    std::move(id), -32000,
                    "Network capture not found or already stopped"));
                return;
              }
              sender.Run(BuildJsonRpcSuccessLine(
                  std::move(id), base::Value(std::move(*result.har).take())));
            },
            CloneMessageId(msg.id), std::move(sender)));
    return;
  }

  if (*tool_name == "browser_network_get_har") {
    if (network_capture_state_->active) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32000,
          "Capture is active; call browser_network_stop_capture for HAR");
      responses.push_back(std::move(err));
      return;
    }
    std::string err =
        framer_.BuildErrorResponse(msg.id, -32000, "No capture started");
    responses.push_back(std::move(err));
    return;
  }

  if (*tool_name == "browser_console_messages") {
    int tab_id = use_resolved_target
                     ? resolved_tab_id
                     : (arguments && arguments->FindInt("tab_id")
                            ? arguments->FindInt("tab_id").value()
                            : active_tab_id_);
    int limit = arguments && arguments->FindInt("limit")
                    ? arguments->FindInt("limit").value()
                    : 100;
    std::optional<int64_t> since_timestamp_ms;
    if (arguments && arguments->FindDouble("since_timestamp_ms")) {
      since_timestamp_ms = static_cast<int64_t>(
          arguments->FindDouble("since_timestamp_ms").value());
    } else if (arguments && arguments->FindInt("since_timestamp_ms")) {
      since_timestamp_ms = static_cast<int64_t>(
          arguments->FindInt("since_timestamp_ms").value());
    }

    std::set<std::string> level_filter;
    if (arguments && arguments->FindList("level_filter")) {
      for (const auto& item : *arguments->FindList("level_filter")) {
        if (item.is_string()) {
          level_filter.insert(item.GetString());
        }
      }
    }

    base::ListValue msgs;
    bool truncated = false;
    bool exists = TabExists(tab_id, tabs_);
    if (g_browser_delegate && exists) {
      int count = 0;
      for (const auto& msg_entry :
           g_browser_delegate->GetConsoleMessages(tab_id)) {
        if (!level_filter.empty() &&
            level_filter.find(msg_entry.level) == level_filter.end()) {
          continue;
        }
        if (since_timestamp_ms.has_value() &&
            msg_entry.timestamp_ms <= since_timestamp_ms.value()) {
          continue;
        }
        if (count >= limit) {
          truncated = true;
          break;
        }
        base::DictValue entry;
        entry.Set("level", msg_entry.level);
        entry.Set("message", msg_entry.message);
        entry.Set("source_url", msg_entry.source_url);
        entry.Set("line", msg_entry.line);
        entry.Set("timestamp_ms", static_cast<double>(msg_entry.timestamp_ms));
        msgs.Append(std::move(entry));
        count++;
      }
    } else {
      auto it = console_buffers_.find(tab_id);
      if (it != console_buffers_.end()) {
        int count = 0;
        for (const auto& msg_entry : it->second) {
          if (!level_filter.empty() &&
              level_filter.find(msg_entry.level) == level_filter.end()) {
            continue;
          }
          if (since_timestamp_ms.has_value() &&
              msg_entry.timestamp_ms <= since_timestamp_ms.value()) {
            continue;
          }
          if (count >= limit) {
            truncated = true;
            break;
          }
          base::DictValue entry;
          entry.Set("level", msg_entry.level);
          entry.Set("message", msg_entry.message);
          entry.Set("source_url", msg_entry.source_url);
          entry.Set("line", msg_entry.line);
          entry.Set("timestamp_ms",
                    static_cast<double>(msg_entry.timestamp_ms));
          msgs.Append(std::move(entry));
          count++;
        }
      }
    }

    base::DictValue result;
    result.Set("messages", std::move(msgs));
    result.Set("truncated", truncated);

    auto redacted = MahoMcpFirewall::Wrap(base::Value(std::move(result)));
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, redacted.get().Clone());
    responses.push_back(std::move(resp));
    return;
  }

  if (*tool_name == "browser_wait_for_navigation") {
    int tab_id = use_resolved_target
                     ? resolved_tab_id
                     : (arguments && arguments->FindInt("tab_id")
                            ? arguments->FindInt("tab_id").value()
                            : active_tab_id_);
    int64_t since_ms = 0;
    if (arguments) {
      if (arguments->FindDouble("since_timestamp_ms")) {
        since_ms = static_cast<int64_t>(
            arguments->FindDouble("since_timestamp_ms").value());
      } else if (arguments->FindInt("since_timestamp_ms")) {
        since_ms = static_cast<int64_t>(
            arguments->FindInt("since_timestamp_ms").value());
      }
    }
    if (since_ms == 0) {
      since_ms = base::Time::Now().InMillisecondsSinceUnixEpoch();
    }
    int timeout_ms = arguments && arguments->FindInt("timeout_ms")
                         ? arguments->FindInt("timeout_ms").value()
                         : 30000;

    base::TimeTicks start = base::TimeTicks::Now();
    base::TimeDelta timeout = base::Milliseconds(timeout_ms);
    std::vector<MahoMcpSession::NavigationEvent> events;
    if (g_browser_delegate) {
      while (base::TimeTicks::Now() - start < timeout) {
        events = g_browser_delegate->GetNavigationEvents(tab_id, since_ms);
        if (!events.empty()) {
          break;
        }
        base::RunLoop run_loop(base::RunLoop::Type::kNestableTasksAllowed);
        base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
            FROM_HERE, run_loop.QuitClosure(), base::Milliseconds(100));
        run_loop.Run();
      }
    }

    base::DictValue result;
    bool got_event = false;
    if (!events.empty()) {
      const auto& ev = events.back();
      result.Set("navigated", true);
      result.Set("url", ev.url);
      result.Set("title", ev.title);
      result.Set("status_code", ev.status_code);
      result.Set("timestamp_ms", static_cast<double>(ev.timestamp_ms));
      got_event = true;
    }

    if (!got_event && g_browser_delegate) {
      int target_tab = (tab_id == 0) ? active_tab_id_ : tab_id;
      for (const auto& t : g_browser_delegate->GetTabList()) {
        if ((tab_id == 0 && t.is_active) || t.id == target_tab) {
          result.Set("navigated", false);
          result.Set("url", t.url);
          result.Set("title", t.title);
          result.Set("status_code", 200);
          result.Set("timestamp_ms",
                     static_cast<double>(
                         base::Time::Now().InMillisecondsSinceUnixEpoch()));
          got_event = true;
          break;
        }
      }
    }
    if (!got_event) {
      const TabInfo* found = nullptr;
      for (const auto& t : tabs_) {
        if (t.id == tab_id) {
          found = &t;
          break;
        }
      }
      if (found) {
        result.Set("navigated", false);
        result.Set("url", found->url);
        result.Set("title", found->title);
        result.Set("status_code", 200);
        result.Set("timestamp_ms",
                   static_cast<double>(
                       base::Time::Now().InMillisecondsSinceUnixEpoch()));
      } else {
        result.Set("timeout", true);
        result.Set("last_url", "");
      }
    }

    auto redacted = MahoMcpFirewall::Wrap(base::Value(std::move(result)));
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, redacted.get().Clone());
    responses.push_back(std::move(resp));
    return;
  }

  if (*tool_name == "browser_tab_switch") {
    if (!arguments || !arguments->FindInt("tab_id")) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32602, "Invalid params: tab_id required");
      responses.push_back(std::move(err));
      return;
    }
    int target_id = arguments->FindInt("tab_id").value();
    bool found = false;
    int previous_active_id = active_tab_id_;

    if (TabExists(target_id, tabs_)) {
      found = true;
      if (g_browser_delegate) {
        g_browser_delegate->ActivateTab(use_resolved_target ? resolved_tab_id
                                                            : target_id);
      }
    } else {
      for (const auto& t : tabs_) {
        if (t.id == target_id) {
          found = true;
          break;
        }
      }
    }

    if (!found) {
      std::string err =
          framer_.BuildErrorResponse(msg.id, -32000, "Unknown tab_id");
      responses.push_back(std::move(err));
      return;
    }
    for (auto& t : tabs_) {
      t.is_active = (t.id == target_id);
    }
    active_tab_id_ = target_id;
    base::DictValue result;
    result.Set("activated", true);
    result.Set("previous_tab_id", previous_active_id);
    auto redacted = MahoMcpFirewall::Wrap(base::Value(std::move(result)));
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, redacted.get().Clone());
    responses.push_back(std::move(resp));
    return;
  }

  if (*tool_name == "browser_set_viewport_size") {
    if (!arguments || !arguments->FindInt("width_px") ||
        !arguments->FindInt("height_px")) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32602, "Invalid params: width_px and height_px required");
      responses.push_back(std::move(err));
      return;
    }
    int width = arguments->FindInt("width_px").value();
    int height = arguments->FindInt("height_px").value();
    if (width < 100 || width > 4096 || height < 100 || height > 4096) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32602,
          "Invalid params: dimensions out of bounds (100-4096)");
      responses.push_back(std::move(err));
      return;
    }
    viewport_width_ = width;
    viewport_height_ = height;

    int tab_id = use_resolved_target
                     ? resolved_tab_id
                     : arguments->FindInt("tab_id").value_or(active_tab_id_);
    if (g_browser_delegate) {
      g_browser_delegate->SetViewportSize(tab_id, width, height);
    }

    base::DictValue result;
    result.Set("width", width);
    result.Set("height", height);
    result.Set("device_scale_factor", 1.0);
    auto redacted = MahoMcpFirewall::Wrap(base::Value(std::move(result)));
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, redacted.get().Clone());
    responses.push_back(std::move(resp));
    return;
  }

  if (*tool_name == "browser_scroll") {
    int tab_id =
        use_resolved_target
            ? resolved_tab_id
            : (arguments ? arguments->FindInt("tab_id").value_or(active_tab_id_)
                         : active_tab_id_);
    append_browser_action_response(MahoMcpBrowserActionHandler::HandleScroll(
        g_browser_delegate, ref_to_ax_id_, scroll_y_positions_, tab_id,
        arguments));
    return;
  }

  if (*tool_name == "browser_hover") {
    int tab_id =
        use_resolved_target
            ? resolved_tab_id
            : (arguments ? arguments->FindInt("tab_id").value_or(active_tab_id_)
                         : active_tab_id_);
    append_browser_action_response(MahoMcpBrowserActionHandler::HandleHover(
        g_browser_delegate, ref_to_ax_id_, tab_id, arguments));
    return;
  }

  if (*tool_name == "browser_key_press") {
    int tab_id =
        use_resolved_target
            ? resolved_tab_id
            : (arguments ? arguments->FindInt("tab_id").value_or(active_tab_id_)
                         : active_tab_id_);
    append_browser_action_response(MahoMcpBrowserActionHandler::HandleKeyPress(
        g_browser_delegate, tab_id, arguments));
    return;
  }

  if (*tool_name == "browser_file_upload_select") {
    int tab_id =
        use_resolved_target
            ? resolved_tab_id
            : (arguments ? arguments->FindInt("tab_id").value_or(active_tab_id_)
                         : active_tab_id_);
    append_browser_action_response(
        MahoMcpBrowserActionHandler::HandleFileUpload(g_browser_delegate,
                                                       tab_id, arguments));
    return;
  }

  std::string err = framer_.BuildErrorResponse(
      msg.id, -32601, "Method not found: unknown tool");
  responses.push_back(std::move(err));
}

bool MahoMcpBrowserDelegate::SelectFileForPendingChooser(
    int tab_id,
    const std::string& path) {
  return false;
}

std::string MahoMcpBrowserDelegate::ListRoutines() {
  return "[]";
}

void MahoMcpBrowserDelegate::RunRoutine(const std::string& id,
                                        RunRoutineCallback callback) {
  std::move(callback).Run(RoutineRunError::kUnavailable,
                          "Routines feature unavailable");
}

void MahoMcpBrowserDelegate::MailListAccounts(MailReadCallback callback) {
  std::move(callback).Run(false, "Mail read broker unavailable");
}

void MahoMcpBrowserDelegate::MailListFolders(const std::string& account_id,
                                             MailReadCallback callback) {
  std::move(callback).Run(false, "Mail read broker unavailable");
}

void MahoMcpBrowserDelegate::MailListEmails(const std::string& account_id,
                                            const std::string& folder_id,
                                            int64_t limit,
                                            int64_t offset,
                                            MailReadCallback callback) {
  std::move(callback).Run(false, "Mail read broker unavailable");
}

void MahoMcpBrowserDelegate::MailGetEmail(const std::string& email_id,
                                          MailReadCallback callback) {
  std::move(callback).Run(false, "Mail read broker unavailable");
}

void MahoMcpBrowserDelegate::MailSearchEmails(const std::string& query_json,
                                              MailReadCallback callback) {
  std::move(callback).Run(false, "Mail read broker unavailable");
}

void MahoMcpBrowserDelegate::MailListThread(const std::string& account_id,
                                            const std::string& message_id,
                                            MailReadCallback callback) {
  std::move(callback).Run(false, "Mail read broker unavailable");
}

void MahoMcpBrowserDelegate::MailAddAccount(const std::string& request_json,
                                            MailReadCallback callback) {
  std::move(callback).Run(false, "Mail onboarding broker unavailable");
}

void MahoMcpBrowserDelegate::MailTestConnection(const std::string& params_json,
                                                MailReadCallback callback) {
  std::move(callback).Run(false, "Mail onboarding broker unavailable");
}

void MahoMcpBrowserDelegate::MailDeleteAccount(const std::string& account_id,
                                               MailReadCallback callback) {
  std::move(callback).Run(false, "Mail onboarding broker unavailable");
}

void MahoMcpBrowserDelegate::MailOAuthStartUrl(const std::string& provider,
                                               const std::string& client_id,
                                               const std::string& redirect_uri,
                                               MailReadCallback callback) {
  std::move(callback).Run(false, "Mail onboarding broker unavailable");
}

void MahoMcpBrowserDelegate::MailOAuthComplete(const std::string& state,
                                               const std::string& code,
                                               MailReadCallback callback) {
  std::move(callback).Run(false, "Mail onboarding broker unavailable");
}

void MahoMcpBrowserDelegate::MailReconnectAccount(const std::string& account_id,
                                                  MailReadCallback callback) {
  std::move(callback).Run(false, "Mail onboarding broker unavailable");
}

void MahoMcpBrowserDelegate::MailImportMigrationArchive(
    const std::string& archive_json,
    MailReadCallback callback) {
  std::move(callback).Run(false, "Mail onboarding broker unavailable");
}

void MahoMcpBrowserDelegate::MailExtractOtp(const std::string& account_id,
                                            const std::string& folder_id,
                                            const std::string& query,
                                            int64_t max_age_seconds,
                                            MailReadCallback callback) {
  std::move(callback).Run(false, "Mail onboarding broker unavailable");
}

void MahoMcpBrowserDelegate::MailSendEmail(const std::string& request_json,
                                           bool /*already_authorized*/,
                                           MailReadCallback callback) {
  std::move(callback).Run(false, "Mail write broker unavailable");
}

void MahoMcpBrowserDelegate::MailSaveDraft(const std::string& request_json,
                                           bool /*already_authorized*/,
                                           MailReadCallback callback) {
  std::move(callback).Run(false, "Mail write broker unavailable");
}

void MahoMcpBrowserDelegate::MailUpdateDraft(const std::string& draft_id,
                                             const std::string& request_json,
                                             bool /*already_authorized*/,
                                             MailReadCallback callback) {
  std::move(callback).Run(false, "Mail write broker unavailable");
}

void MahoMcpBrowserDelegate::MailQueueEmail(const std::string& request_json,
                                            MailReadCallback callback) {
  std::move(callback).Run(false, "Mail write broker unavailable");
}

void MahoMcpBrowserDelegate::MailFlag(const std::string& request_json,
                                      MailReadCallback callback) {
  std::move(callback).Run(false, "Mail write broker unavailable");
}

ai::MailAuthorizationContext
MahoMcpBrowserDelegate::GetMailAuthorizationContext() {
  return {};
}

bool MahoMcpBrowserDelegate::ConfirmMailToolApproval(
    std::string_view tool_name,
    std::string_view redacted_arguments) {
  return false;
}

bool MahoMcpBrowserDelegate::ConfirmCredentialTypingApproval(
    std::string_view tool_name,
    const ResolvedMahoMcpTarget& target) {
  return false;
}

bool MahoMcpBrowserDelegate::ConfirmBrowserActionApproval(
    std::string_view tool_name,
    const ResolvedMahoMcpTarget& target) {
  return false;
}

MahoMcpFeatureGates MahoMcpBrowserDelegate::GetFeatureGates() {
  return {};
}

std::string MahoMcpBrowserDelegate::ListArtifacts(
    std::string_view session_id) {
  return "[]";
}

void MahoMcpBrowserDelegate::ExportArtifact(
    std::string artifact_id,
    base::FilePath destination,
    ExportArtifactCallback callback) {
  std::move(callback).Run(false, "Artifact export unavailable");
}

std::vector<MahoMcpBrowserDelegate::VaultCredentialSummary>
MahoMcpBrowserDelegate::VaultListCredentialsForActivePage(int tab_id) {
  return {};
}

std::optional<std::string> MahoMcpBrowserDelegate::VaultRequestCredentialUse(
    int tab_id,
    const std::string& handle,
    const std::string& origin) {
  return std::nullopt;
}

bool MahoMcpBrowserDelegate::VaultFillCredential(
    int tab_id,
    const std::string& grant_handle,
    ui::AXNodeID ax_id) {
  return false;
}

bool MahoMcpBrowserDelegate::VaultFillTotp(int tab_id,
                                           const std::string& grant_handle,
                                           ui::AXNodeID ax_id) {
  return false;
}

MahoMcpTargetResolution MahoMcpBrowserDelegate::ResolveTabTarget(
    int requested_tab_id) {
  MahoMcpTargetResolution resolution;
  resolution.target.valid = true;
  resolution.target.tab_id = requested_tab_id;
  return resolution;
}

MahoMcpTargetResolution MahoMcpBrowserDelegate::ResolveProfileTarget() {
  MahoMcpTargetResolution resolution;
  resolution.target.valid = true;
  return resolution;
}

bool MahoMcpBrowserDelegate::RevalidateTarget(
    const ResolvedMahoMcpTarget& target) {
  return true;
}

MahoMcpFieldMetadata MahoMcpBrowserDelegate::GetFieldMetadata(
    int tab_id,
    ui::AXNodeID ax_id) {
  return MahoMcpFieldMetadata();
}

// static
bool MahoMcpSession::IsCredentialField(const MahoMcpFieldMetadata& meta) {
  return MahoMcpBrowserActionHandler::IsCredentialField(meta);
}

}  // namespace maho
