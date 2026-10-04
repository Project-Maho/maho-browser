// Copyright 2026 Maho Browser. All rights reserved.

use std::ffi::c_void;
use std::os::raw::c_char;

use base64::Engine as _;
use pgp::composed::{
    Any, Deserializable, KeyType, Message, SecretKeyParamsBuilder, SignedPublicKey,
    SignedSecretKey, StandaloneSignature, SubkeyParamsBuilder,
};
use pgp::crypto::ecc_curve::ECCCurve;
use pgp::crypto::{hash::HashAlgorithm, sym::SymmetricKeyAlgorithm};
use pgp::types::{PublicKeyTrait, SecretKeyTrait};
use rusqlite::params;
use serde::{Deserialize, Serialize};
use smallvec::smallvec;
use uuid::Uuid;

use crate::credentials::{get_encrypted_credential, store_encrypted_credential};
use crate::error::MailFfiError;
use crate::ffi::read_api::{accept_read, accept_read_call, blocking_json, MahoMailReadCallback};
use maho_core::error::AppError;

mod smime;

// Local C-string helper implementations to avoid dependency on private ffi.rs helpers
fn c_string(ptr: *const c_char, name: &'static str) -> Result<String, MailFfiError> {
    if ptr.is_null() {
        return Err(MailFfiError::InvalidArg(name));
    }
    let value = unsafe { std::ffi::CStr::from_ptr(ptr) };
    value
        .to_str()
        .map(str::to_owned)
        .map_err(|_| MailFfiError::InvalidArg(name))
}

fn non_empty(ptr: *const c_char, name: &'static str) -> Result<String, MailFfiError> {
    let value = c_string(ptr, name)?;
    if value.trim().is_empty() {
        return Err(MailFfiError::InvalidArg(name));
    }
    Ok(value)
}

// Helpers for error mapping
fn validation_err(msg: impl Into<String>) -> MailFfiError {
    MailFfiError::Core(AppError::Validation(msg.into()))
}
fn internal_err(msg: impl Into<String>) -> MailFfiError {
    MailFfiError::Core(AppError::Internal(msg.into()))
}
fn not_found_err(msg: impl Into<String>) -> MailFfiError {
    MailFfiError::Core(AppError::NotFound(msg.into()))
}

// Structs
#[derive(Debug, Serialize, Deserialize)]
pub struct PgpKeyInfo {
    pub id: String,
    pub account_id: String,
    pub email: String,
    pub key_type: String,
    pub fingerprint: String,
    pub is_private: bool,
    pub is_default: bool,
    pub expires_at: Option<String>,
    pub created_at: String,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct PgpVerifyResult {
    pub is_valid: bool,
    pub signer_email: Option<String>,
    pub fingerprint: Option<String>,
    pub error: Option<String>,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct SmimeIdentity {
    pub id: String,
    pub account_id: String,
    pub email: String,
    pub subject: String,
    pub issuer: String,
    pub serial_number: String,
    pub fingerprint: String,
    pub not_before: String,
    pub not_after: String,
    pub is_default: bool,
    pub created_at: String,
}

#[derive(Debug, Serialize, Deserialize)]
pub(crate) struct SmimeVerifyResult {
    pub valid: bool,
    pub trusted: bool,
    pub signer_email: Option<String>,
    pub signer_subject: Option<String>,
}

// Private key credential store helpers
fn store_private_key(
    conn: &rusqlite::Connection,
    credential_key: &[u8; 32],
    key_id: &str,
    armored_key: &str,
    is_pgp: bool,
) -> Result<(), MailFfiError> {
    let credential_type = if is_pgp {
        "pgp_private_key"
    } else {
        "smime_private_key"
    };
    store_encrypted_credential(conn, credential_key, key_id, credential_type, armored_key)
}

fn get_private_key(
    conn: &rusqlite::Connection,
    credential_key: &[u8; 32],
    key_id: &str,
    is_pgp: bool,
) -> Result<Option<String>, MailFfiError> {
    let credential_type = if is_pgp {
        "pgp_private_key"
    } else {
        "smime_private_key"
    };
    get_encrypted_credential(conn, credential_key, key_id, credential_type)
}

fn delete_private_key(
    conn: &rusqlite::Connection,
    key_id: &str,
    is_pgp: bool,
) -> Result<(), MailFfiError> {
    let credential_type = if is_pgp {
        "pgp_private_key"
    } else {
        "smime_private_key"
    };
    conn.execute(
        "DELETE FROM encrypted_credentials WHERE account_id = ?1 AND credential_type = ?2",
        rusqlite::params![key_id, credential_type],
    )?;
    Ok(())
}

fn smime_profile_path(db_path: &std::path::Path) -> Result<&std::path::Path, MailFfiError> {
    if db_path.file_name() != Some(std::ffi::OsStr::new("maho_mail.db"))
        || db_path.parent().and_then(std::path::Path::file_name)
            != Some(std::ffi::OsStr::new("MahoMail"))
    {
        return Err(MailFfiError::InvalidRequest(
            "Mail database path does not match the profile layout".into(),
        ));
    }
    db_path
        .parent()
        .and_then(std::path::Path::parent)
        .ok_or_else(|| {
            MailFfiError::InvalidRequest(
                "Mail database path does not identify a profile root".into(),
            )
        })
}

fn smime_key_scope<'a>(
    ctx: &'a crate::state::AppCtx,
    account_id: &'a str,
) -> Result<smime::SmimeKeyScope<'a>, MailFfiError> {
    let profile_path = smime_profile_path(&ctx.db_path)?;
    Ok(smime::SmimeKeyScope {
        profile_path,
        account_id,
    })
}

pub(crate) fn cleanup_smime_keys_for_account(
    ctx: &crate::state::AppCtx,
    conn: &rusqlite::Connection,
    account_id: &str,
) -> Result<Vec<String>, MailFfiError> {
    let mut stmt =
        conn.prepare("SELECT id, cert_pem FROM smime_identities WHERE account_id = ?1")?;
    let identities = stmt
        .query_map([account_id], |row| {
            Ok((row.get::<_, String>(0)?, row.get::<_, String>(1)?))
        })?
        .collect::<Result<Vec<_>, _>>()?;
    if identities.is_empty() {
        return Ok(Vec::new());
    }
    let scope = smime_key_scope(ctx, account_id)?;

    for (identity_id, cert_pem) in &identities {
        if let Some(key_pem) = get_private_key(conn, &ctx.credential_key, identity_id, false)? {
            smime::backend().delete_private_key(cert_pem, &key_pem, scope)?;
        }
    }

    Ok(identities.into_iter().map(|(id, _)| id).collect())
}

