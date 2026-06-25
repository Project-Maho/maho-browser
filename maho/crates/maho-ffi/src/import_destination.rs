//! Production `ImportDestination` implementation backed by `MahoCore`.
//!
//! Bridges the `maho-import` orchestrator to `maho-core` APIs, allowing the
//! Rust orchestrator to write imported data directly into the core state.
//!
//! ## Safety contract
//!
//! `MahoCoreDestination` wraps a raw `*mut MahoCore` pointer. The caller must
//! guarantee:
//! 1. The pointer remains valid for the lifetime of the destination.
//! 2. No other thread mutates MahoCore concurrently while an import is running.
//!
//! In practice this is enforced by the C++ side: the import session holds a
//! reference to the `MahoCore` pointer and the UI thread does not call
//! mutating functions on it until the import completes (or is cancelled).

use std::ffi::{c_void, CString};
use maho_core::maho_core::MahoCore;
use maho_import::orchestrator::ImportDestination;
use maho_types::common::Url;
use maho_types::events::core_update::CoreUpdate;
use maho_types::events::shell_event::ShellEvent;
use maho_types::identifiers::{FolderId, ProfileId, SpaceId, TabId};
use maho_types::space::SpaceColor;

use crate::import::{MahoImportCookieCallback, MahoImportAutofillCallback, MahoImportFaviconCallback};

/// Wrapper to make `*mut MahoCore` usable across threads.
///
/// # Safety
/// The caller must ensure exclusive access to the pointed-to MahoCore for the
/// lifetime of this wrapper. See module-level safety docs.
struct CorePtr(*mut MahoCore);

// SAFETY: We guarantee single-threaded access via the orchestrator design —
// the import worker thread is the only accessor during an active import.
unsafe impl Send for CorePtr {}
unsafe impl Sync for CorePtr {}

/// Production `ImportDestination` that calls into `MahoCore`.
pub struct MahoCoreDestination {
    core: CorePtr,
    cookie_cb: Option<MahoImportCookieCallback>,
    autofill_cb: Option<MahoImportAutofillCallback>,
    favicon_cb: Option<MahoImportFaviconCallback>,
    user_data: *mut c_void,
}

// SAFETY: `MahoCoreDestination` holds three raw pointers that require explicit
// thread-safety reasoning:
//   1. `core: CorePtr` — wraps `*mut MahoCore`. Already justified at its own
//      `unsafe impl Send/Sync` site: the import worker thread is the only
//      accessor during an active import.
//   2. `cookie_cb`/`autofill_cb`/`favicon_cb` — C `extern "C" fn` pointers.
//      Function pointers are trivially `Send + Sync`; the FFI ABI guarantees
//      they remain valid for the process lifetime (C++ static methods).
//   3. `user_data: *mut c_void` — opaque pointer to the C++ bridge instance
//      (`MahoImportSessionBridge*`). The FFI contract on
//      `maho_import_orchestrator_start` requires the caller to keep this
//      valid until the terminal progress callback fires, and the bridge
//      owns the import session (calls `_free` on terminal or destructor).
//      The bridge itself uses `WeakPtr` + `PostTask` internally to guard
//      against UAF when callbacks arrive after destruction.
unsafe impl Send for MahoCoreDestination {}
unsafe impl Sync for MahoCoreDestination {}

impl MahoCoreDestination {
    /// Create a new destination backed by an existing MahoCore pointer.
    ///
    /// # Safety
    /// `core` must be a valid, non-null pointer returned by `maho_core_new` or
    /// `maho_core_new_with_storage`. The caller must guarantee exclusive access
    /// for the duration of the import.
    pub unsafe fn new(core: *mut MahoCore) -> Self {
        Self {
            core: CorePtr(core),
            cookie_cb: None,
            autofill_cb: None,
            favicon_cb: None,
            user_data: std::ptr::null_mut(),
        }
    }

    /// Create a new destination with callback parameters.
    pub unsafe fn new_with_callbacks(
        core: *mut MahoCore,
        cookie_cb: Option<MahoImportCookieCallback>,
        autofill_cb: Option<MahoImportAutofillCallback>,
        favicon_cb: Option<MahoImportFaviconCallback>,
        user_data: *mut c_void,
    ) -> Self {
        Self {
            core: CorePtr(core),
            cookie_cb,
            autofill_cb,
            favicon_cb,
            user_data,
        }
    }

