// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_IMPORTER_OSCRYPT_KEY_PROVIDER_H_
#define MAHO_BROWSER_IMPORTER_OSCRYPT_KEY_PROVIDER_H_

#include <cstdint>
#include <vector>
#include <string>
#include "build/build_config.h"

namespace maho {

class SecureBuffer {
 public:
  SecureBuffer(void* ptr, size_t len);
  ~SecureBuffer();
 private:
  uintptr_t ptr_addr_;
  size_t len_;
};

class OSCryptKeyProvider {
 public:
  virtual ~OSCryptKeyProvider();
  virtual const std::vector<uint8_t>& GetKey() const = 0;
  virtual bool IsValid() const = 0;
};

#if BUILDFLAG(IS_MAC)
class KeychainOSCryptKeyProvider : public OSCryptKeyProvider {
 public:
  explicit KeychainOSCryptKeyProvider(const std::string& service_name);
  ~KeychainOSCryptKeyProvider() override;

  const std::vector<uint8_t>& GetKey() const override;
  bool IsValid() const override;

 private:
  std::vector<uint8_t> key_;
  bool valid_ = false;
};
#endif

}  // namespace maho

#endif  // MAHO_BROWSER_IMPORTER_OSCRYPT_KEY_PROVIDER_H_