fn format_fingerprint(fp: &[u8]) -> String {
    fp.iter()
        .map(|b| format!("{:02X}", b))
        .collect::<Vec<_>>()
        .join("")
}

fn resolve_key_type(key_type: &str) -> Result<KeyType, MailFfiError> {
    match key_type {
        "rsa2048" => Ok(KeyType::Rsa(2048)),
        "rsa4096" => Ok(KeyType::Rsa(4096)),
        "ed25519" => Ok(KeyType::EdDSALegacy),
        _ => Err(validation_err(format!(
            "Unsupported key type: {}. Use rsa2048, rsa4096, or ed25519",
            key_type
        ))),
    }
}

fn resolve_subkey_type(key_type: &str) -> Result<KeyType, MailFfiError> {
    match key_type {
        "rsa2048" => Ok(KeyType::Rsa(2048)),
        "rsa4096" => Ok(KeyType::Rsa(4096)),
        "ed25519" => Ok(KeyType::ECDH(ECCCurve::Curve25519)),
        _ => Err(validation_err(format!(
            "Unsupported key type: {}",
            key_type
        ))),
    }
}

fn map_pgp_key(row: &rusqlite::Row) -> rusqlite::Result<PgpKeyInfo> {
    Ok(PgpKeyInfo {
        id: row.get("id")?,
        account_id: row.get("account_id")?,
        email: row.get("email")?,
        key_type: row.get("key_type")?,
        fingerprint: row.get("fingerprint")?,
        is_private: row.get("is_private")?,
        is_default: row.get("is_default")?,
        expires_at: row.get("expires_at")?,
        created_at: row.get("created_at")?,
    })
}

fn detect_key_type_from_public(pk: &SignedPublicKey) -> String {
    use pgp::crypto::public_key::PublicKeyAlgorithm;
    use pgp::types::PublicParams;
    match pk.primary_key.algorithm() {
        PublicKeyAlgorithm::RSA | PublicKeyAlgorithm::RSAEncrypt | PublicKeyAlgorithm::RSASign => {
            match pk.public_params() {
                PublicParams::RSA { n, .. } => {
                    let bit_size = n.as_bytes().len() * 8;
                    match bit_size {
                        0..=2200 => "rsa2048".to_string(),
                        2201..=4200 => "rsa4096".to_string(),
                        _ => format!("rsa{}", bit_size),
                    }
                }
                _ => "rsa".to_string(),
            }
        }
        PublicKeyAlgorithm::EdDSALegacy | PublicKeyAlgorithm::Ed25519 => "ed25519".to_string(),
        _ => "unknown".to_string(),
    }
}

fn extract_email_from_public(pk: &SignedPublicKey) -> String {
    for uid in &pk.details.users {
        let id_bytes = uid.id.id();
        let id_str = match std::str::from_utf8(id_bytes.as_ref()) {
            Ok(s) => s,
            Err(_) => continue,
        };
        if let Some(start) = id_str.find('<') {
            if let Some(end) = id_str.find('>') {
                return id_str[start + 1..end].to_string();
            }
        }
        if id_str.contains('@') {
            return id_str.to_string();
        }
    }
    String::new()
}

fn get_default_private_key(
    conn: &rusqlite::Connection,
    credential_key: &[u8; 32],
    account_id: &str,
) -> Result<(String, SignedSecretKey), MailFfiError> {
    let key_id: String = conn
        .query_row(
            "SELECT id FROM pgp_keys WHERE account_id = ?1 AND is_private = 1 AND is_default = 1 LIMIT 1",
            rusqlite::params![account_id],
            |row| row.get(0),
        )
        .or_else(|_| {
            conn.query_row(
                "SELECT id FROM pgp_keys WHERE account_id = ?1 AND is_private = 1 LIMIT 1",
                rusqlite::params![account_id],
                |row| row.get(0),
            )
        })
        .map_err(|_| not_found_err("No private key found for this account"))?;

    let armored = get_private_key(conn, credential_key, &key_id, true)?
        .ok_or_else(|| not_found_err("Private key data not found in credential store"))?;

    let (sk, _) = SignedSecretKey::from_string(&armored)
        .map_err(|e| internal_err(format!("Failed to parse private key: {}", e)))?;

    Ok((key_id, sk))
}

fn get_default_public_key(
    conn: &rusqlite::Connection,
    account_id: &str,
) -> Result<Option<SignedPublicKey>, MailFfiError> {
    let key_data_result: Result<Vec<u8>, rusqlite::Error> = conn
        .query_row(
            "SELECT key_data FROM pgp_keys WHERE account_id = ?1 AND is_private = 0 AND is_default = 1 LIMIT 1",
            rusqlite::params![account_id],
            |row| row.get(0),
        )
        .or_else(|_| {
            conn.query_row(
                "SELECT key_data FROM pgp_keys WHERE account_id = ?1 AND is_private = 0 LIMIT 1",
                rusqlite::params![account_id],
                |row| row.get(0),
            )
        });

    match key_data_result {
        Ok(data) => {
            let armor = String::from_utf8(data)
                .map_err(|e| internal_err(format!("Key data not valid UTF-8: {}", e)))?;
            let (pk, _) = SignedPublicKey::from_string(&armor)
                .map_err(|e| internal_err(format!("Failed to parse public key: {}", e)))?;
            Ok(Some(pk))
        }
        Err(_) => Ok(None),
    }
}

fn get_public_keys_for_emails(
    conn: &rusqlite::Connection,
    emails: &[String],
) -> Result<Vec<SignedPublicKey>, MailFfiError> {
    let mut keys = Vec::new();
    for email in emails {
        let key_data: Vec<u8> = conn
            .query_row(
                "SELECT key_data FROM pgp_keys WHERE email = ?1 AND is_private = 0 LIMIT 1",
                rusqlite::params![email],
                |row| row.get(0),
            )
            .map_err(|_| not_found_err(format!("No public key found for recipient: {}", email)))?;

        let armor = String::from_utf8(key_data)
            .map_err(|e| internal_err(format!("Key data not valid UTF-8: {}", e)))?;

        let (pk, _) = SignedPublicKey::from_string(&armor).map_err(|e| {
            internal_err(format!("Failed to parse public key for {}: {}", email, e))
        })?;

        keys.push(pk);
    }
    Ok(keys)
}

