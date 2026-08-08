// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_ai/maho_ai_runtime_event_persistence.h"

#include <utility>

#include "maho/browser/ai/maho_ai_security_utils.h"
#include "maho/browser/ai/maho_credential_redaction.h"

namespace maho::ai {
namespace {

constexpr char kSafeCredentialFailureText[] =
    "Your saved AI credential could not be used.";

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

base::DictValue SerializeRuntimeEventForPersistence(
    const maho_ai::mojom::RuntimeEvent& event) {
  base::DictValue dict;
  dict.Set("kind", static_cast<int>(event.kind));
  dict.Set("session_id", event.session_id);
  if (event.request_id.has_value()) {
    dict.Set("request_id", *event.request_id);
  }
  dict.Set("sequence", static_cast<double>(event.sequence));
  dict.Set("timestamp", event.timestamp);
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
