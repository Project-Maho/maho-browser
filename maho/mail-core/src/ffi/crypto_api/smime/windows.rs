use std::ffi::CStr;
use std::mem::size_of;
use std::path::Path;
use std::ptr::{null, null_mut};
use std::slice;

use base64::Engine as _;
use chrono::{DateTime, Utc};
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use windows_sys::Win32::Foundation::{GetLastError, FILETIME, NTE_BAD_KEYSET, NTE_NOT_FOUND};
use windows_sys::Win32::Security::Cryptography::*;

use super::{
    decode_cms, detached_parts, multipart_signed, pem_decode, pem_encode, smime_opaque,
    ImportedIdentity, SmimeBackend, SmimeKeyScope,
};
use crate::error::MailFfiError;
use crate::ffi::crypto_api::{internal_err, validation_err, SmimeVerifyResult};

pub(super) static WINDOWS_BACKEND: WindowsBackend = WindowsBackend;

pub(super) struct WindowsBackend;

const ENCODING: u32 = X509_ASN_ENCODING | PKCS_7_ASN_ENCODING;
const CNG_REFERENCE_PREFIX: &str = "maho-cng-v1:";
const SOFTWARE_KSP: &str = "Microsoft Software Key Storage Provider";
const EXPORTABLE_POLICY_FLAGS: u32 = NCRYPT_ALLOW_EXPORT_FLAG | NCRYPT_ALLOW_PLAINTEXT_EXPORT_FLAG;

#[derive(Debug, Serialize, Deserialize)]
struct CngKeyReference {
    provider: String,
    key_name: String,
    profile_hash: String,
    account_hash: String,
    certificate_fingerprint: String,
    public_key_hash: String,
}

fn windows_error(operation: &str) -> MailFfiError {
    internal_err(format!(
        "{operation} failed with Windows error 0x{:08X}",
        unsafe { GetLastError() }
    ))
}

fn ncrypt_error(operation: &str, status: i32) -> MailFfiError {
    internal_err(format!(
        "{operation} failed with NCrypt status 0x{:08X}",
        status as u32
    ))
}

struct Cert(*mut CERT_CONTEXT);
impl Drop for Cert {
    fn drop(&mut self) {
        if !self.0.is_null() {
            unsafe { CertFreeCertificateContext(self.0) };
        }
    }
}

struct Store(HCERTSTORE);
impl Drop for Store {
    fn drop(&mut self) {
        if !self.0.is_null() {
            unsafe { CertCloseStore(self.0, 0) };
        }
    }
}

struct NcryptProvider(NCRYPT_PROV_HANDLE);
impl Drop for NcryptProvider {
    fn drop(&mut self) {
        if self.0 != 0 {
            unsafe { NCryptFreeObject(self.0) };
        }
    }
}

struct NcryptKey {
    handle: NCRYPT_KEY_HANDLE,
    owned: bool,
}
impl Drop for NcryptKey {
    fn drop(&mut self) {
        if self.owned && self.handle != 0 {
            unsafe { NCryptFreeObject(self.handle) };
        }
    }
}

impl NcryptKey {
    fn disarm(&mut self) {
        self.handle = 0;
        self.owned = false;
    }
}

fn wide(value: &str) -> Vec<u16> {
    value.encode_utf16().chain(Some(0)).collect()
}

unsafe fn wide_ptr_to_string(
    value: *const u16,
    buffer: &[usize],
    valid_size: usize,
) -> Result<String, MailFfiError> {
    if value.is_null() {
        return Err(internal_err("Windows key provider metadata is missing"));
    }
    let start = buffer.as_ptr() as usize;
    let end = start
        .checked_add(valid_size)
        .ok_or_else(|| internal_err("Windows key provider metadata buffer range overflowed"))?;
    let address = value as usize;
    if address < start || address >= end || address % std::mem::align_of::<u16>() != 0 {
        return Err(internal_err(
            "Windows key provider metadata points outside its buffer",
        ));
    }
    let available = (end - address) / size_of::<u16>();
    let values = slice::from_raw_parts(value, available);
    let length = values
        .iter()
        .position(|value| *value == 0)
        .ok_or_else(|| internal_err("Windows key provider metadata is not null terminated"))?;
    String::from_utf16(&values[..length]).map_err(|error| {
        internal_err(format!(
            "Windows key provider metadata is invalid UTF-16: {error}",
        ))
    })
}

fn scope_hash(value: impl AsRef<[u8]>) -> String {
    Sha256::digest(value.as_ref())
        .iter()
        .map(|byte| format!("{byte:02x}"))
        .collect()
}

fn canonical_profile_hash(profile_path: &Path) -> Result<String, MailFfiError> {
    let canonical = profile_path.canonicalize().map_err(|error| {
        validation_err(format!(
            "Windows S/MIME profile root is not canonical: {error}",
        ))
    })?;
    Ok(scope_hash(canonical.to_string_lossy().to_lowercase()))
}

fn account_hash(account_id: &str) -> Result<String, MailFfiError> {
    if account_id.is_empty() {
        return Err(validation_err(
            "Windows S/MIME key scope requires an account ID",
        ));
    }
    Ok(scope_hash(account_id))
}

fn win32_len(length: usize, value: &str) -> Result<u32, MailFfiError> {
    u32::try_from(length)
        .map_err(|_| validation_err(format!("{value} exceeds the Windows API size limit")))
}

fn encode_reference(reference: &CngKeyReference) -> Result<String, MailFfiError> {
    let json = serde_json::to_vec(reference)
        .map_err(|e| internal_err(format!("Failed to encode Windows key reference: {e}")))?;
    Ok(format!(
        "{CNG_REFERENCE_PREFIX}{}",
        base64::prelude::BASE64_URL_SAFE_NO_PAD.encode(json)
    ))
}

fn decode_reference(
    value: &str,
    scope: SmimeKeyScope<'_>,
) -> Result<CngKeyReference, MailFfiError> {
    let encoded = value
        .strip_prefix(CNG_REFERENCE_PREFIX)
        .ok_or_else(|| internal_err("Not a Windows CNG key reference"))?;
    let json = base64::prelude::BASE64_URL_SAFE_NO_PAD
        .decode(encoded)
        .map_err(|e| internal_err(format!("Invalid Windows key reference encoding: {e}")))?;
    let reference: CngKeyReference = serde_json::from_slice(&json)
        .map_err(|e| internal_err(format!("Invalid Windows key reference payload: {e}")))?;
    if reference.provider != SOFTWARE_KSP
        || reference.key_name.is_empty()
        || reference.profile_hash != canonical_profile_hash(scope.profile_path)?
        || reference.account_hash != account_hash(scope.account_id)?
        || reference.certificate_fingerprint.is_empty()
        || reference.public_key_hash.is_empty()
    {
        return Err(validation_err(
            "Windows S/MIME key reference failed provenance validation",
        ));
    }
    Ok(reference)
}

fn cert_info(cert: *const CERT_CONTEXT) -> Result<*const CERT_INFO, MailFfiError> {
    if cert.is_null() {
        return Err(internal_err("Windows certificate context is missing"));
    }
    let info = unsafe { (*cert).pCertInfo };
    if info.is_null() {
        return Err(internal_err("Windows certificate metadata is missing"));
    }
    Ok(info)
}

