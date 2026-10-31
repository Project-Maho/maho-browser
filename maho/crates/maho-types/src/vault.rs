// allow: SIZE_OK - this file is the single versioned Vault wire-contract registry required by the plan.
use std::fmt;
use std::str::FromStr;

use chrono::{DateTime, Utc};
use serde::de::{SeqAccess, Visitor};
use serde::{Deserialize, Serialize};
use uuid::Uuid;

use crate::identifiers::{ProfileId, TabId};
use crate::passwords::{PasswordProviderDescriptor, PasswordProviderKind};

#[derive(Clone, Debug, PartialEq, Eq)]
#[non_exhaustive]
pub enum VaultContractError {
    UnsupportedSchemaVersion {
        found: u16,
    },
    InvalidIdentifier {
        type_name: &'static str,
        value: String,
    },
    InvalidOrigin {
        value: String,
    },
    InvalidPasskeyRpId {
        value: String,
    },
    InvalidTotpDigits {
        found: u8,
    },
}

impl fmt::Display for VaultContractError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::UnsupportedSchemaVersion { found } => {
                write!(formatter, "unsupported Vault schema version {found}")
            }
            Self::InvalidIdentifier { type_name, value } => {
                write!(formatter, "invalid {type_name} UUID: {value}")
            }
            Self::InvalidOrigin { value } => {
                write!(formatter, "invalid credential origin: {value}")
            }
            Self::InvalidPasskeyRpId { value } => {
                write!(formatter, "invalid passkey RP ID: {value}")
            }
            Self::InvalidTotpDigits { found } => {
                write!(formatter, "invalid TOTP digit count: {found}")
            }
        }
    }
}

impl std::error::Error for VaultContractError {}

macro_rules! define_uuid_id {
    ($name:ident) => {
        #[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, Serialize, Deserialize)]
        #[serde(transparent)]
        pub struct $name(Uuid);

        impl $name {
            pub fn new() -> Self {
                Self(Uuid::new_v4())
            }

            pub const fn from_uuid(value: Uuid) -> Self {
                Self(value)
            }

            pub const fn as_uuid(&self) -> &Uuid {
                &self.0
            }
        }

        impl Default for $name {
            fn default() -> Self {
                Self::new()
            }
        }

        impl fmt::Display for $name {
            fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
                self.0.fmt(formatter)
            }
        }

        impl FromStr for $name {
            type Err = VaultContractError;

            fn from_str(value: &str) -> Result<Self, Self::Err> {
                Uuid::parse_str(value).map(Self).map_err(|_| {
                    VaultContractError::InvalidIdentifier {
                        type_name: stringify!($name),
                        value: value.to_string(),
                    }
                })
            }
        }
    };
}

define_uuid_id!(VaultItemId);
define_uuid_id!(CredentialItemAlias);
define_uuid_id!(CredentialGrantHandle);
define_uuid_id!(VaultSessionId);
define_uuid_id!(VaultTaskId);
define_uuid_id!(VaultWorkspaceId);
define_uuid_id!(VaultAuditRecordId);

macro_rules! define_number {
    ($name:ident, $inner:ty) => {
        #[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Hash, Serialize, Deserialize)]
        #[serde(transparent)]
        pub struct $name($inner);

        impl $name {
            pub const fn new(value: $inner) -> Self {
                Self(value)
            }

            pub const fn value(self) -> $inner {
                self.0
            }
        }
    };
}

define_number!(VaultRevision, u64);
define_number!(VaultKeyVersion, u32);
define_number!(TabGeneration, u64);
define_number!(TotpPeriodSeconds, u32);

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
#[non_exhaustive]
pub enum VaultSchemaVersion {
    V1,
    #[doc(hidden)]
    Unsupported(u16),
}

impl VaultSchemaVersion {
    pub const CURRENT: Self = Self::V1;
}

impl TryFrom<u16> for VaultSchemaVersion {
    type Error = VaultContractError;

    fn try_from(value: u16) -> Result<Self, Self::Error> {
        match value {
            1 => Ok(Self::V1),
            found => Err(VaultContractError::UnsupportedSchemaVersion { found }),
        }
    }
}

