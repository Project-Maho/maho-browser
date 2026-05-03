use serde::{Deserialize, Serialize};
use std::collections::HashMap;

/// Represents a single extension's storage area
#[derive(Debug, Clone, Default, Serialize, Deserialize)]
pub struct ExtensionStorage {
    data: HashMap<String, serde_json::Value>,
}

/// Change record for storage.onChanged events
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct StorageChangeRecord {
    pub old_value: Option<serde_json::Value>,
    pub new_value: Option<serde_json::Value>,
}

impl ExtensionStorage {
    pub fn new() -> Self {
        Self {
            data: HashMap::new(),
        }
    }

    /// Get values for the given keys. If keys is empty, return all.
    pub fn get(&self, keys: &[String]) -> HashMap<String, serde_json::Value> {
        if keys.is_empty() {
            return self.data.clone();
        }
        keys.iter()
            .filter_map(|k| self.data.get(k).map(|v| (k.clone(), v.clone())))
            .collect()
    }

    /// Set multiple key-value pairs. Returns changes for storage.onChanged.
    pub fn set(
        &mut self,
        items: HashMap<String, serde_json::Value>,
    ) -> Vec<(String, StorageChangeRecord)> {
        let mut changes = Vec::new();
        for (key, new_value) in items {
            let old_value = self.data.get(&key).cloned();
            self.data.insert(key.clone(), new_value.clone());
            changes.push((
                key,
                StorageChangeRecord {
                    old_value,
                    new_value: Some(new_value),
                },
            ));
        }
        changes
    }

    /// Remove keys. Returns changes for storage.onChanged.
    pub fn remove(&mut self, keys: &[String]) -> Vec<(String, StorageChangeRecord)> {
        let mut changes = Vec::new();
        for key in keys {
            if let Some(old_value) = self.data.remove(key) {
                changes.push((
                    key.clone(),
                    StorageChangeRecord {
                        old_value: Some(old_value),
                        new_value: None,
                    },
                ));
            }
        }
        changes
    }

    /// Clear all data
    pub fn clear(&mut self) -> Vec<(String, StorageChangeRecord)> {
        let changes: Vec<_> = self
            .data
            .drain()
            .map(|(key, old_value)| {
                (
                    key,
                    StorageChangeRecord {
                        old_value: Some(old_value),
                        new_value: None,
                    },
                )
            })
            .collect();
        changes
    }

    /// Get approximate bytes in use
    pub fn get_bytes_in_use(&self, keys: &[String]) -> usize {
        if keys.is_empty() {
            return self
                .data
                .iter()
                .map(|(k, v)| k.len() + v.to_string().len())
                .sum();
        }
        keys.iter()
            .filter_map(|k| self.data.get(k).map(|v| k.len() + v.to_string().len()))
            .sum()
    }

    /// Check if storage is empty
    pub fn is_empty(&self) -> bool {
        self.data.is_empty()
    }

    /// Get number of stored keys
    pub fn len(&self) -> usize {
        self.data.len()
    }
}

/// Manager for multiple extensions' storage
pub struct ExtensionStorageManager {
    stores: HashMap<String, ExtensionStorage>,
}

impl Default for ExtensionStorageManager {
    fn default() -> Self {
        Self::new()
    }
}

impl ExtensionStorageManager {
    pub fn new() -> Self {
        Self {
            stores: HashMap::new(),
        }
    }

    /// Get or create storage for an extension
    pub fn get_store(&mut self, extension_id: &str) -> &mut ExtensionStorage {
        self.stores.entry(extension_id.to_string()).or_default()
    }

    /// Remove all storage for an extension
    pub fn remove_store(&mut self, extension_id: &str) {
        self.stores.remove(extension_id);
    }

    /// Get total bytes across all extensions
    pub fn total_bytes_in_use(&self) -> usize {
        self.stores.values().map(|s| s.get_bytes_in_use(&[])).sum()
    }
}
