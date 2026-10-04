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

mod callbacks;
mod color;
mod vault;

use maho_core::maho_core::MahoCore;
use maho_import::orchestrator::ImportDestination;
use maho_types::common::Url;
use maho_types::events::core_update::CoreUpdate;
use maho_types::events::shell_event::ShellEvent;
use maho_types::identifiers::{FolderId, ProfileId, SpaceId, TabId};
use std::ffi::c_void;

use crate::import::{
    MahoImportAutofillCallback, MahoImportCookieCallback, MahoImportFaviconCallback,
};

/// Wrapper to make `*mut MahoCore` usable across threads.
///
/// # Safety
/// The caller must ensure exclusive access to the pointed-to MahoCore for the
/// lifetime of this wrapper. See module-level safety docs.
struct CorePtr(*mut MahoCore);

// SAFETY: `crate::import_gate` enforces exclusive access — all other
// `maho_core_*` FFI entry points short-circuit while IMPORT_ACTIVE is set,
// so the import worker thread is the sole accessor of MahoCore during import.
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
    fn create_space(&self, name: &str, theme_color_hex: &str, icon: &str) -> Option<String> {
        let color = color::hex_to_space_color(theme_color_hex);
        let core = self.core_mut();
        let space = core.create_space(name, color, ProfileId::default());
        if !icon.trim().is_empty() {
            // maho-core canonicalizes the value (icon names -> emoji), so the
            // importer hands over whatever the source browser stored.
            core.update_space_config(maho_types::space::SpaceConfigUpdate {
                space_id: space.id.clone(),
                icon: Some(Some(icon.to_string())),
                ..Default::default()
            });
        }
        Some(space.id.0.clone())
    }

    fn create_tab(&self, space_id: &str, url: &str, title: &str) -> Option<String> {
        let sid = SpaceId::new(space_id);
        let tab_id = TabId::generate();
        let tab_id_str = tab_id.0.clone();

        let event = ShellEvent::CreateTab {
            space_id: sid,
            url: Some(Url::new(url)),
            parent_id: None,
            tab_id: Some(tab_id.clone()),
            window_id: None,
            is_private: false,
        };
        self.core_mut().handle_event(event);
        if !title.is_empty() {
            self.core_mut().handle_event(ShellEvent::TabTitleUpdated {
                tab_id,
                title: title.to_string(),
            });
        }
        Some(tab_id_str)
    }

    fn create_tab_in_folder(
        &self,
        space_id: &str,
        url: &str,
        title: &str,
        folder_id: &str,
    ) -> Option<String> {
        // First create the tab
        let tab_id_str = self.create_tab(space_id, url, title)?;

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
            provider_type: None,
            config_json: None,
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

        self.core_mut().add_bookmark(url, title, folder_id);

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
        callbacks::add_cookie(
            self.cookie_cb,
            self.user_data,
            host,
            name,
            value,
            path,
            expires,
            is_secure,
            is_httponly,
            same_site,
        )
    }

    fn add_autofill(
        &self,
        field_name: &str,
        value: &str,
        times_used: i32,
        first_used: i64,
        last_used: i64,
    ) -> bool {
        callbacks::add_autofill(
            self.autofill_cb,
            self.user_data,
            field_name,
            value,
            times_used,
            first_used,
            last_used,
        )
    }

    fn add_favicon(&self, url: &str, png_bytes: &[u8]) -> bool {
        let mut delivered = false;
        if !self.core.0.is_null() {
            // First borrow: read-only scan to collect matching TabIds into an owned Vec.
            let matching = callbacks::find_matching_favicon_tabs(self.core_ref(), url);
            if !matching.is_empty() {
                // Second borrow: mutable update to apply events.
                callbacks::apply_favicon_updates(self.core_mut(), &matching, png_bytes);
                delivered = true;
            }
        }

        if callbacks::dispatch_favicon_callback(self.favicon_cb, self.user_data, url, png_bytes) {
            delivered = true;
        }

        delivered
    }

    fn add_password(&self, entry: maho_import::PasswordEntry) -> bool {
        if self.core.0.is_null() {
            return false;
        }
        vault::add_password(self.core_mut(), entry)
    }
}
