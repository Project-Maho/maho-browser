#[cfg(any(windows, test))]
use base64::Engine as _;

#[cfg(any(windows, test))]
use super::internal_err;
use super::{MailFfiError, SmimeVerifyResult};

#[derive(Debug)]
pub(crate) struct ImportedIdentity {
    pub cert_pem: String,
    pub key_pem: String,
    pub email: String,
    pub subject: String,
    pub issuer: String,
    pub serial_number: String,
    pub fingerprint: String,
    pub not_before: String,
    pub not_after: String,
}

#[derive(Debug, Clone, Copy)]
pub(crate) struct SmimeKeyScope<'a> {
    pub profile_path: &'a std::path::Path,
    pub account_id: &'a str,
}

pub(crate) trait SmimeBackend: Sync {
    fn import_pkcs12(
        &self,
        p12_der: &[u8],
        password: &str,
        scope: SmimeKeyScope<'_>,
    ) -> Result<ImportedIdentity, MailFfiError>;
    fn sign(
        &self,
        cert_pem: &str,
        key_pem: &str,
        body: &str,
        scope: SmimeKeyScope<'_>,
    ) -> Result<String, MailFfiError>;
    fn encrypt(
        &self,
        recipient_certs_pem: &[String],
        sender_cert_pem: Option<&str>,
        body: &str,
    ) -> Result<String, MailFfiError>;
    fn decrypt(
        &self,
        cert_pem: &str,
        key_pem: &str,
        encrypted_body: &str,
        scope: SmimeKeyScope<'_>,
    ) -> Result<String, MailFfiError>;
    fn delete_private_key(
        &self,
        _cert_pem: &str,
        _key_pem: &str,
        _scope: SmimeKeyScope<'_>,
    ) -> Result<(), MailFfiError> {
        Ok(())
    }
    fn certificate_fingerprint(&self, cert_pem: &str) -> Result<String, MailFfiError>;
    fn verify(&self, signed_body: &str) -> SmimeVerifyResult;
}

#[cfg(not(windows))]
mod openssl;
#[cfg(windows)]
mod windows;
#[cfg(all(test, not(windows)))]
mod recipient_review_tests;
pub(super) mod recipient;

#[cfg(not(windows))]
pub(crate) fn backend() -> &'static dyn SmimeBackend {
    &openssl::OPENSSL_BACKEND
}

#[cfg(windows)]
pub(crate) fn backend() -> &'static dyn SmimeBackend {
    &windows::WINDOWS_BACKEND
}

#[cfg(any(windows, test))]
pub(super) fn pem_encode(label: &str, der: &[u8]) -> String {
    let encoded = base64::prelude::BASE64_STANDARD.encode(der);
    let mut output = format!("-----BEGIN {label}-----\n");
    for chunk in encoded.as_bytes().chunks(64) {
        output.push_str(std::str::from_utf8(chunk).expect("base64 is ASCII"));
        output.push('\n');
    }
    output.push_str(&format!("-----END {label}-----\n"));
    output
}

#[cfg(any(windows, test))]
pub(super) fn pem_decode(label: &str, pem: &str) -> Result<Vec<u8>, MailFfiError> {
    let begin = format!("-----BEGIN {label}-----");
    let end = format!("-----END {label}-----");
    let encoded = pem
        .split_once(&begin)
        .and_then(|(_, tail)| tail.split_once(&end).map(|(body, _)| body))
        .ok_or_else(|| internal_err(format!("Failed to parse {label} PEM")))?;
    let compact: String = encoded
        .chars()
        .filter(|c| !c.is_ascii_whitespace())
        .collect();
    base64::prelude::BASE64_STANDARD
        .decode(compact)
        .map_err(|e| internal_err(format!("Failed to decode {label} PEM: {e}")))
}

