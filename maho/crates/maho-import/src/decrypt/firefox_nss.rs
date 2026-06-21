//! Pure-Rust Firefox NSS password decryption (key4.db + logins.json).
//!
//! Supports NSS 4 key databases (Firefox 75+) with PBKDF2-HMAC-SHA256 or
//! PBKDF2-HMAC-SHA384 (Firefox 146+) key derivation and AES-256-CBC encryption.
//! NSS 3 (key3.db with PKCS#12 PBE) returns [`NssError::UnsupportedVersion`].
//!
//! Security: all key material uses [`zeroize::Zeroizing`]; no password/key data
//! is ever logged.

use std::path::Path;

use aes::cipher::{BlockDecryptMut, KeyIvInit, block_padding::Pkcs7};
use hmac::Hmac;
use rusqlite::{Connection, OpenFlags};
use sha2::{Digest, Sha256, Sha384};
use zeroize::Zeroizing;

use super::asn1::{
    self, OID_3DES_CBC, OID_AES_256_CBC, OID_PBES2, OID_PBKDF2,
    OID_PKCS12_PBE_SHA1_3DES, TAG_SEQUENCE,
};

// ---------- Error type ----------

/// Errors from NSS key extraction and decryption.
#[derive(Clone, Debug, thiserror::Error)]
pub enum NssError {
    #[error("key4.db missing")]
    KeyDbMissing,
    #[error("master password incorrect")]
    WrongMasterPassword,
    #[error("unsupported NSS version (legacy NSS 3 key3.db not yet supported)")]
    UnsupportedVersion,
    #[error("ASN.1 parse: {0}")]
    Asn1(String),
    #[error("crypto: {0}")]
    Crypto(String),
    #[error("decode: {0}")]
    Decode(String),
    #[error("database: {0}")]
    Database(String),
}

impl From<asn1::Asn1Error> for NssError {
    fn from(e: asn1::Asn1Error) -> Self {
        NssError::Asn1(e.0)
    }
}

// ---------- Master key ----------

/// The decrypted NSS master key used to decrypt individual login fields.
/// Stores up to 32 bytes of key material (24 for 3DES, 32 for AES-256).
/// Zeroized on drop.
pub struct NssMasterKey {
    key: Zeroizing<Vec<u8>>,
}

impl std::fmt::Debug for NssMasterKey {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("NssMasterKey")
            .field("len", &self.key.len())
            .finish_non_exhaustive()
    }
}

impl NssMasterKey {
    /// Returns the key bytes for 3DES operations (first 24 bytes).
    fn des_key(&self) -> Result<&[u8], NssError> {
        if self.key.len() < 24 {
            return Err(NssError::Crypto(format!(
                "master key too short for 3DES: {} bytes",
                self.key.len()
            )));
        }
        Ok(&self.key[..24])
    }

    /// Returns the key bytes for AES-256 operations (first 32 bytes).
    fn aes_key(&self) -> Result<&[u8], NssError> {
        if self.key.len() < 32 {
            return Err(NssError::Crypto(format!(
                "master key too short for AES-256: {} bytes",
                self.key.len()
            )));
        }
        Ok(&self.key[..32])
    }
}

// ---------- Public API ----------

/// Extracts the NSS master key from a Firefox profile's `key4.db`.
///
/// Copies `key4.db` to a temp file (avoids locking a running Firefox), reads
/// the PBKDF2 parameters, derives the intermediate key, and decrypts the global
/// master key. Verifies correctness by decrypting the "password-check" sentinel.
///
/// # Arguments
/// * `profile_dir` — path to the Firefox profile directory
/// * `master_password` — the user's master password (empty string if none set)
pub fn unlock_nss_master_key(
    profile_dir: &Path,
    master_password: &str,
) -> Result<NssMasterKey, NssError> {
    let key4_path = profile_dir.join("key4.db");
    let key3_path = profile_dir.join("key3.db");

    if !key4_path.exists() {
        if key3_path.exists() {
            return Err(NssError::UnsupportedVersion);
        }
        return Err(NssError::KeyDbMissing);
    }

    // Copy to temp to avoid locking a running Firefox.
    let temp_dir = tempfile::tempdir()
        .map_err(|e| NssError::Database(format!("creating temp dir: {e}")))?;
    let temp_db = temp_dir.path().join("key4.db");
    std::fs::copy(&key4_path, &temp_db)
        .map_err(|e| NssError::Database(format!("copying key4.db: {e}")))?;

    let conn = Connection::open_with_flags(
        &temp_db,
        OpenFlags::SQLITE_OPEN_READ_ONLY | OpenFlags::SQLITE_OPEN_NO_MUTEX,
    )
    .map_err(|e| NssError::Database(format!("opening key4.db: {e}")))?;

    // Read global salt and encrypted password-check from metaData.
    let (global_salt, password_check_blob) = read_metadata(&conn)?;

    // Read the encrypted master key (ASN.1-wrapped) from nssPrivate.
    let encrypted_key_blob = read_nss_private(&conn)?;

    // Detect variant: item1.len() > 40 → SHA-384 (Variant C), else SHA-256 (Variant B).
    let use_sha384 = global_salt.len() > 40;

    // Parse ASN.1 from the encrypted key blob to get PBKDF2 params.
    let key_params = parse_encrypted_key_asn1(&encrypted_key_blob)?;

    // Derive the intermediate key.
    let intermediate = derive_intermediate_key(
        master_password,
        &global_salt,
        &key_params.entry_salt,
        key_params.iterations,
        use_sha384,
    )?;

    // Decrypt the master key.
    let master_key_bytes = decrypt_with_params(
        &intermediate,
        &key_params.iv,
        &key_params.ciphertext,
        key_params.is_aes256,
    )?;

    let master_key = NssMasterKey {
        key: master_key_bytes,
    };

    // Verify by decrypting the password-check sentinel.
    verify_password_check(&master_key, &password_check_blob)?;

    Ok(master_key)
}

