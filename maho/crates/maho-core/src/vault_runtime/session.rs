use std::sync::Weak;

use chrono::{DateTime, Utc};
use maho_types::vault::{
    VaultBatchReadRequest, VaultBatchReadResult, VaultItemId, VaultRevision, VaultSchemaVersion,
};
use zeroize::Zeroize;

use crate::vault_manager::VaultCrudError;
use crate::vault_runtime::repository::VaultRepository;

use super::VaultRuntime;

pub struct VaultBackendSession {
    runtime: Weak<VaultRuntime>,
    profile_database_path: String,
}

#[derive(serde::Serialize)]
#[serde(rename_all = "camelCase")]
pub struct VaultBackendLoginCredential {
    pub schema_version: VaultSchemaVersion,
    pub id: VaultItemId,
    pub revision: VaultRevision,
    pub signon_realm: String,
    pub username: String,
    pub password: String,
    pub created_at: DateTime<Utc>,
    pub updated_at: DateTime<Utc>,
    pub last_used_at: Option<DateTime<Utc>>,
}

impl Drop for VaultBackendLoginCredential {
    fn drop(&mut self) {
        self.password.zeroize();
    }
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
    pub(crate) fn new(runtime: Weak<VaultRuntime>, profile_database_path: &str) -> Self {
        Self {
            runtime,
            profile_database_path: profile_database_path.to_owned(),
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
    ) -> Result<Vec<VaultBackendLoginCredential>, VaultBackendSessionError> {
        let runtime = self
            .runtime
            .upgrade()
            .ok_or(VaultBackendSessionError::RuntimeUnavailable)?;
        runtime.ensure_unlocked().map_err(VaultCrudError::from)?;
        let storage = maho_storage::sqlite::SqliteStorage::open(&self.profile_database_path)
            .map_err(|error| VaultBackendSessionError::Storage(error.to_string()))?;
        runtime
            .login_credentials(VaultRepository::new(&storage, false), origin)
            .map_err(VaultBackendSessionError::from)
    }
}
