// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/mail_helper/maho_mail_arbitration_service.h"

#include <cctype>
#include "base/strings/string_util.h"

namespace maho {

MahoMailArbitrationService::MahoMailArbitrationService() = default;

MahoMailArbitrationService::~MahoMailArbitrationService() = default;

bool MahoMailArbitrationService::IsOriginAllowedForEmail(const GURL& origin,
                                                         const std::string& email) {
  if (!origin.is_valid() || email.empty()) {
    return false;
  }

  size_t at_pos = email.find('@');
  if (at_pos == std::string::npos || at_pos + 1 >= email.size()) {
    return false;
  }

  std::string domain = email.substr(at_pos + 1);
  std::string host(origin.host());

  // Normalize to lowercase for safe matching
  domain = base::ToLowerASCII(domain);
  host = base::ToLowerASCII(host);

  // Exact match or sub-domain match
  return host == domain || host.ends_with("." + domain);
}

std::string MahoMailArbitrationService::ExtractOTPCode(const std::string& message) {
  std::string code;
  bool gathering = false;

  for (char c : message) {
    if (std::isdigit(static_cast<unsigned char>(c))) {
      code += c;
      gathering = true;
    } else if (gathering) {
      // Standard OTP codes range from 4 to 8 digits
      if (code.size() >= 4 && code.size() <= 8) {
        return code;
      }
      code.clear();
      gathering = false;
    }
  }

  if (code.size() >= 4 && code.size() <= 8) {
    return code;
  }

  return "";
}

std::vector<AutofillFieldMapping> MahoMailArbitrationService::GenerateAutofillMappings(
    const std::string& email_address,
    const std::string& otp_code) {
  std::vector<AutofillFieldMapping> mappings;

  if (!email_address.empty()) {
    mappings.push_back({"email", "email", email_address});
    mappings.push_back({"username", "username", email_address});
  }

  if (!otp_code.empty()) {
    mappings.push_back({"otp", "one-time-code", otp_code});
    mappings.push_back({"code", "one-time-code", otp_code});
  }

  return mappings;
}

}  // namespace maho