impl From<VaultSchemaVersion> for u16 {
    fn from(value: VaultSchemaVersion) -> Self {
        match value {
            VaultSchemaVersion::V1 => 1,
            VaultSchemaVersion::Unsupported(value) => value,
        }
    }
}

pub const MAX_VAULT_ENVELOPE_NONCE_LEN: usize = 64;
pub const MAX_VAULT_ENVELOPE_CIPHERTEXT_LEN: usize = 1024 * 1024;
pub const MAX_VAULT_ENVELOPE_TAG_LEN: usize = 64;

struct BoundedBytesVisitor<const MAX: usize>;

impl<'de, const MAX: usize> Visitor<'de> for BoundedBytesVisitor<MAX> {
    type Value = Vec<u8>;

    fn expecting(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(formatter, "at most {MAX} bytes")
    }

    fn visit_seq<A>(self, mut sequence: A) -> Result<Self::Value, A::Error>
    where
        A: SeqAccess<'de>,
    {
        let mut bytes = Vec::with_capacity(sequence.size_hint().unwrap_or(0).min(MAX));
        while let Some(byte) = sequence.next_element()? {
            if bytes.len() == MAX {
                return Err(serde::de::Error::invalid_length(MAX + 1, &self));
            }
            bytes.push(byte);
        }
        Ok(bytes)
    }
}

fn deserialize_bounded_bytes<'de, D, const MAX: usize>(deserializer: D) -> Result<Vec<u8>, D::Error>
where
    D: serde::Deserializer<'de>,
{
    deserializer.deserialize_seq(BoundedBytesVisitor::<MAX>)
}

fn deserialize_vault_nonce<'de, D>(deserializer: D) -> Result<Vec<u8>, D::Error>
where
    D: serde::Deserializer<'de>,
{
    deserialize_bounded_bytes::<D, MAX_VAULT_ENVELOPE_NONCE_LEN>(deserializer)
}

fn deserialize_vault_ciphertext<'de, D>(deserializer: D) -> Result<Vec<u8>, D::Error>
where
    D: serde::Deserializer<'de>,
{
    deserialize_bounded_bytes::<D, MAX_VAULT_ENVELOPE_CIPHERTEXT_LEN>(deserializer)
}

fn deserialize_vault_tag<'de, D>(deserializer: D) -> Result<Vec<u8>, D::Error>
where
    D: serde::Deserializer<'de>,
{
    deserialize_bounded_bytes::<D, MAX_VAULT_ENVELOPE_TAG_LEN>(deserializer)
}

impl Serialize for VaultSchemaVersion {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: serde::Serializer,
    {
        match self {
            Self::V1 => 1_u16.serialize(serializer),
            Self::Unsupported(found) => Err(serde::ser::Error::custom(
                VaultContractError::UnsupportedSchemaVersion { found: *found },
            )),
        }
    }
}

impl<'de> Deserialize<'de> for VaultSchemaVersion {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: serde::Deserializer<'de>,
    {
        Self::try_from(u16::deserialize(deserializer)?).map_err(serde::de::Error::custom)
    }
}

#[derive(Clone, Debug, PartialEq, Eq, Hash, Serialize, Deserialize)]
#[serde(try_from = "String", into = "String")]
pub struct CredentialOrigin(String);

impl CredentialOrigin {
    pub fn as_str(&self) -> &str {
        &self.0
    }
}

impl AsRef<str> for CredentialOrigin {
    fn as_ref(&self) -> &str {
        self.as_str()
    }
}

impl fmt::Display for CredentialOrigin {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        self.0.fmt(formatter)
    }
}

impl From<CredentialOrigin> for String {
    fn from(value: CredentialOrigin) -> Self {
        value.0
    }
}

impl FromStr for CredentialOrigin {
    type Err = VaultContractError;

    fn from_str(value: &str) -> Result<Self, Self::Err> {
        let valid = value.split_once("://").is_some_and(|(scheme, authority)| {
            !scheme.is_empty()
                && !authority.is_empty()
                && !authority.contains(['/', '?', '#'])
                && scheme.chars().all(|character| {
                    character.is_ascii_alphanumeric() || matches!(character, '+' | '-' | '.')
                })
        });
        if valid {
            Ok(Self(value.to_string()))
        } else {
            Err(VaultContractError::InvalidOrigin {
                value: value.to_string(),
            })
        }
    }
}