fn encrypt_attachment_bytes(
    plaintext: &[u8],
    public_keys: &[&SignedPublicKey],
) -> Result<Vec<u8>, MailFfiError> {
    let mut rng = rand::thread_rng();
    let lit_msg = Message::new_literal_bytes("attachment.bin", plaintext);

    let encrypted = lit_msg
        .encrypt_to_keys_seipdv1(&mut rng, SymmetricKeyAlgorithm::AES256, public_keys)
        .map_err(|e| internal_err(format!("Failed to encrypt attachment: {}", e)))?;

    let armored = encrypted
        .to_armored_bytes(Default::default())
        .map_err(|e| internal_err(format!("Failed to armor encrypted attachment: {}", e)))?;

    Ok(armored)
}

// ----------------- PGP FFI Functions -----------------

#[derive(Deserialize)]
struct GeneratePgpKeyRequest {
    account_id: String,
    email: String,
    name: String,
    key_type: String,
}

#[no_mangle]
pub extern "C" fn MahoMailGeneratePgpKey(
    request_json: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(raw) = c_string(request_json, "request_json") else {
            return false;
        };
        let Ok(req) = serde_json::from_str::<GeneratePgpKeyRequest>(&raw) else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let primary_kt = resolve_key_type(&req.key_type)?;
                let sub_kt = resolve_subkey_type(&req.key_type)?;
                let user_id = format!("{} <{}>", req.name, req.email);

                let mut rng = rand::thread_rng();

                let mut params_builder = SecretKeyParamsBuilder::default();
                params_builder
                    .key_type(primary_kt)
                    .can_certify(true)
                    .can_sign(true)
                    .primary_user_id(user_id)
                    .passphrase(None)
                    .preferred_symmetric_algorithms(smallvec![
                        SymmetricKeyAlgorithm::AES256,
                        SymmetricKeyAlgorithm::AES192,
                        SymmetricKeyAlgorithm::AES128,
                    ])
                    .preferred_hash_algorithms(smallvec![
                        HashAlgorithm::SHA2_256,
                        HashAlgorithm::SHA2_384,
                        HashAlgorithm::SHA2_512,
                    ])
                    .preferred_compression_algorithms(smallvec![])
                    .subkey(
                        SubkeyParamsBuilder::default()
                            .key_type(sub_kt)
                            .can_encrypt(true)
                            .passphrase(None)
                            .build()
                            .map_err(|e| {
                                internal_err(format!("Failed to build subkey params: {}", e))
                            })?,
                    );

                let secret_key_params = params_builder
                    .build()
                    .map_err(|e| internal_err(format!("Failed to build key params: {}", e)))?;

                let secret_key = secret_key_params
                    .generate(&mut rng)
                    .map_err(|e| internal_err(format!("Failed to generate key: {}", e)))?;

                let signed_secret_key: SignedSecretKey = secret_key
                    .sign(&mut rng, String::new)
                    .map_err(|e| internal_err(format!("Failed to sign key: {}", e)))?;

                let signed_public_key: SignedPublicKey = signed_secret_key
                    .public_key()
                    .sign(&mut rng, &signed_secret_key, String::new)
                    .map_err(|e| internal_err(format!("Failed to sign public key: {}", e)))?;
                let fingerprint = format_fingerprint(signed_public_key.fingerprint().as_bytes());

                let public_armor = signed_public_key
                    .to_armored_string(None.into())
                    .map_err(|e| internal_err(format!("Failed to armor public key: {}", e)))?;

                let private_armor = signed_secret_key
                    .to_armored_string(None.into())
                    .map_err(|e| internal_err(format!("Failed to armor private key: {}", e)))?;

                let pub_id = Uuid::new_v4().to_string();
                let priv_id = Uuid::new_v4().to_string();

                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;

                store_private_key(&conn, &ctx.credential_key, &priv_id, &private_armor, true)?;

                conn.execute(
                    "INSERT INTO pgp_keys (id, account_id, email, key_type, fingerprint, key_data, is_private, is_default)
                     VALUES (?1, ?2, ?3, ?4, ?5, ?6, 0, 0)",
                    params![pub_id, req.account_id, req.email, req.key_type, fingerprint, public_armor.as_bytes()],
                )?;

                let placeholder = b"encrypted:credential_store";
                conn.execute(
                    "INSERT INTO pgp_keys (id, account_id, email, key_type, fingerprint, key_data, is_private, is_default)
                     VALUES (?1, ?2, ?3, ?4, ?5, ?6, 1, 0)",
                    params![priv_id, req.account_id, req.email, req.key_type, fingerprint, placeholder],
                )?;

                let info = conn.query_row(
                    "SELECT id, account_id, email, key_type, fingerprint, is_private, is_default, expires_at, created_at
                     FROM pgp_keys WHERE id = ?1",
                    params![priv_id],
                    map_pgp_key,
                )?;

                Ok(info)
            })
        })
    })
}

#[derive(Deserialize)]
struct ImportPgpKeyRequest {
    account_id: String,
    key_data: String,
}

