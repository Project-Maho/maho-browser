use std::str::FromStr;

use chrono::{DateTime, Utc};
use maho_storage::sqlite::{VaultAuditRow, VaultPolicyRow};
use maho_types::vault::{
    CredentialItemAlias, CredentialOrigin, VaultAgentPolicy, VaultAuditDecision, VaultAuditPageDto,
    VaultAuditPageRequest, VaultAuditPublicDto, VaultAuditRecordId, VaultErrorCode, VaultItemId,
    VaultPolicy, VaultSchemaVersion, VaultTaskId,
};

use crate::maho_core::MahoCore;
use crate::vault_manager::VaultCrudError;

const DEFAULT_SCOPE: &str = "default";

impl MahoCore {
    pub fn vault_agent_policy_status(&self) -> Result<VaultPolicy, VaultCrudError> {
        self.resolve_vault_agent_policy(None, None, Utc::now())
    }

    pub fn vault_set_agent_policy(
        &self,
        policy: VaultPolicy,
    ) -> Result<VaultPolicy, VaultCrudError> {
        let storage = self.storage_ref().ok_or_else(|| {
            VaultCrudError::Storage("vault policy storage is not configured".to_string())
        })?;
        let profile_id = self.get_active_profile_id().cloned().unwrap_or_default();
        let audit_context = crate::vault_runtime::audit::AuditContext::with_policy(
            profile_id,
            maho_types::vault::VaultAuditOperation::PolicyChanged,
            policy.policy,
        );
        let row = policy_to_row(&policy, Utc::now())?;
        storage
            .vault_audit_transaction(|tx| {
                tx.upsert_policy(&row)?;
                let record = audit_context.build_record(
                    maho_types::vault::VaultAuditDecision::Allowed,
                    None,
                    Utc::now(),
                );
                crate::vault_runtime::audit::append_audit_tx(tx, record)?;
                Ok(())
            })
            .map_err(|error| VaultCrudError::Storage(error.to_string()))?;
        self.resolve_vault_agent_policy(policy.item_id, policy.origin.as_ref(), Utc::now())
    }

    /// Grant seam: callers must use this decision-time read instead of caching
    /// policy, so future grant issuance sees policy changes immediately.
    pub fn resolve_vault_agent_policy(
        &self,
        item_id: Option<VaultItemId>,
        origin: Option<&CredentialOrigin>,
        now: DateTime<Utc>,
    ) -> Result<VaultPolicy, VaultCrudError> {
        let storage = self.storage_ref().ok_or_else(|| {
            VaultCrudError::Storage("vault policy storage is not configured".to_string())
        })?;
        let rows = storage
            .list_vault_policies()
            .map_err(|error| VaultCrudError::Storage(error.to_string()))?;
        let policies = rows
            .into_iter()
            .map(row_to_policy)
            .collect::<Result<Vec<_>, _>>()?;
        for scope in policy_scope_priority(item_id, origin) {
            if let Some(policy) = policies
                .iter()
                .find(|candidate| policy_scope(candidate) == scope)
                .filter(|candidate| !is_expired(candidate, now))
            {
                return Ok(policy.clone());
            }
        }
        Ok(VaultPolicy {
            schema_version: VaultSchemaVersion::CURRENT,
            policy: VaultAgentPolicy::Deny,
            item_id: None,
            origin: None,
            expires_at: None,
        })
    }

    pub fn vault_audit_page(
        &self,
        request: &VaultAuditPageRequest,
    ) -> Result<VaultAuditPageDto, VaultCrudError> {
        let storage = self.storage_ref().ok_or_else(|| {
            VaultCrudError::Storage("vault audit storage is not configured".to_string())
        })?;
        let after_seq = audit_cursor(request.cursor.as_deref())?;
        let limit = usize::try_from(request.limit)
            .map_err(|_| VaultCrudError::Storage("audit limit is too large".to_string()))?;
        // Keyset page query in storage: bounded row materialization (limit + 1),
        // positive limits clamped to 1,000, limit 0 mapped to the default page of
        // 100, and a next cursor returned only when an extra row exists.
        let (rows, next_cursor) = storage
            .page_vault_audit_events(after_seq, limit)
            .map_err(|error| VaultCrudError::Storage(error.to_string()))?;
        let entries = rows
            .into_iter()
            .map(row_to_public_audit)
            .collect::<Result<Vec<_>, _>>()?;
        Ok(VaultAuditPageDto {
            schema_version: VaultSchemaVersion::CURRENT,
            entries,
            next_cursor: next_cursor.map(|seq| seq.to_string()),
        })
    }
}

fn policy_to_row(
    policy: &VaultPolicy,
    updated_at: DateTime<Utc>,
) -> Result<VaultPolicyRow, VaultCrudError> {
    Ok(VaultPolicyRow {
        scope: policy_scope(policy),
        schema_version: i64::from(u16::from(policy.schema_version)),
        policy: token(policy.policy)?,
        item_id: policy.item_id.map(|id| id.to_string()),
        origin: policy.origin.as_ref().map(ToString::to_string),
        expires_at: policy.expires_at.map(|expires_at| expires_at.to_rfc3339()),
        updated_at: updated_at.to_rfc3339(),
    })
}

fn row_to_policy(row: VaultPolicyRow) -> Result<VaultPolicy, VaultCrudError> {
    Ok(VaultPolicy {
        schema_version: schema_version(row.schema_version)?,
        policy: token_value(&row.policy)?,
        item_id: row
            .item_id
            .as_deref()
            .map(VaultItemId::from_str)
            .transpose()
            .map_err(contract_error)?,
        origin: row
            .origin
            .map(CredentialOrigin::try_from)
            .transpose()
            .map_err(contract_error)?,
        expires_at: row.expires_at.as_deref().map(parse_time).transpose()?,
    })
}

