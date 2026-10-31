//! Sync data models for cross-device synchronization.

use base64::Engine;
use serde::{Deserialize, Serialize};
use thiserror::Error;

/// Unique device identifier
pub type DeviceId = String;

/// Device type enum
#[derive(Clone, Debug, Serialize, Deserialize, PartialEq)]
#[serde(rename_all = "lowercase")]
pub enum DeviceType {
    Mac,
    IPhone,
    IPad,
    Android,
    Windows,
    Linux,
    Unknown,
}

impl DeviceType {
    pub fn from_platform() -> Self {
        #[cfg(target_os = "macos")]
        {
            DeviceType::Mac
        }
        #[cfg(target_os = "ios")]
        {
            DeviceType::IPhone
        }
        #[cfg(target_os = "android")]
        {
            DeviceType::Android
        }
        #[cfg(target_os = "windows")]
        {
            DeviceType::Windows
        }
        #[cfg(target_os = "linux")]
        {
            DeviceType::Linux
        }
        #[cfg(not(any(
            target_os = "macos",
            target_os = "ios",
            target_os = "android",
            target_os = "windows",
            target_os = "linux"
        )))]
        {
            DeviceType::Unknown
        }
    }
}

/// Information about a connected device
#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct DeviceInfo {
    pub id: DeviceId,
    pub name: String,
    pub device_type: DeviceType,
    pub last_seen: String, // ISO 8601 timestamp
    pub is_online: bool,
}

/// Sync connection status
#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq, Default)]
#[serde(rename_all = "lowercase")]
pub enum SyncStatus {
    #[default]
    Idle,
    Connecting,
    Syncing,
    Synced,
    Error,
    Offline,
}

/// Stable shell-facing state for the cross-platform sync transport.
///
/// This is intentionally a struct rather than a JSON-encoded enum so every
/// FFI client can display the same pending-work and error information.
#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub struct SyncStateResponse {
    pub kind: SyncStatus,
    pub last_success_at: Option<i64>,
    pub pending_outbox_count: usize,
    pub last_error: Option<String>,
}

impl Default for SyncStateResponse {
    fn default() -> Self {
        Self {
            kind: SyncStatus::Idle,
            last_success_at: None,
            pending_outbox_count: 0,
            last_error: None,
        }
    }
}

/// Current wire version for relay envelopes.
pub const SYNC_PROTOCOL_VERSION: u8 = 2;

/// Versioned opaque transport envelope shared by desktop WebSocket and mobile
/// HTTP sync clients. The relay can validate this metadata without decrypting
/// the payload.
#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub struct SyncEnvelopeV2 {
    pub protocol_version: u8,
    pub delivery_id: String,
    pub payload: String,
    pub relay_seq: Option<u64>,
}

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub struct RelayAckV2 {
    pub delivery_id: String,
    pub seq: u64,
}

#[derive(Clone, Debug, Error, PartialEq, Eq)]
pub enum SyncProtocolError {
    #[error("unsupported sync protocol version {found}")]
    UnsupportedProtocolVersion { found: u8 },
    #[error("sync delivery id is not a UUID")]
    InvalidDeliveryId,
    #[error("sync envelope payload is empty")]
    EmptyPayload,
    #[error("sync envelope payload is not valid base64")]
    InvalidPayloadEncoding,
}

impl SyncEnvelopeV2 {
    pub fn new(payload: Vec<u8>) -> Result<Self, SyncProtocolError> {
        if payload.is_empty() {
            return Err(SyncProtocolError::EmptyPayload);
        }

        Ok(Self {
            protocol_version: SYNC_PROTOCOL_VERSION,
            delivery_id: uuid::Uuid::new_v4().to_string(),
            payload: base64::engine::general_purpose::STANDARD.encode(payload),
            relay_seq: None,
        })
    }

