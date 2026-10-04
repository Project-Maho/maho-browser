use std::str::FromStr;

use chrono::{DateTime, Utc};
use maho_storage::sqlite::{EncryptedVaultItemRow, VaultTombstoneCas};
use maho_types::passwords::PasswordProviderKind;
use maho_types::vault::{
    TotpAlgorithm, VaultItemId, VaultItemKind, VaultItemPublicDto, VaultItemPublicMetadata,
    VaultRevision, VaultSchemaVersion,
};
use zeroize::Zeroizing;

use crate::vault_manager::record::{
    canonical_origins, decode_envelope, encode_envelope, mask_username, provider_token, public_dto,
    serialize_record, LiveSecret, RecordSecret, VaultRecordPayload,
};
use crate::vault_manager::{
    VaultCredentialFormDetails, VaultCrudError, VaultLoginUpdate, VaultManager,
};
use crate::vault_runtime::repository::VaultRepository;

use super::{VaultRuntime, VaultSecretResult};

struct VaultSecretUseRequest {
    id: VaultItemId,
    kind: VaultItemKind,
    expected_revision: Option<VaultRevision>,
}

impl VaultSecretUseRequest {
    const fn new(id: VaultItemId, kind: VaultItemKind) -> Self {
        Self {
            id,
            kind,
            expected_revision: None,
        }
    }

    const fn at_revision(
        id: VaultItemId,
        kind: VaultItemKind,
        expected_revision: VaultRevision,
    ) -> Self {
        Self {
            id,
            kind,
            expected_revision: Some(expected_revision),
        }
    }
}

impl VaultRuntime {
    pub(crate) fn update_login(
        &self,
        repository: &VaultRepository<'_>,
        id: VaultItemId,
        expected_revision: VaultRevision,
        update: VaultLoginUpdate,
    ) -> Result<VaultItemPublicDto, VaultCrudError> {
        self.with_manager(|manager| {
            let (row, record) = active_item(manager, repository, &id)?;
            ensure_revision(&row, expected_revision)?;
            if record.metadata.item_kind != VaultItemKind::Login {
                return Err(VaultCrudError::ItemKindMismatch);
            }
            let username = update.username.trim().to_string();
            let mut metadata = update.metadata;
            metadata.favorite = record.metadata.favorite;
            metadata.trashed_at = record.metadata.trashed_at;
            metadata.totp = record.metadata.totp.clone();
            metadata.item_kind = VaultItemKind::Login;
            metadata = canonicalize_metadata(metadata, Some(&username))?;
            // `None` preserves the stored form detail (an update that carries no
            // browser-form context, e.g. a settings-originated rename, must not
            // silently drop the fields the store round-trip depends on).
            let mut form_details = match update.form_details {
                Some(form_details) => Some(form_details),
                Option::None => record.form_details.clone(),
            };
            if let Some(notes) = update.notes {
                let details = form_details.get_or_insert_with(VaultCredentialFormDetails::default);
                details.notes.clear();
                if !notes.is_empty() {
                    // `VaultCredentialNote` is `ZeroizeOnDrop`, so struct-update
                    // syntax (`..Default::default()`) cannot move out of it.
                    let mut note = crate::vault_manager::VaultCredentialNote::default();
                    note.value = notes.as_str().to_string();
                    // Chromium time is microsecond-precision; Linux clocks
                    // report nanoseconds, which the serializer rejects.
                    note.date_created =
                        DateTime::<Utc>::from_timestamp_micros(Utc::now().timestamp_micros());
                    details.notes.push(note);
                }
            }
            reject_duplicate(
                manager,
                repository,
                &metadata,
                &username,
                form_details.as_ref(),
                id,
            )?;
            let secret = match update.password {
                Some(password) => RecordSecret::Login {
                    password: password.as_str().to_string(),
                },
                Option::None => match &record.secret {
                    RecordSecret::Login { password } => RecordSecret::Login {
                        password: password.clone(),
                    },
                    _ => return Err(VaultCrudError::ItemKindMismatch),
                },
            };
            let mut updated = VaultRecordPayload {
                schema_version: VaultSchemaVersion::CURRENT,
                metadata,
                username: Some(username),
                secret,
                last_used_at: record.last_used_at,
                form_details,
                totp_seed: record.totp_seed.clone(),
            };
            updated.metadata.has_notes = updated.has_notes();
            let updated_row = encrypt_updated_row(manager, &row, &id, &updated)?;
            let dto = public_dto(&updated_row, &updated)?;
            persist_update(manager, repository, &updated_row, row.revision, &id)?;
            Ok(dto)
        })
        .map_err(VaultCrudError::from)?
    }

