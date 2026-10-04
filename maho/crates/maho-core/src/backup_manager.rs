use serde::{Deserialize, Serialize};

use maho_types::common::DateTime;

const BACKUP_MAGIC: &[u8; 4] = b"MAHO";
const BACKUP_VERSION: u32 = 1;
const ENCRYPTION_NONE: u8 = 0;
const ENCRYPTION_XCHACHA20: u8 = 1;
const MAX_BACKUP_HISTORY: usize = 20;

#[derive(Clone, Debug, Serialize, Deserialize)]
pub enum BackupEncryption {
    None,
    XChaCha20Poly1305,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct BackupMetadata {
    pub id: String,
    pub created_at: DateTime,
    pub version: u32,
    pub encryption: BackupEncryption,
    pub size_bytes: u64,
    pub item_counts: BackupItemCounts,
    pub description: Option<String>,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct BackupItemCounts {
    pub bookmarks: usize,
    pub history_entries: usize,
    pub notes: usize,
    pub boosts: usize,
    pub spaces: usize,
    pub settings: bool,
}

#[derive(Clone, Debug, serde::Deserialize)]
pub struct BackupConfig {
    pub include_bookmarks: bool,
    pub include_history: bool,
    pub include_notes: bool,
    pub include_boosts: bool,
    pub include_spaces: bool,
    pub include_settings: bool,
    pub encryption: BackupEncryption,
    pub password: Option<String>,
    pub description: Option<String>,
}

impl Default for BackupConfig {
    fn default() -> Self {
        Self {
            include_bookmarks: true,
            include_history: true,
            include_notes: true,
            include_boosts: true,
            include_spaces: true,
            include_settings: true,
            encryption: BackupEncryption::None,
            password: None,
            description: None,
        }
    }
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct BackupData {
    pub metadata: BackupMetadata,
    pub bookmarks: Option<Vec<serde_json::Value>>,
    pub history: Option<Vec<serde_json::Value>>,
    pub notes: Option<Vec<serde_json::Value>>,
    pub boosts: Option<Vec<serde_json::Value>>,
    pub spaces: Option<Vec<serde_json::Value>>,
    pub settings: Option<serde_json::Value>,
}

#[derive(Debug)]
pub enum BackupError {
    Serialization(String),
    Encryption(String),
    Decryption(String),
    InvalidPassword,
    CorruptedBackup(String),
    IoError(String),
    VersionMismatch { expected: u32, found: u32 },
}

impl std::fmt::Display for BackupError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Serialization(msg) => write!(f, "Serialization error: {}", msg),
            Self::Encryption(msg) => write!(f, "Encryption error: {}", msg),
            Self::Decryption(msg) => write!(f, "Decryption error: {}", msg),
            Self::InvalidPassword => write!(f, "Invalid password"),
            Self::CorruptedBackup(msg) => write!(f, "Corrupted backup: {}", msg),
            Self::IoError(msg) => write!(f, "IO error: {}", msg),
            Self::VersionMismatch { expected, found } => {
                write!(
                    f,
                    "Version mismatch: expected {}, found {}",
                    expected, found
                )
            }
        }
    }
}

impl std::error::Error for BackupError {}

pub struct BackupManager {
    backup_history: Vec<BackupMetadata>,
}

impl Default for BackupManager {
    fn default() -> Self {
        Self::new()
    }
}

impl BackupManager {
    pub fn new() -> Self {
        Self {
            backup_history: Vec::new(),
        }
    }

    pub fn create_backup(
        &mut self,
        config: &BackupConfig,
        data: BackupData,
    ) -> Result<Vec<u8>, BackupError> {
        let json_bytes =
            serde_json::to_vec(&data).map_err(|e| BackupError::Serialization(e.to_string()))?;

        let encryption_flag = match config.encryption {
            BackupEncryption::None => ENCRYPTION_NONE,
            BackupEncryption::XChaCha20Poly1305 => ENCRYPTION_XCHACHA20,
        };

        let payload = match config.encryption {
            BackupEncryption::None => json_bytes,
            BackupEncryption::XChaCha20Poly1305 => {
                return Err(BackupError::Encryption(
                    "Encryption not yet available — XChaCha20-Poly1305 implementation pending"
                        .to_string(),
                ));
            }
        };

        // Build backup file: magic (4) + version (4) + encryption flag (1) + payload
        let mut output = Vec::with_capacity(9 + payload.len());
        output.extend_from_slice(BACKUP_MAGIC);
        output.extend_from_slice(&BACKUP_VERSION.to_le_bytes());
        output.push(encryption_flag);
        output.extend_from_slice(&payload);

        // Record in history
        self.backup_history.push(data.metadata);
        if self.backup_history.len() > MAX_BACKUP_HISTORY {
            let excess = self.backup_history.len() - MAX_BACKUP_HISTORY;
            self.backup_history.drain(0..excess);
        }

        Ok(output)
    }

