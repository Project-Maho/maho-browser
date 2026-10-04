#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wunused-variable"

#ifndef COMPONENTS_PASSWORD_MANAGER_CORE_BROWSER_PASSWORD_FILL_REQUEST_H_
#define COMPONENTS_PASSWORD_MANAGER_CORE_BROWSER_PASSWORD_FILL_REQUEST_H_

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "base/functional/callback.h"
#include "base/types/strong_alias.h"
#include "base/types/token_type.h"
#include "url/origin.h"

namespace password_manager {

using FormPrimaryKey = base::StrongAlias<class FormPrimaryKeyTag, int>;

using PasswordManagerDocumentToken =
    base::TokenType<class PasswordManagerDocumentTokenTag>;

struct PasswordFillRequestContext {
  url::Origin requesting_origin;
  PasswordManagerDocumentToken document_token;

  friend bool operator==(const PasswordFillRequestContext&,
                         const PasswordFillRequestContext&) = default;
};

struct PasswordFillSelection {
  std::optional<FormPrimaryKey> primary_key;
  uint64_t observed_revision = 0;
};

class PasswordFillResolution {
 public:
  explicit PasswordFillResolution(std::u16string secret);
  PasswordFillResolution(const PasswordFillResolution&) = delete;
  PasswordFillResolution& operator=(const PasswordFillResolution&) = delete;
  PasswordFillResolution(PasswordFillResolution&& other) noexcept;
  PasswordFillResolution& operator=(PasswordFillResolution&& other) noexcept;
  ~PasswordFillResolution();

  const std::u16string& secret() const { return secret_; }

 private:
  void Zeroize();

  std::u16string secret_;
};

class PasswordManagerDriver;

class PasswordFillResolver {
 public:
  PasswordFillResolver(PasswordFillResolver&&) noexcept;
  PasswordFillResolver& operator=(PasswordFillResolver&&) noexcept;
  PasswordFillResolver(const PasswordFillResolver&) = delete;
  PasswordFillResolver& operator=(const PasswordFillResolver&) = delete;
  ~PasswordFillResolver();

  void Run(std::optional<PasswordFillResolution> resolution) &&;

 private:
  friend class PasswordManagerDriver;
  explicit PasswordFillResolver(
      base::OnceCallback<void(std::optional<PasswordFillResolution>)> callback);

  base::OnceCallback<void(std::optional<PasswordFillResolution>)> callback_;
};

}  // namespace password_manager

#endif  // COMPONENTS_PASSWORD_MANAGER_CORE_BROWSER_PASSWORD_FILL_REQUEST_H_
