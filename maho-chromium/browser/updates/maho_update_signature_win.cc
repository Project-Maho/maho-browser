// Copyright 2026 Maho Browser. All rights reserved.

#ifdef UNSAFE_BUFFERS_BUILD
// Win32 crypto API interop: CMSG_SIGNER_INFO is populated in-place by
// CryptMsgGetParam via a reinterpret_cast over a byte buffer, which cannot be
// expressed with bounds-checked spans.
#pragma allow_unsafe_buffers
#endif

#include "maho/browser/updates/maho_update_signature_win.h"

#include <stdint.h>
#include <windows.h>
#include <wintrust.h>
#include <softpub.h>
#include <wincrypt.h>

#include <vector>

#include "base/strings/utf_string_conversions.h"

#pragma comment(lib, "wintrust.lib")
#pragma comment(lib, "crypt32.lib")

namespace maho {
namespace updates {

bool VerifyAuthenticode(const base::FilePath& file_path,
                        std::string_view expected_signer_cn) {
  WINTRUST_FILE_INFO file_info = {};
  file_info.cbStruct = sizeof(file_info);
  std::wstring wpath = file_path.value();
  file_info.pcwszFilePath = wpath.c_str();

  WINTRUST_DATA trust_data = {};
  trust_data.cbStruct = sizeof(trust_data);
  trust_data.dwUnionChoice = WTD_CHOICE_FILE;
  trust_data.pFile = &file_info;
  trust_data.dwUIChoice = WTD_UI_NONE;
  // Offline-tolerance: hash + Authenticode chain is the auth boundary, not
  // OCSP. Plan §4 Risks documents this trade-off.
  trust_data.fdwRevocationChecks = WTD_REVOKE_NONE;
  trust_data.dwStateAction = WTD_STATEACTION_VERIFY;

  GUID policy_guid = WINTRUST_ACTION_GENERIC_VERIFY_V2;
  LONG result = WinVerifyTrust(NULL, &policy_guid, &trust_data);

  trust_data.dwStateAction = WTD_STATEACTION_CLOSE;
  WinVerifyTrust(NULL, &policy_guid, &trust_data);

  if (result != ERROR_SUCCESS) {
    return false;
  }

  HCERTSTORE store = nullptr;
  HCRYPTMSG msg = nullptr;
  BOOL ok = CryptQueryObject(
      CERT_QUERY_OBJECT_FILE, wpath.c_str(),
      CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED_EMBED,
      CERT_QUERY_FORMAT_FLAG_ALL, 0, nullptr, nullptr, nullptr,
      &store, &msg, nullptr);
  if (!ok) {
    return false;
  }

  DWORD signer_info_size = 0;
  ok = CryptMsgGetParam(msg, CMSG_SIGNER_INFO_PARAM, 0, nullptr,
                        &signer_info_size);
  if (!ok) {
    CryptMsgClose(msg);
    CertCloseStore(store, 0);
    return false;
  }

  std::vector<uint8_t> signer_info_buf(signer_info_size);
  CMSG_SIGNER_INFO* signer_info =
      reinterpret_cast<CMSG_SIGNER_INFO*>(signer_info_buf.data());
  ok = CryptMsgGetParam(msg, CMSG_SIGNER_INFO_PARAM, 0, signer_info,
                        &signer_info_size);
  if (!ok) {
    CryptMsgClose(msg);
    CertCloseStore(store, 0);
    return false;
  }

  CERT_INFO cert_info = {};
  cert_info.Issuer = signer_info->Issuer;
  cert_info.SerialNumber = signer_info->SerialNumber;
  PCCERT_CONTEXT cert_ctx = CertFindCertificateInStore(
      store, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0,
      CERT_FIND_SUBJECT_CERT, &cert_info, nullptr);

  CryptMsgClose(msg);
  CertCloseStore(store, 0);

  if (!cert_ctx) {
    return false;
  }

  DWORD cn_size = CertGetNameString(cert_ctx, CERT_NAME_ATTR_TYPE, 0,
                                    (void*)szOID_COMMON_NAME, nullptr, 0);
  if (cn_size <= 1) {
    CertFreeCertificateContext(cert_ctx);
    return false;
  }

  std::wstring wcn(cn_size, L'\0');
  CertGetNameString(cert_ctx, CERT_NAME_ATTR_TYPE, 0, (void*)szOID_COMMON_NAME,
                    &wcn[0], cn_size);
  CertFreeCertificateContext(cert_ctx);

  if (!wcn.empty() && wcn.back() == L'\0') {
    wcn.pop_back();
  }
  std::string cn = base::WideToUTF8(wcn);
  return cn == expected_signer_cn;
}

}  // namespace updates
}  // namespace maho
