use maho_storage::StorageError;

use crate::maho_core::MahoCore;
use crate::vault_runtime::repository::VaultRepository;

impl MahoCore {
    fn item_persist_fault_enabled(&self) -> bool {
        #[cfg(test)]
        {
            self.vault_item_persist_fault
        }
        #[cfg(not(test))]
        {
            let _ = self;
            false
        }
    }

    pub(super) fn vault_repository(&self) -> Result<VaultRepository<'_>, StorageError> {
        let storage = self
            .storage
            .as_ref()
            .ok_or_else(|| StorageError::Other("vault storage not configured".to_string()))?;
        Ok(VaultRepository::new(
            storage,
            self.item_persist_fault_enabled(),
        ))
    }

    pub(super) fn vault_mutation_repository(
        &self,
        operation: maho_types::vault::VaultAuditOperation,
    ) -> Result<VaultRepository<'_>, StorageError> {
        let storage = self
            .storage
            .as_ref()
            .ok_or_else(|| StorageError::Other("vault storage not configured".to_string()))?;
        let profile_id = self.get_active_profile_id().cloned().unwrap_or_default();
        let audit = crate::vault_runtime::audit::AuditContext::new(profile_id, operation);
        Ok(VaultRepository::new(storage, self.item_persist_fault_enabled()).with_audit(audit))
    }
}