fn policy_scope(policy: &VaultPolicy) -> String {
    match (policy.item_id, policy.origin.as_ref()) {
        (Some(item_id), Some(origin)) => format!("item_origin:{item_id}|{}", origin.as_str()),
        (Some(item_id), None) => format!("item:{item_id}"),
        (None, Some(origin)) => format!("origin:{}", origin.as_str()),
        (None, None) => DEFAULT_SCOPE.to_string(),
    }
}

fn policy_scope_priority(
    item_id: Option<VaultItemId>,
    origin: Option<&CredentialOrigin>,
) -> Vec<String> {
    let mut scopes = Vec::with_capacity(4);
    if let (Some(item_id), Some(origin)) = (item_id, origin) {
        scopes.push(format!("item_origin:{item_id}|{}", origin.as_str()));
    }
    if let Some(item_id) = item_id {
        scopes.push(format!("item:{item_id}"));
    }
    if let Some(origin) = origin {
        scopes.push(format!("origin:{}", origin.as_str()));
    }
    scopes.push(DEFAULT_SCOPE.to_string());
    scopes
}

fn is_expired(policy: &VaultPolicy, now: DateTime<Utc>) -> bool {
    policy
        .expires_at
        .is_some_and(|expires_at| expires_at <= now)
}

fn row_to_public_audit(row: VaultAuditRow) -> Result<VaultAuditPublicDto, VaultCrudError> {
    Ok(VaultAuditPublicDto {
        schema_version: schema_version(row.schema_version)?,
        id: audit_record_id_from_stored(&row.id),
        timestamp: parse_time(&row.timestamp)?,
        task_id: row
            .task_id
            .as_deref()
            .map(VaultTaskId::from_str)
            .transpose()
            .map_err(contract_error)?,
        origin: row
            .top_origin
            .or(row.frame_origin)
            .map(CredentialOrigin::try_from)
            .transpose()
            .map_err(contract_error)?,
        item_alias: None::<CredentialItemAlias>,
        operation: token_value(&row.operation)?,
        policy: row.policy.as_deref().map(token_value).transpose()?,
        decision: token_value::<VaultAuditDecision>(&row.decision)?,
        reason: row
            .reason
            .as_deref()
            .map(token_value::<VaultErrorCode>)
            .transpose()?,
        device_name: row.device_name,
    })
}

fn audit_cursor(cursor: Option<&str>) -> Result<Option<i64>, VaultCrudError> {
    match cursor.filter(|value| !value.is_empty()) {
        Some(value) => value
            .parse::<i64>()
            .map_err(|_| VaultCrudError::Storage("invalid audit cursor".to_string()))
            .and_then(|parsed| {
                if parsed < 0 {
                    Err(VaultCrudError::Storage(
                        "invalid audit cursor: must be nonnegative".to_string(),
                    ))
                } else {
                    Ok(Some(parsed))
                }
            }),
        None => Ok(None),
    }
}

/// Stored audit ids are opaque TEXT at the storage layer; product-created rows
/// carry UUID ids while imported/legacy rows may not. Map them deterministically
/// into the typed public id (SHA-256 derived, RFC 4122 version-5 shaped) so
/// paging over legacy data stays bounded and stable instead of failing the
/// whole page.
fn audit_record_id_from_stored(id: &str) -> VaultAuditRecordId {
    VaultAuditRecordId::from_str(id).unwrap_or_else(|_| {
        use sha2::{Digest, Sha256};
        let digest: [u8; 32] = Sha256::digest(id.as_bytes()).into();
        let mut bytes = [0u8; 16];
        bytes.copy_from_slice(&digest[..16]);
        bytes[6] = (bytes[6] & 0x0f) | 0x50;
        bytes[8] = (bytes[8] & 0x3f) | 0x80;
        VaultAuditRecordId::from_uuid(uuid::Uuid::from_bytes(bytes))
    })
}

fn parse_time(value: &str) -> Result<DateTime<Utc>, VaultCrudError> {
    DateTime::parse_from_rfc3339(value)
        .map(|value| value.with_timezone(&Utc))
        .map_err(|error| VaultCrudError::Storage(format!("invalid vault timestamp: {error}")))
}

fn schema_version(value: i64) -> Result<VaultSchemaVersion, VaultCrudError> {
    let value = u16::try_from(value)
        .map_err(|_| VaultCrudError::Storage("invalid vault schema version".to_string()))?;
    VaultSchemaVersion::try_from(value).map_err(|error| VaultCrudError::Storage(error.to_string()))
}

fn token<T: serde::Serialize>(value: T) -> Result<String, VaultCrudError> {
    serde_json::to_value(value)
        .map_err(|error| VaultCrudError::Storage(format!("serialize vault token failed: {error}")))?
        .as_str()
        .map(str::to_string)
        .ok_or_else(|| VaultCrudError::Storage("vault token was not a string".to_string()))
}

fn token_value<T: serde::de::DeserializeOwned>(value: &str) -> Result<T, VaultCrudError> {
    serde_json::from_value(serde_json::Value::String(value.to_string()))
        .map_err(|error| VaultCrudError::Storage(format!("invalid vault token: {error}")))
}

fn contract_error(error: maho_types::vault::VaultContractError) -> VaultCrudError {
    VaultCrudError::Storage(error.to_string())
}