/// Decrypts a Base64-encoded encrypted value from `logins.json`.
///
/// The value is the `encryptedUsername` or `encryptedPassword` field.
/// Returns the decrypted UTF-8 plaintext.
pub fn decrypt_nss_value(
    encrypted_b64: &str,
    master_key: &NssMasterKey,
) -> Result<String, NssError> {
    use base64::Engine;
    let raw = base64::engine::general_purpose::STANDARD
        .decode(encrypted_b64)
        .map_err(|e| NssError::Decode(format!("base64: {e}")))?;

    let field = parse_login_field_asn1(&raw)?;

    let plaintext = if field.is_3des {
        decrypt_3des(master_key.des_key()?, &field.iv, &field.ciphertext)?
    } else {
        decrypt_aes256(master_key.aes_key()?, &field.iv, &field.ciphertext)?
    };

    String::from_utf8(plaintext.to_vec())
        .map_err(|_| NssError::Decode("decrypted value is not valid UTF-8".into()))
}

// ---------- Internal: database reads ----------

fn read_metadata(conn: &Connection) -> Result<(Vec<u8>, Vec<u8>), NssError> {
    let mut stmt = conn
        .prepare("SELECT item1, item2 FROM metadata WHERE id = 'password'")
        .map_err(|e| NssError::Database(format!("metadata query: {e}")))?;

    let (item1, item2): (Vec<u8>, Vec<u8>) = stmt
        .query_row([], |row| {
            Ok((row.get(0)?, row.get(1)?))
        })
        .map_err(|e| NssError::Database(format!("metadata row: {e}")))?;

    if item1.is_empty() {
        return Err(NssError::Database("empty global_salt in metadata".into()));
    }

    Ok((item1, item2))
}

fn read_nss_private(conn: &Connection) -> Result<Vec<u8>, NssError> {
    let mut stmt = conn
        .prepare("SELECT a11 FROM nssPrivate")
        .map_err(|e| NssError::Database(format!("nssPrivate query: {e}")))?;

    let a11: Vec<u8> = stmt
        .query_row([], |row| row.get(0))
        .map_err(|e| NssError::Database(format!("nssPrivate row: {e}")))?;

    if a11.is_empty() {
        return Err(NssError::Database("empty a11 in nssPrivate".into()));
    }

    Ok(a11)
}

// ---------- Internal: ASN.1 parsing ----------

/// Parsed parameters from the encrypted key blob (nssPrivate.a11).
struct EncryptedKeyParams {
    entry_salt: Vec<u8>,
    iterations: u32,
    iv: Vec<u8>,
    ciphertext: Vec<u8>,
    is_aes256: bool,
}

