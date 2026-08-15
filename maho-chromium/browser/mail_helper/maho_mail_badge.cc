// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/mail_helper/maho_mail_badge.h"

#include <algorithm>

#include "base/strings/string_number_conversions.h"

namespace maho {

MailUnreadBadgeState::MailUnreadBadgeState() = default;
MailUnreadBadgeState::~MailUnreadBadgeState() = default;

MailBadgePresentation ResolveMailBadgePresentation(bool mail_enabled,
                                                   bool badge_enabled,
                                                   int unread_count) {
  MailBadgePresentation presentation;
  presentation.accessible_name = u"Mail";
  if (!mail_enabled || !badge_enabled || unread_count <= 0) {
    return presentation;
  }

  presentation.text = unread_count > 99
                          ? u"99+"
                          : base::NumberToString16(unread_count);
  presentation.accessible_name = u"Mail, " + presentation.text +
                                 (unread_count == 1 ? u" unread message"
                                                    : u" unread messages");
  presentation.visible = true;
  return presentation;
}

uint64_t MailUnreadBadgeState::BeginRefresh(const std::string& account_id) {
  const uint64_t token = ++next_request_token_;
  latest_request_tokens_[account_id] = token;
  return token;
}

bool MailUnreadBadgeState::CompleteRefresh(const std::string& account_id,
                                           uint64_t request_token,
                                           int64_t unread_count) {
  const auto request = latest_request_tokens_.find(account_id);
  if (request == latest_request_tokens_.end() ||
      request->second != request_token) {
    return false;
  }
  unread_counts_[account_id] = std::max<int64_t>(0, unread_count);
  return true;
}

bool MailUnreadBadgeState::RetainAccounts(
    const std::set<std::string>& account_ids) {
  bool changed = false;
  for (auto it = unread_counts_.begin(); it != unread_counts_.end();) {
    if (!account_ids.contains(it->first)) {
      it = unread_counts_.erase(it);
      changed = true;
    } else {
      ++it;
    }
  }
  for (auto it = latest_request_tokens_.begin();
       it != latest_request_tokens_.end();) {
    if (!account_ids.contains(it->first)) {
      it = latest_request_tokens_.erase(it);
    } else {
      ++it;
    }
  }
  return changed;
}

bool MailUnreadBadgeState::RemoveAccount(const std::string& account_id) {
  latest_request_tokens_.erase(account_id);
  return unread_counts_.erase(account_id) != 0;
}

void MailUnreadBadgeState::Clear() {
  latest_request_tokens_.clear();
  unread_counts_.clear();
}

int64_t MailUnreadBadgeState::total() const {
  int64_t total = 0;
  for (const auto& [account_id, unread_count] : unread_counts_) {
    total += unread_count;
  }
  return total;
}

}  // namespace maho