fn cert_bytes(cert: *const CERT_CONTEXT) -> Result<Vec<u8>, MailFfiError> {
    if cert.is_null() {
        return Err(internal_err("Windows certificate context is missing"));
    }
    let length = unsafe { (*cert).cbCertEncoded as usize };
    let bytes = unsafe { (*cert).pbCertEncoded };
    if length == 0 || bytes.is_null() {
        return Err(internal_err("Windows certificate encoding is missing"));
    }
    Ok(unsafe { slice::from_raw_parts(bytes, length) }.to_vec())
}

fn cert_from_pem(pem: &str) -> Result<Cert, MailFfiError> {
    let der = pem_decode("CERTIFICATE", pem)?;
    let der_len =
        u32::try_from(der.len()).map_err(|_| validation_err("Windows certificate is too large"))?;
    let cert = unsafe { CertCreateCertificateContext(ENCODING, der.as_ptr(), der_len) };
    if cert.is_null() {
        return Err(windows_error("CertCreateCertificateContext"));
    }
    Ok(Cert(cert))
}

fn cert_name(cert: *const CERT_CONTEXT, name_type: u32, flags: u32) -> String {
    let size = unsafe { CertGetNameStringW(cert, name_type, flags, null(), null_mut(), 0) };
    if size <= 1 {
        return String::new();
    }
    let mut buffer = vec![0u16; size as usize];
    let written =
        unsafe { CertGetNameStringW(cert, name_type, flags, null(), buffer.as_mut_ptr(), size) };
    if written <= 1 || written > size {
        return String::new();
    }
    String::from_utf16_lossy(&buffer[..written as usize - 1])
}

fn serial(cert: *const CERT_CONTEXT) -> Result<String, MailFfiError> {
    let info = cert_info(cert)?;
    let blob = unsafe { &(*info).SerialNumber };
    if blob.cbData == 0 || blob.pbData.is_null() {
        return Err(internal_err("Windows certificate serial number is missing"));
    }
    Ok(
        unsafe { slice::from_raw_parts(blob.pbData, blob.cbData as usize) }
            .iter()
            .rev()
            .map(|byte| format!("{byte:02X}"))
            .collect(),
    )
}

fn fingerprint(cert: *const CERT_CONTEXT) -> Result<String, MailFfiError> {
    let mut size = 0;
    if unsafe {
        CertGetCertificateContextProperty(cert, CERT_SHA256_HASH_PROP_ID, null_mut(), &mut size)
    } == 0
    {
        return Err(windows_error(
            "CertGetCertificateContextProperty(SHA-256 size)",
        ));
    }
    if size != 32 {
        return Err(validation_err(
            "Windows certificate fingerprint has an invalid size",
        ));
    }
    let mut bytes = vec![0u8; size as usize];
    if unsafe {
        CertGetCertificateContextProperty(
            cert,
            CERT_SHA256_HASH_PROP_ID,
            bytes.as_mut_ptr().cast(),
            &mut size,
        )
    } == 0
    {
        return Err(windows_error("CertGetCertificateContextProperty(SHA-256)"));
    }
    if size as usize != bytes.len() {
        return Err(validation_err(
            "Windows certificate fingerprint has an invalid size",
        ));
    }
    Ok(bytes[..size as usize]
        .iter()
        .map(|byte| format!("{byte:02X}"))
        .collect::<Vec<_>>()
        .join(":"))
}

fn public_key_hash(cert: *const CERT_CONTEXT) -> Result<String, MailFfiError> {
    let cert_info = cert_info(cert)?;
    let info = unsafe { &(*cert_info).SubjectPublicKeyInfo };
    unsafe {
        if info.Algorithm.pszObjId.is_null() {
            return Err(internal_err(
                "Windows certificate public key algorithm is missing",
            ));
        }
        let algorithm = CStr::from_ptr(info.Algorithm.pszObjId.cast()).to_bytes();
        let parameters = if info.Algorithm.Parameters.cbData == 0 {
            &[][..]
        } else if info.Algorithm.Parameters.pbData.is_null() {
            return Err(internal_err(
                "Windows certificate public key parameters are missing",
            ));
        } else {
            slice::from_raw_parts(
                info.Algorithm.Parameters.pbData,
                info.Algorithm.Parameters.cbData as usize,
            )
        };
        if info.PublicKey.cbData == 0 || info.PublicKey.pbData.is_null() {
            return Err(internal_err("Windows certificate public key is missing"));
        }
        let public_key =
            slice::from_raw_parts(info.PublicKey.pbData, info.PublicKey.cbData as usize);
        let mut hasher = Sha256::new();
        hasher.update(algorithm);
        hasher.update(parameters);
        hasher.update(info.PublicKey.cUnusedBits.to_le_bytes());
        hasher.update(public_key);
        Ok(hasher
            .finalize()
            .iter()
            .map(|byte| format!("{byte:02x}"))
            .collect())
    }
}

struct KeyProviderInfo {
    provider: String,
    key_name: String,
    provider_type: u32,
    key_spec: u32,
    flags: u32,
}

fn read_key_provider_info(
    cert: *const CERT_CONTEXT,
    acquired_key_spec: u32,
) -> Result<KeyProviderInfo, MailFfiError> {
    let mut size = 0;
    if unsafe {
        CertGetCertificateContextProperty(cert, CERT_KEY_PROV_INFO_PROP_ID, null_mut(), &mut size)
    } == 0
    {
        return Err(windows_error(
            "CertGetCertificateContextProperty(CNG provider info size)",
        ));
    }
    if (size as usize) < size_of::<CRYPT_KEY_PROV_INFO>() {
        return Err(validation_err("Windows key provider metadata is truncated"));
    }
    let word_size = size_of::<usize>();
    let words = (size as usize)
        .checked_add(word_size - 1)
        .ok_or_else(|| internal_err("Windows provider metadata size overflowed"))?
        / word_size;
    let mut buffer = vec![0usize; words];
    if unsafe {
        CertGetCertificateContextProperty(
            cert,
            CERT_KEY_PROV_INFO_PROP_ID,
            buffer.as_mut_ptr().cast(),
            &mut size,
        )
    } == 0
    {
        return Err(windows_error(
            "CertGetCertificateContextProperty(CNG provider info)",
        ));
    }
    if (size as usize) < size_of::<CRYPT_KEY_PROV_INFO>()
        || (size as usize) > std::mem::size_of_val(buffer.as_slice())
    {
        return Err(validation_err(
            "Windows key provider metadata has an invalid size",
        ));
    }
    let info = unsafe { &*(buffer.as_ptr().cast::<CRYPT_KEY_PROV_INFO>()) };
    let effective_key_spec = if info.dwKeySpec == 0 {
        acquired_key_spec
    } else {
        info.dwKeySpec
    };
    let provider = unsafe { wide_ptr_to_string(info.pwszProvName, &buffer, size as usize) }?;
    let key_name = unsafe { wide_ptr_to_string(info.pwszContainerName, &buffer, size as usize) }?;
    Ok(KeyProviderInfo {
        provider,
        key_name,
        provider_type: info.dwProvType,
        key_spec: effective_key_spec,
        flags: info.dwFlags,
    })
}

