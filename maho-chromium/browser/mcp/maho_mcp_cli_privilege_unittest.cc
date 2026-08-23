// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "maho/browser/mcp/maho_mcp_session.h"

#include <unistd.h>

#include <memory>
#include <string>

#include "build/build_config.h"
#include "maho/browser/mcp/maho_mcp_lease_registry.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho {
namespace {

std::string TrustedCliExecutableForTesting() {
#if BUILDFLAG(IS_APPLE)
  return "/Applications/Maho.app/Contents/Helpers/maho";
#else
  return "/usr/bin/maho";
#endif
}

std::unique_ptr<MahoMcpSession> MakeCliSession(MahoMcpLeaseRegistry* leases,
                                              bool trusted) {
  return std::make_unique<MahoMcpSession>(
      getuid(), leases, TrustedCliExecutableForTesting(), trusted);
}

void InitializeAsCli(MahoMcpSession* session) {
  const auto responses = session->ProcessData(
      R"({"jsonrpc":"2.0","method":"initialize","id":1,)"
      R"("params":{"protocolVersion":"2025-03-26",)"
      R"("controllerKind":"maho-cli",)"
      R"("clientInfo":{"name":"maho-cli","version":"1"}}})"
      "\n");
  ASSERT_EQ(responses.size(), 1u);
  ASSERT_NE(responses[0].find("\"controllerKind\":\"maho-cli\""),
            std::string::npos)
      << responses[0];
}

TEST(MahoMcpCliPrivilegeTest,
     TrustedCliCanUseExplicitCliControlCapabilitiesThroughToolsCall) {
  MahoMcpLeaseRegistry leases;
  auto session = MakeCliSession(&leases, true);
  InitializeAsCli(session.get());

  const auto acquire = session->ProcessData(
      R"({"jsonrpc":"2.0","method":"tools/call","id":2,)"
      R"("params":{"name":"browser_acquire_lease",)"
      R"("arguments":{"tab_id":17,"ttl_seconds":60}}})"
      "\n");
  ASSERT_EQ(acquire.size(), 1u);
  EXPECT_EQ(acquire[0].find("-32601"), std::string::npos) << acquire[0];
  EXPECT_NE(acquire[0].find("\"acquired\":true"), std::string::npos)
      << acquire[0];

  // The desktop agent addresses browser tools by canonical capability id.
  // Trusted CLI sessions normalize that id, but only inside the explicitly
  // catalogued CLI+ControlPlane intersection.
  const auto release = session->ProcessData(
      R"({"jsonrpc":"2.0","method":"tools/call","id":3,)"
      R"("params":{"name":"lease.release",)"
      R"("arguments":{"tab_id":17}}})"
      "\n");
  ASSERT_EQ(release.size(), 1u);
  EXPECT_EQ(release[0].find("-32601"), std::string::npos) << release[0];
  EXPECT_NE(release[0].find("\"released\":true"), std::string::npos)
      << release[0];
}

TEST(MahoMcpCliPrivilegeTest,
     UnauthenticatedControllerHintCannotElevatePublicToolsCall) {
  MahoMcpLeaseRegistry leases;
  auto session = MakeCliSession(&leases, false);
  const auto initialize = session->ProcessData(
      R"({"jsonrpc":"2.0","method":"initialize","id":1,)"
      R"("params":{"protocolVersion":"2025-03-26",)"
      R"("controllerKind":"maho-cli",)"
      R"("clientInfo":{"name":"maho-cli","version":"1"}}})"
      "\n");
  ASSERT_EQ(initialize.size(), 1u);
  EXPECT_NE(initialize[0].find("\"controllerKind\":\"third-party\""),
            std::string::npos)
      << initialize[0];

  const auto acquire = session->ProcessData(
      R"({"jsonrpc":"2.0","method":"tools/call","id":2,)"
      R"("params":{"name":"browser_acquire_lease",)"
      R"("arguments":{"tab_id":17}}})"
      "\n");
  ASSERT_EQ(acquire.size(), 1u);
  EXPECT_NE(acquire[0].find("-32601"), std::string::npos) << acquire[0];
  EXPECT_NE(acquire[0].find("not available on the public MCP"),
            std::string::npos)
      << acquire[0];
}

}  // namespace
}  // namespace maho
