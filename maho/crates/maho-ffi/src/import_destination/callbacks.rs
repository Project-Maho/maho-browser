//! External delivery callback helpers (cookies, autofill, favicons) for `MahoCoreDestination`.

use std::ffi::{c_void, CString};

use maho_core::maho_core::MahoCore;
use maho_types::events::shell_event::ShellEvent;
use maho_types::identifiers::TabId;

use crate::import::{
    MahoImportAutofillCallback, MahoImportCookieCallback, MahoImportFaviconCallback,
};

pub(super) fn add_cookie(
    cookie_cb: Option<MahoImportCookieCallback>,
    user_data: *mut c_void,
    host: &str,
    name: &str,
    value: &str,
    path: &str,
    expires: i64,
    is_secure: bool,
    is_httponly: bool,
    same_site: i32,
) -> bool {
    let Some(cb) = cookie_cb else {
        return false;
    };
    let (Some(hc), Some(nc), Some(vc), Some(pc)) = (
        CString::new(host).ok(),
        CString::new(name).ok(),
        CString::new(value).ok(),
        CString::new(path).ok(),
    ) else {
        return false;
    };
    // SAFETY: `cookie_cb` is an extern "C" fn pointer with process-lifetime validity.
    // The FFI contract on `maho_import_orchestrator_start` guarantees `user_data` remains
    // valid for the duration of the session until the terminal progress callback fires.
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
            user_data,
        );
    }
    true
}

pub(super) fn add_autofill(
    autofill_cb: Option<MahoImportAutofillCallback>,
    user_data: *mut c_void,
    field_name: &str,
    value: &str,
    times_used: i32,
    first_used: i64,
    last_used: i64,
) -> bool {
    let Some(cb) = autofill_cb else {
        return false;
    };
    let (Some(fnc), Some(vc)) = (CString::new(field_name).ok(), CString::new(value).ok()) else {
        return false;
    };
    // SAFETY: `autofill_cb` is an extern "C" fn pointer with process-lifetime validity.
    // `user_data` is guaranteed valid by the session caller until session completion.
    unsafe {
        cb(
            fnc.as_ptr(),
            vc.as_ptr(),
            times_used,
            first_used,
            last_used,
            user_data,
        );
    }
    true
}

pub(super) fn find_matching_favicon_tabs(core: &MahoCore, url: &str) -> Vec<TabId> {
    let url_host = url_host_lower(url);
    core.tab_manager()
        .get_all_tabs()
        .iter()
        .filter(|t| {
            if t.url.0 == url {
                return true;
            }
            if let Some(host) = url_host.as_deref() {
                url_host_lower(&t.url.0).as_deref() == Some(host)
            } else {
                false
            }
        })
        .map(|t| t.id.clone())
        .collect()
}

pub(super) fn apply_favicon_updates(core: &mut MahoCore, matching: &[TabId], png_bytes: &[u8]) {
    if matching.is_empty() {
        return;
    }
    let image = maho_types::common::ImageData {
        data: png_bytes.to_vec(),
        width: 16,
        height: 16,
        format: maho_types::common::ImageFormat::Png,
    };
    for tab_id in matching {
        core.handle_event(ShellEvent::TabFaviconUpdated {
            tab_id: tab_id.clone(),
            favicon: Some(image.clone()),
        });
    }
}

pub(super) fn dispatch_favicon_callback(
    favicon_cb: Option<MahoImportFaviconCallback>,
    user_data: *mut c_void,
    url: &str,
    png_bytes: &[u8],
) -> bool {
    let Some(cb) = favicon_cb else {
        return false;
    };
    let Ok(url_c) = CString::new(url) else {
        return false;
    };
    // SAFETY: `favicon_cb` is an extern "C" fn pointer with process-lifetime validity.
    // `user_data` is guaranteed valid by the session caller until session completion.
    unsafe {
        cb(
            url_c.as_ptr(),
            png_bytes.as_ptr(),
            png_bytes.len(),
            user_data,
        );
    }
    true
}

fn url_host_lower(url: &str) -> Option<String> {
    let after_scheme = url.split_once("://").map(|(_, rest)| rest).unwrap_or(url);
    let host_with_port = after_scheme.split('/').next()?;
    let host = host_with_port.split(':').next()?;
    if host.is_empty() {
        return None;
    }
    Some(host.to_ascii_lowercase())
}
