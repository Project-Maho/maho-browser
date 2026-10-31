use maho_types::vault::{VaultAuditOperation, VaultItemId, VaultItemPublicDto, VaultRevision};

use crate::maho_core::MahoCore;
use crate::vault_manager::{VaultCrudError, VaultLoginUpdate};

impl MahoCore {
    pub fn vault_update_login(
        &mut self,
        id: VaultItemId,
        expected_revision: VaultRevision,
        update: VaultLoginUpdate,
    ) -> Result<VaultItemPublicDto, VaultCrudError> {
        let repository = self
            .vault_mutation_repository(VaultAuditOperation::ItemUpdated)
            .map_err(storage_error)?;
        let result = self
            .vault_runtime
            .update_login(&repository, id, expected_revision, update);
        let outcome = self.record_vault_operation_result(VaultAuditOperation::ItemUpdated, result);
        self.push_vault_item_sync_after(&outcome);
        outcome
    }

    pub fn vault_delete_item(
        &mut self,
        id: VaultItemId,
        expected_revision: VaultRevision,
    ) -> Result<VaultItemPublicDto, VaultCrudError> {
        let repository = self
            .vault_mutation_repository(VaultAuditOperation::ItemDeleted)
            .map_err(storage_error)?;
        let result = self
            .vault_runtime
            .delete_item(&repository, id, expected_revision);
        let outcome = self.record_vault_operation_result(VaultAuditOperation::ItemDeleted, result);
        self.push_vault_item_sync_after(&outcome);
        outcome
    }
}

fn storage_error(error: maho_storage::StorageError) -> VaultCrudError {
    VaultCrudError::Storage(error.to_string())
}
