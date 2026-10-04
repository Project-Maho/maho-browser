// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ai/maho_authenticated_service_api_broker.h"
#include "maho/browser/ai/maho_credential_redaction.h"

#include <algorithm>
#include <sstream>

#if !defined(MAHO_STANDALONE_TEST)
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/storage_partition.h"
#include "maho/browser/ai/maho_ai_security_utils.h"
#include "net/base/load_flags.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/fetch_api.mojom.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "url/gurl.h"
#endif

namespace maho::ai {

ModelFacingDirectApiRequest::ModelFacingDirectApiRequest() = default;
ModelFacingDirectApiRequest::~ModelFacingDirectApiRequest() = default;
ModelFacingDirectApiRequest::ModelFacingDirectApiRequest(
    const ModelFacingDirectApiRequest&) = default;
ModelFacingDirectApiRequest& ModelFacingDirectApiRequest::operator=(
    const ModelFacingDirectApiRequest&) = default;
ModelFacingDirectApiRequest::ModelFacingDirectApiRequest(
    ModelFacingDirectApiRequest&&) = default;
ModelFacingDirectApiRequest& ModelFacingDirectApiRequest::operator=(
    ModelFacingDirectApiRequest&&) = default;

TransportRequest::TransportRequest() = default;
TransportRequest::~TransportRequest() = default;
TransportRequest::TransportRequest(const TransportRequest&) = default;
TransportRequest& TransportRequest::operator=(const TransportRequest&) = default;
TransportRequest::TransportRequest(TransportRequest&&) = default;
TransportRequest& TransportRequest::operator=(TransportRequest&&) = default;

TransportResponse::TransportResponse() = default;
TransportResponse::~TransportResponse() = default;
TransportResponse::TransportResponse(const TransportResponse&) = default;
TransportResponse& TransportResponse::operator=(const TransportResponse&) = default;
TransportResponse::TransportResponse(TransportResponse&&) = default;
TransportResponse& TransportResponse::operator=(TransportResponse&&) = default;

namespace {

std::string TrimWhitespace(std::string_view s) {
  size_t start = s.find_first_not_of(" \t\r\n");
  if (start == std::string_view::npos) {
    return "";
  }
  size_t end = s.find_last_not_of(" \t\r\n");
  return std::string(s.substr(start, end - start + 1));
}

std::string EscapeJson(std::string_view input) {
  std::string output;
  output.reserve(input.size() + 8);
  for (char c : input) {
    switch (c) {
      case '\"': output += "\\\""; break;
      case '\\': output += "\\\\"; break;
      case '\b': output += "\\b"; break;
      case '\f': output += "\\f"; break;
      case '\n': output += "\\n"; break;
      case '\r': output += "\\r"; break;
      case '\t': output += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buf[8];
          snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
          output += buf;
        } else {
          output += c;
        }
        break;
    }
  }
  return output;
}

}  // namespace

std::string_view ServiceKindToString(DirectApiServiceKind kind) {
  switch (kind) {
    case DirectApiServiceKind::kGmail:
      return "gmail";
    case DirectApiServiceKind::kGoogleDrive:
      return "google_drive";
    case DirectApiServiceKind::kGoogleCalendar:
      return "google_calendar";
    case DirectApiServiceKind::kGoogleSheets:
      return "google_sheets";
    case DirectApiServiceKind::kSlack:
      return "slack";
    case DirectApiServiceKind::kDiscord:
      return "discord";
    case DirectApiServiceKind::kTelegram:
      return "telegram";
    case DirectApiServiceKind::kUnknown:
      return "unknown";
  }
  return "unknown";
}

DirectApiServiceKind ServiceKindFromString(std::string_view name) {
  if (name == "gmail") return DirectApiServiceKind::kGmail;
  if (name == "google_drive") return DirectApiServiceKind::kGoogleDrive;
  if (name == "google_calendar") return DirectApiServiceKind::kGoogleCalendar;
  if (name == "google_sheets") return DirectApiServiceKind::kGoogleSheets;
  if (name == "slack") return DirectApiServiceKind::kSlack;
  if (name == "discord") return DirectApiServiceKind::kDiscord;
  if (name == "telegram") return DirectApiServiceKind::kTelegram;
  return DirectApiServiceKind::kUnknown;
}

std::string_view ErrorCodeToString(DirectApiErrorCode code) {
  switch (code) {
    case DirectApiErrorCode::kOk:
      return "ok";
    case DirectApiErrorCode::kScopeDenied:
      return "scope_denied";
    case DirectApiErrorCode::kMissingCredentials:
      return "missing_credentials";
    case DirectApiErrorCode::kInvalidAuthHandle:
      return "invalid_auth_handle";
    case DirectApiErrorCode::kConfirmationRequired:
      return "confirmation_required";
    case DirectApiErrorCode::kInvalidApprovalToken:
      return "invalid_approval_token";
    case DirectApiErrorCode::kApprovalTokenAlreadyUsed:
      return "approval_token_already_used";
    case DirectApiErrorCode::kRateLimited:
      return "rate_limited";
    case DirectApiErrorCode::kNetworkError:
      return "network_error";
    case DirectApiErrorCode::kServiceUnavailable:
      return "service_unavailable";
    case DirectApiErrorCode::kResourceNotFound:
      return "resource_not_found";
    case DirectApiErrorCode::kBadRequest:
      return "bad_request";
    case DirectApiErrorCode::kOperationCancelled:
      return "operation_cancelled";
    case DirectApiErrorCode::kInternalError:
      return "internal_error";
    case DirectApiErrorCode::kTypedUnavailable:
      return "typed_unavailable";
  }
  return "internal_error";
}

