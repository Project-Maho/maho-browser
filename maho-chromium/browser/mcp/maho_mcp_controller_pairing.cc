// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "maho/browser/mcp/maho_mcp_controller_pairing.h"

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "base/containers/span.h"
#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "base/process/process.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/task/thread_pool.h"
#include "base/uuid.h"
#include "build/build_config.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "crypto/random.h"
#include "crypto/secure_util.h"
#include "crypto/sha2.h"

#if BUILDFLAG(IS_WIN)
#include <windows.h>

#include <aclapi.h>
#include <sddl.h>

#include "base/win/access_token.h"
#include "base/win/sid.h"
#endif

namespace maho {
namespace {

constexpr size_t kTokenSize = 32;

std::string HashToken(std::string_view token) {
  return base::HexEncode(crypto::SHA256HashString(token));
}

bool DigestEquals(const std::string& a, const std::string& b) {
  return a.size() == b.size() &&
         crypto::SecureMemEqual(base::as_byte_span(a), base::as_byte_span(b));
}

constexpr char kKindKey[] = "kind";
constexpr char kNameKey[] = "name";
constexpr char kHashKey[] = "hash";
constexpr char kPairedAtKey[] = "paired_at";
constexpr char kLastUsedAtKey[] = "last_used_at";
constexpr char kRevokedKey[] = "revoked";

bool IsValidControllerId(std::string_view id) {
  return id.size() == 32 &&
         std::ranges::all_of(id, [](char c) { return base::IsHexDigit(c); });
}

double ToJsMs(base::Time time) {
  return time.InMillisecondsFSinceUnixEpoch();
}

#if BUILDFLAG(IS_WIN)
// Protected DACL with full access for the current user only (same policy as
// the DPAPI session-token file).
bool RestrictToCurrentUser(const base::FilePath& path) {
  const std::optional<base::win::AccessToken> token =
      base::win::AccessToken::FromCurrentProcess();
  if (!token) {
    return false;
  }
  const std::optional<std::wstring> sid = token->User().ToSddlString();
  if (!sid) {
    return false;
  }
  const std::wstring sddl = L"D:P(A;;FA;;;" + *sid + L")";
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  if (!::ConvertStringSecurityDescriptorToSecurityDescriptorW(
          sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr)) {
    return false;
  }
  const BOOL ok = ::SetFileSecurityW(path.value().c_str(),
                                     DACL_SECURITY_INFORMATION, descriptor);
  ::LocalFree(descriptor);
  return ok;
}
#endif

}  // namespace

std::string MahoMcpControllerPairing::Issue(base::DictValue& prefs) {
  std::array<uint8_t, kTokenSize> token_bytes = {};
  crypto::RandBytes(token_bytes);
  std::string token = base::HexEncode(token_bytes);
  base::ListValue* hashes = prefs.FindList(kPrefName);
  if (!hashes) {
    hashes = &prefs.Set(kPrefName, base::ListValue())->GetList();
  }
  hashes->Append(HashToken(token));
  return token;
}

bool MahoMcpControllerPairing::Verify(const base::DictValue& prefs,
                                      const std::string& token) {
  const base::ListValue* hashes = prefs.FindList(kPrefName);
  if (!hashes || token.empty()) {
    return false;
  }
  const std::string digest = HashToken(token);
  for (const base::Value& value : *hashes) {
    const std::string* stored_digest = value.GetIfString();
    if (stored_digest && DigestEquals(*stored_digest, digest)) {
      return true;
    }
  }
  return false;
}

bool MahoMcpControllerPairing::Revoke(base::DictValue& prefs,
                                      const std::string& token) {
  base::ListValue* hashes = prefs.FindList(kPrefName);
  if (!hashes || token.empty()) {
    return false;
  }
  const std::string digest = HashToken(token);
  bool removed = false;
  hashes->EraseIf([&](const base::Value& value) {
    const std::string* stored_digest = value.GetIfString();
    const bool matches = stored_digest && DigestEquals(*stored_digest, digest);
    removed |= matches;
    return matches;
  });
  return removed;
}

// static
std::string MahoMcpControllerPairing::NewControllerId() {
  std::array<uint8_t, 16> bytes = {};
  crypto::RandBytes(bytes);
  return base::ToLowerASCII(base::HexEncode(bytes));
}

// static
std::string MahoMcpControllerPairing::NewToken() {
  std::array<uint8_t, kTokenSize> bytes = {};
  crypto::RandBytes(bytes);
  return base::ToLowerASCII(base::HexEncode(bytes));
}

// static
void MahoMcpControllerPairing::StoreController(base::DictValue& controllers,
                                               std::string_view controller_id,
                                               std::string_view kind,
                                               std::string_view name,
                                               std::string_view token,
                                               base::Time now) {
  base::DictValue record;
  record.Set(kKindKey, kind);
  record.Set(kNameKey, name);
  record.Set(kHashKey, HashToken(token));
  record.Set(kPairedAtKey, ToJsMs(now));
  record.Set(kLastUsedAtKey, ToJsMs(now));
  controllers.Set(controller_id, std::move(record));
}

// static
MahoMcpControllerPairing::VerifyResult
MahoMcpControllerPairing::VerifyController(
    const base::DictValue& controllers,
    std::string_view controller_id,
    std::string_view kind,
    std::string_view token) {
  if (!IsValidControllerId(controller_id) || token.empty()) {
    return VerifyResult::kUnknown;
  }
  const base::DictValue* record = controllers.FindDict(controller_id);
  if (!record) {
    return VerifyResult::kUnknown;
  }
  if (record->FindBool(kRevokedKey).value_or(false)) {
    return VerifyResult::kRevoked;
  }
  const std::string* stored_kind = record->FindString(kKindKey);
  const std::string* stored_hash = record->FindString(kHashKey);
  if (!stored_kind || *stored_kind != kind || !stored_hash ||
      !DigestEquals(*stored_hash, HashToken(token))) {
    return VerifyResult::kUnknown;
  }
  return VerifyResult::kPaired;
}

// static
bool MahoMcpControllerPairing::RevokeController(base::DictValue& controllers,
                                                std::string_view controller_id) {
  base::DictValue* record = controllers.FindDict(controller_id);
  if (!record || record->FindBool(kRevokedKey).value_or(false)) {
    return false;
  }
  record->Remove(kHashKey);
  record->Set(kRevokedKey, true);
  return true;
}

// static
void MahoMcpControllerPairing::TouchController(base::DictValue& controllers,
                                               std::string_view controller_id,
                                               base::Time now) {
  if (base::DictValue* record = controllers.FindDict(controller_id)) {
    record->Set(kLastUsedAtKey, ToJsMs(now));
  }
}

// static
base::ListValue MahoMcpControllerPairing::ListControllers(
    const base::DictValue& controllers) {
  base::ListValue list;
  for (const auto [id, value] : controllers) {
    const base::DictValue* record = value.GetIfDict();
    if (!record || record->FindBool(kRevokedKey).value_or(false)) {
      continue;
    }
    const std::string* kind = record->FindString(kKindKey);
    const std::string* name = record->FindString(kNameKey);
    base::DictValue item;
    item.Set("controllerId", id);
    item.Set("kind", kind ? *kind : std::string());
    item.Set("name", name ? *name : std::string());
    item.Set("pairedAt", record->FindDouble(kPairedAtKey).value_or(0));
    item.Set("lastUsedAt", record->FindDouble(kLastUsedAtKey).value_or(0));
    list.Append(std::move(item));
  }
  return list;
}

// static
std::string MahoMcpControllerPairing::ControllerKind(
    const base::DictValue& controllers,
    std::string_view controller_id) {
  const base::DictValue* record = controllers.FindDict(controller_id);
  const std::string* kind = record ? record->FindString(kKindKey) : nullptr;
  return kind ? *kind : std::string();
}

// static
std::string MahoMcpControllerPairing::SanitizeKind(std::string_view kind) {
  const bool valid =
      !kind.empty() && kind.size() <= 32 &&
      std::ranges::all_of(kind, [](char c) {
        return base::IsAsciiLower(c) || base::IsAsciiDigit(c) || c == '-';
      });
  return valid ? std::string(kind) : std::string("third-party");
}

// static
base::FilePath MahoMcpControllerPairing::TokenFilePath(
    const base::FilePath& user_data_dir,
    std::string_view kind,
    std::string_view controller_id) {
  return user_data_dir.AppendASCII("controllers")
      .AppendASCII(SanitizeKind(kind))
      .AppendASCII(base::StrCat({controller_id, ".token"}));
}

// static
bool MahoMcpControllerPairing::WriteTokenFile(const base::FilePath& path,
                                              std::string_view token) {
  const base::FilePath dir = path.DirName();
  if (!base::CreateDirectory(dir)) {
    return false;
  }
#if BUILDFLAG(IS_POSIX)
  if (!base::SetPosixFilePermissions(dir.DirName(), 0700) ||
      !base::SetPosixFilePermissions(dir, 0700)) {
    return false;
  }
#endif
  const base::FilePath temp = path.AddExtensionASCII("tmp");
  base::DeleteFile(temp);
  {
    // FLAG_CREATE fails if the file exists; base::File creates POSIX files
    // with 0600.
    base::File file(temp, base::File::FLAG_CREATE | base::File::FLAG_WRITE);
    if (!file.IsValid() || !file.WriteAtCurrentPosAndCheck(
                               base::as_byte_span(token))) {
      base::DeleteFile(temp);
      return false;
    }
  }
#if BUILDFLAG(IS_WIN)
  if (!RestrictToCurrentUser(temp)) {
    base::DeleteFile(temp);
    return false;
  }
#endif
  if (!base::ReplaceFile(temp, path, nullptr)) {
    base::DeleteFile(temp);
    return false;
  }
  return true;
}

// static
bool MahoMcpControllerPairing::DeleteTokenFile(const base::FilePath& path) {
  return base::DeleteFile(path);
}

MahoMcpPairingGateDecision DecideMahoMcpPairingGate(
    bool gate_enabled,
    bool session_paired,
    bool session_revoked,
    const MahoBrowserToolRegistry::CapabilityDescriptor* descriptor) {
  if (!gate_enabled) {
    return MahoMcpPairingGateDecision::kAllow;
  }
  const bool discovery =
      descriptor &&
      descriptor->mutability == MahoBrowserToolRegistry::Mutability::kReadOnly &&
      descriptor->sensitivity == MahoBrowserToolRegistry::Sensitivity::kLow;
  if (discovery) {
    return MahoMcpPairingGateDecision::kAllow;
  }
  if (session_revoked) {
    return MahoMcpPairingGateDecision::kRevoked;
  }
  return session_paired ? MahoMcpPairingGateDecision::kAllow
                        : MahoMcpPairingGateDecision::kPairingRequired;
}

bool IsMahoMcpPeerBrowserOwned(base::ProcessId pid) {
  const base::ProcessId browser = base::GetCurrentProcId();
  base::ProcessId current = pid;
  // The browser-owned agent runs maho-browser-mcp a few levels below the
  // browser; walk a bounded number of ancestors.
  for (int depth = 0; depth < 6 && current > 0; ++depth) {
    if (current == browser) {
      return true;
    }
    base::Process process = base::Process::Open(current);
    if (!process.IsValid()) {
      return false;
    }
    const base::ProcessId parent = base::GetParentProcessId(process.Handle());
    if (parent == current) {
      return false;
    }
    current = parent;
  }
  return false;
}

struct MahoMcpPairingCoordinator::Pending {
  std::string kind;
  std::string name;
  std::vector<OutcomeCallback> awaiters;
  base::OneShotTimer timeout;
};

// static
MahoMcpPairingCoordinator* MahoMcpPairingCoordinator::Get() {
  static base::NoDestructor<MahoMcpPairingCoordinator> coordinator;
  return coordinator.get();
}

// static
void MahoMcpPairingCoordinator::RegisterLocalStatePrefs(
    PrefRegistrySimple* registry) {
  registry->RegisterDictionaryPref(MahoMcpControllerPairing::kControllersPref);
}

MahoMcpPairingCoordinator::MahoMcpPairingCoordinator() {
  DETACH_FROM_SEQUENCE(sequence_checker_);
}
MahoMcpPairingCoordinator::~MahoMcpPairingCoordinator() = default;

void MahoMcpPairingCoordinator::Configure(PrefService* local_state,
                                          base::FilePath user_data_dir,
                                          Presenter presenter) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  local_state_ = local_state;
  user_data_dir_ = std::move(user_data_dir);
  presenter_ = std::move(presenter);
}

