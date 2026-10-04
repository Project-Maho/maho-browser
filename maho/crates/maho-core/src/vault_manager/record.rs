//! Crate-private, secret-bearing Vault item record: the only place a decrypted
//! credential payload is materialized in `maho-core`.
//!
//! Never derive a secret-exposing `Debug`, never expose a public serde DTO, and
//! never cache: the record is serialized ONLY into a `Zeroizing<Vec<u8>>` for
//! encryption and deserialized transiently for a single operation. Both the
//! payload and its secret variants are `ZeroizeOnDrop`; non-secret metadata is
//! `#[zeroize(skip)]`. Public output is always a secret-free `VaultItemPublicDto`.

use std::str::FromStr;

use chrono::{DateTime, Utc};
use maho_storage::sqlite::EncryptedVaultItemRow;
use maho_types::passwords::PasswordProviderKind;
use maho_types::vault::{
    CredentialOrigin, VaultCiphertextEnvelope, VaultItemId, VaultItemKind, VaultItemPublicDto,
    VaultItemPublicMetadata, VaultRevision, VaultSchemaVersion,
};
use serde::{Deserialize, Serialize};
use zeroize::{Zeroize, ZeroizeOnDrop, Zeroizing};

use super::credential_form::VaultCredentialFormDetails;
use super::item_error::VaultCrudError;

#[derive(Serialize, Deserialize, Zeroize, ZeroizeOnDrop)]
#[serde(tag = "kind", rename_all = "snake_case")]
pub(crate) enum RecordSecret {
    Login {
        password: String,
    },
    Totp {
        seed: Vec<u8>,
    },
    Passkey {
        private_key: Vec<u8>,
    },
    SecureItem {
        bytes: Vec<u8>,
        notes: Option<String>,
    },
    Tombstone,
}

#[derive(Serialize, Deserialize, Zeroize, ZeroizeOnDrop)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub(crate) struct VaultRecordPayload {
    #[zeroize(skip)]
    pub schema_version: VaultSchemaVersion,
    #[zeroize(skip)]
    pub metadata: VaultItemPublicMetadata,
    pub username: Option<String>,
    pub secret: RecordSecret,
    #[zeroize(skip)]
    pub last_used_at: Option<DateTime<Utc>>,
    /// Task 4: browser-form detail for login records. `#[serde(default)]` is
    /// what makes legacy records (written before this field existed) decode:
    /// they yield `None`, i.e. "no stored form detail", and the consumer falls
    /// back to Chromium's own defaults instead of failing the read.
    #[serde(default)]
    pub form_details: Option<VaultCredentialFormDetails>,
    /// Optional login-attached seed; legacy records have no attached TOTP.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub totp_seed: Option<Vec<u8>>,
}

/// Short-lived owned decrypted secret handed to a privileged caller. Every
/// variant zeroizes on drop; there is no `Debug`/serde exposure.
pub(crate) enum LiveSecret {
    LoginPassword(Zeroizing<String>),
    TotpSeed(Zeroizing<Vec<u8>>),
    PasskeyPrivateKey(Zeroizing<Vec<u8>>),
    SecureBytes(Zeroizing<Vec<u8>>),
}

impl VaultRecordPayload {
    pub(crate) fn has_notes(&self) -> bool {
        match &self.secret {
            RecordSecret::Login { .. } => self.form_details.as_ref()
                .is_some_and(|details| details.notes.iter().any(|note| !note.value.is_empty())),
            RecordSecret::SecureItem { notes, .. } => notes.as_ref().is_some_and(|notes| !notes.is_empty()),
            _ => false,
        }
    }
}

impl LiveSecret {
    pub(crate) fn from_record(
        secret: &RecordSecret,
        kind: VaultItemKind,
    ) -> Result<Self, VaultCrudError> {
        match (secret, kind) {
            (RecordSecret::Login { password }, VaultItemKind::Login) => {
                Ok(Self::LoginPassword(Zeroizing::new(password.clone())))
            }
            (RecordSecret::Totp { seed }, VaultItemKind::Totp) => {
                Ok(Self::TotpSeed(Zeroizing::new(seed.clone())))
            }
            (RecordSecret::Passkey { private_key }, VaultItemKind::Passkey) => {
                Ok(Self::PasskeyPrivateKey(Zeroizing::new(private_key.clone())))
            }
            (RecordSecret::SecureItem { bytes, .. }, VaultItemKind::SecureItem) => {
                Ok(Self::SecureBytes(Zeroizing::new(bytes.clone())))
            }
            _ => Err(VaultCrudError::FieldMismatch),
        }
    }
}

pub(crate) fn serialize_record(
    record: &VaultRecordPayload,
) -> Result<Zeroizing<Vec<u8>>, VaultCrudError> {
    serde_json::to_vec(record)
        .map(Zeroizing::new)
        .map_err(|_| VaultCrudError::MalformedPayload)
}