fn key_provider_info(
    cert: *const CERT_CONTEXT,
    acquired_key_spec: u32,
) -> Result<(String, String), MailFfiError> {
    let info = read_key_provider_info(cert, acquired_key_spec)?;
    if info.provider_type != 0
        || info.key_spec != CERT_NCRYPT_KEY_SPEC
        || info.flags & CRYPT_MACHINE_KEYSET != 0
    {
        return Err(validation_err(format!(
            "P12/PFX private key was not persisted as a user-scoped CNG key: \
             provider_type={}, property_key_spec=0x{:08X}, acquired_key_spec=0x{:08X}, \
             flags=0x{:08X}",
            info.provider_type, info.key_spec, acquired_key_spec, info.flags
        )));
    }
    if info.provider != SOFTWARE_KSP || info.key_name.is_empty() {
        return Err(validation_err(
            "P12/PFX private key uses an unsupported Windows key provider",
        ));
    }
    Ok((info.provider, info.key_name))
}

fn export_policy(key: NCRYPT_KEY_HANDLE) -> Result<u32, MailFfiError> {
    let mut policy = 0u32;
    let mut size = size_of::<u32>() as u32;
    let status = unsafe {
        NCryptGetProperty(
            key,
            NCRYPT_EXPORT_POLICY_PROPERTY,
            (&mut policy as *mut u32).cast(),
            size,
            &mut size,
            0,
        )
    };
    if status < 0 {
        return Err(ncrypt_error("NCryptGetProperty(Export Policy)", status));
    }
    if size as usize != size_of::<u32>() {
        return Err(validation_err(
            "Windows CNG export policy has an invalid size",
        ));
    }
    Ok(policy)
}

fn open_named_key_unchecked(
    provider_name: &str,
    key_name: &str,
) -> Result<(NcryptProvider, NcryptKey), MailFfiError> {
    let mut provider = 0;
    let provider_name = wide(provider_name);
    let status = unsafe { NCryptOpenStorageProvider(&mut provider, provider_name.as_ptr(), 0) };
    if status < 0 {
        return Err(ncrypt_error("NCryptOpenStorageProvider", status));
    }
    let provider = NcryptProvider(provider);
    let mut key = 0;
    let key_name = wide(key_name);
    let status = unsafe {
        NCryptOpenKey(
            provider.0,
            &mut key,
            key_name.as_ptr(),
            0,
            NCRYPT_SILENT_FLAG,
        )
    };
    if status < 0 {
        return Err(ncrypt_error("NCryptOpenKey", status));
    }
    let key = NcryptKey {
        handle: key,
        owned: true,
    };
    Ok((provider, key))
}

fn open_named_key(
    provider_name: &str,
    key_name: &str,
) -> Result<(NcryptProvider, NcryptKey), MailFfiError> {
    let (provider, key) = open_named_key_unchecked(provider_name, key_name)?;
    if export_policy(key.handle)? & EXPORTABLE_POLICY_FLAGS != 0 {
        return Err(validation_err("Windows S/MIME private key is exportable"));
    }
    Ok((provider, key))
}

fn open_persisted_key(
    reference: &CngKeyReference,
) -> Result<(NcryptProvider, NcryptKey), MailFfiError> {
    open_named_key(&reference.provider, &reference.key_name)
}

fn delete_named_key(provider_name: &str, key_name: &str) -> Result<(), MailFfiError> {
    let (_provider, key) = match open_named_key_unchecked(provider_name, key_name) {
        Ok(value) => value,
        Err(_) if named_key_is_absent(provider_name, key_name)? => return Ok(()),
        Err(error) => return Err(error),
    };
    delete_key(key)
}

fn named_key_is_absent(provider_name: &str, key_name: &str) -> Result<bool, MailFfiError> {
    let mut provider = 0;
    let provider_name = wide(provider_name);
    let status = unsafe { NCryptOpenStorageProvider(&mut provider, provider_name.as_ptr(), 0) };
    if status < 0 {
        return Err(ncrypt_error("NCryptOpenStorageProvider", status));
    }
    let provider = NcryptProvider(provider);
    let mut key = 0;
    let key_name = wide(key_name);
    let status = unsafe {
        NCryptOpenKey(
            provider.0,
            &mut key,
            key_name.as_ptr(),
            0,
            NCRYPT_SILENT_FLAG,
        )
    };
    if status == NTE_BAD_KEYSET || status == NTE_NOT_FOUND {
        return Ok(true);
    }
    if status < 0 {
        return Err(ncrypt_error("NCryptOpenKey", status));
    }
    unsafe { NCryptFreeObject(key) };
    Ok(false)
}

fn pkcs12_private_key_certificates(
    p12_der: &[u8],
    password: &[u16],
) -> Result<Vec<String>, MailFfiError> {
    let mut blob = CRYPT_INTEGER_BLOB {
        cbData: win32_len(p12_der.len(), "P12/PFX input")?,
        pbData: p12_der.as_ptr().cast_mut(),
    };
    let store = Store(unsafe {
        PFXImportCertStore(
            &mut blob,
            password.as_ptr(),
            PKCS12_ALWAYS_CNG_KSP | PKCS12_NO_PERSIST_KEY | PKCS12_IMPORT_SILENT,
        )
    });
    if store.0.is_null() {
        return Err(validation_err(format!(
            "Failed to decrypt P12/PFX: Windows error 0x{:08X}",
            unsafe { GetLastError() }
        )));
    }

    let mut certificates = Vec::new();
    let mut previous = null();
    loop {
        let current = unsafe { CertEnumCertificatesInStore(store.0, previous) };
        if current.is_null() {
            break;
        }
        previous = current;
        let mut property_size = 0;
        if unsafe {
            CertGetCertificateContextProperty(
                current,
                CERT_KEY_CONTEXT_PROP_ID,
                null_mut(),
                &mut property_size,
            )
        } == 0
            || property_size as usize != size_of::<CERT_KEY_CONTEXT>()
        {
            continue;
        }
        certificates.push(scope_hash(cert_bytes(current)?));
    }
    Ok(certificates)
}

