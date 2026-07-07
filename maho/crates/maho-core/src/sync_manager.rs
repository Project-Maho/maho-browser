//! SyncManager — orchestrates cross-device synchronization.
//!
//! Architecture: The SyncManager is synchronous. The shell layer (macOS/iOS/Android)
//! manages the WebSocket connection and feeds messages here. The SyncManager
//! processes LWW (Last-Writer-Wins) entity sync and produces outgoing messages.

use crate::sync_models::{
    DeviceId, DeviceInfo, ReceivedTab, SyncConfig, SyncEntity, SyncEntityType, SyncMessage,
    SyncStatus,
};
use std::collections::HashMap;

/// SyncManager manages LWW entity sync, device tracking, and message queuing.
pub struct SyncManager {
    config: SyncConfig,
    status: SyncStatus,
    last_error: Option<String>,
    devices: HashMap<DeviceId, DeviceInfo>,
    received_tabs: Vec<ReceivedTab>,
    outgoing_messages: Vec<SyncMessage>,
    pub(crate) encryption_key: Option<crate::sync_crypto::EncryptionKey>,
    pending_pushes: Vec<SyncEntity>,
    // Audit finding H12: bounded LRU cap prevents unbounded growth over
    // long-running sync sessions. Capacity chosen as 10k — large enough that
    // even high-churn workspaces won't see eviction pressure under normal
    // operation; small enough to bound memory.
    entity_versions: lru::LruCache<String, (u64, u32)>,
    pub(crate) last_pulled_versions: HashMap<SyncEntityType, u64>,
    sent_entities: Vec<i64>,
}

impl SyncManager {
    pub fn new(config: SyncConfig) -> Self {
        Self {
            config,
            status: SyncStatus::Idle,
            last_error: None,
            devices: HashMap::new(),
            received_tabs: Vec::new(),
            outgoing_messages: Vec::new(),
            encryption_key: None,
            pending_pushes: Vec::new(),
            entity_versions: lru::LruCache::new(std::num::NonZeroUsize::new(10_000).unwrap()),
            last_pulled_versions: HashMap::new(),
            sent_entities: Vec::new(),
        }
    }

    /// Get current sync status
    pub fn status(&self) -> &SyncStatus {
        &self.status
    }

    pub fn last_error(&self) -> Option<&str> {
        self.last_error.as_deref()
    }

    /// Set the E2E encryption key (derived from sync seed)
    pub fn set_encryption_key(&mut self, key: crate::sync_crypto::EncryptionKey) {
        self.encryption_key = Some(key);
    }

    /// Check if encryption is configured
    pub fn has_encryption_key(&self) -> bool {
        self.encryption_key.is_some()
    }

    /// Set sync status (called by shell when connection state changes)
    pub fn set_status(&mut self, status: SyncStatus) {
        self.status = status;
    }

    /// Get current config
    pub fn config(&self) -> &SyncConfig {
        &self.config
    }

    /// Update config
    pub fn update_config(&mut self, config: SyncConfig) {
        self.config = config;
    }

    pub fn set_last_pulled_version(&mut self, entity_type: SyncEntityType, version: u64) {
        self.last_pulled_versions.insert(entity_type, version);
    }

    pub fn set_entity_version_in_cache(&mut self, key: &str, version: u64, device_id: u32) {
        self.entity_versions.put(key.to_string(), (version, device_id));
    }

    // === LWW Entity Sync ===