    /// Get a mutable reference to the core (unsafe interior).
    fn core_mut(&self) -> &mut MahoCore {
        // SAFETY: guaranteed by caller contract (exclusive access during import).
        unsafe { &mut *self.core.0 }
    }

    fn core_ref(&self) -> &MahoCore {
        // SAFETY: guaranteed by caller contract.
        unsafe { &*self.core.0 }
    }
}

impl ImportDestination for MahoCoreDestination {
    fn create_space(&self, name: &str, theme_color_hex: &str, _icon: &str) -> Option<String> {
        let color = hex_to_space_color(theme_color_hex);
        let space = self
            .core_mut()
            .create_space(name, color, ProfileId::default());
        Some(space.id.0.clone())
    }

    fn create_tab(&self, space_id: &str, url: &str) -> Option<String> {
        let sid = SpaceId::new(space_id);
        let tab_id = TabId::generate();
        let tab_id_str = tab_id.0.clone();

        let event = ShellEvent::CreateTab {
            space_id: sid,
            url: Some(Url::new(url)),
            parent_id: None,
            tab_id: Some(tab_id),
            window_id: None,
        };
        self.core_mut().handle_event(event);
        Some(tab_id_str)
    }

    fn create_tab_in_folder(
        &self,
        space_id: &str,
        url: &str,
        folder_id: &str,
    ) -> Option<String> {
        // First create the tab
        let tab_id_str = self.create_tab(space_id, url)?;

        // Then move it into the folder
        let event = ShellEvent::MoveTabToFolder {
            space_id: SpaceId::new(space_id),
            folder_id: FolderId::new(folder_id),
            tab_id: TabId::new(&tab_id_str),
        };
        self.core_mut().handle_event(event);
        Some(tab_id_str)
    }

    fn create_folder(&self, space_id: &str, name: &str, parent_id: &str) -> Option<String> {
        let parent_folder_id = if parent_id.is_empty() {
            None
        } else {
            Some(FolderId::new(parent_id))
        };

        let event = ShellEvent::CreateFolder {
            space_id: SpaceId::new(space_id),
            name: name.to_string(),
            is_pinned: false,
            parent_folder_id,
        };
        let updates = self.core_mut().handle_event(event);

        // Extract the folder ID from the FolderCreated update.
        for update in updates {
            if let CoreUpdate::FolderCreated { folder } = update {
                return Some(folder.id.0.clone());
            }
        }
        None
    }

    fn pin_tab(&self, tab_id: &str) {
        let event = ShellEvent::PinTab {
            tab_id: TabId::new(tab_id),
        };
        self.core_mut().handle_event(event);
    }

    fn favorite_tab(&self, tab_id: &str) {
        let tid = TabId::new(tab_id);
        self.core_mut().favorite_tab(&tid);
    }

    fn activate_space(&self, space_id: &str) {
        let sid = SpaceId::new(space_id);
        self.core_mut().activate_space(&sid);
    }

    fn get_active_space_id(&self) -> Option<String> {
        let sid = self.core_ref().get_active_space_id();
        if sid.0.is_empty() {
            None
        } else {
            Some(sid.0.clone())
        }
    }

    fn add_bookmark(&self, title: &str, url: &str, folder_path: &[String]) -> Option<String> {
        let folder_id: Option<&str> = if folder_path.is_empty() {
            None
        } else {
            // TODO: Resolve hierarchical folder path to actual folder_id.
            // For now, import bookmarks at root level.
            None
        };

        self.core_mut()
            .add_bookmark(url, title, folder_id);

        // TODO: Expose add_bookmark return value from MahoCore.
        None
    }

    fn add_history(&self, url: &str, title: &str, _visit_time: f64, _visit_count: u32) -> bool {
        // MahoCore's add_history_entry doesn't accept visit_time/visit_count.
        // TODO: Extend MahoCore API to accept visit_time and visit_count for
        // bulk import scenarios.
        self.core_mut().add_history_entry(url, title);
        true
    }