DirectApiErrorCode ErrorCodeFromString(std::string_view str) {
  if (str == "ok") return DirectApiErrorCode::kOk;
  if (str == "scope_denied") return DirectApiErrorCode::kScopeDenied;
  if (str == "missing_credentials") return DirectApiErrorCode::kMissingCredentials;
  if (str == "invalid_auth_handle") return DirectApiErrorCode::kInvalidAuthHandle;
  if (str == "confirmation_required") return DirectApiErrorCode::kConfirmationRequired;
  if (str == "invalid_approval_token") return DirectApiErrorCode::kInvalidApprovalToken;
  if (str == "approval_token_already_used") return DirectApiErrorCode::kApprovalTokenAlreadyUsed;
  if (str == "rate_limited") return DirectApiErrorCode::kRateLimited;
  if (str == "network_error") return DirectApiErrorCode::kNetworkError;
  if (str == "service_unavailable") return DirectApiErrorCode::kServiceUnavailable;
  if (str == "resource_not_found") return DirectApiErrorCode::kResourceNotFound;
  if (str == "bad_request") return DirectApiErrorCode::kBadRequest;
  if (str == "operation_cancelled") return DirectApiErrorCode::kOperationCancelled;
  if (str == "typed_unavailable") return DirectApiErrorCode::kTypedUnavailable;
  return DirectApiErrorCode::kInternalError;
}

std::string_view OutcomeStatusToString(DirectApiOutcomeStatus status) {
  switch (status) {
    case DirectApiOutcomeStatus::kOk:
      return "ok";
    case DirectApiOutcomeStatus::kNeedsConfirmation:
      return "needs_confirmation";
    case DirectApiOutcomeStatus::kForbidden:
      return "forbidden";
    case DirectApiOutcomeStatus::kTypedError:
      return "typed_error";
    case DirectApiOutcomeStatus::kTypedUnavailable:
      return "typed_unavailable";
    case DirectApiOutcomeStatus::kCancelled:
      return "cancelled";
  }
  return "unknown";
}

// ---------------------------------------------------------------------------
// DirectApiOpDescriptor
// ---------------------------------------------------------------------------

DirectApiOpDescriptor::DirectApiOpDescriptor() = default;

DirectApiOpDescriptor::DirectApiOpDescriptor(
    DirectApiServiceKind service_in,
    std::string operation_name_in,
    std::string parameters_json_in,
    bool read_only_in,
    std::vector<std::string> required_scopes_in)
    : service(service_in),
      operation_name(std::move(operation_name_in)),
      parameters_json(std::move(parameters_json_in)),
      read_only(read_only_in),
      required_scopes(std::move(required_scopes_in)) {}

DirectApiOpDescriptor::~DirectApiOpDescriptor() = default;
DirectApiOpDescriptor::DirectApiOpDescriptor(const DirectApiOpDescriptor&) = default;
DirectApiOpDescriptor& DirectApiOpDescriptor::operator=(const DirectApiOpDescriptor&) = default;
DirectApiOpDescriptor::DirectApiOpDescriptor(DirectApiOpDescriptor&&) noexcept = default;
DirectApiOpDescriptor& DirectApiOpDescriptor::operator=(DirectApiOpDescriptor&&) noexcept = default;

// ---------------------------------------------------------------------------
// DirectApiExecutionContext
// ---------------------------------------------------------------------------

DirectApiExecutionContext::DirectApiExecutionContext() = default;
DirectApiExecutionContext::DirectApiExecutionContext(std::string session_id_in)
    : session_id(std::move(session_id_in)) {}
DirectApiExecutionContext::~DirectApiExecutionContext() = default;
DirectApiExecutionContext::DirectApiExecutionContext(const DirectApiExecutionContext&) = default;
DirectApiExecutionContext& DirectApiExecutionContext::operator=(const DirectApiExecutionContext&) = default;

std::string DirectApiExecutionContext::ToJson() const {
  std::ostringstream ss;
  ss << "{\"session_id\":\"" << EscapeJson(session_id) << "\"";
  if (!account_id.empty()) {
    ss << ",\"account_id\":\"" << EscapeJson(account_id) << "\"";
  }
  if (!opaque_auth_handle.empty()) {
    ss << ",\"opaque_auth_handle\":\"" << EscapeJson(opaque_auth_handle) << "\"";
  }
  ss << "}";
  return ss.str();
}

std::string DirectApiExecutionContext::ToDebugString() const {
  return "DirectApiExecutionContext(session=" + session_id +
         ", account=" + (account_id.empty() ? "<none>" : account_id) +
         ", handle=" + (opaque_auth_handle.empty() ? "<none>" : opaque_auth_handle) + ")";
}

// ---------------------------------------------------------------------------
// ModelFacingDirectApiRequest
// ---------------------------------------------------------------------------

ModelFacingDirectApiRequest ModelFacingDirectApiRequest::FromOp(
    const DirectApiOpDescriptor& op) {
  ModelFacingDirectApiRequest req;
  req.service = op.service;
  req.operation_name = op.operation_name;
  req.scopes = op.required_scopes;
  req.parameters_json = op.parameters_json;
  req.read_only = op.read_only;
  return req;
}