    /// Queue an outgoing entity push. If syncing/synced, add EntityPush to
    /// outgoing_messages (encrypt if key set). Otherwise add to pending_pushes.
    pub fn push_entity(&mut self, entity: SyncEntity) {
        if self.status == SyncStatus::Synced || self.status == SyncStatus::Syncing {
            let row_id_opt = entity.queue_row_id;
            let message = SyncMessage::EntityPush { entity };

            if let Some(key) = &self.encryption_key {
                match serde_json::to_vec(&message) {
                    Ok(plaintext) => match crate::sync_crypto::encrypt_update(&plaintext, key) {
                        Ok(data) => {
                            self.outgoing_messages.push(SyncMessage::Encrypted { data });
                            if let Some(row_id) = row_id_opt {
                                self.sent_entities.push(row_id);
                            }
                        }
                        Err(e) => {
                            self.last_error = Some(format!("Encrypt error: {}", e));
                        }
                    },
                    Err(e) => {
                        self.last_error = Some(format!("Serialize error: {}", e));
                    }
                }
            } else {
                self.outgoing_messages.push(message);
                if let Some(row_id) = row_id_opt {
                    self.sent_entities.push(row_id);
                }
            }
        } else {
            self.pending_pushes.push(entity);
        }
    }

    /// Send an EntityPull message to request entities newer than given version
    pub fn pull_entities(&mut self, entity_type: Option<SyncEntityType>) {
        let since_version = match &entity_type {
            Some(t) => self.last_pulled_versions.get(t).copied().unwrap_or(0),
            None => 0,
        };
        let message = SyncMessage::EntityPull {
            entity_type,
            since_version,
        };

        if let Some(key) = &self.encryption_key {
            match serde_json::to_vec(&message) {
                Ok(plaintext) => match crate::sync_crypto::encrypt_update(&plaintext, key) {
                    Ok(data) => {
                        self.outgoing_messages.push(SyncMessage::Encrypted { data });
                    }
                    Err(e) => {
                        self.last_error = Some(format!("Encrypt error: {}", e));
                    }
                },
                Err(e) => {
                    self.last_error = Some(format!("Serialize error: {}", e));
                }
            }
        } else {
            self.outgoing_messages.push(message);
        }
    }

    /// Apply a remote entity if its version is newer than the locally tracked version.
    /// Returns true if the entity was applied.
    pub fn apply_remote_entity(
        &mut self,
        entity: &SyncEntity,
        storage: &Option<maho_storage::sqlite::SqliteStorage>,
    ) -> bool {
        if entity.schema_version > 1 {
            self.last_error = Some(format!(
                "Rejected entity {} due to unsupported schema version {}",
                entity.entity_id, entity.schema_version
            ));
            return false;
        }
        let entity_type_str = entity.entity_type.as_str();
        let cache_key = format!("{}:{}", entity_type_str, entity.entity_id);

        let local_version = if let Some(version_pair) = self.entity_versions.get(&cache_key) {
            Some(*version_pair)
        } else if let Some(ref s) = storage {
            match s.get_entity_version(entity_type_str, &entity.entity_id) {
                Ok(Some((hlc_ts, device_id))) => {
                    self.entity_versions.put(cache_key.clone(), (hlc_ts, device_id));
                    Some((hlc_ts, device_id))
                }
                Ok(None) => None,
                Err(e) => {
                    self.last_error = Some(format!("Database read error in apply: {}", e));
                    return false;
                }
            }
        } else {
            None
        };

        let should_apply = match local_version {
            Some((local_hlc_ts, local_device_id)) => {
                (entity.version, entity.device_id) > (local_hlc_ts, local_device_id)
            }
            None => true,
        };

        if should_apply {
            self.entity_versions.put(cache_key.clone(), (entity.version, entity.device_id));
            if let Some(ref s) = storage {
                if let Err(e) = s.save_entity_version(entity_type_str, &entity.entity_id, entity.version, entity.device_id) {
                    eprintln!(
                        "[sync] Failed to persist entity_version for {}:{}: {}",
                        entity_type_str, entity.entity_id, e
                    );
                }
            }
            
            let last_pulled = self.last_pulled_versions.entry(entity.entity_type.clone()).or_insert(0);
            *last_pulled = (*last_pulled).max(entity.version);
            if let Some(ref s) = storage {
                if let Err(e) = s.set_sync_last_pulled_version(entity_type_str, *last_pulled) {
                    eprintln!(
                        "[sync] Failed to persist last_pulled_version for {}: {}",
                        entity_type_str, e
                    );
                }
            }
            return true;
        }
        false
    }

