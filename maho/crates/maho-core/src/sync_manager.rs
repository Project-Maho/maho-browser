//! SyncManager — orchestrates cross-device synchronization.
//!
//! The shell owns the transport. SyncManager owns entity ordering, E2EE
//! message preparation, durable-version coordination, and profile partitioning.

use crate::sync_models::{
    DeviceId, DeviceInfo, ReceivedTab, SyncConfig, SyncEntity, SyncEntityType, SyncMessage,
    SyncStatus,
};
use std::collections::HashMap;

const MAX_RECEIVED_TABS: usize = 500;

#[derive(Clone, Debug)]
pub(crate) struct QueuedSyncMessage {
    pub message: SyncMessage,
    pub queue_row_id: Option<i64>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SyncTransportOp {
    Push,
    Pull,
    SendTab,
}

impl std::fmt::Display for SyncTransportOp {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(match self {
            Self::Push => "push",
            Self::Pull => "pull",
            Self::SendTab => "send_tab",
        })
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SyncPayloadKind {
    EntityPush,
    EntityBatch,
    SendTab,
}

impl std::fmt::Display for SyncPayloadKind {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(match self {
            Self::EntityPush => "entity push",
            Self::EntityBatch => "entity batch",
            Self::SendTab => "tab transfer",
        })
    }
}

#[derive(Debug, Clone, PartialEq, Eq, thiserror::Error)]
pub enum SyncTransportError {
    #[error("keyless {operation} denied: E2EE key required")]
    KeylessTransportDenied { operation: SyncTransportOp },
    #[error("rejected plaintext {payload}: E2EE envelope required")]
    PlaintextPayloadRejected { payload: SyncPayloadKind },
    #[error("encrypted sync message received without key")]
    MissingEncryptionKey,
    #[error("decrypt error: {detail}")]
    Decrypt { detail: String },
    #[error("decrypt error: invalid nested message: {detail}")]
    MalformedEnvelopeContents { detail: String },
    #[error("rejected entity {entity_id} due to unsupported schema version {schema_version}")]
    UnsupportedSchemaVersion {
        entity_id: String,
        schema_version: u8,
    },
    #[error("rejected profile-scoped {entity_type} entity {entity_id}: profileId is required")]
    MissingProfileScope {
        entity_type: &'static str,
        entity_id: String,
    },
    #[error("database read error in apply: {detail}")]
    DatabaseRead { detail: String },
    #[error("encrypt error: {detail}")]
    Encrypt { detail: String },
    #[error("serialize error: {detail}")]
    Serialize { detail: String },
}

impl SyncTransportError {
    pub fn code(&self) -> &'static str {
        match self {
            Self::KeylessTransportDenied { .. } => "keyless_transport_denied",
            Self::PlaintextPayloadRejected { .. } => "plaintext_payload_rejected",
            Self::MissingEncryptionKey => "missing_encryption_key",
            Self::Decrypt { .. } => "decrypt_failed",
            Self::MalformedEnvelopeContents { .. } => "malformed_envelope_contents",
            Self::UnsupportedSchemaVersion { .. } => "unsupported_schema_version",
            Self::MissingProfileScope { .. } => "missing_profile_scope",
            Self::DatabaseRead { .. } => "database_read_error",
            Self::Encrypt { .. } => "encrypt_failed",
            Self::Serialize { .. } => "serialize_failed",
        }
    }
}

pub struct SyncManager {
    config: SyncConfig,
    status: SyncStatus,
    last_success_at: Option<i64>,
    last_error: Option<String>,
    last_transport_error: Option<SyncTransportError>,
    devices: HashMap<DeviceId, DeviceInfo>,
    received_tabs: Vec<ReceivedTab>,
    outgoing_messages: Vec<QueuedSyncMessage>,
    pub(crate) encryption_key: Option<crate::sync_crypto::EncryptionKey>,
    pending_pushes: Vec<SyncEntity>,
    entity_versions: lru::LruCache<String, (u64, u32, Option<String>, Option<String>)>,
    pub(crate) last_pulled_versions: HashMap<SyncEntityType, u64>,
    profile_last_pulled_versions: HashMap<(SyncEntityType, String), u64>,
    active_profile_id: Option<String>,
    /// Raw SpaceId -> ProfileId. This lets local Tab payloads that only carry a
    /// spaceId acquire an explicit profileId before encryption.
    space_profiles: HashMap<String, String>,
    /// Raw type/id -> last known ProfileId. Used to retain the partition for
    /// local tombstones whose domain payload has already been removed.
    entity_profiles: HashMap<String, String>,
    sent_entities: Vec<i64>,
}

impl SyncManager {
    pub fn new(config: SyncConfig) -> Self {
        Self {
            config,
            status: SyncStatus::Idle,
            last_success_at: None,
            last_error: None,
            last_transport_error: None,
            devices: HashMap::new(),
            received_tabs: Vec::new(),
            outgoing_messages: Vec::new(),
            encryption_key: None,
            pending_pushes: Vec::new(),
            entity_versions: lru::LruCache::new(std::num::NonZeroUsize::new(10_000).unwrap()),
            last_pulled_versions: HashMap::new(),
            profile_last_pulled_versions: HashMap::new(),
            active_profile_id: None,
            space_profiles: HashMap::new(),
            entity_profiles: HashMap::new(),
            sent_entities: Vec::new(),
        }
    }

    pub fn status(&self) -> &SyncStatus {
        &self.status
    }

    pub fn last_success_at(&self) -> Option<i64> {
        self.last_success_at
    }

    pub fn pending_outbox_count(&self) -> usize {
        self.outgoing_messages.len() + self.pending_pushes.len()
    }

    pub fn last_error(&self) -> Option<&str> {
        self.last_error.as_deref()
    }

