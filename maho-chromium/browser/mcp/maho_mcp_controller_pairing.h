// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MAHO_BROWSER_MCP_MAHO_MCP_CONTROLLER_PAIRING_H_
#define MAHO_BROWSER_MCP_MAHO_MCP_CONTROLLER_PAIRING_H_

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/process/process_handle.h"
#include "base/sequence_checker.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "maho/browser/ai/maho_browser_tool_registry.h"

class PrefRegistrySimple;
class PrefService;

namespace maho {

// JSON-RPC errors of the controller-pairing protocol
// (.omo/drafts/aside-adoption/controller-pairing-protocol.md). -32010/-32011
// are taken by action-not-applied / native-input-unavailable.
inline constexpr int kMahoMcpErrorPairingRequired = -32014;
inline constexpr int kMahoMcpErrorPairingRevoked = -32015;

class MahoMcpControllerPairing {
 public:
  static constexpr char kPrefName[] = "maho.mcp.controller_token_hashes";

  static std::string Issue(base::DictValue& prefs);

  static bool Verify(const base::DictValue& prefs,
                     const std::string& token);

  static bool Revoke(base::DictValue& prefs, const std::string& token);

  // Per-controller records live in local state under kControllersPref:
  // {controller_id: {kind, name, hash, paired_at, last_used_at, revoked}}.
  // Only SHA-256(token) is stored; a revoked record keeps a tombstone without
  // the hash so a stale token is reported as revoked, not unknown.
  static constexpr char kControllersPref[] = "maho.mcp.controllers";

  enum class VerifyResult { kPaired, kRevoked, kUnknown };

  static std::string NewControllerId();
  static std::string NewToken();
  static void StoreController(base::DictValue& controllers,
                              std::string_view controller_id,
                              std::string_view kind,
                              std::string_view name,
                              std::string_view token,
                              base::Time now);
  static VerifyResult VerifyController(const base::DictValue& controllers,
                                       std::string_view controller_id,
                                       std::string_view kind,
                                       std::string_view token);
  static bool RevokeController(base::DictValue& controllers,
                               std::string_view controller_id);
  static void TouchController(base::DictValue& controllers,
                              std::string_view controller_id,
                              base::Time now);
  // [{controllerId, kind, name, pairedAt, lastUsedAt}] of live controllers.
  // Never contains token material.
  static base::ListValue ListControllers(
      const base::DictValue& controllers);
  // Returns the kind of a stored controller, or an empty string.
  static std::string ControllerKind(const base::DictValue& controllers,
                                    std::string_view controller_id);

  // Restricts a client-supplied controller kind to [a-z0-9-]{1,32}; anything
  // else becomes "third-party".
  static std::string SanitizeKind(std::string_view kind);

  // <user_data_dir>/controllers/<kind>/<controller_id>.token
  static base::FilePath TokenFilePath(const base::FilePath& user_data_dir,
                                      std::string_view kind,
                                      std::string_view controller_id);
  // Blocking. POSIX: directory 0700, file 0600 created exclusively under a
  // temporary name and renamed. Windows: file ACL limited to the current user.
  static bool WriteTokenFile(const base::FilePath& path,
                             std::string_view token);
  static bool DeleteTokenFile(const base::FilePath& path);
};

// Gate policy for one tool call. Read-only, low-sensitivity capabilities are
// discovery and never need pairing; everything else (including unknown
// capabilities) is a browser-control operation.
enum class MahoMcpPairingGateDecision { kAllow, kPairingRequired, kRevoked };
MahoMcpPairingGateDecision DecideMahoMcpPairingGate(
    bool gate_enabled,
    bool session_paired,
    bool session_revoked,
    const MahoBrowserToolRegistry::CapabilityDescriptor* descriptor);

// True when |pid| is the browser process or one of its descendants (the
// browser-owned agent runs maho-browser-mcp as a grandchild). Such peers are
// part of the browser and are not external controllers.
bool IsMahoMcpPeerBrowserOwned(base::ProcessId pid);

// UI-thread owner of pairing state: approval requests, token issuance and
// revocation. Unconfigured (no local state) means the gate is disabled.
class MahoMcpPairingCoordinator {
 public:
  static constexpr base::TimeDelta kPendingTimeout = base::Seconds(120);

  enum class Status { kPending, kApproved, kDenied, kExpired, kUnknown };
  struct Outcome {
    Status status = Status::kUnknown;
    std::string controller_id;
  };
  using OutcomeCallback = base::OnceCallback<void(const Outcome&)>;
  // Shows the approval UI for (kind, name) and reports the user's decision.
  using Presenter = base::RepeatingCallback<
      void(const std::string& kind,
           const std::string& name,
           base::OnceCallback<void(bool approved)> decision)>;

  static MahoMcpPairingCoordinator* Get();
  static void RegisterLocalStatePrefs(PrefRegistrySimple* registry);

  MahoMcpPairingCoordinator();
  ~MahoMcpPairingCoordinator();
  MahoMcpPairingCoordinator(const MahoMcpPairingCoordinator&) = delete;
  MahoMcpPairingCoordinator& operator=(const MahoMcpPairingCoordinator&) =
      delete;

  void Configure(PrefService* local_state,
                 base::FilePath user_data_dir,
                 Presenter presenter);
  void Reset();
  bool enabled() const { return local_state_ != nullptr; }

  MahoMcpControllerPairing::VerifyResult Verify(std::string_view controller_id,
                                                std::string_view kind,
                                                std::string_view token);
  // Starts (or returns the still-pending) approval request for a session.
  std::string RequestPairing(const std::string& kind, const std::string& name);
  void Await(const std::string& request_id, OutcomeCallback callback);
  Outcome GetOutcome(const std::string& request_id) const;
  base::ListValue List() const;
  bool Revoke(const std::string& controller_id);

 private:
  struct Pending;
  void OnDecision(const std::string& request_id, bool approved);
  void OnTokenWritten(const std::string& request_id,
                      const std::string& controller_id,
                      const std::string& token,
                      bool written);
  void Finish(const std::string& request_id, Outcome outcome);

  raw_ptr<PrefService> local_state_ = nullptr;
  base::FilePath user_data_dir_;
  Presenter presenter_;
  std::map<std::string, std::unique_ptr<Pending>> pending_;
  std::map<std::string, Outcome> finished_;
  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<MahoMcpPairingCoordinator> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_MCP_MAHO_MCP_CONTROLLER_PAIRING_H_
