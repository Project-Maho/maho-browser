use std::ffi::{c_char, CStr, CString};
use std::ptr;

use maho_core::maho_core::MahoCore;

use crate::common::{to_c_string, to_json_cstring};
use crate::ffi_safe;
use crate::import_gate;

/// Set or replace a hot/warm memory block
#[no_mangle]
pub unsafe extern "C" fn maho_core_set_memory_block(
    core: *mut MahoCore,
    label: *const c_char,
    content: *const c_char,
    tier: *const c_char,
) -> bool {
    ffi_safe!(
        {
            if core.is_null() || label.is_null() || content.is_null() || tier.is_null() {
                return false;
            }
            let core = unsafe { &mut *core };
            let label = match CStr::from_ptr(label).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            let content = match CStr::from_ptr(content).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            let tier = match CStr::from_ptr(tier).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            core.set_memory_block(label, content, tier).is_ok()
        },
        false
    )
}

/// Get a memory block content by label. Caller owns returned string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_memory_block(
    core: *mut MahoCore,
    label: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if core.is_null() || label.is_null() {
                return ptr::null_mut();
            }
            let core = unsafe { &*core };
            let label = match CStr::from_ptr(label).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            match core.get_memory_block(label) {
                Some(content) => to_c_string(&content),
                None => ptr::null_mut(),
            }
        },
        ptr::null_mut()
    )
}

/// Get L1 memory briefing text (hot briefing block). Caller owns returned string (free via maho_core_free_string).
#[no_mangle]
pub unsafe extern "C" fn maho_memory_get_l1_briefing(core: *mut MahoCore) -> *mut c_char {
    ffi_safe!(
        {
            if core.is_null() {
                return ptr::null_mut();
            }
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = unsafe { &*core };
            match core.get_l1_briefing() {
                Some(content) => to_c_string(&content),
                None => ptr::null_mut(),
            }
        },
        ptr::null_mut()
    )
}

/// Delete a memory fact by id.
#[no_mangle]
pub unsafe extern "C" fn maho_core_delete_memory(
    core: *mut MahoCore,
    id: *const c_char,
) -> bool {
    ffi_safe!(
        {
            if core.is_null() || id.is_null() {
                return false;
            }
            let core = unsafe { &mut *core };
            let id = match CStr::from_ptr(id).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            core.delete_memory(id)
        },
        false
    )
}

/// Clear all memory facts.
#[no_mangle]
pub unsafe extern "C" fn maho_core_clear_all_memories(core: *mut MahoCore) -> bool {
    ffi_safe!(
        {
            if core.is_null() {
                return false;
            }
            let core = unsafe { &mut *core };
            core.clear_all_memories()
        },
        false
    )
}

/// Search memory using hybrid dense + sparse retrieval. Returns JSON array of HybridMemoryResult.
#[no_mangle]
pub unsafe extern "C" fn maho_core_search_memory_hybrid(
    core: *mut MahoCore,
    query: *const c_char,
    top_k: usize,
) -> *mut c_char {
    ffi_safe!(
        {
            if core.is_null() || query.is_null() {
                return ptr::null_mut();
            }
            let core = unsafe { &*core };
            let query = match CStr::from_ptr(query).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let results = core.search_memory_hybrid(query, top_k);
            to_json_cstring(&results)
        },
        ptr::null_mut()
    )
}

/// Run a step of background memory maintenance jobs.
#[no_mangle]
pub unsafe extern "C" fn maho_core_step_memory_maintenance(core: *mut MahoCore) -> i32 {
    ffi_safe!(
        {
            if core.is_null() {
                return -1;
            }
            let core = unsafe { &mut *core };
            match core.step_memory_maintenance() {
                Ok(n) => n as i32,
                Err(_) => -1,
            }
        },
        -1
    )
}
