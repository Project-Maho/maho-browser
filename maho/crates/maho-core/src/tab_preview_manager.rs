use std::collections::{HashMap, HashSet};

use maho_types::common::{DateTime, ImageData};
use maho_types::identifiers::TabId;

use serde::{Deserialize, Serialize};

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct TabPreview {
    pub tab_id: TabId,
    pub thumbnail: Option<ImageData>,
    pub captured_at: DateTime,
}

#[derive(Clone, Debug)]
pub struct ScreenshotRequest {
    pub tab_id: TabId,
    pub full_page: bool,
    pub save_path: Option<String>,
}

pub struct TabPreviewManager {
    previews: HashMap<TabId, TabPreview>,
    pending_captures: HashSet<TabId>,
    screenshot_requests: Vec<ScreenshotRequest>,
}

impl TabPreviewManager {
    pub fn new() -> Self {
        Self {
            previews: HashMap::new(),
            pending_captures: HashSet::new(),
            screenshot_requests: Vec::new(),
        }
    }

    pub fn total_size_bytes(&self) -> usize {
        self.previews
            .values()
            .filter_map(|p| p.thumbnail.as_ref())
            .map(|thumb| thumb.data.len())
            .sum()
    }

    pub fn evict_lru(&mut self, max_bytes: usize) {
        let mut total = self.total_size_bytes();
        if total <= max_bytes {
            return;
        }

        let mut list: Vec<(TabId, String, usize)> = self
            .previews
            .iter()
            .map(|(id, p)| {
                (
                    id.clone(),
                    p.captured_at.0.clone(),
                    p.thumbnail.as_ref().map(|t| t.data.len()).unwrap_or(0),
                )
            })
            .collect();

        list.sort_by(|a, b| a.1.cmp(&b.1));

        for (id, _, size) in list {
            if total <= max_bytes {
                break;
            }
            self.previews.remove(&id);
            total = total.saturating_sub(size);
        }
    }

    pub fn update_preview(&mut self, tab_id: TabId, thumbnail: ImageData) {
        let preview = TabPreview {
            tab_id: tab_id.clone(),
            thumbnail: Some(thumbnail),
            captured_at: DateTime::now(),
        };
        self.previews.insert(tab_id, preview);
        self.evict_lru(100 * 1024 * 1024);
    }

    pub fn get_preview(&self, tab_id: &TabId) -> Option<&TabPreview> {
        self.previews.get(tab_id)
    }

    pub fn remove_preview(&mut self, tab_id: &TabId) {
        self.previews.remove(tab_id);
    }

    pub fn persist_to_storage(
        &self,
        storage: &maho_storage::lmdb::LmdbStorage,
        private_ids: &HashSet<TabId>,
    ) {
        for (tab_id, preview) in &self.previews {
            // Never persist private/incognito tab thumbnails to disk.
            if private_ids.contains(tab_id) {
                continue;
            }
            if let Ok(serialized) = serde_json::to_vec(preview) {
                let _ = storage.save_preview(tab_id.as_ref(), &serialized);
            }
        }
    }

    pub fn load_from_storage(
        &mut self,
        storage: &maho_storage::lmdb::LmdbStorage,
        tab_ids: &[TabId],
    ) {
        for tab_id in tab_ids {
            if let Ok(Some(bytes)) = storage.load_preview(tab_id.as_ref()) {
                if let Ok(preview) = serde_json::from_slice::<TabPreview>(&bytes) {
                    self.previews.insert(tab_id.clone(), preview);
                }
            }
        }
    }

    pub fn clear_stale_previews(&mut self, max_age_seconds: u64) {
        let now = chrono::Utc::now();
        self.previews.retain(|_, preview| {
            if let Ok(captured) =
                chrono::DateTime::parse_from_rfc3339(preview.captured_at.0.as_str())
            {
                let age = now.signed_duration_since(captured);
                age.num_seconds() < max_age_seconds as i64
            } else {
                true
            }
        });
    }

    pub fn get_preview_count(&self) -> usize {
        self.previews.len()
    }

    pub fn schedule_capture(&mut self, tab_id: TabId) -> bool {
        self.pending_captures.insert(tab_id)
    }

    pub fn take_pending_captures(&mut self) -> Vec<TabId> {
        self.pending_captures.drain().collect()
    }

    pub fn has_preview(&self, tab_id: &TabId) -> bool {
        self.previews.contains_key(tab_id)
    }

    pub fn capture_full_screenshot(
        &mut self,
        tab_id: TabId,
        save_path: Option<String>,
    ) -> ScreenshotRequest {
        let request = ScreenshotRequest {
            tab_id: tab_id.clone(),
            full_page: true,
            save_path,
        };
        self.screenshot_requests.push(request.clone());
        request
    }

    pub fn take_screenshot_requests(&mut self) -> Vec<ScreenshotRequest> {
        self.screenshot_requests.drain(..).collect()
    }

    pub fn has_pending_screenshots(&self) -> bool {
        !self.screenshot_requests.is_empty()
    }
}

impl Default for TabPreviewManager {
    fn default() -> Self {
        Self::new()
    }
}