/// Parses the ASN.1 structure from `nssPrivate.a11`.
///
/// Expected structure (PBES2 variant):
/// ```text
/// SEQUENCE {
///   SEQUENCE {                        -- algorithm
///     OID (PBES2 or PKCS12-PBE)
///     SEQUENCE {                      -- PBES2 params
///       SEQUENCE {                    -- PBKDF2 params
///         OID (PBKDF2)
///         SEQUENCE {
///           OCTET STRING (entry_salt)
///           INTEGER (iterations)
///           [optional: INTEGER (key_length)]
///           [optional: AlgorithmIdentifier (prf)]
///         }
///       }
///       SEQUENCE {                    -- encryption algorithm
///         OID (AES-256-CBC or 3DES-CBC)
///         OCTET STRING (IV)
///       }
///     }
///   }
///   OCTET STRING (ciphertext)
/// }
/// ```
fn parse_encrypted_key_asn1(data: &[u8]) -> Result<EncryptedKeyParams, NssError> {
    let (outer, _) = asn1::read_item(data)?;
    let outer_items = asn1::expect_sequence(&outer)?;

    if outer_items.len() < 2 {
        return Err(NssError::Asn1("expected ≥2 items in outer SEQUENCE".into()));
    }

    let algo_seq = asn1::expect_sequence(&outer_items[0])?;
    let ciphertext = asn1::expect_octet_string(&outer_items[1])?.to_vec();

    if algo_seq.is_empty() {
        return Err(NssError::Asn1("empty algorithm SEQUENCE".into()));
    }

    // Check OID to determine variant.
    if asn1::match_oid(&algo_seq[0], OID_PKCS12_PBE_SHA1_3DES) {
        return Err(NssError::UnsupportedVersion);
    }

    if !asn1::match_oid(&algo_seq[0], OID_PBES2) {
        return Err(NssError::Asn1(format!(
            "unsupported algorithm OID: {:02x?}",
            algo_seq[0].data
        )));
    }

    // PBES2 params
    if algo_seq.len() < 2 {
        return Err(NssError::Asn1("PBES2: missing params SEQUENCE".into()));
    }
    let pbes2_params = asn1::expect_sequence(&algo_seq[1])?;
    if pbes2_params.len() < 2 {
        return Err(NssError::Asn1("PBES2 params: need KDF + cipher".into()));
    }

    // PBKDF2 params
    let kdf_seq = asn1::expect_sequence(&pbes2_params[0])?;
    if kdf_seq.len() < 2 {
        return Err(NssError::Asn1("KDF SEQUENCE too short".into()));
    }
    if !asn1::match_oid(&kdf_seq[0], OID_PBKDF2) {
        return Err(NssError::Asn1("expected PBKDF2 OID in KDF".into()));
    }

    let pbkdf2_params = asn1::expect_sequence(&kdf_seq[1])?;
    if pbkdf2_params.len() < 2 {
        return Err(NssError::Asn1("PBKDF2 params: need salt + iterations".into()));
    }
    let entry_salt = asn1::expect_octet_string(&pbkdf2_params[0])?.to_vec();
    let iterations = asn1::parse_integer_u32(&pbkdf2_params[1])?;

    // Encryption algorithm
    let enc_seq = asn1::expect_sequence(&pbes2_params[1])?;
    if enc_seq.len() < 2 {
        return Err(NssError::Asn1("encryption algo SEQUENCE too short".into()));
    }

    let is_aes256 = if asn1::match_oid(&enc_seq[0], OID_AES_256_CBC) {
        true
    } else if asn1::match_oid(&enc_seq[0], OID_3DES_CBC) {
        false
    } else {
        return Err(NssError::Asn1(format!(
            "unsupported cipher OID: {:02x?}",
            enc_seq[0].data
        )));
    };

    let iv = asn1::expect_octet_string(&enc_seq[1])?.to_vec();

    Ok(EncryptedKeyParams {
        entry_salt,
        iterations,
        iv,
        ciphertext,
        is_aes256,
    })
}

/// Parsed login field ASN.1 structure (from Base64-decoded encrypted username/password).
struct LoginFieldAsn1 {
    iv: Vec<u8>,
    ciphertext: Vec<u8>,
    is_3des: bool,
}

/// Parses the ASN.1 from a decoded login field blob.
///
/// Handles two structural variants:
/// 1. `SEQUENCE { SEQUENCE { OID, OCTET_STRING(iv) }, OCTET_STRING(ciphertext) }`
/// 2. Outer wrapper omitted — peek first child tag.
fn parse_login_field_asn1(data: &[u8]) -> Result<LoginFieldAsn1, NssError> {
    let (outer, _) = asn1::read_item(data)?;

    // The outer item should be a SEQUENCE.
    if outer.tag != TAG_SEQUENCE {
        return Err(NssError::Asn1(format!(
            "login field: expected SEQUENCE, got 0x{:02x}",
            outer.tag
        )));
    }

    let items = asn1::read_items(outer.data)?;

    // Determine structure variant by peeking at the first child.
    let (algo_items, ciphertext) = if items.len() >= 2 && items[0].tag == TAG_SEQUENCE {
        // Variant 1: first child is SEQUENCE (algo), second is OCTET STRING (ciphertext).
        let ct = asn1::expect_octet_string(&items[1])?;
        let algo = asn1::expect_sequence(&items[0])?;
        (algo, ct.to_vec())
    } else if items.len() >= 2 && items[0].tag == asn1::TAG_OID {
        // Variant 2: items are directly [OID, OCTET_STRING(iv)], but we need
        // the ciphertext from a sibling. This case means the outer SEQUENCE
        // directly contains algo items + ciphertext.
        // Re-parse: items[0]=OID, items[1]=OCTET STRING(iv), items[2]=OCTET STRING(ct)
        if items.len() < 3 {
            return Err(NssError::Asn1("login field variant 2: need 3 items".into()));
        }
        let iv = asn1::expect_octet_string(&items[1])?;
        let ct = asn1::expect_octet_string(&items[2])?;

        let is_3des = asn1::match_oid(&items[0], OID_3DES_CBC);
        return Ok(LoginFieldAsn1 {
            iv: iv.to_vec(),
            ciphertext: ct.to_vec(),
            is_3des,
        });
    } else {
        return Err(NssError::Asn1(format!(
            "login field: unexpected structure, first tag=0x{:02x}, len={}",
            items.first().map(|i| i.tag).unwrap_or(0),
            items.len()
        )));
    };

    if algo_items.len() < 2 {
        return Err(NssError::Asn1("login field algo: need OID + IV".into()));
    }

    let is_3des = asn1::match_oid(&algo_items[0], OID_3DES_CBC);
    let iv = asn1::expect_octet_string(&algo_items[1])?.to_vec();

    Ok(LoginFieldAsn1 {
        iv,
        ciphertext,
        is_3des,
    })
}

// ---------- Internal: key derivation ----------

