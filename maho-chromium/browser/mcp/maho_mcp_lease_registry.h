// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MAHO_BROWSER_MCP_MAHO_MCP_LEASE_REGISTRY_H_
#define MAHO_BROWSER_MCP_MAHO_MCP_LEASE_REGISTRY_H_

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "base/synchronization/lock.h"
#include "base/time/time.h"

namespace maho {

// OQ-3 per-tab lease registry: mediates exclusive tab ownership across
// concurrent MCP sessions. TTL-based with heartbeat renewal and
// authorized force-steal semantics.
//
// Owned by the browser process (one instance per MahoMcpSocketServer /
// MahoMcpPipeServer). Passed by pointer to each MahoMcpSession; must
// outlive all sessions. Not thread-safe: all methods run on the IO
// thread that owns the socket/pipe accept loop.
//
// Time discipline (skeptic-preserved from OQ-3 debate): all timestamps
// use base::TimeTicks::Now() (monotonic). Never wall-clock. This isolates
// the lease from user clock adjustments and NTP jumps.
class MahoMcpLeaseRegistry {
 public:
  // Recorded audit event returned by GetAuditLog for testing.
  struct AuditEntry {
    AuditEntry();
    ~AuditEntry();
    AuditEntry(const AuditEntry&);
    AuditEntry& operator=(const AuditEntry&);
    AuditEntry(AuditEntry&&);
    AuditEntry& operator=(AuditEntry&&);

    std::string session_id;   // Session that ACTED (acquired/stole/etc).
    std::string previous;     // Previous holder, empty if none.
    std::string action;       // "acquire" | "release" | "steal" | "expire"
    int64_t tab_id = 0;
    base::TimeTicks at;
  };

  struct AcquireResult {
    AcquireResult();
    ~AcquireResult();
    AcquireResult(const AcquireResult&);
    AcquireResult& operator=(const AcquireResult&);
    AcquireResult(AcquireResult&&);
    AcquireResult& operator=(AcquireResult&&);

    bool ok = false;
    // Set for both !ok contention and successful force-steal.
    std::string previous_holder;
    std::string message;         // Empty on ok.
    base::TimeTicks expires_at;  // Only meaningful when ok.
  };

  MahoMcpLeaseRegistry();
  ~MahoMcpLeaseRegistry();

  MahoMcpLeaseRegistry(const MahoMcpLeaseRegistry&) = delete;
  MahoMcpLeaseRegistry& operator=(const MahoMcpLeaseRegistry&) = delete;

  // Acquire an exclusive lease on |tab_id| for |session_id|. |ttl| is
  // clamped to [10s, 300s]. If another session holds a still-valid lease
  // and |force_steal| is false, returns {ok=false, previous_holder=...}.
  // If |force_steal| is true, the prior holder is audit-logged, evicted, and
  // returned as {ok=true, previous_holder=...}.
  AcquireResult Acquire(int64_t tab_id,
                        const std::string& session_id,
                        base::TimeDelta ttl,
                        bool force_steal);

  // Extend the lease if held by |session_id|. Returns new expires_at on
  // success. Returns default TimeTicks() on failure.
  base::TimeTicks Heartbeat(int64_t tab_id,
                            const std::string& session_id,
                            base::TimeDelta ttl);

  // Release the lease. No-op if |session_id| does not hold it.
  bool Release(int64_t tab_id, const std::string& session_id);

  // Called when a socket session drops. Marks all leases held by
  // |session_id| for a 5-second grace period before auto-release.
  void OnSessionDropped(const std::string& session_id);

  // Predicate used by mutating tool handlers: returns true iff
  // |session_id| currently holds a valid (not-expired) lease on
  // |tab_id|.
  bool IsHeldBy(int64_t tab_id, const std::string& session_id);

  // Test hooks.
  size_t active_lease_count();
  std::vector<AuditEntry> GetAuditLog() const;

 private:
  struct LeaseInfo {
    std::string session_id;
    base::TimeTicks acquired_at;
    base::TimeTicks expires_at;
    // Non-zero when the holder session dropped; the lease auto-releases
    // when |grace_until| passes (drop + 5s).
    base::TimeTicks grace_until;
  };

  // Sweep expired leases lazily on any query. Returns true if the lease
  // for |tab_id| survived; false if it was purged.
  bool SweepAndCheck(int64_t tab_id);

  std::map<int64_t, LeaseInfo> leases_;
  std::vector<AuditEntry> audit_log_;

  mutable base::Lock lock_;
};

}  // namespace maho

#endif  // MAHO_BROWSER_MCP_MAHO_MCP_LEASE_REGISTRY_H_