    /// Apply a batch of remote entities. Returns the list of actually-applied entities.
    pub fn apply_remote_batch(
        &mut self,
        entities: Vec<SyncEntity>,
        storage: &Option<maho_storage::sqlite::SqliteStorage>,
    ) -> Vec<SyncEntity> {
        let mut applied = Vec::new();
        for entity in entities {
            if self.apply_remote_entity(&entity, storage) {
                applied.push(entity);
            }
        }
        applied
    }

    /// Get a view of pending entity pushes
    pub fn get_pending_pushes(&self) -> &[SyncEntity] {
        &self.pending_pushes
    }

    /// Take all pending entity pushes
    pub fn drain_pending_pushes(&mut self) -> Vec<SyncEntity> {
        std::mem::take(&mut self.pending_pushes)
    }

    // === Incoming Message Handling ===

    /// Process an incoming sync message
    pub fn handle_incoming_message(&mut self, msg: SyncMessage) {
        match msg {
            SyncMessage::Encrypted { data } => match &self.encryption_key {
                Some(key) => match crate::sync_crypto::decrypt_update(&data, key) {
                    Ok(decrypted) => match serde_json::from_slice::<SyncMessage>(&decrypted) {
                        Ok(inner) => self.handle_incoming_message(inner),
                        Err(e) => {
                            self.status = SyncStatus::Error;
                            self.last_error =
                                Some(format!("Decrypt error: invalid nested message: {}", e));
                        }
                    },
                    Err(e) => {
                        self.status = SyncStatus::Error;
                        self.last_error = Some(format!("Decrypt error: {}", e));
                    }
                },
                None => {
                    self.status = SyncStatus::Error;
                    self.last_error =
                        Some("Encrypted sync message received without key".to_string());
                }
            },
            SyncMessage::EntityPush { entity } => {
                #[cfg(not(test))]
                {
                    let _ = entity;
                    self.last_error = Some(
                        "EntityPush must be applied via MahoCore::apply_sync_remote_entities \
                         (which threads storage); handle_incoming_message path is test-only"
                            .to_string(),
                    );
                }
                #[cfg(test)]
                {
                    self.apply_remote_entity(&entity, &None);
                }
            }
            SyncMessage::EntityBatch { entities } => {
                #[cfg(not(test))]
                {
                    let _ = entities;
                    self.last_error = Some(
                        "EntityBatch must be applied via MahoCore::apply_sync_remote_entities \
                         (which threads storage); handle_incoming_message path is test-only"
                            .to_string(),
                    );
                }
                #[cfg(test)]
                {
                    self.apply_remote_batch(entities, &None);
                }
            }
            SyncMessage::Ack { entity_id, entity_type, version, device_id } => {
                let key = format!("{}:{}", entity_type.as_str(), entity_id);
                self.entity_versions.put(key, (version, device_id));
            }
            SyncMessage::EntityPull { .. } => {
                // EntityPull is handled by the server/peer — we don't process it here
            }
            SyncMessage::SendTab {
                url,
                title,
                sender_device,
                ..
            } => {
                let sender_name = self
                    .devices
                    .get(&sender_device)
                    .map(|d| d.name.clone())
                    .unwrap_or_else(|| "Unknown".to_string());
                let sender_type = self
                    .devices
                    .get(&sender_device)
                    .map(|d| d.device_type.clone())
                    .unwrap_or(crate::sync_models::DeviceType::Unknown);
                self.received_tabs.push(ReceivedTab {
                    url,
                    title,
                    sender_name,
                    sender_device_type: sender_type,
                    received_at: chrono::Utc::now().to_rfc3339(),
                });
            }
            SyncMessage::DeviceJoined { device } => {
                self.devices.insert(device.id.clone(), device);
            }
            SyncMessage::DeviceLeft { device_id } => {
                self.devices.remove(&device_id);
            }
            SyncMessage::Ping => {
                self.outgoing_messages.push(SyncMessage::Pong);
            }
            SyncMessage::Pong => {}
        }
    }

    // === Outgoing ===