/// Derives the intermediate key from the master password + global salt + entry salt.
///
/// Variant B (SHA-256): hp = SHA-256(global_salt ∥ password_bytes), then
///   PBKDF2-HMAC-SHA256(hp, entry_salt, iterations, 32).
/// Variant C (SHA-384): hp = SHA-384(global_salt ∥ password_bytes), then
///   PBKDF2-HMAC-SHA384(hp, entry_salt, iterations, 32).
fn derive_intermediate_key(
    master_password: &str,
    global_salt: &[u8],
    entry_salt: &[u8],
    iterations: u32,
    use_sha384: bool,
) -> Result<Zeroizing<Vec<u8>>, NssError> {
    let password_bytes = master_password.as_bytes();

    if use_sha384 {
        // Variant C: SHA-384 pre-hash
        let mut hasher = Sha384::new();
        hasher.update(global_salt);
        hasher.update(password_bytes);
        let hp = hasher.finalize();

        let mut derived = Zeroizing::new(vec![0u8; 32]);
        pbkdf2::pbkdf2::<Hmac<Sha384>>(&hp, entry_salt, iterations, &mut derived)
            .map_err(|e| NssError::Crypto(format!("PBKDF2-SHA384: {e}")))?;
        Ok(derived)
    } else {
        // Variant B: SHA-256 pre-hash
        let mut hasher = Sha256::new();
        hasher.update(global_salt);
        hasher.update(password_bytes);
        let hp = hasher.finalize();

        let mut derived = Zeroizing::new(vec![0u8; 32]);
        pbkdf2::pbkdf2::<Hmac<Sha256>>(&hp, entry_salt, iterations, &mut derived)
            .map_err(|e| NssError::Crypto(format!("PBKDF2-SHA256: {e}")))?;
        Ok(derived)
    }
}

// ---------- Internal: decryption primitives ----------

type Aes256CbcDec = cbc::Decryptor<aes::Aes256>;
type TdesEdeCbcDec = cbc::Decryptor<des::TdesEde3>;

/// Decrypts with AES-256-CBC or 3DES-CBC based on params.
fn decrypt_with_params(
    key: &[u8],
    iv: &[u8],
    ciphertext: &[u8],
    is_aes256: bool,
) -> Result<Zeroizing<Vec<u8>>, NssError> {
    if is_aes256 {
        decrypt_aes256(key, iv, ciphertext)
    } else {
        decrypt_3des(key, iv, ciphertext)
    }
}

fn decrypt_aes256(
    key: &[u8],
    iv: &[u8],
    ciphertext: &[u8],
) -> Result<Zeroizing<Vec<u8>>, NssError> {
    if key.len() < 32 {
        return Err(NssError::Crypto(format!(
            "AES-256 key too short: {} bytes",
            key.len()
        )));
    }
    if iv.len() != 16 {
        return Err(NssError::Crypto(format!(
            "AES-256-CBC IV must be 16 bytes, got {}",
            iv.len()
        )));
    }
    if ciphertext.is_empty() || ciphertext.len() % 16 != 0 {
        return Err(NssError::Crypto(format!(
            "invalid AES ciphertext length: {}",
            ciphertext.len()
        )));
    }

    let mut buf = Zeroizing::new(ciphertext.to_vec());
    let decrypted = Aes256CbcDec::new(key[..32].into(), iv.into())
        .decrypt_padded_mut::<Pkcs7>(&mut buf)
        .map_err(|_| NssError::Crypto("AES-256-CBC decrypt/unpad failed".into()))?;

    Ok(Zeroizing::new(decrypted.to_vec()))
}

fn decrypt_3des(
    key: &[u8],
    iv: &[u8],
    ciphertext: &[u8],
) -> Result<Zeroizing<Vec<u8>>, NssError> {
    if key.len() < 24 {
        return Err(NssError::Crypto(format!(
            "3DES key too short: {} bytes",
            key.len()
        )));
    }
    if iv.len() != 8 {
        return Err(NssError::Crypto(format!(
            "3DES-CBC IV must be 8 bytes, got {}",
            iv.len()
        )));
    }
    if ciphertext.is_empty() || ciphertext.len() % 8 != 0 {
        return Err(NssError::Crypto(format!(
            "invalid 3DES ciphertext length: {}",
            ciphertext.len()
        )));
    }

    let mut buf = Zeroizing::new(ciphertext.to_vec());
    let decrypted = TdesEdeCbcDec::new(key[..24].into(), iv.into())
        .decrypt_padded_mut::<Pkcs7>(&mut buf)
        .map_err(|_| NssError::Crypto("3DES-CBC decrypt/unpad failed".into()))?;

    Ok(Zeroizing::new(decrypted.to_vec()))
}

// ---------- Internal: password-check verification ----------

/// Decrypts the password-check blob and verifies it equals "password-check"
/// (with PKCS#7 padding stripped by the decrypt function).
fn verify_password_check(master_key: &NssMasterKey, check_blob: &[u8]) -> Result<(), NssError> {
    let field = parse_login_field_asn1(check_blob)?;

    let plaintext = if field.is_3des {
        decrypt_3des(master_key.des_key()?, &field.iv, &field.ciphertext)?
    } else {
        decrypt_aes256(master_key.aes_key()?, &field.iv, &field.ciphertext)?
    };

    // The decrypted value should be "password-check\x02\x02" but PKCS#7 strip
    // removes the padding, so we check for "password-check".
    if plaintext.starts_with(b"password-check") {
        Ok(())
    } else {
        Err(NssError::WrongMasterPassword)
    }
}

