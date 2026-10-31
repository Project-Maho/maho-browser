// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ai/maho_capability_broker.h"

#include <string>
#include <string_view>
#include <vector>

#include "base/time/time.h"
#include "maho/browser/ai/maho_mail_tool_authorization.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho::ai {
namespace {

TEST(MahoCapabilityBrokerTest, ValidateAllDescriptors) {
  EXPECT_TRUE(MahoCapabilityBroker::ValidateAllDescriptors());
}

TEST(MahoCapabilityBrokerTest, RuntimeConfigContextDefaultsMirrorToday) {
  // Plan row 1 plumbing: the request-context defaults must equal today's
  // effective behavior (guard / confirm-on / proactive-off).
  CapabilityRequestContext ctx;
  EXPECT_EQ(ctx.permission_tier, "guard");
  EXPECT_TRUE(ctx.final_confirm);
  EXPECT_FALSE(ctx.proactive_mode);
}

TEST(MahoCapabilityBrokerTest, RuntimeConfigTierParseFailsClosedToGuard) {
  EXPECT_EQ(ParseRuntimeConfigTier("read_only"), "read_only");
  EXPECT_EQ(ParseRuntimeConfigTier("guard"), "guard");
  EXPECT_EQ(ParseRuntimeConfigTier("full_access"), "full_access");
  EXPECT_EQ(ParseRuntimeConfigTier(""), "guard");
  EXPECT_EQ(ParseRuntimeConfigTier("root"), "guard");
  EXPECT_EQ(ParseRuntimeConfigTier("GUARD"), "guard");
}

TEST(MahoCapabilityBrokerTest, RuntimeConfigPlumbingDoesNotChangeDecisions) {
  // Row 1 is plumbing-only: a non-default runtime_config triple must not
  // alter any Evaluate decision (enforcement lands in later rows).
  MahoCapabilityBroker broker;
  CapabilityRequestContext ctx;
  ctx.principal = CapabilityPrincipal::MakeInternalAgent("session-1");
  ctx.capability_id = "unknown_exploit_tool";
  ctx.permission_tier = "full_access";
  ctx.final_confirm = false;
  ctx.proactive_mode = true;

  CapabilityEvaluationResult result = broker.Evaluate(ctx);
  EXPECT_TRUE(result.IsDenied());
  EXPECT_EQ(result.deny_reason, CapabilityDenyReason::kUnknownCapability);
}

TEST(MahoCapabilityBrokerTest, FsPathWhitelistMatchingIsBoundaryExactAndFailClosed) {
  const std::vector<std::string> roots = {
      "/Users/u/Downloads", "/Users/u/Documents/",
      "/Users/u/Library/Application Support/Maho/sessions/s-1"};
  EXPECT_TRUE(
      FsPathInsideRuntimeWhitelist("/Users/u/Downloads/a.pdf", roots));
  EXPECT_TRUE(FsPathInsideRuntimeWhitelist("/Users/u/Documents", roots));
  EXPECT_TRUE(FsPathInsideRuntimeWhitelist(
      "/Users/u/Library/Application Support/Maho/sessions/s-1/out.txt", roots));
  // Sibling prefix must not match (component-boundary rule).
  EXPECT_FALSE(
      FsPathInsideRuntimeWhitelist("/Users/u/DocumentsEvil/x", roots));
  EXPECT_FALSE(FsPathInsideRuntimeWhitelist("/etc/passwd", roots));
  // Fail-closed edges: empty path, relative path, empty roots, empty root.
  EXPECT_FALSE(FsPathInsideRuntimeWhitelist("", roots));
  EXPECT_FALSE(FsPathInsideRuntimeWhitelist("relative/path", roots));
  EXPECT_FALSE(
      FsPathInsideRuntimeWhitelist("/Users/u/Downloads/a.pdf", {}));
  EXPECT_FALSE(FsPathInsideRuntimeWhitelist("/Users/u/Downloads/a.pdf",
                                            {std::string()}));
  // An all-slash root would whitelist the whole filesystem; it must fail
  // closed exactly like the Rust fs_path_inside_whitelist mirror.
  EXPECT_FALSE(
      FsPathInsideRuntimeWhitelist("/Users/u/Downloads/a.pdf", {"/"}));
  EXPECT_FALSE(FsPathInsideRuntimeWhitelist("/etc/passwd", {"///"}));
}

namespace {
CapabilityRequestContext MakeFileScopedContext(const char* capability_id,
                                               const std::string& fs_path) {
  CapabilityRequestContext ctx;
  ctx.principal = CapabilityPrincipal::MakeControlPlane();
  ctx.surface = CapabilitySurface::kControlPlane;
  ctx.capability_id = capability_id;
  ctx.fs_path = fs_path;
  ctx.fs_whitelist_roots = {"/Users/u/Downloads", "/Users/u/Documents"};
  return ctx;
}
}  // namespace

TEST(MahoCapabilityBrokerTest, PermissionTierReadOnlyDeniesFileWriteAndOutsideRead) {
  MahoCapabilityBroker broker;

  // Writes are denied everywhere, even inside the whitelist, and an approval
  // token can never rescue a tier denial.
  CapabilityRequestContext write_ctx =
      MakeFileScopedContext("artifact.export", "/Users/u/Downloads/a.pdf");
  write_ctx.permission_tier = "read_only";
  write_ctx.approval_token = "user-ok";
  CapabilityEvaluationResult write_res = broker.Evaluate(write_ctx);
  EXPECT_TRUE(write_res.IsDenied());
  EXPECT_EQ(write_res.deny_reason,
            CapabilityDenyReason::kPermissionTierDeniedWrite);

  CapabilityRequestContext write_outside_ctx =
      MakeFileScopedContext("artifact.export", "/etc/passwd");
  write_outside_ctx.permission_tier = "read_only";
  CapabilityEvaluationResult write_outside_res =
      broker.Evaluate(write_outside_ctx);
  EXPECT_TRUE(write_outside_res.IsDenied());
  EXPECT_EQ(write_outside_res.deny_reason,
            CapabilityDenyReason::kPermissionTierDeniedWrite);

  // Out-of-whitelist reads are denied; in-whitelist reads fall through and
  // hit only the structural checks.
  CapabilityRequestContext read_outside_ctx =
      MakeFileScopedContext("page.read_current", "/etc/passwd");
  read_outside_ctx.permission_tier = "read_only";
  read_outside_ctx.surface = CapabilitySurface::kDesktopAgent;
  read_outside_ctx.principal =
      CapabilityPrincipal::MakeInternalAgent("session-1");
  read_outside_ctx.active_tab_id = 1;
  read_outside_ctx.source_origin = "https://example.com";
  CapabilityEvaluationResult read_outside_res =
      broker.Evaluate(read_outside_ctx);
  EXPECT_TRUE(read_outside_res.IsDenied());
  EXPECT_EQ(read_outside_res.deny_reason,
            CapabilityDenyReason::kPermissionTierDeniedRead);

  CapabilityRequestContext read_inside_ctx = read_outside_ctx;
  read_inside_ctx.fs_path = "/Users/u/Downloads/report.pdf";
  CapabilityEvaluationResult read_inside_res = broker.Evaluate(read_inside_ctx);
  EXPECT_TRUE(read_inside_res.IsPermitted());
}

TEST(MahoCapabilityBrokerTest, PermissionTierGuardAskGatesOutsideWhitelistAndIsFreeInside) {
  MahoCapabilityBroker broker;

  // Outside the whitelist, the tier forces the existing approval-token
  // ask-gate even though the request carries a valid context.
  CapabilityRequestContext outside_ctx =
      MakeFileScopedContext("artifact.export", "/etc/passwd");
  outside_ctx.permission_tier = "guard";
  CapabilityEvaluationResult outside_res = broker.Evaluate(outside_ctx);
  EXPECT_TRUE(outside_res.RequiresApproval());
  ASSERT_TRUE(outside_res.approval_request.has_value());
  EXPECT_EQ(outside_res.approval_request->capability_id, "artifact.export");

  // A granted approval token satisfies the ask-gate.
  CapabilityRequestContext approved_ctx = outside_ctx;
  approved_ctx.approval_token = "user-ok";
  CapabilityEvaluationResult approved_res = broker.Evaluate(approved_ctx);
  EXPECT_TRUE(approved_res.IsPermitted());

  // Inside the whitelist, the tier gate adds nothing beyond the descriptor's
  // own structural approval (artifact.export is approval-required).
  CapabilityRequestContext inside_ctx =
      MakeFileScopedContext("artifact.export", "/Users/u/Downloads/a.pdf");
  inside_ctx.permission_tier = "guard";
  CapabilityEvaluationResult inside_no_token = broker.Evaluate(inside_ctx);
  EXPECT_TRUE(inside_no_token.RequiresApproval());
  inside_ctx.approval_token = "user-ok";
  EXPECT_TRUE(broker.Evaluate(inside_ctx).IsPermitted());

  // A read-only descriptor inside the whitelist is fully free under guard.
  CapabilityRequestContext read_ctx =
      MakeFileScopedContext("page.read_current", "/Users/u/Downloads/a.pdf");
  read_ctx.permission_tier = "guard";
  read_ctx.surface = CapabilitySurface::kDesktopAgent;
  read_ctx.principal = CapabilityPrincipal::MakeInternalAgent("session-1");
  read_ctx.active_tab_id = 1;
  read_ctx.source_origin = "https://example.com";
  EXPECT_TRUE(broker.Evaluate(read_ctx).IsPermitted());
}

TEST(MahoCapabilityBrokerTest, PermissionTierFullAccessNeverDisablesStructuralChecks) {
  MahoCapabilityBroker broker;

  // full_access permits out-of-whitelist file operations once structural
  // obligations (here: artifact.export's approval) are satisfied.
  CapabilityRequestContext export_ctx =
      MakeFileScopedContext("artifact.export", "/etc/passwd");
  export_ctx.permission_tier = "full_access";
  export_ctx.approval_token = "user-ok";
  EXPECT_TRUE(broker.Evaluate(export_ctx).IsPermitted());

  // ...but structural lease/origin checks are never disabled by any tier.
  CapabilityRequestContext lease_ctx;
  lease_ctx.principal = CapabilityPrincipal::MakeInternalAgent("session-1");
  lease_ctx.surface = CapabilitySurface::kDesktopAgent;
  lease_ctx.capability_id = "input.click";
  lease_ctx.active_tab_id = 42;
  lease_ctx.source_origin = "https://app.example.com";
  lease_ctx.permission_tier = "full_access";
  CapabilityEvaluationResult lease_res = broker.Evaluate(lease_ctx);
  EXPECT_TRUE(lease_res.IsDenied());
  EXPECT_EQ(lease_res.deny_reason, CapabilityDenyReason::kMissingLease);
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
  // Browser manipulation is full access; only capabilities that leave the
  // browser boundary carry a descriptor-level approval requirement.
  EXPECT_FALSE(RequiresApproval(*desc2));

  const CapabilityDescriptor* desc3 =
      MahoCapabilityBroker::FindCapability("vault.credential.fill");
  ASSERT_NE(desc3, nullptr);
  EXPECT_TRUE(RequiresApproval(*desc3));
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

  // Step 2: With the lease, the click is owed nothing else. Driving the
  // browser is full access, so no approval is requested here — the lease is
  // the obligation, not the user's permission.
  CapabilityRequestContext step2_ctx = step1_ctx;
  step2_ctx.lease_token = "lease-token-abc";

  CapabilityEvaluationResult step2_res = broker.Evaluate(step2_ctx);
  EXPECT_TRUE(step2_res.IsPermitted());
  EXPECT_FALSE(step2_res.approval_request.has_value());

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

// ---- Final-confirmation gate (plan row 5) --------------------------------

namespace {
CapabilityRequestContext MakePageReadContext() {
  CapabilityRequestContext ctx;
  ctx.principal = CapabilityPrincipal::MakeInternalAgent("session-1");
  ctx.surface = CapabilitySurface::kDesktopAgent;
  ctx.capability_id = "page.read_current";
  ctx.active_tab_id = 7;
  ctx.source_origin = "https://example.com";
  return ctx;
}
}  // namespace

TEST(MahoCapabilityBrokerTest, FinalConfirmForcesApprovalForGatedConsequence) {
  MahoCapabilityBroker broker;
  // mail.accounts.list leaves the browser and carries no descriptor-level
  // approval requirement, so the ask below can only come from the
  // final-confirm gate: a declared submit consequence is externally visible
  // and must obtain a token. The same classification inside the browser stays
  // free — see FinalConfirmFailsClosedOnlyOffBrowser.
  CapabilityRequestContext ctx;
  ctx.principal = CapabilityPrincipal::MakeInternalAgent("session-1");
  ctx.surface = CapabilitySurface::kDesktopAgent;
  ctx.capability_id = "mail.accounts.list";
  ctx.action_consequence = "submit";
  CapabilityEvaluationResult res = broker.Evaluate(ctx);
  EXPECT_TRUE(res.RequiresApproval());
  ASSERT_TRUE(res.approval_request.has_value());
  EXPECT_EQ(res.approval_request->capability_id, "mail.accounts.list");

  // The ask routes through the existing approval-token handshake: a granted
  // token satisfies it.
  ctx.approval_token = "user-ok";
  EXPECT_TRUE(broker.Evaluate(ctx).IsPermitted());
}

TEST(MahoCapabilityBrokerTest, FinalConfirmKeepsFreeConsequencesFree) {
  MahoCapabilityBroker broker;
  // Read tools with no declared consequence stay permit-only...
  EXPECT_TRUE(broker.Evaluate(MakePageReadContext()).IsPermitted());
  // ...as do declared ordinary and new_origin (navigation is reversible).
  CapabilityRequestContext ordinary = MakePageReadContext();
  ordinary.action_consequence = "ordinary";
  EXPECT_TRUE(broker.Evaluate(ordinary).IsPermitted());

  CapabilityRequestContext navigation = MakePageReadContext();
  navigation.action_consequence = "new_origin";
  EXPECT_TRUE(broker.Evaluate(navigation).IsPermitted());
}

TEST(MahoCapabilityBrokerTest, FinalConfirmFailsClosedOnlyOffBrowser) {
  MahoCapabilityBroker broker;
  // Off-browser, an explicitly Unknown classification still fails closed into
  // the approval-token ask-gate, and a granted token satisfies it.
  CapabilityRequestContext unknown_ctx;
  unknown_ctx.principal = CapabilityPrincipal::MakeInternalAgent("session-1");
  unknown_ctx.surface = CapabilitySurface::kDesktopAgent;
  unknown_ctx.capability_id = "mail.accounts.list";
  unknown_ctx.action_consequence = "unknown";
  EXPECT_TRUE(broker.Evaluate(unknown_ctx).RequiresApproval());

  unknown_ctx.approval_token = "user-ok";
  EXPECT_TRUE(broker.Evaluate(unknown_ctx).IsPermitted());

  // Inside the browser the gate never applies: driving the browser is full
  // access, so an unclassified click is still just a click...
  CapabilityRequestContext click_ctx;
  click_ctx.principal = CapabilityPrincipal::MakeInternalAgent("session-1");
  click_ctx.surface = CapabilitySurface::kDesktopAgent;
  click_ctx.capability_id = "input.click";
  click_ctx.active_tab_id = 42;
  click_ctx.source_origin = "https://app.example.com";
  click_ctx.lease_token = "lease-1";
  EXPECT_TRUE(broker.Evaluate(click_ctx).IsPermitted());

  // ...and so is one the model classified as a gated consequence class.
  click_ctx.action_consequence = "submit";
  EXPECT_TRUE(broker.Evaluate(click_ctx).IsPermitted());

  // The same holds for an Unknown-classified page read.
  CapabilityRequestContext page_unknown = MakePageReadContext();
  page_unknown.action_consequence = "unknown";
  EXPECT_TRUE(broker.Evaluate(page_unknown).IsPermitted());
}

TEST(MahoCapabilityBrokerTest, FinalConfirmFalsePreservesTodayDecisions) {
  MahoCapabilityBroker broker;
  // final_confirm=false preserves today's outcomes exactly: the gate is
  // inert — no new ask for a gated consequence class.
  CapabilityRequestContext ctx = MakePageReadContext();
  ctx.action_consequence = "submit";
  ctx.final_confirm = false;
  EXPECT_TRUE(broker.Evaluate(ctx).IsPermitted());

  // A validated-ordinary browser action still passes under the default
  // flag: final_confirm only adds the ask for gated classes.
  CapabilityRequestContext ordinary_click;
  ordinary_click.principal = CapabilityPrincipal::MakeInternalAgent("session-1");
  ordinary_click.surface = CapabilitySurface::kDesktopAgent;
  ordinary_click.capability_id = "input.click";
  ordinary_click.active_tab_id = 42;
  ordinary_click.source_origin = "https://app.example.com";
  ordinary_click.lease_token = "lease-1";
  ordinary_click.approval_token = "user-ok";
  ordinary_click.action_consequence = "ordinary";
  EXPECT_TRUE(broker.Evaluate(ordinary_click).IsPermitted());
}

// --- Mail authorization characterization -----------------------------------
// AuthorizeMailTool's reason-code ORDER is a public contract shared with
// maho/crates/maho-agent/src/permission.rs and the website docs, so the order
// itself is pinned here, not just the individual outcomes. Every case forces
// all LATER gates into their deny state, so only correct precedence can
// produce the earlier reason code.

constexpr MailAuthorizationContext MailContextAllOpen() {
  MailAuthorizationContext context;
  context.feature_enabled = true;
  context.helper_ready = true;
  context.helper_starting = false;
  context.read_allowed = true;
  context.global_policy = MailGlobalPolicy::kAllow;
  return context;
}

TEST(MahoMailToolAuthorizationTest, ReasonCodeOrderIsStable) {
  // 1. An unrecognized tool is rejected before any state is consulted.
  EXPECT_EQ(AuthorizeMailTool("not_a_mail_tool", MailContextAllOpen()).reason_code,
            "mail_tool_unknown");

  // 2. Feature gate precedes helper, policy, and consent.
  MailAuthorizationContext feature_off = MailContextAllOpen();
  feature_off.feature_enabled = false;
  feature_off.helper_ready = false;
  feature_off.read_allowed = false;
  feature_off.global_policy = MailGlobalPolicy::kDeny;
  EXPECT_EQ(AuthorizeMailTool("mail_list_emails", feature_off).reason_code,
            "mail_feature_disabled");

  // 3. Helper lifecycle precedes policy and consent, and distinguishes a
  //    starting helper from an unavailable one.
  MailAuthorizationContext starting = MailContextAllOpen();
  starting.helper_ready = false;
  starting.helper_starting = true;
  starting.read_allowed = false;
  starting.global_policy = MailGlobalPolicy::kDeny;
  EXPECT_EQ(AuthorizeMailTool("mail_list_emails", starting).reason_code,
            "mail_helper_starting");

  MailAuthorizationContext down = starting;
  down.helper_starting = false;
  EXPECT_EQ(AuthorizeMailTool("mail_list_emails", down).reason_code,
            "mail_helper_unavailable");

  // 4. A deny policy precedes the read-consent gate.
  MailAuthorizationContext policy_deny = MailContextAllOpen();
  policy_deny.global_policy = MailGlobalPolicy::kDeny;
  policy_deny.read_allowed = false;
  EXPECT_EQ(AuthorizeMailTool("mail_list_emails", policy_deny).reason_code,
            "mail_global_policy_denied");

  // 5. Read consent is the last gate for read tools.
  MailAuthorizationContext no_consent = MailContextAllOpen();
  no_consent.read_allowed = false;
  no_consent.global_policy = MailGlobalPolicy::kPrompt;
  EXPECT_EQ(AuthorizeMailTool("mail_list_emails", no_consent).reason_code,
            "mail_read_consent_required");
  EXPECT_EQ(AuthorizeMailTool("mail_list_emails", MailContextAllOpen()).action,
            MailAuthorizationAction::kAllow);
}

TEST(MahoMailToolAuthorizationTest, WritesNeverInheritGlobalAllow) {
  // A global allow plus read consent still resolves writes to a typed,
  // per-call approval rather than an allow.
  EXPECT_EQ(AuthorizeMailTool("mail_send", MailContextAllOpen()).action,
            MailAuthorizationAction::kRequireApproval);
  EXPECT_EQ(AuthorizeMailTool("mail_send", MailContextAllOpen()).reason_code,
            "mail_typed_approval_required");
  EXPECT_EQ(AuthorizeMailTool("mail_add_account", MailContextAllOpen()).reason_code,
            "mail_typed_approval_required");

  // Writes are not subject to the read-consent gate, but the earlier gates
  // still apply to them in the same order.
  MailAuthorizationContext no_consent = MailContextAllOpen();
  no_consent.read_allowed = false;
  EXPECT_EQ(AuthorizeMailTool("mail_send", no_consent).reason_code,
            "mail_typed_approval_required");

  MailAuthorizationContext feature_off = MailContextAllOpen();
  feature_off.feature_enabled = false;
  EXPECT_EQ(AuthorizeMailTool("mail_send", feature_off).reason_code,
            "mail_feature_disabled");
}

TEST(MahoMailToolAuthorizationTest, ToolInventoryIsClassified) {
  EXPECT_EQ(kMailReadTools.size(), 7u);
  EXPECT_EQ(kMailWriteAccountTools.size(), 12u);
  for (std::string_view tool : kMailReadTools) {
    EXPECT_EQ(ClassifyMailTool(tool), MailToolClass::kRead) << tool;
  }
  for (std::string_view tool : kMailWriteAccountTools) {
    EXPECT_EQ(ClassifyMailTool(tool), MailToolClass::kWriteAccount) << tool;
  }
  EXPECT_EQ(ClassifyMailTool("mail_unknown_future_tool"),
            MailToolClass::kNotMail);
}

}  // namespace
}  // namespace maho::ai