std::string ModelFacingDirectApiRequest::ToJson() const {
  std::ostringstream ss;
  ss << "{\"service\":\"" << ServiceKindToString(service) << "\""
     << ",\"operation_name\":\"" << EscapeJson(operation_name) << "\""
     << ",\"read_only\":" << (read_only ? "true" : "false")
     << ",\"scopes\":[";
  for (size_t i = 0; i < scopes.size(); ++i) {
    if (i > 0) ss << ",";
    ss << "\"" << EscapeJson(scopes[i]) << "\"";
  }
  ss << "],\"parameters\":" << (parameters_json.empty() ? "{}" : parameters_json)
     << "}";
  return ss.str();
}

// ---------------------------------------------------------------------------
// DirectApiExecutionOutcome
// ---------------------------------------------------------------------------

DirectApiExecutionOutcome::DirectApiExecutionOutcome() = default;
DirectApiExecutionOutcome::~DirectApiExecutionOutcome() = default;
DirectApiExecutionOutcome::DirectApiExecutionOutcome(const DirectApiExecutionOutcome&) = default;
DirectApiExecutionOutcome& DirectApiExecutionOutcome::operator=(const DirectApiExecutionOutcome&) = default;
DirectApiExecutionOutcome::DirectApiExecutionOutcome(DirectApiExecutionOutcome&&) noexcept = default;
DirectApiExecutionOutcome& DirectApiExecutionOutcome::operator=(DirectApiExecutionOutcome&&) noexcept = default;

DirectApiExecutionOutcome DirectApiExecutionOutcome::MakeOk(
    DirectApiServiceKind service,
    std::string op_name,
    std::string payload_json) {
  DirectApiExecutionOutcome out;
  out.status = DirectApiOutcomeStatus::kOk;
  out.service = service;
  out.operation_name = std::move(op_name);
  out.payload_json = std::move(payload_json);
  return out;
}

DirectApiExecutionOutcome DirectApiExecutionOutcome::MakeNeedsConfirmation(
    std::string action_id,
    std::string description,
    std::vector<std::string> required_scopes) {
  DirectApiExecutionOutcome out;
  out.status = DirectApiOutcomeStatus::kNeedsConfirmation;
  out.action_id = std::move(action_id);
  out.description = std::move(description);
  out.required_scopes = std::move(required_scopes);
  out.is_policy_denial = true;
  return out;
}

DirectApiExecutionOutcome DirectApiExecutionOutcome::MakeForbidden(
    std::string reason,
    std::vector<std::string> missing_scopes) {
  DirectApiExecutionOutcome out;
  out.status = DirectApiOutcomeStatus::kForbidden;
  out.reason = std::move(reason);
  out.missing_scopes = std::move(missing_scopes);
  out.is_policy_denial = true;
  return out;
}

DirectApiExecutionOutcome DirectApiExecutionOutcome::MakeTypedError(
    DirectApiErrorCode code,
    std::string message,
    bool retryable) {
  DirectApiExecutionOutcome out;
  out.status = DirectApiOutcomeStatus::kTypedError;
  out.error_code = code;
  out.error_message = std::move(message);
  out.retryable = retryable;
  out.is_policy_denial = !retryable &&
      (code == DirectApiErrorCode::kScopeDenied ||
       code == DirectApiErrorCode::kMissingCredentials ||
       code == DirectApiErrorCode::kInvalidAuthHandle ||
       code == DirectApiErrorCode::kConfirmationRequired ||
       code == DirectApiErrorCode::kInvalidApprovalToken ||
       code == DirectApiErrorCode::kApprovalTokenAlreadyUsed);
  return out;
}

DirectApiExecutionOutcome DirectApiExecutionOutcome::MakeTypedUnavailable(
    std::string reason,
    bool can_fallback_to_tabs) {
  DirectApiExecutionOutcome out;
  out.status = DirectApiOutcomeStatus::kTypedUnavailable;
  out.reason = std::move(reason);
  out.can_fallback_to_tabs = can_fallback_to_tabs;
  out.is_policy_denial = false;
  return out;
}

DirectApiExecutionOutcome DirectApiExecutionOutcome::MakeCancelled(
    std::string reason) {
  DirectApiExecutionOutcome out;
  out.status = DirectApiOutcomeStatus::kCancelled;
  out.reason = std::move(reason);
  out.is_policy_denial = false;
  return out;
}

HighLevelDirectApiOutcome DirectApiExecutionOutcome::ToHighLevelOutcome() const {
  HighLevelDirectApiOutcome hl;
  switch (status) {
    case DirectApiOutcomeStatus::kOk:
      hl.type = HighLevelDirectApiOutcome::Type::kSupported;
      hl.payload_or_reason = payload_json;
      break;
    case DirectApiOutcomeStatus::kNeedsConfirmation:
      hl.type = HighLevelDirectApiOutcome::Type::kHardFailure;
      hl.error_code = "CONFIRMATION_REQUIRED";
      hl.payload_or_reason = description;
      hl.is_policy_denial = true;
      hl.can_fallback_to_tabs = false;
      break;
    case DirectApiOutcomeStatus::kForbidden:
      hl.type = HighLevelDirectApiOutcome::Type::kHardFailure;
      hl.error_code = "FORBIDDEN";
      hl.payload_or_reason = reason;
      hl.is_policy_denial = true;
      hl.can_fallback_to_tabs = false;
      break;
    case DirectApiOutcomeStatus::kTypedError:
      if (error_code == DirectApiErrorCode::kRateLimited ||
          error_code == DirectApiErrorCode::kServiceUnavailable) {
        hl.type = HighLevelDirectApiOutcome::Type::kTypedUnavailable;
        hl.payload_or_reason = error_message;
        hl.can_fallback_to_tabs = true;
        hl.is_policy_denial = false;
      } else {
        hl.type = HighLevelDirectApiOutcome::Type::kHardFailure;
        std::string code_str(ErrorCodeToString(error_code));
        std::transform(code_str.begin(), code_str.end(), code_str.begin(), ::toupper);
        hl.error_code = code_str;
        hl.payload_or_reason = error_message;
        hl.is_policy_denial = is_policy_denial;
        hl.can_fallback_to_tabs = false;
      }
      break;
    case DirectApiOutcomeStatus::kTypedUnavailable:
      hl.type = HighLevelDirectApiOutcome::Type::kTypedUnavailable;
      hl.payload_or_reason = reason;
      hl.can_fallback_to_tabs = can_fallback_to_tabs;
      hl.is_policy_denial = false;
      break;
    case DirectApiOutcomeStatus::kCancelled:
      hl.type = HighLevelDirectApiOutcome::Type::kHardFailure;
      hl.error_code = "CANCELLED";
      hl.payload_or_reason = reason;
      hl.is_policy_denial = false;
      hl.can_fallback_to_tabs = false;
      break;
  }
  return hl;
}

