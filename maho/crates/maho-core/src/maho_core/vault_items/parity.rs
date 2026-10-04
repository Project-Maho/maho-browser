use chrono::Utc;
use maho_types::vault::{
    VaultAuditOperation, VaultHealthReport, VaultItemId, VaultItemKind, VaultItemPublicDto,
    VaultItemPublicMetadata, VaultRangeDeleteResult, VaultRevision, VaultTotpCode,
};
use zeroize::Zeroizing;

use crate::maho_core::MahoCore;
use crate::vault_manager::record::{RecordSecret, VaultRecordPayload};
use crate::vault_manager::{VaultCrudError, VaultSecureItemInput};

impl MahoCore {
    fn vault_edit_record(
        &mut self,
        id: VaultItemId,
        revision: VaultRevision,
        edit: impl FnOnce(&mut VaultRecordPayload) -> Result<(), VaultCrudError>,
    ) -> Result<VaultItemPublicDto, VaultCrudError> {
        let repository = self
            .vault_mutation_repository(VaultAuditOperation::ItemUpdated)
            .map_err(storage_error)?;
        let result = self
            .vault_runtime
            .mutate_record(&repository, id, revision, edit);
        let outcome = self.record_vault_operation_result(VaultAuditOperation::ItemUpdated, result);
        self.push_vault_item_sync_after(&outcome);
        outcome
    }

    pub fn vault_trash_item(
        &mut self,
        id: VaultItemId,
        revision: VaultRevision,
    ) -> Result<VaultItemPublicDto, VaultCrudError> {
        self.vault_edit_record(id, revision, |record| {
            record.metadata.trashed_at = Some(Utc::now());
            Ok(())
        })
    }

    pub fn vault_restore_item(
        &mut self,
        id: VaultItemId,
        revision: VaultRevision,
    ) -> Result<VaultItemPublicDto, VaultCrudError> {
        self.vault_edit_record(id, revision, |record| {
            record.metadata.trashed_at = None;
            Ok(())
        })
    }

    pub fn vault_set_favorite(
        &mut self,
        id: VaultItemId,
        revision: VaultRevision,
        favorite: bool,
    ) -> Result<VaultItemPublicDto, VaultCrudError> {
        self.vault_edit_record(id, revision, |record| {
            record.metadata.favorite = favorite;
            Ok(())
        })
    }

    pub fn vault_empty_trash(&mut self) -> Result<VaultRangeDeleteResult, VaultCrudError> {
        let repository = self
            .vault_mutation_repository(VaultAuditOperation::ItemDeleted)
            .map_err(storage_error)?;
        let result = self.vault_runtime.empty_trash(&repository);
        let outcome = self.record_vault_operation_result(VaultAuditOperation::ItemDeleted, result);
        if let Ok(result) = &outcome {
            for id in &result.tombstoned_ids {
                self.push_vault_item_sync(&id.to_string());
            }
        }
        outcome
    }

    pub fn vault_get_notes(
        &mut self,
        id: VaultItemId,
    ) -> Result<Zeroizing<String>, VaultCrudError> {
        let repository = self.vault_repository().map_err(storage_error)?;
        self.vault_runtime.get_notes(&repository, id)
    }

    pub fn vault_get_username(
        &mut self,
        id: VaultItemId,
    ) -> Result<Zeroizing<String>, VaultCrudError> {
        let repository = self.vault_repository().map_err(storage_error)?;
        self.vault_runtime.get_username(&repository, id)
    }

    pub fn vault_add_secure_note(
        &mut self,
        title: String,
        notes: Zeroizing<String>,
    ) -> Result<VaultItemPublicDto, VaultCrudError> {
        self.vault_add_secure_item(VaultSecureItemInput {
            metadata: VaultItemPublicMetadata {
                title,
                origins: Vec::new(),
                username_hint: String::new(),
                item_kind: VaultItemKind::SecureItem,
                totp: None,
                passkey: None,
                favorite: false,
                trashed_at: None,
                has_notes: !notes.is_empty(),
            },
            bytes: Zeroizing::new(Vec::new()),
            notes: Some(notes),
        })
    }

    pub fn vault_update_secure_note(
        &mut self,
        id: VaultItemId,
        revision: VaultRevision,
        title: String,
        notes: Zeroizing<String>,
    ) -> Result<VaultItemPublicDto, VaultCrudError> {
        self.vault_edit_record(id, revision, |record| match &mut record.secret {
            RecordSecret::SecureItem {
                bytes,
                notes: stored,
            } if bytes.is_empty() => {
                *stored = Some(notes.as_str().to_string());
                record.metadata.title = title;
                Ok(())
            }
            _ => Err(VaultCrudError::ItemKindMismatch),
        })
    }

    pub fn vault_set_login_totp(
        &mut self,
        id: VaultItemId,
        revision: VaultRevision,
        secret: &str,
    ) -> Result<VaultItemPublicDto, VaultCrudError> {
        let repository = self
            .vault_mutation_repository(VaultAuditOperation::ItemUpdated)
            .map_err(storage_error)?;
        let result = self
            .vault_runtime
            .set_login_totp(&repository, id, revision, secret);
        let outcome = self.record_vault_operation_result(VaultAuditOperation::ItemUpdated, result);
        self.push_vault_item_sync_after(&outcome);
        outcome
    }

    pub fn vault_totp_code(
        &mut self,
        id: VaultItemId,
        timestamp_secs: u64,
    ) -> Result<VaultTotpCode, VaultCrudError> {
        let repository = self.vault_repository().map_err(storage_error)?;
        self.vault_runtime
            .totp_code(&repository, id, timestamp_secs)
    }

    pub fn vault_health_report(&mut self) -> Result<VaultHealthReport, VaultCrudError> {
        let repository = self.vault_repository().map_err(storage_error)?;
        self.vault_runtime.health_report(&repository)
    }
}

fn storage_error(error: maho_storage::StorageError) -> VaultCrudError {
    VaultCrudError::Storage(error.to_string())
}
