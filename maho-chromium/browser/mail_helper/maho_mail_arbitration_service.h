// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_MAIL_HELPER_MAHO_MAIL_ARBITRATION_SERVICE_H_
#define MAHO_BROWSER_MAIL_HELPER_MAHO_MAIL_ARBITRATION_SERVICE_H_

#include <string>
#include <vector>

#include "base/memory/weak_ptr.h"
#include "url/gurl.h"

namespace maho {

struct AutofillFieldMapping {
  std::string field_id;
  std::string autocomplete_type;
  std::string mapped_value;
};

class MahoMailArbitrationService {
 public:
  MahoMailArbitrationService();
  ~MahoMailArbitrationService();

  MahoMailArbitrationService(const MahoMailArbitrationService&) = delete;
  MahoMailArbitrationService& operator=(const MahoMailArbitrationService&) = delete;

  // T14: Domain binding security matrix (same-origin check)
  bool IsOriginAllowedForEmail(const GURL& origin, const std::string& email);

  // T12: Browser OTP extraction (SMS & Email parsing heuristics)
  std::string ExtractOTPCode(const std::string& message);

  // T15: Autofill overlay mapping rules
  std::vector<AutofillFieldMapping> GenerateAutofillMappings(
      const std::string& email_address,
      const std::string& otp_code);

 private:
  base::WeakPtrFactory<MahoMailArbitrationService> weak_ptr_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_MAIL_HELPER_MAHO_MAIL_ARBITRATION_SERVICE_H_