#[cfg(any(windows, test))]
pub(super) fn smime_opaque(content_type: &str, der: &[u8]) -> String {
    let encoded = base64::prelude::BASE64_STANDARD.encode(der);
    let mut output = format!(
        "MIME-Version: 1.0\r\nContent-Type: application/pkcs7-mime; smime-type={content_type}; name=smime.p7m\r\nContent-Transfer-Encoding: base64\r\nContent-Disposition: attachment; filename=smime.p7m\r\n\r\n"
    );
    for chunk in encoded.as_bytes().chunks(64) {
        output.push_str(std::str::from_utf8(chunk).expect("base64 is ASCII"));
        output.push_str("\r\n");
    }
    output
}

#[cfg(any(windows, test))]
pub(super) fn decode_cms(input: &str) -> Result<Vec<u8>, MailFfiError> {
    for label in ["PKCS7", "CMS"] {
        if input.contains(&format!("-----BEGIN {label}-----")) {
            return pem_decode(label, input);
        }
    }
    if let Some((headers, body)) = input
        .split_once("\r\n\r\n")
        .or_else(|| input.split_once("\n\n"))
    {
        if headers.to_ascii_lowercase().contains("application/pkcs7") {
            let compact: String = body.chars().filter(|c| !c.is_ascii_whitespace()).collect();
            return base64::prelude::BASE64_STANDARD
                .decode(compact)
                .map_err(|e| internal_err(format!("Failed to decode S/MIME data: {e}")));
        }
    }
    base64::prelude::BASE64_STANDARD
        .decode(
            input
                .chars()
                .filter(|c| !c.is_ascii_whitespace())
                .collect::<String>(),
        )
        .map_err(|e| internal_err(format!("Failed to parse S/MIME data: {e}")))
}

#[cfg(any(windows, test))]
pub(super) fn multipart_signed(body: &str, signature: &[u8]) -> String {
    let boundary = "----maho-smime-boundary-7f6f3b1b";
    let encoded = base64::prelude::BASE64_STANDARD.encode(signature);
    let mut output = format!(
        "MIME-Version: 1.0\r\nContent-Type: multipart/signed; protocol=\"application/pkcs7-signature\"; micalg=sha-256; boundary=\"{boundary}\"\r\n\r\n--{boundary}\r\n{body}\r\n--{boundary}\r\nContent-Type: application/pkcs7-signature; name=smime.p7s\r\nContent-Transfer-Encoding: base64\r\nContent-Disposition: attachment; filename=smime.p7s\r\n\r\n"
    );
    for chunk in encoded.as_bytes().chunks(64) {
        output.push_str(std::str::from_utf8(chunk).expect("base64 is ASCII"));
        output.push_str("\r\n");
    }
    output.push_str(&format!("--{boundary}--\r\n"));
    output
}

