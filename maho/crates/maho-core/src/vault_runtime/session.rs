use std::sync::Weak;

use chrono::{DateTime, Utc};
use maho_types::vault::{
    VaultBatchReadRequest, VaultBatchReadResult, VaultItemId, VaultRevision, VaultSchemaVersion,
};
use zeroize::Zeroizing;

use crate::vault_manager::{VaultCredentialFormDetails, VaultCrudError};
use crate::vault_runtime::repository::VaultRepository;

use super::VaultRuntime;

fn serialize_revision_as_decimal_string<S>(
    revision: &VaultRevision,
    serializer: S,
) -> Result<S::Ok, S::Error>
where
    S: serde::Serializer,
{
    serializer.serialize_str(&revision.value().to_string())
}

pub struct VaultBackendSession {
    runtime: Weak<VaultRuntime>,
    profile_database_path: String,
    profile_id: maho_types::identifiers::ProfileId,
}

#[derive(serde::Serialize)]
#[serde(rename_all = "camelCase")]
pub struct VaultBackendLoginCredential {
    pub schema_version: VaultSchemaVersion,
    pub item_id: VaultItemId,
    #[serde(serialize_with = "serialize_revision_as_decimal_string")]
    pub observed_revision: VaultRevision,
    pub signon_realm: String,
    pub username: String,
    pub created_at: DateTime<Utc>,
    pub updated_at: DateTime<Utc>,
    pub last_used_at: Option<DateTime<Utc>>,
    /// Task 4: browser-form detail stored with the record so a Chromium
    /// `PasswordForm` survives store -> Vault -> store. Serialized as
    /// `formDetails`; `null` for legacy records written before this existed, in
    /// which case the C++ adapter falls back to Chromium's own defaults.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub form_details: Option<VaultCredentialFormDetails>,
}

#[derive(Clone, Copy, Debug, serde::Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum VaultBackendSecretAction {
    Fill,
}

#[derive(Debug, thiserror::Error)]
#[non_exhaustive]
pub enum VaultBackendSessionError {
    #[error("Vault backend runtime is unavailable")]
    RuntimeUnavailable,
    #[error("Vault backend storage is unavailable")]
    StorageUnavailable,
    #[error("Vault backend storage failed: {0}")]
    Storage(String),
    #[error(transparent)]
    Batch(#[from] VaultCrudError),
}

impl VaultBackendSession {
    pub(crate) fn new(
        runtime: Weak<VaultRuntime>,
        profile_database_path: &str,
        profile_id: maho_types::identifiers::ProfileId,
    ) -> Self {
        Self {
            runtime,
            profile_database_path: profile_database_path.to_owned(),
            profile_id,
        }
    }

    pub fn batch_read(
        &self,
        request: &VaultBatchReadRequest,
    ) -> Result<VaultBatchReadResult, VaultBackendSessionError> {
        let runtime = self
            .runtime
            .upgrade()
            .ok_or(VaultBackendSessionError::RuntimeUnavailable)?;
        runtime.ensure_unlocked().map_err(VaultCrudError::from)?;
        let storage = maho_storage::sqlite::SqliteStorage::open(&self.profile_database_path)
            .map_err(|error| VaultBackendSessionError::Storage(error.to_string()))?;
        runtime
            .batch_read(VaultRepository::new(&storage, false), request)
            .map_err(VaultBackendSessionError::from)
    }

    pub fn login_credentials(
        &self,
        origin: Option<&str>,
        scheme: Option<u8>,
    ) -> Result<Vec<VaultBackendLoginCredential>, VaultBackendSessionError> {
        let runtime = self
            .runtime
            .upgrade()
            .ok_or(VaultBackendSessionError::RuntimeUnavailable)?;
        runtime.ensure_unlocked().map_err(VaultCrudError::from)?;
        let storage = maho_storage::sqlite::SqliteStorage::open(&self.profile_database_path)
            .map_err(|error| VaultBackendSessionError::Storage(error.to_string()))?;
        runtime
            .login_credentials(VaultRepository::new(&storage, false), origin, scheme)
            .map_err(VaultBackendSessionError::from)
    }

    pub fn resolve_login_secret(
        &self,
        item_id: VaultItemId,
        expected_revision: VaultRevision,
        action: VaultBackendSecretAction,
    ) -> Result<Zeroizing<String>, VaultBackendSessionError> {
        let runtime = self
            .runtime
            .upgrade()
            .ok_or(VaultBackendSessionError::RuntimeUnavailable)?;
        let storage = maho_storage::sqlite::SqliteStorage::open(&self.profile_database_path)
            .map_err(|error| VaultBackendSessionError::Storage(error.to_string()))?;
        let operation = match action {
            VaultBackendSecretAction::Fill => maho_types::vault::VaultAuditOperation::Fill,
        };
        let audit_context = super::audit::AuditContext::new(self.profile_id.clone(), operation);

        let result = if let Err(lock_err) = runtime.ensure_unlocked() {
            Err(VaultCrudError::from(lock_err))
        } else {
            let repository =
                VaultRepository::new(&storage, false).with_audit(audit_context.clone());
            match action {
                VaultBackendSecretAction::Fill => {
                    runtime.use_login_password_at_revision(&repository, item_id, expected_revision)
                }
            }
        };

        let completed = super::audit::complete_operation_result(&storage, &audit_context, result);
        completed.map_err(VaultBackendSessionError::from)
    }
}

#[cfg(test)]
mod tests {
    use chrono::DateTime;
    use maho_types::vault::{VaultItemId, VaultRevision, VaultSchemaVersion};

    use super::VaultBackendLoginCredential;

    fn credential(revision: u64) -> VaultBackendLoginCredential {
        let timestamp = DateTime::from_timestamp(1_700_000_000, 0).expect("valid test timestamp");
        VaultBackendLoginCredential {
            schema_version: VaultSchemaVersion::CURRENT,
            item_id: VaultItemId::new(),
            observed_revision: VaultRevision::new(revision),
            signon_realm: "https://revision.example".to_string(),
            username: "revision@example.test".to_string(),
            created_at: timestamp,
            updated_at: timestamp,
            last_used_at: None,
            form_details: None,
        }
    }

    #[test]
    fn backend_credential_serializes_revision_as_decimal_string() {
        for (revision, expected) in [(42, "42"), (u64::MAX, "18446744073709551615")] {
            let json =
                serde_json::to_value(credential(revision)).expect("serialize backend credential");
            assert_eq!(json["observedRevision"], expected);
            assert!(json["observedRevision"].is_string());
            assert!(json.get("password").is_none());
            assert!(json.get("secret").is_none());
        }
    }
}
