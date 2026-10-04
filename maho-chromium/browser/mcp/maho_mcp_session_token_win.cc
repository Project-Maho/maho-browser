// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef UNSAFE_BUFFERS_BUILD
// Win32 API interop uses raw buffers / pointer arithmetic (memset, .data(),
// pointer offsets) that cannot be expressed with bounds-checked spans.
#pragma allow_unsafe_buffers
#endif

#include "maho/browser/mcp/maho_mcp_session_token_win.h"

#if BUILDFLAG(IS_WIN)

#include <windows.h>

#include <bcrypt.h>
#include <dpapi.h>
#include <sddl.h>
#include <wincrypt.h>

#include <algorithm>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/important_file_writer.h"
#include "base/logging.h"
#include "base/path_service.h"
#include "crypto/random.h"
#include "third_party/boringssl/src/include/openssl/mem.h"

namespace maho {

namespace {

// Retrieve the current user's SID as a string for ACL construction.
std::wstring GetCurrentUserSidString() {
  HANDLE token = nullptr;
  if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token)) {
    LOG(ERROR) << "MCP token: OpenProcessToken failed: " << ::GetLastError();
    return std::wstring();
  }

  DWORD token_info_len = 0;
  ::GetTokenInformation(token, TokenUser, nullptr, 0, &token_info_len);

  std::vector<BYTE> token_info_buffer(token_info_len);
  if (!::GetTokenInformation(token, TokenUser, token_info_buffer.data(),
                             token_info_len, &token_info_len)) {
    LOG(ERROR) << "MCP token: GetTokenInformation failed: "
               << ::GetLastError();
    ::CloseHandle(token);
    return std::wstring();
  }
  ::CloseHandle(token);

  TOKEN_USER* token_user =
      reinterpret_cast<TOKEN_USER*>(token_info_buffer.data());
  LPWSTR sid_string = nullptr;
  if (!::ConvertSidToStringSidW(token_user->User.Sid, &sid_string)) {
    LOG(ERROR) << "MCP token: ConvertSidToStringSidW failed: "
               << ::GetLastError();
    return std::wstring();
  }

  std::wstring result(sid_string);
  ::LocalFree(sid_string);
  return result;
}

// Set the file's ACL to allow only the current user Full Access.
// SDDL: D:P(A;;FA;;;<user-sid>) — Protected DACL, Full Access for user only.
bool SetFileCurrentUserOnly(const base::FilePath& path) {
  std::wstring sid_string = GetCurrentUserSidString();
  if (sid_string.empty()) {
    return false;
  }

  std::wstring sddl = L"D:P(A;;FA;;;" + sid_string + L")";

  PSECURITY_DESCRIPTOR sd = nullptr;
  if (!::ConvertStringSecurityDescriptorToSecurityDescriptorW(
          sddl.c_str(), SDDL_REVISION_1, &sd, nullptr)) {
    LOG(ERROR) << "MCP token: SDDL conversion failed: " << ::GetLastError();
    return false;
  }

  BOOL ok = ::SetFileSecurityW(
      path.value().c_str(), DACL_SECURITY_INFORMATION, sd);
  ::LocalFree(sd);

  if (!ok) {
    LOG(ERROR) << "MCP token: SetFileSecurity failed: " << ::GetLastError();
    return false;
  }
  return true;
}

}  // namespace

MahoMcpSessionToken::MahoMcpSessionToken() = default;
MahoMcpSessionToken::~MahoMcpSessionToken() = default;

// static
std::unique_ptr<MahoMcpSessionToken> MahoMcpSessionToken::Create() {
  auto token = std::unique_ptr<MahoMcpSessionToken>(
      new MahoMcpSessionToken());
  token->token_.resize(kTokenSize);
  crypto::RandBytes(base::span(token->token_));
  return token;
}

bool MahoMcpSessionToken::WriteEncryptedToDisk(const base::FilePath& path) {
  // Encrypt with DPAPI (user-scope: CRYPTPROTECT_LOCAL_MACHINE NOT set).
  DATA_BLOB input_blob;
  input_blob.pbData = token_.data();
  input_blob.cbData = static_cast<DWORD>(token_.size());

  DATA_BLOB output_blob = {};
  if (!::CryptProtectData(&input_blob, L"Maho MCP Session Token",
                          /*pOptionalEntropy=*/nullptr,
                          /*pvReserved=*/nullptr,
                          /*pPromptStruct=*/nullptr,
                          /*dwFlags=*/0, &output_blob)) {
    LOG(ERROR) << "MCP token: CryptProtectData failed: " << ::GetLastError();
    return false;
  }

  std::string ciphertext(reinterpret_cast<char*>(output_blob.pbData),
                         output_blob.cbData);
  ::LocalFree(output_blob.pbData);

  // Ensure parent directory exists.
  base::FilePath dir = path.DirName();
  if (!base::CreateDirectory(dir)) {
    LOG(ERROR) << "MCP token: Failed to create directory: "
               << dir.AsUTF8Unsafe();
    return false;
  }

  // Atomic write via temp file + rename.
  if (!base::ImportantFileWriter::WriteFileAtomically(path, ciphertext)) {
    LOG(ERROR) << "MCP token: Atomic write failed";
    return false;
  }

  // Restrict ACL to current user only.
  if (!SetFileCurrentUserOnly(path)) {
    // Non-fatal but concerning — log and continue.
    LOG(WARNING) << "MCP token: Failed to restrict file ACL";
  }

  return true;
}

bool MahoMcpSessionToken::Matches(base::span<const uint8_t> presented) const {
  if (presented.size() != kTokenSize) {
    return false;
  }
  // Constant-time comparison via BoringSSL's CRYPTO_memcmp.
  return CRYPTO_memcmp(token_.data(), presented.data(), kTokenSize) == 0;
}

// static
base::FilePath MahoMcpSessionToken::GetSessionTokenPath() {
  base::FilePath local_app_data;
  if (!base::PathService::Get(base::DIR_LOCAL_APP_DATA, &local_app_data)) {
    LOG(ERROR) << "MCP token: Failed to get DIR_LOCAL_APP_DATA";
    return base::FilePath();
  }
  return local_app_data.AppendASCII("Maho").AppendASCII("mcp-session-token");
}

}  // namespace maho

#endif  // BUILDFLAG(IS_WIN)
