// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_CHROMIUM_BROWSER_NET_MAHO_SHIELD_SITE_STATE_H_
#define MAHO_CHROMIUM_BROWSER_NET_MAHO_SHIELD_SITE_STATE_H_

#include <optional>
#include <string>

#include "base/containers/flat_set.h"
#include "base/memory/raw_ptr.h"
#include "base/sequence_checker.h"
#include "components/keyed_service/core/keyed_service.h"

class Profile;

namespace maho {

struct MahoShieldCapabilities {
  bool site_exception_toggle = false;
  bool block_scripts = false;
  bool block_fingerprinting = false;
  bool https_upgrade = false;
  bool block_cookies = false;
  bool category_counts = false;
};

class MahoShieldSiteState : public KeyedService {
 public:
  explicit MahoShieldSiteState(Profile* profile);
  ~MahoShieldSiteState() override;

  MahoShieldSiteState(const MahoShieldSiteState&) = delete;
  MahoShieldSiteState& operator=(const MahoShieldSiteState&) = delete;

  int GetEffectiveContentBlockingMode() const;
  MahoShieldCapabilities GetCapabilities() const;
  bool IsSiteExceptedForOrigin(const std::string& origin) const;
  void AddSiteException(const std::string& origin);
  void RemoveSiteException(const std::string& origin);

 private:
  struct OverlayState {
    OverlayState();
    OverlayState(const OverlayState&);
    OverlayState& operator=(const OverlayState&);
    OverlayState(OverlayState&&) noexcept;
    OverlayState& operator=(OverlayState&&) noexcept;
    ~OverlayState();

    int content_blocking_mode = -1;
    base::flat_set<std::string> site_exceptions;
  };

  bool UsesOverlay() const;

  raw_ptr<Profile> profile_ = nullptr;
  std::optional<OverlayState> overlay_;

  SEQUENCE_CHECKER(sequence_checker_);
};

}  // namespace maho

#endif  // MAHO_CHROMIUM_BROWSER_NET_MAHO_SHIELD_SITE_STATE_H_
