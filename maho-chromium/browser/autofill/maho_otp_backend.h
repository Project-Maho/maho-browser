// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_AUTOFILL_MAHO_OTP_BACKEND_H_
#define MAHO_BROWSER_AUTOFILL_MAHO_OTP_BACKEND_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/types/expected.h"
#include "components/one_time_tokens/core/browser/one_time_token.h"
#include "components/one_time_tokens/core/browser/one_time_token_retrieval_error.h"
#include "components/one_time_tokens/core/browser/sms_otp_backend.h"

class KeyedService;
class Profile;

namespace one_time_tokens {
class GmailOtpBackend;
}  // namespace one_time_tokens

namespace maho::autofill {

// Desktop one-time-code source for Chromium's OneTimeTokenService, which only
// ships an Android SMS backend. Asks the Maho mail helper for the newest code
// in macOS Messages, then in Maho Mail. Codes are never logged.
class MahoOtpBackend : public one_time_tokens::SmsOtpBackend {
 public:
  static constexpr int64_t kMaxAgeSeconds = 10 * 60;

  using Callback = base::OnceCallback<void(
      base::expected<one_time_tokens::OneTimeToken,
                     one_time_tokens::OneTimeTokenRetrievalError>)>;
  using ReplyCallback = base::OnceCallback<void(bool, std::string)>;
  // Runs one mail-helper OTP lookup with the given max age (seconds).
  using OtpQuery = base::RepeatingCallback<void(int64_t, ReplyCallback)>;

  MahoOtpBackend(OtpQuery messages_query, OtpQuery mail_query);
  ~MahoOtpBackend() override;

  // nullptr when the profile has no MahoMailService.
  static std::unique_ptr<MahoOtpBackend> CreateForProfile(Profile* profile);

  // Returns the code from a mail-helper reply (`[{"field","name","value"}]`)
  // when it is a plausible OTP, std::nullopt otherwise.
  static std::optional<std::string> ParseOtpReply(bool ok,
                                                  const std::string& json);

  // one_time_tokens::SmsOtpBackend:
  void RetrieveSmsOtp(Callback callback) override;

 private:
  void OnMessagesReply(Callback callback, bool ok, std::string json);
  void OnMailReply(Callback callback, bool ok, std::string json);

  OtpQuery messages_query_;
  OtpQuery mail_query_;
  base::WeakPtrFactory<MahoOtpBackend> weak_ptr_factory_{this};
};

// OneTimeTokenService for `profile` that owns a MahoOtpBackend when no
// platform SMS backend exists.
std::unique_ptr<KeyedService> BuildMahoOneTimeTokenService(
    Profile* profile,
    one_time_tokens::SmsOtpBackend* platform_sms_backend,
    one_time_tokens::GmailOtpBackend* gmail_backend);

}  // namespace maho::autofill

#endif  // MAHO_BROWSER_AUTOFILL_MAHO_OTP_BACKEND_H_