    pub fn last_transport_error(&self) -> Option<&SyncTransportError> {
        self.last_transport_error.as_ref()
    }

    pub fn set_last_transport_error(&mut self, error: SyncTransportError) {
        self.record_transport_error(error);
    }

    fn record_transport_error(&mut self, error: SyncTransportError) {
        self.last_error = Some(error.to_string());
        self.last_transport_error = Some(error);
    }

    pub fn set_encryption_key(&mut self, key: crate::sync_crypto::EncryptionKey) {
        self.encryption_key = Some(key);
    }

    pub fn has_encryption_key(&self) -> bool {
        self.encryption_key.is_some()
    }

    pub fn set_status(&mut self, status: SyncStatus) {
        if status == SyncStatus::Synced {
            self.last_success_at = Some(chrono::Utc::now().timestamp());
        }
        self.status = status;
    }

    pub fn report_transport_state(&mut self, status: SyncStatus, last_error: Option<String>) {
        let becoming_active = matches!(status, SyncStatus::Synced | SyncStatus::Syncing);
        self.set_status(status);
        self.last_error = last_error;
        if becoming_active {
            self.flush_offline_queue();
        }
    }

    pub fn config(&self) -> &SyncConfig {
        &self.config
    }

    pub fn update_config(&mut self, config: SyncConfig) {
        self.config = config;
    }

    pub fn set_active_profile_id(&mut self, profile_id: Option<String>) {
        self.active_profile_id = profile_id.filter(|value| !value.is_empty());
    }

    pub fn active_profile_id(&self) -> Option<&str> {
        self.active_profile_id.as_deref()
    }

    pub fn register_profile_scope(
        &mut self,
        entity_type: SyncEntityType,
        entity_id: String,
        profile_id: String,
    ) {
        if !entity_type.is_profile_scoped() || profile_id.is_empty() {
            return;
        }
        self.entity_profiles.insert(
            Self::raw_scope_key(&entity_type, &entity_id),
            profile_id.clone(),
        );
        if entity_type == SyncEntityType::Space {
            self.space_profiles.insert(entity_id, profile_id);
        }
    }

    pub fn profile_id_for_entity(
        &self,
        entity_type: &SyncEntityType,
        entity_id: &str,
    ) -> Option<&str> {
        self.entity_profiles
            .get(&Self::raw_scope_key(entity_type, entity_id))
            .map(String::as_str)
    }

    pub fn set_last_pulled_version(&mut self, entity_type: SyncEntityType, version: u64) {
        self.last_pulled_versions.insert(entity_type, version);
    }

    pub fn set_last_pulled_version_for_profile(
        &mut self,
        entity_type: SyncEntityType,
        profile_id: String,
        version: u64,
    ) {
        self.profile_last_pulled_versions
            .insert((entity_type, profile_id), version);
    }

    pub fn set_entity_version_in_cache(
        &mut self,
        key: &str,
        version: u64,
        device_id: u32,
        fields_hlc_json: Option<String>,
        payload_json: Option<String>,
    ) {
        self.entity_versions.put(
            key.to_string(),
            (version, device_id, fields_hlc_json, payload_json),
        );
    }

    pub fn remove_entity_version_from_cache(&mut self, key: &str) {
        self.entity_versions.pop(key);
    }

    fn raw_scope_key(entity_type: &SyncEntityType, entity_id: &str) -> String {
        format!("{}:{}", entity_type.as_str(), entity_id)
    }

    fn remember_scope(&mut self, entity: &SyncEntity, profile_id: &str) {
        self.entity_profiles.insert(
            Self::raw_scope_key(&entity.entity_type, &entity.entity_id),
            profile_id.to_string(),
        );
        if entity.entity_type == SyncEntityType::Space {
            self.space_profiles
                .insert(entity.entity_id.clone(), profile_id.to_string());
        }
    }

    fn tab_space_id(entity: &SyncEntity) -> Option<String> {
        if entity.entity_type != SyncEntityType::Tab {
            return None;
        }
        let value = serde_json::from_str::<serde_json::Value>(&entity.payload_json).ok()?;
        let space_id = value
            .get("spaceId")
            .or_else(|| value.get("space_id"))
            .and_then(serde_json::Value::as_str)
            .filter(|v| !v.is_empty())?;
        Some(space_id.to_string())
    }

    fn inject_profile_scope(entity: &mut SyncEntity, profile_id: &str) -> bool {
        if !entity.entity_type.is_profile_scoped() {
            return true;
        }
        let Ok(mut value) = serde_json::from_str::<serde_json::Value>(&entity.payload_json) else {
            return false;
        };
        let Some(object) = value.as_object_mut() else {
            return false;
        };
        object.insert(
            "profileId".to_string(),
            serde_json::Value::String(profile_id.to_string()),
        );
        let Ok(payload_json) = serde_json::to_string(&value) else {
            return false;
        };
        entity.profile_id = Some(profile_id.to_string());
        entity.payload_json = payload_json;
        // Preserve the author's per-field clocks. Fields without a supplied
        // clock (including an injected profileId) use the entity clock at merge.
        true
    }