    pub(crate) fn delete_item(
        &self,
        repository: &VaultRepository<'_>,
        id: VaultItemId,
        expected_revision: VaultRevision,
    ) -> Result<VaultItemPublicDto, VaultCrudError> {
        self.with_manager(|manager| {
            let (row, record) = active_item(manager, repository, &id)?;
            ensure_revision(&row, expected_revision)?;
            let tombstone = VaultRecordPayload {
                schema_version: VaultSchemaVersion::CURRENT,
                metadata: VaultItemPublicMetadata {
                    favorite: false,
                    trashed_at: None,
                    has_notes: false,
                    title: String::new(),
                    origins: Vec::new(),
                    username_hint: String::new(),
                    item_kind: record.metadata.item_kind,
                    totp: None,
                    passkey: None,
                },
                username: None,
                secret: RecordSecret::Tombstone,
                last_used_at: None,
                form_details: None,
                totp_seed: None,
            };
            let envelope = manager
                .encrypt_and_wrap(&serialize_record(&tombstone)?, &id)
                .map_err(VaultCrudError::from)?;
            let envelope = encode_envelope(&envelope)?;
            let timestamp = Utc::now().to_rfc3339();
            let new_revision = row.revision + 1;
            let mut deleted = row.clone();
            deleted.revision = new_revision;
            deleted.updated_at = timestamp.clone();
            let dto = public_dto(&deleted, &record)?;
            match repository
                .tombstone_item_cas(&VaultTombstoneCas {
                    id: &row.id,
                    expected_revision: row.revision,
                    new_revision,
                    envelope: &envelope,
                    updated_at: &timestamp,
                    deleted_at: &timestamp,
                })
                .map_err(storage_error)?
            {
                Some(count) => {
                    manager.set_item_count(count);
                    Ok(dto)
                }
                None => Err(classify_cas_failure(repository, &id, row.revision)),
            }
        })
        .map_err(VaultCrudError::from)?
    }

    pub(crate) fn use_login_password(
        &self,
        repository: &VaultRepository<'_>,
        id: VaultItemId,
    ) -> Result<Zeroizing<String>, VaultCrudError> {
        match self.use_secret(
            repository,
            VaultSecretUseRequest::new(id, VaultItemKind::Login),
        )? {
            VaultSecretResult::LoginPassword(secret) => Ok(secret),
            _ => Err(VaultCrudError::FieldMismatch),
        }
    }

    pub(crate) fn use_login_password_at_revision(
        &self,
        repository: &VaultRepository<'_>,
        id: VaultItemId,
        expected_revision: VaultRevision,
    ) -> Result<Zeroizing<String>, VaultCrudError> {
        match self.use_secret(
            repository,
            VaultSecretUseRequest::at_revision(id, VaultItemKind::Login, expected_revision),
        )? {
            VaultSecretResult::LoginPassword(secret) => Ok(secret),
            _ => Err(VaultCrudError::FieldMismatch),
        }
    }

    pub(crate) fn use_totp_seed(
        &self,
        repository: &VaultRepository<'_>,
        id: VaultItemId,
    ) -> Result<Zeroizing<Vec<u8>>, VaultCrudError> {
        match self.use_secret(
            repository,
            VaultSecretUseRequest::new(id, VaultItemKind::Totp),
        )? {
            VaultSecretResult::Totp { seed, .. } => Ok(seed),
            _ => Err(VaultCrudError::FieldMismatch),
        }
    }

    pub(crate) fn use_passkey_key(
        &self,
        repository: &VaultRepository<'_>,
        id: VaultItemId,
    ) -> Result<Zeroizing<Vec<u8>>, VaultCrudError> {
        match self.use_secret(
            repository,
            VaultSecretUseRequest::new(id, VaultItemKind::Passkey),
        )? {
            VaultSecretResult::PasskeyPrivateKey(secret) => Ok(secret),
            _ => Err(VaultCrudError::FieldMismatch),
        }
    }

    pub(crate) fn use_secure_bytes(
        &self,
        repository: &VaultRepository<'_>,
        id: VaultItemId,
    ) -> Result<Zeroizing<Vec<u8>>, VaultCrudError> {
        match self.use_secret(
            repository,
            VaultSecretUseRequest::new(id, VaultItemKind::SecureItem),
        )? {
            VaultSecretResult::SecureBytes(secret) => Ok(secret),
            _ => Err(VaultCrudError::FieldMismatch),
        }
    }