#[no_mangle]
pub extern "C" fn MahoMailImportPgpKey(
    request_json: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(raw) = c_string(request_json, "request_json") else {
            return false;
        };
        let Ok(req) = serde_json::from_str::<ImportPgpKeyRequest>(&raw) else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let (any, _headers) = Any::from_string(&req.key_data)
                    .map_err(|e| validation_err(format!("Failed to parse key: {}", e)))?;

                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;

                match any {
                    Any::PublicKey(pk) => {
                        let fingerprint = format_fingerprint(pk.fingerprint().as_bytes());
                        let key_type = detect_key_type_from_public(&pk);
                        let email = extract_email_from_public(&pk);
                        let armor = pk
                            .to_armored_string(None.into())
                            .map_err(|e| internal_err(format!("Failed to re-armor key: {}", e)))?;

                        let id = Uuid::new_v4().to_string();
                        conn.execute(
                            "INSERT INTO pgp_keys (id, account_id, email, key_type, fingerprint, key_data, is_private, is_default)
                             VALUES (?1, ?2, ?3, ?4, ?5, ?6, 0, 0)",
                            params![id, req.account_id, email, key_type, fingerprint, armor.as_bytes()],
                        )?;

                        let info = conn.query_row(
                            "SELECT id, account_id, email, key_type, fingerprint, is_private, is_default, expires_at, created_at
                             FROM pgp_keys WHERE id = ?1",
                            params![id],
                            map_pgp_key,
                        )?;
                        Ok(info)
                    }
                    Any::SecretKey(sk) => {
                        let pk: SignedPublicKey = sk.clone().into();
                        let fingerprint = format_fingerprint(pk.fingerprint().as_bytes());
                        let key_type = detect_key_type_from_public(&pk);
                        let email = extract_email_from_public(&pk);

                        let public_armor = pk.to_armored_string(None.into()).map_err(|e| {
                            internal_err(format!("Failed to armor public key: {}", e))
                        })?;
                        let private_armor = sk.to_armored_string(None.into()).map_err(|e| {
                            internal_err(format!("Failed to armor private key: {}", e))
                        })?;

                        let pub_id = Uuid::new_v4().to_string();
                        let priv_id = Uuid::new_v4().to_string();

                        store_private_key(
                            &conn,
                            &ctx.credential_key,
                            &priv_id,
                            &private_armor,
                            true,
                        )?;

                        conn.execute(
                            "INSERT INTO pgp_keys (id, account_id, email, key_type, fingerprint, key_data, is_private, is_default)
                             VALUES (?1, ?2, ?3, ?4, ?5, ?6, 0, 0)",
                            params![pub_id, req.account_id, email, key_type, fingerprint, public_armor.as_bytes()],
                        )?;

                        let placeholder = b"encrypted:credential_store";
                        conn.execute(
                            "INSERT INTO pgp_keys (id, account_id, email, key_type, fingerprint, key_data, is_private, is_default)
                             VALUES (?1, ?2, ?3, ?4, ?5, ?6, 1, 0)",
                            params![priv_id, req.account_id, email, key_type, fingerprint, placeholder],
                        )?;

                        let info = conn.query_row(
                            "SELECT id, account_id, email, key_type, fingerprint, is_private, is_default, expires_at, created_at
                             FROM pgp_keys WHERE id = ?1",
                            params![priv_id],
                            map_pgp_key,
                        )?;
                        Ok(info)
                    }
                    _ => Err(validation_err(
                        "Input is not a PGP key. Expected a public or private key block."
                            .to_string(),
                    )),
                }
            })
        })
    })
}

#[derive(Serialize)]
struct ExportPgpKeyResponse {
    key_data: String,
}

#[no_mangle]
pub extern "C" fn MahoMailExportPgpKey(
    key_id: *const c_char,
    include_private: bool,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(key_id) = non_empty(key_id, "key_id") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;

                let (is_private, key_data): (bool, Vec<u8>) = conn.query_row(
                    "SELECT is_private, key_data FROM pgp_keys WHERE id = ?1",
                    params![key_id],
                    |row| Ok((row.get(0)?, row.get(1)?)),
                )?;

                let exported = if is_private {
                    if include_private {
                        let armored = get_private_key(&conn, &ctx.credential_key, &key_id, true)?
                            .ok_or_else(|| {
                            not_found_err("Private key data not found in credential store")
                        })?;
                        armored
                    } else {
                        let armored = get_private_key(&conn, &ctx.credential_key, &key_id, true)?
                            .ok_or_else(|| not_found_err("Private key data not found"))?;
                        let (sk, _) = SignedSecretKey::from_string(&armored).map_err(|e| {
                            internal_err(format!("Failed to parse private key: {}", e))
                        })?;
                        let pk: SignedPublicKey = sk.into();
                        let public_armor = pk.to_armored_string(None.into()).map_err(|e| {
                            internal_err(format!("Failed to armor public key: {}", e))
                        })?;
                        public_armor
                    }
                } else {
                    let armor = String::from_utf8(key_data)
                        .map_err(|e| internal_err(format!("Key data is not valid UTF-8: {}", e)))?;
                    armor
                };

                Ok(ExportPgpKeyResponse { key_data: exported })
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailListPgpKeys(
    account_id: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(account_id) = non_empty(account_id, "account_id") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                let mut stmt = conn.prepare(
                    "SELECT id, account_id, email, key_type, fingerprint, is_private, is_default, expires_at, created_at
                     FROM pgp_keys WHERE account_id = ?1 ORDER BY is_private DESC, email ASC",
                )?;
                let rows = stmt
                    .query_map(params![account_id], map_pgp_key)?
                    .collect::<Result<Vec<_>, _>>()?;
                Ok(rows)
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailDeletePgpKey(
    key_id: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(key_id) = non_empty(key_id, "key_id") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;

                let is_private: bool = conn
                    .query_row(
                        "SELECT is_private FROM pgp_keys WHERE id = ?1",
                        params![key_id],
                        |row| row.get(0),
                    )
                    .map_err(|_| not_found_err(format!("Key {} not found", key_id)))?;

                if is_private {
                    let _ = delete_private_key(&conn, &key_id, true);
                }

                conn.execute("DELETE FROM pgp_keys WHERE id = ?1", params![key_id])?;
                Ok("{}".to_string())
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailSetDefaultPgpKey(
    account_id: *const c_char,
    key_id: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(account_id) = non_empty(account_id, "account_id") else {
            return false;
        };
        let Ok(key_id) = non_empty(key_id, "key_id") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                conn.execute(
                    "UPDATE pgp_keys SET is_default = 0 WHERE account_id = ?1 AND is_private = 1",
                    params![account_id],
                )?;
                conn.execute(
                    "UPDATE pgp_keys SET is_default = 1 WHERE id = ?1 AND account_id = ?2 AND is_private = 1",
                    params![key_id, account_id],
                )?;
                Ok("{}".to_string())
            })
        })
    })
}

#[derive(Deserialize)]
struct EncryptEmailPgpRequest {
    account_id: String,
    recipient_emails: Vec<String>,
    plaintext: String,
}

#[derive(Serialize)]
struct EncryptEmailPgpResponse {
    armored: String,
}

#[no_mangle]
pub extern "C" fn MahoMailEncryptEmailPgp(
    request_json: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(raw) = c_string(request_json, "request_json") else {
            return false;
        };
        let Ok(req) = serde_json::from_str::<EncryptEmailPgpRequest>(&raw) else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                let public_keys = {
                    let mut keys = get_public_keys_for_emails(&conn, &req.recipient_emails)?;

                    if let Ok(Some(sender_pk)) = get_default_public_key(&conn, &req.account_id) {
                        let sender_fp = format_fingerprint(sender_pk.fingerprint().as_bytes());
                        let already_included = keys
                            .iter()
                            .any(|k| format_fingerprint(k.fingerprint().as_bytes()) == sender_fp);
                        if !already_included {
                            keys.push(sender_pk);
                        }
                    }
                    keys
                };

                let mut rng = rand::thread_rng();
                let lit_msg = Message::new_literal_bytes("message.txt", req.plaintext.as_bytes());
                let enc_key_refs: Vec<&SignedPublicKey> = public_keys.iter().collect();

                let encrypted = lit_msg
                    .encrypt_to_keys_seipdv1(&mut rng, SymmetricKeyAlgorithm::AES256, &enc_key_refs)
                    .map_err(|e| {
                        internal_err(format!("Failed to create encrypted message: {}", e))
                    })?;

                let armored = encrypted
                    .to_armored_string(Default::default())
                    .map_err(|e| {
                        internal_err(format!("Failed to armor encrypted message: {}", e))
                    })?;

                Ok(EncryptEmailPgpResponse { armored })
            })
        })
    })
}

