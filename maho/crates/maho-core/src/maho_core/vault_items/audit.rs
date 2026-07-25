use chrono::{DateTime, Utc};
use maho_storage::sqlite::VaultAuditRow;
use maho_types::identifiers::ProfileId;
use maho_types::vault::{
    VaultAgentPolicy, VaultAuditDecision, VaultAuditOperation, VaultAuditRecord,
    VaultAuditRecordId, VaultErrorCode, VaultSchemaVersion, VaultWorkspaceId,
};
use sha2::{Digest, Sha256};

use crate::maho_core::MahoCore;
use crate::vault_manager::VaultCrudError;

const LOCAL_BROWSER_WORKSPACE_ID: uuid::Uuid = uuid::Uuid::nil();
const LOCAL_DEVICE_NAME: &str = "Maho Browser";

impl MahoCore {
    pub fn vault_record_import_commit_audit(
        &self,
        decision: VaultAuditDecision,
        reason: Option<VaultErrorCode>,
    ) -> Result<(), VaultCrudError> {
        self.append_vault_audit(VaultAuditOperation::ImportCommit, decision, reason)
    }

    pub(crate) fn record_vault_policy_changed_audit(
        &self,
        policy: VaultAgentPolicy,
    ) -> Result<(), VaultCrudError> {
        self.append_vault_audit_with_policy(
            VaultAuditOperation::PolicyChanged,
            Some(policy),
            VaultAuditDecision::Allowed,
            None,
        )
    }

    pub(crate) fn record_vault_operation_result<T>(
        &self,
        operation: VaultAuditOperation,
        result: Result<T, VaultCrudError>,
    ) -> Result<T, VaultCrudError> {
        let (decision, reason) = audit_outcome(&result);
        let audit_result = self.append_vault_audit(operation, decision, reason);
        match result {
            Ok(value) => {
                audit_result?;
                Ok(value)
            }
            Err(error) => {
                audit_result?;
                Err(error)
            }
        }
    }

    fn append_vault_audit(
        &self,
        operation: VaultAuditOperation,
        decision: VaultAuditDecision,
        reason: Option<VaultErrorCode>,
    ) -> Result<(), VaultCrudError> {
        self.append_vault_audit_with_policy(operation, None, decision, reason)
    }

    fn append_vault_audit_with_policy(
        &self,
        operation: VaultAuditOperation,
        policy: Option<VaultAgentPolicy>,
        decision: VaultAuditDecision,
        reason: Option<VaultErrorCode>,
    ) -> Result<(), VaultCrudError> {
        let storage = self.storage_ref().ok_or_else(|| {
            VaultCrudError::Storage("vault storage not configured for audit".to_string())
        })?;
        let previous = storage
            .list_vault_audit_events()
            .map_err(|error| VaultCrudError::Storage(error.to_string()))?;
        let (seq, prev_hash) = next_audit_position(&previous);
        let record = self.build_audit_record(operation, policy, decision, reason, Utc::now());
        let row = audit_row(record, seq, prev_hash)?;
        storage
            .append_vault_audit(&row)
            .map_err(|error| VaultCrudError::Storage(error.to_string()))
    }

    fn build_audit_record(
        &self,
        operation: VaultAuditOperation,
        policy: Option<VaultAgentPolicy>,
        decision: VaultAuditDecision,
        reason: Option<VaultErrorCode>,
        timestamp: DateTime<Utc>,
    ) -> VaultAuditRecord {
        VaultAuditRecord {
            schema_version: VaultSchemaVersion::CURRENT,
            id: VaultAuditRecordId::new(),
            timestamp,
            session_id: None,
            task_id: None,
            profile_id: self
                .get_active_profile_id()
                .cloned()
                .unwrap_or_else(ProfileId::default),
            workspace_id: VaultWorkspaceId::from_uuid(LOCAL_BROWSER_WORKSPACE_ID),
            top_origin: None,
            frame_origin: None,
            item_id: None,
            item_alias: None,
            operation,
            policy,
            decision,
            reason,
            device_name: LOCAL_DEVICE_NAME.to_string(),
        }
    }
}

fn audit_outcome<T>(
    result: &Result<T, VaultCrudError>,
) -> (VaultAuditDecision, Option<VaultErrorCode>) {
    match result {
        Ok(_) => (VaultAuditDecision::Allowed, None),
        Err(VaultCrudError::Locked) => (VaultAuditDecision::Denied, Some(VaultErrorCode::Locked)),
        Err(error) => (VaultAuditDecision::Failed, audit_reason(error)),
    }
}