    pub(crate) fn generate_totp_code(
        &self,
        repository: &VaultRepository<'_>,
        id: VaultItemId,
        timestamp_secs: u64,
    ) -> Result<Zeroizing<String>, VaultCrudError> {
        self.with_manager(|manager| {
            let (row, mut record) = active_item(manager, repository, &id)?;
            if record.metadata.trashed_at.is_some() {
                return Err(VaultCrudError::ItemNotFound);
            }
            let seed = match &record.secret {
                RecordSecret::Totp { seed } => seed.as_slice(),
                RecordSecret::Login { .. } => record.totp_seed.as_deref().ok_or(VaultCrudError::FieldMismatch)?,
                _ => return Err(VaultCrudError::ItemKindMismatch),
            };
            let metadata = record.metadata.totp.as_ref().ok_or(VaultCrudError::MalformedPayload)?;
            let period = u64::from(metadata.period_seconds.value());
            if period == 0 { return Err(VaultCrudError::MalformedPayload); }
            let code = Zeroizing::new(generate_totp(seed, timestamp_secs, metadata.algorithm,
                period, u32::from(u8::from(metadata.digits))));
            record.last_used_at = Some(Utc::now());
            let updated = encrypt_updated_row(manager, &row, &id, &record)?;
            persist_update(manager, repository, &updated, row.revision, &id)?;
            Ok(code)
        }).map_err(VaultCrudError::from)?
    }

    fn use_secret(
        &self,
        repository: &VaultRepository<'_>,
        request: VaultSecretUseRequest,
    ) -> Result<VaultSecretResult, VaultCrudError> {
        self.with_manager(|manager| {
            let (row, mut record) = active_item(manager, repository, &request.id)?;
            if record.metadata.trashed_at.is_some() {
                return Err(VaultCrudError::ItemNotFound);
            }
            if let Some(expected_revision) = request.expected_revision {
                ensure_revision(&row, expected_revision)?;
            }
            if record.metadata.item_kind != request.kind {
                return Err(VaultCrudError::ItemKindMismatch);
            }
            let secret = secret_result_from_record(&record, request.kind)?;
            record.last_used_at = Some(Utc::now());
            let updated_row = encrypt_updated_row(manager, &row, &request.id, &record)?;
            persist_update(manager, repository, &updated_row, row.revision, &request.id)?;
            Ok(secret)
        })
        .map_err(VaultCrudError::from)?
    }
}

fn secret_result_from_record(
    record: &VaultRecordPayload,
    kind: VaultItemKind,
) -> Result<VaultSecretResult, VaultCrudError> {
    match (LiveSecret::from_record(&record.secret, kind)?, kind) {
        (LiveSecret::LoginPassword(secret), VaultItemKind::Login) => {
            Ok(VaultSecretResult::LoginPassword(secret))
        }
        (LiveSecret::TotpSeed(seed), VaultItemKind::Totp) => {
            let metadata = record
                .metadata
                .totp
                .as_ref()
                .ok_or(VaultCrudError::MalformedPayload)?;
            let period_seconds = u64::from(metadata.period_seconds.value());
            if period_seconds == 0 {
                return Err(VaultCrudError::MalformedPayload);
            }
            Ok(VaultSecretResult::Totp {
                seed,
                algorithm: metadata.algorithm,
                digits: u32::from(u8::from(metadata.digits)),
                period_seconds,
            })
        }
        (LiveSecret::PasskeyPrivateKey(secret), VaultItemKind::Passkey) => {
            Ok(VaultSecretResult::PasskeyPrivateKey(secret))
        }
        (LiveSecret::SecureBytes(secret), VaultItemKind::SecureItem) => {
            Ok(VaultSecretResult::SecureBytes(secret))
        }
        _ => Err(VaultCrudError::FieldMismatch),
    }
}

pub(super) fn active_item(
    manager: &VaultManager,
    repository: &VaultRepository<'_>,
    id: &VaultItemId,
) -> Result<(EncryptedVaultItemRow, VaultRecordPayload), VaultCrudError> {
    manager.active_key().map_err(VaultCrudError::from)?;
    let row = repository
        .get_item(&id.to_string())
        .map_err(storage_error)?
        .ok_or(VaultCrudError::ItemNotFound)?;
    if row.deleted_at.is_some() {
        return Err(VaultCrudError::ItemNotFound);
    }
    let stored_id = VaultItemId::from_str(&row.id).map_err(|_| VaultCrudError::MalformedPayload)?;
    let envelope = decode_envelope(&row.envelope)?;
    let plaintext = manager
        .decrypt_record(&envelope, &stored_id)
        .map_err(VaultCrudError::from)?;
    Ok((
        row,
        crate::vault_manager::record::deserialize_record(&plaintext)?,
    ))
}

pub(super) fn ensure_revision(
    row: &EncryptedVaultItemRow,
    expected: VaultRevision,
) -> Result<(), VaultCrudError> {
    let actual = u64::try_from(row.revision).unwrap_or(0);
    if actual == expected.value() {
        Ok(())
    } else {
        Err(VaultCrudError::RevisionConflict {
            expected: expected.value(),
            actual,
        })
    }
}