    /// Send a tab to a specific device
    pub fn send_tab(&mut self, url: &str, title: &str, target_device_id: &str) {
        let message = SyncMessage::SendTab {
            url: url.to_string(),
            title: title.to_string(),
            sender_device: self.config.device_id.clone(),
            target_device: target_device_id.to_string(),
        };

        if let Some(key) = &self.encryption_key {
            match serde_json::to_vec(&message) {
                Ok(plaintext) => match crate::sync_crypto::encrypt_update(&plaintext, key) {
                    Ok(data) => {
                        self.outgoing_messages.push(SyncMessage::Encrypted { data });
                    }
                    Err(e) => {
                        self.last_error = Some(format!("Encrypt error: {}", e));
                    }
                },
                Err(e) => {
                    self.last_error = Some(format!("Serialize error: {}", e));
                }
            }
        } else {
            self.outgoing_messages.push(message);
        }
    }

    /// Get and drain outgoing messages (shell sends these via WebSocket)
    pub fn drain_outgoing_messages(&mut self) -> Vec<SyncMessage> {
        std::mem::take(&mut self.outgoing_messages)
    }

    /// Get and drain sent entities' queue row IDs
    pub fn drain_sent_entities(&mut self) -> Vec<i64> {
        std::mem::take(&mut self.sent_entities)
    }

    /// Get and drain received tabs (UI polls this)
    pub fn drain_received_tabs(&mut self) -> Vec<ReceivedTab> {
        std::mem::take(&mut self.received_tabs)
    }

    // === Device Management ===

    /// Get all known connected devices
    pub fn get_devices(&self) -> Vec<&DeviceInfo> {
        self.devices.values().collect()
    }

    /// Remove a device from the sync group
    pub fn remove_device(&mut self, device_id: &str) {
        self.devices.remove(device_id);
        self.outgoing_messages.push(SyncMessage::DeviceLeft {
            device_id: device_id.to_string(),
        });
    }

    // === Sync Handshake ===

