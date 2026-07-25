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
        let row = policy_to_row(&policy, Utc::now())?;
        storage
            .save_vault_policy(&row)
            .map_err(|error| VaultCrudError::Storage(error.to_string()))?;
        self.record_vault_policy_changed_audit(policy.policy)?;
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
        let rows = storage
            .list_vault_audit_events()
            .map_err(|error| VaultCrudError::Storage(error.to_string()))?;
        let mut filtered = rows
            .into_iter()
            .filter(|row| after_seq.is_none_or(|cursor| row.seq > cursor))
            .collect::<Vec<_>>();
        let next_cursor = if limit == 0 || filtered.len() <= limit {
            None
        } else {
            filtered.truncate(limit);
            filtered.last().map(|row| row.seq.to_string())
        };
        let entries = filtered
            .into_iter()
            .map(row_to_public_audit)
            .collect::<Result<Vec<_>, _>>()?;
        Ok(VaultAuditPageDto {
            schema_version: VaultSchemaVersion::CURRENT,
            entries,
            next_cursor,
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
        id: VaultAuditRecordId::from_str(&row.id).map_err(contract_error)?,
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
            .map(Some)
            .map_err(|_| VaultCrudError::Storage("invalid audit cursor".to_string())),
        None => Ok(None),
    }
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