impl TryFrom<&str> for CredentialOrigin {
    type Error = VaultContractError;

    fn try_from(value: &str) -> Result<Self, Self::Error> {
        Self::from_str(value)
    }
}

impl TryFrom<String> for CredentialOrigin {
    type Error = VaultContractError;

    fn try_from(value: String) -> Result<Self, Self::Error> {
        Self::from_str(&value)
    }
}

#[derive(Clone, Debug, PartialEq, Eq, Hash, Serialize, Deserialize)]
#[serde(try_from = "String", into = "String")]
pub struct PasskeyRpId(String);

impl AsRef<str> for PasskeyRpId {
    fn as_ref(&self) -> &str {
        &self.0
    }
}

impl From<PasskeyRpId> for String {
    fn from(value: PasskeyRpId) -> Self {
        value.0
    }
}

impl TryFrom<&str> for PasskeyRpId {
    type Error = VaultContractError;

    fn try_from(value: &str) -> Result<Self, Self::Error> {
        let valid = !value.is_empty()
            && !value.contains("://")
            && !value.contains(['/', '?', '#'])
            && value.split('.').all(|label| !label.is_empty());
        if valid {
            Ok(Self(value.to_string()))
        } else {
            Err(VaultContractError::InvalidPasskeyRpId {
                value: value.to_string(),
            })
        }
    }
}

impl TryFrom<String> for PasskeyRpId {
    type Error = VaultContractError;

    fn try_from(value: String) -> Result<Self, Self::Error> {
        Self::try_from(value.as_str())
    }
}

macro_rules! string_enum {
    ($name:ident { $($variant:ident),+ $(,)? }) => {
        #[derive(Clone, Copy, Debug, Serialize, Deserialize, PartialEq, Eq, Hash)]
        #[non_exhaustive]
        #[serde(rename_all = "snake_case")]
        pub enum $name {
            $($variant),+
        }
    };
}

string_enum!(VaultItemKind {
    Login,
    Totp,
    Passkey,
    SecureItem
});
#[derive(Clone, Copy, Debug, Serialize, Deserialize, PartialEq, Eq, Hash)]
#[non_exhaustive]
pub enum VaultCipherAlgorithm {
    #[serde(rename = "aes_256_gcm")]
    Aes256Gcm,
}
string_enum!(VaultCiphertextPurpose {
    VaultItem,
    TotpSeed,
    PasskeyPrivateKey
});
string_enum!(VaultLockState {
    Uninitialized,
    Locked,
    Unlocked,
    AutoLocked
});
string_enum!(VaultAgentPolicy {
    Deny,
    AskEveryUse,
    AllowForTask,
    WhileUnlocked,
    AlwaysAllow
});
string_enum!(CredentialField {
    Username,
    Password,
    Totp,
    Passkey
});
string_enum!(CredentialGrantState {
    Active,
    Exhausted,
    Expired,
    Revoked
});
string_enum!(CredentialRevocationReason {
    User,
    PolicyChanged,
    VaultLocked,
    SessionEnded,
    Navigation,
    Expired,
    UsesExhausted
});
string_enum!(VaultAuditOperation {
    Initialized,
    Unlocked,
    Locked,
    ItemCreated,
    ItemUpdated,
    ItemDeleted,
    GrantIssued,
    GrantUsed,
    GrantRevoked,
    Fill,
    TotpFill,
    PasskeyUsed,
    ImportCommit,
    PolicyChanged
});
string_enum!(VaultAuditDecision {
    Allowed,
    Denied,
    Failed
});
string_enum!(TotpAlgorithm {
    Sha1,
    Sha256,
    Sha512
});
string_enum!(PasskeyTransport {
    Internal,
    Usb,
    Nfc,
    Ble,
    Hybrid
});
string_enum!(PasskeyUserVerification {
    Required,
    Preferred,
    Discouraged
});
string_enum!(VaultErrorCode {
    Uninitialized,
    Locked,
    InvalidCredentials,
    RateLimited,
    ProviderUnavailable,
    UnsupportedSchemaVersion,
    RevisionConflict,
    InvalidOrigin,
    InvalidGrant,
    GrantExpired,
    GrantRevoked,
    GrantExhausted,
    SecretFieldForbidden,
    NotFound,
    StorageFailure
});