#[derive(Deserialize)]
struct EncryptAttachmentPgpRequest {
    account_id: String,
    recipient_emails: Vec<String>,
    plaintext: String, // Base64 encoded plaintext bytes
}

#[derive(Serialize)]
struct EncryptAttachmentPgpResponse {
    encrypted_data: String, // Base64 encoded armored encrypted bytes
}

#[no_mangle]
pub extern "C" fn MahoMailEncryptAttachmentPgp(
    request_json: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(raw) = c_string(request_json, "request_json") else {
            return false;
        };
        let Ok(req) = serde_json::from_str::<EncryptAttachmentPgpRequest>(&raw) else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                let plaintext_bytes = base64::prelude::BASE64_STANDARD
                    .decode(&req.plaintext)
                    .map_err(|e| {
                        validation_err(format!("Failed to decode base64 plaintext: {}", e))
                    })?;

                let public_keys = {
                    let mut keys = get_public_keys_for_emails(&conn, &req.recipient_emails)?;

                    if let Ok(Some(sender_pk)) = get_default_public_key(&conn, &req.account_id) {
                        let sender_fp = format_fingerprint(sender_pk.fingerprint().as_bytes());
                        let already_included = keys
                            .iter()
                            .any(|k| format_fingerprint(k.fingerprint().as_bytes()) == sender_fp);
                        if !already_included {
                            keys.push(sender_pk);
                        }
                    }
                    keys
                };

                let enc_key_refs: Vec<&SignedPublicKey> = public_keys.iter().collect();
                let encrypted_bytes = encrypt_attachment_bytes(&plaintext_bytes, &enc_key_refs)?;
                let b64_encrypted = base64::prelude::BASE64_STANDARD.encode(encrypted_bytes);

                Ok(EncryptAttachmentPgpResponse {
                    encrypted_data: b64_encrypted,
                })
            })
        })
    })
}

#[derive(Deserialize)]
struct DecryptEmailPgpRequest {
    account_id: String,
    ciphertext: String,
}

#[derive(Serialize)]
struct DecryptEmailPgpResponse {
    plaintext: String,
}

#[no_mangle]
pub extern "C" fn MahoMailDecryptEmailPgp(
    request_json: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(raw) = c_string(request_json, "request_json") else {
            return false;
        };
        let Ok(req) = serde_json::from_str::<DecryptEmailPgpRequest>(&raw) else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                let (_key_id, secret_key) =
                    get_default_private_key(&conn, &ctx.credential_key, &req.account_id)?;

                let (message, _headers) = Message::from_armor_single(req.ciphertext.as_bytes())
                    .map_err(|e| {
                        internal_err(format!("Failed to parse encrypted message: {}", e))
                    })?;

                let (decrypted, _key_ids) = message
                    .decrypt(String::new, &[&secret_key])
                    .map_err(|e| internal_err(format!("Failed to decrypt message: {}", e)))?;

                let plaintext_bytes = decrypted
                    .get_content()
                    .map_err(|e| internal_err(format!("Failed to read decrypted data: {}", e)))?
                    .ok_or_else(|| internal_err("Decrypted message has no content"))?;

                let plaintext = String::from_utf8(plaintext_bytes).map_err(|e| {
                    internal_err(format!("Decrypted data is not valid UTF-8: {}", e))
                })?;

                Ok(DecryptEmailPgpResponse { plaintext })
            })
        })
    })
}

#[derive(Deserialize)]
struct SignEmailPgpRequest {
    account_id: String,
    message: String,
}

#[derive(Serialize)]
struct SignEmailPgpResponse {
    armored: String,
}

#[no_mangle]
pub extern "C" fn MahoMailSignEmailPgp(
    request_json: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(raw) = c_string(request_json, "request_json") else {
            return false;
        };
        let Ok(req) = serde_json::from_str::<SignEmailPgpRequest>(&raw) else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                let (_key_id, secret_key) =
                    get_default_private_key(&conn, &ctx.credential_key, &req.account_id)?;

                let mut rng = rand::thread_rng();
                let lit_msg = Message::new_literal_bytes("message.txt", req.message.as_bytes());
                let signed_msg = lit_msg
                    .sign(&mut rng, &secret_key, String::new, HashAlgorithm::SHA2_256)
                    .map_err(|e| internal_err(format!("Failed to sign message: {}", e)))?;

                let armored = signed_msg
                    .to_armored_string(Default::default())
                    .map_err(|e| internal_err(format!("Failed to create signed message: {}", e)))?;

                Ok(SignEmailPgpResponse { armored })
            })
        })
    })
}

#[derive(Deserialize)]
struct VerifyEmailPgpRequest {
    sender_email: String,
    message: String,
    signature: String,
}

