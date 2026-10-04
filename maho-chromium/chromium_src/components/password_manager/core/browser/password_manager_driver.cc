#include "components/password_manager/core/browser/password_manager_driver.h"

#include <utility>

namespace password_manager {

void PasswordManagerDriver::FillSuggestion(
    const PasswordFillRequestContext& context,
    const std::u16string& username,
    const std::u16string& password,
    base::OnceCallback<void(bool)> success_callback) {
  if (!IsPasswordFillRequestContextCurrent(context)) {
    std::move(success_callback).Run(false);
    return;
  }
  FillSuggestion(username, password, std::move(success_callback));
}

}