fn verify_key_matches_certificate(
    key: NCRYPT_KEY_HANDLE,
    cert: *const CERT_CONTEXT,
    expected_public_key_hash: &str,
) -> Result<(), MailFfiError> {
    let mut size = 0;
    if unsafe {
        CryptExportPublicKeyInfo(
            key,
            CERT_NCRYPT_KEY_SPEC,
            X509_ASN_ENCODING,
            null_mut(),
            &mut size,
        )
    } == 0
    {
        return Err(windows_error("CryptExportPublicKeyInfo size"));
    }
    if (size as usize) < size_of::<CERT_PUBLIC_KEY_INFO>() {
        return Err(validation_err(
            "Windows exported public key metadata is truncated",
        ));
    }
    let word_size = size_of::<usize>();
    let words = (size as usize)
        .checked_add(word_size - 1)
        .ok_or_else(|| internal_err("Windows public key buffer size overflowed"))?
        / word_size;
    let mut buffer = vec![0usize; words];
    let key_info = buffer.as_mut_ptr().cast::<CERT_PUBLIC_KEY_INFO>();
    if unsafe {
        CryptExportPublicKeyInfo(
            key,
            CERT_NCRYPT_KEY_SPEC,
            X509_ASN_ENCODING,
            key_info,
            &mut size,
        )
    } == 0
    {
        return Err(windows_error("CryptExportPublicKeyInfo"));
    }
    if (size as usize) < size_of::<CERT_PUBLIC_KEY_INFO>()
        || (size as usize) > std::mem::size_of_val(buffer.as_slice())
    {
        return Err(validation_err(
            "Windows exported public key metadata has an invalid size",
        ));
    }
    let certificate_info = cert_info(cert)?;
    let certificate_key = unsafe { &(*certificate_info).SubjectPublicKeyInfo };
    if unsafe { CertComparePublicKeyInfo(X509_ASN_ENCODING, key_info, certificate_key) } == 0
        || public_key_hash(cert)? != expected_public_key_hash
    {
        return Err(validation_err(
            "Windows S/MIME key does not match the stored certificate",
        ));
    }
    Ok(())
}

fn filetime_string(value: FILETIME) -> String {
    let ticks = ((value.dwHighDateTime as u64) << 32) | value.dwLowDateTime as u64;
    let unix_100ns = ticks as i128 - 116_444_736_000_000_000i128;
    let seconds = unix_100ns.div_euclid(10_000_000);
    let nanos = unix_100ns.rem_euclid(10_000_000) * 100;
    i64::try_from(seconds)
        .ok()
        .and_then(|seconds| DateTime::<Utc>::from_timestamp(seconds, nanos as u32))
        .map(|value| value.format("%b %e %H:%M:%S %Y GMT").to_string())
        .unwrap_or_default()
}

fn import_key(key_pem: &str) -> Result<(NcryptProvider, NcryptKey), MailFfiError> {
    let der = pem_decode("PRIVATE KEY", key_pem)?;
    let mut provider = 0;
    let status = unsafe { NCryptOpenStorageProvider(&mut provider, MS_KEY_STORAGE_PROVIDER, 0) };
    if status < 0 {
        return Err(ncrypt_error("NCryptOpenStorageProvider", status));
    }
    let provider = NcryptProvider(provider);
    let mut key = 0;
    let status = unsafe {
        NCryptImportKey(
            provider.0,
            0,
            NCRYPT_PKCS8_PRIVATE_KEY_BLOB,
            null(),
            &mut key,
            der.as_ptr(),
            win32_len(der.len(), "Private key")?,
            NCRYPT_SILENT_FLAG,
        )
    };
    if status < 0 {
        return Err(ncrypt_error("NCryptImportKey(PKCS#8)", status));
    }
    Ok((
        provider,
        NcryptKey {
            handle: key,
            owned: true,
        },
    ))
}

fn resolve_key(
    cert: *const CERT_CONTEXT,
    key_pem: &str,
    scope: SmimeKeyScope<'_>,
) -> Result<(NcryptProvider, NcryptKey), MailFfiError> {
    if !key_pem.starts_with(CNG_REFERENCE_PREFIX) {
        let (provider, key) = import_key(key_pem)?;
        if export_policy(key.handle)? & EXPORTABLE_POLICY_FLAGS != 0 {
            return Err(validation_err("Windows S/MIME private key is exportable"));
        }
        let certificate_public_key_hash = public_key_hash(cert)?;
        verify_key_matches_certificate(key.handle, cert, &certificate_public_key_hash)?;
        return Ok((provider, key));
    }
    let reference = decode_reference(key_pem, scope)?;
    if fingerprint(cert)? != reference.certificate_fingerprint {
        return Err(validation_err(
            "Windows S/MIME key reference does not match the stored certificate",
        ));
    }
    let (provider, key) = open_persisted_key(&reference)?;
    verify_key_matches_certificate(key.handle, cert, &reference.public_key_hash)?;
    Ok((provider, key))
}

fn delete_key(mut key: NcryptKey) -> Result<(), MailFfiError> {
    let status = unsafe { NCryptDeleteKey(key.handle, NCRYPT_SILENT_FLAG) };
    if status < 0 {
        return Err(ncrypt_error("NCryptDeleteKey", status));
    }
    key.disarm();
    Ok(())
}

fn fail_after_delete<T>(key: NcryptKey, error: MailFfiError) -> Result<T, MailFfiError> {
    match delete_key(key) {
        Ok(()) => Err(error),
        Err(cleanup_error) => Err(internal_err(format!(
            "{error}; CNG key cleanup also failed: {cleanup_error}",
        ))),
    }
}

fn bind_key(cert: *const CERT_CONTEXT, key: NCRYPT_KEY_HANDLE) -> Result<(), MailFfiError> {
    let context = CERT_KEY_CONTEXT {
        cbSize: size_of::<CERT_KEY_CONTEXT>() as u32,
        Anonymous: CERT_KEY_CONTEXT_0 { hNCryptKey: key },
        dwKeySpec: CERT_NCRYPT_KEY_SPEC,
    };
    if unsafe {
        CertSetCertificateContextProperty(
            cert,
            CERT_KEY_CONTEXT_PROP_ID,
            0,
            (&context as *const CERT_KEY_CONTEXT).cast(),
        )
    } == 0
    {
        return Err(windows_error("CertSetCertificateContextProperty(CNG key)"));
    }
    Ok(())
}

fn two_pass(
    mut call: impl FnMut(*mut u8, &mut u32) -> i32,
    operation: &str,
) -> Result<Vec<u8>, MailFfiError> {
    let mut size = 0;
    if call(null_mut(), &mut size) == 0 {
        return Err(windows_error(operation));
    }
    if size == 0 {
        return Err(validation_err(format!(
            "{operation} returned an empty output size",
        )));
    }
    let mut output = vec![0u8; size as usize];
    if call(output.as_mut_ptr(), &mut size) == 0 {
        return Err(windows_error(operation));
    }
    if (size as usize) > output.len() {
        return Err(validation_err(format!(
            "{operation} returned an invalid output size",
        )));
    }
    output.truncate(size as usize);
    Ok(output)
}

fn signer_identity(cert: *const CERT_CONTEXT) -> (Option<String>, Option<String>) {
    let email = cert_name(cert, CERT_NAME_EMAIL_TYPE, 0);
    let subject = cert_name(cert, CERT_NAME_RDN_TYPE, 0);
    (
        (!email.is_empty()).then_some(email),
        (!subject.is_empty()).then_some(subject),
    )
}

