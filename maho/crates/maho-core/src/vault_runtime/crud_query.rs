use std::str::FromStr;

use maho_storage::sqlite::EncryptedVaultItemRow;
use maho_types::vault::{
    VaultItemId, VaultItemListRequest, VaultItemPublicDto, VaultItemSearchRequest,
};

use crate::vault_manager::origin::{canonicalize_origin, origin_matches};
use crate::vault_manager::record::{deserialize_record, public_dto, VaultRecordPayload};
use crate::vault_manager::{OriginMatchPolicy, VaultCrudError, VaultManager};
use crate::vault_runtime::repository::VaultRepository;

use super::VaultRuntime;

impl VaultRuntime {
    pub(crate) fn list_items(
        &self,
        repository: VaultRepository<'_>,
        request: &VaultItemListRequest,
    ) -> Result<Vec<VaultItemPublicDto>, VaultCrudError> {
        self.with_manager(|manager| {
            let mut items = retained_records(manager, &repository)?
                .iter()
                .map(|(row, record)| public_dto(row, record))
                .collect::<Result<Vec<_>, _>>()?;
            items.retain(|item| match request.trash {
                maho_types::vault::VaultTrashFilter::Exclude => item.trashed_at.is_none(),
                maho_types::vault::VaultTrashFilter::Only => item.trashed_at.is_some(),
            });
            if request.favorites_only {
                items.retain(|item| item.favorite);
            }
            if let Some(provider) = request.provider.as_ref() {
                items.retain(|item| &item.provider == provider);
            }
            if !request.kinds.is_empty() {
                items.retain(|item| request.kinds.contains(&item.item_kind));
            }
            if let Some(cursor) = &request.cursor {
                items.retain(|item| &list_cursor_token(item) > cursor);
            }
            if request.limit > 0 {
                items.truncate(request.limit as usize);
            }
            Ok(items)
        })
        .map_err(VaultCrudError::from)?
    }

    pub(crate) fn search_items(
        &self,
        repository: VaultRepository<'_>,
        request: &VaultItemSearchRequest,
    ) -> Result<Vec<VaultItemPublicDto>, VaultCrudError> {
        self.match_items(repository, request, OriginMatchPolicy::Exact)
    }

    pub(crate) fn match_items(
        &self,
        repository: VaultRepository<'_>,
        request: &VaultItemSearchRequest,
        policy: OriginMatchPolicy,
    ) -> Result<Vec<VaultItemPublicDto>, VaultCrudError> {
        self.with_manager(|manager| {
            let target = canonicalize_origin(request.origin.as_str())?;
            let mut items = active_dtos(manager, &repository)?;
            if let Some(provider) = request.provider.as_ref() {
                items.retain(|item| &item.provider == provider);
            }
            if !request.kinds.is_empty() {
                items.retain(|item| request.kinds.contains(&item.item_kind));
            }
            items.retain(|item| {
                item.origins
                    .iter()
                    .any(|stored| origin_matches(stored, &target, policy))
            });
            Ok(items)
        })
        .map_err(VaultCrudError::from)?
    }
}

fn active_dtos(
    manager: &VaultManager,
    repository: &VaultRepository<'_>,
) -> Result<Vec<VaultItemPublicDto>, VaultCrudError> {
    active_records(manager, repository)?
        .iter()
        .map(|(row, record)| public_dto(row, record))
        .collect()
}

pub(super) fn active_records(
    manager: &VaultManager,
    repository: &VaultRepository<'_>,
) -> Result<Vec<(EncryptedVaultItemRow, VaultRecordPayload)>, VaultCrudError> {
    Ok(retained_records(manager, repository)?
        .into_iter()
        .filter(|(_, record)| record.metadata.trashed_at.is_none())
        .collect())
}

pub(super) fn retained_records(
    manager: &VaultManager,
    repository: &VaultRepository<'_>,
) -> Result<Vec<(EncryptedVaultItemRow, VaultRecordPayload)>, VaultCrudError> {
    manager.active_key().map_err(VaultCrudError::from)?;
    repository
        .list_items()
        .map_err(|error| VaultCrudError::Storage(error.to_string()))?
        .into_iter()
        .filter(|row| row.deleted_at.is_none())
        .map(|row| decrypt_active_record(manager, row))
        .collect()
}

fn decrypt_active_record(
    manager: &VaultManager,
    row: EncryptedVaultItemRow,
) -> Result<(EncryptedVaultItemRow, VaultRecordPayload), VaultCrudError> {
    let id = VaultItemId::from_str(&row.id).map_err(|_| VaultCrudError::MalformedPayload)?;
    let envelope = crate::vault_manager::record::decode_envelope(&row.envelope)?;
    let plaintext = manager
        .decrypt_record(&envelope, &id)
        .map_err(VaultCrudError::from)?;
    Ok((row, deserialize_record(&plaintext)?))
}

fn list_cursor_token(item: &VaultItemPublicDto) -> String {
    format!("{}|{}", item.created_at.to_rfc3339(), item.id)
}
