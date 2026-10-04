// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/net/maho_shield_site_state.h"

#include <optional>
#include <string>
#include <utility>

#include "base/json/json_reader.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/third_party/maho/maho_bridge.h"

namespace maho {

namespace {

base::flat_set<std::string> ParseSiteExceptionKeys(const std::string& json) {
  base::flat_set<std::string> keys;
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    return keys;
  }

  for (const auto& value : parsed->GetList()) {
    if (value.is_dict()) {
      const std::string* key = value.GetDict().FindString("key");
      if (key) {
        const std::string normalized_key =
            maho::core::NormalizeSiteExceptionKey(*key);
        if (!normalized_key.empty()) {
          keys.insert(normalized_key);
        }
      }
    } else if (value.is_string()) {
      const std::string normalized_key =
          maho::core::NormalizeSiteExceptionKey(value.GetString());
      if (!normalized_key.empty()) {
        keys.insert(normalized_key);
      }
    }
  }
  return keys;
}

bool ContainsNormalizedSiteExceptionKey(const base::flat_set<std::string>& keys,
                                        const std::string& origin) {
  const std::string normalized_key =
      maho::core::NormalizeSiteExceptionKey(origin);
  return !normalized_key.empty() && keys.contains(normalized_key);
}

}  // namespace

MahoShieldSiteState::OverlayState::OverlayState() = default;
MahoShieldSiteState::OverlayState::OverlayState(const OverlayState&) =
    default;
MahoShieldSiteState::OverlayState&
MahoShieldSiteState::OverlayState::operator=(const OverlayState&) = default;
MahoShieldSiteState::OverlayState::OverlayState(OverlayState&&) noexcept =
    default;
MahoShieldSiteState::OverlayState&
MahoShieldSiteState::OverlayState::operator=(OverlayState&&) noexcept =
    default;
MahoShieldSiteState::OverlayState::~OverlayState() = default;

MahoShieldSiteState::MahoShieldSiteState(Profile* profile)
    : profile_(profile) {
  if (profile_ && profile_->IsOffTheRecord()) {
    OverlayState overlay;
    MahoCore* core = maho::GetCore();
    if (core) {
      overlay.content_blocking_mode = maho::core::GetContentBlockingMode(core);
      overlay.site_exceptions =
          ParseSiteExceptionKeys(maho::core::GetSiteExceptions(core));
    }
    overlay_ = std::move(overlay);
  }
}

MahoShieldSiteState::~MahoShieldSiteState() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

int MahoShieldSiteState::GetEffectiveContentBlockingMode() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (UsesOverlay()) {
    return overlay_->content_blocking_mode;
  }

  MahoCore* core = maho::GetCore();
  return core ? maho::core::GetContentBlockingMode(core) : -1;
}

MahoShieldCapabilities MahoShieldSiteState::GetCapabilities() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  MahoShieldCapabilities capabilities;
  capabilities.site_exception_toggle = GetEffectiveContentBlockingMode() == 0;
  return capabilities;
}

bool MahoShieldSiteState::IsSiteExceptedForOrigin(
    const std::string& origin) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (origin.empty()) {
    return false;
  }

  if (UsesOverlay()) {
    return ContainsNormalizedSiteExceptionKey(overlay_->site_exceptions, origin);
  }

  MahoCore* core = maho::GetCore();
  if (!core) {
    return false;
  }
  return ContainsNormalizedSiteExceptionKey(
      ParseSiteExceptionKeys(maho::core::GetSiteExceptions(core)), origin);
}

void MahoShieldSiteState::AddSiteException(const std::string& origin) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (origin.empty()) {
    return;
  }

  if (UsesOverlay()) {
    const std::string key = maho::core::NormalizeSiteExceptionKey(origin);
    if (!key.empty()) {
      overlay_->site_exceptions.insert(key);
    }
    return;
  }

  MahoCore* core = maho::GetCore();
  if (core) {
    maho::core::AddSiteException(core, origin.c_str());
  }
}

void MahoShieldSiteState::RemoveSiteException(const std::string& origin) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (origin.empty()) {
    return;
  }

  if (UsesOverlay()) {
    const std::string key = maho::core::NormalizeSiteExceptionKey(origin);
    if (!key.empty()) {
      overlay_->site_exceptions.erase(key);
    }
    return;
  }

  MahoCore* core = maho::GetCore();
  if (core) {
    maho::core::RemoveSiteException(core, origin.c_str());
  }
}

bool MahoShieldSiteState::UsesOverlay() const {
  return overlay_.has_value();
}

}  // namespace maho
