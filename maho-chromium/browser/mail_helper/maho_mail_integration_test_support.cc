// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/mail_helper/maho_mail_integration_test_support.h"

#include "base/check.h"

namespace maho::test {

MailReplyBarrier::MailReplyBarrier() = default;
MailReplyBarrier::~MailReplyBarrier() = default;

void MailReplyBarrier::Park(base::OnceClosure reply) {
  CHECK(reply_.is_null());
  CHECK(reply);
  reply_ = std::move(reply);
}

void MailReplyBarrier::Release() {
  CHECK(reply_);
  std::move(reply_).Run();
}

void MailReplyBarrier::Drop() {
  reply_.Reset();
}

}  // namespace maho::test