fn certificate_trusted(cert: *const CERT_CONTEXT) -> bool {
    if cert.is_null() {
        return false;
    }
    let mut usage_identifier = szOID_PKIX_KP_EMAIL_PROTECTION as *mut u8;
    let mut chain_para = CERT_CHAIN_PARA::default();
    chain_para.cbSize = size_of::<CERT_CHAIN_PARA>() as u32;
    chain_para.RequestedUsage = CERT_USAGE_MATCH {
        dwType: USAGE_MATCH_TYPE_AND,
        Usage: CTL_USAGE {
            cUsageIdentifier: 1,
            rgpszUsageIdentifier: &mut usage_identifier,
        },
    };
    let mut chain = null_mut();
    if unsafe {
        CertGetCertificateChain(
            null_mut(),
            cert,
            null(),
            null_mut(),
            &chain_para,
            CERT_CHAIN_REVOCATION_CHECK_END_CERT,
            null(),
            &mut chain,
        )
    } == 0
    {
        return false;
    }
    let mut policy = CERT_CHAIN_POLICY_PARA::default();
    policy.cbSize = size_of::<CERT_CHAIN_POLICY_PARA>() as u32;
    let mut status = CERT_CHAIN_POLICY_STATUS::default();
    status.cbSize = size_of::<CERT_CHAIN_POLICY_STATUS>() as u32;
    let ok = unsafe {
        CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_BASE, chain, &policy, &mut status)
    } != 0
        && status.dwError == 0;
    unsafe { CertFreeCertificateChain(chain) };
    ok
}

impl SmimeBackend for WindowsBackend {
    fn import_pkcs12(
        &self,
        p12_der: &[u8],
        password: &str,
        scope: SmimeKeyScope<'_>,
    ) -> Result<ImportedIdentity, MailFfiError> {
        let profile_hash = canonical_profile_hash(scope.profile_path)?;
        let account_hash = account_hash(scope.account_id)?;
        let password = wide(password);
        let private_key_certificates = pkcs12_private_key_certificates(p12_der, &password)?;
        if private_key_certificates.len() != 1 {
            return Err(validation_err(format!(
                "P12/PFX must contain exactly one private key, found {}",
                private_key_certificates.len(),
            )));
        }
        let private_key_certificate = &private_key_certificates[0];
        let mut blob = CRYPT_INTEGER_BLOB {
            cbData: win32_len(p12_der.len(), "P12/PFX input")?,
            pbData: p12_der.as_ptr() as *mut u8,
        };
        let store = unsafe {
            PFXImportCertStore(
                &mut blob,
                password.as_ptr(),
                PKCS12_ALWAYS_CNG_KSP | PKCS12_IMPORT_SILENT,
            )
        };
        if store.is_null() {
            return Err(validation_err(format!(
                "Failed to decrypt P12/PFX: Windows error 0x{:08X}",
                unsafe { GetLastError() }
            )));
        }
        let store = Store(store);
        let mut previous = null();
        let mut target_acquisition_error = None;
        let (cert, provider_name, key_name, provider, key) = loop {
            let current = unsafe { CertEnumCertificatesInStore(store.0, previous) };
            if current.is_null() {
                return Err(target_acquisition_error.unwrap_or_else(|| {
                    validation_err("P12/PFX file does not contain a certificate with a private key")
                }));
            }
            previous = current;
            let current_certificate = scope_hash(cert_bytes(current)?);
            if current_certificate != *private_key_certificate {
                continue;
            }
            let mut handle = 0;
            let mut key_spec = 0;
            let mut caller_free = 0;
            const ACQUIRE_FLAGS: u32 =
                CRYPT_ACQUIRE_ONLY_NCRYPT_KEY_FLAG | CRYPT_ACQUIRE_SILENT_FLAG;
            let acquired = unsafe {
                CryptAcquireCertificatePrivateKey(
                    current,
                    ACQUIRE_FLAGS,
                    null(),
                    &mut handle,
                    &mut key_spec,
                    &mut caller_free,
                )
            } != 0;
            let acquire_error = if acquired {
                validation_err("P12/PFX private key was not acquired as a CNG key")
            } else {
                windows_error("CryptAcquireCertificatePrivateKey(P12/PFX)")
            };
            if acquired && key_spec == CERT_NCRYPT_KEY_SPEC {
                let acquired_key = NcryptKey {
                    handle,
                    owned: caller_free != 0,
                };
                let duplicate = unsafe { CertDuplicateCertificateContext(current) };
                if duplicate.is_null() {
                    return fail_after_delete(
                        acquired_key,
                        windows_error("CertDuplicateCertificateContext"),
                    );
                }
                let cert = Cert(duplicate);
                unsafe { CertFreeCertificateContext(current) };
                let (provider_name, key_name) = match key_provider_info(cert.0, key_spec) {
                    Ok(value) => value,
                    Err(error) => {
                        return fail_after_delete(acquired_key, error);
                    }
                };
                let (provider, key) = match open_named_key(&provider_name, &key_name) {
                    Ok(value) => value,
                    Err(error) => {
                        drop(acquired_key);
                        return match delete_named_key(&provider_name, &key_name) {
                            Ok(()) => Err(error),
                            Err(cleanup_error) => Err(internal_err(format!(
                                "{error}; CNG key cleanup also failed: {cleanup_error}",
                            ))),
                        };
                    }
                };
                drop(acquired_key);
                break (cert, provider_name, key_name, provider, key);
            }
            if caller_free != 0 && handle != 0 {
                if key_spec == CERT_NCRYPT_KEY_SPEC {
                    unsafe { NCryptFreeObject(handle) };
                } else {
                    unsafe { CryptReleaseContext(handle as usize, 0) };
                }
            }
            match read_key_provider_info(current, CERT_NCRYPT_KEY_SPEC) {
                Ok(info)
                    if info.provider_type == 0
                        && info.key_spec == CERT_NCRYPT_KEY_SPEC
                        && !info.provider.is_empty()
                        && !info.key_name.is_empty() =>
                {
                    unsafe { CertFreeCertificateContext(current) };
                    return match delete_named_key(&info.provider, &info.key_name) {
                        Ok(()) => Err(acquire_error),
                        Err(cleanup_error) => Err(internal_err(format!(
                            "{acquire_error}; CNG key cleanup also failed: {cleanup_error}",
                        ))),
                    };
                }
                Err(_) if target_acquisition_error.is_none() => {
                    target_acquisition_error = Some(acquire_error);
                }
                Ok(_) => {}
                Err(_) => {}
            }
        };
        let certificate_fingerprint = match fingerprint(cert.0) {
            Ok(value) => value,
            Err(error) => {
                return fail_after_delete(key, error);
            }
        };
        let public_key_hash = match public_key_hash(cert.0) {
            Ok(value) => value,
            Err(error) => return fail_after_delete(key, error),
        };
        if let Err(error) = (|| {
            if export_policy(key.handle)? & EXPORTABLE_POLICY_FLAGS != 0 {
                return Err(validation_err(
                    "P12/PFX private key was persisted as exportable",
                ));
            }
            verify_key_matches_certificate(key.handle, cert.0, &public_key_hash)
        })() {
            return fail_after_delete(key, error);
        }
        let reference = CngKeyReference {
            provider: provider_name,
            key_name,
            profile_hash,
            account_hash,
            certificate_fingerprint: certificate_fingerprint.clone(),
            public_key_hash,
        };
        let key_pem = match encode_reference(&reference) {
            Ok(value) => value,
            Err(error) => {
                return fail_after_delete(key, error);
            }
        };
        let (_provider, reopened_key) = match open_persisted_key(&reference) {
            Ok(value) => value,
            Err(error) => {
                return fail_after_delete(key, error);
            }
        };
        drop(key);
        drop(provider);
        drop(store);
        let identity = (|| {
            verify_key_matches_certificate(
                reopened_key.handle,
                cert.0,
                &reference.public_key_hash,
            )?;
            let info = cert_info(cert.0)?;
            Ok(ImportedIdentity {
                cert_pem: pem_encode("CERTIFICATE", &cert_bytes(cert.0)?),
                key_pem,
                email: cert_name(cert.0, CERT_NAME_EMAIL_TYPE, 0),
                subject: cert_name(cert.0, CERT_NAME_RDN_TYPE, 0),
                issuer: cert_name(cert.0, CERT_NAME_RDN_TYPE, CERT_NAME_ISSUER_FLAG),
                serial_number: serial(cert.0)?,
                fingerprint: certificate_fingerprint,
                not_before: filetime_string(unsafe { (*info).NotBefore }),
                not_after: filetime_string(unsafe { (*info).NotAfter }),
            })
        })();
        match identity {
            Ok(identity) => Ok(identity),
            Err(error) => fail_after_delete(reopened_key, error),
        }
    }

