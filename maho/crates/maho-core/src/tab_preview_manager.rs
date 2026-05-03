use std::collections::{HashMap, HashSet};

use maho_types::common::{DateTime, ImageData};
use maho_types::identifiers::TabId;

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

    pub fn update_preview(&mut self, tab_id: TabId, thumbnail: ImageData) {
        let preview = TabPreview {
            tab_id: tab_id.clone(),
            thumbnail: Some(thumbnail),
            captured_at: DateTime::now(),
        };
        self.previews.insert(tab_id, preview);
    }

    pub fn get_preview(&self, tab_id: &TabId) -> Option<&TabPreview> {
        self.previews.get(tab_id)
    }

    pub fn remove_preview(&mut self, tab_id: &TabId) {
        self.previews.remove(tab_id);
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