fn canonicalize_metadata(
    mut metadata: VaultItemPublicMetadata,
    username: Option<&str>,
) -> Result<VaultItemPublicMetadata, VaultCrudError> {
    metadata.origins = canonical_origins(&metadata.origins)?;
    if let Some(username) = username {
        metadata.username_hint = mask_username(username);
    }
    Ok(metadata)
}

fn reject_duplicate(
    manager: &VaultManager,
    repository: &VaultRepository<'_>,
    metadata: &VaultItemPublicMetadata,
    username: &str,
    form_details: Option<&VaultCredentialFormDetails>,
    excluded: VaultItemId,
) -> Result<(), VaultCrudError> {
    let provider = provider_token(PasswordProviderKind::MahoNative);
    for (row, record) in super::crud_query::active_records(manager, repository)? {
        let id = VaultItemId::from_str(&row.id).map_err(|_| VaultCrudError::MalformedPayload)?;
        if id == excluded
            || row.provider != provider
            || record.metadata.item_kind != VaultItemKind::Login
        {
            continue;
        }
        if super::crud_create::has_same_identity(&record, &metadata.origins, username, form_details)
        {
            return Err(VaultCrudError::Duplicate { existing: id });
        }
    }
    Ok(())
}

pub(super) fn encrypt_updated_row(
    manager: &VaultManager,
    row: &EncryptedVaultItemRow,
    id: &VaultItemId,
    record: &VaultRecordPayload,
) -> Result<EncryptedVaultItemRow, VaultCrudError> {
    let envelope = manager
        .encrypt_and_wrap(&serialize_record(record)?, id)
        .map_err(VaultCrudError::from)?;
    let mut updated = row.clone();
    updated.revision = row.revision + 1;
    updated.updated_at = Utc::now().to_rfc3339();
    updated.envelope = encode_envelope(&envelope)?;
    Ok(updated)
}

pub(super) fn persist_update(
    manager: &mut VaultManager,
    repository: &VaultRepository<'_>,
    row: &EncryptedVaultItemRow,
    expected: i64,
    id: &VaultItemId,
) -> Result<(), VaultCrudError> {
    match repository
        .update_item_cas(row, expected)
        .map_err(storage_error)?
    {
        Some(count) => {
            manager.set_item_count(count);
            Ok(())
        }
        None => Err(classify_cas_failure(repository, id, expected)),
    }
}

fn classify_cas_failure(
    repository: &VaultRepository<'_>,
    id: &VaultItemId,
    expected: i64,
) -> VaultCrudError {
    match repository.get_item(&id.to_string()).map_err(storage_error) {
        Ok(Some(row)) if row.deleted_at.is_none() => VaultCrudError::RevisionConflict {
            expected: u64::try_from(expected).unwrap_or(0),
            actual: u64::try_from(row.revision).unwrap_or(0),
        },
        Ok(_) => VaultCrudError::ItemNotFound,
        Err(error) => error,
    }
}

fn storage_error(error: maho_storage::StorageError) -> VaultCrudError {
    VaultCrudError::Storage(error.to_string())
}

pub(super) fn generate_totp(
    seed: &[u8],
    timestamp_secs: u64,
    algorithm: TotpAlgorithm,
    period: u64,
    digits: u32,
) -> String {
    use hmac::{Hmac, Mac};
    use sha1::Sha1;
    use sha2::{Sha256, Sha512};

    let time_bytes = (timestamp_secs / period).to_be_bytes();
    let digest = match algorithm {
        TotpAlgorithm::Sha1 => Hmac::<Sha1>::new_from_slice(seed).map(|mut mac| {
            mac.update(&time_bytes);
            mac.finalize().into_bytes().to_vec()
        }),
        TotpAlgorithm::Sha256 => Hmac::<Sha256>::new_from_slice(seed).map(|mut mac| {
            mac.update(&time_bytes);
            mac.finalize().into_bytes().to_vec()
        }),
        TotpAlgorithm::Sha512 => Hmac::<Sha512>::new_from_slice(seed).map(|mut mac| {
            mac.update(&time_bytes);
            mac.finalize().into_bytes().to_vec()
        }),
        _ => return String::new(),
    };
    let Ok(digest) = digest else {
        return String::new();
    };
    let offset = usize::from(digest[digest.len() - 1] & 0x0f);
    if offset + 4 > digest.len() {
        return String::new();
    }
    let value = (u32::from(digest[offset] & 0x7f) << 24)
        | (u32::from(digest[offset + 1]) << 16)
        | (u32::from(digest[offset + 2]) << 8)
        | u32::from(digest[offset + 3]);
    format!(
        "{:0>width$}",
        value % 10u32.pow(digits),
        width = digits as usize
    )
}