    pub fn restore_backup(
        &self,
        data: &[u8],
        _password: Option<&str>,
    ) -> Result<BackupData, BackupError> {
        if data.len() < 9 {
            return Err(BackupError::CorruptedBackup(
                "Backup file too small".to_string(),
            ));
        }

        // Validate magic
        if &data[0..4] != BACKUP_MAGIC {
            return Err(BackupError::CorruptedBackup(
                "Invalid magic bytes — not a Maho backup".to_string(),
            ));
        }

        // Read version
        let version = u32::from_le_bytes([data[4], data[5], data[6], data[7]]);
        if version != BACKUP_VERSION {
            return Err(BackupError::VersionMismatch {
                expected: BACKUP_VERSION,
                found: version,
            });
        }

        // Read encryption flag
        let encryption_flag = data[8];
        let payload = &data[9..];

        let json_bytes = match encryption_flag {
            ENCRYPTION_NONE => payload.to_vec(),
            ENCRYPTION_XCHACHA20 => {
                return Err(BackupError::Encryption(
                    "Encrypted backups cannot be restored — XChaCha20-Poly1305 implementation pending".to_string(),
                ));
            }
            _ => {
                return Err(BackupError::CorruptedBackup(format!(
                    "Unknown encryption flag: {}",
                    encryption_flag
                )));
            }
        };

        let backup_data: BackupData = serde_json::from_slice(&json_bytes).map_err(|e| {
            BackupError::Serialization(format!("Failed to deserialize backup: {}", e))
        })?;

        Ok(backup_data)
    }

    pub fn get_backup_history(&self) -> &[BackupMetadata] {
        &self.backup_history
    }

    pub fn validate_backup(data: &[u8]) -> Result<BackupMetadata, BackupError> {
        if data.len() < 9 {
            return Err(BackupError::CorruptedBackup(
                "Backup file too small".to_string(),
            ));
        }

        if &data[0..4] != BACKUP_MAGIC {
            return Err(BackupError::CorruptedBackup(
                "Invalid magic bytes".to_string(),
            ));
        }

        let version = u32::from_le_bytes([data[4], data[5], data[6], data[7]]);
        if version != BACKUP_VERSION {
            return Err(BackupError::VersionMismatch {
                expected: BACKUP_VERSION,
                found: version,
            });
        }

        let encryption_flag = data[8];
        if encryption_flag != ENCRYPTION_NONE {
            // Can't extract metadata from encrypted backup without decrypting
            return Err(BackupError::Encryption(
                "Cannot validate encrypted backup without password".to_string(),
            ));
        }

        let payload = &data[9..];
        let backup_data: BackupData = serde_json::from_slice(payload)
            .map_err(|e| BackupError::Serialization(format!("Failed to parse backup: {}", e)))?;

        Ok(backup_data.metadata)
    }

    pub fn estimate_backup_size(config: &BackupConfig, data: &BackupData) -> u64 {
        let mut size: u64 = 9;

        if config.include_bookmarks {
            if let Some(bookmarks) = &data.bookmarks {
                size += serde_json::to_string(bookmarks)
                    .map(|s| s.len() as u64)
                    .unwrap_or(0);
            }
        }
        if config.include_history {
            if let Some(history) = &data.history {
                size += serde_json::to_string(history)
                    .map(|s| s.len() as u64)
                    .unwrap_or(0);
            }
        }
        if config.include_notes {
            if let Some(notes) = &data.notes {
                size += serde_json::to_string(notes)
                    .map(|s| s.len() as u64)
                    .unwrap_or(0);
            }
        }
        if config.include_boosts {
            if let Some(boosts) = &data.boosts {
                size += serde_json::to_string(boosts)
                    .map(|s| s.len() as u64)
                    .unwrap_or(0);
            }
        }
        if config.include_spaces {
            if let Some(spaces) = &data.spaces {
                size += serde_json::to_string(spaces)
                    .map(|s| s.len() as u64)
                    .unwrap_or(0);
            }
        }
        if config.include_settings {
            if let Some(settings) = &data.settings {
                size += serde_json::to_string(settings)
                    .map(|s| s.len() as u64)
                    .unwrap_or(0);
            }
        }

        size
    }

    pub fn to_view_model(metadata: &BackupMetadata) -> serde_json::Value {
        serde_json::json!({
            "id": metadata.id,
            "created_at": metadata.created_at.0,
            "version": metadata.version,
            "encryption": match metadata.encryption {
                BackupEncryption::None => "none",
                BackupEncryption::XChaCha20Poly1305 => "xchacha20poly1305",
            },
            "size_bytes": metadata.size_bytes,
            "item_counts": {
                "bookmarks": metadata.item_counts.bookmarks,
                "history_entries": metadata.item_counts.history_entries,
                "notes": metadata.item_counts.notes,
                "boosts": metadata.item_counts.boosts,
                "spaces": metadata.item_counts.spaces,
                "settings": metadata.item_counts.settings,
            },
            "description": metadata.description,
        })
    }
}
