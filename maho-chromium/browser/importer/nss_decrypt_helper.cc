// Copyright 2026 Maho Browser. All rights reserved.

#ifdef UNSAFE_BUFFERS_BUILD
#pragma allow_unsafe_buffers
#endif

#include "maho/browser/importer/nss_decrypt_helper.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "base/base64.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/logging.h"
#include "sql/database.h"
#include "sql/statement.h"

#if BUILDFLAG(IS_MAC)
#include <CommonCrypto/CommonCryptor.h>
#include <CommonCrypto/CommonDigest.h>
#include <CommonCrypto/CommonKeyDerivation.h>
#endif

namespace maho {

NssGlobalKey::NssGlobalKey() = default;
NssGlobalKey::~NssGlobalKey() {
  if (!key_bytes.empty()) {
    volatile uint8_t* p = static_cast<volatile uint8_t*>(key_bytes.data());
    size_t len = key_bytes.size();
    while (len--) {
      *p++ = 0;
    }
  }
}
NssGlobalKey::NssGlobalKey(NssGlobalKey&&) = default;
NssGlobalKey& NssGlobalKey::operator=(NssGlobalKey&&) = default;

namespace {

class SecureZero {
 public:
  SecureZero(void* ptr, size_t len) : ptr_addr_(reinterpret_cast<uintptr_t>(ptr)), len_(len) {}
  ~SecureZero() {
    if (ptr_addr_ && len_ > 0) {
      volatile uint8_t* p = reinterpret_cast<volatile uint8_t*>(ptr_addr_);
      while (len_--) {
        *p++ = 0;
      }
    }
  }
 private:
  uintptr_t ptr_addr_;
  size_t len_;
};

// ASN.1 OIDs for algorithm detection.
// SEC_OID_PKCS5_PBKDF2: 1.2.840.113549.1.5.12
constexpr uint8_t kOidPbkdf2[] = {0x2a, 0x86, 0x48, 0x86, 0xf7,
                                   0x0d, 0x01, 0x05, 0x0c};

// SEC_OID_AES_256_CBC: 2.16.840.1.101.3.4.1.42
constexpr uint8_t kOidAes256Cbc[] = {0x60, 0x86, 0x48, 0x01,
                                      0x65, 0x03, 0x04, 0x01, 0x2a};
// SEC_OID_DES_EDE3_CBC: 1.2.840.113549.3.7
constexpr uint8_t kOid3DesCbc[] = {0x2a, 0x86, 0x48, 0x86,
                                    0xf7, 0x0d, 0x03, 0x07};
// SEC_OID_PKCS12_V2_PBE_WITH_SHA1_AND_3KEY_TRIPLE_DES_CBC:
// 1.2.840.113549.1.12.5.1.3
constexpr uint8_t kOidPkcs12Pbe[] = {0x2a, 0x86, 0x48, 0x86, 0xf7,
                                      0x0d, 0x01, 0x0c, 0x05, 0x01, 0x03};


constexpr size_t k3DesKeyLength = 24;
constexpr size_t kAes256KeyLength = 32;
constexpr size_t k3DesIvLength = 8;
constexpr size_t kAes256IvLength = 16;

// Simple BER/DER ASN.1 parser that reads tag + length and returns the content.
struct Asn1Item {
  uint8_t tag;
  const uint8_t* data;
  size_t length;
};

bool ReadAsn1Item(const uint8_t* buf, size_t buf_len, Asn1Item* out,
                  size_t* bytes_consumed) {
  if (buf_len < 2) {
    return false;
  }

  out->tag = buf[0];
  size_t offset = 1;

  if (buf[offset] & 0x80) {
    size_t num_len_bytes = buf[offset] & 0x7f;
    offset++;
    if (num_len_bytes > 4 || offset + num_len_bytes > buf_len) {
      return false;
    }
    out->length = 0;
    for (size_t i = 0; i < num_len_bytes; i++) {
      out->length = (out->length << 8) | buf[offset + i];
    }
    offset += num_len_bytes;
  } else {
    out->length = buf[offset];
    offset++;
  }

  if (offset + out->length > buf_len) {
    return false;
  }

  out->data = buf + offset;
  *bytes_consumed = offset + out->length;
  return true;
}

bool MatchOid(const uint8_t* data, size_t len, const uint8_t* oid,
              size_t oid_len) {
  return len == oid_len && memcmp(data, oid, oid_len) == 0;
}



#if BUILDFLAG(IS_MAC)

// PKCS#12 PBE key derivation (ID=1 for key, ID=2 for IV).
// Used by Variant A (Firefox <75).
std::vector<uint8_t> Pkcs12Derive(const std::vector<uint8_t>& password_bmp,
                                   const std::vector<uint8_t>& salt,
                                   uint32_t iterations, size_t output_len,
                                   uint8_t id) {
  constexpr size_t kHashLen = CC_SHA1_DIGEST_LENGTH;  // 20
  constexpr size_t kBlockLen = 64;  // SHA-1 block size

  // D = id repeated kBlockLen times
  std::vector<uint8_t> D(kBlockLen, id);

  // S = salt extended to multiple of kBlockLen
  std::vector<uint8_t> S;
  if (!salt.empty()) {
    S.resize(kBlockLen * ((salt.size() + kBlockLen - 1) / kBlockLen));
    for (size_t i = 0; i < S.size(); i++) {
      S[i] = salt[i % salt.size()];
    }
  }

  // P = password (BMP encoded) extended to multiple of kBlockLen
  std::vector<uint8_t> P;
  if (!password_bmp.empty()) {
    P.resize(kBlockLen * ((password_bmp.size() + kBlockLen - 1) / kBlockLen));
    for (size_t i = 0; i < P.size(); i++) {
      P[i] = password_bmp[i % password_bmp.size()];
    }
  }

  // I = S || P
  std::vector<uint8_t> I;
  I.insert(I.end(), S.begin(), S.end());
  I.insert(I.end(), P.begin(), P.end());

  std::vector<uint8_t> result;
  while (result.size() < output_len) {
    // A = H^iterations(D || I)
    std::vector<uint8_t> A_input;
    A_input.insert(A_input.end(), D.begin(), D.end());
    A_input.insert(A_input.end(), I.begin(), I.end());

    uint8_t A[kHashLen];
    CC_SHA1(A_input.data(), static_cast<CC_LONG>(A_input.size()), A);
    for (uint32_t iter = 1; iter < iterations; iter++) {
      CC_SHA1(A, kHashLen, A);
    }

    result.insert(result.end(), A, A + kHashLen);

    if (result.size() >= output_len) {
      break;
    }

    // B = A extended to kBlockLen
    std::vector<uint8_t> B(kBlockLen);
    for (size_t i = 0; i < kBlockLen; i++) {
      B[i] = A[i % kHashLen];
    }

    // I_j = (I_j + B + 1) mod 2^kBlockLen for each kBlockLen-sized chunk
    for (size_t j = 0; j < I.size(); j += kBlockLen) {
      uint16_t carry = 1;
      for (int k = static_cast<int>(kBlockLen) - 1; k >= 0; k--) {
        uint16_t sum = static_cast<uint16_t>(I[j + k]) + B[k] + carry;
        I[j + k] = static_cast<uint8_t>(sum & 0xff);
        carry = sum >> 8;
      }
    }
  }

  result.resize(output_len);
  return result;
}

// Decrypt with 3DES-CBC.
std::vector<uint8_t> Decrypt3Des(const uint8_t* key, size_t key_len,
                                  const uint8_t* iv, const uint8_t* data,
                                  size_t data_len) {
  std::vector<uint8_t> output(data_len + kCCBlockSize3DES);
  size_t out_len = 0;

  CCCryptorStatus status = CCCrypt(kCCDecrypt, kCCAlgorithm3DES,
                                    kCCOptionPKCS7Padding, key, key_len, iv,
                                    data, data_len, output.data(),
                                    output.size(), &out_len);
  if (status != kCCSuccess) {
    return {};
  }
  output.resize(out_len);
  return output;
}

// Decrypt with AES-256-CBC.
std::vector<uint8_t> DecryptAes256Cbc(const uint8_t* key, const uint8_t* iv,
                                       const uint8_t* data, size_t data_len) {
  std::vector<uint8_t> output(data_len + kCCBlockSizeAES128);
  size_t out_len = 0;

  CCCryptorStatus status =
      CCCrypt(kCCDecrypt, kCCAlgorithmAES, kCCOptionPKCS7Padding, key,
              kAes256KeyLength, iv, data, data_len, output.data(),
              output.size(), &out_len);
  if (status != kCCSuccess) {
    return {};
  }
  output.resize(out_len);
  return output;
}

// Variant A: PKCS#12 PBE + 3DES-CBC (Firefox <75).
// The global password is "" (empty), encoded as BMP: {0x00, 0x00}.
NssGlobalKey DeriveKeyVariantA(const uint8_t* entry_salt,
                                size_t entry_salt_len, uint32_t iterations,
                                const uint8_t* encrypted_value,
                                size_t encrypted_value_len) {
  NssGlobalKey result;

  // Empty password in BMP encoding (null-terminated UTF-16BE).
  std::vector<uint8_t> password_bmp = {0x00, 0x00};
  std::vector<uint8_t> salt(entry_salt, entry_salt + entry_salt_len);

  std::vector<uint8_t> key =
      Pkcs12Derive(password_bmp, salt, iterations, k3DesKeyLength, 1);
  std::vector<uint8_t> iv =
      Pkcs12Derive(password_bmp, salt, iterations, k3DesIvLength, 2);

  SecureZero zero_key(key.data(), key.size());
  SecureZero zero_iv(iv.data(), iv.size());

  std::vector<uint8_t> decrypted =
      Decrypt3Des(key.data(), key.size(), iv.data(), encrypted_value,
                  encrypted_value_len);
  if (decrypted.empty()) {
    return result;
  }

  result.key_bytes = std::move(decrypted);
  result.valid = true;
  return result;
}

// Variant B/C: PBKDF2-HMAC-SHA256 + AES-256-CBC (Firefox 75+).
// entry_salt from metaData, iterations from metaData, then decrypt nssPrivate.
NssGlobalKey DeriveKeyVariantBC(const uint8_t* global_salt,
                                 size_t global_salt_len,
                                 const uint8_t* entry_salt,
                                 size_t entry_salt_len, uint32_t iterations,
                                 const uint8_t* encrypted_value,
                                 size_t encrypted_value_len,
                                 const uint8_t* iv, size_t iv_len,
                                 bool use_sha384) {
  NssGlobalKey result;

  // Step 1: SHA-256(global_salt + password).
  // Password is empty string.
  uint8_t hp[CC_SHA256_DIGEST_LENGTH];
  SecureZero zero_hp(hp, sizeof(hp));
  CC_SHA256_CTX ctx;
  CC_SHA256_Init(&ctx);
  CC_SHA256_Update(&ctx, global_salt, static_cast<CC_LONG>(global_salt_len));
  CC_SHA256_Final(hp, &ctx);

  // Step 2: PBKDF2-HMAC-SHA256(hp, entry_salt, iterations, 32).
  uint8_t derived_key[kAes256KeyLength];
  SecureZero zero_derived(derived_key, sizeof(derived_key));

  if (use_sha384) {
    // Variant C (Firefox 146+): Uses SHA-384 in the PBKDF2 step.
    // PBKDF2-HMAC-SHA384 → 32-byte key.
    uint8_t hp384[CC_SHA384_DIGEST_LENGTH];
    SecureZero zero_hp384(hp384, sizeof(hp384));
    CC_SHA512_CTX ctx384;
    CC_SHA384_Init(&ctx384);
    CC_SHA384_Update(&ctx384, global_salt,
                     static_cast<CC_LONG>(global_salt_len));
    CC_SHA384_Final(hp384, &ctx384);

    CCKeyDerivationPBKDF(kCCPBKDF2, reinterpret_cast<const char*>(hp384),
                         CC_SHA384_DIGEST_LENGTH, entry_salt,
                         entry_salt_len, kCCPRFHmacAlgSHA384, iterations,
                         derived_key, kAes256KeyLength);
  } else {
    CCKeyDerivationPBKDF(kCCPBKDF2, reinterpret_cast<const char*>(hp),
                         CC_SHA256_DIGEST_LENGTH, entry_salt,
                         entry_salt_len, kCCPRFHmacAlgSHA256, iterations,
                         derived_key, kAes256KeyLength);
  }

  if (iv_len != kAes256IvLength) {
    LOG(WARNING) << "NSS: unexpected IV length: " << iv_len;
    return result;
  }

  std::vector<uint8_t> decrypted = DecryptAes256Cbc(
      derived_key, iv, encrypted_value, encrypted_value_len);
  if (decrypted.empty()) {
    return result;
  }

  result.key_bytes = std::move(decrypted);
  result.valid = true;
  return result;
}

// Parse the ASN.1 structure in metaData's item1 to extract algorithm params.
struct MetaDataParams {
  const uint8_t* entry_salt = nullptr;
  size_t entry_salt_len = 0;
  uint32_t iterations = 0;
  const uint8_t* iv = nullptr;
  size_t iv_len = 0;
  bool is_pkcs12_pbe = false;
  bool is_aes256_cbc = false;
  bool is_3des_cbc = false;
  bool use_sha384 = false;
};

bool ParseMetaDataAsn1(const uint8_t* data, size_t len,
                        MetaDataParams* params) {
  // Top-level SEQUENCE
  Asn1Item outer;
  size_t consumed = 0;
  if (!ReadAsn1Item(data, len, &outer, &consumed) || outer.tag != 0x30) {
    return false;
  }

  // First element: SEQUENCE containing algorithm info
  Asn1Item algo_seq;
  if (!ReadAsn1Item(outer.data, outer.length, &algo_seq, &consumed) ||
      algo_seq.tag != 0x30) {
    return false;
  }

  // Read OID inside algo_seq
  Asn1Item oid_item;
  size_t oid_consumed = 0;
  if (!ReadAsn1Item(algo_seq.data, algo_seq.length, &oid_item, &oid_consumed) ||
      oid_item.tag != 0x06) {
    return false;
  }

  if (MatchOid(oid_item.data, oid_item.length, kOidPkcs12Pbe,
               sizeof(kOidPkcs12Pbe))) {
    // Variant A: PKCS#12 PBE.
    params->is_pkcs12_pbe = true;

    // Parameters: SEQUENCE { OCTET STRING (salt), INTEGER (iterations) }
    Asn1Item pbe_params;
    size_t pbe_consumed = 0;
    const uint8_t* after_oid = algo_seq.data + oid_consumed;
    size_t remaining = algo_seq.length - oid_consumed;
    if (!ReadAsn1Item(after_oid, remaining, &pbe_params, &pbe_consumed) ||
        pbe_params.tag != 0x30) {
      return false;
    }

    Asn1Item salt_item;
    size_t salt_consumed = 0;
    if (!ReadAsn1Item(pbe_params.data, pbe_params.length, &salt_item,
                      &salt_consumed) ||
        salt_item.tag != 0x04) {
      return false;
    }
    params->entry_salt = salt_item.data;
    params->entry_salt_len = salt_item.length;

    Asn1Item iter_item;
    size_t iter_consumed = 0;
    if (!ReadAsn1Item(pbe_params.data + salt_consumed,
                      pbe_params.length - salt_consumed, &iter_item,
                      &iter_consumed) ||
        iter_item.tag != 0x02) {
      return false;
    }
    params->iterations = 0;
    for (size_t i = 0; i < iter_item.length && i < 4; i++) {
      params->iterations = (params->iterations << 8) | iter_item.data[i];
    }
    return true;
  }

  if (MatchOid(oid_item.data, oid_item.length, kOidPbkdf2,
               sizeof(kOidPbkdf2))) {
    // Variant B/C: PBKDF2 + AES-256-CBC or PBKDF2 + 3DES.
    // SEQUENCE { PBKDF2-params, encryption-algorithm }
    const uint8_t* after_oid = algo_seq.data + oid_consumed;
    size_t remaining = algo_seq.length - oid_consumed;

    // PBKDF2 params: SEQUENCE { salt (OCTET STRING), iterations (INTEGER),
    //                           keyLength (INTEGER, optional),
    //                           prf (AlgorithmIdentifier, optional) }
    Asn1Item pbkdf2_params;
    size_t pbkdf2_consumed = 0;
    if (!ReadAsn1Item(after_oid, remaining, &pbkdf2_params, &pbkdf2_consumed) ||
        pbkdf2_params.tag != 0x30) {
      return false;
    }

    // Salt
    Asn1Item salt_item;
    size_t salt_consumed = 0;
    if (!ReadAsn1Item(pbkdf2_params.data, pbkdf2_params.length, &salt_item,
                      &salt_consumed) ||
        salt_item.tag != 0x04) {
      return false;
    }
    params->entry_salt = salt_item.data;
    params->entry_salt_len = salt_item.length;

    // Iterations
    Asn1Item iter_item;
    size_t iter_consumed = 0;
    const uint8_t* after_salt = pbkdf2_params.data + salt_consumed;
    size_t salt_remaining = pbkdf2_params.length - salt_consumed;
    if (!ReadAsn1Item(after_salt, salt_remaining, &iter_item, &iter_consumed) ||
        iter_item.tag != 0x02) {
      return false;
    }
    params->iterations = 0;
    for (size_t i = 0; i < iter_item.length && i < 4; i++) {
      params->iterations = (params->iterations << 8) | iter_item.data[i];
    }

    // Now find the encryption algorithm (after PBKDF2 params in outer seq).
    const uint8_t* after_pbkdf2 = after_oid + pbkdf2_consumed;
    size_t after_pbkdf2_remaining = remaining - pbkdf2_consumed;

    Asn1Item enc_algo_seq;
    size_t enc_consumed = 0;
    if (!ReadAsn1Item(after_pbkdf2, after_pbkdf2_remaining, &enc_algo_seq,
                      &enc_consumed) ||
        enc_algo_seq.tag != 0x30) {
      return false;
    }

    Asn1Item enc_oid;
    size_t enc_oid_consumed = 0;
    if (!ReadAsn1Item(enc_algo_seq.data, enc_algo_seq.length, &enc_oid,
                      &enc_oid_consumed) ||
        enc_oid.tag != 0x06) {
      return false;
    }

    if (MatchOid(enc_oid.data, enc_oid.length, kOidAes256Cbc,
                 sizeof(kOidAes256Cbc))) {
      params->is_aes256_cbc = true;
    } else if (MatchOid(enc_oid.data, enc_oid.length, kOid3DesCbc,
                        sizeof(kOid3DesCbc))) {
      params->is_3des_cbc = true;
    } else {
      return false;
    }

    // IV: OCTET STRING after encryption OID
    const uint8_t* after_enc_oid = enc_algo_seq.data + enc_oid_consumed;
    size_t enc_remaining = enc_algo_seq.length - enc_oid_consumed;
    Asn1Item iv_item;
    size_t iv_consumed = 0;
    if (!ReadAsn1Item(after_enc_oid, enc_remaining, &iv_item, &iv_consumed) ||
        iv_item.tag != 0x04) {
      return false;
    }
    params->iv = iv_item.data;
    params->iv_len = iv_item.length;

    return true;
  }

  return false;
}

// Parse ASN.1 wrapper around encrypted login field from logins.json.
// Structure: SEQUENCE { SEQUENCE { OID, params }, OCTET STRING (ciphertext) }
struct LoginFieldAsn1 {
  const uint8_t* iv = nullptr;
  size_t iv_len = 0;
  const uint8_t* ciphertext = nullptr;
  size_t ciphertext_len = 0;
  bool is_3des = false;
  bool is_aes256_cbc = false;
};

bool ParseLoginFieldAsn1(const uint8_t* data, size_t len,
                          LoginFieldAsn1* field) {
  if (len < 2) {
    return false;
  }

  Asn1Item first_item;
  size_t consumed = 0;
  if (!ReadAsn1Item(data, len, &first_item, &consumed) || first_item.tag != 0x30) {
    return false;
  }

  // Determine if first_item is the outer wrapper SEQUENCE or the Algorithm SEQUENCE.
  // We check the tag of the first child inside first_item.
  Asn1Item first_child;
  size_t child_consumed = 0;
  if (!ReadAsn1Item(first_item.data, first_item.length, &first_child, &child_consumed)) {
    return false;
  }

  Asn1Item algo_seq;
  const uint8_t* ct_data = nullptr;
  size_t ct_len = 0;

  if (first_child.tag == 0x30) {
    // first_item is the outer wrapper SEQUENCE.
    // first_child is the Algorithm SEQUENCE.
    algo_seq = first_child;

    // The ciphertext OCTET STRING is the second element in the outer SEQUENCE.
    const uint8_t* after_algo = first_item.data + child_consumed;
    size_t outer_remaining = first_item.length - child_consumed;
    Asn1Item ct_item;
    size_t ct_consumed = 0;
    if (!ReadAsn1Item(after_algo, outer_remaining, &ct_item, &ct_consumed) ||
        ct_item.tag != 0x04) {
      return false;
    }
    ct_data = ct_item.data;
    ct_len = ct_item.length;
  } else if (first_child.tag == 0x06) {
    // first_item is the Algorithm SEQUENCE itself (outer wrapper already stripped).
    algo_seq = first_item;

    // The ciphertext OCTET STRING is located after first_item in the input data.
    const uint8_t* after_algo = data + consumed;
    size_t remaining = len - consumed;

    // Add a length guard before trying to read the second item
    if (remaining < 2) {
      return false;
    }

    Asn1Item ct_item;
    size_t ct_consumed = 0;
    if (!ReadAsn1Item(after_algo, remaining, &ct_item, &ct_consumed) ||
        ct_item.tag != 0x04) {
      return false;
    }
    ct_data = ct_item.data;
    ct_len = ct_item.length;
  } else {
    return false;
  }

  // Now parse algo_seq to extract OID, IV, etc.
  Asn1Item oid_item;
  size_t oid_consumed = 0;
  if (!ReadAsn1Item(algo_seq.data, algo_seq.length, &oid_item, &oid_consumed) ||
      oid_item.tag != 0x06) {
    return false;
  }

  const uint8_t* after_oid = algo_seq.data + oid_consumed;
  size_t remaining = algo_seq.length - oid_consumed;

  if (MatchOid(oid_item.data, oid_item.length, kOid3DesCbc,
               sizeof(kOid3DesCbc))) {
    field->is_3des = true;
    Asn1Item iv_item;
    size_t iv_consumed = 0;
    if (!ReadAsn1Item(after_oid, remaining, &iv_item, &iv_consumed) ||
        iv_item.tag != 0x04) {
      return false;
    }
    field->iv = iv_item.data;
    field->iv_len = iv_item.length;
  } else if (MatchOid(oid_item.data, oid_item.length, kOidAes256Cbc,
                       sizeof(kOidAes256Cbc))) {
    field->is_aes256_cbc = true;
    Asn1Item iv_item;
    size_t iv_consumed = 0;
    if (!ReadAsn1Item(after_oid, remaining, &iv_item, &iv_consumed) ||
        iv_item.tag != 0x04) {
      return false;
    }
    field->iv = iv_item.data;
    field->iv_len = iv_item.length;
  } else {
    return false;
  }

  field->ciphertext = ct_data;
  field->ciphertext_len = ct_len;


  return true;
}

// Verifies the password check by decrypting the "password-check" entry.
// For no master password, decrypted value should start with "password-check".
bool VerifyPasswordCheck(const NssGlobalKey& key,
                          const uint8_t* check_data, size_t check_len) {
  LoginFieldAsn1 field;
  if (!ParseLoginFieldAsn1(check_data, check_len, &field)) {
    return false;
  }

  std::vector<uint8_t> decrypted;
  if (field.is_3des) {
    if (key.key_bytes.size() < k3DesKeyLength) {
      return false;
    }
    decrypted = Decrypt3Des(key.key_bytes.data(), k3DesKeyLength, field.iv,
                            field.ciphertext, field.ciphertext_len);
  } else if (field.is_aes256_cbc) {
    if (key.key_bytes.size() < kAes256KeyLength) {
      return false;
    }
    decrypted = DecryptAes256Cbc(key.key_bytes.data(), field.iv,
                                  field.ciphertext, field.ciphertext_len);
  }

  if (decrypted.empty()) {
    return false;
  }

  constexpr char kExpected[] = "password-check";
  constexpr size_t kExpectedLen = sizeof(kExpected) - 1;
  if (decrypted.size() < kExpectedLen) {
    return false;
  }
  return memcmp(decrypted.data(), kExpected, kExpectedLen) == 0;
}

#endif  // BUILDFLAG(IS_MAC)

}  // namespace

NssGlobalKey ExtractNssGlobalKey(const base::FilePath& key4_db_path,
                                 NssDecryptStatus* out_status) {
  NssGlobalKey result;

#if BUILDFLAG(IS_MAC)
  // Copy to temp to avoid locking.
  base::FilePath temp_path;
  if (!base::CreateTemporaryFile(&temp_path)) {
    *out_status = NssDecryptStatus::kKey4DbOpenFailed;
    return result;
  }
  if (!base::CopyFile(key4_db_path, temp_path)) {
    base::DeleteFile(temp_path);
    *out_status = NssDecryptStatus::kKey4DbOpenFailed;
    return result;
  }

  std::vector<uint8_t> item1;
  std::vector<uint8_t> item2;
  std::vector<uint8_t> a11;

  {
    sql::Database db("FirefoxImporter");
    if (!db.Open(temp_path)) {
      base::DeleteFile(temp_path);
      *out_status = NssDecryptStatus::kKey4DbOpenFailed;
      return result;
    }

    if (!db.DoesTableExist("metadata") || !db.DoesTableExist("nssPrivate")) {
      base::DeleteFile(temp_path);
      *out_status = NssDecryptStatus::kKey4DbOpenFailed;
      return result;
    }

    // Get global salt from metadata.
    sql::Statement meta_stmt(db.GetUniqueStatement(
        "SELECT item1, item2 FROM metadata WHERE id = 'password'"));
    if (!meta_stmt.is_valid() || !meta_stmt.Step()) {
      base::DeleteFile(temp_path);
      *out_status = NssDecryptStatus::kKey4DbOpenFailed;
      return result;
    }

    base::span<const uint8_t> item1_blob = meta_stmt.ColumnBlob(0);
    base::span<const uint8_t> item2_blob = meta_stmt.ColumnBlob(1);

    if (item1_blob.empty() || item2_blob.empty()) {
      base::DeleteFile(temp_path);
      *out_status = NssDecryptStatus::kKeyDerivationFailed;
      return result;
    }

    item1.assign(item1_blob.begin(), item1_blob.end());
    item2.assign(item2_blob.begin(), item2_blob.end());

    // Get the encrypted key from nssPrivate.
    sql::Statement priv_stmt(db.GetUniqueStatement(
        "SELECT a11, a102 FROM nssPrivate"));
    if (!priv_stmt.is_valid() || !priv_stmt.Step()) {
      base::DeleteFile(temp_path);
      *out_status = NssDecryptStatus::kKeyDerivationFailed;
      return result;
    }

    base::span<const uint8_t> a11_blob = priv_stmt.ColumnBlob(0);
    if (a11_blob.empty()) {
      base::DeleteFile(temp_path);
      *out_status = NssDecryptStatus::kKeyDerivationFailed;
      return result;
    }
    a11.assign(a11_blob.begin(), a11_blob.end());
  }

  base::DeleteFile(temp_path);

  // Detect variant based on item1 length (per plan):
  // - length 20: Variant A (PKCS#12 PBE + 3DES) or Variant B (PBKDF2 + AES)
  // - length 48: Variant C (SHA384 + AES-256-CBC)
  // Discrimination within 20-byte case: parse ASN.1 to detect OID.
  bool use_sha384 = (item1.size() > 40);

  // Parse the ASN.1 in a11 (nssPrivate encrypted key entry).
  MetaDataParams params;
  if (!ParseMetaDataAsn1(a11.data(), a11.size(), &params)) {
    *out_status = NssDecryptStatus::kInvalidAsn1;
    return result;
  }

  // item1 contains the global salt (first 16-32 bytes depending on variant).
  // For Variant A: the global salt is not used; PKCS#12 PBE uses entry_salt.
  // For Variant B/C: global salt is the raw item1 bytes.
  if (params.is_pkcs12_pbe) {
    // Variant A: PKCS#12 PBE.
    // The encrypted data is in item2 (the "password-check" value).
    // Actually, a11 contains the wrapped global key.
    // Parse a11 for salt + iterations → derive key → decrypt a11's payload.

    // For PKCS#12 PBE, find the encrypted content after the ASN.1 algo header.
    Asn1Item outer;
    size_t consumed = 0;
    if (!ReadAsn1Item(a11.data(), a11.size(), &outer, &consumed)) {
      *out_status = NssDecryptStatus::kInvalidAsn1;
      return result;
    }

    // After algo_seq, read the OCTET STRING (encrypted key).
    Asn1Item algo_seq;
    size_t algo_consumed = 0;
    if (!ReadAsn1Item(outer.data, outer.length, &algo_seq, &algo_consumed)) {
      *out_status = NssDecryptStatus::kInvalidAsn1;
      return result;
    }

    const uint8_t* after_algo = outer.data + algo_consumed;
    size_t remaining = outer.length - algo_consumed;
    Asn1Item encrypted_item;
    size_t enc_consumed = 0;
    if (!ReadAsn1Item(after_algo, remaining, &encrypted_item, &enc_consumed) ||
        encrypted_item.tag != 0x04) {
      *out_status = NssDecryptStatus::kInvalidAsn1;
      return result;
    }

    result = DeriveKeyVariantA(params.entry_salt, params.entry_salt_len,
                                params.iterations, encrypted_item.data,
                                encrypted_item.length);
  } else {
    // Variant B/C: PBKDF2 + AES-256-CBC.
    // item1 is the global salt for the SHA-256 pre-hash.

    // Find encrypted content in a11.
    Asn1Item outer;
    size_t consumed = 0;
    if (!ReadAsn1Item(a11.data(), a11.size(), &outer, &consumed)) {
      *out_status = NssDecryptStatus::kInvalidAsn1;
      return result;
    }

    Asn1Item algo_seq;
    size_t algo_consumed = 0;
    if (!ReadAsn1Item(outer.data, outer.length, &algo_seq, &algo_consumed)) {
      *out_status = NssDecryptStatus::kInvalidAsn1;
      return result;
    }

    const uint8_t* after_algo = outer.data + algo_consumed;
    size_t remaining = outer.length - algo_consumed;
    Asn1Item encrypted_item;
    size_t enc_consumed = 0;
    if (!ReadAsn1Item(after_algo, remaining, &encrypted_item, &enc_consumed) ||
        encrypted_item.tag != 0x04) {
      *out_status = NssDecryptStatus::kInvalidAsn1;
      return result;
    }

    result =
        DeriveKeyVariantBC(item1.data(), item1.size(), params.entry_salt,
                           params.entry_salt_len, params.iterations,
                           encrypted_item.data, encrypted_item.length,
                           params.iv, params.iv_len, use_sha384);
  }

  if (!result.valid) {
    *out_status = NssDecryptStatus::kKeyDerivationFailed;
    return result;
  }

  // Verify with password-check (item2).
  if (!VerifyPasswordCheck(result, item2.data(), item2.size())) {
    LOG(WARNING) << "NSS: password-check verification failed — master password "
                    "may be set";
    result.valid = false;
    *out_status = NssDecryptStatus::kMasterPasswordSet;
    return result;
  }

  *out_status = NssDecryptStatus::kSuccess;

#else
  *out_status = NssDecryptStatus::kUnsupportedAlgorithm;
#endif

  return result;
}

std::string NssDecryptField(const std::string& base64_ciphertext,
                            const NssGlobalKey& global_key,
                            NssDecryptStatus* out_status) {
  DCHECK(!base64_ciphertext.empty());

  if (!global_key.valid) {
    *out_status = NssDecryptStatus::kKeyDerivationFailed;
    return std::string();
  }

#if BUILDFLAG(IS_MAC)
  std::string decoded;
  if (!base::Base64Decode(base64_ciphertext, &decoded)) {
    *out_status = NssDecryptStatus::kInvalidAsn1;
    return std::string();
  }

  const auto* raw = reinterpret_cast<const uint8_t*>(decoded.data());
  size_t raw_len = decoded.size();

  LoginFieldAsn1 field;
  if (!ParseLoginFieldAsn1(raw, raw_len, &field)) {
    *out_status = NssDecryptStatus::kInvalidAsn1;
    return std::string();
  }

  std::vector<uint8_t> decrypted;
  if (field.is_3des) {
    if (global_key.key_bytes.size() < k3DesKeyLength) {
      *out_status = NssDecryptStatus::kDecryptionFailed;
      return std::string();
    }
    decrypted = Decrypt3Des(global_key.key_bytes.data(), k3DesKeyLength,
                            field.iv, field.ciphertext, field.ciphertext_len);
  } else if (field.is_aes256_cbc) {
    if (global_key.key_bytes.size() < kAes256KeyLength) {
      *out_status = NssDecryptStatus::kDecryptionFailed;
      return std::string();
    }
    decrypted = DecryptAes256Cbc(global_key.key_bytes.data(), field.iv,
                                  field.ciphertext, field.ciphertext_len);
  } else {
    *out_status = NssDecryptStatus::kUnsupportedAlgorithm;
    return std::string();
  }

  if (decrypted.empty()) {
    *out_status = NssDecryptStatus::kDecryptionFailed;
    return std::string();
  }

  SecureZero zero_decrypted(decrypted.data(), decrypted.size());

  // PKCS#7 padding is already stripped by CCCrypt with kCCOptionPKCS7Padding.

  *out_status = NssDecryptStatus::kSuccess;
  return std::string(reinterpret_cast<const char*>(decrypted.data()),
                     decrypted.size());

#else
  *out_status = NssDecryptStatus::kUnsupportedAlgorithm;
  return std::string();
#endif
}

}  // namespace maho
