// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_MAIL_HELPER_MAHO_MAIL_BADGE_H_
#define MAHO_BROWSER_MAIL_HELPER_MAHO_MAIL_BADGE_H_

#include <cstdint>
#include <map>
#include <set>
#include <string>

namespace maho {

struct MailBadgePresentation {
  std::u16string text;
  std::u16string accessible_name;
  bool visible = false;
};

MailBadgePresentation ResolveMailBadgePresentation(bool mail_enabled,
                                                   bool badge_enabled,
                                                   int unread_count);

// Tracks per-account unread refreshes. A completion is accepted only when its
// token is still the newest request for that account, preventing a late older
// callback from restoring stale badge state.
class MailUnreadBadgeState {
 public:
  MailUnreadBadgeState();
  ~MailUnreadBadgeState();

  uint64_t BeginRefresh(const std::string& account_id);
  bool CompleteRefresh(const std::string& account_id,
                       uint64_t request_token,
                       int64_t unread_count);
  bool RetainAccounts(const std::set<std::string>& account_ids);
  bool RemoveAccount(const std::string& account_id);
  void Clear();
  int64_t total() const;

 private:
  uint64_t next_request_token_ = 0;
  std::map<std::string, uint64_t> latest_request_tokens_;
  std::map<std::string, int64_t> unread_counts_;
};

}  // namespace maho

#endif  // MAHO_BROWSER_MAIL_HELPER_MAHO_MAIL_BADGE_H_