    pub fn validate(&self) -> Result<(), SyncProtocolError> {
        if self.protocol_version != SYNC_PROTOCOL_VERSION {
            return Err(SyncProtocolError::UnsupportedProtocolVersion {
                found: self.protocol_version,
            });
        }
        if uuid::Uuid::parse_str(&self.delivery_id).is_err() {
            return Err(SyncProtocolError::InvalidDeliveryId);
        }
        if self.payload.is_empty() {
            return Err(SyncProtocolError::EmptyPayload);
        }
        let bytes = base64::engine::general_purpose::STANDARD
            .decode(&self.payload)
            .map_err(|_| SyncProtocolError::InvalidPayloadEncoding)?;
        if bytes.is_empty() {
            return Err(SyncProtocolError::EmptyPayload);
        }
        Ok(())
    }

    pub fn payload_bytes(&self) -> Result<Vec<u8>, SyncProtocolError> {
        self.validate()?;
        base64::engine::general_purpose::STANDARD
            .decode(&self.payload)
            .map_err(|_| SyncProtocolError::InvalidPayloadEncoding)
    }
}

/// Sync configuration
#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SyncConfig {
    pub server_url: String,
    pub sync_key: Option<String>,
    pub device_id: DeviceId,
    pub device_name: String,
    pub auto_sync: bool,
    pub sync_interval_secs: u64,
    #[serde(default, skip_serializing_if = "std::collections::HashSet::is_empty")]
    pub disabled_entity_types: std::collections::HashSet<SyncEntityType>,
}

impl Default for SyncConfig {
    fn default() -> Self {
        Self {
            server_url: "wss://relay.mahobrowser.com".to_string(),
            sync_key: None,
            device_id: uuid::Uuid::new_v4().to_string(),
            device_name: hostname(),
            auto_sync: true,
            sync_interval_secs: 30,
            disabled_entity_types: std::collections::HashSet::new(),
        }
    }
}

/// Syncable entity types
#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq, Hash)]
#[serde(rename_all = "lowercase")]
#[non_exhaustive]
pub enum SyncEntityType {
    Space,
    Tab,
    Bookmark,
    Note,
    Boost,
    ReadingListItem,
    SearchEngine,
    Shortcut,
    Memory,
    Extension,
    BookmarkFolder,
    CssMod,
    Easel,
    Conversation,
    ConversationProject,
    ConversationTurn,
    AutofillAddress,
    AutofillPayment,
    Settings,
    SharedCollection,
    VaultItem,
}

impl SyncEntityType {
    pub fn is_profile_scoped(&self) -> bool {
        matches!(
            self,
            SyncEntityType::Space
                | SyncEntityType::Tab
                | SyncEntityType::Bookmark
                | SyncEntityType::BookmarkFolder
                | SyncEntityType::Note
                | SyncEntityType::Boost
                | SyncEntityType::ReadingListItem
                | SyncEntityType::CssMod
                | SyncEntityType::Easel
        )
    }

    pub fn cursor_storage_key(&self, profile_id: Option<&str>) -> String {
        match profile_id.filter(|id| !id.is_empty() && self.is_profile_scoped()) {
            Some(pid) => format!("sync:cursor:{}:{}", self.as_str(), pid),
            None => format!("sync:cursor:{}", self.as_str()),
        }
    }

    pub fn as_str(&self) -> &'static str {
        match self {
            SyncEntityType::Space => "space",
            SyncEntityType::Tab => "tab",
            SyncEntityType::Bookmark => "bookmark",
            SyncEntityType::BookmarkFolder => "bookmarkfolder",
            SyncEntityType::Note => "note",
            SyncEntityType::Boost => "boost",
            SyncEntityType::ReadingListItem => "readinglistitem",
            SyncEntityType::SearchEngine => "searchengine",
            SyncEntityType::Shortcut => "shortcut",
            SyncEntityType::Memory => "memory",
            SyncEntityType::Extension => "extension",
            SyncEntityType::CssMod => "cssmod",
            SyncEntityType::Easel => "easel",
            SyncEntityType::Conversation => "conversation",
            SyncEntityType::ConversationProject => "conversationproject",
            SyncEntityType::ConversationTurn => "conversationturn",
            SyncEntityType::AutofillAddress => "autofilladdress",
            SyncEntityType::AutofillPayment => "autofillpayment",
            SyncEntityType::Settings => "settings",
            SyncEntityType::SharedCollection => "sharedcollection",
            SyncEntityType::VaultItem => "vaultitem",
        }
    }
}

