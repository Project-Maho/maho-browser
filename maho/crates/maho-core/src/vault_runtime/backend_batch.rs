use std::str::FromStr;

use chrono::{DateTime, Utc};
use maho_types::vault::{
    VaultBatchReadRequest, VaultBatchReadResult, VaultItemCreatedRange, VaultItemId, VaultItemKind,
    VaultItemPublicMetadata, VaultRangeDeleteResult, VaultSchemaVersion,
};

use crate::vault_manager::origin::{canonicalize_origin, origin_matches};
use crate::vault_manager::record::{
    decode_envelope, deserialize_record, encode_envelope, public_dto, serialize_record,
    RecordSecret, VaultRecordPayload,
};
use crate::vault_manager::{VaultCredentialFormDetails, VaultCrudError};
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
                if record.metadata.trashed_at.is_some() {
                    missing_ids.push(*id);
                    continue;
                }
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
            let remaining = repository
                .tombstone_items(&candidates)
                .map_err(|error| VaultCrudError::Storage(error.to_string()))?;
            manager.set_item_count(remaining);
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
        scheme: Option<u8>,
    ) -> Result<Vec<VaultBackendLoginCredential>, VaultCrudError> {
        self.with_manager(|manager| {
            let requested = match origin {
                Some(origin) if origin.trim().is_empty() => {
                    manager.active_key().map_err(VaultCrudError::from)?;
                    return Ok(Vec::new());
                }
                requested => requested,
            };
            let android_realm = requested.filter(|origin| origin.starts_with("android://"));
            if android_realm.is_some_and(|realm| !is_valid_android_realm(realm)) {
                manager.active_key().map_err(VaultCrudError::from)?;
                return Ok(Vec::new());
            }
            let records = super::crud_query::active_records(manager, &repository)?;
            // An explicit request scheme decides the lookup kind: Basic and
            // Digest queries match the stored HTTP-auth realm, while an HTML
            // query resolves by web origin even when a `Basic realm=""` item
            // shares the same signon realm. Discovery without a scheme keeps
            // the stored-realm inference: a stored Basic/Digest realm
            // identifies an HTTP-auth query; its suffix is opaque, not a URL
            // path.
            let http_auth_realm = match (scheme, requested) {
                (Some(1..=2), requested) => requested,
                (Some(_), _) => None,
                (None, requested) => requested.filter(|realm| {
                    records.iter().any(|(_, record)| {
                        record.metadata.item_kind == VaultItemKind::Login
                            && record.form_details.as_ref().is_some_and(|details| {
                                matches!(details.scheme, 1 | 2) && details.signon_realm == *realm
                            })
                    })
                }),
            };
            let target = match requested {
                Some(origin) if android_realm.is_none() && http_auth_realm.is_none() => {
                    Some(canonicalize_origin(origin)?)
                }
                _ => None,
            };
            let mut credentials = Vec::new();
            for (row, record) in records {
                if record.metadata.item_kind != VaultItemKind::Login {
                    continue;
                }
                if let Some(realm) = android_realm.or(http_auth_realm) {
                    let matches_realm = record.form_details.as_ref().is_some_and(|details| {
                        details.signon_realm == realm
                            && (android_realm.is_some() || matches!(details.scheme, 1 | 2))
                    });
                    if !matches_realm {
                        continue;
                    }
                } else if let Some(target) = &target {
                    // HTTP-auth credentials must never match by web origin alone.
                    if record
                        .form_details
                        .as_ref()
                        .is_some_and(|details| matches!(details.scheme, 1 | 2))
                    {
                        continue;
                    }
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
                credentials.push(credential_from_record(
                    public,
                    record.username.clone(),
                    record.form_details.clone(),
                ));
            }
            Ok(credentials)
        })
        .map_err(VaultCrudError::from)?
    }
}

fn is_valid_android_realm(realm: &str) -> bool {
    let Some(authority) = realm.strip_prefix("android://") else {
        return false;
    };
    // Chromium accepts one root slash, but realm identity remains unmodified.
    let authority = authority.strip_suffix('/').unwrap_or(authority);
    if authority.is_empty() || authority.contains(['/', '?', '#']) {
        return false;
    }
    let Some((certificate_hash, package_name)) = authority.split_once('@') else {
        return false;
    };
    if certificate_hash.is_empty() || package_name.is_empty() || package_name.contains('@') {
        return false;
    }

    let first_padding = certificate_hash.find('=').unwrap_or(certificate_hash.len());
    let (hash, padding) = certificate_hash.split_at(first_padding);
    !hash.is_empty()
        && certificate_hash.len() % 4 == 0
        && hash
            .chars()
            .all(|character| character.is_ascii_alphanumeric() || matches!(character, '-' | '_'))
        && padding.len() <= 2
        && padding.chars().all(|character| character == '=')
        && package_name
            .chars()
            .all(|character| character.is_ascii_alphanumeric() || matches!(character, '.' | '_'))
}

fn credential_from_record(
    public: maho_types::vault::VaultItemPublicDto,
    username: Option<String>,
    form_details: Option<VaultCredentialFormDetails>,
) -> VaultBackendLoginCredential {
    // The stored `signonRealm` is authoritative when present: it can express
    // realms a canonical origin cannot (android://, HTTP-auth realms). Legacy
    // records without form detail fall back to the first origin, then the title,
    // exactly as before.
    let signon_realm = form_details
        .as_ref()
        .map(|details| details.signon_realm.clone())
        .filter(|realm| !realm.is_empty())
        .or_else(|| {
            public
                .origins
                .first()
                .map(|origin| origin.as_str().to_string())
        })
        .unwrap_or_else(|| public.title.clone());
    VaultBackendLoginCredential {
        schema_version: public.schema_version,
        item_id: public.id,
        observed_revision: public.revision,
        signon_realm,
        username: username.unwrap_or_default(),
        created_at: public.created_at,
        updated_at: public.updated_at,
        last_used_at: public.last_used_at,
        form_details,
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

pub(super) fn tombstone_candidate(
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
    let new_revision = next_tombstone_revision(row.revision)?;
    Ok(VaultTombstoneCandidate {
        id: row.id,
        expected_revision: row.revision,
        new_revision,
        envelope: encode_envelope(&envelope)?,
        timestamp: timestamp.to_string(),
    })
}

fn next_tombstone_revision(revision: i64) -> Result<i64, VaultCrudError> {
    revision
        .checked_add(1)
        .ok_or_else(|| VaultCrudError::Storage("range tombstone revision overflow".to_string()))
}

#[cfg(test)]
mod tests {
    use super::next_tombstone_revision;
    use crate::vault_manager::VaultCrudError;

    #[test]
    fn tombstone_revision_overflow_fails_closed() {
        assert!(matches!(
            next_tombstone_revision(i64::MAX),
            Err(VaultCrudError::Storage(message)) if message == "range tombstone revision overflow"
        ));
    }
}