void MahoMcpPairingCoordinator::Reset() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  weak_factory_.InvalidateWeakPtrs();
  pending_.clear();
  finished_.clear();
  local_state_ = nullptr;
  user_data_dir_.clear();
  presenter_.Reset();
  DETACH_FROM_SEQUENCE(sequence_checker_);
}

MahoMcpControllerPairing::VerifyResult MahoMcpPairingCoordinator::Verify(
    std::string_view controller_id,
    std::string_view kind,
    std::string_view token) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!local_state_) {
    return MahoMcpControllerPairing::VerifyResult::kUnknown;
  }
  const auto result = MahoMcpControllerPairing::VerifyController(
      local_state_->GetDict(MahoMcpControllerPairing::kControllersPref),
      controller_id, kind, token);
  if (result == MahoMcpControllerPairing::VerifyResult::kPaired) {
    ScopedDictPrefUpdate update(local_state_,
                                MahoMcpControllerPairing::kControllersPref);
    MahoMcpControllerPairing::TouchController(update.Get(), controller_id,
                                              base::Time::Now());
  }
  return result;
}

std::string MahoMcpPairingCoordinator::RequestPairing(const std::string& kind,
                                                      const std::string& name) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  for (const auto& [id, pending] : pending_) {
    if (pending->kind == kind && pending->name == name) {
      return id;
    }
  }
  const std::string request_id =
      base::Uuid::GenerateRandomV4().AsLowercaseString();
  auto pending = std::make_unique<Pending>();
  pending->kind = kind;
  pending->name = name;
  pending->timeout.Start(
      FROM_HERE, kPendingTimeout,
      base::BindOnce(&MahoMcpPairingCoordinator::Finish,
                     weak_factory_.GetWeakPtr(), request_id,
                     Outcome{Status::kExpired, std::string()}));
  pending_[request_id] = std::move(pending);
  if (!local_state_ || !presenter_) {
    // Fail closed: nobody can approve, so the request is denied.
    Finish(request_id, Outcome{Status::kDenied, std::string()});
    return request_id;
  }
  presenter_.Run(kind, name,
                 base::BindOnce(&MahoMcpPairingCoordinator::OnDecision,
                                weak_factory_.GetWeakPtr(), request_id));
  return request_id;
}