/// A versioned entity for LWW sync
#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SyncEntity {
    pub entity_type: SyncEntityType,
    pub entity_id: String,
    pub version: u64, // HLC timestamp
    #[serde(default)]
    pub device_id: u32,
    #[serde(default = "default_schema_version")]
    pub schema_version: u8,
    pub modified_at: i64,
    pub payload_json: String,
    pub deleted: bool,
    #[serde(skip)]
    pub queue_row_id: Option<i64>,
    #[serde(default)]
    pub fields_hlc_json: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub profile_id: Option<String>,
}

impl SyncEntity {
    pub fn new(
        entity_type: SyncEntityType,
        entity_id: String,
        version: u64,
        modified_at: i64,
        payload_json: String,
        deleted: bool,
    ) -> Self {
        Self {
            entity_type,
            entity_id,
            version,
            device_id: 0,
            schema_version: 1,
            modified_at,
            payload_json,
            deleted,
            queue_row_id: None,
            fields_hlc_json: None,
            profile_id: None,
        }
    }
    pub fn cache_key(&self) -> Option<String> {
        let storage_id = self.storage_entity_id()?;
        Some(format!("{}:{}", self.entity_type.as_str(), storage_id))
    }

    pub fn profile_scope(&self) -> Option<String> {
        if !self.entity_type.is_profile_scoped() {
            return None;
        }
        if let Some(profile_id) = self.profile_id.as_deref().filter(|s| !s.is_empty()) {
            return Some(profile_id.to_string());
        }
        Self::profile_id_from_payload(&self.entity_type, &self.payload_json)
    }

    pub fn storage_entity_id(&self) -> Option<String> {
        if self.entity_type.is_profile_scoped() {
            let profile_id = self.profile_scope()?;
            Some(format!("{}:{}", profile_id, self.entity_id))
        } else {
            Some(self.entity_id.clone())
        }
    }

    pub fn profile_id_from_payload(
        _entity_type: &SyncEntityType,
        payload_json: &str,
    ) -> Option<String> {
        if payload_json.trim().is_empty() {
            return None;
        }
        let parsed: serde_json::Value = serde_json::from_str(payload_json).ok()?;
        for field in ["profileId", "profile_id"] {
            if let Some(id) = parsed.get(field).and_then(|v| v.as_str()) {
                if !id.is_empty() {
                    return Some(id.to_string());
                }
            }
        }
        None
    }
}

/// Read canonical versions and reconcile the alternate keys emitted by older
/// local writers. Never let an older canonical row hide a newer local edit.
pub(crate) fn load_persisted_entity_version(
    storage: &maho_storage::sqlite::SqliteStorage,
    entity_type: &SyncEntityType,
    entity_id: &str,
    profile_id: Option<&str>,
) -> Result<Option<maho_storage::sqlite::EntityVersionRecord>, maho_storage::error::StorageError> {
    let canonical_id = if entity_type.is_profile_scoped() {
        let Some(profile_id) = profile_id else {
            return Ok(None);
        };
        format!("{profile_id}:{entity_id}")
    } else {
        entity_id.to_string()
    };
    let mut profiles: Vec<String> = profile_id.into_iter().map(str::to_string).collect();
    if !entity_type.is_profile_scoped() {
        for json in storage.get_all_profiles()? {
            let profile: maho_types::profile::ProfileConfig = serde_json::from_str(&json)?;
            profiles.push(profile.id.to_string());
        }
    }
    profiles.sort();
    profiles.dedup();
    let mut latest = storage.get_entity_version(entity_type.as_str(), &canonical_id)?;
    let mut migrated = false;
    for profile in profiles {
        let legacy_id = format!("profile:{profile}\u{1f}{entity_id}");
        if let Some(candidate) = storage.get_entity_version(entity_type.as_str(), &legacy_id)? {
            if latest
                .as_ref()
                .is_none_or(|row| (candidate.0, candidate.1) > (row.0, row.1))
            {
                latest = Some(candidate);
                migrated = true;
            }
        }
    }
    if migrated {
        if let Some((version, device, fields, payload)) = &latest {
            storage.save_entity_version(
                entity_type.as_str(),
                &canonical_id,
                *version,
                *device,
                fields.as_deref(),
                payload.as_deref(),
            )?;
        }
    }
    Ok(latest)
}

