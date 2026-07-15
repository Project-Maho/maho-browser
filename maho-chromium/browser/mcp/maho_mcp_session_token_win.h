// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MAHO_BROWSER_MCP_MAHO_MCP_SESSION_TOKEN_WIN_H_
#define MAHO_BROWSER_MCP_MAHO_MCP_SESSION_TOKEN_WIN_H_

#include "build/build_config.h"

#if BUILDFLAG(IS_WIN)

#include <memory>
#include <vector>

#include "base/containers/span.h"
#include "base/files/file_path.h"

namespace maho {

// Generates and manages a per-launch 32-byte random session token for MCP
// authentication on Windows.
//
// The token is encrypted with DPAPI (user-scope, not CRYPTPROTECT_LOCAL_MACHINE)
// and written to disk so that local MCP clients can read, decrypt, and present
// it during the `initialize` handshake. The file is ACL-restricted to the
// current user only.
//
// Lifecycle:
//   1. Browser calls Create() at startup — generates 32 random bytes.
//   2. WriteEncryptedToDisk() persists the DPAPI-encrypted ciphertext.
//   3. Each MCP session calls Matches() to validate the client's presented token.
//   4. Token is never persisted in cleartext; regenerated every launch.
class MahoMcpSessionToken {
 public:
  ~MahoMcpSessionToken();

  MahoMcpSessionToken(const MahoMcpSessionToken&) = delete;
  MahoMcpSessionToken& operator=(const MahoMcpSessionToken&) = delete;

  // Generates a new 32-byte random token using BCryptGenRandom.
  // Returns nullptr on failure.
  static std::unique_ptr<MahoMcpSessionToken> Create();

  // Encrypts the token with CryptProtectData (user-scope) and atomically
  // writes the ciphertext to |path|. Sets the file ACL to current-user-only.
  // Returns true on success.
  bool WriteEncryptedToDisk(const base::FilePath& path);

  // Constant-time comparison of |presented| against the stored token.
  // Returns false if lengths differ or bytes don't match.
  bool Matches(base::span<const uint8_t> presented) const;

  // Returns the canonical path for the session token file:
  // %LOCALAPPDATA%\Maho\mcp-session-token
  static base::FilePath GetSessionTokenPath();

  // Returns a span over the raw 32-byte token (for testing only).
  base::span<const uint8_t> raw_token_for_testing() const {
    return base::span(token_);
  }

  static constexpr size_t kTokenSize = 32;

 private:
  MahoMcpSessionToken();

  std::vector<uint8_t> token_;
};

}  // namespace maho

#endif  // BUILDFLAG(IS_WIN)

#endif  // MAHO_BROWSER_MCP_MAHO_MCP_SESSION_TOKEN_WIN_H_