std::string DirectApiExecutionOutcome::ToJson() const {
  std::ostringstream ss;
  ss << "{\"status\":\"" << OutcomeStatusToString(status) << "\"";
  switch (status) {
    case DirectApiOutcomeStatus::kOk:
      ss << ",\"service\":\"" << ServiceKindToString(service) << "\""
         << ",\"operation_name\":\"" << EscapeJson(operation_name) << "\""
         << ",\"payload\":" << (payload_json.empty() ? "{}" : payload_json);
      break;
    case DirectApiOutcomeStatus::kNeedsConfirmation:
      ss << ",\"action_id\":\"" << EscapeJson(action_id) << "\""
         << ",\"description\":\"" << EscapeJson(description) << "\""
         << ",\"required_scopes\":[";
      for (size_t i = 0; i < required_scopes.size(); ++i) {
        if (i > 0) ss << ",";
        ss << "\"" << EscapeJson(required_scopes[i]) << "\"";
      }
      ss << "]";
      break;
    case DirectApiOutcomeStatus::kForbidden:
      ss << ",\"reason\":\"" << EscapeJson(reason) << "\""
         << ",\"missing_scopes\":[";
      for (size_t i = 0; i < missing_scopes.size(); ++i) {
        if (i > 0) ss << ",";
        ss << "\"" << EscapeJson(missing_scopes[i]) << "\"";
      }
      ss << "]";
      break;
    case DirectApiOutcomeStatus::kTypedError:
      ss << ",\"code\":\"" << ErrorCodeToString(error_code) << "\""
         << ",\"message\":\"" << EscapeJson(error_message) << "\""
         << ",\"retryable\":" << (retryable ? "true" : "false");
      break;
    case DirectApiOutcomeStatus::kTypedUnavailable:
      ss << ",\"reason\":\"" << EscapeJson(reason) << "\""
         << ",\"can_fallback_to_tabs\":" << (can_fallback_to_tabs ? "true" : "false");
      break;
    case DirectApiOutcomeStatus::kCancelled:
      ss << ",\"reason\":\"" << EscapeJson(reason) << "\"";
      break;
  }
  ss << "}";
  return ss.str();
}

std::string DirectApiExecutionOutcome::ToDebugString() const {
  return "DirectApiExecutionOutcome(" + std::string(OutcomeStatusToString(status)) + ")";
}

// ---------------------------------------------------------------------------
// VaultCredential
// ---------------------------------------------------------------------------

VaultCredential::VaultCredential() = default;
VaultCredential::VaultCredential(std::string token_type_in, std::string secret_in)
    : token_type(std::move(token_type_in)), secret(std::move(secret_in)) {}
VaultCredential::~VaultCredential() = default;
VaultCredential::VaultCredential(const VaultCredential&) = default;
VaultCredential& VaultCredential::operator=(const VaultCredential&) = default;

VaultCredential VaultCredential::Bearer(std::string token) {
  return VaultCredential("Bearer", std::move(token));
}

VaultCredential VaultCredential::Cookie(std::string cookie_header) {
  return VaultCredential("Cookie", std::move(cookie_header));
}

std::string VaultCredential::FormatAuthHeader() const {
  if (token_type == "Bearer") {
    return "Bearer " + secret;
  }
  if (token_type == "Cookie") {
    return secret;
  }
  return token_type + " " + secret;
}

std::string VaultCredential::ToDebugString() const {
  return "VaultCredential(" + token_type + " [REDACTED])";
}

// ---------------------------------------------------------------------------
// InMemoryCredentialVault
// ---------------------------------------------------------------------------

InMemoryCredentialVault::InMemoryCredentialVault() = default;
InMemoryCredentialVault::~InMemoryCredentialVault() = default;

std::optional<VaultCredential> InMemoryCredentialVault::GetCredential(
    const std::string& handle) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = credentials_.find(handle);
  if (it != credentials_.end()) {
    return it->second;
  }
  return std::nullopt;
}

void InMemoryCredentialVault::StoreCredential(const std::string& handle,
                                             VaultCredential cred) {
  std::lock_guard<std::mutex> lock(mutex_);
  credentials_[handle] = std::move(cred);
}

// ---------------------------------------------------------------------------
// InMemoryApprovalTokenStore
// ---------------------------------------------------------------------------

