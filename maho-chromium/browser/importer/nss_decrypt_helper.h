// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_IMPORTER_NSS_DECRYPT_HELPER_H_
#define MAHO_BROWSER_IMPORTER_NSS_DECRYPT_HELPER_H_

#include <cstdint>
#include <string>
#include <vector>

#include "base/files/file_path.h"

namespace maho {

// Errors from NSS key derivation or decryption.
enum class NssDecryptStatus {
  kSuccess = 0,
  kKey4DbOpenFailed = 1,
  kMasterPasswordSet = 2,
  kKeyDerivationFailed = 3,
  kDecryptionFailed = 4,
  kInvalidAsn1 = 5,
  kUnsupportedAlgorithm = 6,
};

// Holds the decrypted global key extracted from key4.db.
// This key is used to decrypt individual login ciphertexts from logins.json.
struct NssGlobalKey {
  NssGlobalKey();
  ~NssGlobalKey();
  NssGlobalKey(NssGlobalKey&&);
  NssGlobalKey& operator=(NssGlobalKey&&);

  std::vector<uint8_t> key_bytes;
  bool valid = false;
};

// Extracts the global decryption key from Firefox's key4.db.
// Only supports profiles with no master password (empty password).
// |key4_db_path| is the full path to key4.db.
// |out_status| receives the derivation status.
NssGlobalKey ExtractNssGlobalKey(const base::FilePath& key4_db_path,
                                 NssDecryptStatus* out_status);

// Decrypts a single Firefox encrypted field (Base64 → ASN.1 → decrypt).
// The |base64_ciphertext| is the raw Base64-encoded value from logins.json
// (e.g. "encryptedUsername" or "encryptedPassword").
// |global_key| is from ExtractNssGlobalKey().
// Returns the decrypted plaintext, or empty string on error.
std::string NssDecryptField(const std::string& base64_ciphertext,
                            const NssGlobalKey& global_key,
                            NssDecryptStatus* out_status);

}  // namespace maho

#endif  // MAHO_BROWSER_IMPORTER_NSS_DECRYPT_HELPER_H_
