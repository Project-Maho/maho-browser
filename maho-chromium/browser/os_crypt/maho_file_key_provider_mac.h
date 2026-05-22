// Copyright 2026 Maho Browser. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MAHO_BROWSER_OS_CRYPT_MAHO_FILE_KEY_PROVIDER_MAC_H_
#define MAHO_BROWSER_OS_CRYPT_MAHO_FILE_KEY_PROVIDER_MAC_H_

#include "base/files/file_path.h"
#include "components/os_crypt/async/browser/key_provider.h"

namespace maho {

// MahoFileKeyProvider is a macOS-only os_crypt_async::KeyProvider that
// persists the underlying encryption password to a file in the user data
// directory, rather than relying on the macOS Keychain.
//
// This provides an alternative to KeychainKeyProvider that avoids Keychain
// prompts and ad-hoc code-signing issues during development and testing.
//
// Key derivation matches KeychainKeyProvider exactly so that data encrypted
// by one provider can be decrypted by the other when the same password is
// used:
//   - Tag:        "v10"
//   - Algorithm:  AES-128-CBC
//   - KDF:        PBKDF2-HMAC-SHA1, salt="saltysalt", iterations=1003,
//                 derived key size=16 bytes
//
// Password lifecycle:
//   1. If the password file already exists, load it.
//   2. Otherwise attempt a one-time migration from the Keychain via
//      KeychainPassword::GetPassword(). If the Keychain returns a non-empty
//      password, save it to the file and use it.
//   3. If neither source is available, generate a new 32-byte random password,
//      save it atomically, and use it going forward.
//
// File writes use base::ImportantFileWriter::WriteFileAtomically to prevent
// partial writes from producing a corrupt password file.
//
// Registration: injected by apply_chromium_src_overrides.py into
// chrome/browser/browser_process_impl.cc with precedence 15 (higher than
// KeychainKeyProvider's precedence 10) so Maho's file-based key takes
// precedence on macOS.
class MahoFileKeyProvider : public os_crypt_async::KeyProvider {
 public:
  // |user_data_dir| is the directory returned by
  //   base::PathService::Get(chrome::DIR_USER_DATA, &dir)
  // inside BrowserProcessImpl::PreMainMessageLoopRun().
  explicit MahoFileKeyProvider(const base::FilePath& user_data_dir);

  MahoFileKeyProvider(const MahoFileKeyProvider&) = delete;
  MahoFileKeyProvider& operator=(const MahoFileKeyProvider&) = delete;
  ~MahoFileKeyProvider() override;

  // os_crypt_async::KeyProvider interface.
  void GetKey(KeyCallback callback) override;
  bool UseForEncryption() override;
  bool IsCompatibleWithOsCryptSync() override;

 private:
  const base::FilePath user_data_dir_;
};

}  // namespace maho

#endif  // MAHO_BROWSER_OS_CRYPT_MAHO_FILE_KEY_PROVIDER_MAC_H_