#[no_mangle]
pub extern "C" fn MahoMailVerifyEmailPgp(
    request_json: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(raw) = c_string(request_json, "request_json") else {
            return false;
        };
        let Ok(req) = serde_json::from_str::<VerifyEmailPgpRequest>(&raw) else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                let key_data_result: Result<Vec<u8>, rusqlite::Error> = conn.query_row(
                    "SELECT key_data FROM pgp_keys WHERE email = ?1 AND is_private = 0 LIMIT 1",
                    params![req.sender_email],
                    |row| row.get(0),
                );

                let key_data = match key_data_result {
                    Ok(data) => data,
                    Err(_) => {
                        return Ok(PgpVerifyResult {
                            is_valid: false,
                            signer_email: Some(req.sender_email),
                            fingerprint: None,
                            error: Some("No public key found for sender".to_string()),
                        });
                    }
                };

                let armor = String::from_utf8(key_data)
                    .map_err(|e| internal_err(format!("Key data not valid UTF-8: {}", e)))?;

                let (pk, _) = SignedPublicKey::from_string(&armor)
                    .map_err(|e| internal_err(format!("Failed to parse public key: {}", e)))?;

                let fingerprint = format_fingerprint(pk.fingerprint().as_bytes());

                let is_signed_message = req.signature.contains("-----BEGIN PGP MESSAGE-----")
                    || req.signature.contains("-----BEGIN PGP SIGNED MESSAGE-----");

                if is_signed_message {
                    match Message::from_armor_single(req.signature.as_bytes()) {
                        Ok((parsed, _)) => match parsed.verify(&pk) {
                            Ok(()) => Ok(PgpVerifyResult {
                                is_valid: true,
                                signer_email: Some(req.sender_email),
                                fingerprint: Some(fingerprint),
                                error: None,
                            }),
                            Err(e) => Ok(PgpVerifyResult {
                                is_valid: false,
                                signer_email: Some(req.sender_email),
                                fingerprint: Some(fingerprint),
                                error: Some(format!("Signature verification failed: {}", e)),
                            }),
                        },
                        Err(e) => Ok(PgpVerifyResult {
                            is_valid: false,
                            signer_email: Some(req.sender_email),
                            fingerprint: Some(fingerprint),
                            error: Some(format!("Failed to parse signed message: {}", e)),
                        }),
                    }
                } else {
                    match StandaloneSignature::from_string(&req.signature) {
                        Ok((standalone_sig, _)) => {
                            let is_valid =
                                standalone_sig.verify(&pk, req.message.as_bytes()).is_ok();
                            Ok(PgpVerifyResult {
                                is_valid,
                                signer_email: Some(req.sender_email),
                                fingerprint: Some(fingerprint),
                                error: if is_valid {
                                    None
                                } else {
                                    Some("Signature does not match".to_string())
                                },
                            })
                        }
                        Err(e) => Ok(PgpVerifyResult {
                            is_valid: false,
                            signer_email: Some(req.sender_email),
                            fingerprint: Some(fingerprint),
                            error: Some(format!("Failed to parse signature: {}", e)),
                        }),
                    }
                }
            })
        })
    })
}

// ----------------- S/MIME Helpers -----------------

fn map_smime_identity(row: &rusqlite::Row) -> rusqlite::Result<SmimeIdentity> {
    Ok(SmimeIdentity {
        id: row.get("id")?,
        account_id: row.get("account_id")?,
        email: row.get("email")?,
        subject: row.get("subject")?,
        issuer: row.get("issuer")?,
        serial_number: row.get("serial_number")?,
        fingerprint: row.get("fingerprint")?,
        not_before: row.get("not_before")?,
        not_after: row.get("not_after")?,
        is_default: row.get("is_default")?,
        created_at: row.get("created_at")?,
    })
}

fn get_default_smime_identity(
    conn: &rusqlite::Connection,
    account_id: &str,
) -> Result<(String, String, String), MailFfiError> {
    let result: Result<(String, String), rusqlite::Error> = conn
        .query_row(
            "SELECT id, cert_pem FROM smime_identities WHERE account_id = ?1 AND is_default = 1 LIMIT 1",
            rusqlite::params![account_id],
            |row| Ok((row.get(0)?, row.get(1)?)),
        )
        .or_else(|_| {
            conn.query_row(
                "SELECT id, cert_pem FROM smime_identities WHERE account_id = ?1 LIMIT 1",
                rusqlite::params![account_id],
                |row| Ok((row.get(0)?, row.get(1)?)),
            )
        });

    match result {
        Ok((id, cert_pem)) => Ok((id, cert_pem, account_id.to_string())),
        Err(_) => Err(not_found_err("No S/MIME identity found for this account")),
    }
}

// ----------------- S/MIME FFI Functions -----------------

#[derive(Deserialize)]
struct ImportSmimeIdentityRequest {
    account_id: String,
    p12_data: String, // Base64 encoded
    password: String,
}

#[no_mangle]
pub extern "C" fn MahoMailImportSmimeIdentity(
    request_json: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(raw) = c_string(request_json, "request_json") else {
            return false;
        };
        let Ok(req) = serde_json::from_str::<ImportSmimeIdentityRequest>(&raw) else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let p12_bytes = base64::prelude::BASE64_STANDARD
                    .decode(&req.p12_data)
                    .map_err(|e| {
                        validation_err(format!("Failed to decode base64 p12_data: {}", e))
                    })?;

                let imported = smime::backend().import_pkcs12(
                    &p12_bytes,
                    &req.password,
                    smime_key_scope(&ctx, &req.account_id)?,
                )?;

                let id = Uuid::new_v4().to_string();

                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;

                let persistence_result = (|| -> Result<(), MailFfiError> {
                    let transaction = conn.unchecked_transaction()?;
                    store_private_key(
                        &transaction,
                        &ctx.credential_key,
                        &id,
                        &imported.key_pem,
                        false,
                    )?;
                    transaction.execute(
                        "INSERT INTO smime_identities (id, account_id, email, subject, issuer, serial_number, fingerprint, not_before, not_after, cert_pem, is_default)
                         VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, 0)",
                        params![id, req.account_id, imported.email, imported.subject, imported.issuer, imported.serial_number, imported.fingerprint, imported.not_before, imported.not_after, imported.cert_pem],
                    )?;
                    transaction.commit()?;
                    Ok(())
                })();
                if let Err(error) = persistence_result {
                    let cleanup = smime::backend().delete_private_key(
                        &imported.cert_pem,
                        &imported.key_pem,
                        smime_key_scope(&ctx, &req.account_id)?,
                    );
                    return match cleanup {
                        Ok(()) => Err(error.into()),
                        Err(cleanup_error) => Err(internal_err(format!(
                            "{error}; CNG key cleanup also failed: {cleanup_error}",
                        ))),
                    };
                }

                let identity = conn.query_row(
                    "SELECT id, account_id, email, subject, issuer, serial_number, fingerprint, not_before, not_after, is_default, created_at
                     FROM smime_identities WHERE id = ?1",
                    params![id],
                    map_smime_identity,
                )?;

                Ok(identity)
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailListSmimeIdentities(
    account_id: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(account_id) = non_empty(account_id, "account_id") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                let mut stmt = conn.prepare(
                    "SELECT id, account_id, email, subject, issuer, serial_number, fingerprint, not_before, not_after, is_default, created_at
                     FROM smime_identities WHERE account_id = ?1 ORDER BY is_default DESC, email ASC",
                )?;
                let rows = stmt
                    .query_map(params![account_id], map_smime_identity)?
                    .collect::<Result<Vec<_>, _>>()?;
                Ok(rows)
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailDeleteSmimeIdentity(
    id: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(id) = non_empty(id, "id") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                let (cert_pem, account_id) = conn.query_row(
                    "SELECT cert_pem, account_id FROM smime_identities WHERE id = ?1",
                    params![id],
                    |row| Ok((row.get::<_, String>(0)?, row.get::<_, String>(1)?)),
                )?;
                if let Some(key_pem) = get_private_key(&conn, &ctx.credential_key, &id, false)? {
                    smime::backend().delete_private_key(
                        &cert_pem,
                        &key_pem,
                        smime_key_scope(&ctx, &account_id)?,
                    )?;
                }
                delete_private_key(&conn, &id, false)?;
                conn.execute("DELETE FROM smime_identities WHERE id = ?1", params![id])?;
                Ok("{}".to_string())
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailSetDefaultSmimeIdentity(
    account_id: *const c_char,
    identity_id: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(account_id) = non_empty(account_id, "account_id") else {
            return false;
        };
        let Ok(identity_id) = non_empty(identity_id, "identity_id") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                conn.execute(
                    "UPDATE smime_identities SET is_default = 0 WHERE account_id = ?1",
                    params![account_id],
                )?;
                conn.execute(
                    "UPDATE smime_identities SET is_default = 1 WHERE id = ?1 AND account_id = ?2",
                    params![identity_id, account_id],
                )?;
                Ok("{}".to_string())
            })
        })
    })
}

