// Copyright 2026 The Maho Authors. All rights reserved.

#include "maho/browser/mcp/maho_mcp_capability_registry.h"

#include "base/no_destructor.h"
#include "base/notreached.h"
#include "url/gurl.h"

namespace maho {

namespace {

std::vector<MahoMcpCapabilityRegistry*>& ActiveRegistriesStorage() {
  static base::NoDestructor<std::vector<MahoMcpCapabilityRegistry*>>
      active_registries;
  return *active_registries;
}

}  // namespace

MahoMcpCapabilityRegistry::MahoMcpCapabilityRegistry() {
  // The registry is constructed on the socket IO sequence (in
  // MahoMcpSession's ctor, from MahoMcpSocketServer::OnAcceptComplete), but all
  // of its data methods are invoked on the UI thread
  // (MahoMcpSession::ProcessData is posted to content::GetUIThreadTaskRunner).
  // Detach here so the sequence checker binds to the UI thread on first use
  // rather than to the IO construction sequence.
  DETACH_FROM_SEQUENCE(sequence_checker_);
  RegisterActive();
}
MahoMcpCapabilityRegistry::~MahoMcpCapabilityRegistry() {
  // Destroyed on the socket IO sequence. The owned_tab_ids_/granted_origins_
  // members auto-clear on destruction, so do NOT call Clear() here: it asserts
  // the UI data sequence, which the dtor does not run on.
  UnregisterActive();
}

// static
const std::vector<MahoMcpCapabilityRegistry*>&
MahoMcpCapabilityRegistry::ActiveRegistries() {
  return ActiveRegistriesStorage();
}

void MahoMcpCapabilityRegistry::RegisterActive() {
  auto& registries = ActiveRegistriesStorage();
  auto it = std::find(registries.begin(), registries.end(), this);
  if (it == registries.end())
    registries.push_back(this);
}

void MahoMcpCapabilityRegistry::UnregisterActive() {
  auto& registries = ActiveRegistriesStorage();
  auto it = std::find(registries.begin(), registries.end(), this);
  if (it != registries.end())
    registries.erase(it);
}

bool MahoMcpCapabilityRegistry::IsValidGrantOrigin(
    const url::Origin& origin) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (origin.opaque()) return false;
  const std::string& scheme = origin.scheme();
  if (scheme != "https" && scheme != "http") return false;
  if (origin.host().empty()) return false;
  if (origin.host().find('*') != std::string::npos) return false;
  return true;
}

bool MahoMcpCapabilityRegistry::GrantExactOrigin(const url::Origin& origin) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsValidGrantOrigin(origin)) return false;
  return granted_origins_.insert(origin).second;
}

bool MahoMcpCapabilityRegistry::RevokeExactOrigin(const url::Origin& origin) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return granted_origins_.erase(origin) > 0;
}

bool MahoMcpCapabilityRegistry::IsOriginGranted(
    const url::Origin& origin) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return granted_origins_.contains(origin);
}

std::set<url::Origin> MahoMcpCapabilityRegistry::ListGrantedOrigins() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return granted_origins_;
}

MahoMcpCapabilityRegistry::AdoptResult MahoMcpCapabilityRegistry::AdoptTab(
    int tab_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (owned_tab_ids_.contains(tab_id))
    return AdoptResult::kAlreadyOwnedByThisSession;
  owned_tab_ids_.insert(tab_id);
  return AdoptResult::kOk;
}

void MahoMcpCapabilityRegistry::ReleaseTab(int tab_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  owned_tab_ids_.erase(tab_id);
}

bool MahoMcpCapabilityRegistry::IsTabOwned(int tab_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return owned_tab_ids_.contains(tab_id);
}

std::set<int> MahoMcpCapabilityRegistry::ListOwnedTabs() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return owned_tab_ids_;
}

void MahoMcpCapabilityRegistry::Clear() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  granted_origins_.clear();
  owned_tab_ids_.clear();
}

}  // namespace maho