#[cfg(any(windows, test))]
pub(super) fn detached_parts(input: &str) -> Option<(Vec<u8>, Vec<u8>)> {
    let boundary = input.lines().find_map(|line| {
        line.split("boundary=").nth(1).map(|value| {
            value
                .trim()
                .trim_matches('"')
                .trim_end_matches('\r')
                .to_string()
        })
    })?;
    let marker = format!("--{boundary}");
    let mut parts = input.split(&marker);
    parts.next()?;

    // The serializer adds one framing CRLF before and after the signed body.
    // Remove exactly those two delimiters; trimming all newlines changes the
    // bytes CryptVerifyDetachedMessageSignature hashes.
    let body_part = parts.next()?.strip_prefix("\r\n")?;
    let body = body_part.strip_suffix("\r\n")?.as_bytes().to_vec();

    let signature_part = parts.next()?.strip_prefix("\r\n")?;
    let encoded = signature_part
        .split_once("\r\n\r\n")?
        .1
        .lines()
        .take_while(|line| !line.starts_with("--"))
        .collect::<String>();
    let signature = base64::prelude::BASE64_STANDARD.decode(encoded).ok()?;
    Some((body, signature))
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::path::Path;

    const FIXED_PKCS12: &[u8] =
        include_bytes!("../../../../../../chromium/src/net/data/ssl/certificates/client_1.p12");
    const FIXED_PKCS12_WITHOUT_KEY: &[u8] =
        include_bytes!("../../../../../../chromium/src/net/data/ssl/certificates/client-nokey.p12");
    #[cfg(not(windows))]
    const FIXED_EXPIRED_CERT_AND_KEY: &[u8] =
        include_bytes!("../../../../../../chromium/src/net/data/ssl/certificates/expired_cert.pem");
    const FIXED_PASSWORD: &str = "chrome";
    const FIXED_BODY: &str = "Subject: Maho S/MIME golden vector\r\n\r\nfixed body\r\n";

    fn fixed_scope() -> SmimeKeyScope<'static> {
        SmimeKeyScope {
            profile_path: Path::new("."),
            account_id: "golden@example.com",
        }
    }

    #[test]
    fn shared_cms_framing_round_trips_fixed_bytes() {
        let der = b"fixed-cms-golden-vector";
        let smime = smime_opaque("enveloped-data", der);
        assert_eq!(decode_cms(&smime).unwrap(), der);
        assert_eq!(pem_decode("PKCS7", &pem_encode("PKCS7", der)).unwrap(), der);
    }

    #[test]
    fn windows_detached_parser_preserves_exact_signed_body_bytes() {
        let signature = b"fixed-detached-signature";
        for body in [
            "body without trailing newline",
            "body with one trailing CRLF\r\n",
            "body with two trailing CRLFs\r\n\r\n",
            "body with trailing spaces   \r\n",
            "body containing LF only\n",
        ] {
            let serialized = multipart_signed(body, signature);
            let (parsed_body, parsed_signature) =
                detached_parts(&serialized).expect("generated multipart parses");
            assert_eq!(
                parsed_body,
                body.as_bytes(),
                "body bytes changed for {body:?}"
            );
            assert_eq!(parsed_signature, signature);
        }
    }

    #[cfg(not(windows))]
    #[test]
    fn windows_import_cleanup_contract_covers_early_persistent_key_failures() {
        let source = include_str!("windows.rs");
        let import = source
            .split_once("    fn import_pkcs12(")
            .expect("Windows import implementation exists")
            .1
            .split_once("    fn sign(")
            .expect("Windows import implementation has a bounded body")
            .0;

        let preflight = import
            .find("pkcs12_private_key_certificates(p12_der, &password)")
            .expect("import preflights the certificate owning the private key");
        let persisted_import = import
            .find("PKCS12_ALWAYS_CNG_KSP | PKCS12_IMPORT_SILENT")
            .expect("import persists the CNG key");
        let certificate_match = import
            .find("current_certificate != *private_key_certificate")
            .expect("persisted enumeration selects the preflight key certificate");
        let acquisition = import
            .find("CryptAcquireCertificatePrivateKey(")
            .expect("selected certificate acquisition is attempted");
        assert!(preflight < persisted_import);
        assert!(persisted_import < certificate_match);
        assert!(certificate_match < acquisition);

        let acquisition_failure = import
            .find("windows_error(\"CryptAcquireCertificatePrivateKey(P12/PFX)\")")
            .expect("acquisition failure is captured");
        let provider_lookup = import
            .find("match read_key_provider_info(current, CERT_NCRYPT_KEY_SPEC)")
            .expect("failed acquisition checks for an identifiable persisted key");
        let cleanup = provider_lookup
            + import[provider_lookup..]
                .find("delete_named_key(&info.provider, &info.key_name)")
                .expect("an identifiable persisted key is deleted");
        assert!(acquisition < acquisition_failure);
        assert!(acquisition_failure < provider_lookup);
        assert!(provider_lookup < cleanup);
        assert!(import.contains("CNG key cleanup also failed: {cleanup_error}"));
        assert!(import.contains("target_acquisition_error.unwrap_or_else"));

        let delete = source
            .split_once("fn delete_named_key(")
            .expect("named-key cleanup exists")
            .1
            .split_once("fn named_key_is_absent(")
            .expect("named-key cleanup has a bounded body")
            .0;
        assert!(delete.contains("named_key_is_absent(provider_name, key_name)? => return Ok(())"));
    }

    #[cfg(not(windows))]
    #[test]
    fn windows_decrypt_transfers_key_ownership_to_the_releasing_certificate_store() {
        let source = include_str!("windows.rs");
        let decrypt = source
            .split_once("    fn decrypt(")
            .expect("Windows decrypt implementation exists")
            .1
            .split_once("    fn delete_private_key(")
            .expect("Windows decrypt implementation has a bounded body")
            .0;

        assert!(decrypt.contains("CERT_STORE_CREATE_NEW_FLAG"));
        assert!(
            !decrypt.contains("CERT_STORE_NO_CRYPT_RELEASE_FLAG"),
            "the store must release the CNG handle transferred to its certificate",
        );

        let bind = decrypt
            .find("bind_key(store_cert.0, key.handle)?")
            .expect("decrypt binds the owned CNG key to the store certificate");
        let disarm = decrypt
            .find("key.disarm()")
            .expect("decrypt disarms Rust ownership after the successful bind");
        let decrypt_call = decrypt
            .find("CryptDecryptMessage(")
            .expect("decrypt consumes the bound certificate");
        assert!(
            bind < disarm,
            "ownership transfers only after bind succeeds"
        );
        assert!(
            disarm < decrypt_call,
            "the certificate is the sole key-handle owner during decryption",
        );
    }

    #[test]
    fn shared_backend_golden_vector_import_sign_and_verify() {
        let identity = backend()
            .import_pkcs12(FIXED_PKCS12, FIXED_PASSWORD, fixed_scope())
            .expect("fixed PKCS#12 imports");
        assert!(!identity.cert_pem.is_empty());
        assert!(!identity.key_pem.is_empty());
        assert_eq!(identity.fingerprint.len(), 95);

        let signed = backend()
            .sign(
                &identity.cert_pem,
                &identity.key_pem,
                FIXED_BODY,
                fixed_scope(),
            )
            .expect("fixed body signs");
        let verified = backend().verify(&signed);
        assert!(verified.valid, "fixed signature must verify: {verified:?}");
    }

    #[cfg(not(windows))]
    #[test]
    fn expired_certificate_signature_is_valid_but_not_trusted() {
        use std::cmp::Ordering;

        use ::openssl::asn1::Asn1Time;
        use ::openssl::pkey::PKey;
        use ::openssl::x509::X509;

        let cert =
            X509::from_pem(FIXED_EXPIRED_CERT_AND_KEY).expect("fixed expired certificate parses");
        let key = PKey::private_key_from_pem(FIXED_EXPIRED_CERT_AND_KEY)
            .expect("fixed expired private key parses");
        let reference_time = Asn1Time::from_str_x509("20260101000000Z").expect("fixed time parses");
        assert_eq!(
            cert.not_after()
                .compare(&reference_time)
                .expect("certificate validity compares to the fixed time"),
            Ordering::Less,
            "test certificate must be expired before the fixed reference time",
        );

        let cert_pem = String::from_utf8(cert.to_pem().expect("certificate encodes as PEM"))
            .expect("certificate PEM is UTF-8");
        let key_pem = String::from_utf8(
            key.private_key_to_pem_pkcs8()
                .expect("private key encodes as PEM"),
        )
        .expect("private key PEM is UTF-8");
        let signed = backend()
            .sign(&cert_pem, &key_pem, FIXED_BODY, fixed_scope())
            .expect("fixed body signs with the expired certificate");

        let verified = backend().verify(&signed);
        assert!(verified.valid);
        assert!(!verified.trusted);
    }

    #[test]
    fn shared_backend_golden_vector_encrypts_and_decrypts() {
        let identity = backend()
            .import_pkcs12(FIXED_PKCS12, FIXED_PASSWORD, fixed_scope())
            .expect("fixed PKCS#12 imports");
        let encrypted = backend()
            .encrypt(&[identity.cert_pem.clone()], None, FIXED_BODY)
            .expect("fixed body encrypts");
        let decrypted = backend()
            .decrypt(
                &identity.cert_pem,
                &identity.key_pem,
                &encrypted,
                fixed_scope(),
            )
            .expect("fixed body decrypts");
        assert_eq!(decrypted, FIXED_BODY);
    }

    #[test]
    fn shared_backend_golden_vector_rejects_wrong_password_and_missing_key() {
        assert!(backend()
            .import_pkcs12(FIXED_PKCS12, "wrong-password", fixed_scope())
            .is_err());
        assert!(backend()
            .import_pkcs12(FIXED_PKCS12_WITHOUT_KEY, FIXED_PASSWORD, fixed_scope())
            .is_err());
    }

    #[test]
    fn shared_backend_golden_vector_rejects_malformed_cms() {
        let identity = backend()
            .import_pkcs12(FIXED_PKCS12, FIXED_PASSWORD, fixed_scope())
            .expect("fixed PKCS#12 imports");
        assert!(backend()
            .decrypt(
                &identity.cert_pem,
                &identity.key_pem,
                "not cms",
                fixed_scope()
            )
            .is_err());
        assert!(!backend().verify("not cms").valid);
    }

    #[cfg(windows)]
    #[test]
    fn windows_import_persists_restart_safe_non_exportable_keys_and_cleans_them_up() {
        struct CleanupOnDrop {
            cert_pem: String,
            key_pem: String,
        }

        impl Drop for CleanupOnDrop {
            fn drop(&mut self) {
                let _ = backend().delete_private_key(&self.cert_pem, &self.key_pem, fixed_scope());
            }
        }

        let missing_profile =
            std::env::temp_dir().join(format!("maho-smime-missing-{}", uuid::Uuid::new_v4(),));
        let missing_scope = SmimeKeyScope {
            profile_path: &missing_profile,
            account_id: "golden@example.com",
        };
        assert!(backend()
            .import_pkcs12(FIXED_PKCS12, FIXED_PASSWORD, missing_scope)
            .is_err());
        let empty_account_scope = SmimeKeyScope {
            profile_path: Path::new("."),
            account_id: "",
        };
        assert!(backend()
            .import_pkcs12(FIXED_PKCS12, FIXED_PASSWORD, empty_account_scope)
            .is_err());

        let first = backend()
            .import_pkcs12(FIXED_PKCS12, FIXED_PASSWORD, fixed_scope())
            .expect("first fixed PKCS#12 import persists its CNG key");
        let first_cleanup = CleanupOnDrop {
            cert_pem: first.cert_pem.clone(),
            key_pem: first.key_pem.clone(),
        };
        assert!(first.key_pem.starts_with("maho-cng-v1:"));
        assert!(!first.key_pem.contains("PRIVATE KEY"));

        let second = backend()
            .import_pkcs12(FIXED_PKCS12, FIXED_PASSWORD, fixed_scope())
            .expect("re-import handles a persisted-key name collision");
        let second_cleanup = CleanupOnDrop {
            cert_pem: second.cert_pem.clone(),
            key_pem: second.key_pem.clone(),
        };
        assert!(second.key_pem.starts_with("maho-cng-v1:"));
        assert_ne!(
            first.key_pem, second.key_pem,
            "separate identities must not share cleanup ownership"
        );
        let wrong_profile = SmimeKeyScope {
            profile_path: Path::new(".."),
            account_id: "golden@example.com",
        };
        assert!(backend()
            .sign(&first.cert_pem, &first.key_pem, FIXED_BODY, wrong_profile)
            .is_err());
        let wrong_account = SmimeKeyScope {
            profile_path: Path::new("."),
            account_id: "GOLDEN@example.com",
        };
        assert!(backend()
            .sign(&first.cert_pem, &first.key_pem, FIXED_BODY, wrong_account)
            .is_err());
        let mut tampered_reference = first.key_pem.clone();
        tampered_reference.push('A');
        assert!(backend()
            .sign(
                &first.cert_pem,
                &tampered_reference,
                FIXED_BODY,
                fixed_scope(),
            )
            .is_err());

        // import_pkcs12 has already closed its certificate store and key
        // handles. These calls therefore exercise reopening the persisted key,
        // matching helper-process restart behavior rather than handle reuse.
        let signed = backend()
            .sign(&first.cert_pem, &first.key_pem, FIXED_BODY, fixed_scope())
            .expect("persisted key signs after importer handles are closed");
        let verified = backend().verify(&signed);
        assert!(
            verified.valid,
            "persisted-key signature must verify: {verified:?}"
        );

        let encrypted = backend()
            .encrypt(&[first.cert_pem.clone()], None, FIXED_BODY)
            .expect("fixed body encrypts to the persisted identity");
        let decrypted = backend()
            .decrypt(&first.cert_pem, &first.key_pem, &encrypted, fixed_scope())
            .expect("persisted key decrypts after importer handles are closed");
        assert_eq!(decrypted, FIXED_BODY);

        backend()
            .delete_private_key(&first.cert_pem, &first.key_pem, fixed_scope())
            .expect("identity cleanup deletes the first CNG container");
        backend()
            .delete_private_key(&first.cert_pem, &first.key_pem, fixed_scope())
            .expect("deleting an already-missing CNG container is idempotent");
        std::mem::forget(first_cleanup);
        assert!(backend()
            .sign(&first.cert_pem, &first.key_pem, FIXED_BODY, fixed_scope())
            .is_err());

        backend()
            .sign(&second.cert_pem, &second.key_pem, FIXED_BODY, fixed_scope())
            .expect("cleaning one re-import leaves the other identity usable");
        backend()
            .delete_private_key(&second.cert_pem, &second.key_pem, fixed_scope())
            .expect("account cleanup deletes the remaining CNG container");
        std::mem::forget(second_cleanup);
        assert!(backend()
            .sign(&second.cert_pem, &second.key_pem, FIXED_BODY, fixed_scope())
            .is_err());
    }

    #[cfg(windows)]
    #[test]
    fn windows_repeated_decrypt_releases_handles_and_preserves_key_deletion() {
        struct CleanupOnDrop {
            cert_pem: String,
            key_pem: String,
        }

        impl Drop for CleanupOnDrop {
            fn drop(&mut self) {
                let _ = backend().delete_private_key(&self.cert_pem, &self.key_pem, fixed_scope());
            }
        }

        let identity = backend()
            .import_pkcs12(FIXED_PKCS12, FIXED_PASSWORD, fixed_scope())
            .expect("fixed PKCS#12 import persists its CNG key");
        let cleanup = CleanupOnDrop {
            cert_pem: identity.cert_pem.clone(),
            key_pem: identity.key_pem.clone(),
        };
        let encrypted = backend()
            .encrypt(&[identity.cert_pem.clone()], None, FIXED_BODY)
            .expect("fixed body encrypts to the persisted identity");

        for iteration in 0..256 {
            let decrypted = backend()
                .decrypt(
                    &identity.cert_pem,
                    &identity.key_pem,
                    &encrypted,
                    fixed_scope(),
                )
                .unwrap_or_else(|error| panic!("decrypt iteration {iteration} failed: {error}"));
            assert_eq!(decrypted, FIXED_BODY);
        }

        backend()
            .delete_private_key(&identity.cert_pem, &identity.key_pem, fixed_scope())
            .expect("the persisted key remains deletable after repeated decrypts");
        std::mem::forget(cleanup);
        assert!(backend()
            .decrypt(
                &identity.cert_pem,
                &identity.key_pem,
                &encrypted,
                fixed_scope(),
            )
            .is_err());
        backend()
            .delete_private_key(&identity.cert_pem, &identity.key_pem, fixed_scope())
            .expect("persisted-key deletion remains idempotent");
    }
}