#[derive(Serialize)]
struct ExportSmimeCertResponse {
    cert_pem: String,
}

#[no_mangle]
pub extern "C" fn MahoMailExportSmimeCert(
    id: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(id) = non_empty(id, "id") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                let cert_pem: String = conn
                    .query_row(
                        "SELECT cert_pem FROM smime_identities WHERE id = ?1",
                        params![id],
                        |row| row.get(0),
                    )
                    .map_err(|_| not_found_err(format!("S/MIME identity {} not found", id)))?;
                Ok(ExportSmimeCertResponse { cert_pem })
            })
        })
    })
}

#[derive(Deserialize)]
struct SignEmailSmimeRequest {
    account_id: String,
    body: String,
}

#[derive(Serialize)]
struct SignEmailSmimeResponse {
    smime: String,
}

#[no_mangle]
pub extern "C" fn MahoMailSignEmailSmime(
    request_json: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(raw) = c_string(request_json, "request_json") else {
            return false;
        };
        let Ok(req) = serde_json::from_str::<SignEmailSmimeRequest>(&raw) else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                let (identity_id, cert_pem, _) =
                    get_default_smime_identity(&conn, &req.account_id)?;

                let key_pem = get_private_key(&conn, &ctx.credential_key, &identity_id, false)?
                    .ok_or_else(|| {
                        not_found_err("S/MIME private key not found in credential store")
                    })?;

                let smime = smime::backend().sign(
                    &cert_pem,
                    &key_pem,
                    &req.body,
                    smime_key_scope(&ctx, &req.account_id)?,
                )?;

                Ok(SignEmailSmimeResponse { smime })
            })
        })
    })
}

#[derive(Deserialize)]
struct EncryptEmailSmimeRequest {
    account_id: String,
    recipient_emails: Vec<String>,
    recipient_certs_pem: Vec<String>,
    body: String,
    body_html: Option<String>,
    #[serde(default)]
    attachments: Vec<maho_core::smtp_client::ComposeAttachment>,
    #[serde(default)]
    sign: bool,
}

#[derive(Serialize)]
struct EncryptEmailSmimeResponse {
    smime: String,
}

#[no_mangle]
pub extern "C" fn MahoMailEncryptEmailSmime(
    request_json: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(raw) = c_string(request_json, "request_json") else {
            return false;
        };
        let Ok(req) = serde_json::from_str::<EncryptEmailSmimeRequest>(&raw) else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                if req.recipient_emails.is_empty()
                    || req.recipient_emails.iter().any(|email| !smime::recipient::mailbox(email))
                {
                    return Err(validation_err("A complete list of recipient mailboxes is required"));
                }
                let mut candidates = Vec::new();
                for pem in req.recipient_certs_pem {
                    let emails = smime::recipient::emails(&pem)?;
                    candidates.push((pem, emails));
                }
                let mut statement = conn.prepare(
                    "SELECT cert_pem FROM smime_identities WHERE account_id = ?1",
                )?;
                let stored = statement.query_map(params![req.account_id], |row| row.get::<_, String>(0))?;
                for pem in stored {
                    let pem = pem?;
                    match smime::recipient::emails(&pem) {
                        Ok(emails) => candidates.push((pem, emails)),
                        Err(error) => {
                            // An expired/unusable imported identity is not a recipient candidate.
                            log::debug!("S/MIME recipient candidate excluded: {error}");
                        }
                    }
                }
                let mut recipients = Vec::new();
                for email in &req.recipient_emails {
                    let (pem, _) = candidates.iter().find(|(_, names)| names.iter().any(|name| name == email))
                        .ok_or_else(|| validation_err(format!("No usable S/MIME certificate for {email}")))?;
                    if !recipients.contains(pem) { recipients.push(pem.clone()); }
                }
                let mut entity = String::from_utf8(maho_core::smtp_client::build_mime_entity(
                    Some(&req.body), req.body_html.as_deref(), &req.attachments,
                )?).map_err(|error| internal_err(error.to_string()))?;
                if req.sign {
                    let (id, cert, _) = get_default_smime_identity(&conn, &req.account_id)?;
                    let key = get_private_key(&conn, &ctx.credential_key, &id, false)?
                        .ok_or_else(|| not_found_err("S/MIME private key not found in credential store"))?;
                    entity = smime::backend().sign(&cert, &key, &entity, smime_key_scope(&ctx, &req.account_id)?)?;
                }
                let smime = smime::backend().encrypt(
                    &recipients,
                    None,
                    &entity,
                )?;

                Ok(EncryptEmailSmimeResponse { smime })
            })
        })
    })
}

#[derive(Deserialize)]
struct DecryptEmailSmimeRequest {
    account_id: String,
    encrypted_body: String,
}

#[derive(Serialize)]
struct DecryptEmailSmimeResponse {
    decrypted: String,
}