void MahoMcpPairingCoordinator::OnDecision(const std::string& request_id,
                                           bool approved) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto it = pending_.find(request_id);
  if (it == pending_.end()) {
    return;  // Already expired.
  }
  if (!approved || !local_state_) {
    Finish(request_id, Outcome{Status::kDenied, std::string()});
    return;
  }
  const std::string controller_id = MahoMcpControllerPairing::NewControllerId();
  const std::string token = MahoMcpControllerPairing::NewToken();
  const base::FilePath path = MahoMcpControllerPairing::TokenFilePath(
      user_data_dir_, it->second->kind, controller_id);
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::TaskPriority::USER_BLOCKING,
       base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN},
      base::BindOnce(&MahoMcpControllerPairing::WriteTokenFile, path, token),
      base::BindOnce(&MahoMcpPairingCoordinator::OnTokenWritten,
                     weak_factory_.GetWeakPtr(), request_id, controller_id,
                     token));
}

void MahoMcpPairingCoordinator::OnTokenWritten(const std::string& request_id,
                                               const std::string& controller_id,
                                               const std::string& token,
                                               bool written) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto it = pending_.find(request_id);
  if (it == pending_.end() || !local_state_) {
    return;
  }
  if (!written) {
    Finish(request_id, Outcome{Status::kDenied, std::string()});
    return;
  }
  {
    ScopedDictPrefUpdate update(local_state_,
                                MahoMcpControllerPairing::kControllersPref);
    MahoMcpControllerPairing::StoreController(
        update.Get(), controller_id, it->second->kind, it->second->name, token,
        base::Time::Now());
  }
  Finish(request_id, Outcome{Status::kApproved, controller_id});
}