    /// Bind a local Tab/Space to a profile before it enters an encrypted wire
    /// envelope. Domain Space payloads already contain profileId. Tab payloads
    /// are resolved by spaceId, previously-observed entity scope, then the
    /// explicitly bound active profile. Tombstones reuse the remembered scope.
    fn bind_local_profile_scope(&mut self, entity: &mut SyncEntity) -> bool {
        if !entity.entity_type.is_profile_scoped() {
            return true;
        }
        let explicit_profile = entity.profile_id.clone().filter(|value| !value.is_empty());
        let payload_profile =
            SyncEntity::profile_id_from_payload(&entity.entity_type, &entity.payload_json);
        if matches!(
            (explicit_profile.as_deref(), payload_profile.as_deref()),
            (Some(explicit), Some(payload)) if explicit != payload
        ) {
            return false;
        }
        let resolved = explicit_profile
            .or(payload_profile)
            .or_else(|| {
                Self::tab_space_id(entity)
                    .and_then(|space_id| self.space_profiles.get(&space_id).cloned())
            })
            .or_else(|| {
                self.entity_profiles
                    .get(&Self::raw_scope_key(&entity.entity_type, &entity.entity_id))
                    .cloned()
            })
            .or_else(|| self.active_profile_id.clone());
        let Some(profile_id) = resolved else {
            return false;
        };
        if !Self::inject_profile_scope(entity, &profile_id) {
            return false;
        }
        self.remember_scope(entity, &profile_id);
        true
    }

    fn bind_remote_profile_scope(&mut self, entity: &mut SyncEntity) -> bool {
        if !entity.entity_type.is_profile_scoped() {
            return true;
        }
        let explicit_profile = entity.profile_id.clone().filter(|value| !value.is_empty());
        let payload_profile =
            SyncEntity::profile_id_from_payload(&entity.entity_type, &entity.payload_json);
        if matches!(
            (explicit_profile.as_deref(), payload_profile.as_deref()),
            (Some(explicit), Some(payload)) if explicit != payload
        ) {
            return false;
        }
        let resolved = explicit_profile
            .or(payload_profile)
            .or_else(|| {
                Self::tab_space_id(entity)
                    .and_then(|space_id| self.space_profiles.get(&space_id).cloned())
            })
            .or_else(|| {
                self.entity_profiles
                    .get(&Self::raw_scope_key(&entity.entity_type, &entity.entity_id))
                    .cloned()
            });
        let Some(profile_id) = resolved else {
            return false;
        };
        if !Self::inject_profile_scope(entity, &profile_id) {
            return false;
        }
        self.remember_scope(entity, &profile_id);
        true
    }

    fn profile_for_pull(&self, entity_type: &Option<SyncEntityType>) -> Option<String> {
        entity_type
            .as_ref()
            .filter(|entity_type| entity_type.is_profile_scoped())
            .and(self.active_profile_id.clone())
    }

    fn record_last_pulled(
        &mut self,
        entity: &SyncEntity,
        storage: &Option<maho_storage::sqlite::SqliteStorage>,
    ) {
        let entity_type = entity.entity_type.clone();
        if entity_type.is_profile_scoped() {
            let Some(profile_id) = entity.profile_scope() else {
                return;
            };
            let last_pulled = self
                .profile_last_pulled_versions
                .entry((entity_type.clone(), profile_id.clone()))
                .or_insert(0);
            *last_pulled = (*last_pulled).max(entity.version);
            if let Some(storage) = storage {
                let key = entity_type.cursor_storage_key(Some(&profile_id));
                if let Err(error) = storage.set_sync_last_pulled_version(&key, *last_pulled) {
                    eprintln!("[sync] Failed to persist cursor {key}: {error}");
                }
            }
        } else {
            let last_pulled = self
                .last_pulled_versions
                .entry(entity_type.clone())
                .or_insert(0);
            *last_pulled = (*last_pulled).max(entity.version);
            if let Some(storage) = storage {
                if let Err(error) =
                    storage.set_sync_last_pulled_version(entity_type.as_str(), *last_pulled)
                {
                    eprintln!(
                        "[sync] Failed to persist cursor {}: {}",
                        entity_type.as_str(),
                        error
                    );
                }
            }
        }
    }

    pub fn prepare_local_entity(
        &mut self,
        entity: &mut SyncEntity,
    ) -> Result<(), SyncTransportError> {
        if self.bind_local_profile_scope(entity) {
            Ok(())
        } else {
            Err(SyncTransportError::MissingProfileScope {
                entity_type: entity.entity_type.as_str(),
                entity_id: entity.entity_id.clone(),
            })
        }
    }

    pub fn prepare_remote_entity(
        &mut self,
        entity: &mut SyncEntity,
    ) -> Result<(), SyncTransportError> {
        if self.bind_remote_profile_scope(entity) {
            Ok(())
        } else {
            Err(SyncTransportError::MissingProfileScope {
                entity_type: entity.entity_type.as_str(),
                entity_id: entity.entity_id.clone(),
            })
        }
    }

    pub fn push_entity(&mut self, mut entity: SyncEntity) {
        if let Err(error) = self.prepare_local_entity(&mut entity) {
            self.record_transport_error(error);
            return;
        }

        if let Some(cache_key) = entity.cache_key() {
            self.entity_versions.put(
                cache_key,
                (
                    entity.version,
                    entity.device_id,
                    entity.fields_hlc_json.clone(),
                    Some(entity.payload_json.clone()),
                ),
            );
        }

        if matches!(self.status, SyncStatus::Synced | SyncStatus::Syncing) {
            let queue_row_id = entity.queue_row_id;
            let message = SyncMessage::EntityPush { entity };
            match self.encrypt_message(&message, SyncTransportOp::Push) {
                Ok(data) => {
                    self.outgoing_messages.push(QueuedSyncMessage {
                        message: SyncMessage::Encrypted { data },
                        queue_row_id,
                    });
                    if let Some(row_id) = queue_row_id {
                        self.sent_entities.push(row_id);
                    }
                }
                Err(error) => self.record_transport_error(error),
            }
        } else {
            self.pending_pushes.push(entity);
        }
    }