#[no_mangle]
pub extern "C" fn MahoMailDecryptEmailSmime(
    request_json: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(raw) = c_string(request_json, "request_json") else {
            return false;
        };
        let Ok(req) = serde_json::from_str::<DecryptEmailSmimeRequest>(&raw) else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                let (identity_id, cert_pem, _) =
                    get_default_smime_identity(&conn, &req.account_id)?;

                let key_pem = get_private_key(&conn, &ctx.credential_key, &identity_id, false)?
                    .ok_or_else(|| {
                        not_found_err("S/MIME private key not found in credential store")
                    })?;

                let decrypted = smime::backend().decrypt(
                    &cert_pem,
                    &key_pem,
                    &req.encrypted_body,
                    smime_key_scope(&ctx, &req.account_id)?,
                )?;

                Ok(DecryptEmailSmimeResponse { decrypted })
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailVerifyEmailSmime(
    signed_body: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(signed_body) = non_empty(signed_body, "signed_body") else {
            return false;
        };
        accept_read(callback, user_data, move |_ctx| {
            blocking_json(move || Ok(smime::backend().verify(&signed_body)))
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailCleanupSmimeForAccount(
    account_id: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(account_id) = non_empty(account_id, "account_id") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let mut conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                let transaction = conn.transaction()?;
                let identity_ids = cleanup_smime_keys_for_account(&ctx, &transaction, &account_id)?;
                for identity_id in &identity_ids {
                    delete_private_key(&transaction, identity_id, false)?;
                }
                transaction.execute(
                    "DELETE FROM smime_identities WHERE account_id = ?1",
                    [&account_id],
                )?;
                transaction.commit()?;

                Ok("{}".to_string())
            })
        })
    })
}

// ----------------- Unit Tests -----------------

#[cfg(test)]
mod tests {
    use super::*;
    #[cfg(not(windows))]
    use openssl::asn1::Asn1Time;
    #[cfg(not(windows))]
    use openssl::bn::BigNum;
    #[cfg(not(windows))]
    use openssl::hash::MessageDigest;
    #[cfg(not(windows))]
    use openssl::pkey::PKey;
    #[cfg(not(windows))]
    use openssl::rsa::Rsa;
    #[cfg(not(windows))]
    use openssl::x509::{X509NameBuilder, X509};

    #[test]
    fn test_format_fingerprint() {
        let fp = vec![0xAB, 0xCD, 0xEF, 0x12, 0x34];
        let result = format_fingerprint(&fp);
        assert_eq!(result, "ABCDEF1234");
    }

    #[test]
    fn test_format_fingerprint_empty() {
        let fp: Vec<u8> = vec![];
        let result = format_fingerprint(&fp);
        assert_eq!(result, "");
    }

    #[test]
    fn test_smime_profile_path_requires_profile_mail_directory_layout() {
        assert_eq!(
            smime_profile_path(std::path::Path::new(
                "/profiles/default/MahoMail/maho_mail.db",
            ))
            .unwrap(),
            std::path::Path::new("/profiles/default"),
        );
        assert!(smime_profile_path(std::path::Path::new("maho_mail.db")).is_err());
        assert!(
            smime_profile_path(std::path::Path::new("/profiles/default/mail/maho_mail.db"))
                .is_err(),
        );
        assert!(
            smime_profile_path(std::path::Path::new("/profiles/default/MahoMail/other.db",))
                .is_err(),
        );
    }

    #[test]
    fn test_resolve_key_type_rsa2048() {
        let result = resolve_key_type("rsa2048");
        assert!(result.is_ok());
        match result.unwrap() {
            KeyType::Rsa(2048) => (),
            _ => panic!("Expected RSA 2048"),
        }
    }

    #[test]
    fn test_resolve_key_type_rsa4096() {
        let result = resolve_key_type("rsa4096");
        assert!(result.is_ok());
        match result.unwrap() {
            KeyType::Rsa(4096) => (),
            _ => panic!("Expected RSA 4096"),
        }
    }

    #[test]
    fn test_resolve_key_type_ed25519() {
        let result = resolve_key_type("ed25519");
        assert!(result.is_ok());
        match result.unwrap() {
            KeyType::EdDSALegacy => (),
            _ => panic!("Expected EdDSALegacy"),
        }
    }

    #[test]
    fn test_resolve_key_type_invalid() {
        let result = resolve_key_type("invalid");
        assert!(result.is_err());
    }

    #[cfg(not(windows))]
    fn create_test_cert_with_email(email: &str) -> (X509, PKey<openssl::pkey::Private>) {
        let rsa = Rsa::generate(2048).expect("Failed to generate RSA key");
        let pkey = PKey::from_rsa(rsa).expect("Failed to create PKey");

        let mut name_builder = X509NameBuilder::new().expect("Failed to create name builder");
        name_builder
            .append_entry_by_nid(openssl::nid::Nid::COMMONNAME, "Test User")
            .expect("Failed to add CN");
        name_builder
            .append_entry_by_nid(openssl::nid::Nid::PKCS9_EMAILADDRESS, email)
            .expect("Failed to add email");
        let name = name_builder.build();

        let mut cert_builder = X509::builder().expect("Failed to create cert builder");
        cert_builder
            .set_subject_name(&name)
            .expect("Failed to set subject");
        cert_builder
            .set_issuer_name(&name)
            .expect("Failed to set issuer");

        let not_before = Asn1Time::days_from_now(0).expect("Failed to create not_before");
        let not_after = Asn1Time::days_from_now(365).expect("Failed to create not_after");
        cert_builder
            .set_not_before(&not_before)
            .expect("Failed to set not_before");
        cert_builder
            .set_not_after(&not_after)
            .expect("Failed to set not_after");

        let serial = BigNum::from_u32(1).expect("Failed to create serial");
        let serial = serial.to_asn1_integer().expect("Failed to convert serial");
        cert_builder
            .set_serial_number(&serial)
            .expect("Failed to set serial");

        cert_builder
            .set_pubkey(&pkey)
            .expect("Failed to set pubkey");
        cert_builder
            .sign(&pkey, MessageDigest::sha256())
            .expect("Failed to sign cert");

        (cert_builder.build(), pkey)
    }

    #[cfg(not(windows))]
    #[test]
    fn test_smime_backend_fingerprint_format() {
        let (cert, _) = create_test_cert_with_email("test@example.com");
        let cert_pem = String::from_utf8(cert.to_pem().unwrap()).unwrap();
        let fingerprint = smime::backend().certificate_fingerprint(&cert_pem).unwrap();
        assert_eq!(fingerprint.len(), 95);
        for c in fingerprint.chars() {
            assert!(c.is_ascii_hexdigit() || c == ':');
        }
    }
}