// ---------- Test support (pub(crate) for use by parsers::firefox::passwords tests) ----------

#[cfg(test)]
pub(crate) mod tests_support {
    use super::*;
    use aes::cipher::BlockEncryptMut as _;

    type Aes256CbcEnc = cbc::Encryptor<aes::Aes256>;

    fn encrypt_aes256_inner(key: &[u8; 32], iv: &[u8; 16], plaintext: &[u8]) -> Vec<u8> {
        let block_size = 16;
        let pad_len = block_size - (plaintext.len() % block_size);
        let total = plaintext.len() + pad_len;
        let mut buf = vec![0u8; total];
        buf[..plaintext.len()].copy_from_slice(plaintext);
        let ct = Aes256CbcEnc::new(key.into(), iv.into())
            .encrypt_padded_mut::<Pkcs7>(&mut buf, plaintext.len())
            .unwrap();
        ct.to_vec()
    }

    fn asn1_seq(content: &[u8]) -> Vec<u8> {
        let mut out = vec![0x30];
        encode_len(content.len(), &mut out);
        out.extend_from_slice(content);
        out
    }

    fn asn1_octet(content: &[u8]) -> Vec<u8> {
        let mut out = vec![0x04];
        encode_len(content.len(), &mut out);
        out.extend_from_slice(content);
        out
    }

    fn asn1_oid(content: &[u8]) -> Vec<u8> {
        let mut out = vec![0x06];
        encode_len(content.len(), &mut out);
        out.extend_from_slice(content);
        out
    }

    fn asn1_int(val: u32) -> Vec<u8> {
        let mut bytes = Vec::new();
        if val == 0 {
            bytes.push(0);
        } else {
            let mut v = val;
            let mut temp = Vec::new();
            while v > 0 {
                temp.push((v & 0xFF) as u8);
                v >>= 8;
            }
            temp.reverse();
            if temp[0] & 0x80 != 0 {
                bytes.push(0x00);
            }
            bytes.extend_from_slice(&temp);
        }
        let mut out = vec![0x02];
        encode_len(bytes.len(), &mut out);
        out.extend_from_slice(&bytes);
        out
    }

    fn encode_len(len: usize, out: &mut Vec<u8>) {
        if len < 128 {
            out.push(len as u8);
        } else if len < 256 {
            out.push(0x81);
            out.push(len as u8);
        } else {
            out.push(0x82);
            out.push((len >> 8) as u8);
            out.push((len & 0xFF) as u8);
        }
    }

    /// Creates a test Firefox profile with key4.db + logins.json.
    /// Returns (TempDir, profile_path, master_key_bytes).
    /// `credentials`: &[username, password, hostname, ...]
    pub(crate) fn create_test_profile(
        master_password: &str,
        credentials: &[&str],
    ) -> (tempfile::TempDir, std::path::PathBuf, Vec<u8>) {
        use base64::Engine;
        use rusqlite::Connection;

        let temp = tempfile::tempdir().unwrap();
        let profile = temp.path().to_path_buf();
        let global_salt = vec![0xAA; 20];
        let master_key = vec![0x55; 32];
        let entry_salt = b"test_entry_salt!";
        let iterations: u32 = 1;
        let iv = [0x01u8; 16];

        // Derive intermediate key.
        let mut hasher = Sha256::new();
        hasher.update(&global_salt);
        hasher.update(master_password.as_bytes());
        let hp = hasher.finalize();
        let mut intermediate = vec![0u8; 32];
        pbkdf2::pbkdf2::<Hmac<Sha256>>(&hp, entry_salt, iterations, &mut intermediate).unwrap();

        // Encrypt master key.
        let encrypted_master_key = encrypt_aes256_inner(
            intermediate.as_slice().try_into().unwrap(),
            &iv,
            &master_key,
        );

        // Build a11 ASN.1.
        let pbkdf2_params = asn1_seq(&[asn1_octet(entry_salt), asn1_int(iterations)].concat());
        let kdf_seq = asn1_seq(&[asn1_oid(super::asn1::OID_PBKDF2), pbkdf2_params].concat());
        let enc_algo_seq = asn1_seq(&[asn1_oid(super::asn1::OID_AES_256_CBC), asn1_octet(&iv)].concat());
        let pbes2_params = asn1_seq(&[kdf_seq, enc_algo_seq].concat());
        let algo_seq = asn1_seq(&[asn1_oid(super::asn1::OID_PBES2), pbes2_params].concat());
        let a11 = asn1_seq(&[algo_seq, asn1_octet(&encrypted_master_key)].concat());

        // Build password-check (encrypt "password-check" with master_key via AES-256-CBC).
        let check_iv = [0x03u8; 16];
        let encrypted_check = encrypt_aes256_inner(
            master_key[..32].try_into().unwrap(),
            &check_iv,
            b"password-check",
        );
        let check_algo = asn1_seq(&[asn1_oid(super::asn1::OID_AES_256_CBC), asn1_octet(&check_iv)].concat());
        let password_check_blob = asn1_seq(&[check_algo, asn1_octet(&encrypted_check)].concat());

        // Create key4.db.
        let conn = Connection::open(profile.join("key4.db")).unwrap();
        conn.execute_batch(
            "CREATE TABLE metadata (id TEXT PRIMARY KEY, item1 BLOB, item2 BLOB);
             CREATE TABLE nssPrivate (a11 BLOB);"
        ).unwrap();
        conn.execute(
            "INSERT INTO metadata (id, item1, item2) VALUES ('password', ?1, ?2)",
            rusqlite::params![global_salt, password_check_blob],
        ).unwrap();
        conn.execute(
            "INSERT INTO nssPrivate (a11) VALUES (?1)",
            rusqlite::params![a11],
        ).unwrap();
        drop(conn);

        // Create logins.json.
        let mut logins = Vec::new();
        let mut i = 0;
        while i + 2 < credentials.len() {
            let username = credentials[i];
            let password = credentials[i + 1];
            let hostname = credentials[i + 2];

            // Encrypt username and password.
            let field_iv = [0x04u8; 16];
            let enc_user = encrypt_aes256_inner(
                master_key[..32].try_into().unwrap(),
                &field_iv,
                username.as_bytes(),
            );
            let enc_pass = encrypt_aes256_inner(
                master_key[..32].try_into().unwrap(),
                &field_iv,
                password.as_bytes(),
            );

            // Wrap in ASN.1.
            let user_algo = asn1_seq(&[asn1_oid(super::asn1::OID_AES_256_CBC), asn1_octet(&field_iv)].concat());
            let user_blob = asn1_seq(&[user_algo, asn1_octet(&enc_user)].concat());
            let pass_algo = asn1_seq(&[asn1_oid(super::asn1::OID_AES_256_CBC), asn1_octet(&field_iv)].concat());
            let pass_blob = asn1_seq(&[pass_algo, asn1_octet(&enc_pass)].concat());

            let user_b64 = base64::engine::general_purpose::STANDARD.encode(&user_blob);
            let pass_b64 = base64::engine::general_purpose::STANDARD.encode(&pass_blob);

            logins.push(serde_json::json!({
                "hostname": hostname,
                "formSubmitURL": hostname,
                "encryptedUsername": user_b64,
                "encryptedPassword": pass_b64,
            }));
            i += 3;
        }

        let logins_json = serde_json::json!({ "logins": logins });
        std::fs::write(
            profile.join("logins.json"),
            serde_json::to_string_pretty(&logins_json).unwrap(),
        ).unwrap();

        (temp, profile, master_key)
    }
}

