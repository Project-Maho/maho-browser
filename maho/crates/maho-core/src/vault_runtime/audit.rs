use chrono::{DateTime, Utc};
use maho_storage::sqlite::{SqliteStorage, VaultAuditRow, VaultTx};
use maho_storage::StorageError;
use maho_types::identifiers::ProfileId;
use maho_types::vault::{
    VaultAgentPolicy, VaultAuditDecision, VaultAuditOperation, VaultAuditRecord,
    VaultAuditRecordId, VaultErrorCode, VaultSchemaVersion, VaultWorkspaceId,
};
use sha2::{Digest, Sha256};

use crate::vault_manager::VaultCrudError;

pub(crate) const LOCAL_BROWSER_WORKSPACE_ID: uuid::Uuid = uuid::Uuid::nil();
pub(crate) const LOCAL_DEVICE_NAME: &str = "Maho Browser";

#[derive(Clone, Debug)]
pub(crate) struct AuditContext {
    pub profile_id: ProfileId,
    pub operation: VaultAuditOperation,
    pub policy: Option<VaultAgentPolicy>,
}

impl AuditContext {
    pub(crate) fn new(profile_id: ProfileId, operation: VaultAuditOperation) -> Self {
        Self {
            profile_id,
            operation,
            policy: None,
        }
    }

    pub(crate) fn with_policy(
        profile_id: ProfileId,
        operation: VaultAuditOperation,
        policy: VaultAgentPolicy,
    ) -> Self {
        Self {
            profile_id,
            operation,
            policy: Some(policy),
        }
    }

    pub(crate) fn build_record(
        &self,
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
            profile_id: self.profile_id.clone(),
            workspace_id: VaultWorkspaceId::from_uuid(LOCAL_BROWSER_WORKSPACE_ID),
            top_origin: None,
            frame_origin: None,
            item_id: None,
            item_alias: None,
            operation: self.operation,
            policy: self.policy,
            decision,
            reason,
            device_name: LOCAL_DEVICE_NAME.to_string(),
        }
    }
}

pub(crate) fn append_audit_tx(
    tx: &VaultTx<'_>,
    record: VaultAuditRecord,
) -> Result<VaultAuditRow, StorageError> {
    // Tail primitive reads only (seq, entry_hash) inside the writer reservation,
    // so an append materializes at most one row of audit history. Sequence
    // allocation is checked: overflow is an error, never saturating reuse.
    let head = tx.audit_tail()?;
    let (seq, prev_hash) = match head {
        Some((tail_seq, tail_hash)) => {
            let next = tail_seq
                .checked_add(1)
                .ok_or_else(|| StorageError::Other("vault audit sequence overflow".to_string()))?;
            (next, Some(tail_hash))
        }
        None => (1, None),
    };
    let row = audit_row(record, seq, prev_hash).map_err(|e| StorageError::Other(e.to_string()))?;
    tx.append_audit(&row)?;
    Ok(row)
}

pub(crate) fn append_standalone_audit(
    storage: &SqliteStorage,
    record: VaultAuditRecord,
) -> Result<VaultAuditRow, VaultCrudError> {
    storage
        .vault_audit_transaction(|tx| append_audit_tx(tx, record))
        .map_err(|error| VaultCrudError::Storage(error.to_string()))
}

pub(crate) fn complete_operation_result<T>(
    storage: &SqliteStorage,
    context: &AuditContext,
    result: Result<T, VaultCrudError>,
) -> Result<T, VaultCrudError> {
    match result {
        Ok(value) => Ok(value),
        Err(error) => {
            let (decision, reason) = audit_outcome(&error);
            let record = context.build_record(decision, reason, Utc::now());
            let audit_result = append_standalone_audit(storage, record);
            audit_result?;
            Err(error)
        }
    }
}

pub(crate) fn audit_outcome(
    error: &VaultCrudError,
) -> (VaultAuditDecision, Option<VaultErrorCode>) {
    match error {
        VaultCrudError::Locked => (VaultAuditDecision::Denied, Some(VaultErrorCode::Locked)),
        other => (VaultAuditDecision::Failed, audit_reason(other)),
    }
}

pub(crate) fn audit_reason(error: &VaultCrudError) -> Option<VaultErrorCode> {
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

pub(crate) fn audit_row(
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

pub(crate) fn audit_token<T: serde::Serialize>(value: T) -> Result<String, VaultCrudError> {
    let value = serde_json::to_value(value).map_err(|error| {
        VaultCrudError::Storage(format!("serialize vault audit token failed: {error}"))
    })?;
    value
        .as_str()
        .map(str::to_string)
        .ok_or_else(|| VaultCrudError::Storage("vault audit token was not a string".to_string()))
}

pub(crate) fn audit_hash(row: &VaultAuditRow) -> Vec<u8> {
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