impl Default for VaultItemKind {
    fn default() -> Self {
        Self::Login
    }
}

impl Default for VaultLockState {
    fn default() -> Self {
        Self::Uninitialized
    }
}

impl Default for VaultAgentPolicy {
    fn default() -> Self {
        Self::Deny
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
#[non_exhaustive]
pub enum TotpDigits {
    Six,
    Eight,
}

impl TryFrom<u8> for TotpDigits {
    type Error = VaultContractError;

    fn try_from(value: u8) -> Result<Self, Self::Error> {
        match value {
            6 => Ok(Self::Six),
            8 => Ok(Self::Eight),
            found => Err(VaultContractError::InvalidTotpDigits { found }),
        }
    }
}

impl From<TotpDigits> for u8 {
    fn from(value: TotpDigits) -> Self {
        match value {
            TotpDigits::Six => 6,
            TotpDigits::Eight => 8,
        }
    }
}

impl Serialize for TotpDigits {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: serde::Serializer,
    {
        u8::from(*self).serialize(serializer)
    }
}

impl<'de> Deserialize<'de> for TotpDigits {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: serde::Deserializer<'de>,
    {
        Self::try_from(u8::deserialize(deserializer)?).map_err(serde::de::Error::custom)
    }
}

#[derive(Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct VaultCiphertextEnvelope {
    pub schema_version: VaultSchemaVersion,
    pub algorithm: VaultCipherAlgorithm,
    pub purpose: VaultCiphertextPurpose,
    pub key_version: VaultKeyVersion,
    #[serde(deserialize_with = "deserialize_vault_nonce")]
    pub nonce: Vec<u8>,
    #[serde(deserialize_with = "deserialize_vault_ciphertext")]
    pub ciphertext: Vec<u8>,
    #[serde(deserialize_with = "deserialize_vault_tag")]
    pub tag: Vec<u8>,
}

impl fmt::Debug for VaultCiphertextEnvelope {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter
            .debug_struct("VaultCiphertextEnvelope")
            .field("schema_version", &self.schema_version)
            .field("algorithm", &self.algorithm)
            .field("purpose", &self.purpose)
            .field("key_version", &self.key_version)
            .field("nonce", &"[REDACTED]")
            .field("ciphertext", &"[REDACTED]")
            .field("tag", &"[REDACTED]")
            .finish()
    }
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct TotpMetadata {
    pub issuer: String,
    pub account_label: String,
    pub algorithm: TotpAlgorithm,
    pub digits: TotpDigits,
    pub period_seconds: TotpPeriodSeconds,
    pub created_at: DateTime<Utc>,
    pub last_used_at: Option<DateTime<Utc>>,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct PasskeyMetadata {
    pub rp_id: PasskeyRpId,
    pub user_name_hint: String,
    pub display_name: String,
    pub credential_id: Vec<u8>,
    pub transports: Vec<PasskeyTransport>,
    pub discoverable: bool,
    pub backup_eligible: bool,
    pub backup_state: bool,
    pub user_verification: PasskeyUserVerification,
    pub created_at: DateTime<Utc>,
    pub last_used_at: Option<DateTime<Utc>>,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct VaultItemPublicMetadata {
    #[serde(default)]
    pub favorite: bool,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub trashed_at: Option<DateTime<Utc>>,
    #[serde(default)]
    pub has_notes: bool,
    pub title: String,
    pub origins: Vec<CredentialOrigin>,
    pub username_hint: String,
    pub item_kind: VaultItemKind,
    pub totp: Option<TotpMetadata>,
    pub passkey: Option<PasskeyMetadata>,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct EncryptedVaultItemDto {
    pub schema_version: VaultSchemaVersion,
    pub id: VaultItemId,
    pub revision: VaultRevision,
    pub provider: PasswordProviderKind,
    pub item_kind: VaultItemKind,
    pub envelope: VaultCiphertextEnvelope,
    pub created_at: DateTime<Utc>,
    pub updated_at: DateTime<Utc>,
    pub deleted_at: Option<DateTime<Utc>>,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct VaultItemPublicDto {
    #[serde(default)]
    pub favorite: bool,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub trashed_at: Option<DateTime<Utc>>,
    #[serde(default)]
    pub has_notes: bool,
    pub schema_version: VaultSchemaVersion,
    pub id: VaultItemId,
    pub revision: VaultRevision,
    pub provider: PasswordProviderKind,
    pub item_kind: VaultItemKind,
    pub title: String,
    pub origins: Vec<CredentialOrigin>,
    pub username_hint: String,
    pub created_at: DateTime<Utc>,
    pub updated_at: DateTime<Utc>,
    pub last_used_at: Option<DateTime<Utc>>,
    pub totp: Option<TotpMetadata>,
    pub passkey: Option<PasskeyMetadata>,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct VaultBatchReadRequest {
    pub schema_version: VaultSchemaVersion,
    pub item_ids: Vec<VaultItemId>,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct VaultBatchReadResult {
    pub schema_version: VaultSchemaVersion,
    pub items: Vec<VaultItemPublicDto>,
    pub missing_ids: Vec<VaultItemId>,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct VaultItemCreatedRange {
    pub schema_version: VaultSchemaVersion,
    pub from_inclusive: Option<DateTime<Utc>>,
    pub until_exclusive: Option<DateTime<Utc>>,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct VaultRangeDeleteResult {
    pub schema_version: VaultSchemaVersion,
    pub tombstoned_ids: Vec<VaultItemId>,
    pub count: u64,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct VaultItemListRequest {
    #[serde(default)]
    pub trash: VaultTrashFilter,
    #[serde(default)]
    pub favorites_only: bool,
    pub schema_version: VaultSchemaVersion,
    pub provider: Option<PasswordProviderKind>,
    pub kinds: Vec<VaultItemKind>,
    pub cursor: Option<String>,
    pub limit: u32,
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum VaultTrashFilter {
    #[default]
    Exclude,
    Only,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct VaultHealthReport {
    pub weak: Vec<VaultItemId>,
    pub reused: Vec<Vec<VaultItemId>>,
    pub total_logins: u64,
}

#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct VaultTotpCode {
    pub code: String,
    pub seconds_remaining: u64,
    pub period: u64,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct VaultItemSearchRequest {
    pub schema_version: VaultSchemaVersion,
    pub origin: CredentialOrigin,
    pub provider: Option<PasswordProviderKind>,
    pub kinds: Vec<VaultItemKind>,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct VaultItemUpdateRequest {
    pub schema_version: VaultSchemaVersion,
    pub id: VaultItemId,
    pub expected_revision: VaultRevision,
    pub public_metadata: VaultItemPublicMetadata,
    pub encrypted_payload: VaultCiphertextEnvelope,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct AgentVaultItemDto {
    pub schema_version: VaultSchemaVersion,
    pub alias: CredentialItemAlias,
    pub item_kind: VaultItemKind,
    pub display_label: String,
    pub username_hint: String,
    pub available_fields: Vec<CredentialField>,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct VaultStatus {
    pub schema_version: VaultSchemaVersion,
    pub lock_state: VaultLockState,
    pub selected_provider: PasswordProviderKind,
    pub effective_provider: PasswordProviderKind,
    pub item_count: u64,
    pub agent_policy_default: VaultAgentPolicy,
    pub auto_lock_minutes: u32,
    pub failed_unlock_count: u32,
    pub retry_at: Option<DateTime<Utc>>,
    /// Whether the Vault key has an account-escrowed wrap slot (zero-input
    /// cross-device unlock). Passphrase-provisioned vaults report false until
    /// migrated.
    pub account_escrowed: bool,
}

/// Ciphertext-only wire record for one Vault item's cross-device sync. The
/// envelope is the vault database's opaque item blob (already encrypted under
/// the vault key); the sync layer transports it without interpreting it, so the
/// decryption key never leaves the Vault device layer.
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct VaultItemSyncDto {
    pub schema_version: VaultSchemaVersion,
    pub id: VaultItemId,
    pub revision: i64,
    pub provider: String,
    pub item_kind: String,
    pub envelope: String,
    pub created_at: String,
    pub updated_at: String,
    pub deleted_at: Option<String>,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct VaultPolicy {
    pub schema_version: VaultSchemaVersion,
    pub policy: VaultAgentPolicy,
    pub item_id: Option<VaultItemId>,
    pub origin: Option<CredentialOrigin>,
    pub expires_at: Option<DateTime<Utc>>,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct CredentialCapabilityGrant {
    pub schema_version: VaultSchemaVersion,
    pub handle: CredentialGrantHandle,
    pub session_id: VaultSessionId,
    pub task_id: VaultTaskId,
    pub profile_id: ProfileId,
    pub workspace_id: VaultWorkspaceId,
    pub tab_id: TabId,
    pub tab_generation: TabGeneration,
    pub top_origin: CredentialOrigin,
    pub frame_origin: CredentialOrigin,
    pub item_id: VaultItemId,
    pub item_alias: CredentialItemAlias,
    pub allowed_fields: Vec<CredentialField>,
    pub policy: VaultAgentPolicy,
    pub issued_at: DateTime<Utc>,
    pub expires_at: DateTime<Utc>,
    pub max_uses: u32,
    pub used_count: u32,
    pub state: CredentialGrantState,
    pub revoked_at: Option<DateTime<Utc>>,
    pub revocation_reason: Option<CredentialRevocationReason>,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct CredentialGrantDto {
    pub schema_version: VaultSchemaVersion,
    pub handle: CredentialGrantHandle,
    pub item_alias: CredentialItemAlias,
    pub allowed_fields: Vec<CredentialField>,
    pub expires_at: DateTime<Utc>,
    pub remaining_uses: u32,
    pub state: CredentialGrantState,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct VaultAuditRecord {
    pub schema_version: VaultSchemaVersion,
    pub id: VaultAuditRecordId,
    pub timestamp: DateTime<Utc>,
    pub session_id: Option<VaultSessionId>,
    pub task_id: Option<VaultTaskId>,
    pub profile_id: ProfileId,
    pub workspace_id: VaultWorkspaceId,
    pub top_origin: Option<CredentialOrigin>,
    pub frame_origin: Option<CredentialOrigin>,
    pub item_id: Option<VaultItemId>,
    pub item_alias: Option<CredentialItemAlias>,
    pub operation: VaultAuditOperation,
    pub policy: Option<VaultAgentPolicy>,
    pub decision: VaultAuditDecision,
    pub reason: Option<VaultErrorCode>,
    pub device_name: String,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct VaultAuditPublicDto {
    pub schema_version: VaultSchemaVersion,
    pub id: VaultAuditRecordId,
    pub timestamp: DateTime<Utc>,
    pub task_id: Option<VaultTaskId>,
    pub origin: Option<CredentialOrigin>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub item_alias: Option<CredentialItemAlias>,
    pub operation: VaultAuditOperation,
    pub policy: Option<VaultAgentPolicy>,
    pub decision: VaultAuditDecision,
    pub reason: Option<VaultErrorCode>,
    pub device_name: String,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct VaultAuditPageRequest {
    pub schema_version: VaultSchemaVersion,
    pub cursor: Option<String>,
    pub limit: u32,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct VaultAuditPageDto {
    pub schema_version: VaultSchemaVersion,
    pub entries: Vec<VaultAuditPublicDto>,
    pub next_cursor: Option<String>,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct VaultProviderStatus {
    pub schema_version: VaultSchemaVersion,
    pub selected_provider: PasswordProviderKind,
    pub effective_provider: PasswordProviderKind,
    pub providers: Vec<PasswordProviderDescriptor>,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct VaultErrorDto {
    pub schema_version: VaultSchemaVersion,
    pub code: VaultErrorCode,
    pub message: String,
    pub retry_at: Option<DateTime<Utc>>,
}