// ---------- Tests ----------

#[cfg(test)]
mod tests {
    use super::*;
    use aes::cipher::BlockEncryptMut as _;
    use std::io::Write;

    type Aes256CbcEnc = cbc::Encryptor<aes::Aes256>;
    type TdesEdeCbcEnc = cbc::Encryptor<des::TdesEde3>;

    fn encrypt_aes256(key: &[u8; 32], iv: &[u8; 16], plaintext: &[u8]) -> Vec<u8> {
        let block_size = 16;
        let pad_len = block_size - (plaintext.len() % block_size);
        let total = plaintext.len() + pad_len;
        let mut buf = vec![0u8; total];
        buf[..plaintext.len()].copy_from_slice(plaintext);
        let ct = Aes256CbcEnc::new(key.into(), iv.into())
            .encrypt_padded_mut::<Pkcs7>(&mut buf, plaintext.len())
            .unwrap();
        ct.to_vec()
    }

    fn encrypt_3des(key: &[u8], iv: &[u8], plaintext: &[u8]) -> Vec<u8> {
        let block_size = 8;
        let pad_len = block_size - (plaintext.len() % block_size);
        let total = plaintext.len() + pad_len;
        let mut buf = vec![0u8; total];
        buf[..plaintext.len()].copy_from_slice(plaintext);
        let ct = TdesEdeCbcEnc::new(key[..24].into(), iv.into())
            .encrypt_padded_mut::<Pkcs7>(&mut buf, plaintext.len())
            .unwrap();
        ct.to_vec()
    }

    /// Helper: builds an ASN.1 SEQUENCE wrapping given bytes.
    fn asn1_seq(content: &[u8]) -> Vec<u8> {
        let mut out = vec![0x30];
        encode_length(content.len(), &mut out);
        out.extend_from_slice(content);
        out
    }

    /// Helper: builds an ASN.1 OCTET STRING wrapping given bytes.
    fn asn1_octet(content: &[u8]) -> Vec<u8> {
        let mut out = vec![0x04];
        encode_length(content.len(), &mut out);
        out.extend_from_slice(content);
        out
    }

    /// Helper: builds an ASN.1 OID wrapping given encoded bytes.
    fn asn1_oid(content: &[u8]) -> Vec<u8> {
        let mut out = vec![0x06];
        encode_length(content.len(), &mut out);
        out.extend_from_slice(content);
        out
    }

    /// Helper: builds an ASN.1 INTEGER from u32.
    fn asn1_int(val: u32) -> Vec<u8> {
        let mut bytes = Vec::new();
        if val == 0 {
            bytes.push(0);
        } else {
            let mut v = val;
            let mut temp = Vec::new();
            while v > 0 {
                temp.push((v & 0xFF) as u8);
                v >>= 8;
            }
            temp.reverse();
            // Add leading zero if high bit set (positive number).
            if temp[0] & 0x80 != 0 {
                bytes.push(0x00);
            }
            bytes.extend_from_slice(&temp);
        }
        let mut out = vec![0x02];
        encode_length(bytes.len(), &mut out);
        out.extend_from_slice(&bytes);
        out
    }

