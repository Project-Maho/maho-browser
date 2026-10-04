use std::collections::HashMap;

use maho_types::sharing::SharedCollection;
use serde::{Deserialize, Serialize};

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct SharedCollectionViewModel {
    pub id: String,
    pub name: String,
    pub permission: String,
    pub member_count: u32,
}

pub struct SharingManager {
    collections: HashMap<String, SharedCollection>,
}

impl Default for SharingManager {
    fn default() -> Self {
        Self::new()
    }
}

impl SharingManager {
    pub fn new() -> Self {
        Self {
            collections: HashMap::new(),
        }
    }

    pub fn create_collection(&mut self, space_id: &str, name: &str) -> SharedCollection {
        let id = uuid::Uuid::new_v4().to_string();
        let share_link = format!("https://mahobrowser.com/shared/{}", id);
        let collection = SharedCollection {
            id: id.clone(),
            space_id: space_id.to_string(),
            name: name.to_string(),
            permission: "view".to_string(),
            member_count: 1,
            share_link: Some(share_link),
            created_at: chrono::Utc::now().to_rfc3339(),
        };
        self.collections.insert(id, collection.clone());
        collection
    }

    pub fn get_share_link(&self, collection_id: &str) -> Option<&str> {
        self.collections.get(collection_id)?.share_link.as_deref()
    }

    pub fn update_permissions(&mut self, collection_id: &str, permission: &str) -> bool {
        const VALID_PERMISSIONS: &[&str] = &["view", "edit", "admin"];
        if !VALID_PERMISSIONS.contains(&permission) {
            return false;
        }
        if let Some(c) = self.collections.get_mut(collection_id) {
            c.permission = permission.to_string();
            true
        } else {
            false
        }
    }

    pub fn revoke_share(&mut self, collection_id: &str) {
        self.collections.remove(collection_id);
    }

    pub fn get_all_collections(&self) -> Vec<SharedCollectionViewModel> {
        self.collections
            .values()
            .map(|c| SharedCollectionViewModel {
                id: c.id.clone(),
                name: c.name.clone(),
                permission: c.permission.clone(),
                member_count: c.member_count,
            })
            .collect()
    }

    pub fn join_collection(&mut self, share_link: &str) -> bool {
        if let Some(id) = share_link.split('/').next_back() {
            if !id.is_empty()
                && uuid::Uuid::parse_str(id).is_ok()
                && !self.collections.contains_key(id)
            {
                let collection = SharedCollection {
                    id: id.to_string(),
                    space_id: String::new(),
                    name: format!("Shared Collection {}", &id[..8.min(id.len())]),
                    permission: "view".to_string(),
                    member_count: 1,
                    share_link: Some(share_link.to_string()),
                    created_at: chrono::Utc::now().to_rfc3339(),
                };
                self.collections.insert(id.to_string(), collection);
                return true;
            }
        }
        false
    }

    pub fn load_from_storage(&mut self, collections: Vec<SharedCollection>) {
        for c in collections {
            self.collections.insert(c.id.clone(), c);
        }
    }

    pub fn get_all_for_storage(&self) -> Vec<&SharedCollection> {
        self.collections.values().collect()
    }
}
