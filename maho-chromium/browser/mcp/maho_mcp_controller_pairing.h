// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MAHO_BROWSER_MCP_MAHO_MCP_CONTROLLER_PAIRING_H_
#define MAHO_BROWSER_MCP_MAHO_MCP_CONTROLLER_PAIRING_H_

#include <string>

#include "base/values.h"

namespace maho {

class MahoMcpControllerPairing {
 public:
  static constexpr char kPrefName[] = "maho.mcp.controller_token_hashes";

  static std::string Issue(base::Value::Dict& prefs);

  static bool Verify(const base::Value::Dict& prefs,
                     const std::string& token);

  static bool Revoke(base::Value::Dict& prefs, const std::string& token);
};

}  // namespace maho

#endif  // MAHO_BROWSER_MCP_MAHO_MCP_CONTROLLER_PAIRING_H_
