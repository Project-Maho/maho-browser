//! Cross-device sync transport for Vault items: opaque-envelope records in,
//! opaque-envelope rows out. Nothing here decrypts an item or touches key
//! material; the vault key layer stays the only place secrets exist.

use base64::Engine as _;
use maho_storage::sqlite::EncryptedVaultItemRow;
use maho_types::vault::{VaultItemId, VaultItemSyncDto, VaultSchemaVersion};

use crate::maho_core::MahoCore;
use crate::sync_models::SyncEntityType;

impl MahoCore {
    /// Pushes one Vault item's ciphertext-only sync record after a local
    /// mutation. Best-effort: a missing row or an unparseable id skips the push
    /// instead of failing the mutation that already committed.
    pub(crate) fn push_vault_item_sync(&mut self, id: &str) {
        let Some((payload, deleted)) = self.vault_item_sync_payload(id) else {
            return;
        };
        self.push_sync_entity(SyncEntityType::VaultItem, id.to_string(), payload, deleted);
    }

    /// Pushes the item after a successful mutation; failures leave the outcome
    /// untouched because the mutation itself already committed.
    pub(crate) fn push_vault_item_sync_after(
        &mut self,
        outcome: &Result<
            maho_types::vault::VaultItemPublicDto,
            crate::vault_manager::VaultCrudError,
        >,
    ) {
        if let Ok(item) = outcome {
            self.push_vault_item_sync(&item.id.to_string());
        }
    }

    fn vault_item_sync_payload(&self, id: &str) -> Option<(String, bool)> {
        let storage = self.storage.as_ref()?;
        let row = storage.get_vault_item(id).ok().flatten()?;
        let dto = vault_item_sync_dto_from_row(&row)?;
        let payload = serde_json::to_string(&dto).ok()?;
        Some((payload, row.deleted_at.is_some()))
    }
}

pub(crate) fn vault_item_sync_dto_from_row(row: &EncryptedVaultItemRow) -> Option<VaultItemSyncDto> {
    let id = uuid::Uuid::parse_str(&row.id).ok()?;
    Some(VaultItemSyncDto {
        schema_version: VaultSchemaVersion::CURRENT,
        id: VaultItemId::from_uuid(id),
        revision: row.revision,
        provider: row.provider.clone(),
        item_kind: row.item_kind.clone(),
        envelope: base64::engine::general_purpose::STANDARD.encode(&row.envelope),
        created_at: row.created_at.clone(),
        updated_at: row.updated_at.clone(),
        deleted_at: row.deleted_at.clone(),
    })
}

pub(crate) fn vault_item_storage_row_from_dto(
    dto: &VaultItemSyncDto,
) -> Option<EncryptedVaultItemRow> {
    let envelope = base64::engine::general_purpose::STANDARD
        .decode(dto.envelope.as_bytes())
        .ok()?;
    Some(EncryptedVaultItemRow {
        id: dto.id.to_string(),
        schema_version: i64::from(u16::from(dto.schema_version)),
        revision: dto.revision,
        provider: dto.provider.clone(),
        item_kind: dto.item_kind.clone(),
        envelope,
        created_at: dto.created_at.clone(),
        updated_at: dto.updated_at.clone(),
        deleted_at: dto.deleted_at.clone(),
    })
}