    fn sign(
        &self,
        cert_pem: &str,
        key_pem: &str,
        body: &str,
        scope: SmimeKeyScope<'_>,
    ) -> Result<String, MailFfiError> {
        let cert = cert_from_pem(cert_pem)?;
        let (_provider, mut key) = resolve_key(cert.0, key_pem, scope)?;
        bind_key(cert.0, key.handle)?;
        key.disarm();
        let mut para = CRYPT_SIGN_MESSAGE_PARA::default();
        para.cbSize = size_of::<CRYPT_SIGN_MESSAGE_PARA>() as u32;
        para.dwMsgEncodingType = ENCODING;
        para.pSigningCert = cert.0;
        para.HashAlgorithm.pszObjId = szOID_NIST_sha256 as *mut u8;
        let mut message_certs = [cert.0];
        para.cMsgCert = 1;
        para.rgpMsgCert = message_certs.as_mut_ptr();
        let data = body.as_bytes();
        let data_ptr = data.as_ptr();
        let data_len = win32_len(data.len(), "S/MIME signed body")?;
        let signature = two_pass(
            |output, size| unsafe {
                CryptSignMessage(&para, 1, 1, &data_ptr, &data_len, output, size)
            },
            "CryptSignMessage",
        )?;
        Ok(multipart_signed(body, &signature))
    }

    fn encrypt(
        &self,
        recipients: &[String],
        sender: Option<&str>,
        body: &str,
    ) -> Result<String, MailFfiError> {
        let mut certs = Vec::new();
        let mut fingerprints = Vec::new();
        for pem in recipients {
            let cert = cert_from_pem(pem)?;
            fingerprints.push(fingerprint(cert.0)?);
            certs.push(cert);
        }
        if let Some(sender) = sender {
            if let Ok(cert) = cert_from_pem(sender) {
                if fingerprint(cert.0)
                    .map(|fp| !fingerprints.contains(&fp))
                    .unwrap_or(false)
                {
                    certs.push(cert);
                }
            }
        }
        if certs.is_empty() {
            return Err(validation_err(
                "At least one S/MIME recipient certificate is required",
            ));
        }
        let pointers: Vec<*const CERT_CONTEXT> =
            certs.iter().map(|cert| cert.0 as *const _).collect();
        let mut para = CRYPT_ENCRYPT_MESSAGE_PARA::default();
        para.cbSize = size_of::<CRYPT_ENCRYPT_MESSAGE_PARA>() as u32;
        para.dwMsgEncodingType = ENCODING;
        para.ContentEncryptionAlgorithm.pszObjId = szOID_NIST_AES256_CBC as *mut u8;
        let recipient_count = win32_len(pointers.len(), "S/MIME recipient count")?;
        let body_len = win32_len(body.len(), "S/MIME encrypted body")?;
        let der = two_pass(
            |output, size| unsafe {
                CryptEncryptMessage(
                    &para,
                    recipient_count,
                    pointers.as_ptr(),
                    body.as_ptr(),
                    body_len,
                    output,
                    size,
                )
            },
            "CryptEncryptMessage",
        )?;
        Ok(smime_opaque("enveloped-data", &der))
    }

    fn decrypt(
        &self,
        cert_pem: &str,
        key_pem: &str,
        encrypted: &str,
        scope: SmimeKeyScope<'_>,
    ) -> Result<String, MailFfiError> {
        let cert = cert_from_pem(cert_pem)?;
        let (_provider, mut key) = resolve_key(cert.0, key_pem, scope)?;
        let store = Store(unsafe {
            CertOpenStore(
                CERT_STORE_PROV_MEMORY,
                0,
                0,
                CERT_STORE_CREATE_NEW_FLAG,
                null(),
            )
        });
        if store.0.is_null() {
            return Err(windows_error("CertOpenStore"));
        }
        let mut store_cert = null_mut();
        if unsafe {
            CertAddCertificateContextToStore(
                store.0,
                cert.0,
                CERT_STORE_ADD_ALWAYS,
                &mut store_cert,
            )
        } == 0
        {
            return Err(windows_error("CertAddCertificateContextToStore"));
        }
        let store_cert = Cert(store_cert);
        bind_key(store_cert.0, key.handle)?;
        // CERT_KEY_CONTEXT_PROP_ID owns the bound handle after a successful set.
        // The store was deliberately opened without NO_CRYPT_RELEASE, so its
        // certificate context releases exactly this handle during teardown.
        key.disarm();
        let mut stores = [store.0];
        let mut para = CRYPT_DECRYPT_MESSAGE_PARA::default();
        para.cbSize = size_of::<CRYPT_DECRYPT_MESSAGE_PARA>() as u32;
        para.dwMsgAndCertEncodingType = ENCODING;
        para.cCertStore = 1;
        para.rghCertStore = stores.as_mut_ptr();
        let cms = decode_cms(encrypted)?;
        let cms_len = win32_len(cms.len(), "S/MIME encrypted message")?;
        let plaintext = two_pass(
            |output, size| unsafe {
                CryptDecryptMessage(&para, cms.as_ptr(), cms_len, output, size, null_mut())
            },
            "CryptDecryptMessage",
        )?;
        String::from_utf8(plaintext)
            .map_err(|e| internal_err(format!("Decrypted data is not valid UTF-8: {e}")))
    }

    fn delete_private_key(
        &self,
        cert_pem: &str,
        key_pem: &str,
        scope: SmimeKeyScope<'_>,
    ) -> Result<(), MailFfiError> {
        if !key_pem.starts_with(CNG_REFERENCE_PREFIX) {
            return Ok(());
        }
        let cert = cert_from_pem(cert_pem)?;
        let reference = decode_reference(key_pem, scope)?;
        if fingerprint(cert.0)? != reference.certificate_fingerprint {
            return Err(validation_err(
                "Windows S/MIME key reference does not match the stored certificate",
            ));
        }
        let (_provider, key) = match open_persisted_key(&reference) {
            Ok(value) => value,
            Err(_) if named_key_is_absent(&reference.provider, &reference.key_name)? => {
                return Ok(())
            }
            Err(error) => return Err(error),
        };
        verify_key_matches_certificate(key.handle, cert.0, &reference.public_key_hash)?;
        delete_key(key)
    }

