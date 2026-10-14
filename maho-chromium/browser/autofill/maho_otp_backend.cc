// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/autofill/maho_otp_backend.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "components/one_time_tokens/core/browser/one_time_token_service_impl.h"
#include "components/one_time_tokens/core/browser/one_time_token_type.h"
#include "maho/browser/mail_helper/maho_mail_service.h"
#include "maho/browser/mail_helper/maho_mail_service_factory.h"

namespace maho::autofill {

namespace {

using one_time_tokens::OneTimeToken;
using one_time_tokens::OneTimeTokenRetrievalError;

struct MahoOtpBackendHolder {
  std::unique_ptr<MahoOtpBackend> backend;
};

// The holder base is constructed before and destroyed after the service base,
// so the service's raw_ptr to the backend never dangles.
class MahoOneTimeTokenService : private MahoOtpBackendHolder,
                                public one_time_tokens::OneTimeTokenServiceImpl {
 public:
  MahoOneTimeTokenService(std::unique_ptr<MahoOtpBackend> backend,
                          one_time_tokens::GmailOtpBackend* gmail_backend)
      : MahoOtpBackendHolder{std::move(backend)},
        OneTimeTokenServiceImpl(MahoOtpBackendHolder::backend.get(),
                                gmail_backend) {}
};

}  // namespace

MahoOtpBackend::MahoOtpBackend(OtpQuery messages_query, OtpQuery mail_query)
    : messages_query_(std::move(messages_query)),
      mail_query_(std::move(mail_query)) {}

MahoOtpBackend::~MahoOtpBackend() = default;

// static
std::unique_ptr<MahoOtpBackend> MahoOtpBackend::CreateForProfile(
    Profile* profile) {
  maho::MahoMailService* service =
      maho::MahoMailServiceFactory::GetForProfile(profile);
  if (!service) {
    return nullptr;
  }
  // OneTimeTokenServiceFactory DependsOn MahoMailServiceFactory, so the mail
  // service outlives this backend.
  auto messages_query = base::BindRepeating(
      [](maho::MahoMailService* service, int64_t max_age,
         ReplyCallback reply) {
        service->ExtractMessagesOtp(max_age, /*page_origin=*/std::string(),
                                    std::move(reply));
      },
      base::Unretained(service));
  auto mail_query = base::BindRepeating(
      [](maho::MahoMailService* service, int64_t max_age,
         ReplyCallback reply) {
        service->ExtractOtp(/*account_id=*/std::string(),
                            /*folder_id=*/std::string(),
                            /*query=*/std::string(), max_age,
                            /*page_origin=*/std::string(), std::move(reply));
      },
      base::Unretained(service));
  return std::make_unique<MahoOtpBackend>(std::move(messages_query),
                                          std::move(mail_query));
}

// static
std::optional<std::string> MahoOtpBackend::ParseOtpReply(
    bool ok,
    const std::string& json) {
  if (!ok || json.empty()) {
    return std::nullopt;
  }
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    return std::nullopt;
  }
  for (const base::Value& entry : parsed->GetList()) {
    const base::DictValue* dict = entry.GetIfDict();
    if (!dict) {
      continue;
    }
    const std::string* value = dict->FindString("value");
    if (value && OneTimeToken::IsPotentialOtp(base::UTF8ToUTF16(*value))) {
      return *value;
    }
  }
  return std::nullopt;
}

void MahoOtpBackend::RetrieveSmsOtp(Callback callback) {
  messages_query_.Run(
      kMaxAgeSeconds,
      base::BindOnce(&MahoOtpBackend::OnMessagesReply,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

void MahoOtpBackend::OnMessagesReply(Callback callback,
                                     bool ok,
                                     std::string json) {
  if (std::optional<std::string> code = ParseOtpReply(ok, json)) {
    std::move(callback).Run(OneTimeToken(
        one_time_tokens::OneTimeTokenType::kSmsOtp, *code,
        base::TimeTicks::Now()));
    return;
  }
  mail_query_.Run(kMaxAgeSeconds,
                  base::BindOnce(&MahoOtpBackend::OnMailReply,
                                 weak_ptr_factory_.GetWeakPtr(),
                                 std::move(callback)));
}

void MahoOtpBackend::OnMailReply(Callback callback,
                                 bool ok,
                                 std::string json) {
  if (std::optional<std::string> code = ParseOtpReply(ok, json)) {
    std::move(callback).Run(OneTimeToken(
        one_time_tokens::OneTimeTokenType::kSmsOtp, *code,
        base::TimeTicks::Now()));
    return;
  }
  std::move(callback).Run(
      base::unexpected(OneTimeTokenRetrievalError::kSmsOtpBackendError));
}

std::unique_ptr<KeyedService> BuildMahoOneTimeTokenService(
    Profile* profile,
    one_time_tokens::SmsOtpBackend* platform_sms_backend,
    one_time_tokens::GmailOtpBackend* gmail_backend) {
  if (!platform_sms_backend) {
    if (std::unique_ptr<MahoOtpBackend> backend =
            MahoOtpBackend::CreateForProfile(profile)) {
      return std::make_unique<MahoOneTimeTokenService>(std::move(backend),
                                                       gmail_backend);
    }
  }
  return std::make_unique<one_time_tokens::OneTimeTokenServiceImpl>(
      platform_sms_backend, gmail_backend);
}

}  // namespace maho::autofill