    fn encrypt_message(
        &self,
        message: &SyncMessage,
        operation: SyncTransportOp,
    ) -> Result<Vec<u8>, SyncTransportError> {
        let key = self
            .encryption_key
            .as_ref()
            .ok_or(SyncTransportError::KeylessTransportDenied { operation })?;
        let plaintext =
            serde_json::to_vec(message).map_err(|error| SyncTransportError::Serialize {
                detail: error.to_string(),
            })?;
        crate::sync_crypto::encrypt_update(&plaintext, key)
            .map_err(|detail| SyncTransportError::Encrypt { detail })
    }

    pub fn pull_entities(&mut self, entity_type: Option<SyncEntityType>) {
        let profile_id = self.profile_for_pull(&entity_type);
        let since_version = match (&entity_type, profile_id.as_deref()) {
            (Some(entity_type), Some(profile_id)) => self
                .profile_last_pulled_versions
                .get(&(entity_type.clone(), profile_id.to_string()))
                .copied()
                .unwrap_or(0),
            (Some(entity_type), None) => self
                .last_pulled_versions
                .get(entity_type)
                .copied()
                .unwrap_or(0),
            (None, _) => 0,
        };
        let message = SyncMessage::EntityPull {
            entity_type,
            since_version,
        };
        match self.encrypt_message(&message, SyncTransportOp::Pull) {
            Ok(data) => self.outgoing_messages.push(QueuedSyncMessage {
                message: SyncMessage::Encrypted { data },
                queue_row_id: None,
            }),
            Err(error) => self.record_transport_error(error),
        }
    }

    pub fn apply_remote_entity(
        &mut self,
        entity: &mut SyncEntity,
        storage: &Option<maho_storage::sqlite::SqliteStorage>,
    ) -> bool {
        if entity.schema_version > 1 {
            self.record_transport_error(SyncTransportError::UnsupportedSchemaVersion {
                entity_id: entity.entity_id.clone(),
                schema_version: entity.schema_version,
            });
            return false;
        }
        if let Err(error) = self.prepare_remote_entity(entity) {
            self.record_transport_error(error);
            return false;
        }
        let entity_type_str = entity.entity_type.as_str();
        let Some(storage_entity_id) = entity.storage_entity_id() else {
            return false;
        };
        let Some(cache_key) = entity.cache_key() else {
            return false;
        };

        if let Some(storage) = storage {
            match storage.get_sync_tombstone(entity_type_str, &storage_entity_id) {
                Ok(Some(tombstone))
                    if !entity.deleted && tombstone >= (entity.version, entity.device_id) =>
                {
                    return false;
                }
                Ok(_) => {}
                Err(error) => {
                    self.record_transport_error(SyncTransportError::DatabaseRead {
                        detail: error.to_string(),
                    });
                    return false;
                }
            }
        }

        let local_version = if let Some(version) = self.entity_versions.get(&cache_key) {
            Some(version.clone())
        } else if let Some(storage) = storage {
            match crate::sync_models::load_persisted_entity_version(
                storage,
                &entity.entity_type,
                &entity.entity_id,
                entity.profile_id.as_deref(),
            ) {
                Ok(Some((hlc_ts, device_id, fields_hlc_json, payload_json))) => {
                    let value = (hlc_ts, device_id, fields_hlc_json, payload_json);
                    self.entity_versions.put(cache_key.clone(), value.clone());
                    Some(value)
                }
                Ok(None) => None,
                Err(error) => {
                    self.record_transport_error(SyncTransportError::DatabaseRead {
                        detail: error.to_string(),
                    });
                    return false;
                }
            }
        } else {
            None
        };

        if entity.deleted {
            let should_apply = local_version
                .as_ref()
                .is_none_or(|(hlc_ts, device_id, _, _)| {
                    (entity.version, entity.device_id) > (*hlc_ts, *device_id)
                });
            if !should_apply {
                return false;
            }
            self.entity_versions.put(
                cache_key,
                (
                    entity.version,
                    entity.device_id,
                    None,
                    Some(entity.payload_json.clone()),
                ),
            );
            if let Some(storage) = storage {
                if storage
                    .save_entity_version(
                        entity_type_str,
                        &storage_entity_id,
                        entity.version,
                        entity.device_id,
                        None,
                        Some(&entity.payload_json),
                    )
                    .is_err()
                {
                    return false;
                }
                if storage
                    .save_sync_tombstone(
                        entity_type_str,
                        &storage_entity_id,
                        entity.version,
                        entity.device_id,
                    )
                    .is_err()
                {
                    return false;
                }
            }
            self.record_last_pulled(entity, storage);
            return true;
        }

        let mut should_apply = local_version.is_none();
        let mut final_payload = entity.payload_json.clone();
        let mut final_fields_hlc = entity.fields_hlc_json.clone();

        if let Some((local_hlc, local_device, local_fields_json, local_payload_json)) =
            local_version
        {
            type FieldsHlc = HashMap<String, (u64, u32)>;
            let local_fields: FieldsHlc = local_fields_json
                .as_deref()
                .and_then(|json| serde_json::from_str(json).ok())
                .unwrap_or_default();
            let remote_fields: FieldsHlc = entity
                .fields_hlc_json
                .as_deref()
                .and_then(|json| serde_json::from_str(json).ok())
                .unwrap_or_default();
            let local_value = local_payload_json
                .as_deref()
                .and_then(|json| serde_json::from_str::<serde_json::Value>(json).ok())
                .unwrap_or(serde_json::Value::Null);
            let remote_value = serde_json::from_str::<serde_json::Value>(&entity.payload_json)
                .unwrap_or(serde_json::Value::Null);

            if let (serde_json::Value::Object(local), serde_json::Value::Object(remote)) =
                (local_value, remote_value)
            {
                let mut merged = serde_json::Map::new();
                let mut merged_fields = FieldsHlc::new();
                let keys: std::collections::HashSet<&String> =
                    local.keys().chain(remote.keys()).collect();
                if (entity.version, entity.device_id) > (local_hlc, local_device) {
                    should_apply = true;
                }
                for key in keys {
                    match (local.get(key), remote.get(key)) {
                        (Some(local_value), Some(remote_value)) => {
                            let local_version = local_fields
                                .get(key)
                                .copied()
                                .unwrap_or((local_hlc, local_device));
                            let remote_version = remote_fields
                                .get(key)
                                .copied()
                                .unwrap_or((entity.version, entity.device_id));
                            if remote_version > local_version {
                                merged.insert(key.clone(), remote_value.clone());
                                merged_fields.insert(key.clone(), remote_version);
                                if local_value != remote_value {
                                    should_apply = true;
                                }
                            } else {
                                merged.insert(key.clone(), local_value.clone());
                                merged_fields.insert(key.clone(), local_version);
                            }
                        }
                        (Some(local_value), None) => {
                            let version = local_fields
                                .get(key)
                                .copied()
                                .unwrap_or((local_hlc, local_device));
                            merged.insert(key.clone(), local_value.clone());
                            merged_fields.insert(key.clone(), version);
                        }
                        (None, Some(remote_value)) => {
                            let version = remote_fields
                                .get(key)
                                .copied()
                                .unwrap_or((entity.version, entity.device_id));
                            merged.insert(key.clone(), remote_value.clone());
                            merged_fields.insert(key.clone(), version);
                            should_apply = true;
                        }
                        (None, None) => {}
                    }
                }
                if should_apply {
                    if let Ok(payload) = serde_json::to_string(&merged) {
                        final_payload = payload;
                    }
                    if let Ok(fields) = serde_json::to_string(&merged_fields) {
                        final_fields_hlc = Some(fields);
                    }
                }
            } else {
                should_apply = (entity.version, entity.device_id) > (local_hlc, local_device);
            }

            if should_apply {
                let final_version = entity.version.max(local_hlc);
                entity.device_id = if final_version == entity.version {
                    entity.device_id
                } else {
                    local_device
                };
                entity.version = final_version;
            }
        }

        if !should_apply {
            return false;
        }

        entity.payload_json = final_payload.clone();
        entity.fields_hlc_json = final_fields_hlc.clone();
        self.entity_versions.put(
            cache_key,
            (
                entity.version,
                entity.device_id,
                final_fields_hlc.clone(),
                Some(final_payload.clone()),
            ),
        );
        if let Some(storage) = storage {
            if storage
                .save_entity_version(
                    entity_type_str,
                    &storage_entity_id,
                    entity.version,
                    entity.device_id,
                    final_fields_hlc.as_deref(),
                    Some(&final_payload),
                )
                .is_err()
            {
                return false;
            }
            if storage
                .remove_sync_tombstone(
                    entity_type_str,
                    &storage_entity_id,
                    entity.version,
                    entity.device_id,
                )
                .is_err()
            {
                return false;
            }
        }
        self.record_last_pulled(entity, storage);
        true
    }