fn default_schema_version() -> u8 {
    1
}

/// Messages exchanged between devices via the sync relay
#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(tag = "type", rename_all = "camelCase")]
pub enum SyncMessage {
    /// Push a local entity change to server/peers
    EntityPush {
        entity: SyncEntity,
    },
    /// Request entities newer than given version
    EntityPull {
        entity_type: Option<SyncEntityType>,
        since_version: u64,
    },
    /// Batch of entities (response to EntityPull)
    EntityBatch {
        entities: Vec<SyncEntity>,
    },
    /// Server acknowledges an entity push
    Ack {
        entity_id: String,
        entity_type: SyncEntityType,
        version: u64,
        device_id: u32,
    },
    /// Send a tab to another device
    SendTab {
        url: String,
        title: String,
        sender_device: DeviceId,
        target_device: DeviceId,
    },
    /// Encrypted wrapper
    Encrypted {
        data: Vec<u8>,
    },
    /// A device joined the sync group
    DeviceJoined {
        device: DeviceInfo,
    },
    /// A device left the sync group
    DeviceLeft {
        device_id: DeviceId,
    },
    /// Ping/pong for keepalive
    Ping,
    Pong,
}

/// Received tab notification
#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ReceivedTab {
    pub url: String,
    pub title: String,
    pub sender_name: String,
    pub sender_device_type: DeviceType,
    pub received_at: String,
}

/// Result of generating a sync key
#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SyncKeyResult {
    pub sync_key: String,
    pub room_id: String,
    pub recovery_phrase: String,
}

fn hostname() -> String {
    #[cfg(any(target_os = "macos", target_os = "linux"))]
    {
        std::process::Command::new("hostname")
            .output()
            .ok()
            .and_then(|o| String::from_utf8(o.stdout).ok())
            .map(|s| s.trim().to_string())
            .unwrap_or_else(|| "Unknown Device".to_string())
    }
    #[cfg(target_os = "windows")]
    {
        std::env::var("COMPUTERNAME")
            .or_else(|_| std::env::var("USERDOMAIN"))
            .unwrap_or_else(|_| "Windows Device".to_string())
    }
    #[cfg(not(any(target_os = "macos", target_os = "linux", target_os = "windows")))]
    {
        "Unknown Device".to_string()
    }
}

pub fn build_fields_hlc(payload_json: &str, version: u64, device_id: u32) -> Option<String> {
    if let Ok(serde_json::Value::Object(map)) =
        serde_json::from_str::<serde_json::Value>(payload_json)
    {
        let mut fields = std::collections::HashMap::new();
        for key in map.keys() {
            fields.insert(key.clone(), (version, device_id));
        }
        serde_json::to_string(&fields).ok()
    } else {
        None
    }
}

#[cfg(test)]
mod tests {
    use super::SyncConfig;

    #[test]
    fn sync_config_default_server_url() {
        assert_eq!(
            SyncConfig::default().server_url,
            "wss://relay.mahobrowser.com",
        );
    }
}