InMemoryApprovalTokenStore::InMemoryApprovalTokenStore() = default;
InMemoryApprovalTokenStore::~InMemoryApprovalTokenStore() = default;

std::string InMemoryApprovalTokenStore::IssueToken(const std::string& action_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::string token = "appr_" + std::to_string(++counter_) + "_auth";
  valid_tokens_[action_id].insert(token);
  return token;
}

DirectApiErrorCode InMemoryApprovalTokenStore::ConsumeToken(
    const std::string& action_id,
    const std::string& token) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (consumed_tokens_.count(token)) {
    return DirectApiErrorCode::kApprovalTokenAlreadyUsed;
  }
  auto it = valid_tokens_.find(action_id);
  if (it != valid_tokens_.end()) {
    if (it->second.erase(token) > 0) {
      consumed_tokens_.insert(token);
      return DirectApiErrorCode::kOk;
    }
  }
  return DirectApiErrorCode::kInvalidApprovalToken;
}

// ---------------------------------------------------------------------------
// ScopeRegistry
// ---------------------------------------------------------------------------

bool ScopeRegistry::CheckScopes(
    const std::string& handle_or_account,
    DirectApiServiceKind service,
    const std::vector<std::string>& required,
    std::vector<std::string>* out_missing) const {
  std::unordered_set<std::string> granted =
      GetGrantedScopes(handle_or_account, service);
  std::vector<std::string> missing;
  for (const auto& req : required) {
    if (granted.find(req) == granted.end()) {
      missing.push_back(req);
    }
  }
  if (out_missing) {
    *out_missing = missing;
  }
  return missing.empty();
}

InMemoryScopeRegistry::InMemoryScopeRegistry() = default;
InMemoryScopeRegistry::~InMemoryScopeRegistry() = default;

std::unordered_set<std::string> InMemoryScopeRegistry::GetGrantedScopes(
    const std::string& handle_or_account,
    DirectApiServiceKind service) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = grants_.find({handle_or_account, service});
  if (it != grants_.end()) {
    return it->second;
  }
  return {};
}

void InMemoryScopeRegistry::GrantScopes(
    const std::string& handle_or_account,
    DirectApiServiceKind service,
    const std::vector<std::string>& scopes) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto& set = grants_[{handle_or_account, service}];
  for (const auto& s : scopes) {
    set.insert(s);
  }
}

// ---------------------------------------------------------------------------
// InMemoryDirectApiTransport
// ---------------------------------------------------------------------------

InMemoryDirectApiTransport::InMemoryDirectApiTransport() = default;
InMemoryDirectApiTransport::~InMemoryDirectApiTransport() = default;

void InMemoryDirectApiTransport::SetResponse(
    DirectApiServiceKind service,
    const std::string& operation_name,
    base::expected<TransportResponse, TransportError> response) {
  std::lock_guard<std::mutex> lock(mutex_);
  canned_responses_[{service, operation_name}] = std::move(response);
}

std::vector<TransportRequest> InMemoryDirectApiTransport::GetSentRequests() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return sent_requests_;
}

size_t InMemoryDirectApiTransport::SentCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return sent_requests_.size();
}

void InMemoryDirectApiTransport::Send(TransportRequest request,
                                      Callback callback) {
  base::expected<TransportResponse, TransportError> response_to_send;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    sent_requests_.push_back(request);
    auto it = canned_responses_.find({request.service, request.operation_name});
    if (it != canned_responses_.end()) {
      response_to_send = it->second;
    } else {
      TransportResponse default_ok;
      default_ok.status_code = 200;
      default_ok.body = "{\"status\":\"default_ok\",\"service\":\"" +
                        std::string(ServiceKindToString(request.service)) +
                        "\",\"op\":\"" + request.operation_name + "\"}";
      response_to_send = default_ok;
    }
  }
  // Invoke callback outside the mutex lock to prevent deadlocks with external observers
  std::move(callback).Run(std::move(response_to_send));
}

// ---------------------------------------------------------------------------
// ProfileCookieJarDirectApiTransport
// ---------------------------------------------------------------------------

ProfileCookieJarDirectApiTransport::ProfileCookieJarDirectApiTransport(
    Profile* profile)
    : profile_(profile) {}

ProfileCookieJarDirectApiTransport::~ProfileCookieJarDirectApiTransport() = default;

