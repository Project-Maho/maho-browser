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
}
