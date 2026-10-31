use maho_types::vault::{VaultAuditOperation, VaultItemId, VaultRevision};
use zeroize::Zeroizing;

use crate::maho_core::MahoCore;
use crate::vault_manager::VaultCrudError;

impl MahoCore {
    pub fn vault_use_login_password(
        &mut self,
        id: VaultItemId,
    ) -> Result<Zeroizing<String>, VaultCrudError> {
        let repository = self
            .vault_mutation_repository(VaultAuditOperation::Fill)
            .map_err(storage_error)?;
        let result = self.vault_runtime.use_login_password(&repository, id);
        self.record_vault_operation_result(VaultAuditOperation::Fill, result)
    }

    pub fn vault_use_login_password_at_revision(
        &mut self,
        id: VaultItemId,
        expected_revision: VaultRevision,
    ) -> Result<Zeroizing<String>, VaultCrudError> {
        let repository = self
            .vault_mutation_repository(VaultAuditOperation::Fill)
            .map_err(storage_error)?;
        let result =
            self.vault_runtime
                .use_login_password_at_revision(&repository, id, expected_revision);
        self.record_vault_operation_result(VaultAuditOperation::Fill, result)
    }

    pub fn vault_use_totp_seed(
        &mut self,
        id: VaultItemId,
    ) -> Result<Zeroizing<Vec<u8>>, VaultCrudError> {
        let repository = self
            .vault_mutation_repository(VaultAuditOperation::TotpFill)
            .map_err(storage_error)?;
        let result = self.vault_runtime.use_totp_seed(&repository, id);
        self.record_vault_operation_result(VaultAuditOperation::TotpFill, result)
    }

    pub fn vault_use_passkey_key(
        &mut self,
        id: VaultItemId,
    ) -> Result<Zeroizing<Vec<u8>>, VaultCrudError> {
        let repository = self
            .vault_mutation_repository(VaultAuditOperation::PasskeyUsed)
            .map_err(storage_error)?;
        let result = self.vault_runtime.use_passkey_key(&repository, id);
        self.record_vault_operation_result(VaultAuditOperation::PasskeyUsed, result)
    }

    pub fn vault_use_secure_bytes(
        &mut self,
        id: VaultItemId,
    ) -> Result<Zeroizing<Vec<u8>>, VaultCrudError> {
        let repository = self
            .vault_mutation_repository(VaultAuditOperation::Fill)
            .map_err(storage_error)?;
        let result = self.vault_runtime.use_secure_bytes(&repository, id);
        self.record_vault_operation_result(VaultAuditOperation::Fill, result)
    }

    pub fn vault_generate_totp_code(
        &mut self,
        id: VaultItemId,
        timestamp_secs: u64,
    ) -> Result<Zeroizing<String>, VaultCrudError> {
        let repository = self
            .vault_mutation_repository(VaultAuditOperation::TotpFill)
            .map_err(storage_error)?;
        let result = self
            .vault_runtime
            .generate_totp_code(&repository, id, timestamp_secs);
        self.record_vault_operation_result(VaultAuditOperation::TotpFill, result)
    }
}

fn storage_error(error: maho_storage::StorageError) -> VaultCrudError {
    VaultCrudError::Storage(error.to_string())
}
