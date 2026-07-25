// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "maho/browser/mcp/maho_mcp_lease_registry.h"

#include <algorithm>

namespace maho {

MahoMcpLeaseRegistry::AuditEntry::AuditEntry() = default;
MahoMcpLeaseRegistry::AuditEntry::~AuditEntry() = default;
MahoMcpLeaseRegistry::AuditEntry::AuditEntry(const AuditEntry&) = default;
MahoMcpLeaseRegistry::AuditEntry&
MahoMcpLeaseRegistry::AuditEntry::operator=(const AuditEntry&) = default;
MahoMcpLeaseRegistry::AuditEntry::AuditEntry(AuditEntry&&) = default;
MahoMcpLeaseRegistry::AuditEntry&
MahoMcpLeaseRegistry::AuditEntry::operator=(AuditEntry&&) = default;

MahoMcpLeaseRegistry::AcquireResult::AcquireResult() = default;
MahoMcpLeaseRegistry::AcquireResult::~AcquireResult() = default;
MahoMcpLeaseRegistry::AcquireResult::AcquireResult(const AcquireResult&) =
    default;
MahoMcpLeaseRegistry::AcquireResult&
MahoMcpLeaseRegistry::AcquireResult::operator=(const AcquireResult&) = default;
MahoMcpLeaseRegistry::AcquireResult::AcquireResult(AcquireResult&&) = default;
MahoMcpLeaseRegistry::AcquireResult&
MahoMcpLeaseRegistry::AcquireResult::operator=(AcquireResult&&) = default;

namespace {

constexpr base::TimeDelta kMinTtl = base::Seconds(10);
constexpr base::TimeDelta kMaxTtl = base::Seconds(300);
constexpr base::TimeDelta kSocketDropGrace = base::Seconds(5);

base::TimeDelta ClampTtl(base::TimeDelta ttl) {
  return std::clamp(ttl, kMinTtl, kMaxTtl);
}

}  // namespace

MahoMcpLeaseRegistry::MahoMcpLeaseRegistry() {
}

MahoMcpLeaseRegistry::~MahoMcpLeaseRegistry() = default;

bool MahoMcpLeaseRegistry::SweepAndCheck(int64_t tab_id) {
  auto it = leases_.find(tab_id);
  if (it == leases_.end()) {
    return false;
  }
  const base::TimeTicks now = base::TimeTicks::Now();
  const LeaseInfo& info = it->second;
  const bool ttl_expired = now > info.expires_at;
  const bool grace_expired =
      !info.grace_until.is_null() && now > info.grace_until;
  if (ttl_expired || grace_expired) {
    AuditEntry entry;
    entry.session_id = info.session_id;
    entry.previous = info.session_id;
    entry.action = "expire";
    entry.tab_id = tab_id;
    entry.at = now;
    audit_log_.push_back(std::move(entry));
    leases_.erase(it);
    return false;
  }
  return true;
}

MahoMcpLeaseRegistry::AcquireResult MahoMcpLeaseRegistry::Acquire(
    int64_t tab_id,
    const std::string& session_id,
    base::TimeDelta ttl,
    bool force_steal) {
  base::AutoLock lock(lock_);
  const base::TimeDelta clamped = ClampTtl(ttl);
  const base::TimeTicks now = base::TimeTicks::Now();

  const bool held = SweepAndCheck(tab_id);
  if (held) {
    LeaseInfo& info = leases_[tab_id];
    if (info.session_id == session_id) {
      info.expires_at = now + clamped;
      AcquireResult result;
      result.ok = true;
      result.expires_at = info.expires_at;
      return result;
    }
    if (!force_steal) {
      AcquireResult result;
      result.ok = false;
      result.previous_holder = info.session_id;
      result.message = "tab already leased";
      return result;
    }
    AuditEntry steal_entry;
    steal_entry.session_id = session_id;
    steal_entry.previous = info.session_id;
    steal_entry.action = "steal";
    steal_entry.tab_id = tab_id;
    steal_entry.at = now;
    audit_log_.push_back(std::move(steal_entry));
  }

  LeaseInfo new_info;
  new_info.session_id = session_id;
  new_info.acquired_at = now;
  new_info.expires_at = now + clamped;
  leases_[tab_id] = new_info;

  AuditEntry acquire_entry;
  acquire_entry.session_id = session_id;
  acquire_entry.action = "acquire";
  acquire_entry.tab_id = tab_id;
  acquire_entry.at = now;
  audit_log_.push_back(std::move(acquire_entry));

  AcquireResult result;
  result.ok = true;
  result.expires_at = new_info.expires_at;
  return result;
}

base::TimeTicks MahoMcpLeaseRegistry::Heartbeat(
    int64_t tab_id,
    const std::string& session_id,
    base::TimeDelta ttl) {
  base::AutoLock lock(lock_);
  if (!SweepAndCheck(tab_id)) {
    return {};
  }
  LeaseInfo& info = leases_[tab_id];
  if (info.session_id != session_id) {
    return {};
  }
  info.expires_at = base::TimeTicks::Now() + ClampTtl(ttl);
  info.grace_until = {};
  return info.expires_at;
}

bool MahoMcpLeaseRegistry::Release(int64_t tab_id,
                                    const std::string& session_id) {
  base::AutoLock lock(lock_);
  auto it = leases_.find(tab_id);
  if (it == leases_.end() || it->second.session_id != session_id) {
    return false;
  }
  AuditEntry entry;
  entry.session_id = session_id;
  entry.previous = session_id;
  entry.action = "release";
  entry.tab_id = tab_id;
  entry.at = base::TimeTicks::Now();
  audit_log_.push_back(std::move(entry));
  leases_.erase(it);
  return true;
}

void MahoMcpLeaseRegistry::OnSessionDropped(const std::string& session_id) {
  base::AutoLock lock(lock_);
  const base::TimeTicks now = base::TimeTicks::Now();
  for (auto& [tab_id, info] : leases_) {
    if (info.session_id == session_id && info.grace_until.is_null()) {
      info.grace_until = now + kSocketDropGrace;
    }
  }
}

bool MahoMcpLeaseRegistry::IsHeldBy(int64_t tab_id,
                                     const std::string& session_id) {
  base::AutoLock lock(lock_);
  if (!SweepAndCheck(tab_id)) {
    return false;
  }
  return leases_[tab_id].session_id == session_id;
}

size_t MahoMcpLeaseRegistry::active_lease_count() {
  base::AutoLock lock(lock_);
  return leases_.size();
}

std::vector<MahoMcpLeaseRegistry::AuditEntry>
MahoMcpLeaseRegistry::GetAuditLog() const {
  base::AutoLock lock(lock_);
  return audit_log_;
}

}  // namespace maho
