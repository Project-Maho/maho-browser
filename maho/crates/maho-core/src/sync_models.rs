//! Sync data models for cross-device synchronization.

use serde::{Deserialize, Serialize};

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
#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Default)]
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
            server_url: "wss://sync.mahobrowser.com".to_string(),
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
    ConversationTurn,
    AutofillAddress,
    AutofillPayment,
    Settings,
    SharedCollection,
}

impl SyncEntityType {
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
            SyncEntityType::ConversationTurn => "conversationturn",
            SyncEntityType::AutofillAddress => "autofilladdress",
            SyncEntityType::AutofillPayment => "autofillpayment",
            SyncEntityType::Settings => "settings",
            SyncEntityType::SharedCollection => "sharedcollection",
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
