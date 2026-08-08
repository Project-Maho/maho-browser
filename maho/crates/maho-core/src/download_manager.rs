use std::collections::HashMap;

use maho_types::common::DateTime;
use maho_types::identifiers::DownloadId;
use maho_types::traits::shell_renderer::{DownloadState, DownloadViewModel};

pub struct Download {
    pub id: DownloadId,
    pub filename: String,
    pub url: String,
    pub total_bytes: u64,
    pub received_bytes: u64,
    pub state: DownloadState,
    pub started_at: DateTime,
    pub file_path: Option<String>,
    pub resume_data: Option<Vec<u8>>,
    pub mime_type: Option<String>,
    pub error: Option<String>,
    pub completed_at: Option<DateTime>,
    pub original_filename: Option<String>,
    pub chromium_guid: Option<String>,
}

pub struct DownloadManager {
    downloads: HashMap<DownloadId, Download>,
}

impl Default for DownloadManager {
    fn default() -> Self {
        Self::new()
    }
}

impl DownloadManager {
    pub fn new() -> Self {
        Self {
            downloads: HashMap::new(),
        }
    }

    pub fn start_download(
        &mut self,
        filename: String,
        url: String,
        total_bytes: u64,
        file_path: Option<String>,
        mime_type: Option<String>,
        chromium_guid: Option<String>,
    ) -> DownloadId {
        let id = DownloadId::generate();
        let download = Download {
            id: id.clone(),
            filename,
            url,
            total_bytes,
            received_bytes: 0,
            state: DownloadState::Downloading,
            started_at: DateTime::now(),
            file_path,
            resume_data: None,
            mime_type,
            error: None,
            completed_at: None,
            original_filename: None,
            chromium_guid,
        };
        self.downloads.insert(id.clone(), download);
        id
    }

    pub fn update_progress(&mut self, download_id: &DownloadId, received_bytes: u64) {
        if let Some(download) = self.downloads.get_mut(download_id) {
            download.received_bytes = received_bytes;
        }
    }

    pub fn complete_download(&mut self, download_id: &DownloadId) {
        if let Some(download) = self.downloads.get_mut(download_id) {
            download.state = DownloadState::Completed;
            download.received_bytes = download.total_bytes;
            download.completed_at = Some(DateTime::now());
        }
    }

    pub fn fail_download(&mut self, download_id: &DownloadId, error: String) {
        if let Some(download) = self.downloads.get_mut(download_id) {
            download.state = DownloadState::Failed;
            download.error = Some(error);
        }
    }

    pub fn set_resume_data(&mut self, download_id: &DownloadId, data: Vec<u8>) {
        if let Some(download) = self.downloads.get_mut(download_id) {
            download.resume_data = Some(data);
        }
    }

    pub fn remove_download(&mut self, download_id: &DownloadId) -> Option<Download> {
        self.downloads.remove(download_id)
    }

    pub fn cancel_download(&mut self, download_id: &DownloadId) {
        if let Some(download) = self.downloads.get_mut(download_id) {
            download.state = DownloadState::Cancelled;
        }
    }

    pub fn pause_download(&mut self, download_id: &DownloadId) {
        if let Some(download) = self.downloads.get_mut(download_id) {
            download.state = DownloadState::Paused;
        }
    }

    pub fn resume_download(&mut self, download_id: &DownloadId) {
        if let Some(download) = self.downloads.get_mut(download_id) {
            download.state = DownloadState::Downloading;
        }
    }

    pub fn get_download(&self, download_id: &DownloadId) -> Option<&Download> {
        self.downloads.get(download_id)
    }

    pub fn get_all_downloads(&self) -> Vec<&Download> {
        self.downloads.values().collect()
    }

    pub fn get_active_downloads(&self) -> Vec<&Download> {
        self.downloads
            .values()
            .filter(|d| matches!(d.state, DownloadState::Downloading))
            .collect()
    }

    pub fn to_view_model(download: &Download) -> DownloadViewModel {
        DownloadViewModel {
            id: download.id.to_string(),
            filename: download.filename.clone(),
            url: download.url.clone(),
            total_bytes: download.total_bytes,
            received_bytes: download.received_bytes,
            state: download.state.clone(),
            file_path: download.file_path.clone(),
            mime_type: download.mime_type.clone(),
            error: download.error.clone(),
            started_at: download.started_at.to_string(),
            completed_at: download.completed_at.as_ref().map(|dt| dt.to_string()),
            original_filename: download.original_filename.clone(),
        }
    }

    pub fn get_all_view_models(&self) -> Vec<DownloadViewModel> {
        self.downloads.values().map(Self::to_view_model).collect()
    }

    pub fn rename_download(
        &mut self,
        download_id: &DownloadId,
        new_name: &str,
    ) -> Option<(String, String)> {
        if let Some(download) = self.downloads.get_mut(download_id) {
            let old_name = download.filename.clone();
            if download.original_filename.is_none() {
                download.original_filename = Some(old_name.clone());
            }
            download.filename = new_name.to_string();
            Some((old_name, new_name.to_string()))
        } else {
            None
        }
    }

    pub fn restore_download_name(&mut self, download_id: &DownloadId) -> Option<(String, String)> {
        if let Some(download) = self.downloads.get_mut(download_id) {
            if let Some(original) = download.original_filename.take() {
                let old_name = download.filename.clone();
                download.filename = original.clone();
                Some((old_name, original))
            } else {
                None
            }
        } else {
            None
        }
    }

    pub fn seed_download(&mut self, download: Download) {
        self.downloads.insert(download.id.clone(), download);
    }

    pub fn update_metadata(
        &mut self,
        download_id: &DownloadId,
        filename: Option<String>,
        file_path: Option<String>,
        mime_type: Option<String>,
        total_bytes: Option<u64>,
    ) {
        if let Some(download) = self.downloads.get_mut(download_id) {
            if let Some(f) = filename {
                if !f.is_empty() {
                    download.filename = f;
                }
            }
            if let Some(p) = file_path {
                if !p.is_empty() {
                    download.file_path = Some(p);
                }
            }
            if let Some(m) = mime_type {
                if !m.is_empty() {
                    download.mime_type = Some(m);
                }
            }
            if let Some(t) = total_bytes {
                if t > 0 {
                    download.total_bytes = t;
                }
            }
        }
    }

    pub fn find_by_chromium_guid(&self, guid: &str) -> Option<&Download> {
        self.downloads
            .values()
            .find(|d| d.chromium_guid.as_deref() == Some(guid))
    }
}
