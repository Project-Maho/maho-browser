use maho_types::common::DateTime;
use serde::{Deserialize, Serialize};
use std::collections::HashMap;

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct ReadingListItem {
    pub id: String,
    pub url: String,
    pub title: String,
    pub excerpt: Option<String>,
    pub site_name: Option<String>,
    pub favicon_url: Option<String>,
    pub preview_image_url: Option<String>,
    pub added_at: DateTime,
    pub read_at: Option<DateTime>,
    pub is_read: bool,
    pub estimated_read_minutes: Option<u32>,
    pub offline_content: Option<String>,
    pub tags: Vec<String>,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct ReadingListViewModel {
    pub id: String,
    pub url: String,
    pub title: String,
    pub excerpt: Option<String>,
    pub site_name: Option<String>,
    pub favicon_url: Option<String>,
    #[serde(rename = "isRead")]
    pub is_read: bool,
    pub added_at: String,
    pub estimated_read_minutes: Option<u32>,
    pub tag_count: usize,
}

#[derive(Debug)]
pub struct ReadingListManager {
    items: HashMap<String, ReadingListItem>,
}

impl ReadingListManager {
    pub fn new() -> Self {
        Self {
            items: HashMap::new(),
        }
    }

    pub fn add_item(&mut self, url: &str, title: &str) -> ReadingListItem {
        let item = ReadingListItem {
            id: uuid::Uuid::new_v4().to_string(),
            url: url.to_string(),
            title: title.to_string(),
            excerpt: None,
            site_name: None,
            favicon_url: None,
            preview_image_url: None,
            added_at: DateTime::now(),
            read_at: None,
            is_read: false,
            estimated_read_minutes: None,
            offline_content: None,
            tags: Vec::new(),
        };
        self.items.insert(item.id.clone(), item.clone());
        item
    }

    #[allow(clippy::too_many_arguments)]
    pub fn add_item_with_metadata(
        &mut self,
        url: &str,
        title: &str,
        excerpt: Option<&str>,
        site_name: Option<&str>,
        favicon_url: Option<&str>,
        preview_image_url: Option<&str>,
        estimated_read_minutes: Option<u32>,
    ) -> ReadingListItem {
        let item = ReadingListItem {
            id: uuid::Uuid::new_v4().to_string(),
            url: url.to_string(),
            title: title.to_string(),
            excerpt: excerpt.map(String::from),
            site_name: site_name.map(String::from),
            favicon_url: favicon_url.map(String::from),
            preview_image_url: preview_image_url.map(String::from),
            added_at: DateTime::now(),
            read_at: None,
            is_read: false,
            estimated_read_minutes,
            offline_content: None,
            tags: Vec::new(),
        };
        self.items.insert(item.id.clone(), item.clone());
        item
    }

    pub fn remove_item(&mut self, id: &str) -> Option<ReadingListItem> {
        self.items.remove(id)
    }

    pub fn mark_as_read(&mut self, id: &str) -> bool {
        if let Some(item) = self.items.get_mut(id) {
            item.is_read = true;
            item.read_at = Some(DateTime::now());
            true
        } else {
            false
        }
    }

    pub fn mark_as_unread(&mut self, id: &str) -> bool {
        if let Some(item) = self.items.get_mut(id) {
            item.is_read = false;
            item.read_at = None;
            true
        } else {
            false
        }
    }

    pub fn toggle_read(&mut self, id: &str) -> Option<bool> {
        self.items.get_mut(id).map(|item| {
            item.is_read = !item.is_read;
            if item.is_read {
                item.read_at = Some(DateTime::now());
            } else {
                item.read_at = None;
            }
            item.is_read
        })
    }

    pub fn get_item(&self, id: &str) -> Option<&ReadingListItem> {
        self.items.get(id)
    }

    pub fn get_all_items(&self) -> Vec<&ReadingListItem> {
        let mut items: Vec<&ReadingListItem> = self.items.values().collect();
        items.sort_by(|a, b| b.added_at.0.cmp(&a.added_at.0));
        items
    }

    pub fn get_unread_items(&self) -> Vec<&ReadingListItem> {
        let mut items: Vec<&ReadingListItem> =
            self.items.values().filter(|item| !item.is_read).collect();
        items.sort_by(|a, b| b.added_at.0.cmp(&a.added_at.0));
        items
    }

    pub fn get_read_items(&self) -> Vec<&ReadingListItem> {
        let mut items: Vec<&ReadingListItem> =
            self.items.values().filter(|item| item.is_read).collect();
        items.sort_by(|a, b| match (&b.read_at, &a.read_at) {
            (Some(b_read), Some(a_read)) => b_read.0.cmp(&a_read.0),
            (Some(_), None) => std::cmp::Ordering::Greater,
            (None, Some(_)) => std::cmp::Ordering::Less,
            (None, None) => std::cmp::Ordering::Equal,
        });
        items
    }

    pub fn set_offline_content(&mut self, id: &str, content: &str) -> bool {
        if let Some(item) = self.items.get_mut(id) {
            item.offline_content = Some(content.to_string());
            true
        } else {
            false
        }
    }

    pub fn add_tag(&mut self, id: &str, tag: &str) -> bool {
        if let Some(item) = self.items.get_mut(id) {
            let tag = tag.to_string();
            if !item.tags.contains(&tag) {
                item.tags.push(tag);
            }
            true
        } else {
            false
        }
    }

    pub fn remove_tag(&mut self, id: &str, tag: &str) -> bool {
        if let Some(item) = self.items.get_mut(id) {
            item.tags.retain(|t| t != tag);
            true
        } else {
            false
        }
    }

    pub fn search_items(&self, query: &str) -> Vec<&ReadingListItem> {
        let query_lower = query.to_lowercase();
        self.items
            .values()
            .filter(|item| {
                item.title.to_lowercase().contains(&query_lower)
                    || item.url.to_lowercase().contains(&query_lower)
                    || item
                        .excerpt
                        .as_ref()
                        .map(|e| e.to_lowercase().contains(&query_lower))
                        .unwrap_or(false)
            })
            .collect()
    }

    pub fn get_item_count(&self) -> usize {
        self.items.len()
    }

    pub fn get_unread_count(&self) -> usize {
        self.items.values().filter(|item| !item.is_read).count()
    }

    pub fn to_view_model(item: &ReadingListItem) -> ReadingListViewModel {
        ReadingListViewModel {
            id: item.id.clone(),
            url: item.url.clone(),
            title: item.title.clone(),
            excerpt: item.excerpt.clone(),
            site_name: item.site_name.clone(),
            favicon_url: item.favicon_url.clone(),
            is_read: item.is_read,
            added_at: item.added_at.0.clone(),
            estimated_read_minutes: item.estimated_read_minutes,
            tag_count: item.tags.len(),
        }
    }

    pub fn get_all_view_models(&self) -> Vec<ReadingListViewModel> {
        self.get_all_items()
            .into_iter()
            .map(Self::to_view_model)
            .collect()
    }

    /// Insert a pre-existing item (used for loading from storage)
    pub fn insert_item(&mut self, item: ReadingListItem) {
        self.items.insert(item.id.clone(), item);
    }
}

impl Default for ReadingListManager {
    fn default() -> Self {
        Self::new()
    }
}