    pub fn apply_remote_batch(
        &mut self,
        entities: Vec<SyncEntity>,
        storage: &Option<maho_storage::sqlite::SqliteStorage>,
    ) -> Vec<SyncEntity> {
        let mut applied = Vec::new();
        for mut entity in entities {
            if self.apply_remote_entity(&mut entity, storage) {
                applied.push(entity);
            }
        }
        applied
    }

    pub fn get_pending_pushes(&self) -> &[SyncEntity] {
        &self.pending_pushes
    }

    pub fn drain_pending_pushes(&mut self) -> Vec<SyncEntity> {
        std::mem::take(&mut self.pending_pushes)
    }

    pub fn handle_incoming_message(&mut self, message: SyncMessage) {
        self.handle_incoming_message_impl(message, false);
    }

    pub fn handle_authenticated_message(&mut self, message: SyncMessage) {
        self.handle_incoming_message_impl(message, true);
    }

    fn handle_incoming_message_impl(&mut self, message: SyncMessage, authenticated: bool) {
        match message {
            SyncMessage::Encrypted { data } => {
                let outcome = match self.encryption_key.as_ref() {
                    Some(key) => crate::sync_crypto::decrypt_update(&data, key)
                        .map_err(|detail| SyncTransportError::Decrypt { detail })
                        .and_then(|plaintext| {
                            serde_json::from_slice::<SyncMessage>(&plaintext).map_err(|error| {
                                SyncTransportError::MalformedEnvelopeContents {
                                    detail: error.to_string(),
                                }
                            })
                        }),
                    None => Err(SyncTransportError::MissingEncryptionKey),
                };
                match outcome {
                    Ok(inner) => self.handle_incoming_message_impl(inner, true),
                    Err(error) => {
                        self.status = SyncStatus::Error;
                        self.record_transport_error(error);
                    }
                }
            }
            SyncMessage::EntityPush { entity } => {
                if !authenticated {
                    self.record_transport_error(SyncTransportError::PlaintextPayloadRejected {
                        payload: SyncPayloadKind::EntityPush,
                    });
                    return;
                }
                #[cfg(test)]
                {
                    let mut entity = entity;
                    self.apply_remote_entity(&mut entity, &None);
                }
                #[cfg(not(test))]
                {
                    let _ = entity;
                    self.last_error = Some(
                        "EntityPush must be applied via MahoCore::apply_sync_remote_entities"
                            .to_string(),
                    );
                }
            }
            SyncMessage::EntityBatch { entities } => {
                if !authenticated {
                    self.record_transport_error(SyncTransportError::PlaintextPayloadRejected {
                        payload: SyncPayloadKind::EntityBatch,
                    });
                    return;
                }
                #[cfg(test)]
                self.apply_remote_batch(entities, &None);
                #[cfg(not(test))]
                {
                    let _ = entities;
                    self.last_error = Some(
                        "EntityBatch must be applied via MahoCore::apply_sync_remote_entities"
                            .to_string(),
                    );
                }
            }
            SyncMessage::Ack {
                entity_id,
                entity_type,
                version,
                device_id,
            } => {
                // The legacy ACK frame does not carry a profile partition. Never
                // let it overwrite a profile-local LWW cache entry. Durable relay
                // delivery ACKs use RelayAckV2 and queue row delivery IDs instead.
                if !entity_type.is_profile_scoped() {
                    let key = Self::raw_scope_key(&entity_type, &entity_id);
                    self.entity_versions
                        .put(key, (version, device_id, None, None));
                }
            }
            SyncMessage::EntityPull { .. } => {}
            SyncMessage::SendTab {
                url,
                title,
                sender_device,
                ..
            } => {
                if !authenticated {
                    self.record_transport_error(SyncTransportError::PlaintextPayloadRejected {
                        payload: SyncPayloadKind::SendTab,
                    });
                    return;
                }
                let sender_name = self
                    .devices
                    .get(&sender_device)
                    .map(|device| device.name.clone())
                    .unwrap_or_else(|| "Unknown".to_string());
                let sender_device_type = self
                    .devices
                    .get(&sender_device)
                    .map(|device| device.device_type.clone())
                    .unwrap_or(crate::sync_models::DeviceType::Unknown);
                self.received_tabs.push(ReceivedTab {
                    url,
                    title,
                    sender_name,
                    sender_device_type,
                    received_at: chrono::Utc::now().to_rfc3339(),
                });
                if self.received_tabs.len() > MAX_RECEIVED_TABS {
                    let excess = self.received_tabs.len() - MAX_RECEIVED_TABS;
                    self.received_tabs.drain(0..excess);
                }
            }
            SyncMessage::DeviceJoined { device } => {
                self.devices.insert(device.id.clone(), device);
            }
            SyncMessage::DeviceLeft { device_id } => {
                self.devices.remove(&device_id);
            }
            SyncMessage::Ping => self.outgoing_messages.push(QueuedSyncMessage {
                message: SyncMessage::Pong,
                queue_row_id: None,
            }),
            SyncMessage::Pong => {}
        }
    }

