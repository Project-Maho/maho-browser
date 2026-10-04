use openssl::hash::MessageDigest;
use openssl::pkcs12::Pkcs12;
use openssl::pkcs7::{Pkcs7, Pkcs7Flags};
use openssl::pkey::PKey;
use openssl::stack::Stack;
use openssl::symm::Cipher;
use openssl::x509::store::X509StoreBuilder;
use openssl::x509::{X509NameRef, X509Ref, X509};

use super::{ImportedIdentity, SmimeBackend, SmimeKeyScope};
use crate::error::MailFfiError;
use crate::ffi::crypto_api::{internal_err, validation_err, SmimeVerifyResult};

pub(super) static OPENSSL_BACKEND: OpenSslBackend = OpenSslBackend;

pub(super) struct OpenSslBackend;

fn x509_name_to_string(name: &X509NameRef) -> String {
    name.entries()
        .map(|entry| {
            let key = entry.object().nid().short_name().unwrap_or("??");
            let value = entry
                .data()
                .as_utf8()
                .map(|value| value.to_string())
                .unwrap_or_default();
            format!("{key}={value}")
        })
        .collect::<Vec<_>>()
        .join(", ")
}

fn extract_email(cert: &X509Ref) -> String {
    if let Some(email) = cert
        .subject_alt_names()
        .into_iter()
        .flatten()
        .find_map(|name| name.email().map(str::to_owned))
    {
        return email;
    }
    for nid in [
        openssl::nid::Nid::PKCS9_EMAILADDRESS,
        openssl::nid::Nid::COMMONNAME,
    ] {
        if let Some(value) = cert
            .subject_name()
            .entries_by_nid(nid)
            .next()
            .and_then(|entry| entry.data().as_utf8().ok())
            .map(|value| value.to_string())
        {
            if nid != openssl::nid::Nid::COMMONNAME || value.contains('@') {
                return value;
            }
        }
    }
    String::new()
}

fn fingerprint(cert: &X509Ref) -> Result<String, MailFfiError> {
    cert.digest(MessageDigest::sha256())
        .map(|bytes| {
            bytes
                .iter()
                .map(|b| format!("{b:02X}"))
                .collect::<Vec<_>>()
                .join(":")
        })
        .map_err(|e| internal_err(format!("Failed to compute certificate fingerprint: {e}")))
}

fn parse_pkcs7(input: &str) -> Result<(Pkcs7, Option<Vec<u8>>), openssl::error::ErrorStack> {
    Pkcs7::from_smime(input.as_bytes())
        .or_else(|_| Pkcs7::from_pem(input.as_bytes()).map(|pkcs7| (pkcs7, None)))
        .or_else(|_| Pkcs7::from_der(input.as_bytes()).map(|pkcs7| (pkcs7, None)))
}

impl SmimeBackend for OpenSslBackend {
    fn import_pkcs12(
        &self,
        p12_der: &[u8],
        password: &str,
        _scope: SmimeKeyScope<'_>,
    ) -> Result<ImportedIdentity, MailFfiError> {
        let parsed = Pkcs12::from_der(p12_der)
            .map_err(|e| validation_err(format!("Failed to parse P12/PFX file: {e}")))?
            .parse2(password)
            .map_err(|e| validation_err(format!("Failed to decrypt P12/PFX: {e}")))?;
        let cert = parsed
            .cert
            .ok_or_else(|| validation_err("P12/PFX file does not contain a certificate"))?;
        let key = parsed
            .pkey
            .ok_or_else(|| validation_err("P12/PFX file does not contain a private key"))?;
        Ok(ImportedIdentity {
            cert_pem: String::from_utf8(
                cert.to_pem()
                    .map_err(|e| internal_err(format!("Failed to encode certificate PEM: {e}")))?,
            )
            .map_err(|e| internal_err(format!("Certificate PEM is not valid UTF-8: {e}")))?,
            key_pem: String::from_utf8(
                key.private_key_to_pem_pkcs8()
                    .map_err(|e| internal_err(format!("Failed to encode private key PEM: {e}")))?,
            )
            .map_err(|e| internal_err(format!("Private key PEM is not valid UTF-8: {e}")))?,
            email: extract_email(&cert),
            subject: x509_name_to_string(cert.subject_name()),
            issuer: x509_name_to_string(cert.issuer_name()),
            serial_number: cert
                .serial_number()
                .to_bn()
                .and_then(|bn| bn.to_hex_str())
                .map(|s| s.to_string())
                .unwrap_or_default(),
            fingerprint: fingerprint(&cert)?,
            not_before: cert.not_before().to_string(),
            not_after: cert.not_after().to_string(),
        })
    }

    fn sign(
        &self,
        cert_pem: &str,
        key_pem: &str,
        body: &str,
        _scope: SmimeKeyScope<'_>,
    ) -> Result<String, MailFfiError> {
        let cert = X509::from_pem(cert_pem.as_bytes())
            .map_err(|e| internal_err(format!("Failed to parse certificate: {e}")))?;
        let key = PKey::private_key_from_pem(key_pem.as_bytes())
            .map_err(|e| internal_err(format!("Failed to parse private key: {e}")))?;
        let certs =
            Stack::new().map_err(|e| internal_err(format!("Failed to create cert stack: {e}")))?;
        let flags = Pkcs7Flags::DETACHED | Pkcs7Flags::STREAM;
        let pkcs7 = Pkcs7::sign(&cert, &key, &certs, body.as_bytes(), flags)
            .map_err(|e| internal_err(format!("S/MIME signing failed: {e}")))?;
        String::from_utf8(
            pkcs7.to_smime(body.as_bytes(), flags).map_err(|e| {
                internal_err(format!("Failed to encode S/MIME signed message: {e}"))
            })?,
        )
        .map_err(|e| internal_err(format!("S/MIME output is not valid UTF-8: {e}")))
    }