    fn certificate_fingerprint(&self, cert_pem: &str) -> Result<String, MailFfiError> {
        let cert = cert_from_pem(cert_pem)?;
        fingerprint(cert.0)
    }

    fn verify(&self, signed: &str) -> SmimeVerifyResult {
        let mut para = CRYPT_VERIFY_MESSAGE_PARA::default();
        para.cbSize = size_of::<CRYPT_VERIFY_MESSAGE_PARA>() as u32;
        para.dwMsgAndCertEncodingType = ENCODING;
        let mut signer = null_mut();
        let verified = if let Some((body, signature)) = detached_parts(signed) {
            let body_ptr = body.as_ptr();
            if let (Ok(body_len), Ok(signature_len)) = (
                win32_len(body.len(), "S/MIME signed body"),
                win32_len(signature.len(), "S/MIME signature"),
            ) {
                (unsafe {
                    CryptVerifyDetachedMessageSignature(
                        &para,
                        0,
                        signature.as_ptr(),
                        signature_len,
                        1,
                        &body_ptr,
                        &body_len,
                        &mut signer,
                    )
                }) != 0
            } else {
                false
            }
        } else if let Ok(cms) = decode_cms(signed) {
            let mut decoded_size = 0;
            if let Ok(cms_len) = win32_len(cms.len(), "S/MIME signed message") {
                (unsafe {
                    CryptVerifyMessageSignature(
                        &para,
                        0,
                        cms.as_ptr(),
                        cms_len,
                        null_mut(),
                        &mut decoded_size,
                        &mut signer,
                    )
                }) != 0
            } else {
                false
            }
        } else {
            false
        };
        if !verified || signer.is_null() {
            if !signer.is_null() {
                unsafe { CertFreeCertificateContext(signer) };
            }
            return SmimeVerifyResult {
                valid: false,
                trusted: false,
                signer_email: None,
                signer_subject: Some("Failed to parse or verify S/MIME data".to_string()),
            };
        }
        let signer = Cert(signer);
        let trusted = certificate_trusted(signer.0);
        let (email, mut subject) = signer_identity(signer.0);
        if !trusted {
            subject = subject.map(|value| format!("{value} (certificate not trusted)"));
        }
        SmimeVerifyResult {
            valid: true,
            trusted,
            signer_email: email,
            signer_subject: subject,
        }
    }
}

#[cfg(test)]
mod tests {
    use std::fs;
    use std::process::Command;

    use uuid::Uuid;

    use super::*;

    struct TempTree(std::path::PathBuf);

    impl Drop for TempTree {
        fn drop(&mut self) {
            let _ = fs::remove_dir_all(&self.0);
        }
    }

