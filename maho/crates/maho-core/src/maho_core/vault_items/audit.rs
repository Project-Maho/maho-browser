use chrono::Utc;
use maho_types::vault::{VaultAuditDecision, VaultAuditOperation, VaultErrorCode};

use crate::maho_core::MahoCore;
use crate::vault_manager::VaultCrudError;

impl MahoCore {
    pub fn vault_record_import_commit_audit(
        &self,
        decision: VaultAuditDecision,
        reason: Option<VaultErrorCode>,
    ) -> Result<(), VaultCrudError> {
        let storage = self
            .storage_ref()
            .ok_or_else(|| VaultCrudError::Storage("vault storage not configured".to_string()))?;
        let profile_id = self.get_active_profile_id().cloned().unwrap_or_default();
        let audit = crate::vault_runtime::audit::AuditContext::new(
            profile_id,
            VaultAuditOperation::ImportCommit,
        );
        let record = audit.build_record(decision, reason, Utc::now());
        crate::vault_runtime::audit::append_standalone_audit(storage, record)?;
        Ok(())
    }

    pub(crate) fn record_vault_operation_result<T>(
        &self,
        operation: VaultAuditOperation,
        result: Result<T, VaultCrudError>,
    ) -> Result<T, VaultCrudError> {
        let storage = self.storage_ref().ok_or_else(|| {
            VaultCrudError::Storage("vault storage not configured for audit".to_string())
        })?;
        let profile_id = self.get_active_profile_id().cloned().unwrap_or_default();
        let audit_context = crate::vault_runtime::audit::AuditContext::new(profile_id, operation);
        crate::vault_runtime::audit::complete_operation_result(storage, &audit_context, result)
    }
}
