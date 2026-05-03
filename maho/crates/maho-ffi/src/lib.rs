#![allow(clippy::missing_safety_doc)]

use std::ffi::{c_char, CStr, CString};
use std::path::Path;
use std::ptr;

use maho_core::maho_core::MahoCore;

fn to_json_cstring<T: serde::Serialize + ?Sized>(value: &T) -> *mut c_char {
    match serde_json::to_string(value) {
        Ok(json) => CString::new(json)
            .map(CString::into_raw)
            .unwrap_or(ptr::null_mut()),
        Err(_) => ptr::null_mut(),
    }
}

#[no_mangle]
pub extern "C" fn maho_core_new() -> *mut MahoCore {
    Box::into_raw(Box::new(MahoCore::new()))
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`, or null.
#[no_mangle]
pub unsafe extern "C" fn maho_core_free(ptr: *mut MahoCore) {
    if !ptr.is_null() {
        drop(Box::from_raw(ptr));
    }
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `event_json` must be a valid null-terminated C string.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_handle_event(
    ptr: *mut MahoCore,
    event_json: *const c_char,
) -> *mut c_char {
    if ptr.is_null() || event_json.is_null() {
        return ptr::null_mut();
    }
    let core = &mut *ptr;
    let json_str = match CStr::from_ptr(event_json).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    let event: maho_types::events::shell_event::ShellEvent = match serde_json::from_str(json_str) {
        Ok(e) => e,
        Err(_) => return ptr::null_mut(),
    };
    let updates = core.handle_event(event);
    to_json_cstring(&updates)
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_tab_view_models(ptr: *mut MahoCore) -> *mut c_char {
    if ptr.is_null() {
        return ptr::null_mut();
    }
    let core = &*ptr;
    to_json_cstring(&core.get_tab_view_models())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_space_view_models(ptr: *mut MahoCore) -> *mut c_char {
    if ptr.is_null() {
        return ptr::null_mut();
    }
    let core = &*ptr;
    to_json_cstring(&core.get_space_view_models())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_active_space_id(ptr: *mut MahoCore) -> *mut c_char {
    if ptr.is_null() {
        return ptr::null_mut();
    }
    let core = &*ptr;
    to_json_cstring(&core.get_active_space_id())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_account_state(ptr: *mut MahoCore) -> *mut c_char {
    if ptr.is_null() {
        return ptr::null_mut();
    }
    let core = &*ptr;
    to_json_cstring(&core.get_account_state_json())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_auth_state(ptr: *mut MahoCore) -> *mut c_char {
    if ptr.is_null() {
        return ptr::null_mut();
    }
    let core = &*ptr;
    to_json_cstring(&core.get_auth_state_json())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_sign_in(
    ptr: *mut MahoCore,
    event_json: *const c_char,
) -> *mut c_char {
    if ptr.is_null() || event_json.is_null() {
        return ptr::null_mut();
    }
    let core = &mut *ptr;
    let json_str = match CStr::from_ptr(event_json).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    let event: maho_types::events::shell_event::ShellEvent = match serde_json::from_str(json_str) {
        Ok(e) => e,
        Err(_) => return ptr::null_mut(),
    };
    let updates = core.handle_event(event);
    to_json_cstring(&updates)
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_tick(ptr: *mut MahoCore) -> *mut c_char {
    if ptr.is_null() {
        return ptr::null_mut();
    }
    let core = &mut *ptr;
    let updates = core.tick();
    to_json_cstring(&updates)
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `space_id_json` must be a valid null-terminated C string containing a SpaceId JSON.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_pinned_tabs(
    ptr: *mut MahoCore,
    space_id_json: *const c_char,
) -> *mut c_char {
    if ptr.is_null() || space_id_json.is_null() {
        return ptr::null_mut();
    }
    let core = &*ptr;
    let json_str = match CStr::from_ptr(space_id_json).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    let space_id = match serde_json::from_str(json_str) {
        Ok(id) => id,
        Err(_) => return ptr::null_mut(),
    };
    to_json_cstring(&core.get_pinned_tabs(&space_id))
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `space_id_json` must be a valid null-terminated C string containing a SpaceId JSON.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_space_tabs(
    ptr: *mut MahoCore,
    space_id_json: *const c_char,
) -> *mut c_char {
    if ptr.is_null() || space_id_json.is_null() {
        return ptr::null_mut();
    }
    let core = &*ptr;
    let json_str = match CStr::from_ptr(space_id_json).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    let space_id = match serde_json::from_str(json_str) {
        Ok(id) => id,
        Err(_) => return ptr::null_mut(),
    };
    to_json_cstring(&core.get_space_tabs(&space_id))
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `space_id_json` must be a valid null-terminated C string containing a SpaceId JSON.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_favorite_tabs(
    ptr: *mut MahoCore,
    space_id_json: *const c_char,
) -> *mut c_char {
    if ptr.is_null() || space_id_json.is_null() {
        return ptr::null_mut();
    }
    let core = &*ptr;
    let json_str = match CStr::from_ptr(space_id_json).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    let space_id = match serde_json::from_str(json_str) {
        Ok(id) => id,
        Err(_) => return ptr::null_mut(),
    };
    to_json_cstring(&core.get_favorite_tabs(&space_id))
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `space_id_json` must be a valid null-terminated C string containing a SpaceId JSON.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_today_tabs(
    ptr: *mut MahoCore,
    space_id_json: *const c_char,
) -> *mut c_char {
    if ptr.is_null() || space_id_json.is_null() {
        return ptr::null_mut();
    }
    let core = &*ptr;
    let json_str = match CStr::from_ptr(space_id_json).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    let space_id = match serde_json::from_str(json_str) {
        Ok(id) => id,
        Err(_) => return ptr::null_mut(),
    };
    to_json_cstring(&core.get_today_tabs(&space_id))
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_boost_view_models(ptr: *mut MahoCore) -> *mut c_char {
    if ptr.is_null() {
        return ptr::null_mut();
    }
    let core = &*ptr;
    to_json_cstring(&core.get_boost_view_models())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_css_mods(ptr: *mut MahoCore) -> *mut c_char {
    if ptr.is_null() {
        return ptr::null_mut();
    }
    let core = &*ptr;
    to_json_cstring(&core.css_mod_manager().get_all_mods())
}

/// Returns the combined CSS string of all enabled mods.
/// Caller must free with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_combined_css(ptr: *mut MahoCore) -> *mut c_char {
    if ptr.is_null() {
        return std::ptr::null_mut();
    }
    let core = &*ptr;
    let css = core.css_mod_manager().generate_combined_css();
    if css.is_empty() {
        return std::ptr::null_mut();
    }
    match CString::new(css) {
        Ok(s) => s.into_raw(),
        Err(_) => std::ptr::null_mut(),
    }
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_installed_extensions(ptr: *mut MahoCore) -> *mut c_char {
    if ptr.is_null() {
        return ptr::null_mut();
    }
    let core = &*ptr;
    to_json_cstring(&core.get_installed_extensions())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `url` must be a valid null-terminated C string.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_boosts_for_url(
    ptr: *mut MahoCore,
    url: *const c_char,
) -> *mut c_char {
    if ptr.is_null() || url.is_null() {
        return ptr::null_mut();
    }
    let core = &*ptr;
    let url_str = match CStr::from_ptr(url).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    to_json_cstring(&core.get_boosts_for_url(url_str))
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_note_view_models(ptr: *mut MahoCore) -> *mut c_char {
    if ptr.is_null() {
        return ptr::null_mut();
    }
    let core = &*ptr;
    to_json_cstring(&core.get_note_view_models())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_download_view_models(ptr: *mut MahoCore) -> *mut c_char {
    if ptr.is_null() {
        return ptr::null_mut();
    }
    let core = &*ptr;
    to_json_cstring(&core.get_download_view_models())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_settings(ptr: *mut MahoCore) -> *mut c_char {
    if ptr.is_null() {
        return ptr::null_mut();
    }
    let core = &*ptr;
    to_json_cstring(core.get_settings())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_settings_view_model(ptr: *mut MahoCore) -> *mut c_char {
    if ptr.is_null() {
        return ptr::null_mut();
    }
    let core = &*ptr;
    to_json_cstring(&core.get_settings_view_model())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_find_bar_state(ptr: *mut MahoCore) -> *mut c_char {
    if ptr.is_null() {
        return ptr::null_mut();
    }
    let core = &*ptr;
    to_json_cstring(&core.get_find_bar_state())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_split_view_config(ptr: *mut MahoCore) -> *mut c_char {
    if ptr.is_null() {
        return ptr::null_mut();
    }
    let core = &*ptr;
    to_json_cstring(&core.get_split_view_config())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `space_id_json` must be a valid null-terminated C string containing a SpaceId JSON.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_archived_tabs(
    ptr: *mut MahoCore,
    space_id_json: *const c_char,
) -> *mut c_char {
    if ptr.is_null() || space_id_json.is_null() {
        return ptr::null_mut();
    }
    let core = &*ptr;
    let json_str = match CStr::from_ptr(space_id_json).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    let space_id = match serde_json::from_str(json_str) {
        Ok(id) => id,
        Err(_) => return ptr::null_mut(),
    };
    to_json_cstring(&core.get_archived_tabs(&space_id))
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `space_id_json` must be a valid JSON string representing a SpaceId.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_folder_view_models(
    ptr: *mut MahoCore,
    space_id_json: *const c_char,
) -> *mut c_char {
    if ptr.is_null() || space_id_json.is_null() {
        return ptr::null_mut();
    }
    let core = &*ptr;
    let json_str = match CStr::from_ptr(space_id_json).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    let space_id = match serde_json::from_str(json_str) {
        Ok(id) => id,
        Err(_) => return ptr::null_mut(),
    };
    to_json_cstring(&core.get_folder_view_models(&space_id))
}

/// # Safety
/// All pointers must be valid C strings. `ptr` must be from `maho_core_new`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_should_block_request(
    ptr: *mut MahoCore,
    url: *const c_char,
    source_url: *const c_char,
    request_type: *const c_char,
) -> bool {
    if ptr.is_null() || url.is_null() || source_url.is_null() || request_type.is_null() {
        return false;
    }
    let core = &*ptr;
    let url_str = match CStr::from_ptr(url).to_str() {
        Ok(s) => s,
        Err(_) => return false,
    };
    let source_str = match CStr::from_ptr(source_url).to_str() {
        Ok(s) => s,
        Err(_) => return false,
    };
    let type_str = match CStr::from_ptr(request_type).to_str() {
        Ok(s) => s,
        Err(_) => return false,
    };
    core.should_block_request(url_str, source_str, type_str)
}

/// Returns cosmetic filter resources for a URL as JSON.
/// The JSON has fields: hideSelectors (string[]), injectedScript (string|null), generichide (bool).
///
/// # Safety
/// All pointers must be valid C strings. `ptr` must be from `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_cosmetic_resources(
    ptr: *mut MahoCore,
    url: *const c_char,
) -> *mut c_char {
    if ptr.is_null() || url.is_null() {
        return ptr::null_mut();
    }
    let core = &*ptr;
    let url_str = match CStr::from_ptr(url).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    let resources = core.get_cosmetic_resources(url_str);
    to_json_cstring(&resources)
}

/// # Safety
/// `ptr` must be from `maho_core_new`. `path` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_save_content_engine_cache(
    ptr: *mut MahoCore,
    path: *const c_char,
) -> bool {
    if ptr.is_null() || path.is_null() {
        return false;
    }
    let core = &*ptr;
    let path_str = match CStr::from_ptr(path).to_str() {
        Ok(s) => s,
        Err(_) => return false,
    };
    core.save_content_engine_cache(path_str).is_ok()
}