    fn encode_length(len: usize, out: &mut Vec<u8>) {
        if len < 128 {
            out.push(len as u8);
        } else if len < 256 {
            out.push(0x81);
            out.push(len as u8);
        } else {
            out.push(0x82);
            out.push((len >> 8) as u8);
            out.push((len & 0xFF) as u8);
        }
    }

    /// Creates a test key4.db with known parameters.
    /// Returns (temp_dir, profile_path) — temp_dir must stay alive.
    fn create_test_key4_db(
        global_salt: &[u8],
        master_password: &str,
        master_key: &[u8],
    ) -> (tempfile::TempDir, std::path::PathBuf) {
        let temp = tempfile::tempdir().unwrap();
        let profile = temp.path().to_path_buf();
        let db_path = profile.join("key4.db");

        let conn = Connection::open(&db_path).unwrap();

        // Create tables.
        conn.execute_batch(
            "CREATE TABLE metadata (id TEXT PRIMARY KEY, item1 BLOB, item2 BLOB);
             CREATE TABLE nssPrivate (a11 BLOB);"
        ).unwrap();

        // Derive intermediate key the same way unlock_nss_master_key does.
        let use_sha384 = global_salt.len() > 40;
        let entry_salt = b"test_entry_salt!"; // 16 bytes
        let iterations: u32 = 1;

        let password_bytes = master_password.as_bytes();
        let intermediate = if use_sha384 {
            let mut hasher = Sha384::new();
            hasher.update(global_salt);
            hasher.update(password_bytes);
            let hp = hasher.finalize();
            let mut derived = vec![0u8; 32];
            pbkdf2::pbkdf2::<Hmac<Sha384>>(&hp, entry_salt, iterations, &mut derived).unwrap();
            derived
        } else {
            let mut hasher = Sha256::new();
            hasher.update(global_salt);
            hasher.update(password_bytes);
            let hp = hasher.finalize();
            let mut derived = vec![0u8; 32];
            pbkdf2::pbkdf2::<Hmac<Sha256>>(&hp, entry_salt, iterations, &mut derived).unwrap();
            derived
        };

        let iv = [0x01u8; 16]; // 16-byte IV for AES-256-CBC

        // Encrypt the master key.
        let encrypted_master_key = encrypt_aes256(
            intermediate.as_slice().try_into().unwrap(),
            &iv,
            master_key,
        );

        // Build ASN.1 for a11 (PBES2 + PBKDF2 + AES-256-CBC).
        let pbkdf2_params_content = [
            asn1_octet(entry_salt),
            asn1_int(iterations),
        ].concat();
        let pbkdf2_params = asn1_seq(&pbkdf2_params_content);

        let kdf_content = [
            asn1_oid(OID_PBKDF2),
            pbkdf2_params,
        ].concat();
        let kdf_seq = asn1_seq(&kdf_content);

        let enc_algo_content = [
            asn1_oid(OID_AES_256_CBC),
            asn1_octet(&iv),
        ].concat();
        let enc_algo_seq = asn1_seq(&enc_algo_content);

        let pbes2_params_content = [kdf_seq, enc_algo_seq].concat();
        let pbes2_params = asn1_seq(&pbes2_params_content);

        let algo_content = [
            asn1_oid(OID_PBES2),
            pbes2_params,
        ].concat();
        let algo_seq = asn1_seq(&algo_content);

        let a11 = asn1_seq(&[algo_seq, asn1_octet(&encrypted_master_key)].concat());

        let check_iv = [0x02u8; 8];
        let password_check_blob = if master_key.len() >= 32 {
            let check_iv_aes = [0x03u8; 16];
            let encrypted_check = encrypt_aes256(
                master_key[..32].try_into().unwrap(),
                &check_iv_aes,
                b"password-check",
            );
            let algo_content = [
                asn1_oid(OID_AES_256_CBC),
                asn1_octet(&check_iv_aes),
            ].concat();
            let algo = asn1_seq(&algo_content);
            asn1_seq(&[algo, asn1_octet(&encrypted_check)].concat())
        } else {
            let encrypted_check = encrypt_3des(
                &master_key[..24],
                &check_iv,
                b"password-check",
            );
            let algo_content = [
                asn1_oid(OID_3DES_CBC),
                asn1_octet(&check_iv),
            ].concat();
            let algo = asn1_seq(&algo_content);
            asn1_seq(&[algo, asn1_octet(&encrypted_check)].concat())
        };

        conn.execute(
            "INSERT INTO metadata (id, item1, item2) VALUES ('password', ?1, ?2)",
            rusqlite::params![global_salt, password_check_blob],
        ).unwrap();

        conn.execute(
            "INSERT INTO nssPrivate (a11) VALUES (?1)",
            rusqlite::params![a11],
        ).unwrap();

        (temp, profile)
    }

    #[test]
    fn unlock_with_correct_master_password_succeeds() {
        let global_salt = vec![0xAA; 20]; // 20 bytes → Variant B (SHA-256)
        let master_key = vec![0x55; 32]; // 32-byte master key for AES-256

        let (_temp, profile) = create_test_key4_db(&global_salt, "", &master_key);
        let key = unlock_nss_master_key(&profile, "").unwrap();
        assert_eq!(key.key.as_slice(), &master_key);
    }