void MahoMcpPairingCoordinator::Finish(const std::string& request_id,
                                       Outcome outcome) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto it = pending_.find(request_id);
  if (it == pending_.end()) {
    return;
  }
  std::vector<OutcomeCallback> awaiters = std::move(it->second->awaiters);
  pending_.erase(it);
  finished_[request_id] = outcome;
  for (OutcomeCallback& awaiter : awaiters) {
    std::move(awaiter).Run(outcome);
  }
}

void MahoMcpPairingCoordinator::Await(const std::string& request_id,
                                      OutcomeCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (auto it = pending_.find(request_id); it != pending_.end()) {
    it->second->awaiters.push_back(std::move(callback));
    return;
  }
  std::move(callback).Run(GetOutcome(request_id));
}

MahoMcpPairingCoordinator::Outcome MahoMcpPairingCoordinator::GetOutcome(
    const std::string& request_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (pending_.contains(request_id)) {
    return Outcome{Status::kPending, std::string()};
  }
  auto it = finished_.find(request_id);
  return it == finished_.end() ? Outcome{} : it->second;
}

base::ListValue MahoMcpPairingCoordinator::List() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!local_state_) {
    return base::ListValue();
  }
  return MahoMcpControllerPairing::ListControllers(
      local_state_->GetDict(MahoMcpControllerPairing::kControllersPref));
}

bool MahoMcpPairingCoordinator::Revoke(const std::string& controller_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!local_state_) {
    return false;
  }
  const std::string kind = MahoMcpControllerPairing::ControllerKind(
      local_state_->GetDict(MahoMcpControllerPairing::kControllersPref),
      controller_id);
  bool revoked = false;
  {
    ScopedDictPrefUpdate update(local_state_,
                                MahoMcpControllerPairing::kControllersPref);
    revoked =
        MahoMcpControllerPairing::RevokeController(update.Get(), controller_id);
  }
  if (revoked && !kind.empty()) {
    base::ThreadPool::PostTask(
        FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
        base::BindOnce(base::IgnoreResult(
                           &MahoMcpControllerPairing::DeleteTokenFile),
                       MahoMcpControllerPairing::TokenFilePath(
                           user_data_dir_, kind, controller_id)));
  }
  return revoked;
}

}  // namespace maho
