// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ai/maho_authenticated_service_api_broker.h"

#include <cassert>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#ifndef MAHO_STANDALONE_TEST
#include "testing/gtest/include/gtest/gtest.h"

namespace maho::ai {

// Test 1: Credential resolution & transport header injection probe
TEST(MahoAuthenticatedServiceApiBrokerTest, CredentialResolutionAndRedactionProbe) {
  auto vault = std::make_shared<InMemoryCredentialVault>();
  auto approval_store = std::make_shared<InMemoryApprovalTokenStore>();
  auto scope_registry = std::make_shared<InMemoryScopeRegistry>();
  auto transport = std::make_unique<InMemoryDirectApiTransport>();
  auto* transport_ptr = transport.get();

  const std::string raw_secret = "secret-token-super-private-987654321";
  vault->StoreCredential("gmail-handle-1", VaultCredential::Bearer(raw_secret));
  scope_registry->GrantScopes("gmail-handle-1", DirectApiServiceKind::kGmail,
                              {"gmail.readonly"});

  TransportResponse canned_resp;
  canned_resp.status_code = 200;
  canned_resp.body = "{\"messages\":[\"msg_1\",\"msg_2\"]}";
  transport_ptr->SetResponse(DirectApiServiceKind::kGmail, "messages.list",
                             canned_resp);

  MahoAuthenticatedServiceApiBroker broker(
      nullptr, vault, approval_store, scope_registry, std::move(transport));

  DirectApiOpDescriptor op(DirectApiServiceKind::kGmail, "messages.list",
                           "{\"q\":\"label:unread\"}", /*read_only=*/true,
                           {"gmail.readonly"});

  // Model-facing representation assertion: strictly no secret token
  ModelFacingDirectApiRequest model_req = ModelFacingDirectApiRequest::FromOp(op);
  std::string model_json = model_req.ToJson();
  EXPECT_EQ(model_json.find(raw_secret), std::string::npos);
  EXPECT_NE(model_json.find("gmail"), std::string::npos);
  EXPECT_NE(model_json.find("messages.list"), std::string::npos);
  EXPECT_NE(model_json.find("gmail.readonly"), std::string::npos);

  DirectApiExecutionContext ctx("session-1");
  ctx.opaque_auth_handle = "gmail-handle-1";

  std::string ctx_json = ctx.ToJson();
  EXPECT_EQ(ctx_json.find(raw_secret), std::string::npos);
  EXPECT_NE(ctx_json.find("gmail-handle-1"), std::string::npos);

  std::string ctx_debug = ctx.ToDebugString();
  EXPECT_EQ(ctx_debug.find(raw_secret), std::string::npos);

  // Vault credential debug representation must redact
  auto stored_cred = vault->GetCredential("gmail-handle-1");
  ASSERT_TRUE(stored_cred.has_value());
  EXPECT_EQ(stored_cred->ToDebugString().find(raw_secret), std::string::npos);
  EXPECT_NE(stored_cred->ToDebugString().find("[REDACTED]"), std::string::npos);

  // Execute op
  DirectApiExecutionOutcome outcome_result;
  broker.ExecuteOp(
      op, ctx, std::nullopt,
      base::BindOnce(
          [](DirectApiExecutionOutcome* out, DirectApiExecutionOutcome outcome) {
            *out = std::move(outcome);
          },
          &outcome_result));

  EXPECT_EQ(outcome_result.status, DirectApiOutcomeStatus::kOk);
  EXPECT_EQ(outcome_result.service, DirectApiServiceKind::kGmail);
  EXPECT_EQ(outcome_result.operation_name, "messages.list");
  EXPECT_EQ(outcome_result.payload_json.find(raw_secret), std::string::npos);
  EXPECT_NE(outcome_result.payload_json.find("msg_1"), std::string::npos);

  // Outcome json serialization probe
  std::string outcome_json = outcome_result.ToJson();
  EXPECT_EQ(outcome_json.find(raw_secret), std::string::npos);

  // Invariant: Secret was injected exclusively at transport layer
  EXPECT_EQ(transport_ptr->SentCount(), 1u);
  auto sent_reqs = transport_ptr->GetSentRequests();
  ASSERT_EQ(sent_reqs.size(), 1u);
  auto it = sent_reqs[0].headers.find("Authorization");
  ASSERT_NE(it, sent_reqs[0].headers.end());
  EXPECT_EQ(it->second, "Bearer " + raw_secret);
}

// Test 2: Approval gate fail-closed and single-use token enforcement
TEST(MahoAuthenticatedServiceApiBrokerTest, ApprovalGateFailClosedAndSingleUseToken) {
  auto vault = std::make_shared<InMemoryCredentialVault>();
  auto approval_store = std::make_shared<InMemoryApprovalTokenStore>();
  auto scope_registry = std::make_shared<InMemoryScopeRegistry>();
  auto transport = std::make_unique<InMemoryDirectApiTransport>();
  auto* transport_ptr = transport.get();

  vault->StoreCredential("slack-handle", VaultCredential::Bearer("slack-tok"));
  scope_registry->GrantScopes("slack-handle", DirectApiServiceKind::kSlack,
                              {"chat:write"});

  TransportResponse ok_resp;
  ok_resp.status_code = 200;
  ok_resp.body = "{\"ok\":true,\"ts\":\"12345\"}";
  transport_ptr->SetResponse(DirectApiServiceKind::kSlack, "chat.postMessage",
                             ok_resp);

  MahoAuthenticatedServiceApiBroker broker(
      nullptr, vault, approval_store, scope_registry, std::move(transport));

  DirectApiOpDescriptor mut_op(
      DirectApiServiceKind::kSlack, "chat.postMessage",
      "{\"channel\":\"C123\",\"text\":\"Hello\"}", /*read_only=*/false,
      {"chat:write"});

  DirectApiExecutionContext ctx("session-1");
  ctx.opaque_auth_handle = "slack-handle";

  // 1. Invocations without approval token -> NeedsConfirmation (Fail-closed)
  DirectApiExecutionOutcome outcome1;
  broker.ExecuteOp(
      mut_op, ctx, std::nullopt,
      base::BindOnce(
          [](DirectApiExecutionOutcome* out, DirectApiExecutionOutcome outcome) {
            *out = std::move(outcome);
          },
          &outcome1));

  EXPECT_EQ(outcome1.status, DirectApiOutcomeStatus::kNeedsConfirmation);
  EXPECT_EQ(outcome1.action_id, "slack:chat.postMessage");
  EXPECT_TRUE(outcome1.is_policy_denial);
  // Zero network dispatch
  EXPECT_EQ(transport_ptr->SentCount(), 0u);

  // 2. Issue approval token for this action_id
  std::string token = approval_store->IssueToken(outcome1.action_id);
  EXPECT_FALSE(token.empty());

  // 3. First execution with approval token -> Succeeds
  DirectApiExecutionOutcome outcome2;
  broker.ExecuteOp(
      mut_op, ctx, token,
      base::BindOnce(
          [](DirectApiExecutionOutcome* out, DirectApiExecutionOutcome outcome) {
            *out = std::move(outcome);
          },
          &outcome2));

  EXPECT_EQ(outcome2.status, DirectApiOutcomeStatus::kOk);
  EXPECT_EQ(transport_ptr->SentCount(), 1u);

  // 4. Second execution with the SAME token -> REJECTED (single-use invariant)
  DirectApiExecutionOutcome outcome3;
  broker.ExecuteOp(
      mut_op, ctx, token,
      base::BindOnce(
          [](DirectApiExecutionOutcome* out, DirectApiExecutionOutcome outcome) {
            *out = std::move(outcome);
          },
          &outcome3));

  EXPECT_EQ(outcome3.status, DirectApiOutcomeStatus::kTypedError);
  EXPECT_EQ(outcome3.error_code,
            DirectApiErrorCode::kApprovalTokenAlreadyUsed);
  EXPECT_FALSE(outcome3.retryable);
  EXPECT_TRUE(outcome3.is_policy_denial);

  // Invariant: ZERO second network dispatch
  EXPECT_EQ(transport_ptr->SentCount(), 1u);
}

// Test 3: Scope enforcement fails closed before network dispatch
TEST(MahoAuthenticatedServiceApiBrokerTest, ScopeEnforcementFailsClosedPreNetwork) {
  auto vault = std::make_shared<InMemoryCredentialVault>();
  auto approval_store = std::make_shared<InMemoryApprovalTokenStore>();
  auto scope_registry = std::make_shared<InMemoryScopeRegistry>();
  auto transport = std::make_unique<InMemoryDirectApiTransport>();
  auto* transport_ptr = transport.get();

  vault->StoreCredential("acc-1", VaultCredential::Bearer("tok-acc-1"));
  scope_registry->GrantScopes("acc-1", DirectApiServiceKind::kGmail,
                              {"gmail.readonly"});

  MahoAuthenticatedServiceApiBroker broker(
      nullptr, vault, approval_store, scope_registry, std::move(transport));

  // Op requests gmail.send in addition to gmail.readonly
  DirectApiOpDescriptor op(
      DirectApiServiceKind::kGmail, "messages.send",
      "{\"to\":\"user@example.com\"}", /*read_only=*/true,
      {"gmail.send", "gmail.readonly"});

  DirectApiExecutionContext ctx("session-1");
  ctx.opaque_auth_handle = "acc-1";

  DirectApiExecutionOutcome outcome;
  broker.ExecuteOp(
      op, ctx, std::nullopt,
      base::BindOnce(
          [](DirectApiExecutionOutcome* out, DirectApiExecutionOutcome outcome) {
            *out = std::move(outcome);
          },
          &outcome));

  EXPECT_EQ(outcome.status, DirectApiOutcomeStatus::kForbidden);
  ASSERT_EQ(outcome.missing_scopes.size(), 1u);
  EXPECT_EQ(outcome.missing_scopes[0], "gmail.send");
  EXPECT_TRUE(outcome.is_policy_denial);

  // Invariant: ZERO network dispatch
  EXPECT_EQ(transport_ptr->SentCount(), 0u);
}

// Test 4: Typed outcomes across services and error mapping with HTML sanitization
TEST(MahoAuthenticatedServiceApiBrokerTest, TypedOutcomesAndHtmlSanitization) {
  auto vault = std::make_shared<InMemoryCredentialVault>();
  auto approval_store = std::make_shared<InMemoryApprovalTokenStore>();
  auto scope_registry = std::make_shared<InMemoryScopeRegistry>();
  auto transport = std::make_unique<InMemoryDirectApiTransport>();
  auto* transport_ptr = transport.get();

  std::vector<std::pair<DirectApiServiceKind, std::string>> services = {
      {DirectApiServiceKind::kGmail, "users.messages.get"},
      {DirectApiServiceKind::kGoogleCalendar, "events.list"},
      {DirectApiServiceKind::kGoogleSheets, "spreadsheets.values.get"},
      {DirectApiServiceKind::kGoogleDrive, "files.list"},
      {DirectApiServiceKind::kSlack, "conversations.history"},
      {DirectApiServiceKind::kDiscord, "channels.messages.list"},
      {DirectApiServiceKind::kTelegram, "getUpdates"},
  };

  for (const auto& [service, op_name] : services) {
    std::string handle = std::string(ServiceKindToString(service)) + "-handle";
    vault->StoreCredential(handle, VaultCredential::Bearer("test-tok"));
    TransportResponse ok_resp;
    ok_resp.status_code = 200;
    ok_resp.body = "{\"service\":\"" +
                   std::string(ServiceKindToString(service)) +
                   "\",\"status\":\"ok\"}";
    transport_ptr->SetResponse(service, op_name, ok_resp);

    MahoAuthenticatedServiceApiBroker broker(
        nullptr, vault, approval_store, scope_registry,
        std::make_unique<InMemoryDirectApiTransport>());

    // Use fresh transport for clean assertion
    auto sub_transport = std::make_unique<InMemoryDirectApiTransport>();
    sub_transport->SetResponse(service, op_name, ok_resp);
    MahoAuthenticatedServiceApiBroker sub_broker(
        nullptr, vault, approval_store, scope_registry, std::move(sub_transport));

    DirectApiOpDescriptor op(service, op_name, "{}", true, {});
    DirectApiExecutionContext ctx("s");
    ctx.opaque_auth_handle = handle;

    DirectApiExecutionOutcome res;
    sub_broker.ExecuteOp(
        op, ctx, std::nullopt,
        base::BindOnce(
            [](DirectApiExecutionOutcome* out, DirectApiExecutionOutcome outcome) {
              *out = std::move(outcome);
            },
            &res));

    EXPECT_EQ(res.status, DirectApiOutcomeStatus::kOk);
    EXPECT_EQ(res.service, service);
    EXPECT_EQ(res.operation_name, op_name);
  }

  // Error responses testing
  MahoAuthenticatedServiceApiBroker broker(
      nullptr, vault, approval_store, scope_registry, std::move(transport));

  // 403 Forbidden
  {
    TransportResponse forbidden_resp;
    forbidden_resp.status_code = 403;
    forbidden_resp.body = "{\"error\":\"Unauthorized\"}";
    transport_ptr->SetResponse(DirectApiServiceKind::kGmail, "forbidden_op",
                               forbidden_resp);

    DirectApiOpDescriptor op(DirectApiServiceKind::kGmail, "forbidden_op", "{}", true, {});
    DirectApiExecutionContext ctx("s");
    ctx.opaque_auth_handle = "gmail-handle";

    DirectApiExecutionOutcome outcome;
    broker.ExecuteOp(
        op, ctx, std::nullopt,
        base::BindOnce(
            [](DirectApiExecutionOutcome* out, DirectApiExecutionOutcome o) {
              *out = std::move(o);
            },
            &outcome));

    EXPECT_EQ(outcome.status, DirectApiOutcomeStatus::kForbidden);
    EXPECT_TRUE(outcome.is_policy_denial);
  }

  // 429 RateLimited
  {
    TransportResponse rate_resp;
    rate_resp.status_code = 429;
    rate_resp.body = "{\"error\":\"Too Many Requests\"}";
    transport_ptr->SetResponse(DirectApiServiceKind::kGoogleCalendar,
                               "rate_limited_op", rate_resp);

    DirectApiOpDescriptor op(DirectApiServiceKind::kGoogleCalendar,
                             "rate_limited_op", "{}", true, {});
    DirectApiExecutionContext ctx("s");
    ctx.opaque_auth_handle = "google_calendar-handle";

    DirectApiExecutionOutcome outcome;
    broker.ExecuteOp(
        op, ctx, std::nullopt,
        base::BindOnce(
            [](DirectApiExecutionOutcome* out, DirectApiExecutionOutcome o) {
              *out = std::move(o);
            },
            &outcome));

    EXPECT_EQ(outcome.status, DirectApiOutcomeStatus::kTypedError);
    EXPECT_EQ(outcome.error_code, DirectApiErrorCode::kRateLimited);
    EXPECT_TRUE(outcome.retryable);

    // Fallback surface conversion
    auto hl = outcome.ToHighLevelOutcome();
    EXPECT_EQ(hl.type, HighLevelDirectApiOutcome::Type::kTypedUnavailable);
    EXPECT_TRUE(hl.can_fallback_to_tabs);
  }

  // Raw HTML dump sanitization
  {
    TransportResponse html_resp;
    html_resp.status_code = 400;
    html_resp.body =
        "<!DOCTYPE html><html><body><h1>502 Bad Gateway</h1></body></html>";
    transport_ptr->SetResponse(DirectApiServiceKind::kGoogleSheets,
                               "bad_gateway", html_resp);

    DirectApiOpDescriptor op(DirectApiServiceKind::kGoogleSheets,
                             "bad_gateway", "{}", true, {});
    DirectApiExecutionContext ctx("s");
    ctx.opaque_auth_handle = "google_sheets-handle";

    DirectApiExecutionOutcome outcome;
    broker.ExecuteOp(
        op, ctx, std::nullopt,
        base::BindOnce(
            [](DirectApiExecutionOutcome* out, DirectApiExecutionOutcome o) {
              *out = std::move(o);
            },
            &outcome));

    EXPECT_EQ(outcome.status, DirectApiOutcomeStatus::kTypedError);
    EXPECT_EQ(outcome.error_code, DirectApiErrorCode::kBadRequest);
    EXPECT_EQ(outcome.error_message.find("<!DOCTYPE"), std::string::npos);
    EXPECT_EQ(outcome.error_message,
              "HTML error response from upstream service");
  }
}

// Test 5: Cancel mid-flight and fallback surface rules
TEST(MahoAuthenticatedServiceApiBrokerTest, CancelMidFlightAndFallbackSurface) {
  auto vault = std::make_shared<InMemoryCredentialVault>();
  auto approval_store = std::make_shared<InMemoryApprovalTokenStore>();
  auto scope_registry = std::make_shared<InMemoryScopeRegistry>();
  auto transport = std::make_unique<InMemoryDirectApiTransport>();
  auto* transport_ptr = transport.get();

  vault->StoreCredential("tg-handle", VaultCredential::Bearer("tg-tok"));
  scope_registry->GrantScopes("tg-handle", DirectApiServiceKind::kTelegram,
                              {"bot"});

  TransportError cancel_err;
  cancel_err.kind = TransportErrorKind::kCancelled;
  cancel_err.message = "Cancelled by user";
  transport_ptr->SetResponse(DirectApiServiceKind::kTelegram, "sendMessage",
                             base::unexpected(cancel_err));

  std::string token = approval_store->IssueToken("telegram:sendMessage");

  MahoAuthenticatedServiceApiBroker broker(
      nullptr, vault, approval_store, scope_registry, std::move(transport));

  DirectApiOpDescriptor op(DirectApiServiceKind::kTelegram, "sendMessage",
                           "{\"chat_id\":123,\"text\":\"hi\"}",
                           /*read_only=*/false, {"bot"});

  DirectApiExecutionContext ctx("s");
  ctx.opaque_auth_handle = "tg-handle";

  DirectApiExecutionOutcome outcome;
  broker.ExecuteOp(
      op, ctx, token,
      base::BindOnce(
          [](DirectApiExecutionOutcome* out, DirectApiExecutionOutcome o) {
            *out = std::move(o);
          },
          &outcome));

  EXPECT_EQ(outcome.status, DirectApiOutcomeStatus::kCancelled);
  EXPECT_FALSE(outcome.is_policy_denial);

  auto hl = outcome.ToHighLevelOutcome();
  EXPECT_EQ(hl.type, HighLevelDirectApiOutcome::Type::kHardFailure);
  EXPECT_EQ(hl.error_code, "CANCELLED");
  EXPECT_FALSE(hl.can_fallback_to_tabs);
}

// Test 6: Capability-backed supported operation exposure
TEST(MahoAuthenticatedServiceApiBrokerTest, WireDescriptorExposure) {
  auto ops = MahoAuthenticatedServiceApiBroker::GetSupportedOperations();
  EXPECT_FALSE(ops.empty());

  EXPECT_TRUE(MahoAuthenticatedServiceApiBroker::IsOperationSupported(
      DirectApiServiceKind::kGmail, "messages.list"));
  EXPECT_TRUE(MahoAuthenticatedServiceApiBroker::IsOperationSupported(
      DirectApiServiceKind::kSlack, "chat.postMessage"));
  EXPECT_FALSE(MahoAuthenticatedServiceApiBroker::IsOperationSupported(
      DirectApiServiceKind::kGmail, "non_existent_op"));
  EXPECT_FALSE(MahoAuthenticatedServiceApiBroker::IsOperationSupported(
      DirectApiServiceKind::kUnknown, "anything"));
}

// Test 7: Malformed input handling
TEST(MahoAuthenticatedServiceApiBrokerTest, AdversarialMalformedInputHandling) {
  auto vault = std::make_shared<InMemoryCredentialVault>();
  auto approval_store = std::make_shared<InMemoryApprovalTokenStore>();
  auto scope_registry = std::make_shared<InMemoryScopeRegistry>();
  auto transport = std::make_unique<InMemoryDirectApiTransport>();

  MahoAuthenticatedServiceApiBroker broker(
      nullptr, vault, approval_store, scope_registry, std::move(transport));

  // 1. Empty operation name
  {
    DirectApiOpDescriptor empty_op(DirectApiServiceKind::kGmail, "   ", "{}",
                                   true, {});
    DirectApiExecutionContext ctx("s");
    ctx.opaque_auth_handle = "h1";

    DirectApiExecutionOutcome out;
    broker.ExecuteOp(
        empty_op, ctx, std::nullopt,
        base::BindOnce(
            [](DirectApiExecutionOutcome* o, DirectApiExecutionOutcome res) {
              *o = std::move(res);
            },
            &out));

    EXPECT_EQ(out.status, DirectApiOutcomeStatus::kTypedError);
    EXPECT_EQ(out.error_code, DirectApiErrorCode::kBadRequest);
  }

  // 2. Missing credentials / no handle
  {
    DirectApiOpDescriptor op(DirectApiServiceKind::kGmail, "list", "{}", true,
                             {});
    DirectApiExecutionContext ctx("s");

    DirectApiExecutionOutcome out;
    broker.ExecuteOp(
        op, ctx, std::nullopt,
        base::BindOnce(
            [](DirectApiExecutionOutcome* o, DirectApiExecutionOutcome res) {
              *o = std::move(res);
            },
            &out));

    EXPECT_EQ(out.status, DirectApiOutcomeStatus::kTypedError);
    EXPECT_EQ(out.error_code, DirectApiErrorCode::kMissingCredentials);
  }

  // 3. Invalid auth handle (not in vault)
  {
    DirectApiOpDescriptor op(DirectApiServiceKind::kGmail, "list", "{}", true,
                             {});
    DirectApiExecutionContext ctx("s");
    ctx.opaque_auth_handle = "non-existent";

    DirectApiExecutionOutcome out;
    broker.ExecuteOp(
        op, ctx, std::nullopt,
        base::BindOnce(
            [](DirectApiExecutionOutcome* o, DirectApiExecutionOutcome res) {
              *o = std::move(res);
            },
            &out));

    EXPECT_EQ(out.status, DirectApiOutcomeStatus::kTypedError);
    EXPECT_EQ(out.error_code, DirectApiErrorCode::kInvalidAuthHandle);
  }
}

}  // namespace maho::ai