    pub fn send_tab(&mut self, url: &str, title: &str, target_device_id: &str) {
        let message = SyncMessage::SendTab {
            url: url.to_string(),
            title: title.to_string(),
            sender_device: self.config.device_id.clone(),
            target_device: target_device_id.to_string(),
        };
        match self.encrypt_message(&message, SyncTransportOp::SendTab) {
            Ok(data) => self.outgoing_messages.push(QueuedSyncMessage {
                message: SyncMessage::Encrypted { data },
                queue_row_id: None,
            }),
            Err(error) => self.record_transport_error(error),
        }
    }

    pub fn drain_outgoing_messages(&mut self) -> Vec<SyncMessage> {
        self.drain_outgoing_messages_with_queue_rows()
            .into_iter()
            .map(|queued| queued.message)
            .collect()
    }

    pub(crate) fn drain_outgoing_messages_with_queue_rows(&mut self) -> Vec<QueuedSyncMessage> {
        std::mem::take(&mut self.outgoing_messages)
    }

    pub(crate) fn restore_outgoing_messages(&mut self, mut messages: Vec<QueuedSyncMessage>) {
        if messages.is_empty() {
            return;
        }
        std::mem::swap(&mut self.outgoing_messages, &mut messages);
        self.outgoing_messages.extend(messages);
    }

    pub fn drain_sent_entities(&mut self) -> Vec<i64> {
        std::mem::take(&mut self.sent_entities)
    }

    pub fn drain_received_tabs(&mut self) -> Vec<ReceivedTab> {
        std::mem::take(&mut self.received_tabs)
    }

    pub fn get_devices(&self) -> Vec<&DeviceInfo> {
        self.devices.values().collect()
    }

    pub fn remove_device(&mut self, device_id: &str) {
        self.devices.remove(device_id);
        self.outgoing_messages.push(QueuedSyncMessage {
            message: SyncMessage::DeviceLeft {
                device_id: device_id.to_string(),
            },
            queue_row_id: None,
        });
    }

    pub fn disconnect_device(&mut self, device_id: &str) -> bool {
        if device_id == self.config.device_id {
            return false;
        }
        if self.devices.remove(device_id).is_none() {
            return false;
        }
        self.outgoing_messages.push(QueuedSyncMessage {
            message: SyncMessage::DeviceLeft {
                device_id: device_id.to_string(),
            },
            queue_row_id: None,
        });
        true
    }

    pub fn rename_device(&mut self, device_id: &str, new_name: String) -> bool {
        if device_id == self.config.device_id {
            return false;
        }
        let Some(device) = self.devices.get_mut(device_id) else {
            return false;
        };
        device.name = new_name;
        self.outgoing_messages.push(QueuedSyncMessage {
            message: SyncMessage::DeviceJoined {
                device: device.clone(),
            },
            queue_row_id: None,
        });
        true
    }

    pub fn flush_offline_queue(&mut self) {
        let pending = self.drain_pending_pushes();
        for entity in pending {
            self.push_entity(entity);
        }
    }

    pub fn set_entity_sync_enabled(&mut self, entity_type: SyncEntityType, enabled: bool) {
        if enabled {
            self.config.disabled_entity_types.remove(&entity_type);
        } else {
            self.config.disabled_entity_types.insert(entity_type);
        }
    }