pub(crate) fn deserialize_record(bytes: &[u8]) -> Result<VaultRecordPayload, VaultCrudError> {
    serde_json::from_slice(bytes).map_err(|_| VaultCrudError::MalformedPayload)
}

pub(crate) fn encode_envelope(
    envelope: &VaultCiphertextEnvelope,
) -> Result<Vec<u8>, VaultCrudError> {
    serde_json::to_vec(envelope).map_err(|_| VaultCrudError::MalformedPayload)
}

pub(crate) fn decode_envelope(bytes: &[u8]) -> Result<VaultCiphertextEnvelope, VaultCrudError> {
    serde_json::from_slice(bytes).map_err(|_| VaultCrudError::MalformedPayload)
}

pub(crate) fn provider_token(provider: PasswordProviderKind) -> String {
    serde_json::to_value(provider)
        .ok()
        .and_then(|value| value.as_str().map(str::to_string))
        .unwrap_or_else(|| "maho_native".to_string())
}

fn parse_provider(token: &str) -> Result<PasswordProviderKind, VaultCrudError> {
    serde_json::from_value(serde_json::Value::String(token.to_string()))
        .map_err(|_| VaultCrudError::MalformedPayload)
}

pub(crate) fn kind_token(kind: VaultItemKind) -> String {
    serde_json::to_value(kind)
        .ok()
        .and_then(|value| value.as_str().map(str::to_string))
        .unwrap_or_else(|| "login".to_string())
}

fn parse_kind(token: &str) -> Result<VaultItemKind, VaultCrudError> {
    serde_json::from_value(serde_json::Value::String(token.to_string()))
        .map_err(|_| VaultCrudError::MalformedPayload)
}

/// Derive a non-authoritative masked username hint. This is display-only and is
/// NEVER used as the duplicate-identity key (that uses the actual username).
pub(crate) fn mask_username(username: &str) -> String {
    let trimmed = username.trim();
    if trimmed.is_empty() {
        return String::new();
    }
    match trimmed.split_once('@') {
        Some((local, domain)) => {
            let first = local.chars().next().unwrap_or('*');
            format!("{first}***@{domain}")
        }
        None => {
            let first = trimmed.chars().next().unwrap_or('*');
            format!("{first}***")
        }
    }
}

pub(crate) fn public_dto(
    row: &EncryptedVaultItemRow,
    record: &VaultRecordPayload,
) -> Result<VaultItemPublicDto, VaultCrudError> {
    let id = VaultItemId::from_str(&row.id).map_err(|_| VaultCrudError::MalformedPayload)?;
    Ok(VaultItemPublicDto {
        favorite: record.metadata.favorite,
        trashed_at: record.metadata.trashed_at,
        has_notes: record.metadata.has_notes,
        schema_version: VaultSchemaVersion::CURRENT,
        id,
        revision: VaultRevision::new(u64::try_from(row.revision).unwrap_or(0)),
        provider: parse_provider(&row.provider)?,
        item_kind: parse_kind(&row.item_kind)?,
        title: record.metadata.title.clone(),
        origins: record.metadata.origins.clone(),
        username_hint: record.metadata.username_hint.clone(),
        created_at: parse_timestamp(&row.created_at)?,
        updated_at: parse_timestamp(&row.updated_at)?,
        last_used_at: record.last_used_at,
        totp: record.metadata.totp.clone(),
        passkey: record.metadata.passkey.clone(),
    })
}

pub(crate) fn canonical_origins(
    origins: &[CredentialOrigin],
) -> Result<Vec<CredentialOrigin>, VaultCrudError> {
    origins
        .iter()
        .map(|origin| super::origin::canonicalize_origin(origin.as_str()))
        .collect()
}

fn parse_timestamp(value: &str) -> Result<DateTime<Utc>, VaultCrudError> {
    DateTime::parse_from_rfc3339(value)
        .map(|timestamp| timestamp.with_timezone(&Utc))
        .map_err(|_| VaultCrudError::MalformedPayload)
}

#[cfg(test)]
mod parity_tests {
    use super::*;

    #[test]
    fn vault_legacy_payload_decodes_without_parity_fields() {
        let payload = br#"{"schemaVersion":1,"metadata":{"title":"Legacy","origins":[],"usernameHint":"a***","itemKind":"login","totp":null,"passkey":null},"username":"alice","secret":{"kind":"login","password":"legacy-secret"},"lastUsedAt":null}"#;
        let record = deserialize_record(payload).unwrap();
        assert!(!record.metadata.favorite);
        assert!(!record.metadata.has_notes);
        assert!(record.metadata.trashed_at.is_none());
        assert!(record.totp_seed.is_none());
        assert!(matches!(&record.secret, RecordSecret::Login { password } if password == "legacy-secret"));
    }
}
