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
        let repository = self.vault_repository().map_err(storage_error)?;
        let result = self
            .vault_runtime
            .update_login(&repository, id, expected_revision, update);
        self.record_vault_operation_result(VaultAuditOperation::ItemUpdated, result)
    }

    pub fn vault_delete_item(
        &mut self,
        id: VaultItemId,
        expected_revision: VaultRevision,
    ) -> Result<VaultItemPublicDto, VaultCrudError> {
        let repository = self.vault_repository().map_err(storage_error)?;
        let result = self
            .vault_runtime
            .delete_item(&repository, id, expected_revision);
        self.record_vault_operation_result(VaultAuditOperation::ItemDeleted, result)
    }
}

fn storage_error(error: maho_storage::StorageError) -> VaultCrudError {
    VaultCrudError::Storage(error.to_string())
}