void ProfileCookieJarDirectApiTransport::Send(TransportRequest request,
                                              Callback callback) {
#if !defined(MAHO_STANDALONE_TEST)
  if (!profile_ || !maho::ai_security::IsProfileEligible(profile_)) {
    TransportError err;
    err.kind = TransportErrorKind::kNetworkError;
    err.message = "Profile ineligible or null for zero-tab direct API transport";
    std::move(callback).Run(base::unexpected(err));
    return;
  }

  GURL url(request.url);
  if (!url.is_valid() || maho::ai_security::IsUrlBlocked(url)) {
    TransportError err;
    err.kind = TransportErrorKind::kNetworkError;
    err.message = "Blocked or invalid direct API endpoint URL";
    std::move(callback).Run(base::unexpected(err));
    return;
  }

  auto resource_request = std::make_unique<network::ResourceRequest>();
  resource_request->url = url;
  resource_request->method = request.method;
  resource_request->credentials_mode = network::mojom::CredentialsMode::kInclude;
  for (const auto& [header_name, header_val] : request.headers) {
    resource_request->headers.SetHeader(header_name, header_val);
  }

  auto url_loader_factory = profile_->GetDefaultStoragePartition()
                                ->GetURLLoaderFactoryForBrowserProcess();
  if (!url_loader_factory) {
    TransportError err;
    err.kind = TransportErrorKind::kNetworkError;
    err.message = "No URLLoaderFactory available for browser process";
    std::move(callback).Run(base::unexpected(err));
    return;
  }

  net::NetworkTrafficAnnotationTag traffic_annotation =
      net::DefineNetworkTrafficAnnotation("maho_direct_api_broker", R"(
        semantics {
          sender: "Maho Authenticated Service API Broker"
          description:
            "Executes direct zero-tab API requests for authenticated web services using the profile cookie jar."
          trigger:
            "Agent tool dispatch requesting direct service API execution."
          data:
            "Structured API request payload with profile cookies / auth headers."
          destination: OTHER
        }
        policy {
          cookies_allowed: YES
          cookies_store: "user profile"
          setting: "This feature cannot be disabled in settings."
          policy_exception_justification: "Zero-tab direct service API capability."
        })");

  auto loader = network::SimpleURLLoader::Create(std::move(resource_request),
                                                 traffic_annotation);
  loader->SetAllowHttpErrorResults(true);

  if (!request.body.empty() && request.method != "GET") {
    loader->AttachStringForUpload(request.body, "application/json");
  }

  auto* loader_ptr = loader.get();
  constexpr size_t kMaxResponseDownloadBytes = 10 * 1024 * 1024; // 10MB limit
  loader_ptr->DownloadToString(
      url_loader_factory.get(),
      base::BindOnce(
          [](std::unique_ptr<network::SimpleURLLoader> kept_loader,
             Callback cb,
             std::optional<std::string> response_body) {
            if (!response_body) {
              TransportError err;
              err.kind = TransportErrorKind::kNetworkError;
              err.message = "Direct API request failed or exceeded size limit";
              std::move(cb).Run(base::unexpected(err));
              return;
            }
            int status_code = 200;
            if (kept_loader->ResponseInfo() &&
                kept_loader->ResponseInfo()->headers) {
              status_code =
                  kept_loader->ResponseInfo()->headers->response_code();
            }
            TransportResponse resp;
            resp.status_code = status_code;
            resp.body = *response_body;
            std::move(cb).Run(resp);
          },
          std::move(loader), std::move(callback)),
      kMaxResponseDownloadBytes);
#else
  (void)request;
  TransportResponse default_ok;
  default_ok.status_code = 200;
  default_ok.body = "{\"status\":\"standalone_ok\"}";
  std::move(callback).Run(default_ok);
#endif
}

// ---------------------------------------------------------------------------
// MahoAuthenticatedServiceApiBroker
// ---------------------------------------------------------------------------

MahoAuthenticatedServiceApiBroker::MahoAuthenticatedServiceApiBroker(
    Profile* profile)
    : profile_(profile),
      vault_(std::make_shared<InMemoryCredentialVault>()),
      approval_store_(std::make_shared<InMemoryApprovalTokenStore>()),
      scope_registry_(std::make_shared<InMemoryScopeRegistry>()),
      transport_(std::make_unique<ProfileCookieJarDirectApiTransport>(profile)) {}

MahoAuthenticatedServiceApiBroker::MahoAuthenticatedServiceApiBroker(
    Profile* profile,
    std::shared_ptr<CredentialVault> vault,
    std::shared_ptr<ApprovalTokenStore> approval_store,
    std::shared_ptr<ScopeRegistry> scope_registry,
    std::unique_ptr<DirectApiTransport> transport)
    : profile_(profile),
      vault_(std::move(vault)),
      approval_store_(std::move(approval_store)),
      scope_registry_(std::move(scope_registry)),
      transport_(std::move(transport)) {}

MahoAuthenticatedServiceApiBroker::~MahoAuthenticatedServiceApiBroker() = default;

// static
MahoAuthenticatedServiceApiBroker*
MahoAuthenticatedServiceApiBroker::GetForProfile(Profile* profile) {
  static MahoAuthenticatedServiceApiBroker* kDefaultBroker =
      new MahoAuthenticatedServiceApiBroker(nullptr);
  if (!profile) {
    return kDefaultBroker;
  }
  static std::unordered_map<Profile*, std::unique_ptr<MahoAuthenticatedServiceApiBroker>>*
      kProfileBrokers = new std::unordered_map<
          Profile*, std::unique_ptr<MahoAuthenticatedServiceApiBroker>>();
  auto it = kProfileBrokers->find(profile);
  if (it == kProfileBrokers->end()) {
    it = kProfileBrokers->emplace(
        profile,
        std::make_unique<MahoAuthenticatedServiceApiBroker>(profile)).first;
  }
  return it->second.get();
}

void MahoAuthenticatedServiceApiBroker::ExecuteOp(
    const DirectApiOpDescriptor& op,
    const std::optional<std::string>& approval_token,
    OutcomeCallback callback) {
  DirectApiExecutionContext default_ctx("default_session");
  ExecuteOp(op, default_ctx, approval_token, std::move(callback));
}

