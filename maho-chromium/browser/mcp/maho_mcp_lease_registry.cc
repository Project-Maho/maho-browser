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

MahoMcpLeaseRegistry::BorrowTicket::BorrowTicket() = default;
MahoMcpLeaseRegistry::BorrowTicket::~BorrowTicket() = default;
MahoMcpLeaseRegistry::BorrowTicket::BorrowTicket(const BorrowTicket&) = default;
MahoMcpLeaseRegistry::BorrowTicket&
MahoMcpLeaseRegistry::BorrowTicket::operator=(const BorrowTicket&) = default;
MahoMcpLeaseRegistry::BorrowTicket::BorrowTicket(BorrowTicket&&) = default;
MahoMcpLeaseRegistry::BorrowTicket&
MahoMcpLeaseRegistry::BorrowTicket::operator=(BorrowTicket&&) = default;

namespace {

constexpr base::TimeDelta kMinTtl = base::Seconds(10);
constexpr base::TimeDelta kMaxTtl = base::Seconds(300);
constexpr base::TimeDelta kSocketDropGrace = base::Seconds(5);
constexpr size_t kMaxAuditEntries = 4096;
constexpr uint64_t kMaxAuditPayloadBytes = 1024 * 1024;

base::TimeDelta ClampTtl(base::TimeDelta ttl) {
  return std::clamp(ttl, kMinTtl, kMaxTtl);
}

}  // namespace

MahoMcpLeaseRegistry::MahoMcpLeaseRegistry() {
}

MahoMcpLeaseRegistry::~MahoMcpLeaseRegistry() = default;

void MahoMcpLeaseRegistry::PushAuditEntry(AuditEntry entry) {
  const size_t entry_bytes =
      entry.session_id.size() + entry.previous.size() + entry.action.size();
  audit_payload_bytes_ += entry_bytes;
  audit_log_.push_back(std::move(entry));

  while ((audit_log_.size() > kMaxAuditEntries ||
          audit_payload_bytes_ > kMaxAuditPayloadBytes) &&
         !audit_log_.empty()) {
    const auto& front = audit_log_.front();
    const size_t front_bytes =
        front.session_id.size() + front.previous.size() + front.action.size();
    if (audit_payload_bytes_ >= front_bytes) {
      audit_payload_bytes_ -= front_bytes;
    } else {
      audit_payload_bytes_ = 0;
    }
    audit_log_.pop_front();
  }
}

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
    entry.lease_epoch = info.epoch;
    PushAuditEntry(std::move(entry));
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

  std::string previous_holder;
  const bool held = SweepAndCheck(tab_id);
  if (held) {
    LeaseInfo& info = leases_[tab_id];
    if (info.session_id == session_id) {
      info.expires_at = now + clamped;
      info.grace_until = {};
      info.epoch = next_epoch_++;
      AcquireResult result;
      result.ok = true;
      result.expires_at = info.expires_at;
      result.lease_epoch = info.epoch;
      AuditEntry entry;
      entry.session_id = session_id;
      entry.previous = session_id;
      entry.action = "acquire";
      entry.tab_id = tab_id;
      entry.at = now;
      entry.lease_epoch = info.epoch;
      PushAuditEntry(std::move(entry));
      return result;
    }
    if (!force_steal) {
      AcquireResult result;
      result.ok = false;
      result.previous_holder = info.session_id;
      result.message = "tab already leased";
      return result;
    }
    previous_holder = info.session_id;
    const uint64_t steal_epoch = next_epoch_++;
    AuditEntry steal_entry;
    steal_entry.session_id = session_id;
    steal_entry.previous = info.session_id;
    steal_entry.action = "steal";
    steal_entry.tab_id = tab_id;
    steal_entry.at = now;
    steal_entry.lease_epoch = steal_epoch;
    PushAuditEntry(std::move(steal_entry));

    info.session_id = session_id;
    info.acquired_at = now;
    info.expires_at = now + clamped;
    info.grace_until = {};
    info.epoch = steal_epoch;

    AcquireResult result;
    result.ok = true;
    result.previous_holder = previous_holder;
    result.expires_at = info.expires_at;
    result.lease_epoch = steal_epoch;
    return result;
  }

  const uint64_t new_epoch = next_epoch_++;
  LeaseInfo new_info;
  new_info.session_id = session_id;
  new_info.acquired_at = now;
  new_info.expires_at = now + clamped;
  new_info.epoch = new_epoch;
  leases_[tab_id] = new_info;

  AuditEntry acquire_entry;
  acquire_entry.session_id = session_id;
  acquire_entry.action = "acquire";
  acquire_entry.tab_id = tab_id;
  acquire_entry.at = now;
  acquire_entry.lease_epoch = new_epoch;
  PushAuditEntry(std::move(acquire_entry));

  AcquireResult result;
  result.ok = true;
  result.previous_holder = previous_holder;
  result.expires_at = new_info.expires_at;
  result.lease_epoch = new_epoch;
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
  entry.lease_epoch = it->second.epoch;
  PushAuditEntry(std::move(entry));
  leases_.erase(it);
  return true;
}

bool MahoMcpLeaseRegistry::CancelLeaseOnDisconnect(
    int64_t tab_id,
    const std::string& session_id) {
  base::AutoLock lock(lock_);
  auto it = leases_.find(tab_id);
  if (it == leases_.end() || it->second.session_id != session_id) {
    return false;
  }
  AuditEntry entry;
  entry.session_id = session_id;
  entry.previous = session_id;
  entry.action = "cancel";
  entry.tab_id = tab_id;
  entry.at = base::TimeTicks::Now();
  entry.lease_epoch = it->second.epoch;
  PushAuditEntry(std::move(entry));
  leases_.erase(it);
  return true;
}

