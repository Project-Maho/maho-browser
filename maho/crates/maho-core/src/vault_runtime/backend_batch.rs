use std::str::FromStr;

use chrono::{DateTime, Utc};
use maho_types::vault::{
    VaultBatchReadRequest, VaultBatchReadResult, VaultItemCreatedRange, VaultItemId, VaultItemKind,
    VaultItemPublicMetadata, VaultRangeDeleteResult, VaultSchemaVersion,
};
use zeroize::Zeroizing;

use crate::vault_manager::origin::{canonicalize_origin, origin_matches};
use crate::vault_manager::record::{
    decode_envelope, deserialize_record, encode_envelope, public_dto, serialize_record, LiveSecret,
    RecordSecret, VaultRecordPayload,
};
use crate::vault_manager::VaultCrudError;
use crate::vault_runtime::repository::{VaultRepository, VaultTombstoneCandidate};
use crate::vault_runtime::session::VaultBackendLoginCredential;

use super::VaultRuntime;

impl VaultRuntime {
    pub(crate) fn batch_read(
        &self,
        repository: VaultRepository<'_>,
        request: &VaultBatchReadRequest,
    ) -> Result<VaultBatchReadResult, VaultCrudError> {
        self.with_manager(|manager| {
            manager.active_key().map_err(VaultCrudError::from)?;
            let mut items = Vec::with_capacity(request.item_ids.len());
            let mut missing_ids = Vec::new();

            for id in &request.item_ids {
                let row = repository
                    .get_item(&id.to_string())
                    .map_err(|error| VaultCrudError::Storage(error.to_string()))?;
                let Some(row) = row.filter(|row| row.deleted_at.is_none()) else {
                    missing_ids.push(*id);
                    continue;
                };
                let envelope = decode_envelope(&row.envelope)?;
                let plaintext = manager
                    .decrypt_record(&envelope, id)
                    .map_err(VaultCrudError::from)?;
                let record = deserialize_record(&plaintext)?;
                items.push(public_dto(&row, &record)?);
            }

            Ok(VaultBatchReadResult {
                schema_version: VaultSchemaVersion::CURRENT,
                items,
                missing_ids,
            })
        })
        .map_err(VaultCrudError::from)?
    }

    pub(crate) fn delete_created_range(
        &self,
        repository: VaultRepository<'_>,
        range: &VaultItemCreatedRange,
    ) -> Result<VaultRangeDeleteResult, VaultCrudError> {
        self.with_manager(|manager| {
            manager.active_key().map_err(VaultCrudError::from)?;
            let rows = repository
                .list_items()
                .map_err(|error| VaultCrudError::Storage(error.to_string()))?;
            let mut candidates = Vec::new();
            let timestamp = Utc::now().to_rfc3339();
            for row in rows {
                if row.deleted_at.is_none() && created_in_range(&row.created_at, range)? {
                    candidates.push(tombstone_candidate(manager, row, &timestamp)?);
                }
            }
            let tombstoned_ids = candidates
                .iter()
                .map(|candidate| VaultItemId::from_str(&candidate.id))
                .collect::<Result<Vec<VaultItemId>, _>>()
                .map_err(|_| VaultCrudError::MalformedPayload)?;
            let count = u64::try_from(tombstoned_ids.len()).map_err(|_| {
                VaultCrudError::Storage("range deletion count overflow".to_string())
            })?;
            let item_count = manager
                .status()
                .item_count
                .checked_sub(count)
                .ok_or_else(|| VaultCrudError::Storage("vault item count underflow".to_string()))?;
            repository
                .tombstone_items(&candidates)
                .map_err(|error| VaultCrudError::Storage(error.to_string()))?;
            manager.set_item_count(item_count);
            Ok(VaultRangeDeleteResult {
                schema_version: VaultSchemaVersion::CURRENT,
                count,
                tombstoned_ids,
            })
        })
        .map_err(VaultCrudError::from)?
    }

    pub(crate) fn login_credentials(
        &self,
        repository: VaultRepository<'_>,
        origin: Option<&str>,
    ) -> Result<Vec<VaultBackendLoginCredential>, VaultCrudError> {
        self.with_manager(|manager| {
            let target = match origin.filter(|origin| !origin.trim().is_empty()) {
                Some(origin) => Some(canonicalize_origin(origin)?),
                None => None,
            };
            let mut credentials = Vec::new();
            for (row, record) in super::crud_query::active_records(manager, &repository)? {
                if record.metadata.item_kind != VaultItemKind::Login {
                    continue;
                }
                if let Some(target) = &target {
                    let matches_origin = record.metadata.origins.iter().any(|stored| {
                        origin_matches(
                            stored,
                            target,
                            crate::vault_manager::OriginMatchPolicy::Exact,
                        )
                    });
                    if !matches_origin {
                        continue;
                    }
                }
                let public = public_dto(&row, &record)?;
                let LiveSecret::LoginPassword(password) =
                    LiveSecret::from_record(&record.secret, VaultItemKind::Login)?
                else {
                    return Err(VaultCrudError::FieldMismatch);
                };
                credentials.push(credential_from_record(
                    public,
                    record.username.clone(),
                    password,
                ));
            }
            Ok(credentials)
        })
        .map_err(VaultCrudError::from)?
    }
}

fn credential_from_record(
    public: maho_types::vault::VaultItemPublicDto,
    username: Option<String>,
    password: Zeroizing<String>,
) -> VaultBackendLoginCredential {
    let signon_realm = public
        .origins
        .first()
        .map(|origin| origin.as_str().to_string())
        .unwrap_or_else(|| public.title.clone());
    VaultBackendLoginCredential {
        schema_version: public.schema_version,
        id: public.id,
        revision: public.revision,
        signon_realm,
        username: username.unwrap_or_default(),
        password: password.to_string(),
        created_at: public.created_at,
        updated_at: public.updated_at,
        last_used_at: public.last_used_at,
    }
}

fn created_in_range(
    created_at: &str,
    range: &VaultItemCreatedRange,
) -> Result<bool, VaultCrudError> {
    let created_at =
        DateTime::parse_from_rfc3339(created_at).map_err(|_| VaultCrudError::MalformedPayload)?;
    let created_at = created_at.with_timezone(&Utc);
    Ok(range
        .from_inclusive
        .is_none_or(|from_inclusive| created_at >= from_inclusive)
        && range
            .until_exclusive
            .is_none_or(|until_exclusive| created_at < until_exclusive))
}

fn tombstone_candidate(
    manager: &crate::vault_manager::VaultManager,
    row: maho_storage::sqlite::EncryptedVaultItemRow,
    timestamp: &str,
) -> Result<VaultTombstoneCandidate, VaultCrudError> {
    let id = VaultItemId::from_str(&row.id).map_err(|_| VaultCrudError::MalformedPayload)?;
    let envelope = decode_envelope(&row.envelope)?;
    let plaintext = manager
        .decrypt_record(&envelope, &id)
        .map_err(VaultCrudError::from)?;
    let record = deserialize_record(&plaintext)?;
    let tombstone = VaultRecordPayload {
        schema_version: VaultSchemaVersion::CURRENT,
        metadata: VaultItemPublicMetadata {
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
    };
    let envelope = manager
        .encrypt_and_wrap(&serialize_record(&tombstone)?, &id)
        .map_err(VaultCrudError::from)?;
    Ok(VaultTombstoneCandidate {
        id: row.id,
        expected_revision: row.revision,
        new_revision: row.revision + 1,
        envelope: encode_envelope(&envelope)?,
        timestamp: timestamp.to_string(),
    })
}