void MahoAuthenticatedServiceApiBroker::ExecuteOp(
    const DirectApiOpDescriptor& op,
    const DirectApiExecutionContext& context,
    const std::optional<std::string>& approval_token,
    OutcomeCallback callback) {
  // Step 1: Malformed input validation
  std::string trimmed_op_name = TrimWhitespace(op.operation_name);
  if (trimmed_op_name.empty()) {
    std::move(callback).Run(DirectApiExecutionOutcome::MakeTypedError(
        DirectApiErrorCode::kBadRequest,
        "Operation name cannot be empty",
        /*retryable=*/false));
    return;
  }

  if (op.service == DirectApiServiceKind::kUnknown) {
    std::move(callback).Run(DirectApiExecutionOutcome::MakeTypedError(
        DirectApiErrorCode::kBadRequest,
        "Invalid or unsupported service",
        /*retryable=*/false));
    return;
  }

  std::string action_id =
      std::string(ServiceKindToString(op.service)) + ":" + op.operation_name;

  // Step 2: Confirmation check (Fail-closed)
  bool requires_confirmation = !op.read_only;
  if (requires_confirmation) {
    if (!approval_token.has_value() || approval_token->empty()) {
      approval_store_->IssueToken(action_id);
      std::move(callback).Run(DirectApiExecutionOutcome::MakeNeedsConfirmation(
          action_id,
          "Action " + op.operation_name + " on service " +
              std::string(ServiceKindToString(op.service)) +
              " requires explicit user confirmation",
          op.required_scopes));
      return;
    }

    DirectApiErrorCode consume_status =
        approval_store_->ConsumeToken(action_id, *approval_token);
    if (consume_status != DirectApiErrorCode::kOk) {
      std::move(callback).Run(DirectApiExecutionOutcome::MakeTypedError(
          consume_status,
          "Confirmation verification failed: " +
              std::string(ErrorCodeToString(consume_status)),
          /*retryable=*/false));
      return;
    }
  }

  // Step 3: Auth target resolution and Scope enforcement
  std::string auth_target = !context.opaque_auth_handle.empty()
                                ? context.opaque_auth_handle
                                : context.account_id;
  auth_target = TrimWhitespace(auth_target);
  if (auth_target.empty()) {
    std::move(callback).Run(DirectApiExecutionOutcome::MakeTypedError(
        DirectApiErrorCode::kMissingCredentials,
        "No authentication handle or account ID provided in execution context",
        /*retryable=*/false));
    return;
  }

  if (!op.required_scopes.empty()) {
    std::vector<std::string> missing_scopes;
    if (!scope_registry_->CheckScopes(auth_target, op.service,
                                     op.required_scopes, &missing_scopes)) {
      std::move(callback).Run(DirectApiExecutionOutcome::MakeForbidden(
          "Scope enforcement failed for service " +
              std::string(ServiceKindToString(op.service)),
          missing_scopes));
      return;
    }
  }

  // Step 4: Vault credential resolution & header injection
  auto cred = vault_->GetCredential(auth_target);
  if (!cred.has_value()) {
    std::move(callback).Run(DirectApiExecutionOutcome::MakeTypedError(
        DirectApiErrorCode::kInvalidAuthHandle,
        "Credential not found for handle '" + auth_target + "'",
        /*retryable=*/false));
    return;
  }

  TransportRequest req;
  req.service = op.service;
  req.operation_name = op.operation_name;
  req.method = !op.http_method.empty() ? op.http_method
                                       : (op.read_only ? "GET" : "POST");
  req.url = op.endpoint_url;
  req.body = op.parameters_json;

  // Header injection happens Op-specifically at transport boundary only.
  // Secrets and tokens are NEVER inlined into outcome structs or returned to model.
  req.headers["Authorization"] = cred->FormatAuthHeader();
  req.headers["Content-Type"] = "application/json";

  // Step 5: Transport execution and outcome mapping
  DirectApiServiceKind service = op.service;
  std::string operation_name = op.operation_name;

  transport_->Send(
      std::move(req),
      base::BindOnce(
          [](DirectApiServiceKind service, std::string operation_name,
             OutcomeCallback cb,
             base::expected<TransportResponse, TransportError> result) {
            if (!result.has_value()) {
              const auto& err = result.error();
              if (err.kind == TransportErrorKind::kCancelled) {
                std::move(cb).Run(DirectApiExecutionOutcome::MakeCancelled(
                    "Direct API operation was cancelled mid-flight"));
                return;
              }
              if (err.kind == TransportErrorKind::kTimeout) {
                std::move(cb).Run(DirectApiExecutionOutcome::MakeTypedError(
                    DirectApiErrorCode::kNetworkError,
                    "Direct API transport request timed out",
                    /*retryable=*/true));
                return;
              }
              std::string redacted_err_msg =
                  maho::credential_redaction::RedactJsonOrText(err.message);
              std::move(cb).Run(DirectApiExecutionOutcome::MakeTypedError(
                  DirectApiErrorCode::kNetworkError,
                  "Network error: " + redacted_err_msg,
                  /*retryable=*/true));
              return;
            }

            const auto& resp = result.value();
            constexpr size_t kMaxResponseBytes = 10 * 1024 * 1024; // 10MB limit
            if (resp.body.size() > kMaxResponseBytes) {
              std::move(cb).Run(DirectApiExecutionOutcome::MakeTypedError(
                  DirectApiErrorCode::kInternalError,
                  "Direct API response payload too large",
                  /*retryable=*/false));
              return;
            }

            if (resp.status_code >= 200 && resp.status_code <= 299) {
              std::string redacted_body =
                  maho::credential_redaction::RedactJsonOrText(resp.body);
              std::move(cb).Run(DirectApiExecutionOutcome::MakeOk(
                  service, operation_name, std::move(redacted_body)));
              return;
            }

            if (resp.status_code == 401 || resp.status_code == 403) {
              std::move(cb).Run(DirectApiExecutionOutcome::MakeForbidden(
                  "Upstream service rejected authorization (" +
                      std::to_string(resp.status_code) + ")",
                  {}));
              return;
            }

            if (resp.status_code == 429) {
              std::move(cb).Run(DirectApiExecutionOutcome::MakeTypedError(
                  DirectApiErrorCode::kRateLimited,
                  "Rate limit exceeded",
                  /*retryable=*/true));
              return;
            }

            if (resp.status_code == 400) {
              std::move(cb).Run(DirectApiExecutionOutcome::MakeTypedError(
                  DirectApiErrorCode::kBadRequest,
                  SanitizeErrorPayload(resp.body),
                  /*retryable=*/false));
              return;
            }

            if (resp.status_code == 404) {
              std::move(cb).Run(DirectApiExecutionOutcome::MakeTypedError(
                  DirectApiErrorCode::kResourceNotFound,
                  SanitizeErrorPayload(resp.body),
                  /*retryable=*/false));
              return;
            }

            if (resp.status_code >= 500 && resp.status_code <= 599) {
              std::move(cb).Run(DirectApiExecutionOutcome::MakeTypedError(
                  DirectApiErrorCode::kServiceUnavailable,
                  "Service unavailable: " + std::to_string(resp.status_code),
                  /*retryable=*/true));
              return;
            }

            std::move(cb).Run(DirectApiExecutionOutcome::MakeTypedError(
                DirectApiErrorCode::kInternalError,
                "Unexpected response status: " + std::to_string(resp.status_code),
                /*retryable=*/false));
          },
          service, std::move(operation_name), std::move(callback)));
}