#else  // MAHO_STANDALONE_TEST

int main() {
  using namespace maho::ai;
  std::cout << "[RUN] MahoAuthenticatedServiceApiBroker standalone tests..."
            << std::endl;

  // Test 1: Credential resolution & redaction probe
  {
    auto vault = std::make_shared<InMemoryCredentialVault>();
    auto approval_store = std::make_shared<InMemoryApprovalTokenStore>();
    auto scope_registry = std::make_shared<InMemoryScopeRegistry>();
    auto transport = std::make_unique<InMemoryDirectApiTransport>();
    auto* transport_ptr = transport.get();

    const std::string raw_secret = "secret-token-super-private-987654321";
    vault->StoreCredential("gmail-handle-1",
                           VaultCredential::Bearer(raw_secret));
    scope_registry->GrantScopes("gmail-handle-1", DirectApiServiceKind::kGmail,
                                {"gmail.readonly"});

    TransportResponse canned_resp;
    canned_resp.status_code = 200;
    canned_resp.body = "{\"messages\":[\"msg_1\",\"msg_2\"]}";
    transport_ptr->SetResponse(DirectApiServiceKind::kGmail, "messages.list",
                               canned_resp);

    MahoAuthenticatedServiceApiBroker broker(
        nullptr, vault, approval_store, scope_registry, std::move(transport));

    DirectApiOpDescriptor op(DirectApiServiceKind::kGmail, "messages.list",
                             "{\"q\":\"label:unread\"}", /*read_only=*/true,
                             {"gmail.readonly"});

    ModelFacingDirectApiRequest model_req =
        ModelFacingDirectApiRequest::FromOp(op);
    std::string model_json = model_req.ToJson();
    assert(model_json.find(raw_secret) == std::string::npos);
    assert(model_json.find("gmail") != std::string::npos);
    assert(model_json.find("messages.list") != std::string::npos);

    DirectApiExecutionContext ctx("session-1");
    ctx.opaque_auth_handle = "gmail-handle-1";
    assert(ctx.ToJson().find(raw_secret) == std::string::npos);

    auto cred = vault->GetCredential("gmail-handle-1");
    assert(cred.has_value());
    assert(cred->ToDebugString().find(raw_secret) == std::string::npos);
    assert(cred->ToDebugString().find("[REDACTED]") != std::string::npos);

    DirectApiExecutionOutcome outcome_result;
    broker.ExecuteOp(
        op, ctx, std::nullopt,
        base::BindOnce(
            [](DirectApiExecutionOutcome* out,
               DirectApiExecutionOutcome outcome) {
              *out = std::move(outcome);
            },
            &outcome_result));

    assert(outcome_result.status == DirectApiOutcomeStatus::kOk);
    assert(outcome_result.service == DirectApiServiceKind::kGmail);
    assert(outcome_result.operation_name == "messages.list");
    assert(outcome_result.payload_json.find(raw_secret) == std::string::npos);
    assert(outcome_result.payload_json.find("msg_1") != std::string::npos);
    assert(outcome_result.ToJson().find(raw_secret) == std::string::npos);

    assert(transport_ptr->SentCount() == 1);
    auto sent = transport_ptr->GetSentRequests();
    assert(sent[0].headers.at("Authorization") == "Bearer " + raw_secret);
  }

  // Test 2: Approval gate fail closed & single use
  {
    auto vault = std::make_shared<InMemoryCredentialVault>();
    auto approval_store = std::make_shared<InMemoryApprovalTokenStore>();
    auto scope_registry = std::make_shared<InMemoryScopeRegistry>();
    auto transport = std::make_unique<InMemoryDirectApiTransport>();
    auto* transport_ptr = transport.get();

    vault->StoreCredential("slack-handle", VaultCredential::Bearer("tok"));
    scope_registry->GrantScopes("slack-handle", DirectApiServiceKind::kSlack,
                                {"chat:write"});

    TransportResponse ok_resp;
    ok_resp.status_code = 200;
    ok_resp.body = "{\"ok\":true}";
    transport_ptr->SetResponse(DirectApiServiceKind::kSlack, "chat.postMessage",
                               ok_resp);

    MahoAuthenticatedServiceApiBroker broker(
        nullptr, vault, approval_store, scope_registry, std::move(transport));

    DirectApiOpDescriptor mut_op(
        DirectApiServiceKind::kSlack, "chat.postMessage",
        "{\"channel\":\"C123\",\"text\":\"Hello\"}", /*read_only=*/false,
        {"chat:write"});

    DirectApiExecutionContext ctx("session-1");
    ctx.opaque_auth_handle = "slack-handle";

    DirectApiExecutionOutcome out1;
    broker.ExecuteOp(
        mut_op, ctx, std::nullopt,
        base::BindOnce(
            [](DirectApiExecutionOutcome* o, DirectApiExecutionOutcome res) {
              *o = std::move(res);
            },
            &out1));

    assert(out1.status == DirectApiOutcomeStatus::kNeedsConfirmation);
    assert(out1.action_id == "slack:chat.postMessage");
    assert(transport_ptr->SentCount() == 0);

    std::string token = approval_store->IssueToken(out1.action_id);
    assert(!token.empty());

    DirectApiExecutionOutcome out2;
    broker.ExecuteOp(
        mut_op, ctx, token,
        base::BindOnce(
            [](DirectApiExecutionOutcome* o, DirectApiExecutionOutcome res) {
              *o = std::move(res);
            },
            &out2));

    assert(out2.status == DirectApiOutcomeStatus::kOk);
    assert(transport_ptr->SentCount() == 1);

    DirectApiExecutionOutcome out3;
    broker.ExecuteOp(
        mut_op, ctx, token,
        base::BindOnce(
            [](DirectApiExecutionOutcome* o, DirectApiExecutionOutcome res) {
              *o = std::move(res);
            },
            &out3));

    assert(out3.status == DirectApiOutcomeStatus::kTypedError);
    assert(out3.error_code == DirectApiErrorCode::kApprovalTokenAlreadyUsed);
    assert(transport_ptr->SentCount() == 1);
  }

  // Test 3: Scope enforcement fails closed pre-network
  {
    auto vault = std::make_shared<InMemoryCredentialVault>();
    auto approval_store = std::make_shared<InMemoryApprovalTokenStore>();
    auto scope_registry = std::make_shared<InMemoryScopeRegistry>();
    auto transport = std::make_unique<InMemoryDirectApiTransport>();
    auto* transport_ptr = transport.get();

    vault->StoreCredential("acc-1", VaultCredential::Bearer("tok"));
    scope_registry->GrantScopes("acc-1", DirectApiServiceKind::kGmail,
                                {"gmail.readonly"});

    MahoAuthenticatedServiceApiBroker broker(
        nullptr, vault, approval_store, scope_registry, std::move(transport));

    DirectApiOpDescriptor op(DirectApiServiceKind::kGmail, "messages.send", "{}",
                             true, {"gmail.send", "gmail.readonly"});

    DirectApiExecutionContext ctx("s");
    ctx.opaque_auth_handle = "acc-1";

    DirectApiExecutionOutcome out;
    broker.ExecuteOp(
        op, ctx, std::nullopt,
        base::BindOnce(
            [](DirectApiExecutionOutcome* o, DirectApiExecutionOutcome res) {
              *o = std::move(res);
            },
            &out));

    assert(out.status == DirectApiOutcomeStatus::kForbidden);
    assert(out.missing_scopes.size() == 1);
    assert(out.missing_scopes[0] == "gmail.send");
    assert(transport_ptr->SentCount() == 0);
  }

  // Test 4: Typed outcomes and HTML sanitization
  {
    auto vault = std::make_shared<InMemoryCredentialVault>();
    auto approval_store = std::make_shared<InMemoryApprovalTokenStore>();
    auto scope_registry = std::make_shared<InMemoryScopeRegistry>();
    auto transport = std::make_unique<InMemoryDirectApiTransport>();
    auto* transport_ptr = transport.get();

    vault->StoreCredential("cal-handle", VaultCredential::Bearer("tok"));

    TransportResponse rate_resp;
    rate_resp.status_code = 429;
    rate_resp.body = "{\"error\":\"Too Many Requests\"}";
    transport_ptr->SetResponse(DirectApiServiceKind::kGoogleCalendar,
                               "events.list", rate_resp);

    MahoAuthenticatedServiceApiBroker broker(
        nullptr, vault, approval_store, scope_registry, std::move(transport));

    DirectApiOpDescriptor op(DirectApiServiceKind::kGoogleCalendar,
                             "events.list", "{}", true, {});
    DirectApiExecutionContext ctx("s");
    ctx.opaque_auth_handle = "cal-handle";

    DirectApiExecutionOutcome out;
    broker.ExecuteOp(
        op, ctx, std::nullopt,
        base::BindOnce(
            [](DirectApiExecutionOutcome* o, DirectApiExecutionOutcome res) {
              *o = std::move(res);
            },
            &out));

    assert(out.status == DirectApiOutcomeStatus::kTypedError);
    assert(out.error_code == DirectApiErrorCode::kRateLimited);
    assert(out.retryable);

    auto hl = out.ToHighLevelOutcome();
    assert(hl.type == HighLevelDirectApiOutcome::Type::kTypedUnavailable);
    assert(hl.can_fallback_to_tabs);

    std::string sanitized = MahoAuthenticatedServiceApiBroker::SanitizeErrorPayload(
        "<html><head><title>500</title></head><body>500 Internal Error</body></html>");
    assert(sanitized == "HTML error response from upstream service");
  }

  // Test 5: Cancel mid-flight
  {
    auto vault = std::make_shared<InMemoryCredentialVault>();
    auto approval_store = std::make_shared<InMemoryApprovalTokenStore>();
    auto scope_registry = std::make_shared<InMemoryScopeRegistry>();
    auto transport = std::make_unique<InMemoryDirectApiTransport>();
    auto* transport_ptr = transport.get();

    vault->StoreCredential("tg-handle", VaultCredential::Bearer("tok"));
    TransportError cancel_err;
    cancel_err.kind = TransportErrorKind::kCancelled;
    cancel_err.message = "Cancelled";
    transport_ptr->SetResponse(DirectApiServiceKind::kTelegram, "sendMessage",
                               base::unexpected(cancel_err));

    std::string token = approval_store->IssueToken("telegram:sendMessage");
    MahoAuthenticatedServiceApiBroker broker(
        nullptr, vault, approval_store, scope_registry, std::move(transport));

    DirectApiOpDescriptor op(DirectApiServiceKind::kTelegram, "sendMessage", "{}",
                             false, {});
    DirectApiExecutionContext ctx("s");
    ctx.opaque_auth_handle = "tg-handle";

    DirectApiExecutionOutcome out;
    broker.ExecuteOp(
        op, ctx, token,
        base::BindOnce(
            [](DirectApiExecutionOutcome* o, DirectApiExecutionOutcome res) {
              *o = std::move(res);
            },
            &out));

    assert(out.status == DirectApiOutcomeStatus::kCancelled);
    auto hl = out.ToHighLevelOutcome();
    assert(hl.type == HighLevelDirectApiOutcome::Type::kHardFailure);
    assert(hl.error_code == "CANCELLED");
    assert(!hl.can_fallback_to_tabs);
  }

  // Test 6: Supported operations
  {
    assert(MahoAuthenticatedServiceApiBroker::IsOperationSupported(
        DirectApiServiceKind::kGmail, "messages.list"));
    assert(MahoAuthenticatedServiceApiBroker::IsOperationSupported(
        DirectApiServiceKind::kTelegram, "sendMessage"));
    assert(!MahoAuthenticatedServiceApiBroker::IsOperationSupported(
        DirectApiServiceKind::kTelegram, "invalid_op"));
  }

  std::cout << "[PASS] All MahoAuthenticatedServiceApiBroker standalone tests passed!"
            << std::endl;
  return 0;
}

#endif  // MAHO_STANDALONE_TEST