    pub fn is_leader(&self) -> bool {
        if !matches!(self.status, SyncStatus::Synced | SyncStatus::Syncing) {
            return false;
        }
        let local_hash = hash_device_id(&self.config.device_id);
        self.devices
            .values()
            .map(|device| hash_device_id(&device.id))
            .fold(local_hash, u32::min)
            == local_hash
    }
}

fn hash_device_id(device_id: &str) -> u32 {
    let bytes = uuid::Uuid::parse_str(device_id)
        .map(|uuid| uuid.into_bytes().to_vec())
        .unwrap_or_else(|_| device_id.as_bytes().to_vec());
    crate::account_manager::fnv1a_bytes(&bytes)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::sync_crypto;
    use crate::sync_models::DeviceType;

    fn config() -> SyncConfig {
        SyncConfig {
            server_url: "wss://test.example.com".to_string(),
            sync_key: None,
            device_id: "test-device-1".to_string(),
            device_name: "Test Device".to_string(),
            auto_sync: true,
            sync_interval_secs: 30,
            disabled_entity_types: std::collections::HashSet::new(),
        }
    }

    fn key() -> sync_crypto::EncryptionKey {
        let seed = sync_crypto::generate_sync_seed();
        sync_crypto::derive_encryption_key(&seed).unwrap()
    }

    fn tab(id: &str, profile: &str, version: u64) -> SyncEntity {
        let payload_json = serde_json::json!({
            "id": id,
            "spaceId": format!("space-{profile}"),
            "profileId": profile,
            "url": "https://example.com"
        })
        .to_string();
        SyncEntity {
            entity_type: SyncEntityType::Tab,
            entity_id: id.to_string(),
            profile_id: None,
            version,
            device_id: 1,
            schema_version: 1,
            modified_at: 1,
            fields_hlc_json: crate::sync_models::build_fields_hlc(&payload_json, version, 1),
            payload_json,
            deleted: false,
            queue_row_id: None,
        }
    }

    fn space(id: &str, profile: &str, version: u64) -> SyncEntity {
        let payload_json = serde_json::json!({
            "id": id,
            "profileId": profile,
            "name": profile
        })
        .to_string();
        SyncEntity {
            entity_type: SyncEntityType::Space,
            entity_id: id.to_string(),
            profile_id: None,
            version,
            device_id: 1,
            schema_version: 1,
            modified_at: 1,
            fields_hlc_json: crate::sync_models::build_fields_hlc(&payload_json, version, 1),
            payload_json,
            deleted: false,
            queue_row_id: None,
        }
    }

    #[test]
    fn same_raw_tab_id_is_independent_across_profiles() {
        let mut manager = SyncManager::new(config());
        let mut a = tab("same", "a", 10);
        let mut b = tab("same", "b", 1);
        assert!(manager.apply_remote_entity(&mut a, &None));
        assert!(manager.apply_remote_entity(&mut b, &None));
        assert!(manager
            .entity_versions
            .peek(&a.cache_key().unwrap())
            .is_some());
        assert!(manager
            .entity_versions
            .peek(&b.cache_key().unwrap())
            .is_some());
    }

    #[test]
    fn same_raw_space_id_is_independent_across_profiles() {
        let mut manager = SyncManager::new(config());
        let mut a = space("same", "a", 10);
        let mut b = space("same", "b", 1);
        assert!(manager.apply_remote_entity(&mut a, &None));
        assert!(manager.apply_remote_entity(&mut b, &None));
    }

    #[test]
    fn remote_profile_scoped_entity_without_scope_is_rejected() {
        let mut manager = SyncManager::new(config());
        let mut entity = tab("missing", "a", 1);
        entity.payload_json = serde_json::json!({"id":"missing","spaceId":"unknown"}).to_string();
        assert!(!manager.apply_remote_entity(&mut entity, &None));
        assert!(matches!(
            manager.last_transport_error(),
            Some(SyncTransportError::MissingProfileScope { .. })
        ));
    }

    #[test]
    fn local_tab_is_enriched_from_active_profile_before_encryption() {
        let mut manager = SyncManager::new(config());
        manager.set_active_profile_id(Some("work".to_string()));
        manager.set_status(SyncStatus::Synced);
        manager.set_encryption_key(key());
        let mut entity = tab("local", "work", 1);
        entity.payload_json = serde_json::json!({"id":"local","spaceId":"s"}).to_string();
        manager.push_entity(entity);
        let SyncMessage::Encrypted { data } = manager.drain_outgoing_messages().remove(0) else {
            panic!("expected encrypted push");
        };
        let plaintext =
            sync_crypto::decrypt_update(&data, manager.encryption_key.as_ref().unwrap()).unwrap();
        let SyncMessage::EntityPush { entity } = serde_json::from_slice(&plaintext).unwrap() else {
            panic!("expected entity push");
        };
        assert_eq!(entity.profile_scope().as_deref(), Some("work"));
    }

    #[test]
    fn local_tab_is_enriched_from_space_profile() {
        let mut manager = SyncManager::new(config());
        let mut remote_space = space("work-space", "work", 1);
        assert!(manager.apply_remote_entity(&mut remote_space, &None));
        manager.set_status(SyncStatus::Synced);
        manager.set_encryption_key(key());
        let mut entity = tab("local", "work", 2);
        entity.payload_json = serde_json::json!({"id":"local","spaceId":"work-space"}).to_string();
        manager.push_entity(entity);
        let SyncMessage::Encrypted { data } = manager.drain_outgoing_messages().remove(0) else {
            panic!("expected encrypted push");
        };
        let plaintext =
            sync_crypto::decrypt_update(&data, manager.encryption_key.as_ref().unwrap()).unwrap();
        let SyncMessage::EntityPush { entity } = serde_json::from_slice(&plaintext).unwrap() else {
            panic!("expected entity push");
        };
        assert_eq!(entity.profile_scope().as_deref(), Some("work"));
    }

    #[test]
    fn local_tombstone_reuses_remembered_profile() {
        let mut manager = SyncManager::new(config());
        manager.set_active_profile_id(Some("work".to_string()));
        manager.push_entity(tab("gone", "work", 1));
        manager.set_active_profile_id(Some("personal".to_string()));
        let mut tombstone = tab("gone", "work", 2);
        tombstone.deleted = true;
        tombstone.payload_json = "{}".to_string();
        manager.push_entity(tombstone);
        let pending = manager.get_pending_pushes();
        assert_eq!(
            pending.last().unwrap().profile_scope().as_deref(),
            Some("work")
        );
    }

    #[test]
    fn profile_cursor_is_selected_without_changing_pull_wire_shape() {
        let mut manager = SyncManager::new(config());
        manager.set_active_profile_id(Some("b".to_string()));
        manager.set_last_pulled_version_for_profile(SyncEntityType::Tab, "b".to_string(), 42);
        manager.set_encryption_key(key());
        manager.pull_entities(Some(SyncEntityType::Tab));
        let SyncMessage::Encrypted { data } = manager.drain_outgoing_messages().remove(0) else {
            panic!("expected encrypted pull");
        };
        let plaintext =
            sync_crypto::decrypt_update(&data, manager.encryption_key.as_ref().unwrap()).unwrap();
        match serde_json::from_slice::<SyncMessage>(&plaintext).unwrap() {
            SyncMessage::EntityPull {
                entity_type,
                since_version,
            } => {
                assert_eq!(entity_type, Some(SyncEntityType::Tab));
                assert_eq!(since_version, 42);
            }
            other => panic!("unexpected message: {other:?}"),
        }
    }

    #[test]
    fn profile_scoped_legacy_ack_does_not_create_raw_lww_entry() {
        let mut manager = SyncManager::new(config());
        manager.handle_incoming_message(SyncMessage::Ack {
            entity_id: "same".to_string(),
            entity_type: SyncEntityType::Tab,
            version: 9,
            device_id: 4,
        });
        assert!(manager.entity_versions.peek("tab:same").is_none());
    }

    #[test]
    fn push_and_send_tab_fail_closed_without_e2ee_key() {
        let mut manager = SyncManager::new(config());
        manager.set_active_profile_id(Some("a".to_string()));
        manager.set_status(SyncStatus::Synced);
        manager.push_entity(tab("x", "a", 1));
        assert!(manager.drain_outgoing_messages().is_empty());
        assert!(matches!(
            manager.last_transport_error(),
            Some(SyncTransportError::KeylessTransportDenied {
                operation: SyncTransportOp::Push
            })
        ));
        manager.send_tab("https://example.com", "Example", "peer");
        assert!(matches!(
            manager.last_transport_error(),
            Some(SyncTransportError::KeylessTransportDenied {
                operation: SyncTransportOp::SendTab
            })
        ));
    }

    #[test]
    fn plaintext_entity_push_is_rejected() {
        let mut manager = SyncManager::new(config());
        manager.handle_incoming_message(SyncMessage::EntityPush {
            entity: tab("plain", "a", 1),
        });
        assert!(matches!(
            manager.last_transport_error(),
            Some(SyncTransportError::PlaintextPayloadRejected { .. })
        ));
    }

    #[test]
    fn encrypted_send_tab_is_authenticated() {
        let mut manager = SyncManager::new(config());
        manager.set_encryption_key(key());
        manager.devices.insert(
            "peer".to_string(),
            DeviceInfo {
                id: "peer".to_string(),
                name: "Peer".to_string(),
                device_type: DeviceType::Mac,
                last_seen: "now".to_string(),
                is_online: true,
            },
        );
        let inner = SyncMessage::SendTab {
            url: "https://example.com".to_string(),
            title: "Example".to_string(),
            sender_device: "peer".to_string(),
            target_device: "test-device-1".to_string(),
        };
        let plaintext = serde_json::to_vec(&inner).unwrap();
        let data =
            sync_crypto::encrypt_update(&plaintext, manager.encryption_key.as_ref().unwrap())
                .unwrap();
        manager.handle_incoming_message(SyncMessage::Encrypted { data });
        assert_eq!(manager.drain_received_tabs().len(), 1);
    }

    #[test]
    fn received_tabs_are_bounded() {
        let mut manager = SyncManager::new(config());
        for index in 0..(MAX_RECEIVED_TABS + 5) {
            manager.handle_authenticated_message(SyncMessage::SendTab {
                url: format!("https://example.com/{index}"),
                title: index.to_string(),
                sender_device: "peer".to_string(),
                target_device: "local".to_string(),
            });
        }
        assert_eq!(manager.drain_received_tabs().len(), MAX_RECEIVED_TABS);
    }

    #[test]
    fn offline_queue_flushes_after_sync_connects() {
        let mut manager = SyncManager::new(config());
        manager.set_active_profile_id(Some("a".to_string()));
        manager.push_entity(tab("queued", "a", 1));
        assert_eq!(manager.get_pending_pushes().len(), 1);
        manager.set_encryption_key(key());
        manager.report_transport_state(SyncStatus::Synced, None);
        assert!(manager.get_pending_pushes().is_empty());
        assert_eq!(manager.drain_outgoing_messages().len(), 1);
    }

    #[test]
    fn leader_requires_active_sync_state() {
        let mut manager = SyncManager::new(config());
        assert!(!manager.is_leader());
        manager.set_status(SyncStatus::Synced);
        assert!(manager.is_leader());
    }
}