    /// Flush pending pushes (when reconnecting after offline)
    pub fn flush_offline_queue(&mut self) {
        let pending = self.drain_pending_pushes();
        for entity in pending {
            self.push_entity(entity);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::sync_crypto;
    use crate::sync_models::{DeviceType, SyncEntityType, SyncMessage, SyncStatus};

    fn default_config() -> SyncConfig {
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

    fn make_entity(id: &str, version: u64) -> SyncEntity {
        SyncEntity {
            entity_type: SyncEntityType::Tab,
            entity_id: id.to_string(),
            version,
            device_id: 0,
            schema_version: 1,
            modified_at: 1000,
            payload_json: r#"{"url":"https://example.com"}"#.to_string(),
            deleted: false,
            queue_row_id: None,
        }
    }

    fn make_device(id: &str, name: &str) -> DeviceInfo {
        DeviceInfo {
            id: id.to_string(),
            name: name.to_string(),
            device_type: DeviceType::Mac,
            last_seen: "2026-01-01T00:00:00Z".to_string(),
            is_online: true,
        }
    }

    fn test_encryption_key() -> sync_crypto::EncryptionKey {
        let seed = sync_crypto::generate_sync_seed();
        sync_crypto::derive_encryption_key(&seed).unwrap()
    }

    // CM-01
    #[test]
    fn test_push_entity_synced_state() {
        let mut mgr = SyncManager::new(default_config());
        mgr.set_status(SyncStatus::Synced);
        let entity = make_entity("tab-1", 1);
        mgr.push_entity(entity);
        let msgs = mgr.drain_outgoing_messages();
        assert_eq!(msgs.len(), 1);
        match &msgs[0] {
            SyncMessage::EntityPush { entity } => {
                assert_eq!(entity.entity_id, "tab-1");
            }
            other => panic!("Expected EntityPush, got {:?}", other),
        }
    }

    // CM-02
    #[test]
    fn test_push_entity_idle_state() {
        let mut mgr = SyncManager::new(default_config());
        // status defaults to Idle
        let entity = make_entity("tab-2", 1);
        mgr.push_entity(entity);
        let msgs = mgr.drain_outgoing_messages();
        assert!(msgs.is_empty());
        assert_eq!(mgr.get_pending_pushes().len(), 1);
        assert_eq!(mgr.get_pending_pushes()[0].entity_id, "tab-2");
    }

    // CM-03
    #[test]
    fn test_push_entity_with_encryption() {
        let mut mgr = SyncManager::new(default_config());
        mgr.set_status(SyncStatus::Synced);
        mgr.set_encryption_key(test_encryption_key());
        let entity = make_entity("tab-3", 1);
        mgr.push_entity(entity);
        let msgs = mgr.drain_outgoing_messages();
        assert_eq!(msgs.len(), 1);
        match &msgs[0] {
            SyncMessage::Encrypted { data } => {
                assert!(!data.is_empty());
            }
            other => panic!("Expected Encrypted, got {:?}", other),
        }
    }

    // CM-04
    #[test]
    fn test_pull_entities() {
        let mut mgr = SyncManager::new(default_config());
        mgr.pull_entities(Some(SyncEntityType::Tab));
        let msgs = mgr.drain_outgoing_messages();
        assert_eq!(msgs.len(), 1);
        match &msgs[0] {
            SyncMessage::EntityPull {
                entity_type,
                since_version,
            } => {
                assert_eq!(entity_type, &Some(SyncEntityType::Tab));
                assert_eq!(*since_version, 0);
            }
            other => panic!("Expected EntityPull, got {:?}", other),
        }
    }

    // CM-05
    #[test]
    fn test_apply_remote_entity_newer() {
        let mut mgr = SyncManager::new(default_config());
        let entity = make_entity("tab-5", 10);
        assert!(mgr.apply_remote_entity(&entity, &None));
        // Version should be stored
        assert_eq!(mgr.entity_versions.peek(&"tab:tab-5".to_string()), Some(&(10, 0)));
    }

    // CM-06
    #[test]
    fn test_apply_remote_entity_older() {
        let mut mgr = SyncManager::new(default_config());
        // Insert a version first
        let newer = make_entity("tab-6", 10);
        mgr.apply_remote_entity(&newer, &None);
        // Now try an older version
        let older = make_entity("tab-6", 5);
        assert!(!mgr.apply_remote_entity(&older, &None));
        // Version should remain 10
        assert_eq!(mgr.entity_versions.peek(&"tab:tab-6".to_string()), Some(&(10, 0)));
    }

    // CM-07
    #[test]
    fn test_apply_remote_batch() {
        let mut mgr = SyncManager::new(default_config());
        // Pre-populate some versions
        mgr.entity_versions.put("tab:e-1".to_string(), (5, 0));
        mgr.entity_versions.put("tab:e-2".to_string(), (5, 0));

        let batch = vec![
            make_entity("e-1", 10), // newer → apply
            make_entity("e-2", 3),  // older → skip
            make_entity("e-3", 1),  // new → apply
            make_entity("e-4", 7),  // new → apply
            make_entity("e-1", 4),  // older than updated → skip
        ];
        let applied = mgr.apply_remote_batch(batch, &None);
        assert_eq!(applied.len(), 3);
    }

    // CM-08
    #[test]
    fn test_handle_incoming_entity_push() {
        let mut mgr = SyncManager::new(default_config());
        let msg = SyncMessage::EntityPush {
            entity: make_entity("tab-8", 5),
        };
        mgr.handle_incoming_message(msg);
        assert_eq!(mgr.entity_versions.peek(&"tab:tab-8".to_string()), Some(&(5, 0)));
    }

    // CM-09
    #[test]
    fn test_handle_incoming_encrypted() {
        let key = test_encryption_key();
        let mut mgr = SyncManager::new(default_config());
        mgr.set_encryption_key(key);

        // Build an encrypted EntityPush message using the same key
        let inner = SyncMessage::EntityPush {
            entity: make_entity("tab-9", 3),
        };
        let plaintext = serde_json::to_vec(&inner).unwrap();
        let encrypted_data = sync_crypto::encrypt_update(&plaintext, mgr.encryption_key.as_ref().unwrap()).unwrap();

        mgr.handle_incoming_message(SyncMessage::Encrypted {
            data: encrypted_data,
        });
        assert_eq!(mgr.entity_versions.peek(&"tab:tab-9".to_string()), Some(&(3, 0)));
        assert_ne!(mgr.status(), &SyncStatus::Error);
    }

    // CM-10
    #[test]
    fn test_handle_incoming_encrypted_no_key() {
        let mut mgr = SyncManager::new(default_config());
        // No encryption key set
        mgr.handle_incoming_message(SyncMessage::Encrypted {
            data: vec![0u8; 64],
        });
        assert_eq!(mgr.status(), &SyncStatus::Error);
        assert!(mgr.last_error().unwrap().contains("without key"));
    }

    // CM-11
    #[test]
    fn test_handle_incoming_send_tab() {
        let mut mgr = SyncManager::new(default_config());
        // Register sender device so name is known
        let device = make_device("sender-1", "MacBook Pro");
        mgr.devices.insert(device.id.clone(), device);

        mgr.handle_incoming_message(SyncMessage::SendTab {
            url: "https://example.com".to_string(),
            title: "Example".to_string(),
            sender_device: "sender-1".to_string(),
            target_device: "test-device-1".to_string(),
        });

        let tabs = mgr.drain_received_tabs();
        assert_eq!(tabs.len(), 1);
        assert_eq!(tabs[0].url, "https://example.com");
        assert_eq!(tabs[0].title, "Example");
        assert_eq!(tabs[0].sender_name, "MacBook Pro");
    }

    // CM-12
    #[test]
    fn test_handle_incoming_device_joined() {
        let mut mgr = SyncManager::new(default_config());
        let device = make_device("dev-2", "iPhone");
        mgr.handle_incoming_message(SyncMessage::DeviceJoined {
            device: device.clone(),
        });
        let devices = mgr.get_devices();
        assert_eq!(devices.len(), 1);
        assert_eq!(devices[0].id, "dev-2");
        assert_eq!(devices[0].name, "iPhone");
    }

    // CM-13
    #[test]
    fn test_handle_incoming_ping() {
        let mut mgr = SyncManager::new(default_config());
        mgr.handle_incoming_message(SyncMessage::Ping);
        let msgs = mgr.drain_outgoing_messages();
        assert_eq!(msgs.len(), 1);
        match &msgs[0] {
            SyncMessage::Pong => {}
            other => panic!("Expected Pong, got {:?}", other),
        }
    }

    // CM-14
    #[test]
    fn test_send_tab_with_encryption() {
        let mut mgr = SyncManager::new(default_config());
        mgr.set_encryption_key(test_encryption_key());
        mgr.send_tab("https://example.com", "Example", "target-dev");
        let msgs = mgr.drain_outgoing_messages();
        assert_eq!(msgs.len(), 1);
        match &msgs[0] {
            SyncMessage::Encrypted { data } => {
                assert!(!data.is_empty());
            }
            other => panic!("Expected Encrypted, got {:?}", other),
        }
    }

    // CM-15
    #[test]
    fn test_flush_offline_queue() {
        let mut mgr = SyncManager::new(default_config());
        // Push while idle → goes to pending
        mgr.push_entity(make_entity("p-1", 1));
        mgr.push_entity(make_entity("p-2", 2));
        assert_eq!(mgr.get_pending_pushes().len(), 2);
        assert!(mgr.drain_outgoing_messages().is_empty());

        // Now go online and flush
        mgr.set_status(SyncStatus::Synced);
        mgr.flush_offline_queue();
        assert!(mgr.get_pending_pushes().is_empty());
        let msgs = mgr.drain_outgoing_messages();
        assert_eq!(msgs.len(), 2);
    }

    // CM-16
    #[test]
    fn test_drain_outgoing_messages() {
        let mut mgr = SyncManager::new(default_config());
        mgr.handle_incoming_message(SyncMessage::Ping);
        mgr.handle_incoming_message(SyncMessage::Ping);
        let msgs = mgr.drain_outgoing_messages();
        assert_eq!(msgs.len(), 2);
        // After drain, outgoing should be empty
        assert!(mgr.drain_outgoing_messages().is_empty());
    }

    // CM-17
    #[test]
    fn test_drain_received_tabs() {
        let mut mgr = SyncManager::new(default_config());
        mgr.handle_incoming_message(SyncMessage::SendTab {
            url: "https://a.com".to_string(),
            title: "A".to_string(),
            sender_device: "s1".to_string(),
            target_device: "test-device-1".to_string(),
        });
        mgr.handle_incoming_message(SyncMessage::SendTab {
            url: "https://b.com".to_string(),
            title: "B".to_string(),
            sender_device: "s2".to_string(),
            target_device: "test-device-1".to_string(),
        });
        let tabs = mgr.drain_received_tabs();
        assert_eq!(tabs.len(), 2);
        // After drain, received_tabs should be empty
        assert!(mgr.drain_received_tabs().is_empty());
    }

    // H12: LRU eviction at cap
    #[test]
    fn entity_versions_lru_evicts_at_cap() {
        let mut sm = SyncManager::new(default_config());
        for i in 0u64..10_001 {
            let entity = make_entity(&format!("entity_{}", i), i + 1);
            sm.apply_remote_entity(&entity, &None);
        }
        assert!(sm.entity_versions.peek(&"tab:entity_0".to_string()).is_none());
        assert!(sm.entity_versions.peek(&"tab:entity_10000".to_string()).is_some());
    }

    #[test]
    fn test_hlc_and_persistent_tiebreaking_and_queue_lifecycle() {
        let mut sm = SyncManager::new(default_config());
        
        let mut entity1 = make_entity("tab-10", 100);
        entity1.device_id = 1;
        
        let mut entity2 = make_entity("tab-10", 100);
        entity2.device_id = 2;
        
        assert!(sm.apply_remote_entity(&entity1, &None));
        assert!(sm.apply_remote_entity(&entity2, &None));
        
        let mut entity3 = make_entity("tab-10", 100);
        entity3.device_id = 1;
        assert!(!sm.apply_remote_entity(&entity3, &None));
        
        let mut entity4 = make_entity("tab-11", 101);
        entity4.queue_row_id = Some(42);
        
        sm.set_status(SyncStatus::Synced);
        sm.push_entity(entity4);
        
        let sent_ids = sm.drain_sent_entities();
        assert_eq!(sent_ids, vec![42]);
    }

    #[test]
    fn test_is_leader() {
        let mut sm = SyncManager::new(default_config());
        assert!(!sm.is_leader());
        sm.set_status(SyncStatus::Synced);
        assert!(sm.is_leader());
    }
}

impl SyncManager {
    /// Enable or disable syncing of a specific entity type.
    pub fn set_entity_sync_enabled(&mut self, entity_type: crate::sync_models::SyncEntityType, enabled: bool) {
        if enabled {
            self.config.disabled_entity_types.remove(&entity_type);
        } else {
            self.config.disabled_entity_types.insert(entity_type);
        }
    }

    pub fn is_leader(&self) -> bool {
        if self.status != SyncStatus::Synced && self.status != SyncStatus::Syncing {
            return false;
        }
        let local_hash = hash_device_id(&self.config.device_id);
        let mut min_hash = local_hash;
        for dev in self.devices.values() {
            let dev_hash = hash_device_id(&dev.id);
            if dev_hash < min_hash {
                min_hash = dev_hash;
            }
        }
        min_hash == local_hash
    }
}

fn hash_device_id(device_id: &str) -> u32 {
    let bytes = if let Ok(uuid) = uuid::Uuid::parse_str(device_id) {
        uuid.into_bytes().to_vec()
    } else {
        device_id.as_bytes().to_vec()
    };
    crate::account_manager::fnv1a_bytes(&bytes)
}