    fn add_cookie(
        &self,
        host: &str,
        name: &str,
        value: &str,
        path: &str,
        expires: i64,
        is_secure: bool,
        is_httponly: bool,
        same_site: i32,
    ) -> bool {
        let Some(cb) = self.cookie_cb else { return false; };
        let (Some(hc), Some(nc), Some(vc), Some(pc)) = (
            CString::new(host).ok(),
            CString::new(name).ok(),
            CString::new(value).ok(),
            CString::new(path).ok(),
        ) else { return false; };
        unsafe {
            cb(
                hc.as_ptr(),
                nc.as_ptr(),
                vc.as_ptr(),
                pc.as_ptr(),
                expires,
                is_secure,
                is_httponly,
                same_site,
                self.user_data,
            );
        }
        true
    }

    fn add_autofill(
        &self,
        field_name: &str,
        value: &str,
        times_used: i32,
        first_used: i64,
        last_used: i64,
    ) -> bool {
        let Some(cb) = self.autofill_cb else { return false; };
        let (Some(fnc), Some(vc)) = (
            CString::new(field_name).ok(),
            CString::new(value).ok(),
        ) else { return false; };
        unsafe {
            cb(
                fnc.as_ptr(),
                vc.as_ptr(),
                times_used,
                first_used,
                last_used,
                self.user_data,
            );
        }
        true
    }

    fn add_favicon(&self, url: &str, png_bytes: &[u8]) -> bool {
        let mut delivered = false;

        let matching: Vec<TabId> = self
            .core_ref()
            .tab_manager()
            .get_all_tabs()
            .iter()
            .filter(|t| t.url.0 == url)
            .map(|t| t.id.clone())
            .collect();
        if !matching.is_empty() {
            let image = maho_types::common::ImageData {
                data: png_bytes.to_vec(),
                width: 16,
                height: 16,
                format: maho_types::common::ImageFormat::Png,
            };
            for tab_id in matching {
                self.core_mut().handle_event(ShellEvent::TabFaviconUpdated {
                    tab_id,
                    favicon: Some(image.clone()),
                });
            }
            delivered = true;
        }

        if let Some(cb) = self.favicon_cb {
            if let Ok(url_c) = CString::new(url) {
                unsafe {
                    cb(
                        url_c.as_ptr(),
                        png_bytes.as_ptr(),
                        png_bytes.len(),
                        self.user_data,
                    );
                }
                delivered = true;
            }
        }

        delivered
    }
}

/// Convert a hex color string (e.g. "#FF5733" or "FF5733") to `SpaceColor`.
///
/// SpaceColor uses HSB (hue 0–360, saturation 0–1, brightness 0–1).
/// Falls back to a default blue if parsing fails.
fn hex_to_space_color(hex: &str) -> SpaceColor {
    let hex = hex.trim_start_matches('#');
    if hex.len() < 6 {
        return default_space_color();
    }

    let r = u8::from_str_radix(&hex[0..2], 16).unwrap_or(100);
    let g = u8::from_str_radix(&hex[2..4], 16).unwrap_or(149);
    let b = u8::from_str_radix(&hex[4..6], 16).unwrap_or(237);

    let (h, s, v) = rgb_to_hsb(r, g, b);
    SpaceColor {
        hue: h,
        saturation: s,
        brightness: v,
        grain: 0.0,
    }
}

fn default_space_color() -> SpaceColor {
    SpaceColor {
        hue: 220.0,
        saturation: 0.7,
        brightness: 0.9,
        grain: 0.0,
    }
}

/// Convert RGB (0–255 each) to HSB (hue 0–360, saturation 0–1, brightness 0–1).
fn rgb_to_hsb(r: u8, g: u8, b: u8) -> (f64, f64, f64) {
    let rf = r as f64 / 255.0;
    let gf = g as f64 / 255.0;
    let bf = b as f64 / 255.0;

    let max = rf.max(gf).max(bf);
    let min = rf.min(gf).min(bf);
    let delta = max - min;

    let brightness = max;
    let saturation = if max == 0.0 { 0.0 } else { delta / max };

    let hue = if delta == 0.0 {
        0.0
    } else if max == rf {
        60.0 * (((gf - bf) / delta) % 6.0)
    } else if max == gf {
        60.0 * (((bf - rf) / delta) + 2.0)
    } else {
        60.0 * (((rf - gf) / delta) + 4.0)
    };

    let hue = if hue < 0.0 { hue + 360.0 } else { hue };

    (hue, saturation, brightness)
}
