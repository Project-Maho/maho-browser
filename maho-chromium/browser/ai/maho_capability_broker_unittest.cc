// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ai/maho_capability_broker.h"

#include <string>
#include <vector>

#include "base/time/time.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho::ai {
namespace {

TEST(MahoCapabilityBrokerTest, ValidateAllDescriptors) {
  EXPECT_TRUE(MahoCapabilityBroker::ValidateAllDescriptors());
}

TEST(MahoCapabilityBrokerTest, LookupByCanonicalIdAndToolName) {
  const CapabilityDescriptor* desc1 =
      MahoCapabilityBroker::FindCapabilityById("page.read_current");
  ASSERT_NE(desc1, nullptr);
  EXPECT_EQ(desc1->tool_name, "read_current_page");
  EXPECT_EQ(desc1->category, CapabilityCategory::kPage);

  const CapabilityDescriptor* desc2 =
      MahoCapabilityBroker::FindCapabilityByToolName("browser_click");
  ASSERT_NE(desc2, nullptr);
  EXPECT_EQ(desc2->canonical_id, "input.click");
  EXPECT_EQ(desc2->mutability, CapabilityMutability::kMutable);
  EXPECT_TRUE(RequiresLease(*desc2));
  EXPECT_TRUE(RequiresApproval(*desc2));

  const CapabilityDescriptor* desc3 =
      MahoCapabilityBroker::FindCapability("vault.credential.fill");
  ASSERT_NE(desc3, nullptr);
  EXPECT_EQ(desc3->category, CapabilityCategory::kVault);
  EXPECT_EQ(desc3->user_presence_requirement,
            UserPresenceRequirement::kBiometricStrong);
  EXPECT_EQ(desc3->minimum_device_protection,
            MinimumDeviceProtection::kHardwareKey);
  EXPECT_TRUE(desc3->transaction_binding);

  const CapabilityDescriptor* non_existent =
      MahoCapabilityBroker::FindCapability("non_existent_capability");
  EXPECT_EQ(non_existent, nullptr);
}

TEST(MahoCapabilityBrokerTest, FailClosedOnUnknownCapability) {
  MahoCapabilityBroker broker;
  CapabilityRequestContext ctx;
  ctx.principal = CapabilityPrincipal::MakeInternalAgent("session-1");
  ctx.capability_id = "unknown_exploit_tool";

  CapabilityEvaluationResult result = broker.Evaluate(ctx);
  EXPECT_TRUE(result.IsDenied());
  EXPECT_EQ(result.deny_reason, CapabilityDenyReason::kUnknownCapability);
}

TEST(MahoCapabilityBrokerTest, FailClosedOnUnknownPrincipal) {
  MahoCapabilityBroker broker;
  CapabilityRequestContext ctx;
  ctx.capability_id = "page.read_current";
  // Default principal has kind == kUnknown

  CapabilityEvaluationResult result = broker.Evaluate(ctx);
  EXPECT_TRUE(result.IsDenied());
  EXPECT_EQ(result.deny_reason, CapabilityDenyReason::kInsufficientPrincipalAuth);
}

TEST(MahoCapabilityBrokerTest, SurfaceAuthorizationEnforcement) {
  MahoCapabilityBroker broker;

  // 1. In-browser Agent accessing read_current_page (allowed)
  CapabilityRequestContext agent_ctx;
  agent_ctx.principal = CapabilityPrincipal::MakeInternalAgent("session-1");
  agent_ctx.surface = CapabilitySurface::kDesktopAgent;
  agent_ctx.capability_id = "page.read_current";
  agent_ctx.active_tab_id = 10;
  agent_ctx.source_origin = "https://example.com";

  CapabilityEvaluationResult agent_res = broker.Evaluate(agent_ctx);
  EXPECT_TRUE(agent_res.IsPermitted());
  EXPECT_TRUE(agent_res.obligations.requires_url_redaction);

  // 2. ControlPlane-only capability (vault fill) from Public MCP (disallowed)
  CapabilityRequestContext mcp_ctx;
  mcp_ctx.principal = CapabilityPrincipal::MakeCliGeneric(
      "mcp-client-1", AuthenticationStrength::kUnixUidMatch);
  mcp_ctx.surface = CapabilitySurface::kPublicMcp;
  mcp_ctx.capability_id = "vault.credential.fill";
  mcp_ctx.active_tab_id = 10;

  CapabilityEvaluationResult mcp_res = broker.Evaluate(mcp_ctx);
  EXPECT_TRUE(mcp_res.IsDenied());
  EXPECT_EQ(mcp_res.deny_reason, CapabilityDenyReason::kSurfaceNotAllowed);
}

TEST(MahoCapabilityBrokerTest, FeatureGateEnforcement) {
  MahoCapabilityBroker broker;
  FeatureGateState disabled_features;
  disabled_features.mail_enabled = false;
  disabled_features.vault_enabled = false;
  disabled_features.routines_enabled = false;

  CapabilityRequestContext mail_ctx;
  mail_ctx.principal = CapabilityPrincipal::MakeInternalAgent("session-1");
  mail_ctx.surface = CapabilitySurface::kDesktopAgent;
  mail_ctx.capability_id = "mail.accounts.list";

  CapabilityEvaluationResult mail_res =
      broker.Evaluate(mail_ctx, disabled_features);
  EXPECT_TRUE(mail_res.IsDenied());
  EXPECT_EQ(mail_res.deny_reason, CapabilityDenyReason::kFeatureDisabled);

  CapabilityRequestContext routine_ctx;
  routine_ctx.principal = CapabilityPrincipal::MakeCliGeneric(
      "cli-1", AuthenticationStrength::kUnixUidMatch);
  routine_ctx.surface = CapabilitySurface::kBrowserMcp;
  routine_ctx.capability_id = "routines.list";

  CapabilityEvaluationResult routine_res =
      broker.Evaluate(routine_ctx, disabled_features);
  EXPECT_TRUE(routine_res.IsDenied());
  EXPECT_EQ(routine_res.deny_reason, CapabilityDenyReason::kFeatureDisabled);
}

TEST(MahoCapabilityBrokerTest, IncognitoAndGuestIsolation) {
  MahoCapabilityBroker broker;
  CapabilityRequestContext incognito_ctx;
  incognito_ctx.principal = CapabilityPrincipal::MakeInternalAgent("session-1");
  incognito_ctx.surface = CapabilitySurface::kBrowserMcp;
  incognito_ctx.is_incognito = true;
  incognito_ctx.capability_id = "history.search";

  CapabilityEvaluationResult res = broker.Evaluate(incognito_ctx);
  EXPECT_TRUE(res.IsDenied());
  EXPECT_EQ(res.deny_reason,
            CapabilityDenyReason::kIncognitoOrGuestDisallowed);
}

TEST(MahoCapabilityBrokerTest, LeaseAndApprovalLifecycle) {
  MahoCapabilityBroker broker;

  // Step 1: Missing lease
  CapabilityRequestContext step1_ctx;
  step1_ctx.principal = CapabilityPrincipal::MakeInternalAgent("session-1");
  step1_ctx.surface = CapabilitySurface::kDesktopAgent;
  step1_ctx.capability_id = "input.click";
  step1_ctx.active_tab_id = 42;
  step1_ctx.source_origin = "https://app.example.com";

  CapabilityEvaluationResult step1_res = broker.Evaluate(step1_ctx);
  EXPECT_TRUE(step1_res.IsDenied());
  EXPECT_EQ(step1_res.deny_reason, CapabilityDenyReason::kMissingLease);

  // Step 2: With lease, but missing approval
  CapabilityRequestContext step2_ctx = step1_ctx;
  step2_ctx.lease_token = "lease-token-abc";

  CapabilityEvaluationResult step2_res = broker.Evaluate(step2_ctx);
  EXPECT_TRUE(step2_res.RequiresApproval());
  ASSERT_TRUE(step2_res.approval_request.has_value());
  EXPECT_EQ(step2_res.approval_request->capability_id, "input.click");

  // Step 3: With valid lease and approval token
  CapabilityRequestContext step3_ctx = step2_ctx;
  step3_ctx.approval_token = "user-approval-token-xyz";

  CapabilityEvaluationResult step3_res = broker.Evaluate(step3_ctx);
  EXPECT_TRUE(step3_res.IsPermitted());
  EXPECT_TRUE(step3_res.obligations.requires_lease_heartbeat);
  EXPECT_TRUE(step3_res.obligations.requires_target_tab_revalidation);
  EXPECT_EQ(step3_res.obligations.audit_class, CapabilityAuditClass::kMutation);
}

TEST(MahoCapabilityBrokerTest, OriginPolicyAndDomainBlocklist) {
  MahoCapabilityBroker broker;

  // 1. Blocked domain
  CapabilityRequestContext block_ctx;
  block_ctx.principal = CapabilityPrincipal::MakeInternalAgent("session-1");
  block_ctx.surface = CapabilitySurface::kDesktopAgent;
  block_ctx.capability_id = "page.read_current";
  block_ctx.active_tab_id = 1;
  block_ctx.source_origin = "https://malicious-site.com";
  block_ctx.blocked_domains = {"malicious-site.com"};

  CapabilityEvaluationResult block_res = broker.Evaluate(block_ctx);
  EXPECT_TRUE(block_res.IsDenied());
  EXPECT_EQ(block_res.deny_reason, CapabilityDenyReason::kDomainBlocked);

  // 2. Active tab origin mismatch
  CapabilityRequestContext mismatch_ctx;
  mismatch_ctx.principal = CapabilityPrincipal::MakeInternalAgent("session-1");
  mismatch_ctx.surface = CapabilitySurface::kDesktopAgent;
  mismatch_ctx.capability_id = "input.click";
  mismatch_ctx.active_tab_id = 1;
  mismatch_ctx.lease_token = "lease-1";
  mismatch_ctx.approval_token = "appr-1";
  mismatch_ctx.source_origin = "https://safe.example.com";
  mismatch_ctx.destination_origin = "https://other.example.com";

  CapabilityEvaluationResult mismatch_res = broker.Evaluate(mismatch_ctx);
  EXPECT_TRUE(mismatch_res.IsDenied());
  EXPECT_EQ(mismatch_res.deny_reason, CapabilityDenyReason::kOriginNotAllowed);
}

TEST(MahoCapabilityBrokerTest, UserPresenceAndHardwareProtection) {
  MahoCapabilityBroker broker;

  // 1. Vault credential fill without user presence token -> RequiresUserPresence
  CapabilityRequestContext vault_ctx;
  vault_ctx.principal = CapabilityPrincipal::MakeControlPlane();
  vault_ctx.surface = CapabilitySurface::kControlPlane;
  vault_ctx.capability_id = "vault.credential.fill";
  vault_ctx.active_tab_id = 5;
  vault_ctx.source_origin = "https://bank.com";
  vault_ctx.request_digest = "sha256:abcd1234efgh5678";
  vault_ctx.approval_token = "approval-ok";

  CapabilityEvaluationResult vault_res = broker.Evaluate(vault_ctx);
  EXPECT_TRUE(vault_res.RequiresUserPresence());
  ASSERT_TRUE(vault_res.presence_request.has_value());
  EXPECT_EQ(vault_res.presence_request->requirement,
            UserPresenceRequirement::kBiometricStrong);
  EXPECT_EQ(vault_res.presence_request->minimum_device_protection,
            MinimumDeviceProtection::kHardwareKey);

  // 2. RevalidateAfterPresence with matching digest and fresh timestamp -> Permit
  base::Time now = base::Time::Now();
  CapabilityEvaluationResult reval_res = broker.RevalidateAfterPresence(
      vault_ctx, "presence-token-valid", "sha256:abcd1234efgh5678", now, now);
  EXPECT_TRUE(reval_res.IsPermitted());
  EXPECT_TRUE(reval_res.obligations.transaction_binding);
  EXPECT_EQ(reval_res.obligations.audit_class, CapabilityAuditClass::kHighRisk);

  // 3. RevalidateAfterPresence with digest mismatch -> Deny(kTransactionBindingMismatch)
  CapabilityEvaluationResult mismatch_res = broker.RevalidateAfterPresence(
      vault_ctx, "presence-token-valid", "sha256:WRONG_DIGEST", now, now);
  EXPECT_TRUE(mismatch_res.IsDenied());
  EXPECT_EQ(mismatch_res.deny_reason,
            CapabilityDenyReason::kTransactionBindingMismatch);

  // 4. RevalidateAfterPresence expired -> Deny(kStaleUserPresence)
  CapabilityEvaluationResult stale_res = broker.RevalidateAfterPresence(
      vault_ctx, "presence-token-valid", "sha256:abcd1234efgh5678",
      now - base::Seconds(65), now);
  EXPECT_TRUE(stale_res.IsDenied());
  EXPECT_EQ(stale_res.deny_reason, CapabilityDenyReason::kStaleUserPresence);
}

TEST(MahoCapabilityBrokerTest, TabAdoptionEnforcementForLocalMcp) {
  MahoCapabilityBroker broker;
  CapabilityRequestContext mcp_ctx;
  mcp_ctx.principal = CapabilityPrincipal::MakeCliGeneric(
      "cli-client", AuthenticationStrength::kUnixUidMatch);
  mcp_ctx.surface = CapabilitySurface::kBrowserMcp;
  mcp_ctx.capability_id = "tab.get";
  mcp_ctx.target_tab_id = 99;
  mcp_ctx.adopted_tabs = {10, 20, 30};  // Tab 99 is not adopted

  CapabilityEvaluationResult res = broker.Evaluate(mcp_ctx);
  EXPECT_TRUE(res.IsDenied());
  EXPECT_EQ(res.deny_reason, CapabilityDenyReason::kTabNotAdopted);

  // When adopted tab includes 99 -> Permit
  mcp_ctx.adopted_tabs.push_back(99);
  CapabilityEvaluationResult permit_res = broker.Evaluate(mcp_ctx);
  EXPECT_TRUE(permit_res.IsPermitted());
}

}  // namespace
}  // namespace maho::ai
