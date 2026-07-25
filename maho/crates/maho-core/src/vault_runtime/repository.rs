use maho_storage::sqlite::{EncryptedVaultItemRow, SqliteStorage, VaultTombstoneCas};
use maho_storage::StorageError;

pub(crate) struct VaultTombstoneCandidate {
    pub(crate) id: String,
    pub(crate) expected_revision: i64,
    pub(crate) new_revision: i64,
    pub(crate) envelope: Vec<u8>,
    pub(crate) timestamp: String,
}

pub(crate) struct VaultRepository<'storage> {
    storage: &'storage SqliteStorage,
    inject_post_write_fault: bool,
}

impl<'storage> VaultRepository<'storage> {
    pub(crate) fn new(storage: &'storage SqliteStorage, inject_post_write_fault: bool) -> Self {
        Self {
            storage,
            inject_post_write_fault,
        }
    }

    pub(crate) fn insert_item(&self, row: &EncryptedVaultItemRow) -> Result<bool, StorageError> {
        self.storage.vault_transaction(|transaction| {
            let inserted = transaction.insert_item_new(row)?;
            self.fail_after_candidate_write()?;
            Ok(inserted)
        })
    }

    pub(crate) fn update_item_cas(
        &self,
        row: &EncryptedVaultItemRow,
        expected_revision: i64,
    ) -> Result<bool, StorageError> {
        self.storage.vault_transaction(|transaction| {
            let updated = transaction.update_item_cas(row, expected_revision)?;
            self.fail_after_candidate_write()?;
            Ok(updated)
        })
    }

    pub(crate) fn tombstone_item_cas(
        &self,
        cas: &VaultTombstoneCas<'_>,
    ) -> Result<bool, StorageError> {
        self.storage.vault_transaction(|transaction| {
            let tombstoned = transaction.tombstone_item_cas(cas)?;
            self.fail_after_candidate_write()?;
            Ok(tombstoned)
        })
    }

    pub(crate) fn tombstone_items(
        &self,
        candidates: &[VaultTombstoneCandidate],
    ) -> Result<(), StorageError> {
        self.storage.vault_transaction(|transaction| {
            for candidate in candidates {
                let persisted = transaction.tombstone_item_cas(&VaultTombstoneCas {
                    id: &candidate.id,
                    expected_revision: candidate.expected_revision,
                    new_revision: candidate.new_revision,
                    envelope: &candidate.envelope,
                    updated_at: &candidate.timestamp,
                    deleted_at: &candidate.timestamp,
                })?;
                if !persisted {
                    return Err(StorageError::Other(
                        "range tombstone candidate did not persist".to_string(),
                    ));
                }
                self.fail_after_candidate_write()?;
            }
            Ok(())
        })
    }

    pub(crate) fn get_item(&self, id: &str) -> Result<Option<EncryptedVaultItemRow>, StorageError> {
        self.storage.get_vault_item(id)
    }

    pub(crate) fn list_items(&self) -> Result<Vec<EncryptedVaultItemRow>, StorageError> {
        self.storage.list_vault_items()
    }

    pub(crate) fn count_active_items(&self) -> Result<u64, StorageError> {
        self.storage.count_active_vault_items()
    }

    fn fail_after_candidate_write(&self) -> Result<(), StorageError> {
        if self.inject_post_write_fault {
            return Err(StorageError::Other(
                "injected item-persist fault".to_string(),
            ));
        }
        Ok(())
    }
}
