// Copyright 2026 Maho Browser. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "maho/browser/os_crypt/maho_file_key_provider_mac.h"

#include <array>
#include <string>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/important_file_writer.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/task/thread_pool.h"
#include "base/types/expected.h"
#include "components/os_crypt/async/common/algorithm.mojom.h"
#include "crypto/random.h"
#include "third_party/boringssl/src/include/openssl/evp.h"

#define MAHO_RUST_OSCRYPT_MAC 1

#if defined(MAHO_RUST_OSCRYPT_MAC)
#include "maho/third_party/maho/maho_ffi.h"
#endif

namespace maho {

namespace {

// Matches upstream keychain_key_provider.mm constants exactly.
// Tag identifies v10-encrypted data (AES-128-CBC with PBKDF2-derived key).
constexpr char kKeyTag[] = "v10";
constexpr size_t kDerivedKeySize = 16;
#if !defined(MAHO_RUST_OSCRYPT_MAC)
constexpr auto kSalt =
    std::to_array<uint8_t>({'s', 'a', 'l', 't', 'y', 's', 'a', 'l', 't'});
constexpr size_t kIterations = 1003;
#endif

constexpr char kPasswordFileName[] = "maho_oscrypt_key";

#if !defined(MAHO_RUST_OSCRYPT_MAC)
constexpr size_t kRandomPasswordBytes = 32;
#endif

#if !defined(MAHO_RUST_OSCRYPT_MAC)
bool DeriveOsCryptKey(base::span<const uint8_t> password,
                      base::span<const uint8_t> salt,
                      base::span<uint8_t> result) {
  return PKCS5_PBKDF2_HMAC_SHA1(
             base::as_chars(password).data(), static_cast<int>(password.size()),
             salt.data(), static_cast<int>(salt.size()),
             static_cast<int>(kIterations), static_cast<int>(result.size()),
             result.data()) == 1;
}
#endif

base::expected<os_crypt_async::Encryptor::Key,
               os_crypt_async::KeyProvider::KeyError>
GetKeyTask(const base::FilePath& user_data_dir) {
  const base::FilePath password_file =
      user_data_dir.AppendASCII(kPasswordFileName);

#if defined(MAHO_RUST_OSCRYPT_MAC)
  uint8_t derived_key[kDerivedKeySize];
  uint32_t status = 0;
  bool ok = maho_core_derive_oscrypt_key(password_file.value().c_str(),
                                         derived_key, &status);
  if (!ok) {
    LOG(ERROR) << "Failed to derive OSCrypt key in Rust, status: " << status;
    return base::unexpected(
        os_crypt_async::KeyProvider::KeyError::kTemporarilyUnavailable);
  }

  uint32_t secure_status = 0;
  if (!maho_core_oscrypt_key_path_is_secure(password_file.value().c_str(),
                                            &secure_status)) {
    LOG(ERROR) << "OSCrypt key file failed path/permission security check, "
                  "status: "
               << secure_status;
    return base::unexpected(
        os_crypt_async::KeyProvider::KeyError::kPermanentlyUnavailable);
  }

  return os_crypt_async::Encryptor::Key(
      derived_key, os_crypt_async::mojom::Algorithm::kAES128CBC);
#else
  std::string password;

  if (base::PathExists(password_file)) {
    if (!base::ReadFileToString(password_file, &password) || password.empty()) {
      return base::unexpected(
          os_crypt_async::KeyProvider::KeyError::kTemporarilyUnavailable);
    }
  } else {
    // No existing key file: generate a fresh random password. This provider
    // is intentionally file-only — no Keychain access is performed at any
    // point (not on first run, not on subsequent runs).
    password.resize(kRandomPasswordBytes);
    crypto::RandBytes(base::as_writable_byte_span(password));

    // Persist atomically so subsequent launches reuse the same password.
    if (!base::ImportantFileWriter::WriteFileAtomically(password_file,
                                                        password)) {
      return base::unexpected(
          os_crypt_async::KeyProvider::KeyError::kTemporarilyUnavailable);
    }

    if (!base::SetPosixFilePermissions(password_file, 0600)) {
      return base::unexpected(
          os_crypt_async::KeyProvider::KeyError::kTemporarilyUnavailable);
    }
  }

  std::array<uint8_t, kDerivedKeySize> key_bytes;
  if (!DeriveOsCryptKey(base::as_byte_span(password), kSalt, key_bytes)) {
    return base::unexpected(
        os_crypt_async::KeyProvider::KeyError::kTemporarilyUnavailable);
  }

  return os_crypt_async::Encryptor::Key(
      key_bytes, os_crypt_async::mojom::Algorithm::kAES128CBC);
#endif
}

}  // namespace

MahoFileKeyProvider::MahoFileKeyProvider(const base::FilePath& user_data_dir)
    : user_data_dir_(user_data_dir) {}

MahoFileKeyProvider::~MahoFileKeyProvider() = default;

void MahoFileKeyProvider::GetKey(KeyCallback callback) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::TaskPriority::USER_BLOCKING, base::MayBlock()},
      base::BindOnce(&GetKeyTask, user_data_dir_),
      base::BindOnce(std::move(callback), kKeyTag));
}

bool MahoFileKeyProvider::UseForEncryption() {
  return true;
}

bool MahoFileKeyProvider::IsCompatibleWithOsCryptSync() {
  return true;
}

}  // namespace maho