fn audit_reason(error: &VaultCrudError) -> Option<VaultErrorCode> {
    match error {
        VaultCrudError::Locked => Some(VaultErrorCode::Locked),
        VaultCrudError::ItemNotFound => Some(VaultErrorCode::NotFound),
        VaultCrudError::RevisionConflict { .. } => Some(VaultErrorCode::RevisionConflict),
        VaultCrudError::InvalidOrigin { .. } => Some(VaultErrorCode::InvalidOrigin),
        VaultCrudError::ItemKindMismatch | VaultCrudError::FieldMismatch => {
            Some(VaultErrorCode::SecretFieldForbidden)
        }
        VaultCrudError::Duplicate { .. } => None,
        VaultCrudError::MalformedPayload | VaultCrudError::Storage(_) | VaultCrudError::Crypto => {
            Some(VaultErrorCode::StorageFailure)
        }
    }
}

fn next_audit_position(previous: &[VaultAuditRow]) -> (i64, Option<Vec<u8>>) {
    let Some(last) = previous.last() else {
        return (1, None);
    };
    (last.seq.saturating_add(1), Some(last.entry_hash.clone()))
}

fn audit_row(
    record: VaultAuditRecord,
    seq: i64,
    prev_hash: Option<Vec<u8>>,
) -> Result<VaultAuditRow, VaultCrudError> {
    let mut row = VaultAuditRow {
        id: record.id.to_string(),
        schema_version: i64::from(u16::from(record.schema_version)),
        seq,
        timestamp: record.timestamp.to_rfc3339(),
        session_id: record.session_id.map(|id| id.to_string()),
        task_id: record.task_id.map(|id| id.to_string()),
        profile_id: record.profile_id.to_string(),
        workspace_id: record.workspace_id.to_string(),
        top_origin: record.top_origin.map(String::from),
        frame_origin: record.frame_origin.map(String::from),
        item_id: None,
        item_alias: None,
        operation: audit_token(record.operation)?,
        policy: record.policy.map(audit_token).transpose()?,
        decision: audit_token(record.decision)?,
        reason: record.reason.map(audit_token).transpose()?,
        device_name: record.device_name,
        prev_hash,
        entry_hash: Vec::new(),
    };
    row.entry_hash = audit_hash(&row);
    Ok(row)
}

fn audit_token<T: serde::Serialize>(value: T) -> Result<String, VaultCrudError> {
    let value = serde_json::to_value(value).map_err(|error| {
        VaultCrudError::Storage(format!("serialize vault audit token failed: {error}"))
    })?;
    value
        .as_str()
        .map(str::to_string)
        .ok_or_else(|| VaultCrudError::Storage("vault audit token was not a string".to_string()))
}

fn audit_hash(row: &VaultAuditRow) -> Vec<u8> {
    let mut hasher = Sha256::new();
    hash_string(&mut hasher, &row.id);
    hash_string(&mut hasher, &row.schema_version.to_string());
    hash_string(&mut hasher, &row.seq.to_string());
    hash_string(&mut hasher, &row.timestamp);
    hash_optional_string(&mut hasher, &row.session_id);
    hash_optional_string(&mut hasher, &row.task_id);
    hash_string(&mut hasher, &row.profile_id);
    hash_string(&mut hasher, &row.workspace_id);
    hash_optional_string(&mut hasher, &row.top_origin);
    hash_optional_string(&mut hasher, &row.frame_origin);
    hash_string(&mut hasher, &row.operation);
    hash_optional_string(&mut hasher, &row.policy);
    hash_string(&mut hasher, &row.decision);
    hash_optional_string(&mut hasher, &row.reason);
    hash_string(&mut hasher, &row.device_name);
    hash_optional_bytes(&mut hasher, row.prev_hash.as_deref());
    hasher.finalize().to_vec()
}

fn hash_string(hasher: &mut Sha256, value: &str) {
    hash_bytes(hasher, value.as_bytes());
}

fn hash_optional_string(hasher: &mut Sha256, value: &Option<String>) {
    match value {
        Some(value) => hash_string(hasher, value),
        Option::None => hash_bytes(hasher, &[]),
    }
}

fn hash_optional_bytes(hasher: &mut Sha256, value: Option<&[u8]>) {
    match value {
        Some(value) => hash_bytes(hasher, value),
        Option::None => hash_bytes(hasher, &[]),
    }
}

fn hash_bytes(hasher: &mut Sha256, value: &[u8]) {
    let len = u64::try_from(value.len()).unwrap_or(u64::MAX);
    hasher.update(len.to_le_bytes());
    hasher.update(value);
}
