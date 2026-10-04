// Copyright 2026 The Maho Authors. All rights reserved.

#ifndef MAHO_BROWSER_MCP_MAHO_MCP_CAPABILITY_REGISTRY_H_
#define MAHO_BROWSER_MCP_MAHO_MCP_CAPABILITY_REGISTRY_H_

#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "base/sequence_checker.h"
#include "url/origin.h"

namespace maho {

class MahoMcpCapabilityRegistry {
 public:
  MahoMcpCapabilityRegistry();
  ~MahoMcpCapabilityRegistry();

  MahoMcpCapabilityRegistry(const MahoMcpCapabilityRegistry&) = delete;
  MahoMcpCapabilityRegistry& operator=(const MahoMcpCapabilityRegistry&) = delete;

  bool GrantExactOrigin(const url::Origin& origin);
  bool RevokeExactOrigin(const url::Origin& origin);
  bool IsOriginGranted(const url::Origin& origin) const;
  std::set<url::Origin> ListGrantedOrigins() const;
  bool IsValidGrantOrigin(const url::Origin& origin) const;

  enum class AdoptResult { kOk, kAlreadyOwnedByThisSession, kConflict };
  AdoptResult AdoptTab(int tab_id);
  void ReleaseTab(int tab_id);
  bool IsTabOwned(int tab_id) const;
  std::set<int> ListOwnedTabs() const;

  void Clear();

  // Returns all active registries. The navigation throttle iterates
  // this set to find the owning registry for a given tab.
  static const std::vector<MahoMcpCapabilityRegistry*>& ActiveRegistries();

 private:
  std::set<url::Origin> granted_origins_;
  std::set<int> owned_tab_ids_;
  void RegisterActive();
  void UnregisterActive();

  SEQUENCE_CHECKER(sequence_checker_);
};

}  // namespace maho

#endif  // MAHO_BROWSER_MCP_MAHO_MCP_CAPABILITY_REGISTRY_H_