    fn encrypt(
        &self,
        recipients: &[String],
        sender: Option<&str>,
        body: &str,
    ) -> Result<String, MailFfiError> {
        let mut certs =
            Stack::new().map_err(|e| internal_err(format!("Failed to create cert stack: {e}")))?;
        let mut fingerprints = Vec::new();
        for pem in recipients {
            let cert = X509::from_pem(pem.as_bytes())
                .map_err(|e| internal_err(format!("Failed to parse recipient certificate: {e}")))?;
            fingerprints.push(fingerprint(&cert)?);
            certs
                .push(cert)
                .map_err(|e| internal_err(format!("Failed to add cert to stack: {e}")))?;
        }
        if let Some(sender) = sender {
            if let Ok(cert) = X509::from_pem(sender.as_bytes()) {
                if fingerprint(&cert)
                    .map(|fp| !fingerprints.contains(&fp))
                    .unwrap_or(false)
                {
                    certs.push(cert).map_err(|e| {
                        internal_err(format!("Failed to add sender certificate: {e}"))
                    })?;
                }
            }
        }
        let flags = Pkcs7Flags::STREAM;
        let pkcs7 = Pkcs7::encrypt(&certs, body.as_bytes(), Cipher::aes_256_cbc(), flags)
            .map_err(|e| internal_err(format!("S/MIME encryption failed: {e}")))?;
        String::from_utf8(
            pkcs7.to_smime(body.as_bytes(), flags).map_err(|e| {
                internal_err(format!("Failed to encode S/MIME encrypted message: {e}"))
            })?,
        )
        .map_err(|e| internal_err(format!("S/MIME output is not valid UTF-8: {e}")))
    }

    fn decrypt(
        &self,
        cert_pem: &str,
        key_pem: &str,
        encrypted: &str,
        _scope: SmimeKeyScope<'_>,
    ) -> Result<String, MailFfiError> {
        let cert = X509::from_pem(cert_pem.as_bytes())
            .map_err(|e| internal_err(format!("Failed to parse certificate: {e}")))?;
        let key = PKey::private_key_from_pem(key_pem.as_bytes())
            .map_err(|e| internal_err(format!("Failed to parse private key: {e}")))?;
        let plaintext = parse_pkcs7(encrypted)
            .map_err(|e| internal_err(format!("Failed to parse S/MIME encrypted data: {e}")))?
            .0
            .decrypt(&key, &cert, Pkcs7Flags::empty())
            .map_err(|e| internal_err(format!("S/MIME decryption failed: {e}")))?;
        String::from_utf8(plaintext)
            .map_err(|e| internal_err(format!("Decrypted data is not valid UTF-8: {e}")))
    }

    fn certificate_fingerprint(&self, cert_pem: &str) -> Result<String, MailFfiError> {
        let cert = X509::from_pem(cert_pem.as_bytes())
            .map_err(|e| internal_err(format!("Failed to parse certificate: {e}")))?;
        fingerprint(&cert)
    }

    fn verify(&self, signed: &str) -> SmimeVerifyResult {
        let (pkcs7, detached_content) = match parse_pkcs7(signed) {
            Ok(value) => value,
            Err(error) => {
                return SmimeVerifyResult {
                    valid: false,
                    trusted: false,
                    signer_email: None,
                    signer_subject: Some(format!("Failed to parse S/MIME data: {error}")),
                }
            }
        };
        let store = match X509StoreBuilder::new().and_then(|mut builder| {
            builder.set_default_paths()?;
            Ok(builder.build())
        }) {
            Ok(value) => value,
            Err(error) => {
                return SmimeVerifyResult {
                    valid: false,
                    trusted: false,
                    signer_email: None,
                    signer_subject: Some(format!("Failed to build X509 store: {error}")),
                }
            }
        };
        let certs = match Stack::new() {
            Ok(value) => value,
            Err(error) => {
                return SmimeVerifyResult {
                    valid: false,
                    trusted: false,
                    signer_email: None,
                    signer_subject: Some(format!("Failed to create cert stack: {error}")),
                }
            }
        };
        let mut output = Vec::new();
        let trusted = pkcs7
            .verify(
                &certs,
                &store,
                detached_content.as_deref(),
                Some(&mut output),
                Pkcs7Flags::empty(),
            )
            .is_ok();
        let mut signer_email = None;
        let mut signer_subject = None;
        if let Ok(signers) = pkcs7.signers(&certs, Pkcs7Flags::empty()) {
            if let Some(cert) = signers.iter().next() {
                signer_email = Some(extract_email(cert));
                signer_subject = Some(x509_name_to_string(cert.subject_name()));
            }
        }
        let valid = trusted
            || pkcs7
                .verify(
                    &certs,
                    &store,
                    detached_content.as_deref(),
                    Some(&mut output),
                    Pkcs7Flags::NOVERIFY,
                )
                .is_ok();
        if valid && !trusted {
            signer_subject =
                signer_subject.map(|subject| format!("{subject} (certificate not trusted)"));
        }
        SmimeVerifyResult {
            valid,
            trusted,
            signer_email,
            signer_subject,
        }
    }
}
