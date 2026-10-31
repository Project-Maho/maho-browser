use maho_storage::sqlite::{EncryptedVaultItemRow, SqliteStorage, VaultTombstoneCas};
use maho_storage::StorageError;

use super::audit::AuditContext;

pub(crate) struct VaultTombstoneCandidate {
    pub(crate) id: String,
    pub(crate) expected_revision: i64,
    pub(crate) new_revision: i64,
    pub(crate) envelope: Vec<u8>,
    pub(crate) timestamp: String,
}

pub(crate) struct VaultRepository<'storage> {
    storage: &'storage SqliteStorage,
    audit: Option<AuditContext>,
    inject_post_write_fault: bool,
}

impl<'storage> VaultRepository<'storage> {
    pub(crate) fn new(storage: &'storage SqliteStorage, inject_post_write_fault: bool) -> Self {
        Self {
            storage,
            audit: None,
            inject_post_write_fault,
        }
    }

    pub(crate) fn with_audit(mut self, audit: AuditContext) -> Self {
        self.audit = Some(audit);
        self
    }

    pub(crate) fn insert_item(
        &self,
        row: &EncryptedVaultItemRow,
    ) -> Result<Option<u64>, StorageError> {
        self.storage.vault_audit_transaction(|tx| {
            let inserted = tx.insert_item_new(row)?;
            if !inserted {
                return Ok(None);
            }
            self.fail_after_candidate_write()?;
            if let Some(audit) = &self.audit {
                let record = audit.build_record(
                    maho_types::vault::VaultAuditDecision::Allowed,
                    None,
                    chrono::Utc::now(),
                );
                super::audit::append_audit_tx(tx, record)?;
            }
            let count = tx.count_active_items()?;
            Ok(Some(count))
        })
    }

    pub(crate) fn update_item_cas(
        &self,
        row: &EncryptedVaultItemRow,
        expected_revision: i64,
    ) -> Result<Option<u64>, StorageError> {
        self.storage.vault_audit_transaction(|tx| {
            let updated = tx.update_item_cas(row, expected_revision)?;
            if !updated {
                return Ok(None);
            }
            self.fail_after_candidate_write()?;
            if let Some(audit) = &self.audit {
                let record = audit.build_record(
                    maho_types::vault::VaultAuditDecision::Allowed,
                    None,
                    chrono::Utc::now(),
                );
                super::audit::append_audit_tx(tx, record)?;
            }
            let count = tx.count_active_items()?;
            Ok(Some(count))
        })
    }

    pub(crate) fn tombstone_item_cas(
        &self,
        cas: &VaultTombstoneCas<'_>,
    ) -> Result<Option<u64>, StorageError> {
        self.storage.vault_audit_transaction(|tx| {
            let tombstoned = tx.tombstone_item_cas(cas)?;
            if !tombstoned {
                return Ok(None);
            }
            self.fail_after_candidate_write()?;
            if let Some(audit) = &self.audit {
                let record = audit.build_record(
                    maho_types::vault::VaultAuditDecision::Allowed,
                    None,
                    chrono::Utc::now(),
                );
                super::audit::append_audit_tx(tx, record)?;
            }
            let count = tx.count_active_items()?;
            Ok(Some(count))
        })
    }

    pub(crate) fn tombstone_items(
        &self,
        candidates: &[VaultTombstoneCandidate],
    ) -> Result<u64, StorageError> {
        self.storage.vault_audit_transaction(|tx| {
            for candidate in candidates {
                let persisted = tx.tombstone_item_cas(&VaultTombstoneCas {
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
            if let Some(audit) = &self.audit {
                let record = audit.build_record(
                    maho_types::vault::VaultAuditDecision::Allowed,
                    None,
                    chrono::Utc::now(),
                );
                super::audit::append_audit_tx(tx, record)?;
            }
            tx.count_active_items()
        })
    }

    pub(crate) fn get_item(&self, id: &str) -> Result<Option<EncryptedVaultItemRow>, StorageError> {
        self.storage.get_vault_item(id)
    }

    pub(crate) fn list_items(&self) -> Result<Vec<EncryptedVaultItemRow>, StorageError> {
        self.storage.list_vault_items()
    }

    #[allow(dead_code)]
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
