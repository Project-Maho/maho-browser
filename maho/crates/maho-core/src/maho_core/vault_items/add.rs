use maho_types::vault::{VaultAuditOperation, VaultItemPublicDto};

use crate::maho_core::MahoCore;
use crate::vault_manager::{
    VaultCrudError, VaultLoginInput, VaultPasskeyInput, VaultSecureItemInput, VaultTotpInput,
};

impl MahoCore {
    pub fn vault_add_login(
        &mut self,
        input: VaultLoginInput,
    ) -> Result<VaultItemPublicDto, VaultCrudError> {
        let result = self
            .vault_runtime
            .add_login(self.vault_repository().map_err(storage_error)?, input);
        self.record_vault_operation_result(VaultAuditOperation::ItemCreated, result)
    }

    pub fn vault_add_totp(
        &mut self,
        input: VaultTotpInput,
    ) -> Result<VaultItemPublicDto, VaultCrudError> {
        let result = self
            .vault_runtime
            .add_totp(self.vault_repository().map_err(storage_error)?, input);
        self.record_vault_operation_result(VaultAuditOperation::ItemCreated, result)
    }

    pub fn vault_add_passkey(
        &mut self,
        input: VaultPasskeyInput,
    ) -> Result<VaultItemPublicDto, VaultCrudError> {
        let result = self
            .vault_runtime
            .add_passkey(self.vault_repository().map_err(storage_error)?, input);
        self.record_vault_operation_result(VaultAuditOperation::ItemCreated, result)
    }

    pub fn vault_add_secure_item(
        &mut self,
        input: VaultSecureItemInput,
    ) -> Result<VaultItemPublicDto, VaultCrudError> {
        let result = self
            .vault_runtime
            .add_secure_item(self.vault_repository().map_err(storage_error)?, input);
        self.record_vault_operation_result(VaultAuditOperation::ItemCreated, result)
    }
}

fn storage_error(error: maho_storage::StorageError) -> VaultCrudError {
    VaultCrudError::Storage(error.to_string())
}