    struct ImportedKeyGuard<'a> {
        identity: &'a ImportedIdentity,
        scope: SmimeKeyScope<'a>,
        armed: bool,
    }

    impl Drop for ImportedKeyGuard<'_> {
        fn drop(&mut self) {
            if self.armed {
                let _ = WINDOWS_BACKEND.delete_private_key(
                    &self.identity.cert_pem,
                    &self.identity.key_pem,
                    self.scope,
                );
            }
        }
    }

    const TEST_PFX_PASSWORD: &str = "maho-test-password";

    /// Creates an isolated temp tree with a `profile` directory and a freshly
    /// generated self-signed PFX, returning `(temp_tree, profile, pfx_bytes)`.
    ///
    /// The certificate is generated with `-KeyExportPolicy Exportable` so the
    /// PFX can be exported at all; the resulting key material is then removed
    /// from `Cert:\CurrentUser\My`, leaving the on-disk PFX as the only copy.
    /// Non-exportability is a property the CNG *import* path must enforce, and
    /// is asserted separately by `imported_key_is_not_exportable`.
    fn generate_test_pfx() -> (TempTree, std::path::PathBuf, Vec<u8>) {
        let root =
            std::env::temp_dir().join(format!("maho-smime-cng-{}", Uuid::new_v4().as_hyphenated()));
        let temp_tree = TempTree(root.clone());
        let profile = root.join("profile");
        let pfx_path = root.join("identity.pfx");
        fs::create_dir_all(&profile).expect("create test profile");
        let escaped_pfx = pfx_path.to_string_lossy().replace('\'', "''");
        let script = format!(
            "$ErrorActionPreference='Stop'; \
             $cert=New-SelfSignedCertificate \
               -Subject 'CN=Maho S-MIME CNG Test' \
               -CertStoreLocation 'Cert:\\CurrentUser\\My' \
               -KeyAlgorithm RSA -KeyLength 2048 \
               -KeyExportPolicy Exportable -KeySpec KeyExchange \
               -KeyUsage DigitalSignature,KeyEncipherment; \
             try {{ \
               $password=ConvertTo-SecureString '{TEST_PFX_PASSWORD}' \
                 -AsPlainText -Force; \
               Export-PfxCertificate -Cert $cert -FilePath '{escaped_pfx}' \
                 -Password $password | Out-Null \
             }} finally {{ \
               Remove-Item -LiteralPath \
                 ('Cert:\\CurrentUser\\My\\' + $cert.Thumbprint) \
                 -DeleteKey -Force \
             }}"
        );
        let output = Command::new("powershell.exe")
            .args(["-NoProfile", "-NonInteractive", "-Command", &script])
            .output()
            .expect("launch PowerShell");
        assert!(
            output.status.success(),
            "PowerShell PFX generation failed: {}",
            String::from_utf8_lossy(&output.stderr)
        );
        let pfx = fs::read(&pfx_path).expect("read generated PFX");
        (temp_tree, profile, pfx)
    }

    #[test]
    fn imported_key_persists_reopens_and_deletes() {
        let (_temp_tree, profile, pfx) = generate_test_pfx();
        let account_id = Uuid::new_v4().to_string();
        let scope = SmimeKeyScope {
            profile_path: &profile,
            account_id: &account_id,
        };
        let identity = WINDOWS_BACKEND
            .import_pkcs12(&pfx, TEST_PFX_PASSWORD, scope)
            .expect("import PFX into user-scoped CNG");
        assert!(identity.key_pem.starts_with(CNG_REFERENCE_PREFIX));
        let mut key_guard = ImportedKeyGuard {
            identity: &identity,
            scope,
            armed: true,
        };

        let signed = WINDOWS_BACKEND
            .sign(
                &identity.cert_pem,
                &identity.key_pem,
                "persistent signing key",
                scope,
            )
            .expect("reopen persisted key for signing");
        assert!(WINDOWS_BACKEND.verify(&signed).valid);

        let recipient_certs = [identity.cert_pem.clone()];
        let encrypted = WINDOWS_BACKEND
            .encrypt(&recipient_certs, None, "persistent decryption key")
            .expect("encrypt with imported certificate");
        assert_eq!(
            WINDOWS_BACKEND
                .decrypt(&identity.cert_pem, &identity.key_pem, &encrypted, scope,)
                .expect("reopen persisted key for decryption"),
            "persistent decryption key"
        );

        WINDOWS_BACKEND
            .delete_private_key(&identity.cert_pem, &identity.key_pem, scope)
            .expect("delete persisted CNG key");
        key_guard.armed = false;
        assert!(WINDOWS_BACKEND
            .sign(
                &identity.cert_pem,
                &identity.key_pem,
                "deleted signing key",
                scope,
            )
            .is_err());
    }

    /// The imported CNG key must be non-exportable: neither the plaintext nor
    /// the encrypted export policy flag may be set on the persisted key, and a
    /// direct `NCryptExportKey` attempt for the private blob must fail.
    #[test]
    fn imported_key_is_not_exportable() {
        let (_temp_tree, profile, pfx) = generate_test_pfx();
        let account_id = Uuid::new_v4().to_string();
        let scope = SmimeKeyScope {
            profile_path: &profile,
            account_id: &account_id,
        };
        let identity = WINDOWS_BACKEND
            .import_pkcs12(&pfx, TEST_PFX_PASSWORD, scope)
            .expect("import PFX into user-scoped CNG");
        let _key_guard = ImportedKeyGuard {
            identity: &identity,
            scope,
            armed: true,
        };

        let reference = decode_reference(&identity.key_pem, scope).expect("decode key reference");
        let (_provider, key) =
            open_named_key_unchecked(&reference.provider, &reference.key_name).expect("open key");

        let policy = export_policy(key.handle).expect("read export policy");
        assert_eq!(
            policy & EXPORTABLE_POLICY_FLAGS,
            0,
            "persisted CNG key reports an exportable policy: 0x{policy:08X}"
        );

        // The policy flags are advisory metadata; assert the KSP actually
        // refuses to hand back private key material.
        let mut needed = 0u32;
        let status = unsafe {
            NCryptExportKey(
                key.handle,
                0,
                BCRYPT_RSAFULLPRIVATE_BLOB,
                null(),
                null_mut(),
                0,
                &mut needed,
                NCRYPT_SILENT_FLAG,
            )
        };
        assert!(
            status < 0,
            "NCryptExportKey unexpectedly succeeded for a non-exportable key (status 0x{status:08X}, {needed} bytes)"
        );
    }

    /// A wrong PFX password must be rejected as a validation error and must not
    /// leave any CNG key material behind.
    #[test]
    fn import_with_wrong_password_is_rejected() {
        let (_temp_tree, profile, pfx) = generate_test_pfx();
        let account_id = Uuid::new_v4().to_string();
        let scope = SmimeKeyScope {
            profile_path: &profile,
            account_id: &account_id,
        };
        let error = WINDOWS_BACKEND
            .import_pkcs12(&pfx, "definitely-the-wrong-password", scope)
            .expect_err("import must fail with an incorrect password");
        let message = error.to_string();
        assert!(
            message.contains("Failed to decrypt P12/PFX"),
            "unexpected error for wrong password: {message}"
        );

        // The correct password must still work afterwards, proving the failed
        // attempt left no partial state behind.
        let identity = WINDOWS_BACKEND
            .import_pkcs12(&pfx, TEST_PFX_PASSWORD, scope)
            .expect("import with the correct password after a failed attempt");
        let _key_guard = ImportedKeyGuard {
            identity: &identity,
            scope,
            armed: true,
        };
    }

    /// Operations against a reference whose CNG key no longer exists must fail
    /// cleanly, and deleting an already-absent key must be idempotent.
    #[test]
    fn missing_key_operations_fail_cleanly() {
        let (_temp_tree, profile, pfx) = generate_test_pfx();
        let account_id = Uuid::new_v4().to_string();
        let scope = SmimeKeyScope {
            profile_path: &profile,
            account_id: &account_id,
        };
        let identity = WINDOWS_BACKEND
            .import_pkcs12(&pfx, TEST_PFX_PASSWORD, scope)
            .expect("import PFX into user-scoped CNG");

        WINDOWS_BACKEND
            .delete_private_key(&identity.cert_pem, &identity.key_pem, scope)
            .expect("delete persisted CNG key");

        // Deleting an already-absent key is idempotent, not an error.
        WINDOWS_BACKEND
            .delete_private_key(&identity.cert_pem, &identity.key_pem, scope)
            .expect("deleting an absent CNG key must be idempotent");

        assert!(
            WINDOWS_BACKEND
                .sign(
                    &identity.cert_pem,
                    &identity.key_pem,
                    "missing signing key",
                    scope,
                )
                .is_err(),
            "signing must fail once the CNG key is gone"
        );

        let recipient_certs = [identity.cert_pem.clone()];
        let encrypted = WINDOWS_BACKEND
            .encrypt(&recipient_certs, None, "missing decryption key")
            .expect("encrypting only needs the certificate, not the private key");
        assert!(
            WINDOWS_BACKEND
                .decrypt(&identity.cert_pem, &identity.key_pem, &encrypted, scope)
                .is_err(),
            "decryption must fail once the CNG key is gone"
        );

        let reference = decode_reference(&identity.key_pem, scope).expect("decode key reference");
        assert!(
            named_key_is_absent(&reference.provider, &reference.key_name)
                .expect("probe deleted key"),
            "the CNG key must be absent after deletion"
        );
    }

    /// A key reference from a different profile or account must be rejected by
    /// provenance validation rather than silently resolving to another scope's key.
    #[test]
    fn key_reference_from_foreign_scope_is_rejected() {
        let (_temp_tree, profile, pfx) = generate_test_pfx();
        let account_id = Uuid::new_v4().to_string();
        let scope = SmimeKeyScope {
            profile_path: &profile,
            account_id: &account_id,
        };
        let identity = WINDOWS_BACKEND
            .import_pkcs12(&pfx, TEST_PFX_PASSWORD, scope)
            .expect("import PFX into user-scoped CNG");
        let _key_guard = ImportedKeyGuard {
            identity: &identity,
            scope,
            armed: true,
        };

        let foreign_account = Uuid::new_v4().to_string();
        let foreign_account_scope = SmimeKeyScope {
            profile_path: &profile,
            account_id: &foreign_account,
        };
        assert!(
            decode_reference(&identity.key_pem, foreign_account_scope).is_err(),
            "a reference must not validate under a different account"
        );
        assert!(
            WINDOWS_BACKEND
                .sign(
                    &identity.cert_pem,
                    &identity.key_pem,
                    "foreign account",
                    foreign_account_scope,
                )
                .is_err(),
            "signing must fail under a different account scope"
        );

        let foreign_profile = profile.join("other-profile");
        fs::create_dir_all(&foreign_profile).expect("create foreign profile");
        let foreign_profile_scope = SmimeKeyScope {
            profile_path: &foreign_profile,
            account_id: &account_id,
        };
        assert!(
            decode_reference(&identity.key_pem, foreign_profile_scope).is_err(),
            "a reference must not validate under a different profile"
        );
    }

    #[test]
    fn certificate_trusted_rejects_null_context() {
        assert!(!certificate_trusted(null()));
    }
}