std::string MahoAuthenticatedServiceApiBroker::SanitizeErrorPayload(
    const std::string& body) {
  if (body.find("<html") != std::string::npos ||
      body.find("<HTML") != std::string::npos ||
      body.find("<!DOCTYPE") != std::string::npos ||
      body.find("<!doctype") != std::string::npos) {
    return "HTML error response from upstream service";
  }

  // Quick JSON message / error extraction if present
  auto find_json_field = [&](const std::string& field) -> std::optional<std::string> {
    std::string pattern = "\"" + field + "\":\"";
    size_t pos = body.find(pattern);
    if (pos != std::string::npos) {
      size_t start = pos + pattern.size();
      size_t end = body.find('\"', start);
      if (end != std::string::npos) {
        return body.substr(start, end - start);
      }
    }
    return std::nullopt;
  };

  if (auto msg = find_json_field("message")) {
    return maho::credential_redaction::RedactJsonOrText(*msg);
  }
  if (auto err = find_json_field("error")) {
    return maho::credential_redaction::RedactJsonOrText(*err);
  }

  return maho::credential_redaction::RedactJsonOrText(body);
}

std::vector<DirectApiOpDescriptor>
MahoAuthenticatedServiceApiBroker::GetSupportedOperations() {
  return {
      // Gmail
      DirectApiOpDescriptor(DirectApiServiceKind::kGmail, "messages.list",
                            "{}", true, {"gmail.readonly"}),
      DirectApiOpDescriptor(DirectApiServiceKind::kGmail, "messages.get",
                            "{}", true, {"gmail.readonly"}),
      DirectApiOpDescriptor(DirectApiServiceKind::kGmail, "messages.send",
                            "{}", false, {"gmail.send"}),

      // Google Calendar
      DirectApiOpDescriptor(DirectApiServiceKind::kGoogleCalendar, "events.list",
                            "{}", true, {"calendar.readonly"}),
      DirectApiOpDescriptor(DirectApiServiceKind::kGoogleCalendar, "events.create",
                            "{}", false, {"calendar.events"}),

      // Google Sheets
      DirectApiOpDescriptor(DirectApiServiceKind::kGoogleSheets,
                            "spreadsheets.values.get", "{}", true,
                            {"sheets.readonly"}),
      DirectApiOpDescriptor(DirectApiServiceKind::kGoogleSheets,
                            "spreadsheets.values.update", "{}", false,
                            {"sheets.spreadsheets"}),

      // Google Drive
      DirectApiOpDescriptor(DirectApiServiceKind::kGoogleDrive, "files.list",
                            "{}", true, {"drive.readonly"}),
      DirectApiOpDescriptor(DirectApiServiceKind::kGoogleDrive, "files.get",
                            "{}", true, {"drive.readonly"}),

      // Slack
      DirectApiOpDescriptor(DirectApiServiceKind::kSlack, "conversations.history",
                            "{}", true, {"channels:history"}),
      DirectApiOpDescriptor(DirectApiServiceKind::kSlack, "chat.postMessage",
                            "{}", false, {"chat:write"}),

      // Discord
      DirectApiOpDescriptor(DirectApiServiceKind::kDiscord,
                            "channels.messages.list", "{}", true,
                            {"messages.read"}),
      DirectApiOpDescriptor(DirectApiServiceKind::kDiscord,
                            "channels.messages.create", "{}", false,
                            {"messages.write"}),

      // Telegram
      DirectApiOpDescriptor(DirectApiServiceKind::kTelegram, "getUpdates",
                            "{}", true, {"bot"}),
      DirectApiOpDescriptor(DirectApiServiceKind::kTelegram, "sendMessage",
                            "{}", false, {"bot"}),
  };
}

bool MahoAuthenticatedServiceApiBroker::IsOperationSupported(
    DirectApiServiceKind service,
    std::string_view op_name) {
  for (const auto& op : GetSupportedOperations()) {
    if (op.service == service && op.operation_name == op_name) {
      return true;
    }
  }
  return false;
}

}  // namespace maho::ai
