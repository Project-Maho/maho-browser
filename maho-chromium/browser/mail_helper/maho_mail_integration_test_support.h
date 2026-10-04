// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_MAIL_HELPER_MAHO_MAIL_INTEGRATION_TEST_SUPPORT_H_
#define MAHO_BROWSER_MAIL_HELPER_MAHO_MAIL_INTEGRATION_TEST_SUPPORT_H_

#include "base/functional/callback.h"

namespace maho::test {

// A manually released test reply. Tests park the exact callback before
// triggering work, then release it explicitly; no clock advancement, sleeps,
// or polling are involved.
class MailReplyBarrier {
 public:
  MailReplyBarrier();
  ~MailReplyBarrier();

  MailReplyBarrier(const MailReplyBarrier&) = delete;
  MailReplyBarrier& operator=(const MailReplyBarrier&) = delete;

  void Park(base::OnceClosure reply);
  bool has_reply() const { return !reply_.is_null(); }
  void Release();
  void Drop();

 private:
  base::OnceClosure reply_;
};

}  // namespace maho::test

#endif  // MAHO_BROWSER_MAIL_HELPER_MAHO_MAIL_INTEGRATION_TEST_SUPPORT_H_