/// # Safety
/// `ptr` must be from `maho_core_new`. `path` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_load_content_engine_cache(
    ptr: *mut MahoCore,
    path: *const c_char,
) -> bool {
    if ptr.is_null() || path.is_null() {
        return false;
    }
    let core = &mut *ptr;
    let path_str = match CStr::from_ptr(path).to_str() {
        Ok(s) => s,
        Err(_) => return false,
    };
    core.load_content_engine_cache(path_str).is_ok()
}

/// # Safety
/// `s` must be a valid pointer returned by one of the `maho_core_*` functions, or null.
#[no_mangle]
pub unsafe extern "C" fn maho_string_free(s: *mut c_char) {
    if !s.is_null() {
        drop(CString::from_raw(s));
    }
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `url` and `title` must be valid null-terminated C strings.
#[no_mangle]
pub unsafe extern "C" fn maho_core_add_history_entry(
    ptr: *mut MahoCore,
    url: *const c_char,
    title: *const c_char,
) {
    if ptr.is_null() || url.is_null() || title.is_null() {
        return;
    }
    let core = &mut *ptr;
    let url_str = match CStr::from_ptr(url).to_str() {
        Ok(s) => s,
        Err(_) => return,
    };
    let title_str = match CStr::from_ptr(title).to_str() {
        Ok(s) => s,
        Err(_) => return,
    };
    core.add_history_entry(url_str, title_str);
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `query` must be a valid null-terminated C string.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_search_history(
    ptr: *mut MahoCore,
    query: *const c_char,
    limit: usize,
) -> *mut c_char {
    if ptr.is_null() || query.is_null() {
        return ptr::null_mut();
    }
    let core = &*ptr;
    let query_str = match CStr::from_ptr(query).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    let results = core.search_history(query_str, limit);
    to_json_cstring(&results)
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `query` must be a valid null-terminated C string.
/// `tabs_json` must be a valid null-terminated C string containing a JSON array of
/// `[{"id":"...", "title":"...", "url":"..."}]`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_command_bar_search(
    ptr: *mut MahoCore,
    query: *const c_char,
    tabs_json: *const c_char,
) -> *mut c_char {
    if ptr.is_null() || query.is_null() || tabs_json.is_null() {
        return ptr::null_mut();
    }
    let core = &*ptr;
    let query_str = match CStr::from_ptr(query).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    let tabs_str = match CStr::from_ptr(tabs_json).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };

    #[derive(serde::Deserialize)]
    struct SimpleTab {
        id: String,
        title: String,
        url: String,
    }

    let simple_tabs: Vec<SimpleTab> = serde_json::from_str(tabs_str).unwrap_or_default();
    let tabs = simple_tabs
        .into_iter()
        .map(|t| {
            use maho_types::common::DateTime;
            use maho_types::identifiers::{SpaceId, TabId};
            use maho_types::traits::shell_renderer::TabViewModel;
            TabViewModel {
                id: TabId::new(&t.id),
                space_id: SpaceId::default(),
                title: t.title,
                custom_title: None,
                url: t.url,
                favicon: None,
                is_loading: false,
                is_pinned: false,
                is_favorite: false,
                favorite_order: None,
                is_muted: false,
                is_playing_audio: false,
                lifecycle_state: "active".to_string(),
                children: Vec::new(),
                created_at: DateTime::now(),
                last_active_at: DateTime::now(),
            }
        })
        .collect();

    let results = core.command_bar_search_with_tabs(query_str, tabs);
    to_json_cstring(&results)
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new` or `maho_core_new_with_storage`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_clear_history(ptr: *mut MahoCore) {
    if ptr.is_null() {
        return;
    }
    let core = &mut *ptr;
    core.clear_history();
}

/// # Safety
/// `path` must be a valid null-terminated C string pointing to a directory.
/// The caller must ensure the directory exists or can be created.
/// Returns a pointer to a new MahoCore instance with both LMDB and SQLite storage initialized.
/// Returns null on error. Caller must free with `maho_core_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_new_with_storage(path: *const c_char) -> *mut MahoCore {
    if path.is_null() {
        return ptr::null_mut();
    }
    let path_str = match CStr::from_ptr(path).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    let base_path = Path::new(path_str);

    let lmdb_path = base_path.join("state");
    if std::fs::create_dir_all(&lmdb_path).is_err() {
        return ptr::null_mut();
    }

    let sqlite_path = base_path.join("maho.db");
    let sqlite_path_str = match sqlite_path.to_str() {
        Some(s) => s,
        None => return ptr::null_mut(),
    };

    let core = MahoCore::new()
        .with_storage(sqlite_path_str)
        .with_lmdb_storage(&lmdb_path);

    Box::into_raw(Box::new(core))
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new` or `maho_core_new_with_storage`.
/// Returns 1 on success, 0 on error.
#[no_mangle]
pub unsafe extern "C" fn maho_core_save_state(ptr: *mut MahoCore) -> i32 {
    if ptr.is_null() {
        return 0;
    }
    let core = &*ptr;
    match core.save_state() {
        Ok(_) => 1,
        Err(_) => 0,
    }
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new` or `maho_core_new_with_storage`.
/// Returns 1 on success, 0 on error.
#[no_mangle]
pub unsafe extern "C" fn maho_core_load_state(ptr: *mut MahoCore) -> i32 {
    if ptr.is_null() {
        return 0;
    }
    let core = &mut *ptr;
    match core.load_state() {
        Ok(_) => 1,
        Err(_) => 0,
    }
}

/// Sets or clears the parent of a tab (tree reparenting).
/// `new_parent_id_ptr` can be null to clear the parent.
///
/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new` or `maho_core_new_with_storage`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_set_tab_parent(
    ptr: *mut MahoCore,
    tab_id_ptr: *const std::os::raw::c_char,
    new_parent_id_ptr: *const std::os::raw::c_char,
) {
    if ptr.is_null() || tab_id_ptr.is_null() {
        return;
    }
    let core = &mut *ptr;
    let tab_id_str = std::ffi::CStr::from_ptr(tab_id_ptr).to_string_lossy();
    let tab_id = maho_types::identifiers::TabId::new(tab_id_str.as_ref());

    let new_parent_id = if new_parent_id_ptr.is_null() {
        None
    } else {
        let parent_str = std::ffi::CStr::from_ptr(new_parent_id_ptr).to_string_lossy();
        Some(maho_types::identifiers::TabId::new(parent_str.as_ref()))
    };

    let event = maho_types::events::shell_event::ShellEvent::SetTabParent {
        tab_id,
        new_parent_id,
    };
    core.handle_event(event);
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new` or `maho_core_new_with_storage`.
/// `tab_id_ptr` must be a valid null-terminated C string.
/// `title_ptr` may be null to clear the custom title.
#[no_mangle]
pub unsafe extern "C" fn maho_core_set_tab_custom_title(
    ptr: *mut MahoCore,
    tab_id_ptr: *const std::os::raw::c_char,
    title_ptr: *const std::os::raw::c_char,
) {
    if ptr.is_null() || tab_id_ptr.is_null() {
        return;
    }
    let core = &mut *ptr;
    let tab_id_str = std::ffi::CStr::from_ptr(tab_id_ptr).to_string_lossy();
    let tab_id = maho_types::identifiers::TabId::new(tab_id_str.as_ref());

    let custom_title = if title_ptr.is_null() {
        None
    } else {
        let title_str = std::ffi::CStr::from_ptr(title_ptr).to_string_lossy();
        let trimmed = title_str.trim();
        if trimmed.is_empty() { None } else { Some(trimmed.to_string()) }
    };

    let event = maho_types::events::shell_event::ShellEvent::SetTabCustomTitle {
        tab_id,
        custom_title,
    };
    core.handle_event(event);
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `url` and `title` must be valid null-terminated C strings.
/// `folder_id` may be null to use the default folder.
#[no_mangle]
pub unsafe extern "C" fn maho_core_add_bookmark(
    ptr: *mut MahoCore,
    url: *const c_char,
    title: *const c_char,
    folder_id: *const c_char,
) {
    if ptr.is_null() || url.is_null() || title.is_null() {
        return;
    }
    let core = &mut *ptr;
    let url_str = match CStr::from_ptr(url).to_str() {
        Ok(s) => s,
        Err(_) => return,
    };
    let title_str = match CStr::from_ptr(title).to_str() {
        Ok(s) => s,
        Err(_) => return,
    };
    let folder_id_opt = if folder_id.is_null() {
        None
    } else {
        CStr::from_ptr(folder_id).to_str().ok()
    };
    core.add_bookmark(url_str, title_str, folder_id_opt);
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `bookmark_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_remove_bookmark(ptr: *mut MahoCore, bookmark_id: *const c_char) {
    if ptr.is_null() || bookmark_id.is_null() {
        return;
    }
    let core = &mut *ptr;
    let bookmark_id_str = match CStr::from_ptr(bookmark_id).to_str() {
        Ok(s) => s,
        Err(_) => return,
    };
    core.remove_bookmark(bookmark_id_str);
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `query` must be a valid null-terminated C string.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_search_bookmarks(
    ptr: *mut MahoCore,
    query: *const c_char,
) -> *mut c_char {
    if ptr.is_null() || query.is_null() {
        return ptr::null_mut();
    }
    let core = &*ptr;
    let query_str = match CStr::from_ptr(query).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    to_json_cstring(&core.search_bookmarks(query_str))
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `bookmark_id` and `folder_id` must be valid null-terminated C strings.
/// `folder_id` may be null to move to root.
#[no_mangle]
pub unsafe extern "C" fn maho_core_move_bookmark(
    ptr: *mut MahoCore,
    bookmark_id: *const c_char,
    folder_id: *const c_char,
) {
    if ptr.is_null() || bookmark_id.is_null() {
        return;
    }
    let core = &mut *ptr;
    let bookmark_id_str = match CStr::from_ptr(bookmark_id).to_str() {
        Ok(s) => s,
        Err(_) => return,
    };
    let folder_id_opt = if folder_id.is_null() {
        None
    } else {
        CStr::from_ptr(folder_id).to_str().ok()
    };
    core.move_bookmark(bookmark_id_str, folder_id_opt);
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `origin` and `permission` must be valid null-terminated C strings.
#[no_mangle]
pub unsafe extern "C" fn maho_core_grant_permission(
    ptr: *mut MahoCore,
    origin: *const c_char,
    permission: *const c_char,
) {
    if ptr.is_null() || origin.is_null() || permission.is_null() {
        return;
    }
    let core = &mut *ptr;
    let origin_str = match CStr::from_ptr(origin).to_str() {
        Ok(s) => s,
        Err(_) => return,
    };
    let permission_str = match CStr::from_ptr(permission).to_str() {
        Ok(s) => s,
        Err(_) => return,
    };
    core.grant_permission(origin_str, permission_str);
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `origin` and `permission` must be valid null-terminated C strings.
#[no_mangle]
pub unsafe extern "C" fn maho_core_revoke_permission(
    ptr: *mut MahoCore,
    origin: *const c_char,
    permission: *const c_char,
) {
    if ptr.is_null() || origin.is_null() || permission.is_null() {
        return;
    }
    let core = &mut *ptr;
    let origin_str = match CStr::from_ptr(origin).to_str() {
        Ok(s) => s,
        Err(_) => return,
    };
    let permission_str = match CStr::from_ptr(permission).to_str() {
        Ok(s) => s,
        Err(_) => return,
    };
    core.revoke_permission(origin_str, permission_str);
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `origin` and `permission` must be valid null-terminated C strings.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_query_permission(
    ptr: *mut MahoCore,
    origin: *const c_char,
    permission: *const c_char,
) -> *mut c_char {
    if ptr.is_null() || origin.is_null() || permission.is_null() {
        return ptr::null_mut();
    }
    let core = &*ptr;
    let origin_str = match CStr::from_ptr(origin).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    let permission_str = match CStr::from_ptr(permission).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    to_json_cstring(&core.query_permission(origin_str, permission_str))
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `download_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_pause_download(ptr: *mut MahoCore, download_id: *const c_char) {
    if ptr.is_null() || download_id.is_null() {
        return;
    }
    let core = &mut *ptr;
    let download_id_str = match CStr::from_ptr(download_id).to_str() {
        Ok(s) => s,
        Err(_) => return,
    };
    core.pause_download(download_id_str);
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `download_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_resume_download(ptr: *mut MahoCore, download_id: *const c_char) {
    if ptr.is_null() || download_id.is_null() {
        return;
    }
    let core = &mut *ptr;
    let download_id_str = match CStr::from_ptr(download_id).to_str() {
        Ok(s) => s,
        Err(_) => return,
    };
    core.resume_download(download_id_str);
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `download_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_cancel_download(ptr: *mut MahoCore, download_id: *const c_char) {
    if ptr.is_null() || download_id.is_null() {
        return;
    }
    let core = &mut *ptr;
    let download_id_str = match CStr::from_ptr(download_id).to_str() {
        Ok(s) => s,
        Err(_) => return,
    };
    core.cancel_download(download_id_str);
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `download_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_remove_download(ptr: *mut MahoCore, download_id: *const c_char) {
    if ptr.is_null() || download_id.is_null() {
        return;
    }
    let core = &mut *ptr;
    let download_id_str = match CStr::from_ptr(download_id).to_str() {
        Ok(s) => s,
        Err(_) => return,
    };
    core.remove_download(download_id_str);
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `entry_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_delete_history_entry(
    ptr: *mut MahoCore,
    entry_id: *const c_char,
) {
    if ptr.is_null() || entry_id.is_null() {
        return;
    }
    let core = &mut *ptr;
    let entry_id_str = match CStr::from_ptr(entry_id).to_str() {
        Ok(s) => s,
        Err(_) => return,
    };
    core.delete_history_entry(entry_id_str);
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `tab_id` must be a valid null-terminated C string.
/// `data` must point to `data_len` valid bytes.
#[no_mangle]
pub unsafe extern "C" fn maho_core_update_tab_preview(
    ptr: *mut MahoCore,
    tab_id: *const c_char,
    data: *const u8,
    data_len: usize,
) {
    if ptr.is_null() || tab_id.is_null() || data.is_null() {
        return;
    }
    let core = &mut *ptr;
    let tab_id_str = match CStr::from_ptr(tab_id).to_str() {
        Ok(s) => s,
        Err(_) => return,
    };
    let image_bytes = std::slice::from_raw_parts(data, data_len).to_vec();
    let image_data = maho_types::common::ImageData {
        data: image_bytes,
        width: 0,
        height: 0,
        format: maho_types::common::ImageFormat::Png,
    };
    let tid = maho_types::identifiers::TabId::new(tab_id_str);
    core.update_tab_preview(&tid, image_data);
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `tab_id` must be a valid null-terminated C string.
/// `out_len` must be a valid pointer to a `usize`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_tab_preview(
    ptr: *mut MahoCore,
    tab_id: *const c_char,
    out_len: *mut usize,
) -> *mut u8 {
    if ptr.is_null() || tab_id.is_null() || out_len.is_null() {
        return ptr::null_mut();
    }
    let core = &*ptr;
    let tab_id_str = match CStr::from_ptr(tab_id).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    let tid = maho_types::identifiers::TabId::new(tab_id_str);
    match core.get_tab_preview(&tid) {
        Some(preview) => match &preview.thumbnail {
            Some(img) => {
                let bytes = img.data.clone();
                *out_len = bytes.len();
                let boxed = bytes.into_boxed_slice();
                Box::into_raw(boxed) as *mut u8
            }
            None => {
                *out_len = 0;
                ptr::null_mut()
            }
        },
        None => {
            *out_len = 0;
            ptr::null_mut()
        }
    }
}

/// # Safety
/// `data` must have been returned by `maho_core_get_tab_preview` with matching `len`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_free_preview_data(data: *mut u8, len: usize) {
    if !data.is_null() && len > 0 {
        let _ = Vec::from_raw_parts(data, len, len);
    }
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_take_pending_preview_captures(
    ptr: *mut MahoCore,
) -> *mut c_char {
    if ptr.is_null() {
        return ptr::null_mut();
    }
    let core = &mut *ptr;
    let captures = core.take_pending_preview_captures();
    let tab_ids: Vec<String> = captures.into_iter().map(|id| id.to_string()).collect();
    to_json_cstring(&tab_ids)
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `tab_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_schedule_preview_capture(
    ptr: *mut MahoCore,
    tab_id: *const c_char,
) -> bool {
    if ptr.is_null() || tab_id.is_null() {
        return false;
    }
    let core = &mut *ptr;
    let tab_id_str = match CStr::from_ptr(tab_id).to_str() {
        Ok(s) => s,
        Err(_) => return false,
    };
    let tid = maho_types::identifiers::TabId::new(tab_id_str);
    core.schedule_preview_capture(tid)
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_all_bookmarks(ptr: *mut MahoCore) -> *mut c_char {
    if ptr.is_null() {
        return ptr::null_mut();
    }
    let core = &*ptr;
    let bookmarks: Vec<_> = core
        .get_all_bookmark_entries()
        .iter()
        .map(|b| {
            serde_json::json!({
                "id": b.id.0,
                "title": b.title,
                "url": b.url,
                "folderId": b.folder_id,
                "favicon": b.favicon,
                "createdAt": b.created_at.to_string(),
            })
        })
        .collect();
    to_json_cstring(&bookmarks)
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `name` must be a valid null-terminated C string.
/// `parent_id` may be null to create a root-level folder.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_create_bookmark_folder(
    ptr: *mut MahoCore,
    name: *const c_char,
    parent_id: *const c_char,
) -> *mut c_char {
    if ptr.is_null() || name.is_null() {
        return ptr::null_mut();
    }
    let core = &mut *ptr;
    let name_str = match CStr::from_ptr(name).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    let parent_id_opt = if parent_id.is_null() {
        None
    } else {
        CStr::from_ptr(parent_id).to_str().ok().map(String::from)
    };
    let folder = core.create_bookmark_folder(name_str.to_string(), parent_id_opt);
    let result = serde_json::json!({
        "id": folder.id,
        "name": folder.name,
        "parentId": folder.parent_id,
        "createdAt": folder.created_at.to_string(),
    });
    to_json_cstring(&result)
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `json` must be a valid null-terminated C string containing JSON with
/// `filename`, `url`, `total_bytes`, and optionally `file_path` and `mime_type`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_start_download(
    ptr: *mut MahoCore,
    json: *const c_char,
) -> *mut c_char {
    if ptr.is_null() || json.is_null() {
        return ptr::null_mut();
    }
    let core = &mut *ptr;
    let json_str = match CStr::from_ptr(json).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    let parsed: serde_json::Value = match serde_json::from_str(json_str) {
        Ok(v) => v,
        Err(_) => return ptr::null_mut(),
    };
    let filename = match parsed["filename"].as_str() {
        Some(s) => s,
        None => return ptr::null_mut(),
    };
    let url = match parsed["url"].as_str() {
        Some(s) => s,
        None => return ptr::null_mut(),
    };
    let total_bytes = parsed["total_bytes"].as_u64().unwrap_or(0);
    let file_path = parsed["file_path"].as_str();
    let mime_type = parsed["mime_type"].as_str();
    let download_id = core.start_download(filename, url, total_bytes, file_path, mime_type);
    to_json_cstring(&download_id)
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `download_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_update_download_progress(
    ptr: *mut MahoCore,
    download_id: *const c_char,
    received_bytes: u64,
) {
    if ptr.is_null() || download_id.is_null() {
        return;
    }
    let core = &mut *ptr;
    let download_id_str = match CStr::from_ptr(download_id).to_str() {
        Ok(s) => s,
        Err(_) => return,
    };
    core.update_download_progress(download_id_str, received_bytes);
}

#[no_mangle]
pub unsafe extern "C" fn maho_core_complete_download(
    ptr: *mut MahoCore,
    download_id: *const c_char,
) -> *mut c_char {
    if ptr.is_null() || download_id.is_null() {
        return ptr::null_mut();
    }
    let core = &mut *ptr;
    let download_id_str = match CStr::from_ptr(download_id).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    let updates = core.complete_download(download_id_str);
    to_json_cstring(&updates)
}

#[no_mangle]
pub unsafe extern "C" fn maho_core_restore_download_name(
    ptr: *mut MahoCore,
    download_id: *const c_char,
) -> *mut c_char {
    if ptr.is_null() || download_id.is_null() {
        return ptr::null_mut();
    }
    let core = &mut *ptr;
    let download_id_str = match CStr::from_ptr(download_id).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    if let Some(update) = core.restore_download_name(download_id_str) {
        to_json_cstring(&vec![update])
    } else {
        to_json_cstring(&Vec::<maho_types::events::core_update::CoreUpdate>::new())
    }
}



#[no_mangle]
pub unsafe extern "C" fn maho_core_get_filter_lists(ptr: *mut MahoCore) -> *mut c_char {
    if ptr.is_null() {
        return std::ptr::null_mut();
    }
    let core = &*ptr;
    let json = core.get_filter_lists_json();
    match CString::new(json) {
        Ok(s) => s.into_raw(),
        Err(_) => std::ptr::null_mut(),
    }
}

#[no_mangle]
pub unsafe extern "C" fn maho_core_add_filter_list(
    ptr: *mut MahoCore,
    id: *const c_char,
    name: *const c_char,
    url: *const c_char,
) {
    if ptr.is_null() || id.is_null() || name.is_null() || url.is_null() {
        return;
    }
    let core = &mut *ptr;
    let id_str = match CStr::from_ptr(id).to_str() {
        Ok(s) => s.to_string(),
        Err(_) => return,
    };
    let name_str = match CStr::from_ptr(name).to_str() {
        Ok(s) => s.to_string(),
        Err(_) => return,
    };
    let url_str = match CStr::from_ptr(url).to_str() {
        Ok(s) => s.to_string(),
        Err(_) => return,
    };
    core.add_filter_list(id_str, name_str, url_str);
}

#[no_mangle]
pub unsafe extern "C" fn maho_core_update_filter_list_content(
    ptr: *mut MahoCore,
    id: *const c_char,
    content: *const c_char,
) {
    if ptr.is_null() || id.is_null() || content.is_null() {
        return;
    }
    let core = &mut *ptr;
    let id_str = match CStr::from_ptr(id).to_str() {
        Ok(s) => s,
        Err(_) => return,
    };
    let content_str = match CStr::from_ptr(content).to_str() {
        Ok(s) => s.to_string(),
        Err(_) => return,
    };
    core.update_filter_list_content(id_str, content_str);
}

#[no_mangle]
pub unsafe extern "C" fn maho_core_rebuild_content_rules(ptr: *mut MahoCore) {
    if ptr.is_null() {
        return;
    }
    let core = &mut *ptr;
    core.rebuild_content_rules();
}

#[no_mangle]
pub unsafe extern "C" fn maho_core_get_content_rule_count(ptr: *mut MahoCore) -> usize {
    if ptr.is_null() {
        return 0;
    }
    let core = &*ptr;
    core.get_content_rule_count()
}

#[no_mangle]
/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
/// `query` must be a valid null-terminated UTF-8 string.
pub unsafe extern "C" fn maho_core_get_search_url(
    core: *mut MahoCore,
    query: *const c_char,
) -> *mut c_char {
    let core = match core.as_ref() {
        Some(c) => c,
        None => return std::ptr::null_mut(),
    };
    if query.is_null() {
        return std::ptr::null_mut();
    }
    let query_str = match CStr::from_ptr(query).to_str() {
        Ok(s) => s,
        Err(_) => return std::ptr::null_mut(),
    };
    let url = core.get_search_url(query_str);
    match CString::new(url) {
        Ok(s) => s.into_raw(),
        Err(_) => std::ptr::null_mut(),
    }
}

#[no_mangle]
/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
pub unsafe extern "C" fn maho_core_get_search_engines(core: *mut MahoCore) -> *mut c_char {
    let core = unsafe {
        if core.is_null() {
            return std::ptr::null_mut();
        }
        &*core
    };
    let engines = core.get_search_engines();
    match serde_json::to_string(&engines) {
        Ok(json) => match CString::new(json) {
            Ok(s) => s.into_raw(),
            Err(_) => std::ptr::null_mut(),
        },
        Err(_) => std::ptr::null_mut(),
    }
}

#[no_mangle]
/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
pub unsafe extern "C" fn maho_core_get_site_search_entries(core: *mut MahoCore) -> *mut c_char {
    let core = match core.as_ref() {
        Some(c) => c,
        None => return std::ptr::null_mut(),
    };
    let entries = &core.get_settings().general.site_search_entries;
    to_json_cstring(entries)
}

#[no_mangle]
/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
/// `json` must be a valid null-terminated UTF-8 string.
pub unsafe extern "C" fn maho_core_set_site_search_entries(
    core: *mut MahoCore,
    json: *const c_char,
) {
    let core = match core.as_mut() {
        Some(c) => c,
        None => return,
    };
    let json_str = match CStr::from_ptr(json).to_str() {
        Ok(s) => s,
        Err(_) => return,
    };
    if let Ok(entries) =
        serde_json::from_str::<Vec<maho_types::settings::SiteSearchEntry>>(json_str)
    {
        let update = maho_types::settings::SettingsUpdate {
            general: Some(maho_types::settings::GeneralSettingsUpdate {
                site_search_entries: Some(entries),
                ..Default::default()
            }),
            ..Default::default()
        };
        core.update_settings(update);
    }
}

#[no_mangle]
/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
pub unsafe extern "C" fn maho_core_get_recent_searches(core: *mut MahoCore) -> *mut c_char {
    let core = unsafe {
        if core.is_null() {
            return std::ptr::null_mut();
        }
        &*core
    };
    let searches = core.get_recent_searches();
    match serde_json::to_string(&searches) {
        Ok(json) => match CString::new(json) {
            Ok(s) => s.into_raw(),
            Err(_) => std::ptr::null_mut(),
        },
        Err(_) => std::ptr::null_mut(),
    }
}

#[no_mangle]
/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
/// `query` must be a valid null-terminated C string.
pub unsafe extern "C" fn maho_core_save_search(core: *mut MahoCore, query: *const c_char) {
    let core = unsafe {
        if core.is_null() {
            return;
        }
        &mut *core
    };
    let query_str = unsafe {
        if query.is_null() {
            return;
        }
        match CStr::from_ptr(query).to_str() {
            Ok(s) => s.to_string(),
            Err(_) => return,
        }
    };
    core.save_search(query_str);
}

#[no_mangle]
/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
pub unsafe extern "C" fn maho_core_get_notifications(core: *mut MahoCore) -> *mut c_char {
    let core = unsafe {
        if core.is_null() {
            return std::ptr::null_mut();
        }
        &*core
    };
    let notifications = core.get_notifications();
    match serde_json::to_string(&notifications) {
        Ok(json) => match CString::new(json) {
            Ok(s) => s.into_raw(),
            Err(_) => std::ptr::null_mut(),
        },
        Err(_) => std::ptr::null_mut(),
    }
}

#[no_mangle]
/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
/// `notification_id` must be a valid null-terminated C string.
pub unsafe extern "C" fn maho_core_dismiss_notification(
    core: *mut MahoCore,
    notification_id: *const c_char,
) {
    let core = unsafe {
        if core.is_null() {
            return;
        }
        &mut *core
    };
    let id = unsafe {
        if notification_id.is_null() {
            return;
        }
        match CStr::from_ptr(notification_id).to_str() {
            Ok(s) => s.to_string(),
            Err(_) => return,
        }
    };
    core.dismiss_notification(&id);
}

#[no_mangle]
/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
pub unsafe extern "C" fn maho_core_dismiss_all_notifications(core: *mut MahoCore) {
    let core = unsafe {
        if core.is_null() {
            return;
        }
        &mut *core
    };
    core.dismiss_all_notifications();
}

#[no_mangle]
/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
/// `origin` must be a valid null-terminated C string.
pub unsafe extern "C" fn maho_core_set_notification_filter(
    core: *mut MahoCore,
    origin: *const c_char,
    allowed: bool,
) {
    let core = unsafe {
        if core.is_null() {
            return;
        }
        &mut *core
    };
    let origin_str = unsafe {
        if origin.is_null() {
            return;
        }
        match CStr::from_ptr(origin).to_str() {
            Ok(s) => s.to_string(),
            Err(_) => return,
        }
    };
    core.set_notification_filter(origin_str, allowed);
}

#[no_mangle]
/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
/// `site` must be a valid null-terminated C string.
pub unsafe extern "C" fn maho_core_get_zoom(core: *mut MahoCore, site: *const c_char) -> f64 {
    let core = unsafe {
        if core.is_null() {
            return 1.0;
        }
        &*core
    };
    let site_str = unsafe {
        if site.is_null() {
            return 1.0;
        }
        match CStr::from_ptr(site).to_str() {
            Ok(s) => s,
            Err(_) => return 1.0,
        }
    };
    core.get_zoom_for_site(site_str)
}

#[no_mangle]
/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
/// `site` must be a valid null-terminated C string.
pub unsafe extern "C" fn maho_core_set_zoom(core: *mut MahoCore, site: *const c_char, zoom: f64) {
    let core = unsafe {
        if core.is_null() {
            return;
        }
        &mut *core
    };
    let site_str = unsafe {
        if site.is_null() {
            return;
        }
        match CStr::from_ptr(site).to_str() {
            Ok(s) => s.to_string(),
            Err(_) => return,
        }
    };
    core.set_zoom_for_site(site_str, zoom);
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `tab_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_toggle_reader_mode(
    ptr: *mut MahoCore,
    tab_id: *const c_char,
) -> bool {
    if ptr.is_null() || tab_id.is_null() {
        return false;
    }
    let core = &mut *ptr;
    let tab_id_str = match CStr::from_ptr(tab_id).to_str() {
        Ok(s) => s,
        Err(_) => return false,
    };
    core.toggle_reader_mode(tab_id_str)
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `tab_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_is_reader_mode(
    ptr: *mut MahoCore,
    tab_id: *const c_char,
) -> bool {
    if ptr.is_null() || tab_id.is_null() {
        return false;
    }
    let core = &mut *ptr;
    let tab_id_str = match CStr::from_ptr(tab_id).to_str() {
        Ok(s) => s,
        Err(_) => return false,
    };
    core.is_reader_mode(tab_id_str)
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `item_key` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_record_usage(ptr: *mut MahoCore, item_key: *const c_char) {
    if ptr.is_null() || item_key.is_null() {
        return;
    }
    let core = &mut *ptr;
    let key = match CStr::from_ptr(item_key).to_str() {
        Ok(s) => s,
        Err(_) => return,
    };
    core.record_command_bar_usage(key);
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Returns a JSON array of [key, count] pairs. Caller must free with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_top_used(ptr: *mut MahoCore, limit: usize) -> *mut c_char {
    if ptr.is_null() {
        return ptr::null_mut();
    }
    let core = &*ptr;
    let results = core.get_top_used_commands(limit);
    to_json_cstring(&results)
}

/// # Safety
/// `ptr` must be valid. `note_id` and `tab_id` must be valid null-terminated C strings.
#[no_mangle]
pub unsafe extern "C" fn maho_core_link_note_to_tab(
    ptr: *mut MahoCore,
    note_id: *const c_char,
    tab_id: *const c_char,
) {
    if ptr.is_null() || note_id.is_null() || tab_id.is_null() {
        return;
    }
    let core = &mut *ptr;
    let nid = match CStr::from_ptr(note_id).to_str() {
        Ok(s) => s,
        Err(_) => return,
    };
    let tid = match CStr::from_ptr(tab_id).to_str() {
        Ok(s) => s,
        Err(_) => return,
    };
    core.link_note_to_tab(nid, tid);
}

/// # Safety
/// `ptr` must be valid. `note_id` and `url` must be valid null-terminated C strings.
#[no_mangle]
pub unsafe extern "C" fn maho_core_link_note_to_url(
    ptr: *mut MahoCore,
    note_id: *const c_char,
    url: *const c_char,
) {
    if ptr.is_null() || note_id.is_null() || url.is_null() {
        return;
    }
    let core = &mut *ptr;
    let nid = match CStr::from_ptr(note_id).to_str() {
        Ok(s) => s,
        Err(_) => return,
    };
    let u = match CStr::from_ptr(url).to_str() {
        Ok(s) => s,
        Err(_) => return,
    };
    core.link_note_to_url(nid, u);
}

/// # Safety
/// `ptr` must be valid. `note_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_unlink_note_from_tab(
    ptr: *mut MahoCore,
    note_id: *const c_char,
) {
    if ptr.is_null() || note_id.is_null() {
        return;
    }
    let core = &mut *ptr;
    let nid = match CStr::from_ptr(note_id).to_str() {
        Ok(s) => s,
        Err(_) => return,
    };
    core.unlink_note_from_tab(nid);
}

/// # Safety
/// `ptr` must be valid. `note_id` must be a valid null-terminated C string.
/// Returns the linked tab ID or null. Caller must free with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_linked_tab_id(
    ptr: *mut MahoCore,
    note_id: *const c_char,
) -> *mut c_char {
    if ptr.is_null() || note_id.is_null() {
        return ptr::null_mut();
    }
    let core = &*ptr;
    let nid = match CStr::from_ptr(note_id).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    match core.get_linked_tab_id(nid) {
        Some(tid) => CString::new(tid)
            .map(CString::into_raw)
            .unwrap_or(ptr::null_mut()),
        None => ptr::null_mut(),
    }
}

/// # Safety
/// `ptr` must be valid. `format` must be a valid null-terminated C string ("markdown", "html", "json").
/// Returns exported string. Caller must free with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_export_notes(
    ptr: *mut MahoCore,
    format: *const c_char,
) -> *mut c_char {
    if ptr.is_null() || format.is_null() {
        return ptr::null_mut();
    }
    let core = &*ptr;
    let fmt = match CStr::from_ptr(format).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    let result = core.export_notes(fmt);
    CString::new(result)
        .map(CString::into_raw)
        .unwrap_or(ptr::null_mut())
}

/// # Safety
/// `ptr` must be valid. `note_id` and `format` must be valid null-terminated C strings.
/// Returns exported string or null. Caller must free with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_export_single_note(
    ptr: *mut MahoCore,
    note_id: *const c_char,
    format: *const c_char,
) -> *mut c_char {
    if ptr.is_null() || note_id.is_null() || format.is_null() {
        return ptr::null_mut();
    }
    let core = &*ptr;
    let nid = match CStr::from_ptr(note_id).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    let fmt = match CStr::from_ptr(format).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    match core.export_single_note(nid, fmt) {
        Some(result) => CString::new(result)
            .map(CString::into_raw)
            .unwrap_or(ptr::null_mut()),
        None => ptr::null_mut(),
    }
}

/// # Safety
/// `ptr` must be valid. `query` must be a valid null-terminated C string.
/// Returns JSON array of [note_id, rank] pairs. Caller must free with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_search_notes_fts(
    ptr: *mut MahoCore,
    query: *const c_char,
) -> *mut c_char {
    if ptr.is_null() || query.is_null() {
        return ptr::null_mut();
    }
    let core = &*ptr;
    let q = match CStr::from_ptr(query).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    let results = core.search_notes_fts(q);
    to_json_cstring(&results)
}

/// # Safety
/// `ptr` must be valid.
/// Returns JSON array of boost templates. Caller must free with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_boost_templates(ptr: *mut MahoCore) -> *mut c_char {
    if ptr.is_null() {
        return ptr::null_mut();
    }
    let core = &*ptr;
    let json = core.get_boost_templates_json();
    CString::new(json)
        .map(CString::into_raw)
        .unwrap_or(ptr::null_mut())
}

/// # Safety
/// `ptr` must be valid. `template_id` and `url_pattern` must be valid null-terminated C strings.
/// Returns JSON boost view model or null. Caller must free with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_create_boost_from_template(
    ptr: *mut MahoCore,
    template_id: *const c_char,
    url_pattern: *const c_char,
) -> *mut c_char {
    if ptr.is_null() || template_id.is_null() || url_pattern.is_null() {
        return ptr::null_mut();
    }
    let core = &mut *ptr;
    let tid = match CStr::from_ptr(template_id).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    let pat = match CStr::from_ptr(url_pattern).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    match core.create_boost_from_template(tid, pat) {
        Some(json) => CString::new(json)
            .map(CString::into_raw)
            .unwrap_or(ptr::null_mut()),
        None => ptr::null_mut(),
    }
}

/// # Safety
/// `ptr` must be valid. `url` and `space_id` must be valid null-terminated C strings.
/// Returns JSON array of boost view models. Caller must free with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_boosts_for_url_in_space(
    ptr: *mut MahoCore,
    url: *const c_char,
    space_id: *const c_char,
) -> *mut c_char {
    if ptr.is_null() || url.is_null() || space_id.is_null() {
        return ptr::null_mut();
    }
    let core = &*ptr;
    let u = match CStr::from_ptr(url).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    let sid = match CStr::from_ptr(space_id).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    let results = core.get_boosts_for_url_in_space(u, sid);
    to_json_cstring(&results)
}

/// # Safety
/// `ptr` must be valid.
/// Returns JSON reader settings. Caller must free with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_reader_settings(ptr: *mut MahoCore) -> *mut c_char {
    if ptr.is_null() {
        return ptr::null_mut();
    }
    let core = &*ptr;
    let json = core.get_reader_settings_json();
    CString::new(json)
        .map(CString::into_raw)
        .unwrap_or(ptr::null_mut())
}

#[no_mangle]
pub unsafe extern "C" fn maho_core_create_backup(
    ptr: *mut MahoCore,
    config_json: *const c_char,
) -> *mut c_char {
    if ptr.is_null() || config_json.is_null() {
        return ptr::null_mut();
    }
    let core = &mut *ptr;
    let cfg = match CStr::from_ptr(config_json).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    match core.create_backup(cfg) {
        Ok(bytes) => {
            let encoded = base64_encode(&bytes);
            CString::new(encoded)
                .map(CString::into_raw)
                .unwrap_or(ptr::null_mut())
        }
        Err(_) => ptr::null_mut(),
    }
}

#[no_mangle]
pub unsafe extern "C" fn maho_core_restore_backup(
    ptr: *mut MahoCore,
    backup_b64: *const c_char,
    password: *const c_char,
) -> *mut c_char {
    if ptr.is_null() || backup_b64.is_null() {
        return ptr::null_mut();
    }
    let core = &mut *ptr;
    let b64 = match CStr::from_ptr(backup_b64).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    let pw = if password.is_null() {
        None
    } else {
        CStr::from_ptr(password).to_str().ok()
    };
    let bytes = match base64_decode(b64) {
        Some(b) => b,
        None => return ptr::null_mut(),
    };
    match core.restore_backup(&bytes, pw) {
        Ok(json) => CString::new(json)
            .map(CString::into_raw)
            .unwrap_or(ptr::null_mut()),
        Err(_) => ptr::null_mut(),
    }
}

#[no_mangle]
pub unsafe extern "C" fn maho_core_get_backup_history(ptr: *mut MahoCore) -> *mut c_char {
    if ptr.is_null() {
        return ptr::null_mut();
    }
    let core = &*ptr;
    let json = core.get_backup_history_json();
    CString::new(json)
        .map(CString::into_raw)
        .unwrap_or(ptr::null_mut())
}

#[no_mangle]
pub unsafe extern "C" fn maho_core_add_to_reading_list(
    ptr: *mut MahoCore,
    url: *const c_char,
    title: *const c_char,
) -> *mut c_char {
    if ptr.is_null() || url.is_null() || title.is_null() {
        return ptr::null_mut();
    }
    let core = &mut *ptr;
    let u = match CStr::from_ptr(url).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    let t = match CStr::from_ptr(title).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    let json = core.add_to_reading_list(u, t);
    CString::new(json)
        .map(CString::into_raw)
        .unwrap_or(ptr::null_mut())
}

#[no_mangle]
pub unsafe extern "C" fn maho_core_remove_from_reading_list(
    ptr: *mut MahoCore,
    item_id: *const c_char,
) -> bool {
    if ptr.is_null() || item_id.is_null() {
        return false;
    }
    let core = &mut *ptr;
    let id = match CStr::from_ptr(item_id).to_str() {
        Ok(s) => s,
        Err(_) => return false,
    };
    core.remove_from_reading_list(id)
}

#[no_mangle]
pub unsafe extern "C" fn maho_core_toggle_reading_list_read(
    ptr: *mut MahoCore,
    item_id: *const c_char,
) -> i8 {
    if ptr.is_null() || item_id.is_null() {
        return -1;
    }
    let core = &mut *ptr;
    let id = match CStr::from_ptr(item_id).to_str() {
        Ok(s) => s,
        Err(_) => return -1,
    };
    match core.toggle_reading_list_read(id) {
        Some(true) => 1,
        Some(false) => 0,
        None => -1,
    }
}

#[no_mangle]
pub unsafe extern "C" fn maho_core_get_reading_list(ptr: *mut MahoCore) -> *mut c_char {
    if ptr.is_null() {
        return ptr::null_mut();
    }
    let core = &*ptr;
    let json = core.get_reading_list_json();
    CString::new(json)
        .map(CString::into_raw)
        .unwrap_or(ptr::null_mut())
}

#[no_mangle]
pub unsafe extern "C" fn maho_core_get_unread_count(ptr: *mut MahoCore) -> usize {
    if ptr.is_null() {
        return 0;
    }
    let core = &*ptr;
    core.get_unread_count()
}

#[no_mangle]
pub unsafe extern "C" fn maho_core_detect_browser_profiles() -> *mut c_char {
    let json = MahoCore::detect_browser_profiles_json();
    CString::new(json)
        .map(CString::into_raw)
        .unwrap_or(ptr::null_mut())
}

#[no_mangle]
pub unsafe extern "C" fn maho_core_import_chrome_bookmarks(
    ptr: *mut MahoCore,
    profile_path: *const c_char,
) -> *mut c_char {
    if ptr.is_null() || profile_path.is_null() {
        return ptr::null_mut();
    }
    let core = &mut *ptr;
    let path = match CStr::from_ptr(profile_path).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    match core.import_chrome_bookmarks(path) {
        Ok(json) => CString::new(json)
            .map(CString::into_raw)
            .unwrap_or(ptr::null_mut()),
        Err(_) => ptr::null_mut(),
    }
}

#[no_mangle]
pub unsafe extern "C" fn maho_core_import_firefox_bookmarks(
    ptr: *mut MahoCore,
    profile_path: *const c_char,
) -> *mut c_char {
    if ptr.is_null() || profile_path.is_null() {
        return ptr::null_mut();
    }
    let core = &mut *ptr;
    let path = match CStr::from_ptr(profile_path).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    match core.import_firefox_bookmarks(path) {
        Ok(json) => CString::new(json)
            .map(CString::into_raw)
            .unwrap_or(ptr::null_mut()),
        Err(_) => ptr::null_mut(),
    }
}

fn base64_encode(data: &[u8]) -> String {
    const CHARS: &[u8] = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    let mut result = String::with_capacity(data.len().div_ceil(3) * 4);
    for chunk in data.chunks(3) {
        let b0 = chunk[0] as u32;
        let b1 = if chunk.len() > 1 { chunk[1] as u32 } else { 0 };
        let b2 = if chunk.len() > 2 { chunk[2] as u32 } else { 0 };
        let triple = (b0 << 16) | (b1 << 8) | b2;
        result.push(CHARS[((triple >> 18) & 0x3F) as usize] as char);
        result.push(CHARS[((triple >> 12) & 0x3F) as usize] as char);
        if chunk.len() > 1 {
            result.push(CHARS[((triple >> 6) & 0x3F) as usize] as char);
        } else {
            result.push('=');
        }
        if chunk.len() > 2 {
            result.push(CHARS[(triple & 0x3F) as usize] as char);
        } else {
            result.push('=');
        }
    }
    result
}

fn base64_decode(input: &str) -> Option<Vec<u8>> {
    fn char_to_val(c: u8) -> Option<u32> {
        match c {
            b'A'..=b'Z' => Some((c - b'A') as u32),
            b'a'..=b'z' => Some((c - b'a' + 26) as u32),
            b'0'..=b'9' => Some((c - b'0' + 52) as u32),
            b'+' => Some(62),
            b'/' => Some(63),
            b'=' => Some(0),
            _ => None,
        }
    }
    let bytes = input.as_bytes();
    if !bytes.len().is_multiple_of(4) {
        return None;
    }
    let mut result = Vec::with_capacity(bytes.len() / 4 * 3);
    for chunk in bytes.chunks(4) {
        let a = char_to_val(chunk[0])?;
        let b = char_to_val(chunk[1])?;
        let c = char_to_val(chunk[2])?;
        let d = char_to_val(chunk[3])?;
        let triple = (a << 18) | (b << 12) | (c << 6) | d;
        result.push(((triple >> 16) & 0xFF) as u8);
        if chunk[2] != b'=' {
            result.push(((triple >> 8) & 0xFF) as u8);
        }
        if chunk[3] != b'=' {
            result.push((triple & 0xFF) as u8);
        }
    }
    Some(result)
}

#[no_mangle]
pub unsafe extern "C" fn maho_core_get_shortcuts(ptr: *mut MahoCore) -> *mut c_char {
    if ptr.is_null() {
        return ptr::null_mut();
    }
    let core = &*ptr;
    let json = core.get_all_shortcuts_json();
    CString::new(json)
        .map(CString::into_raw)
        .unwrap_or(ptr::null_mut())
}

#[no_mangle]
pub unsafe extern "C" fn maho_core_set_shortcut(
    ptr: *mut MahoCore,
    action: *const c_char,
    key_combo_json: *const c_char,
) -> *mut c_char {
    if ptr.is_null() || action.is_null() || key_combo_json.is_null() {
        return ptr::null_mut();
    }
    let core = &mut *ptr;
    let action_str = match CStr::from_ptr(action).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    let combo_str = match CStr::from_ptr(key_combo_json).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    match core.set_shortcut_json(action_str, combo_str) {
        Ok(result) => CString::new(result)
            .map(CString::into_raw)
            .unwrap_or(ptr::null_mut()),
        Err(conflict_json) => CString::new(format!(r#"{{"error":{}}}"#, conflict_json))
            .map(CString::into_raw)
            .unwrap_or(ptr::null_mut()),
    }
}

#[no_mangle]
pub unsafe extern "C" fn maho_core_reset_shortcut(ptr: *mut MahoCore, action: *const c_char) {
    if ptr.is_null() || action.is_null() {
        return;
    }
    let core = &mut *ptr;
    if let Ok(s) = CStr::from_ptr(action).to_str() {
        core.reset_shortcut(s);
    }
}

#[no_mangle]
pub unsafe extern "C" fn maho_core_reset_all_shortcuts(ptr: *mut MahoCore) {
    if ptr.is_null() {
        return;
    }
    let core = &mut *ptr;
    core.reset_all_shortcuts();
}

#[no_mangle]
pub unsafe extern "C" fn maho_core_resolve_shortcut(
    ptr: *mut MahoCore,
    key_combo_json: *const c_char,
) -> *mut c_char {
    if ptr.is_null() || key_combo_json.is_null() {
        return ptr::null_mut();
    }
    let core = &*ptr;
    let combo_str = match CStr::from_ptr(key_combo_json).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    match core.resolve_shortcut_json(combo_str) {
        Some(action) => CString::new(action)
            .map(CString::into_raw)
            .unwrap_or(ptr::null_mut()),
        None => ptr::null_mut(),
    }
}

#[no_mangle]
pub unsafe extern "C" fn maho_core_set_density(ptr: *mut MahoCore, density: *const c_char) {
    if ptr.is_null() || density.is_null() {
        return;
    }
    let core = &mut *ptr;
    if let Ok(s) = CStr::from_ptr(density).to_str() {
        core.set_density(s);
    }
}

#[no_mangle]
pub unsafe extern "C" fn maho_core_set_accent_color(ptr: *mut MahoCore, color_json: *const c_char) {
    if ptr.is_null() || color_json.is_null() {
        return;
    }
    let core = &mut *ptr;
    if let Ok(s) = CStr::from_ptr(color_json).to_str() {
        core.set_accent_color(s);
    }
}

#[no_mangle]
pub unsafe extern "C" fn maho_core_set_custom_chrome_css(ptr: *mut MahoCore, css: *const c_char) {
    if ptr.is_null() {
        return;
    }
    let core = &mut *ptr;
    let css_value = if css.is_null() {
        None
    } else {
        CStr::from_ptr(css).to_str().ok().map(|s| s.to_string())
    };
    core.set_custom_chrome_css(css_value);
}

#[no_mangle]
pub unsafe extern "C" fn maho_core_set_window_transparency(ptr: *mut MahoCore, enabled: bool) {
    if ptr.is_null() {
        return;
    }
    let core = &mut *ptr;
    core.set_window_transparency(enabled);
}

#[no_mangle]
pub unsafe extern "C" fn maho_core_get_toolbar_items(ptr: *mut MahoCore) -> *mut c_char {
    if ptr.is_null() {
        return ptr::null_mut();
    }
    let core = &*ptr;
    let json = core.get_toolbar_items_json();
    CString::new(json)
        .map(CString::into_raw)
        .unwrap_or(ptr::null_mut())
}

#[no_mangle]
pub unsafe extern "C" fn maho_core_set_toolbar_items(
    ptr: *mut MahoCore,
    items_json: *const c_char,
) -> *mut c_char {
    if ptr.is_null() || items_json.is_null() {
        return ptr::null_mut();
    }
    let core = &mut *ptr;
    let json_str = match CStr::from_ptr(items_json).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    match core.set_toolbar_items_json(json_str) {
        Ok(()) => CString::new(r#"{"success":true}"#)
            .map(CString::into_raw)
            .unwrap_or(ptr::null_mut()),
        Err(e) => CString::new(format!(r#"{{"error":"{}"}}"#, e))
            .map(CString::into_raw)
            .unwrap_or(ptr::null_mut()),
    }
}

#[no_mangle]
pub unsafe extern "C" fn maho_core_set_app_icon(
    ptr: *mut MahoCore,
    path: *const c_char,
) -> *mut c_char {
    if ptr.is_null() {
        return ptr::null_mut();
    }
    let core = &mut *ptr;
    if path.is_null() {
        core.set_app_icon(None);
    } else {
        match CStr::from_ptr(path).to_str() {
            Ok(s) => {
                if s.is_empty() || s == "null" {
                    core.set_app_icon(None);
                } else {
                    core.set_app_icon(Some(s.to_string()));
                }
            }
            Err(_) => return ptr::null_mut(),
        }
    }
    CString::new(r#"{"ok":true}"#)
        .map(CString::into_raw)
        .unwrap_or(ptr::null_mut())
}

#[no_mangle]
pub unsafe extern "C" fn maho_core_get_default_toolbar_items(ptr: *mut MahoCore) -> *mut c_char {
    if ptr.is_null() {
        return ptr::null_mut();
    }
    let core = &*ptr;
    let json = core.get_default_toolbar_items_json();
    CString::new(json)
        .map(CString::into_raw)
        .unwrap_or(ptr::null_mut())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `url` must be a valid null-terminated C string.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_handle_url_scheme(
    ptr: *mut MahoCore,
    url: *const c_char,
) -> *mut c_char {
    if ptr.is_null() || url.is_null() {
        return ptr::null_mut();
    }
    let core = &mut *ptr;
    let url_str = match CStr::from_ptr(url).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    let result = core.handle_url_scheme(url_str);
    CString::new(result)
        .map(CString::into_raw)
        .unwrap_or(ptr::null_mut())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `level` must be a valid null-terminated C string containing one of:
/// "normal", "warning", "critical", or "extreme".
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_handle_memory_pressure(
    ptr: *mut MahoCore,
    level: *const c_char,
) -> *mut c_char {
    if ptr.is_null() || level.is_null() {
        return ptr::null_mut();
    }
    let core = &mut *ptr;
    let level_str = match CStr::from_ptr(level).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    let pressure_level = match level_str {
        "normal" => maho_types::common::MemoryPressureLevel::Normal,
        "warning" => maho_types::common::MemoryPressureLevel::Warning,
        "critical" => maho_types::common::MemoryPressureLevel::Critical,
        "extreme" => maho_types::common::MemoryPressureLevel::Extreme,
        _ => return ptr::null_mut(),
    };
    let result = core.handle_memory_pressure(pressure_level);
    CString::new(result)
        .map(CString::into_raw)
        .unwrap_or(ptr::null_mut())
}

/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
/// `tab_id` and `form_json` must be valid null-terminated C strings.
#[no_mangle]
pub unsafe extern "C" fn maho_core_save_form_data(
    core: *mut MahoCore,
    tab_id: *const c_char,
    form_json: *const c_char,
) {
    if core.is_null() || tab_id.is_null() || form_json.is_null() {
        return;
    }
    let core = unsafe { &mut *core };
    let tab_id = unsafe { CStr::from_ptr(tab_id) }.to_str().unwrap_or("");
    let form_json = unsafe { CStr::from_ptr(form_json) }.to_str().unwrap_or("");
    core.save_form_data(tab_id, form_json.to_string());
}

/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_set_crash_save_interval(core: *mut MahoCore, seconds: u64) {
    if core.is_null() {
        return;
    }
    let core = unsafe { &mut *core };
    core.set_crash_save_interval(seconds);
}

/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_searchable_items(core: *mut MahoCore) -> *mut c_char {
    if core.is_null() {
        return std::ptr::null_mut();
    }
    let core = &*core;
    let result = core.get_searchable_items();
    CString::new(result)
        .map(|s| s.into_raw())
        .unwrap_or(std::ptr::null_mut())
}

/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
/// `tab_id` and `query` must be valid null-terminated C strings.
#[no_mangle]
pub unsafe extern "C" fn maho_core_start_find(
    core: *mut MahoCore,
    tab_id: *const c_char,
    query: *const c_char,
    case_sensitive: bool,
    whole_word: bool,
) -> *mut c_char {
    if core.is_null() || tab_id.is_null() || query.is_null() {
        return ptr::null_mut();
    }
    let core = &mut *core;
    let tab_id_str = match CStr::from_ptr(tab_id).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    let query_str = match CStr::from_ptr(query).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    let tid = maho_types::identifiers::TabId::new(tab_id_str);
    let session = core.start_find(tid, query_str.to_string(), case_sensitive, whole_word);
    to_json_cstring(&session)
}

/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_find_next(core: *mut MahoCore) -> *mut c_char {
    if core.is_null() {
        return ptr::null_mut();
    }
    let core = &mut *core;
    let result = core.find_next();
    to_json_cstring(&result)
}

/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_find_previous(core: *mut MahoCore) -> *mut c_char {
    if core.is_null() {
        return ptr::null_mut();
    }
    let core = &mut *core;
    let result = core.find_previous();
    to_json_cstring(&result)
}

/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_dismiss_find(core: *mut MahoCore) {
    if core.is_null() {
        return;
    }
    let core = &mut *core;
    core.dismiss_find();
}

/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
/// `settings_json` must be a valid null-terminated C string containing JSON.
#[no_mangle]
pub unsafe extern "C" fn maho_core_update_settings(
    core: *mut MahoCore,
    settings_json: *const c_char,
) {
    if core.is_null() || settings_json.is_null() {
        return;
    }
    let core = &mut *core;
    let json_str = match CStr::from_ptr(settings_json).to_str() {
        Ok(s) => s,
        Err(_) => return,
    };
    let update: maho_types::settings::SettingsUpdate = match serde_json::from_str(json_str) {
        Ok(u) => u,
        Err(_) => return,
    };
    core.update_settings(update);
}

/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_atc_rules(core: *mut MahoCore) -> *mut c_char {
    if core.is_null() {
        return ptr::null_mut();
    }
    let core = &*core;
    let rules = core.get_atc_rules();
    to_json_cstring(&rules)
}

/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
/// `rule_json` must be a valid null-terminated C string containing JSON.
#[no_mangle]
pub unsafe extern "C" fn maho_core_add_atc_rule(
    core: *mut MahoCore,
    rule_json: *const c_char,
) -> *mut c_char {
    if core.is_null() || rule_json.is_null() {
        return ptr::null_mut();
    }
    let core = &mut *core;
    let json_str = match CStr::from_ptr(rule_json).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    let rule: maho_core::atc_manager::ATCRule = match serde_json::from_str(json_str) {
        Ok(r) => r,
        Err(_) => return ptr::null_mut(),
    };
    let id = core.add_atc_rule(rule);
    CString::new(id)
        .map(CString::into_raw)
        .unwrap_or(ptr::null_mut())
}

/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
/// `rule_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_remove_atc_rule(
    core: *mut MahoCore,
    rule_id: *const c_char,
) -> *mut c_char {
    if core.is_null() || rule_id.is_null() {
        return ptr::null_mut();
    }
    let core = &mut *core;
    let rule_id_str = match CStr::from_ptr(rule_id).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    let result = core.remove_atc_rule(rule_id_str);
    to_json_cstring(&result)
}

/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
/// `rule_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_toggle_atc_rule(
    core: *mut MahoCore,
    rule_id: *const c_char,
    enabled: bool,
) {
    if core.is_null() || rule_id.is_null() {
        return;
    }
    let core = &mut *core;
    let rule_id_str = match CStr::from_ptr(rule_id).to_str() {
        Ok(s) => s,
        Err(_) => return,
    };
    core.toggle_atc_rule(rule_id_str, enabled);
}

/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
/// `query` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_search_history_paginated(
    core: *mut MahoCore,
    query: *const c_char,
    limit: usize,
    offset: usize,
) -> *mut c_char {
    if core.is_null() || query.is_null() {
        return ptr::null_mut();
    }
    let core = &*core;
    let query_str = match CStr::from_ptr(query).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    let results = core.search_history_with_offset(query_str, limit, offset);
    to_json_cstring(&results)
}

/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_history_grouped_by_date(core: *mut MahoCore) -> *mut c_char {
    if core.is_null() {
        return ptr::null_mut();
    }
    let core = &*core;
    let results = core.get_history_grouped_by_date();
    to_json_cstring(&results)
}

/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
/// `folder_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_delete_bookmark_folder(
    core: *mut MahoCore,
    folder_id: *const c_char,
) -> *mut c_char {
    if core.is_null() || folder_id.is_null() {
        return ptr::null_mut();
    }
    let core = &mut *core;
    let folder_id_str = match CStr::from_ptr(folder_id).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    let result = core.delete_bookmark_folder(folder_id_str);
    to_json_cstring(&result)
}

// === Space Management ===

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `name`, `color_json`, `profile_id` must be valid null-terminated C strings.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_create_space(
    ptr: *mut MahoCore,
    name: *const c_char,
    color_json: *const c_char,
    profile_id: *const c_char,
) -> *mut c_char {
    if ptr.is_null() || name.is_null() || color_json.is_null() || profile_id.is_null() {
        return ptr::null_mut();
    }
    let core = &mut *ptr;
    let name_str = match CStr::from_ptr(name).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    let color: maho_types::space::SpaceColor = match CStr::from_ptr(color_json).to_str() {
        Ok(s) => match serde_json::from_str(s) {
            Ok(c) => c,
            Err(_) => return ptr::null_mut(),
        },
        Err(_) => return ptr::null_mut(),
    };
    let pid = match CStr::from_ptr(profile_id).to_str() {
        Ok(s) => maho_types::identifiers::ProfileId::new(s),
        Err(_) => return ptr::null_mut(),
    };
    let space = core.create_space(name_str, color, pid);
    to_json_cstring(&space)
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `space_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_delete_space(ptr: *mut MahoCore, space_id: *const c_char) {
    if ptr.is_null() || space_id.is_null() {
        return;
    }
    let core = &mut *ptr;
    let id_str = match CStr::from_ptr(space_id).to_str() {
        Ok(s) => s,
        Err(_) => return,
    };
    let id = maho_types::identifiers::SpaceId::new(id_str);
    core.delete_space(&id);
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `space_id` and `name` must be valid null-terminated C strings.
#[no_mangle]
pub unsafe extern "C" fn maho_core_rename_space(
    ptr: *mut MahoCore,
    space_id: *const c_char,
    name: *const c_char,
) {
    if ptr.is_null() || space_id.is_null() || name.is_null() {
        return;
    }
    let core = &mut *ptr;
    let id_str = match CStr::from_ptr(space_id).to_str() {
        Ok(s) => s,
        Err(_) => return,
    };
    let name_str = match CStr::from_ptr(name).to_str() {
        Ok(s) => s,
        Err(_) => return,
    };
    let id = maho_types::identifiers::SpaceId::new(id_str);
    core.rename_space(&id, name_str);
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `space_id` and `color_json` must be valid null-terminated C strings.
#[no_mangle]
pub unsafe extern "C" fn maho_core_recolor_space(
    ptr: *mut MahoCore,
    space_id: *const c_char,
    color_json: *const c_char,
) {
    if ptr.is_null() || space_id.is_null() || color_json.is_null() {
        return;
    }
    let core = &mut *ptr;
    let id_str = match CStr::from_ptr(space_id).to_str() {
        Ok(s) => s,
        Err(_) => return,
    };
    let color: maho_types::space::SpaceColor = match CStr::from_ptr(color_json).to_str() {
        Ok(s) => match serde_json::from_str(s) {
            Ok(c) => c,
            Err(_) => return,
        },
        Err(_) => return,
    };
    let id = maho_types::identifiers::SpaceId::new(id_str);
    core.recolor_space(&id, color);
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `space_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_reorder_space(
    ptr: *mut MahoCore,
    space_id: *const c_char,
    from: usize,
    to: usize,
) {
    if ptr.is_null() || space_id.is_null() {
        return;
    }
    let core = &mut *ptr;
    let id_str = match CStr::from_ptr(space_id).to_str() {
        Ok(s) => s,
        Err(_) => return,
    };
    let id = maho_types::identifiers::SpaceId::new(id_str);
    core.reorder_space(&id, from, to);
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `space_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_activate_space(ptr: *mut MahoCore, space_id: *const c_char) {
    if ptr.is_null() || space_id.is_null() {
        return;
    }
    let core = &mut *ptr;
    let id_str = match CStr::from_ptr(space_id).to_str() {
        Ok(s) => s,
        Err(_) => return,
    };
    let id = maho_types::identifiers::SpaceId::new(id_str);
    core.activate_space(&id);
}

// === Profile Management ===

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `name` must be a valid null-terminated C string.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_create_profile(
    ptr: *mut MahoCore,
    name: *const c_char,
) -> *mut c_char {
    if ptr.is_null() || name.is_null() {
        return ptr::null_mut();
    }
    let core = &mut *ptr;
    let name_str = match CStr::from_ptr(name).to_str() {
        Ok(s) => s.to_string(),
        Err(_) => return ptr::null_mut(),
    };
    let profile = core.create_profile(name_str);
    to_json_cstring(&profile)
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `profile_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_delete_profile(
    ptr: *mut MahoCore,
    profile_id: *const c_char,
) -> bool {
    if ptr.is_null() || profile_id.is_null() {
        return false;
    }
    let core = &mut *ptr;
    let id_str = match CStr::from_ptr(profile_id).to_str() {
        Ok(s) => s,
        Err(_) => return false,
    };
    let id = maho_types::identifiers::ProfileId::new(id_str);
    core.delete_profile(&id)
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_list_profiles(ptr: *mut MahoCore) -> *mut c_char {
    if ptr.is_null() {
        return ptr::null_mut();
    }
    let core = &*ptr;
    let profiles = core.list_profiles();
    to_json_cstring(&profiles)
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `profile_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_switch_profile(
    ptr: *mut MahoCore,
    profile_id: *const c_char,
) -> bool {
    if ptr.is_null() || profile_id.is_null() {
        return false;
    }
    let core = &mut *ptr;
    let id_str = match CStr::from_ptr(profile_id).to_str() {
        Ok(s) => s,
        Err(_) => return false,
    };
    let id = maho_types::identifiers::ProfileId::new(id_str);
    core.switch_profile(&id)
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `profile_id` must be a valid null-terminated C string.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_profile_data_store_id(
    ptr: *mut MahoCore,
    profile_id: *const c_char,
) -> *mut c_char {
    if ptr.is_null() || profile_id.is_null() {
        return ptr::null_mut();
    }
    let core = &*ptr;
    let id_str = match CStr::from_ptr(profile_id).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    let id = maho_types::identifiers::ProfileId::new(id_str);
    match core.get_profile_data_store_id(&id) {
        Some(data_store_id) => to_json_cstring(&data_store_id),
        None => ptr::null_mut(),
    }
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_active_profile_id(ptr: *mut MahoCore) -> *mut c_char {
    if ptr.is_null() {
        return ptr::null_mut();
    }
    let core = &*ptr;
    match core.get_active_profile_id() {
        Some(id) => to_json_cstring(id),
        None => ptr::null_mut(),
    }
}

// === Sync FFI Functions ===

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `server_url` and `sync_key` must be valid null-terminated C strings.
#[no_mangle]
pub unsafe extern "C" fn maho_core_start_sync(
    ptr: *mut MahoCore,
    server_url: *const c_char,
    sync_key: *const c_char,
) {
    if ptr.is_null() || server_url.is_null() || sync_key.is_null() {
        return;
    }
    let core = &mut *ptr;
    let url = match CStr::from_ptr(server_url).to_str() {
        Ok(s) => s,
        Err(_) => return,
    };
    let key = match CStr::from_ptr(sync_key).to_str() {
        Ok(s) => s,
        Err(_) => return,
    };
    core.start_sync(url, key);
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_stop_sync(ptr: *mut MahoCore) {
    if ptr.is_null() {
        return;
    }
    let core = &mut *ptr;
    core.stop_sync();
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_sync_status(ptr: *mut MahoCore) -> *mut c_char {
    if ptr.is_null() {
        return ptr::null_mut();
    }
    let core = &*ptr;
    let json = core.get_sync_status();
    CString::new(json)
        .map(CString::into_raw)
        .unwrap_or(ptr::null_mut())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_connected_devices(ptr: *mut MahoCore) -> *mut c_char {
    if ptr.is_null() {
        return ptr::null_mut();
    }
    let core = &*ptr;
    let json = core.get_connected_devices();
    CString::new(json)
        .map(CString::into_raw)
        .unwrap_or(ptr::null_mut())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `url`, `title`, and `target_device_id` must be valid null-terminated C strings.
#[no_mangle]
pub unsafe extern "C" fn maho_core_send_tab(
    ptr: *mut MahoCore,
    url: *const c_char,
    title: *const c_char,
    target_device_id: *const c_char,
) {
    if ptr.is_null() || url.is_null() || title.is_null() || target_device_id.is_null() {
        return;
    }
    let core = &mut *ptr;
    let url_str = match CStr::from_ptr(url).to_str() {
        Ok(s) => s,
        Err(_) => return,
    };
    let title_str = match CStr::from_ptr(title).to_str() {
        Ok(s) => s,
        Err(_) => return,
    };
    let device_str = match CStr::from_ptr(target_device_id).to_str() {
        Ok(s) => s,
        Err(_) => return,
    };
    core.send_tab_to_device(url_str, title_str, device_str);
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_generate_sync_key(ptr: *mut MahoCore) -> *mut c_char {
    if ptr.is_null() {
        return ptr::null_mut();
    }
    let core = &*ptr;
    let json = core.generate_sync_key();
    CString::new(json)
        .map(CString::into_raw)
        .unwrap_or(ptr::null_mut())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `recovery_phrase` must be a valid null-terminated C string.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_join_sync(
    ptr: *mut MahoCore,
    recovery_phrase: *const c_char,
) -> *mut c_char {
    if ptr.is_null() || recovery_phrase.is_null() {
        return ptr::null_mut();
    }
    let core = &mut *ptr;
    let phrase = match CStr::from_ptr(recovery_phrase).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    let json = core.join_sync(phrase);
    CString::new(json)
        .map(CString::into_raw)
        .unwrap_or(ptr::null_mut())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `device_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_remove_sync_device(
    ptr: *mut MahoCore,
    device_id: *const c_char,
) {
    if ptr.is_null() || device_id.is_null() {
        return;
    }
    let core = &mut *ptr;
    let id_str = match CStr::from_ptr(device_id).to_str() {
        Ok(s) => s,
        Err(_) => return,
    };
    core.remove_sync_device(id_str);
}

/// Resets a pinned tab back to its original pinned URL.
///
/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `tab_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_reset_pinned_tab(
    ptr: *mut MahoCore,
    tab_id: *const c_char,
) -> bool {
    if ptr.is_null() || tab_id.is_null() {
        return false;
    }
    let core = &mut *ptr;
    let tab_id_str = match CStr::from_ptr(tab_id).to_str() {
        Ok(s) => s,
        Err(_) => return false,
    };
    let tid = maho_types::identifiers::TabId::new(tab_id_str);
    core.reset_pinned_tab(&tid)
}

/// Checks whether a navigation in a pinned tab should open in a peek tab instead.
/// Returns a JSON CoreUpdate (OpenPeekTab) if the URL is cross-domain, or null if navigation is allowed.
///
/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `tab_id` must be a valid null-terminated C string.
/// `url` must be a valid null-terminated C string.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_check_pinned_navigation(
    ptr: *mut MahoCore,
    tab_id: *const c_char,
    url: *const c_char,
) -> *mut c_char {
    if ptr.is_null() || tab_id.is_null() || url.is_null() {
        return ptr::null_mut();
    }
    let core = &*ptr;
    let tab_id_str = match CStr::from_ptr(tab_id).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    let url_str = match CStr::from_ptr(url).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    let tid = maho_types::identifiers::TabId::new(tab_id_str);
    let new_url = maho_types::common::Url::new(url_str);
    match core.check_pinned_navigation(&tid, &new_url) {
        Some(update) => to_json_cstring(&update),
        None => ptr::null_mut(),
    }
}

/// Apply incoming entity batch from server.
/// `entities_json` is a JSON array of SyncEntity objects.
/// Returns JSON array of actually-applied entities.
///
/// # Safety
/// `ptr` must be valid. `entities_json` must be a valid C string.
/// Caller must free returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_apply_remote_entities(
    ptr: *mut MahoCore,
    entities_json: *const c_char,
) -> *mut c_char {
    if ptr.is_null() || entities_json.is_null() {
        return ptr::null_mut();
    }
    let core = &mut *ptr;
    let json_str = match CStr::from_ptr(entities_json).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    let entities: Vec<maho_core::sync_models::SyncEntity> = match serde_json::from_str(json_str) {
        Ok(e) => e,
        Err(_) => return ptr::null_mut(),
    };
    let applied = core.apply_sync_remote_entities(entities);
    to_json_cstring(&applied)
}

/// Get pending outgoing messages as JSON array.
/// Shell calls this periodically to send via WebSocket.
///
/// # Safety
/// `ptr` must be valid.
/// Caller must free returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_drain_outgoing(ptr: *mut MahoCore) -> *mut c_char {
    if ptr.is_null() {
        return ptr::null_mut();
    }
    let core = &mut *ptr;
    let messages = core.drain_sync_outgoing();
    to_json_cstring(&messages)
}

/// Send a tab to another device.
/// `device_id` is the target device ID.
/// `tab_json` is a JSON object with "url" and "title" fields.
///
/// # Safety
/// `ptr`, `device_id`, `tab_json` must be valid C strings.
/// Caller must free returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_send_tab_to_device(
    ptr: *mut MahoCore,
    device_id: *const c_char,
    tab_json: *const c_char,
) -> *mut c_char {
    if ptr.is_null() || device_id.is_null() || tab_json.is_null() {
        return ptr::null_mut();
    }
    let core = &mut *ptr;
    let device_str = match CStr::from_ptr(device_id).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    let tab_str = match CStr::from_ptr(tab_json).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    let tab: serde_json::Value = match serde_json::from_str(tab_str) {
        Ok(v) => v,
        Err(_) => return ptr::null_mut(),
    };
    let url = match tab["url"].as_str() {
        Some(s) => s,
        None => return ptr::null_mut(),
    };
    let title = match tab["title"].as_str() {
        Some(s) => s,
        None => return ptr::null_mut(),
    };
    core.send_tab_to_device(url, title, device_str);
    to_json_cstring(&serde_json::json!({"success": true}))
}

// === Easels ===

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_easel_view_models(ptr: *mut MahoCore) -> *mut c_char {
    if ptr.is_null() {
        return ptr::null_mut();
    }
    let core = &*ptr;
    to_json_cstring(&core.get_easel_view_models())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `easel_id` must be a valid null-terminated C string.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_easel(
    ptr: *mut MahoCore,
    easel_id: *const c_char,
) -> *mut c_char {
    if ptr.is_null() || easel_id.is_null() {
        return ptr::null_mut();
    }
    let core = &*ptr;
    let id_str = match CStr::from_ptr(easel_id).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    match core.get_easel(id_str) {
        Some(easel) => to_json_cstring(easel),
        None => ptr::null_mut(),
    }
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `name` must be a valid null-terminated C string.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_create_easel(
    ptr: *mut MahoCore,
    name: *const c_char,
) -> *mut c_char {
    if ptr.is_null() || name.is_null() {
        return ptr::null_mut();
    }
    let core = &mut *ptr;
    let name_str = match CStr::from_ptr(name).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    let easel = core.create_easel_persisted(name_str);
    to_json_cstring(&easel)
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `easel_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_delete_easel(
    ptr: *mut MahoCore,
    easel_id: *const c_char,
) -> bool {
    if ptr.is_null() || easel_id.is_null() {
        return false;
    }
    let core = &mut *ptr;
    let id_str = match CStr::from_ptr(easel_id).to_str() {
        Ok(s) => s,
        Err(_) => return false,
    };
    core.delete_easel_persisted(id_str)
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `easel_id` and `json` must be valid null-terminated C strings.
#[no_mangle]
pub unsafe extern "C" fn maho_core_update_easel(
    ptr: *mut MahoCore,
    easel_id: *const c_char,
    json: *const c_char,
) -> bool {
    if ptr.is_null() || easel_id.is_null() || json.is_null() {
        return false;
    }
    let core = &mut *ptr;
    let id_str = match CStr::from_ptr(easel_id).to_str() {
        Ok(s) => s,
        Err(_) => return false,
    };
    let json_str = match CStr::from_ptr(json).to_str() {
        Ok(s) => s,
        Err(_) => return false,
    };
    core.update_easel_persisted(id_str, json_str)
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_autofill_addresses(ptr: *mut MahoCore) -> *mut c_char {
    if ptr.is_null() {
        return ptr::null_mut();
    }
    let core = &*ptr;
    to_json_cstring(core.get_autofill_addresses())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_autofill_payments(ptr: *mut MahoCore) -> *mut c_char {
    if ptr.is_null() {
        return ptr::null_mut();
    }
    let core = &*ptr;
    to_json_cstring(core.get_autofill_payments())
}

/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// String parameters must be valid null-terminated UTF-8.
#[no_mangle]
pub unsafe extern "C" fn maho_core_create_shared_collection(
    ptr: *mut MahoCore,
    space_id: *const c_char,
    name: *const c_char,
) -> *mut c_char {
    if ptr.is_null() || space_id.is_null() || name.is_null() {
        return ptr::null_mut();
    }
    let core = &mut *ptr;
    let space_str = match CStr::from_ptr(space_id).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    let name_str = match CStr::from_ptr(name).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    match core.create_shared_collection(space_str, name_str) {
        Some(json) => CString::new(json)
            .map(CString::into_raw)
            .unwrap_or(ptr::null_mut()),
        None => ptr::null_mut(),
    }
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// String parameters must be valid null-terminated UTF-8.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_share_link(
    ptr: *mut MahoCore,
    collection_id: *const c_char,
) -> *mut c_char {
    if ptr.is_null() || collection_id.is_null() {
        return ptr::null_mut();
    }
    let core = &*ptr;
    let id_str = match CStr::from_ptr(collection_id).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    match core.get_share_link(id_str) {
        Some(link) => CString::new(link)
            .map(CString::into_raw)
            .unwrap_or(ptr::null_mut()),
        None => ptr::null_mut(),
    }
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// String parameters must be valid null-terminated UTF-8.
#[no_mangle]
pub unsafe extern "C" fn maho_core_update_share_permissions(
    ptr: *mut MahoCore,
    collection_id: *const c_char,
    permission: *const c_char,
) {
    if ptr.is_null() || collection_id.is_null() || permission.is_null() {
        return;
    }
    let core = &mut *ptr;
    let id_str = match CStr::from_ptr(collection_id).to_str() {
        Ok(s) => s,
        Err(_) => return,
    };
    let perm_str = match CStr::from_ptr(permission).to_str() {
        Ok(s) => s,
        Err(_) => return,
    };
    let _ = core.update_share_permissions(id_str, perm_str);
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// String parameters must be valid null-terminated UTF-8.
#[no_mangle]
pub unsafe extern "C" fn maho_core_revoke_share(ptr: *mut MahoCore, collection_id: *const c_char) {
    if ptr.is_null() || collection_id.is_null() {
        return;
    }
    let core = &mut *ptr;
    let id_str = match CStr::from_ptr(collection_id).to_str() {
        Ok(s) => s,
        Err(_) => return,
    };
    core.revoke_share(id_str);
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_shared_collections(ptr: *mut MahoCore) -> *mut c_char {
    if ptr.is_null() {
        return ptr::null_mut();
    }
    let core = &*ptr;
    match core.get_shared_collections() {
        Some(json) => CString::new(json)
            .map(CString::into_raw)
            .unwrap_or(ptr::null_mut()),
        None => ptr::null_mut(),
    }
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// String parameters must be valid null-terminated UTF-8.
#[no_mangle]
pub unsafe extern "C" fn maho_core_join_shared_collection(
    ptr: *mut MahoCore,
    share_link: *const c_char,
) -> bool {
    if ptr.is_null() || share_link.is_null() {
        return false;
    }
    let core = &mut *ptr;
    let link_str = match CStr::from_ptr(share_link).to_str() {
        Ok(s) => s,
        Err(_) => return false,
    };
    core.join_shared_collection(link_str)
}
