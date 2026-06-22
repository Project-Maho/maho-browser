// Copyright 2026 Maho Browser. All rights reserved.

#ifdef UNSAFE_BUFFERS_BUILD
#pragma allow_unsafe_buffers
#endif

#include "maho/browser/importer/oscrypt_key_provider.h"

#include <vector>
#include <string>
#include "build/build_config.h"

#if BUILDFLAG(IS_MAC)
#include "maho/third_party/maho/maho_ffi.h"
#endif

namespace maho {

SecureBuffer::SecureBuffer(void* ptr, size_t len)
    : ptr_addr_(reinterpret_cast<uintptr_t>(ptr)), len_(len) {}

SecureBuffer::~SecureBuffer() {
  if (ptr_addr_ && len_ > 0) {
    volatile uint8_t* p = reinterpret_cast<volatile uint8_t*>(ptr_addr_);
    while (len_--) {
      *p++ = 0;
    }
  }
}

OSCryptKeyProvider::~OSCryptKeyProvider() = default;

#if BUILDFLAG(IS_MAC)
KeychainOSCryptKeyProvider::KeychainOSCryptKeyProvider(const std::string& service_name) {
  uint8_t derived_key[16] = {};
  uint32_t key_status = 0;
  if (maho_core_derive_oscrypt_key(service_name.c_str(), derived_key, &key_status)) {
    key_ = std::vector<uint8_t>(derived_key, derived_key + 16);
    valid_ = true;
  }
}

KeychainOSCryptKeyProvider::~KeychainOSCryptKeyProvider() {
  if (!key_.empty()) {
    SecureBuffer zero(key_.data(), key_.size());
  }
}

const std::vector<uint8_t>& KeychainOSCryptKeyProvider::GetKey() const {
  return key_;
}

bool KeychainOSCryptKeyProvider::IsValid() const {
  return valid_;
}
#endif

}  // namespace maho