    #[test]
    fn unlock_with_wrong_master_password_fails() {
        let global_salt = vec![0xAA; 20];
        let master_key = vec![0x55; 32];

        let (_temp, profile) = create_test_key4_db(&global_salt, "", &master_key);
        let err = unlock_nss_master_key(&profile, "wrongpassword").unwrap_err();
        // Should fail at decryption (wrong intermediate key → garbage decryption → bad padding or wrong check)
        assert!(
            matches!(err, NssError::Crypto(_) | NssError::WrongMasterPassword),
            "expected Crypto or WrongMasterPassword, got: {err:?}"
        );
    }

    #[test]
    fn unlock_with_nonempty_master_password_succeeds() {
        let global_salt = vec![0xBB; 20];
        let master_key = vec![0x77; 32];

        let (_temp, profile) = create_test_key4_db(&global_salt, "test123", &master_key);
        let key = unlock_nss_master_key(&profile, "test123").unwrap();
        assert_eq!(key.key.as_slice(), &master_key);
    }

    #[test]
    fn nss3_returns_unsupported_version() {
        let temp = tempfile::tempdir().unwrap();
        // Create key3.db (empty file) but no key4.db.
        std::fs::File::create(temp.path().join("key3.db")).unwrap();

        let err = unlock_nss_master_key(temp.path(), "").unwrap_err();
        assert!(matches!(err, NssError::UnsupportedVersion));
    }

    #[test]
    fn missing_key_db_returns_error() {
        let temp = tempfile::tempdir().unwrap();
        let err = unlock_nss_master_key(temp.path(), "").unwrap_err();
        assert!(matches!(err, NssError::KeyDbMissing));
    }

    #[test]
    fn decrypt_value_known_vector() {
        // Create a known master key and encrypt a test value.
        let master_key_bytes = vec![0x42; 32];
        let iv = [0x10; 16];
        let plaintext = "hunter2";

        let encrypted = encrypt_aes256(
            master_key_bytes.as_slice().try_into().unwrap(),
            &iv,
            plaintext.as_bytes(),
        );

        // Build ASN.1: SEQUENCE { SEQUENCE { OID(AES-256-CBC), OCTET STRING(iv) }, OCTET STRING(ct) }
        let algo_content = [
            asn1_oid(OID_AES_256_CBC),
            asn1_octet(&iv),
        ].concat();
        let algo = asn1_seq(&algo_content);
        let field_blob = asn1_seq(&[algo, asn1_octet(&encrypted)].concat());

        // Base64-encode.
        use base64::Engine;
        let b64 = base64::engine::general_purpose::STANDARD.encode(&field_blob);

        let key = NssMasterKey {
            key: Zeroizing::new(master_key_bytes),
        };
        let result = decrypt_nss_value(&b64, &key).unwrap();
        assert_eq!(result, "hunter2");
    }

    #[test]
    fn decrypt_value_3des_known_vector() {
        let master_key_bytes = vec![0x42; 24];
        let iv = [0x10; 8];
        let plaintext = "secret_password";

        let encrypted = encrypt_3des(
            &master_key_bytes,
            &iv,
            plaintext.as_bytes(),
        );

        let algo_content = [
            asn1_oid(OID_3DES_CBC),
            asn1_octet(&iv),
        ].concat();
        let algo = asn1_seq(&algo_content);
        let field_blob = asn1_seq(&[algo, asn1_octet(&encrypted)].concat());

        use base64::Engine;
        let b64 = base64::engine::general_purpose::STANDARD.encode(&field_blob);

        let key = NssMasterKey {
            key: Zeroizing::new(master_key_bytes),
        };
        let result = decrypt_nss_value(&b64, &key).unwrap();
        assert_eq!(result, "secret_password");
    }

    #[test]
    fn malformed_asn1_returns_parse_error() {
        use base64::Engine;
        let garbage = base64::engine::general_purpose::STANDARD.encode(&[0xFF, 0x01, 0x02]);
        let key = NssMasterKey {
            key: Zeroizing::new(vec![0x42; 32]),
        };
        let err = decrypt_nss_value(&garbage, &key).unwrap_err();
        assert!(matches!(err, NssError::Asn1(_)));
    }

    #[test]
    fn corrupted_key4_db_returns_error() {
        let temp = tempfile::tempdir().unwrap();
        // Create a corrupt key4.db (not valid SQLite).
        let mut f = std::fs::File::create(temp.path().join("key4.db")).unwrap();
        f.write_all(b"not a sqlite database").unwrap();

        let err = unlock_nss_master_key(temp.path(), "").unwrap_err();
        assert!(matches!(err, NssError::Database(_)));
    }

    #[test]
    fn sha384_variant_c_works() {
        // global_salt > 40 bytes triggers Variant C (SHA-384)
        let global_salt = vec![0xCC; 48];
        let master_key = vec![0xDD; 32];

        let (_temp, profile) = create_test_key4_db(&global_salt, "", &master_key);
        let key = unlock_nss_master_key(&profile, "").unwrap();
        assert_eq!(key.key.as_slice(), &master_key);
    }
}
