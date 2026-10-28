use crate::sync_models::SyncEntity;
use serde::{Deserialize, Serialize};
use std::collections::HashMap;

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Snapshot {
    pub schema_version: u8,
    pub hlc_ts_ceiling: u64,
    pub created_at: String,
    pub creator_device_id: u32,
    pub entities: HashMap<String, Vec<SyncEntity>>,
    /// Client-held accounts only: the Vault key slots (base64), wrapped by keys
    /// derived from the recovery phrase, so a device joining with the phrase can
    /// unlock the synced Vault items. Absent for escrow accounts.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub vault_keys: Option<SnapshotVaultKeys>,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SnapshotVaultKeys {
    pub kdf_params: String,
    pub wrapped_user_key: String,
    pub wrapped_recovery_key: String,
}