uint64_t MahoMcpLeaseRegistry::LeaseEpoch(int64_t tab_id) {
  base::AutoLock lock(lock_);
  if (!SweepAndCheck(tab_id)) {
    return 0;
  }
  return leases_[tab_id].epoch;
}

void MahoMcpLeaseRegistry::OnSessionDropped(const std::string& session_id) {
  base::AutoLock lock(lock_);
  const base::TimeTicks now = base::TimeTicks::Now();
  for (auto& [tab_id, info] : leases_) {
    if (info.session_id == session_id && info.grace_until.is_null()) {
      info.grace_until = now + kSocketDropGrace;
    }
  }
  std::vector<int64_t> dropped_borrows;
  for (const auto& [tab_id, ticket] : borrow_tickets_) {
    if (ticket.session_id == session_id) {
      dropped_borrows.push_back(tab_id);
    }
  }
  for (int64_t tab_id : dropped_borrows) {
    auto it = borrow_tickets_.find(tab_id);
    if (it != borrow_tickets_.end()) {
      AuditEntry entry;
      entry.session_id = session_id;
      entry.action = "return";
      entry.tab_id = tab_id;
      entry.at = now;
      entry.lease_epoch = it->second.epoch;
      PushAuditEntry(std::move(entry));
      borrow_tickets_.erase(it);
    }
  }
}

std::optional<MahoMcpLeaseRegistry::BorrowTicket>
MahoMcpLeaseRegistry::TryBorrow(int64_t tab_id,
                                std::string_view session_id,
                                int64_t origin_space_id,
                                int64_t agent_space_id,
                                base::TimeDelta ttl) {
  base::AutoLock lock(lock_);
  SweepExpiredBorrows();
  auto it = borrow_tickets_.find(tab_id);
  if (it != borrow_tickets_.end()) {
    return std::nullopt;
  }

  const base::TimeTicks now = base::TimeTicks::Now();
  const base::TimeDelta clamped = ClampTtl(ttl);
  const uint64_t epoch = next_epoch_++;

  BorrowTicket ticket;
  ticket.session_id = std::string(session_id);
  ticket.tab_id = tab_id;
  ticket.origin_space_id = origin_space_id;
  ticket.agent_space_id = agent_space_id;
  ticket.borrowed_at = now;
  ticket.expires_at = now + clamped;
  ticket.epoch = epoch;

  borrow_tickets_[tab_id] = ticket;

  AuditEntry entry;
  entry.session_id = ticket.session_id;
  entry.action = "borrow";
  entry.tab_id = tab_id;
  entry.at = now;
  entry.lease_epoch = epoch;
  PushAuditEntry(std::move(entry));

  return ticket;
}

bool MahoMcpLeaseRegistry::ReturnBorrowed(int64_t tab_id,
                                          std::string_view session_id) {
  base::AutoLock lock(lock_);
  auto it = borrow_tickets_.find(tab_id);
  if (it == borrow_tickets_.end() || it->second.session_id != session_id) {
    return false;
  }
  AuditEntry entry;
  entry.session_id = std::string(session_id);
  entry.action = "return";
  entry.tab_id = tab_id;
  entry.at = base::TimeTicks::Now();
  entry.lease_epoch = it->second.epoch;
  PushAuditEntry(std::move(entry));
  borrow_tickets_.erase(it);
  return true;
}

std::optional<MahoMcpLeaseRegistry::BorrowTicket>
MahoMcpLeaseRegistry::GetBorrowTicket(int64_t tab_id) const {
  base::AutoLock lock(lock_);
  auto it = borrow_tickets_.find(tab_id);
  if (it == borrow_tickets_.end()) {
    return std::nullopt;
  }
  if (base::TimeTicks::Now() > it->second.expires_at) {
    return std::nullopt;
  }
  return it->second;
}

size_t MahoMcpLeaseRegistry::SweepExpiredBorrows() {
  const base::TimeTicks now = base::TimeTicks::Now();
  std::vector<int64_t> expired_ids;
  for (const auto& [tab_id, ticket] : borrow_tickets_) {
    if (now > ticket.expires_at) {
      expired_ids.push_back(tab_id);
    }
  }
  for (int64_t tab_id : expired_ids) {
    auto it = borrow_tickets_.find(tab_id);
    if (it != borrow_tickets_.end()) {
      AuditEntry entry;
      entry.session_id = it->second.session_id;
      entry.action = "borrow_expire";
      entry.tab_id = tab_id;
      entry.at = now;
      entry.lease_epoch = it->second.epoch;
      PushAuditEntry(std::move(entry));
      borrow_tickets_.erase(it);
    }
  }
  return expired_ids.size();
}

bool MahoMcpLeaseRegistry::IsHeldBy(int64_t tab_id,
                                     const std::string& session_id) {
  base::AutoLock lock(lock_);
  if (!SweepAndCheck(tab_id)) {
    return false;
  }
  return leases_[tab_id].session_id == session_id;
}

bool MahoMcpLeaseRegistry::HasActiveLease(int64_t tab_id) {
  base::AutoLock lock(lock_);
  return SweepAndCheck(tab_id);
}

size_t MahoMcpLeaseRegistry::active_lease_count() {
  base::AutoLock lock(lock_);
  return leases_.size();
}

std::vector<MahoMcpLeaseRegistry::AuditEntry>
MahoMcpLeaseRegistry::GetAuditLog() const {
  base::AutoLock lock(lock_);
  return std::vector<AuditEntry>(audit_log_.begin(), audit_log_.end());
}

}  // namespace maho
