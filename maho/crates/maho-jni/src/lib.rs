use jni::objects::{JByteArray, JClass, JString};
use jni::sys::{jboolean, jint, jlong, jstring};
use jni::JNIEnv;
use maho_core::maho_core::MahoCore;
use std::ffi::{c_char, c_void, CStr};
use std::sync::Mutex;

static CONTENT_BLOCKER_QUERY_GATE: Mutex<()> = Mutex::new(());

#[derive(serde::Serialize)]
#[serde(rename_all = "camelCase")]
struct ContentBlockerMutationError {
    code: String,
    message: String,
}

#[derive(serde::Serialize)]
#[serde(rename_all = "camelCase")]
struct ContentBlockerMutationResult {
    success: bool,
    #[serde(skip_serializing_if = "Option::is_none")]
    error: Option<ContentBlockerMutationError>,
    state: Option<maho_types::content_blocking::ContentBlockerStateDto>,
    compile_required: bool,
}

fn content_blocker_result(
    core: Option<&MahoCore>,
    success: bool,
    error: Option<ContentBlockerMutationError>,
    compile_required: bool,
) -> ContentBlockerMutationResult {
    ContentBlockerMutationResult {
        success,
        error,
        state: core.map(MahoCore::get_content_blocker_state_dto),
        compile_required,
    }
}

fn content_blocker_error(code: &str, message: impl Into<String>) -> ContentBlockerMutationError {
    ContentBlockerMutationError {
        code: code.to_string(),
        message: message.into(),
    }
}

fn apply_filter_list_update_result(
    core: &mut MahoCore,
    update_json: &str,
) -> ContentBlockerMutationResult {
    let response = match serde_json::from_str(update_json) {
        Ok(response) => response,
        Err(error) => {
            return content_blocker_result(
                Some(core),
                false,
                Some(content_blocker_error("invalid_json", error.to_string())),
                false,
            );
        }
    };
    match core.apply_filter_list_update_response(response) {
        Ok(compile_required) => content_blocker_result(Some(core), true, None, compile_required),
        Err(error) => content_blocker_result(
            Some(core),
            false,
            Some(content_blocker_error("update_rejected", error)),
            false,
        ),
    }
}

fn to_json_jstring<T: serde::Serialize>(env: &mut JNIEnv, value: &T) -> jstring {
    match serde_json::to_string(value) {
        Ok(json) => match env.new_string(&json) {
            Ok(s) => s.into_raw(),
            Err(_) => std::ptr::null_mut(),
        },
        Err(_) => std::ptr::null_mut(),
    }
}

fn get_rust_string(env: &mut JNIEnv, input: &JString) -> Option<String> {
    env.get_string(input).ok().map(|s| s.into())
}

/// # Safety
/// `ptr` must have been created by `nativeCreate` and not yet freed.
unsafe fn get_core(ptr: jlong) -> Option<&'static mut MahoCore> {
    let ptr = ptr as *mut MahoCore;
    if ptr.is_null() {
        None
    } else {
        Some(&mut *ptr)
    }
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeCreate(
    _env: JNIEnv,
    _class: JClass,
) -> jlong {
    let core = Box::new(MahoCore::new());
    Box::into_raw(core) as jlong
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeCreateWithStorage<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    storage_path: JString<'local>,
) -> jlong {
    let path_str = match get_rust_string(&mut env, &storage_path) {
        Some(s) => s,
        None => {
            let core = Box::new(MahoCore::new());
            return Box::into_raw(core) as jlong;
        }
    };

    let base_path = std::path::Path::new(&path_str);

    let lmdb_path = base_path.join("state");
    if std::fs::create_dir_all(&lmdb_path).is_err() {
        let core = Box::new(MahoCore::new());
        return Box::into_raw(core) as jlong;
    }

    let sqlite_path = base_path.join("maho.db");
    let sqlite_path_str = match sqlite_path.to_str() {
        Some(s) => s,
        None => {
            let core = Box::new(MahoCore::new());
            return Box::into_raw(core) as jlong;
        }
    };

    let core = MahoCore::new()
        .with_storage(sqlite_path_str)
        .with_lmdb_storage(&lmdb_path);
    Box::into_raw(Box::new(core)) as jlong
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeSetSqlcipherKey<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    key: JString<'local>,
) -> jboolean {
    let key_str = match get_rust_string(&mut env, &key) {
        Some(s) => s,
        None => return jni::sys::JNI_FALSE,
    };
    match maho_storage::sqlite::set_sqlcipher_key(&key_str) {
        Ok(_) => jni::sys::JNI_TRUE,
        Err(_) => jni::sys::JNI_FALSE,
    }
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeDestroy(
    _env: JNIEnv,
    _class: JClass,
    ptr: jlong,
) {
    if ptr != 0 {
        unsafe { drop(Box::from_raw(ptr as *mut MahoCore)) };
    }
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeHandleEvent<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    event_json: JString<'local>,
) -> jstring {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return std::ptr::null_mut(),
    };
    let json_str = match get_rust_string(&mut env, &event_json) {
        Some(s) => s,
        None => return std::ptr::null_mut(),
    };
    let event: maho_types::events::shell_event::ShellEvent = match serde_json::from_str(&json_str) {
        Ok(e) => e,
        Err(_) => return std::ptr::null_mut(),
    };
    let updates = core.handle_event(event);
    to_json_jstring(&mut env, &updates)
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeGetTabViewModels<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
) -> jstring {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return std::ptr::null_mut(),
    };
    let models = core.get_tab_view_models();
    to_json_jstring(&mut env, &models)
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeGetSpaceViewModels<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
) -> jstring {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return std::ptr::null_mut(),
    };
    let models = core.get_space_view_models();
    to_json_jstring(&mut env, &models)
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeGetSettings<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
) -> jstring {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return std::ptr::null_mut(),
    };
    let settings = core.get_settings();
    to_json_jstring(&mut env, settings)
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeUpdateSettings<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    settings_json: JString<'local>,
) {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return,
    };
    let json_str = match get_rust_string(&mut env, &settings_json) {
        Some(s) => s,
        None => return,
    };
    let update: maho_types::settings::SettingsUpdate = match serde_json::from_str(&json_str) {
        Ok(u) => u,
        Err(_) => return,
    };
    core.update_settings(update);
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeGetBookmarks<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
) -> jstring {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return std::ptr::null_mut(),
    };
    let bookmarks = core.get_all_bookmark_entries();
    to_json_jstring(&mut env, &bookmarks)
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeGetPinnedTabs<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    space_id_json: JString<'local>,
) -> jstring {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return std::ptr::null_mut(),
    };
    let json_str = match get_rust_string(&mut env, &space_id_json) {
        Some(s) => s,
        None => return std::ptr::null_mut(),
    };
    let space_id: maho_types::identifiers::SpaceId = match serde_json::from_str(&json_str) {
        Ok(id) => id,
        Err(_) => return std::ptr::null_mut(),
    };
    let models = core.get_pinned_tabs(&space_id);
    to_json_jstring(&mut env, &models)
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeGetTodayTabs<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    space_id_json: JString<'local>,
) -> jstring {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return std::ptr::null_mut(),
    };
    let json_str = match get_rust_string(&mut env, &space_id_json) {
        Some(s) => s,
        None => return std::ptr::null_mut(),
    };
    let space_id: maho_types::identifiers::SpaceId = match serde_json::from_str(&json_str) {
        Ok(id) => id,
        Err(_) => return std::ptr::null_mut(),
    };
    let models = core.get_today_tabs(&space_id);
    to_json_jstring(&mut env, &models)
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeGetArchivedTabs<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    space_id_json: JString<'local>,
) -> jstring {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return std::ptr::null_mut(),
    };
    let json_str = match get_rust_string(&mut env, &space_id_json) {
        Some(s) => s,
        None => return std::ptr::null_mut(),
    };
    let space_id: maho_types::identifiers::SpaceId = match serde_json::from_str(&json_str) {
        Ok(id) => id,
        Err(_) => return std::ptr::null_mut(),
    };
    let models = core.get_archived_tabs(&space_id);
    to_json_jstring(&mut env, &models)
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeGetFavoriteTabs<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    space_id_json: JString<'local>,
) -> jstring {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return std::ptr::null_mut(),
    };
    let json_str = match get_rust_string(&mut env, &space_id_json) {
        Some(s) => s,
        None => return std::ptr::null_mut(),
    };
    let space_id: maho_types::identifiers::SpaceId = match serde_json::from_str(&json_str) {
        Ok(id) => id,
        Err(_) => return std::ptr::null_mut(),
    };
    let models = core.get_favorite_tabs(&space_id);
    to_json_jstring(&mut env, &models)
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeFavoriteTab<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    tab_id_json: JString<'local>,
) {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return,
    };
    let json_str = match get_rust_string(&mut env, &tab_id_json) {
        Some(s) => s,
        None => return,
    };
    let tab_id: maho_types::identifiers::TabId = match serde_json::from_str(&json_str) {
        Ok(id) => id,
        Err(_) => return,
    };
    core.favorite_tab(&tab_id);
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeTransitionTabRole<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    tab_id_json: JString<'local>,
    role_json: JString<'local>,
) {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return,
    };
    let tab_id_str = match get_rust_string(&mut env, &tab_id_json) {
        Some(s) => s,
        None => return,
    };
    let role_str = match get_rust_string(&mut env, &role_json) {
        Some(s) => s,
        None => return,
    };
    let tab_id: maho_types::identifiers::TabId = match serde_json::from_str(&tab_id_str) {
        Ok(id) => id,
        Err(_) => return,
    };
    let role: maho_types::tab::TabRole = match serde_json::from_str(&role_str) {
        Ok(r) => r,
        Err(_) => return,
    };
    core.transition_tab_role(&tab_id, role);
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeSchedulePreviewCapture<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    tab_id_json: JString<'local>,
) -> jboolean {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return jni::sys::JNI_FALSE,
    };
    let json_str = match get_rust_string(&mut env, &tab_id_json) {
        Some(s) => s,
        None => return jni::sys::JNI_FALSE,
    };
    let tab_id: maho_types::identifiers::TabId = match serde_json::from_str(&json_str) {
        Ok(id) => id,
        Err(_) => return jni::sys::JNI_FALSE,
    };
    if core.schedule_preview_capture(tab_id) {
        jni::sys::JNI_TRUE
    } else {
        jni::sys::JNI_FALSE
    }
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeTakePendingPreviewCaptures<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
) -> jstring {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return std::ptr::null_mut(),
    };
    let tab_ids = core.take_pending_preview_captures();
    to_json_jstring(&mut env, &tab_ids)
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeHasTabPreview<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    tab_id_json: JString<'local>,
) -> jboolean {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return jni::sys::JNI_FALSE,
    };
    let json_str = match get_rust_string(&mut env, &tab_id_json) {
        Some(s) => s,
        None => return jni::sys::JNI_FALSE,
    };
    let tab_id: maho_types::identifiers::TabId = match serde_json::from_str(&json_str) {
        Ok(id) => id,
        Err(_) => return jni::sys::JNI_FALSE,
    };
    if core.has_tab_preview(&tab_id) {
        jni::sys::JNI_TRUE
    } else {
        jni::sys::JNI_FALSE
    }
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeGetActiveSpaceId<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
) -> jstring {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return std::ptr::null_mut(),
    };
    let space_id = core.get_active_space_id();
    to_json_jstring(&mut env, &space_id)
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeTick<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
) -> jstring {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return std::ptr::null_mut(),
    };
    let updates = core.tick();
    to_json_jstring(&mut env, &updates)
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeSaveState<'local>(
    _env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
) -> jboolean {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return jni::sys::JNI_FALSE,
    };
    match core.save_state() {
        Ok(_) => jni::sys::JNI_TRUE,
        Err(_) => jni::sys::JNI_FALSE,
    }
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeLoadState<'local>(
    _env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
) -> jboolean {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return jni::sys::JNI_FALSE,
    };
    match core.load_state() {
        Ok(_) => jni::sys::JNI_TRUE,
        Err(_) => jni::sys::JNI_FALSE,
    }
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeSearchHistory<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    query: JString<'local>,
    limit: jlong,
) -> jstring {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return std::ptr::null_mut(),
    };
    let query_str = match get_rust_string(&mut env, &query) {
        Some(s) => s,
        None => return std::ptr::null_mut(),
    };
    let results = core.search_history(&query_str, limit as usize);
    to_json_jstring(&mut env, &results)
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeGetSearchEngines<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
) -> jstring {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return std::ptr::null_mut(),
    };
    let engines = core.get_search_engines();
    to_json_jstring(&mut env, &engines)
}

// === Content Blocker (6 functions) ===

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeCompileContentRules<'local>(
    env: JNIEnv<'local>,
    _class: JClass<'local>,
    _ptr: jlong,
) -> jstring {
    match env.new_string("[]") {
        Ok(s) => s.into_raw(),
        Err(_) => std::ptr::null_mut(),
    }
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeGetContentRules<'local>(
    env: JNIEnv<'local>,
    _class: JClass<'local>,
    _ptr: jlong,
) -> jstring {
    match env.new_string("[]") {
        Ok(s) => s.into_raw(),
        Err(_) => std::ptr::null_mut(),
    }
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeGetFilterLists<'local>(
    env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
) -> jstring {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return std::ptr::null_mut(),
    };
    let lists = core.get_filter_lists_json();
    match env.new_string(&lists) {
        Ok(s) => s.into_raw(),
        Err(_) => std::ptr::null_mut(),
    }
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeAddFilterList<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    id: JString<'local>,
    name: JString<'local>,
    url: JString<'local>,
) {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return,
    };
    let id_str = match get_rust_string(&mut env, &id) {
        Some(s) => s,
        None => return,
    };
    let name_str = match get_rust_string(&mut env, &name) {
        Some(s) => s,
        None => return,
    };
    let url_str = match get_rust_string(&mut env, &url) {
        Some(s) => s,
        None => return,
    };
    let event = maho_types::events::shell_event::ShellEvent::AddFilterList {
        id: id_str,
        name: name_str,
        url: url_str,
    };
    core.handle_event(event);
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeRebuildContentRules<'local>(
    _env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
) {
    let _ = ptr;
    // Legacy ABI only. Production callers must request candidate-aware worker
    // compilation through the typed update result instead of rebuilding inline.
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeGetContentRuleCount<'local>(
    _env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
) -> jlong {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return 0,
    };
    core.get_content_rule_count() as jlong
}

// === Reader Mode (3 functions) ===

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeToggleReaderMode<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    tab_id_json: JString<'local>,
) -> jboolean {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return jni::sys::JNI_FALSE,
    };
    let json_str = match get_rust_string(&mut env, &tab_id_json) {
        Some(s) => s,
        None => return jni::sys::JNI_FALSE,
    };
    let tab_id: maho_types::identifiers::TabId = match serde_json::from_str(&json_str) {
        Ok(id) => id,
        Err(_) => return jni::sys::JNI_FALSE,
    };
    if core.toggle_reader_mode(&tab_id.to_string()) {
        jni::sys::JNI_TRUE
    } else {
        jni::sys::JNI_FALSE
    }
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeIsReaderMode<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    tab_id_json: JString<'local>,
) -> jboolean {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return jni::sys::JNI_FALSE,
    };
    let json_str = match get_rust_string(&mut env, &tab_id_json) {
        Some(s) => s,
        None => return jni::sys::JNI_FALSE,
    };
    let tab_id: maho_types::identifiers::TabId = match serde_json::from_str(&json_str) {
        Ok(id) => id,
        Err(_) => return jni::sys::JNI_FALSE,
    };
    if core.is_reader_mode(&tab_id.to_string()) {
        jni::sys::JNI_TRUE
    } else {
        jni::sys::JNI_FALSE
    }
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeGetReaderSettings<'local>(
    env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
) -> jstring {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return std::ptr::null_mut(),
    };
    let settings = core.get_reader_settings_json();
    match env.new_string(settings) {
        Ok(s) => s.into_raw(),
        Err(_) => std::ptr::null_mut(),
    }
}

// === Find in Page (4 functions) ===

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeStartFind<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    tab_id_json: JString<'local>,
    query: JString<'local>,
) -> jstring {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return std::ptr::null_mut(),
    };
    let tab_id_str = match get_rust_string(&mut env, &tab_id_json) {
        Some(s) => s,
        None => return std::ptr::null_mut(),
    };
    let tab_id: maho_types::identifiers::TabId = match serde_json::from_str(&tab_id_str) {
        Ok(id) => id,
        Err(_) => return std::ptr::null_mut(),
    };
    let query_str = match get_rust_string(&mut env, &query) {
        Some(s) => s,
        None => return std::ptr::null_mut(),
    };
    let session = core.start_find(tab_id, query_str, false, false);
    to_json_jstring(&mut env, &session)
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeFindNext<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
) -> jstring {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return std::ptr::null_mut(),
    };
    let result = core.find_next();
    to_json_jstring(&mut env, &result)
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeFindPrevious<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
) -> jstring {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return std::ptr::null_mut(),
    };
    let result = core.find_previous();
    to_json_jstring(&mut env, &result)
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeDismissFind<'local>(
    _env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
) {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return,
    };
    core.dismiss_find();
}

// === Downloads (6 functions) ===

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeGetDownloadViewModels<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
) -> jstring {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return std::ptr::null_mut(),
    };
    let models = core.get_download_view_models();
    to_json_jstring(&mut env, &models)
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeStartDownload<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    url: JString<'local>,
    filename: JString<'local>,
    content_type: JString<'local>,
) -> jstring {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return std::ptr::null_mut(),
    };
    let url_str = match get_rust_string(&mut env, &url) {
        Some(s) => s,
        None => return std::ptr::null_mut(),
    };
    let filename_str = match get_rust_string(&mut env, &filename) {
        Some(s) => s,
        None => return std::ptr::null_mut(),
    };
    let mime_type = match get_rust_string(&mut env, &content_type) {
        Some(s) => {
            if s.is_empty() {
                None
            } else {
                Some(s)
            }
        }
        None => None,
    };
    let download_id =
        core.start_download(&filename_str, &url_str, 0, None, mime_type.as_deref(), None);
    match env.new_string(&download_id) {
        Ok(s) => s.into_raw(),
        Err(_) => std::ptr::null_mut(),
    }
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeUpdateDownloadProgress<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    download_id: JString<'local>,
    bytes_received: jlong,
    _total_bytes: jlong,
) {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return,
    };
    let download_id_str = match get_rust_string(&mut env, &download_id) {
        Some(s) => s,
        None => return,
    };
    core.update_download_progress(&download_id_str, bytes_received as u64);
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativePauseDownload<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    download_id: JString<'local>,
) {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return,
    };
    let download_id_str = match get_rust_string(&mut env, &download_id) {
        Some(s) => s,
        None => return,
    };
    core.pause_download(&download_id_str);
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeResumeDownload<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    download_id: JString<'local>,
) {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return,
    };
    let download_id_str = match get_rust_string(&mut env, &download_id) {
        Some(s) => s,
        None => return,
    };
    core.resume_download(&download_id_str);
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeCancelDownload<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    download_id: JString<'local>,
) {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return,
    };
    let download_id_str = match get_rust_string(&mut env, &download_id) {
        Some(s) => s,
        None => return,
    };
    core.cancel_download(&download_id_str);
}

// === Zoom (2 functions) ===

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeGetZoom<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    site: JString<'local>,
) -> jni::sys::jdouble {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return 1.0,
    };
    let site_str = match get_rust_string(&mut env, &site) {
        Some(s) => s,
        None => return 1.0,
    };
    core.get_zoom_for_site(&site_str)
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeSetZoom<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    site: JString<'local>,
    zoom: jni::sys::jdouble,
) {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return,
    };
    let site_str = match get_rust_string(&mut env, &site) {
        Some(s) => s,
        None => return,
    };
    core.set_zoom_for_site(site_str, zoom);
}

// === Phase 3: Bookmarks, History, Settings (8 functions) ===

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeAddBookmark<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    url: JString<'local>,
    title: JString<'local>,
    folder_id: JString<'local>,
) {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return,
    };
    let url_str = match get_rust_string(&mut env, &url) {
        Some(s) => s,
        None => return,
    };
    let title_str = match get_rust_string(&mut env, &title) {
        Some(s) => s,
        None => return,
    };
    let folder_id_opt = get_rust_string(&mut env, &folder_id).filter(|s| !s.is_empty());
    core.add_bookmark(&url_str, &title_str, folder_id_opt.as_deref());
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeRemoveBookmark<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    bookmark_id: JString<'local>,
) {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return,
    };
    let id_str = match get_rust_string(&mut env, &bookmark_id) {
        Some(s) => s,
        None => return,
    };
    core.remove_bookmark(&id_str);
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeSearchBookmarks<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    query: JString<'local>,
) -> jstring {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return std::ptr::null_mut(),
    };
    let query_str = match get_rust_string(&mut env, &query) {
        Some(s) => s,
        None => return std::ptr::null_mut(),
    };
    let results = core.search_bookmarks(&query_str);
    to_json_jstring(&mut env, &results)
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeCreateBookmarkFolder<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    name: JString<'local>,
    parent_id: JString<'local>,
) -> jstring {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return std::ptr::null_mut(),
    };
    let name_str = match get_rust_string(&mut env, &name) {
        Some(s) => s,
        None => return std::ptr::null_mut(),
    };
    let parent_id_opt = get_rust_string(&mut env, &parent_id).filter(|s| !s.is_empty());
    let folder = core.create_bookmark_folder(name_str, parent_id_opt);
    to_json_jstring(&mut env, &folder)
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeDeleteBookmarkFolder<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    folder_id: JString<'local>,
) -> jstring {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return std::ptr::null_mut(),
    };
    let id_str = match get_rust_string(&mut env, &folder_id) {
        Some(s) => s,
        None => return std::ptr::null_mut(),
    };
    let result = core.delete_bookmark_folder(&id_str);
    to_json_jstring(&mut env, &result)
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeGetHistoryGroupedByDate<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
) -> jstring {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return std::ptr::null_mut(),
    };
    let results = core.get_history_grouped_by_date();
    to_json_jstring(&mut env, &results)
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeDeleteHistoryEntry<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    entry_id: JString<'local>,
) {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return,
    };
    let id_str = match get_rust_string(&mut env, &entry_id) {
        Some(s) => s,
        None => return,
    };
    core.delete_history_entry(&id_str);
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeClearHistory<'local>(
    _env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
) {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return,
    };
    core.clear_history();
}

// === Sync JNI Functions ===

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeStartSync<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    server_url: JString<'local>,
    sync_key: JString<'local>,
) {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return,
    };
    let url = match get_rust_string(&mut env, &server_url) {
        Some(s) => s,
        None => return,
    };
    let key = match get_rust_string(&mut env, &sync_key) {
        Some(s) => s,
        None => return,
    };
    core.start_sync(&url, &key);
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeStopSync<'local>(
    _env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
) {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return,
    };
    core.stop_sync();
}

#[no_mangle]
#[allow(unused_mut)]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeGetSyncStatus<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
) -> jstring {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return std::ptr::null_mut(),
    };
    let json = core.get_sync_status();
    match env.new_string(&json) {
        Ok(s) => s.into_raw(),
        Err(_) => std::ptr::null_mut(),
    }
}

#[no_mangle]
#[allow(unused_mut)]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeGetSyncRoomId<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
) -> jstring {
    let core = match unsafe { get_core(ptr) } {
        Some(core) => core,
        None => return std::ptr::null_mut(),
    };
    match core.get_sync_room_id() {
        Some(room_id) => env
            .new_string(room_id)
            .map_or(std::ptr::null_mut(), |value| value.into_raw()),
        None => std::ptr::null_mut(),
    }
}

#[no_mangle]
#[allow(unused_mut)]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeLeaseSyncOutgoingEnvelopes<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
) -> jstring {
    let core = match unsafe { get_core(ptr) } {
        Some(core) => core,
        None => return std::ptr::null_mut(),
    };
    match core.drain_sync_outgoing_envelopes() {
        Ok(envelopes) => to_json_jstring(&mut env, &envelopes),
        Err(_) => std::ptr::null_mut(),
    }
}

#[no_mangle]
#[allow(unused_mut)]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeAcknowledgeSyncEnvelope<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    ack_json: JString<'local>,
) -> jboolean {
    let core = match unsafe { get_core(ptr) } {
        Some(core) => core,
        None => return 0,
    };
    let ack_json = match get_rust_string(&mut env, &ack_json) {
        Some(value) => value,
        None => return 0,
    };
    core.accept_sync_ack(&ack_json)
        .is_ok_and(|acknowledged| acknowledged) as jboolean
}

#[no_mangle]
#[allow(unused_mut)]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeGetSyncReceiveCursor<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    room_id: JString<'local>,
) -> jlong {
    let core = match unsafe { get_core(ptr) } {
        Some(core) => core,
        None => return 0,
    };
    let room_id = match get_rust_string(&mut env, &room_id) {
        Some(value) => value,
        None => return 0,
    };
    core.get_sync_receive_cursor(&room_id).unwrap_or(0) as jlong
}

#[no_mangle]
#[allow(unused_mut)]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeApplySyncEnvelopeAndAdvanceCursor<
    'local,
>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    room_id: JString<'local>,
    envelope_json: JString<'local>,
) -> jboolean {
    let core = match unsafe { get_core(ptr) } {
        Some(core) => core,
        None => return 0,
    };
    let room_id = match get_rust_string(&mut env, &room_id) {
        Some(value) => value,
        None => return 0,
    };
    let envelope_json = match get_rust_string(&mut env, &envelope_json) {
        Some(value) => value,
        None => return 0,
    };
    core.apply_sync_envelope_and_advance_cursor(&room_id, &envelope_json)
        .is_ok() as jboolean
}

#[no_mangle]
#[allow(unused_mut)]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeReportSyncTransportState<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    report_json: JString<'local>,
) {
    let Some(core) = (unsafe { get_core(ptr) }) else {
        return;
    };
    let Some(report_json) = get_rust_string(&mut env, &report_json) else {
        return;
    };
    let _ = core.report_sync_transport_state(&report_json);
}

#[no_mangle]
#[allow(unused_mut)]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeGetConnectedDevices<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
) -> jstring {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return std::ptr::null_mut(),
    };
    let json = core.get_connected_devices();
    match env.new_string(&json) {
        Ok(s) => s.into_raw(),
        Err(_) => std::ptr::null_mut(),
    }
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeSendTab<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    url: JString<'local>,
    title: JString<'local>,
    target_device_id: JString<'local>,
) {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return,
    };
    let url_str = match get_rust_string(&mut env, &url) {
        Some(s) => s,
        None => return,
    };
    let title_str = match get_rust_string(&mut env, &title) {
        Some(s) => s,
        None => return,
    };
    let device_str = match get_rust_string(&mut env, &target_device_id) {
        Some(s) => s,
        None => return,
    };
    core.send_tab_to_device(&url_str, &title_str, &device_str);
}

#[no_mangle]
#[allow(unused_mut)]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeGenerateSyncKey<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
) -> jstring {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return std::ptr::null_mut(),
    };
    let json = core.generate_sync_key();
    match env.new_string(&json) {
        Ok(s) => s.into_raw(),
        Err(_) => std::ptr::null_mut(),
    }
}

#[no_mangle]
#[allow(unused_mut)]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeGenerateSyncBootstrap<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
) -> jstring {
    let core = match unsafe { get_core(ptr) } {
        Some(core) => core,
        None => return std::ptr::null_mut(),
    };
    env.new_string(core.generate_sync_bootstrap())
        .map_or(std::ptr::null_mut(), |value| value.into_raw())
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeJoinSync<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    recovery_phrase: JString<'local>,
) -> jstring {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return std::ptr::null_mut(),
    };
    let phrase = match get_rust_string(&mut env, &recovery_phrase) {
        Some(s) => s,
        None => return std::ptr::null_mut(),
    };
    let json = core.join_sync("wss://relay.mahobrowser.com", &phrase);
    match env.new_string(&json) {
        Ok(s) => s.into_raw(),
        Err(_) => std::ptr::null_mut(),
    }
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeConfigureSyncBootstrap<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    server_url: JString<'local>,
    bootstrap_seed: JString<'local>,
) -> jstring {
    let core = match unsafe { get_core(ptr) } {
        Some(core) => core,
        None => return std::ptr::null_mut(),
    };
    let server_url = match get_rust_string(&mut env, &server_url) {
        Some(value) => value,
        None => return std::ptr::null_mut(),
    };
    let bootstrap_seed = match get_rust_string(&mut env, &bootstrap_seed) {
        Some(value) => value,
        None => return std::ptr::null_mut(),
    };
    let result = match core.configure_sync_encryption_from_bootstrap(&server_url, &bootstrap_seed) {
        Ok(room_id) => serde_json::json!({ "success": true, "roomId": room_id }),
        Err(error) => serde_json::json!({ "success": false, "error": error }),
    };
    env.new_string(result.to_string())
        .map_or(std::ptr::null_mut(), |value| value.into_raw())
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeRemoveSyncDevice<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    device_id: JString<'local>,
) {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return,
    };
    let id_str = match get_rust_string(&mut env, &device_id) {
        Some(s) => s,
        None => return,
    };
    core.remove_sync_device(&id_str);
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeGetActiveProfileId<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
) -> jstring {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return std::ptr::null_mut(),
    };
    match core.get_active_profile_id() {
        Some(id) => to_json_jstring(&mut env, id),
        None => std::ptr::null_mut(),
    }
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeListProfiles<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
) -> jstring {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return std::ptr::null_mut(),
    };
    let profiles = core.list_profiles();
    to_json_jstring(&mut env, &profiles)
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeCreateProfile<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    name: JString<'local>,
) -> jstring {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return std::ptr::null_mut(),
    };
    let name_str = match get_rust_string(&mut env, &name) {
        Some(s) => s,
        None => return std::ptr::null_mut(),
    };
    let profile = core.create_profile_persisted(name_str);
    to_json_jstring(&mut env, &profile)
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeDeleteProfile<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    profile_id: JString<'local>,
) -> jboolean {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return 0,
    };
    let id_str = match get_rust_string(&mut env, &profile_id) {
        Some(s) => s,
        None => return 0,
    };
    let id = maho_types::identifiers::ProfileId::new(id_str);
    if core.delete_profile(&id) {
        1
    } else {
        0
    }
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeGetProfileDataStoreId<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    profile_id: JString<'local>,
) -> jstring {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return std::ptr::null_mut(),
    };
    let id_str = match get_rust_string(&mut env, &profile_id) {
        Some(s) => s,
        None => return std::ptr::null_mut(),
    };
    let id = maho_types::identifiers::ProfileId::new(id_str);
    match core.get_profile_data_store_id(&id) {
        Some(ds_id) => to_json_jstring(&mut env, &ds_id),
        None => std::ptr::null_mut(),
    }
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeResetPinnedTab<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    tab_id: JString<'local>,
) -> jstring {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return std::ptr::null_mut(),
    };
    let tid = match get_rust_string(&mut env, &tab_id) {
        Some(s) => s,
        None => return std::ptr::null_mut(),
    };
    let event = maho_types::events::shell_event::ShellEvent::ResetPinnedTab {
        tab_id: maho_types::identifiers::TabId::new(tid),
    };
    let updates = core.handle_event(event);
    to_json_jstring(&mut env, &updates)
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeCheckPinnedNavigation<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    tab_id: JString<'local>,
    url: JString<'local>,
) -> jstring {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return std::ptr::null_mut(),
    };
    let tid = match get_rust_string(&mut env, &tab_id) {
        Some(s) => s,
        None => return std::ptr::null_mut(),
    };
    let url_str = match get_rust_string(&mut env, &url) {
        Some(s) => s,
        None => return std::ptr::null_mut(),
    };
    let tab = maho_types::identifiers::TabId::new(tid);
    let nav_url = maho_types::common::Url::new(url_str);
    match core.check_pinned_navigation(&tab, &nav_url) {
        Some(update) => to_json_jstring(&mut env, &update),
        None => std::ptr::null_mut(),
    }
}

// === Tabs (5 functions) ===

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeGetFolderViewModels<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    space_id_json: JString<'local>,
) -> jstring {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return std::ptr::null_mut(),
    };
    let json_str = match get_rust_string(&mut env, &space_id_json) {
        Some(s) => s,
        None => return std::ptr::null_mut(),
    };
    let space_id: maho_types::identifiers::SpaceId = match serde_json::from_str(&json_str) {
        Ok(id) => id,
        Err(_) => return std::ptr::null_mut(),
    };
    let models = core.get_folder_view_models(&space_id);
    to_json_jstring(&mut env, &models)
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeSetTabParent<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    tab_id_json: JString<'local>,
    new_parent_id_json: JString<'local>,
) {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return,
    };
    let tab_id_str = match get_rust_string(&mut env, &tab_id_json) {
        Some(s) => s,
        None => return,
    };
    let tab_id = maho_types::identifiers::TabId::new(tab_id_str);
    let new_parent_id = match get_rust_string(&mut env, &new_parent_id_json) {
        Some(s) if !s.is_empty() => Some(maho_types::identifiers::TabId::new(s)),
        _ => None,
    };
    let event = maho_types::events::shell_event::ShellEvent::SetTabParent {
        tab_id,
        new_parent_id,
    };
    core.handle_event(event);
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeUpdateTabPreview<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    tab_id_json: JString<'local>,
    data: JByteArray<'local>,
) {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return,
    };
    let tab_id_str = match get_rust_string(&mut env, &tab_id_json) {
        Some(s) => s,
        None => return,
    };
    let bytes = match env.convert_byte_array(&data) {
        Ok(b) => b,
        Err(_) => return,
    };
    let image_data = maho_types::common::ImageData {
        data: bytes,
        width: 0,
        height: 0,
        format: maho_types::common::ImageFormat::Png,
    };
    let tid = maho_types::identifiers::TabId::new(tab_id_str);
    core.update_tab_preview(&tid, image_data);
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeGetTabPreview<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    tab_id_json: JString<'local>,
) -> jstring {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return std::ptr::null_mut(),
    };
    let json_str = match get_rust_string(&mut env, &tab_id_json) {
        Some(s) => s,
        None => return std::ptr::null_mut(),
    };
    let tid: maho_types::identifiers::TabId = match serde_json::from_str(&json_str) {
        Ok(id) => id,
        Err(_) => return std::ptr::null_mut(),
    };
    match core.get_tab_preview(&tid) {
        Some(preview) => {
            let result = serde_json::json!({
                "tab_id": preview.tab_id.to_string(),
                "has_thumbnail": preview.thumbnail.is_some(),
                "captured_at": preview.captured_at.to_string(),
            });
            to_json_jstring(&mut env, &result)
        }
        None => std::ptr::null_mut(),
    }
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeFreePreviewData(
    _env: JNIEnv,
    _class: JClass,
) {
    // No-op in JNI — JVM handles memory
}

// === Spaces (6 functions) ===

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeCreateSpace<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    name: JString<'local>,
    color_json: JString<'local>,
    profile_id: JString<'local>,
) -> jstring {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return std::ptr::null_mut(),
    };
    let name_str = match get_rust_string(&mut env, &name) {
        Some(s) => s,
        None => return std::ptr::null_mut(),
    };
    let color_str = match get_rust_string(&mut env, &color_json) {
        Some(s) => s,
        None => return std::ptr::null_mut(),
    };
    let color: maho_types::space::SpaceColor = match serde_json::from_str(&color_str) {
        Ok(c) => c,
        Err(_) => return std::ptr::null_mut(),
    };
    let pid_str = match get_rust_string(&mut env, &profile_id) {
        Some(s) => s,
        None => return std::ptr::null_mut(),
    };
    let pid = maho_types::identifiers::ProfileId::new(pid_str);
    let space = core.create_space(&name_str, color, pid);
    to_json_jstring(&mut env, &space)
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeDeleteSpace<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    space_id_json: JString<'local>,
) {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return,
    };
    let json_str = match get_rust_string(&mut env, &space_id_json) {
        Some(s) => s,
        None => return,
    };
    let id = maho_types::identifiers::SpaceId::new(json_str);
    core.delete_space(&id);
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeRenameSpace<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    space_id_json: JString<'local>,
    name: JString<'local>,
) {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return,
    };
    let id_str = match get_rust_string(&mut env, &space_id_json) {
        Some(s) => s,
        None => return,
    };
    let name_str = match get_rust_string(&mut env, &name) {
        Some(s) => s,
        None => return,
    };
    let id = maho_types::identifiers::SpaceId::new(id_str);
    core.rename_space(&id, &name_str);
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeRecolorSpace<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    space_id_json: JString<'local>,
    color_json: JString<'local>,
) {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return,
    };
    let id_str = match get_rust_string(&mut env, &space_id_json) {
        Some(s) => s,
        None => return,
    };
    let color_str = match get_rust_string(&mut env, &color_json) {
        Some(s) => s,
        None => return,
    };
    let color: maho_types::space::SpaceColor = match serde_json::from_str(&color_str) {
        Ok(c) => c,
        Err(_) => return,
    };
    let id = maho_types::identifiers::SpaceId::new(id_str);
    core.recolor_space(&id, color);
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeReorderSpace<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    space_id_json: JString<'local>,
    from: jlong,
    to: jlong,
) {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return,
    };
    let id_str = match get_rust_string(&mut env, &space_id_json) {
        Some(s) => s,
        None => return,
    };
    let id = maho_types::identifiers::SpaceId::new(id_str);
    core.reorder_space(&id, from as usize, to as usize);
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeActivateSpace<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    space_id_json: JString<'local>,
) {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return,
    };
    let id_str = match get_rust_string(&mut env, &space_id_json) {
        Some(s) => s,
        None => return,
    };
    let id = maho_types::identifiers::SpaceId::new(id_str);
    core.activate_space(&id);
}

// === Profiles (1 function) ===

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeSwitchProfile<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    profile_id: JString<'local>,
) -> jboolean {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return jni::sys::JNI_FALSE,
    };
    let id_str = match get_rust_string(&mut env, &profile_id) {
        Some(s) => s,
        None => return jni::sys::JNI_FALSE,
    };
    let id = maho_types::identifiers::ProfileId::new(id_str);
    if core.switch_profile(&id) {
        jni::sys::JNI_TRUE
    } else {
        jni::sys::JNI_FALSE
    }
}

// === Bookmarks (1 function) ===

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeMoveBookmark<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    bookmark_id: JString<'local>,
    folder_id: JString<'local>,
) {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return,
    };
    let bookmark_id_str = match get_rust_string(&mut env, &bookmark_id) {
        Some(s) => s,
        None => return,
    };
    let folder_id_opt = get_rust_string(&mut env, &folder_id).filter(|s| !s.is_empty());
    core.move_bookmark(&bookmark_id_str, folder_id_opt.as_deref());
}

// === History (2 functions) ===

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeAddHistoryEntry<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    url: JString<'local>,
    title: JString<'local>,
) {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return,
    };
    let url_str = match get_rust_string(&mut env, &url) {
        Some(s) => s,
        None => return,
    };
    let title_str = match get_rust_string(&mut env, &title) {
        Some(s) => s,
        None => return,
    };
    core.add_history_entry(&url_str, &title_str);
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeSearchHistoryPaginated<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    query: JString<'local>,
    limit: jlong,
    offset: jlong,
) -> jstring {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return std::ptr::null_mut(),
    };
    let query_str = match get_rust_string(&mut env, &query) {
        Some(s) => s,
        None => return std::ptr::null_mut(),
    };
    let results = core.search_history_with_offset(&query_str, limit as usize, offset as usize);
    to_json_jstring(&mut env, &results)
}

// === Settings (16 functions) ===

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeGetSiteSearchEntries<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
) -> jstring {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return std::ptr::null_mut(),
    };
    let entries = &core.get_settings().general.site_search_entries;
    to_json_jstring(&mut env, entries)
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeSetSiteSearchEntries<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    json: JString<'local>,
) {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return,
    };
    let json_str = match get_rust_string(&mut env, &json) {
        Some(s) => s,
        None => return,
    };
    let entries: Vec<maho_types::settings::SiteSearchEntry> = match serde_json::from_str(&json_str)
    {
        Ok(e) => e,
        Err(_) => return,
    };
    let update = maho_types::settings::SettingsUpdate {
        general: Some(maho_types::settings::GeneralSettingsUpdate {
            site_search_entries: Some(entries),
            ..Default::default()
        }),
        ..Default::default()
    };
    core.update_settings(update);
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeGetNotifications<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
) -> jstring {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return std::ptr::null_mut(),
    };
    let notifications = core.get_notifications();
    to_json_jstring(&mut env, &notifications)
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeDismissNotification<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    notification_id: JString<'local>,
) {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return,
    };
    let id_str = match get_rust_string(&mut env, &notification_id) {
        Some(s) => s,
        None => return,
    };
    core.dismiss_notification(&id_str);
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeDismissAllNotifications<'local>(
    _env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
) {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return,
    };
    core.dismiss_all_notifications();
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeSetNotificationFilter<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    origin: JString<'local>,
    allowed: jboolean,
) {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return,
    };
    let origin_str = match get_rust_string(&mut env, &origin) {
        Some(s) => s,
        None => return,
    };
    core.set_notification_filter(origin_str, allowed != 0);
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeGetToolbarItems<'local>(
    env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
) -> jstring {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return std::ptr::null_mut(),
    };
    let json = core.get_toolbar_items_json();
    match env.new_string(&json) {
        Ok(s) => s.into_raw(),
        Err(_) => std::ptr::null_mut(),
    }
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeSetToolbarItems<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    items_json: JString<'local>,
) -> jstring {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return std::ptr::null_mut(),
    };
    let json_str = match get_rust_string(&mut env, &items_json) {
        Some(s) => s,
        None => return std::ptr::null_mut(),
    };
    let result = match core.set_toolbar_items_json(&json_str) {
        Ok(()) => r#"{"success":true}"#.to_string(),
        Err(e) => format!(r#"{{"error":"{}"}}"#, e),
    };
    match env.new_string(&result) {
        Ok(s) => s.into_raw(),
        Err(_) => std::ptr::null_mut(),
    }
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeSetAppIcon<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    path: JString<'local>,
) {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return,
    };
    let path_opt = get_rust_string(&mut env, &path).filter(|s| !s.is_empty());
    core.set_app_icon(path_opt);
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeGetDefaultToolbarItems<'local>(
    env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
) -> jstring {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return std::ptr::null_mut(),
    };
    let json = core.get_default_toolbar_items_json();
    match env.new_string(&json) {
        Ok(s) => s.into_raw(),
        Err(_) => std::ptr::null_mut(),
    }
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeSaveFormData<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    tab_id: JString<'local>,
    form_json: JString<'local>,
) {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return,
    };
    let tab_id_str = match get_rust_string(&mut env, &tab_id) {
        Some(s) => s,
        None => return,
    };
    let form_json_str = match get_rust_string(&mut env, &form_json) {
        Some(s) => s,
        None => return,
    };
    core.save_form_data(&tab_id_str, form_json_str);
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeSetCrashSaveInterval<'local>(
    _env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    seconds: jlong,
) {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return,
    };
    core.set_crash_save_interval(seconds as u64);
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeSetDensity<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    density: JString<'local>,
) {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return,
    };
    let density_str = match get_rust_string(&mut env, &density) {
        Some(s) => s,
        None => return,
    };
    core.set_density(&density_str);
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeSetCustomChromeCss<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    css: JString<'local>,
) {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return,
    };
    let css_opt = get_rust_string(&mut env, &css).filter(|s| !s.is_empty());
    core.set_custom_chrome_css(css_opt);
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeSetWindowTransparency<'local>(
    _env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    enabled: jboolean,
) {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return,
    };
    core.set_window_transparency(enabled != 0);
}

// === Downloads (1 function) ===

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeRemoveDownload<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    download_id: JString<'local>,
) {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return,
    };
    let download_id_str = match get_rust_string(&mut env, &download_id) {
        Some(s) => s,
        None => return,
    };
    core.remove_download(&download_id_str);
}

// === Content Blocker (5 functions) ===

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeUpdateFilterListContent<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    id: JString<'local>,
    content: JString<'local>,
) {
    let _ = (&mut env, ptr, id, content);
    // Legacy ABI only. A raw replacement cannot make candidate promotion or
    // compile-required status observable, so it is intentionally constrained.
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeApplyFilterListUpdateResult<
    'local,
>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    update_json: JString<'local>,
) -> jstring {
    let core = match unsafe { get_core(ptr) } {
        Some(core) => core,
        None => return std::ptr::null_mut(),
    };
    let update_json = match get_rust_string(&mut env, &update_json) {
        Some(value) => value,
        None => return std::ptr::null_mut(),
    };
    to_json_jstring(
        &mut env,
        &apply_filter_list_update_result(core, &update_json),
    )
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeGetAtcRules<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
) -> jstring {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return std::ptr::null_mut(),
    };
    let rules = core.get_atc_rules();
    to_json_jstring(&mut env, &rules)
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeAddAtcRule<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    rule_json: JString<'local>,
) -> jstring {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return std::ptr::null_mut(),
    };
    let json_str = match get_rust_string(&mut env, &rule_json) {
        Some(s) => s,
        None => return std::ptr::null_mut(),
    };
    let rule: maho_core::atc_manager::ATCRule = match serde_json::from_str(&json_str) {
        Ok(r) => r,
        Err(_) => return std::ptr::null_mut(),
    };
    let id = core.add_atc_rule(rule);
    match env.new_string(&id) {
        Ok(s) => s.into_raw(),
        Err(_) => std::ptr::null_mut(),
    }
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeRemoveAtcRule<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    rule_id: JString<'local>,
) -> jstring {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return std::ptr::null_mut(),
    };
    let rule_id_str = match get_rust_string(&mut env, &rule_id) {
        Some(s) => s,
        None => return std::ptr::null_mut(),
    };
    let result = core.remove_atc_rule(&rule_id_str);
    to_json_jstring(&mut env, &result)
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeToggleAtcRule<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    rule_id: JString<'local>,
    enabled: jboolean,
) {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return,
    };
    let rule_id_str = match get_rust_string(&mut env, &rule_id) {
        Some(s) => s,
        None => return,
    };
    core.toggle_atc_rule(&rule_id_str, enabled != 0);
}

// === Command Bar (5 functions) ===

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeRecordUsage<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    item_key: JString<'local>,
) {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return,
    };
    let key_str = match get_rust_string(&mut env, &item_key) {
        Some(s) => s,
        None => return,
    };
    core.record_command_bar_usage(&key_str);
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeGetTopUsed<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    limit: jlong,
) -> jstring {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return std::ptr::null_mut(),
    };
    let results = core.get_top_used_commands(limit as usize);
    to_json_jstring(&mut env, &results)
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeGetSearchableItems<'local>(
    env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
) -> jstring {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return std::ptr::null_mut(),
    };
    let result = core.get_searchable_items();
    match env.new_string(&result) {
        Ok(s) => s.into_raw(),
        Err(_) => std::ptr::null_mut(),
    }
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeGetRecentSearches<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
) -> jstring {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return std::ptr::null_mut(),
    };
    let searches = core.get_recent_searches();
    to_json_jstring(&mut env, &searches)
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeSaveSearch<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    query: JString<'local>,
) {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return,
    };
    let query_str = match get_rust_string(&mut env, &query) {
        Some(s) => s,
        None => return,
    };
    core.save_search(query_str);
}

// === Notes (8 functions) ===

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeGetNoteViewModels<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
) -> jstring {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return std::ptr::null_mut(),
    };
    let models = core.get_note_view_models();
    to_json_jstring(&mut env, &models)
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeLinkNoteToTab<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    note_id: JString<'local>,
    tab_id: JString<'local>,
) {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return,
    };
    let nid_str = match get_rust_string(&mut env, &note_id) {
        Some(s) => s,
        None => return,
    };
    let tid_str = match get_rust_string(&mut env, &tab_id) {
        Some(s) => s,
        None => return,
    };
    core.link_note_to_tab(&nid_str, &tid_str);
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeLinkNoteToUrl<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    note_id: JString<'local>,
    url: JString<'local>,
) {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return,
    };
    let nid_str = match get_rust_string(&mut env, &note_id) {
        Some(s) => s,
        None => return,
    };
    let url_str = match get_rust_string(&mut env, &url) {
        Some(s) => s,
        None => return,
    };
    core.link_note_to_url(&nid_str, &url_str);
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeUnlinkNoteFromTab<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    note_id: JString<'local>,
) {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return,
    };
    let nid_str = match get_rust_string(&mut env, &note_id) {
        Some(s) => s,
        None => return,
    };
    core.unlink_note_from_tab(&nid_str);
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeGetLinkedTabId<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    note_id: JString<'local>,
) -> jstring {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return std::ptr::null_mut(),
    };
    let nid_str = match get_rust_string(&mut env, &note_id) {
        Some(s) => s,
        None => return std::ptr::null_mut(),
    };
    match core.get_linked_tab_id(&nid_str) {
        Some(tid) => match env.new_string(&tid) {
            Ok(s) => s.into_raw(),
            Err(_) => std::ptr::null_mut(),
        },
        None => std::ptr::null_mut(),
    }
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeExportNotes<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    format: JString<'local>,
) -> jstring {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return std::ptr::null_mut(),
    };
    let fmt_str = match get_rust_string(&mut env, &format) {
        Some(s) => s,
        None => return std::ptr::null_mut(),
    };
    let result = core.export_notes(&fmt_str);
    match env.new_string(&result) {
        Ok(s) => s.into_raw(),
        Err(_) => std::ptr::null_mut(),
    }
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeExportSingleNote<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    note_id: JString<'local>,
    format: JString<'local>,
) -> jstring {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return std::ptr::null_mut(),
    };
    let nid_str = match get_rust_string(&mut env, &note_id) {
        Some(s) => s,
        None => return std::ptr::null_mut(),
    };
    let fmt_str = match get_rust_string(&mut env, &format) {
        Some(s) => s,
        None => return std::ptr::null_mut(),
    };
    match core.export_single_note(&nid_str, &fmt_str) {
        Some(result) => match env.new_string(&result) {
            Ok(s) => s.into_raw(),
            Err(_) => std::ptr::null_mut(),
        },
        None => std::ptr::null_mut(),
    }
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeSearchNotesFts<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    query: JString<'local>,
) -> jstring {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return std::ptr::null_mut(),
    };
    let query_str = match get_rust_string(&mut env, &query) {
        Some(s) => s,
        None => return std::ptr::null_mut(),
    };
    let results = core.search_notes_fts(&query_str);
    to_json_jstring(&mut env, &results)
}

// === Reading List (3 functions) ===

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeToggleReadingListRead<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    item_id: JString<'local>,
) -> jlong {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return -1,
    };
    let id_str = match get_rust_string(&mut env, &item_id) {
        Some(s) => s,
        None => return -1,
    };
    match core.toggle_reading_list_read(&id_str) {
        Some(true) => 1,
        Some(false) => 0,
        None => -1,
    }
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeGetReadingList<'local>(
    env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
) -> jstring {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return std::ptr::null_mut(),
    };
    let json = core.get_reading_list_json();
    match env.new_string(&json) {
        Ok(s) => s.into_raw(),
        Err(_) => std::ptr::null_mut(),
    }
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeGetUnreadCount<'local>(
    _env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
) -> jlong {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return 0,
    };
    core.get_unread_count() as jlong
}

// === Sync (2 functions) ===

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeApplyRemoteEntities<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    entities_json: JString<'local>,
) -> jstring {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return std::ptr::null_mut(),
    };
    let json_str = match get_rust_string(&mut env, &entities_json) {
        Some(s) => s,
        None => return std::ptr::null_mut(),
    };
    let entities: Vec<maho_core::sync_models::SyncEntity> = match serde_json::from_str(&json_str) {
        Ok(e) => e,
        Err(_) => return std::ptr::null_mut(),
    };
    let applied = core.apply_sync_remote_entities(entities);
    to_json_jstring(&mut env, &applied)
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeDrainOutgoing<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
) -> jstring {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return std::ptr::null_mut(),
    };
    let messages = core.drain_sync_outgoing();
    to_json_jstring(&mut env, &messages)
}

// === Shortcuts (5 functions) ===

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeGetShortcuts<'local>(
    env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
) -> jstring {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return std::ptr::null_mut(),
    };
    let json = core.get_all_shortcuts_json();
    match env.new_string(&json) {
        Ok(s) => s.into_raw(),
        Err(_) => std::ptr::null_mut(),
    }
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeSetShortcut<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    action: JString<'local>,
    key_combo_json: JString<'local>,
) -> jstring {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return std::ptr::null_mut(),
    };
    let action_str = match get_rust_string(&mut env, &action) {
        Some(s) => s,
        None => return std::ptr::null_mut(),
    };
    let combo_str = match get_rust_string(&mut env, &key_combo_json) {
        Some(s) => s,
        None => return std::ptr::null_mut(),
    };
    let result = match core.set_shortcut_json(&action_str, &combo_str) {
        Ok(result) => result,
        Err(conflict_json) => format!(r#"{{"error":{}}}"#, conflict_json),
    };
    match env.new_string(&result) {
        Ok(s) => s.into_raw(),
        Err(_) => std::ptr::null_mut(),
    }
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeResetShortcut<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    action: JString<'local>,
) {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return,
    };
    let action_str = match get_rust_string(&mut env, &action) {
        Some(s) => s,
        None => return,
    };
    core.reset_shortcut(&action_str);
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeResetAllShortcuts<'local>(
    _env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
) {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return,
    };
    core.reset_all_shortcuts();
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeResolveShortcut<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    key_combo_json: JString<'local>,
) -> jstring {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return std::ptr::null_mut(),
    };
    let combo_str = match get_rust_string(&mut env, &key_combo_json) {
        Some(s) => s,
        None => return std::ptr::null_mut(),
    };
    match core.resolve_shortcut_json(&combo_str) {
        Some(action) => match env.new_string(&action) {
            Ok(s) => s.into_raw(),
            Err(_) => std::ptr::null_mut(),
        },
        None => std::ptr::null_mut(),
    }
}

// === Backups (3 functions) ===

fn base64_encode(data: &[u8]) -> String {
    const CHARS: &[u8] = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    let mut result = String::with_capacity((data.len() + 2) / 3 * 4);
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
    if bytes.len() % 4 != 0 {
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
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeCreateBackup<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    config_json: JString<'local>,
) -> jstring {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return std::ptr::null_mut(),
    };
    let cfg_str = match get_rust_string(&mut env, &config_json) {
        Some(s) => s,
        None => return std::ptr::null_mut(),
    };
    match core.create_backup(&cfg_str) {
        Ok(bytes) => {
            let encoded = base64_encode(&bytes);
            match env.new_string(&encoded) {
                Ok(s) => s.into_raw(),
                Err(_) => std::ptr::null_mut(),
            }
        }
        Err(_) => std::ptr::null_mut(),
    }
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeRestoreBackup<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    backup_b64: JString<'local>,
    password: JString<'local>,
) -> jstring {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return std::ptr::null_mut(),
    };
    let b64_str = match get_rust_string(&mut env, &backup_b64) {
        Some(s) => s,
        None => return std::ptr::null_mut(),
    };
    let pw_opt = get_rust_string(&mut env, &password).filter(|s| !s.is_empty());
    let bytes = match base64_decode(&b64_str) {
        Some(b) => b,
        None => return std::ptr::null_mut(),
    };
    match core.restore_backup(&bytes, pw_opt.as_deref()) {
        Ok(json) => match env.new_string(&json) {
            Ok(s) => s.into_raw(),
            Err(_) => std::ptr::null_mut(),
        },
        Err(_) => std::ptr::null_mut(),
    }
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeGetBackupHistory<'local>(
    env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
) -> jstring {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return std::ptr::null_mut(),
    };
    let json = core.get_backup_history_json();
    match env.new_string(&json) {
        Ok(s) => s.into_raw(),
        Err(_) => std::ptr::null_mut(),
    }
}

// === Import/Export (3 functions) ===

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeDetectBrowserProfiles<'local>(
    env: JNIEnv<'local>,
    _class: JClass<'local>,
) -> jstring {
    let json = MahoCore::detect_browser_profiles_json();
    match env.new_string(&json) {
        Ok(s) => s.into_raw(),
        Err(_) => std::ptr::null_mut(),
    }
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeImportChromeBookmarks<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    profile_path: JString<'local>,
) -> jstring {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return std::ptr::null_mut(),
    };
    let path_str = match get_rust_string(&mut env, &profile_path) {
        Some(s) => s,
        None => return std::ptr::null_mut(),
    };
    match core.import_chrome_bookmarks(&path_str) {
        Ok(json) => match env.new_string(&json) {
            Ok(s) => s.into_raw(),
            Err(_) => std::ptr::null_mut(),
        },
        Err(_) => std::ptr::null_mut(),
    }
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeImportFirefoxBookmarks<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    profile_path: JString<'local>,
) -> jstring {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return std::ptr::null_mut(),
    };
    let path_str = match get_rust_string(&mut env, &profile_path) {
        Some(s) => s,
        None => return std::ptr::null_mut(),
    };
    match core.import_firefox_bookmarks(&path_str) {
        Ok(json) => match env.new_string(&json) {
            Ok(s) => s.into_raw(),
            Err(_) => std::ptr::null_mut(),
        },
        Err(_) => std::ptr::null_mut(),
    }
}

// ============================================================
// Agent JNI Bindings
// ============================================================

use async_trait::async_trait;
use jni::objects::GlobalRef;
use jni::JavaVM;
use maho_agent::{
    AgentError, AgentRuntime, AgentStorage, MutexAgentStorage, SessionRuntime,
    ZeroizedString,
};
use std::collections::{HashMap, HashSet, VecDeque};
use std::io::Read;
use std::os::fd::{AsRawFd, FromRawFd};
use std::sync::Arc;
use std::sync::OnceLock;

const MAX_AGENT_EVENT_QUEUE_DEPTH: usize = 2048;
const MAX_ANDROID_ARTIFACT_BYTES: u64 = 50 * 1024 * 1024;

#[derive(Default)]
struct AgentEventQueue {
    events: VecDeque<String>,
    artifact_ids: HashSet<String>,
}

impl AgentEventQueue {
    fn push(&mut self, event_type: &str, artifact_id: Option<&str>, payload: String) {
        if let Some(artifact_id) = artifact_id {
            if !self.artifact_ids.insert(artifact_id.to_string()) {
                return;
            }
        }
        if self.events.len() == MAX_AGENT_EVENT_QUEUE_DEPTH {
            self.events.pop_front();
        }
        self.events.push_back(payload);
        if event_type == "complete" || event_type == "error" {
            self.artifact_ids.clear();
        }
    }

    fn pop(&mut self) -> Option<String> {
        self.events.pop_front()
    }
}

pub struct MahoAgentSession {
    pub runtime: Arc<dyn AgentRuntime>,
    pub storage: Arc<MutexAgentStorage>,
    pub isolation: Arc<SessionRuntime>,
    pub session_id: String,
    event_queue: Arc<Mutex<AgentEventQueue>>,
    completion_generation: Arc<(Mutex<u64>, std::sync::Condvar)>,
    pub artifact_root: std::path::PathBuf,
}



static AGENT_SESSIONS: OnceLock<Mutex<HashMap<u64, Arc<MahoAgentSession>>>> = OnceLock::new();
static NEXT_AGENT_SESSION_HANDLE: AtomicU64 = AtomicU64::new(1);

fn agent_sessions() -> &'static Mutex<HashMap<u64, Arc<MahoAgentSession>>> {
    AGENT_SESSIONS.get_or_init(|| Mutex::new(HashMap::new()))
}

fn register_agent_session(session: MahoAgentSession) -> jlong {
    let handle = NEXT_AGENT_SESSION_HANDLE.fetch_add(1, Ordering::Relaxed);
    match agent_sessions().lock() {
        Ok(mut sessions) => {
            sessions.insert(handle, Arc::new(session));
            handle as jlong
        }
        Err(_) => 0,
    }
}

fn acquire_agent_session(handle: jlong) -> Option<Arc<MahoAgentSession>> {
    let handle = u64::try_from(handle).ok().filter(|value| *value != 0)?;
    agent_sessions().lock().ok()?.get(&handle).cloned()
}

fn remove_agent_session(handle: jlong) -> Option<Arc<MahoAgentSession>> {
    let handle = u64::try_from(handle).ok().filter(|value| *value != 0)?;
    agent_sessions().lock().ok()?.remove(&handle)
}

#[derive(Debug, PartialEq, serde::Serialize)]
#[serde(rename_all = "camelCase")]
struct AndroidArtifactInfo {
    artifact_id: String,
    session_id: String,
    display_name: String,
    mime_type: String,
    size_bytes: u64,
    created_at: f64,
}

impl From<maho_types::artifact::ArtifactInfo> for AndroidArtifactInfo {
    fn from(artifact: maho_types::artifact::ArtifactInfo) -> Self {
        Self {
            artifact_id: artifact.artifact_id,
            session_id: artifact.session_id,
            display_name: artifact.display_name,
            mime_type: artifact.mime_type,
            size_bytes: artifact.size_bytes,
            created_at: (artifact.created_at_ms as f64) / 1000.0,
        }
    }
}

static JAVA_VM: OnceLock<JavaVM> = OnceLock::new();
static MAHO_BRIDGE_CLASS: OnceLock<GlobalRef> = OnceLock::new();

fn get_jni_env() -> Option<jni::AttachGuard<'static>> {
    let vm = JAVA_VM.get()?;
    vm.attach_current_thread().ok()
}

fn call_android_permission_callback(tool_name: &str, arguments: &str) -> i32 {
    let Some(mut env_guard) = get_jni_env() else {
        return 1;
    };
    let env = &mut *env_guard;

    let Some(cls_ref) = MAHO_BRIDGE_CLASS.get() else {
        return 1;
    };
    let cls = unsafe {
        &*(cls_ref.as_obj() as *const jni::objects::JObject as *const jni::objects::JClass)
    };

    let Ok(tool_jstr) = env.new_string(tool_name) else {
        return 1;
    };
    let Ok(args_jstr) = env.new_string(arguments) else {
        return 1;
    };

    let val = env.call_static_method(
        cls,
        "requestAgentPermission",
        "(Ljava/lang/String;Ljava/lang/String;)I",
        &[
            jni::objects::JValue::Object(tool_jstr.as_ref()).into(),
            jni::objects::JValue::Object(args_jstr.as_ref()).into(),
        ],
    );

    match val {
        Ok(v) => v.i().unwrap_or(1),
        Err(_) => 1,
    }
}

fn call_android_secure_storage_callback(provider: &str) -> Option<String> {
    let Some(mut env_guard) = get_jni_env() else {
        return None;
    };
    let env = &mut *env_guard;

    let Some(cls_ref) = MAHO_BRIDGE_CLASS.get() else {
        return None;
    };
    let cls = unsafe {
        &*(cls_ref.as_obj() as *const jni::objects::JObject as *const jni::objects::JClass)
    };

    let Ok(provider_jstr) = env.new_string(provider) else {
        return None;
    };

    let val = env.call_static_method(
        cls,
        "getAgentSecureStorage",
        "(Ljava/lang/String;)Ljava/lang/String;",
        &[jni::objects::JValue::Object(provider_jstr.as_ref()).into()],
    );

    match val {
        Ok(v) => {
            let obj = v.l().ok()?;
            if obj.is_null() {
                None
            } else {
                let jstr = jni::objects::JString::from(obj);
                env.get_string(&jstr).ok().map(|s| s.into())
            }
        }
        Err(_) => None,
    }
}

#[derive(serde::Deserialize)]
struct AndroidSecureStorageResponse {
    key: String,
    #[serde(default)]
    base_url: Option<String>,
    #[serde(default)]
    model: Option<String>,
}

fn parse_android_secure_storage_response(
    response: &str,
) -> Result<maho_agent::SecureKeyBundle, AgentError> {
    let parsed = serde_json::from_str::<AndroidSecureStorageResponse>(response);
    let (key, base_url, model) = match parsed {
        Ok(bundle) => (bundle.key, bundle.base_url, bundle.model),
        Err(_) if response.trim_start().starts_with('{') => {
            // Stored secure-storage blob is present but unreadable: treat as an
            // unusable credential, not a generic execution failure.
            return Err(AgentError::Credential(
                maho_agent::CredentialError::CredentialUnusable,
            ));
        }
        Err(_) => (response.to_string(), None, None),
    };
    if key.is_empty() {
        return Err(AgentError::Credential(
            maho_agent::CredentialError::SecureStoreUnavailable,
        ));
    }

    Ok(maho_agent::SecureKeyBundle {
        key: ZeroizedString::new(key),
        base_url: base_url.filter(|value| !value.is_empty()),
        model: model.filter(|value| !value.is_empty()),
    })
}

fn jni_permission_handler(
    req: maho_agent::PermissionRequest,
) -> std::pin::Pin<Box<dyn std::future::Future<Output = maho_agent::PermissionDecision> + Send>> {
    let decision = call_android_permission_callback(&req.tool_name, &req.arguments);
    let p_decision = if decision == 0 {
        maho_agent::PermissionDecision::Allow
    } else {
        maho_agent::PermissionDecision::Deny
    };
    Box::pin(async move { p_decision })
}

fn jni_secure_storage_handler(provider: &str) -> Result<maho_agent::SecureKeyBundle, AgentError> {
    let response = call_android_secure_storage_callback(provider).ok_or_else(|| {
        AgentError::Credential(maho_agent::CredentialError::SecureStoreUnavailable)
    })?;
    parse_android_secure_storage_response(&response)
}

/// Convert a turn error into the text pushed to the agent event queue.
///
/// Typed credential errors become the same structured envelope the desktop and
/// iOS FFI layers emit (`{"version":1,"kind":"credential_error","code":...}`),
/// so the Android WebView UI can render an actionable credential card instead of
/// leaking the raw diagnostic string. All other errors keep the plain display
/// string.
fn agent_error_event_text(error: &AgentError) -> String {
    match error {
        AgentError::Credential(credential_error) => serde_json::json!({
            "version": 1,
            "kind": "credential_error",
            "code": credential_error.code(),
        })
        .to_string(),
        _ => error.to_string(),
    }
}

use std::sync::atomic::{AtomicU64, Ordering};
static EVENT_SEQ: AtomicU64 = AtomicU64::new(0);

fn enqueue_event(queue: &Arc<Mutex<AgentEventQueue>>, event_type: &str, data: serde_json::Value) {
    let seq = EVENT_SEQ.fetch_add(1, Ordering::Relaxed);
    let artifact_id = if event_type == "artifact_created" {
        data.get("artifact_id").and_then(serde_json::Value::as_str)
    } else {
        None
    };
    let payload = serde_json::json!({
        "type": event_type,
        "seq": seq,
        "data": data
    });
    if let Ok(mut lock) = queue.lock() {
        lock.push(event_type, artifact_id, payload.to_string());
    }
}

fn enqueue_agent_stream_event(
    queue: &Arc<Mutex<AgentEventQueue>>,
    event: maho_agent::AgentStreamEvent,
) {
    match event {
        // Tokens use the dedicated streaming callback to avoid duplicates.
        maho_agent::AgentStreamEvent::Token(_) => {}
        maho_agent::AgentStreamEvent::Thinking(thinking) => {
            enqueue_event(queue, "thinking", serde_json::json!(thinking));
        }
        maho_agent::AgentStreamEvent::ToolCall { id, name, args } => {
            enqueue_event(
                queue,
                "tool_call",
                serde_json::json!({ "id": id, "name": name, "args": args }),
            );
        }
        maho_agent::AgentStreamEvent::ToolResult {
            id,
            name,
            result,
            succeeded,
        } => {
            enqueue_event(
                queue,
                "tool_result",
                serde_json::json!({ "id": id, "name": name, "result": result, "success": succeeded }),
            );
        }
        maho_agent::AgentStreamEvent::ArtifactCreated { artifact } => {
            enqueue_event(
                queue,
                "artifact_created",
                serde_json::json!({
                    "artifact_id": artifact.artifact_id,
                    "session_id": artifact.session_id,
                    "display_name": artifact.display_name,
                    "mime_type": artifact.mime_type,
                    "size_bytes": artifact.size_bytes,
                    "created_at": (artifact.created_at_ms as f64) / 1000.0,
                }),
            );
        }
        maho_agent::AgentStreamEvent::Status {
            elapsed_secs,
            message,
        } => {
            enqueue_event(
                queue,
                "status",
                serde_json::json!({
                    "elapsed_secs": elapsed_secs,
                    "message": message,
                }),
            );
        }
        maho_agent::AgentStreamEvent::ProofOrReason {
            proof_locator,
            reason,
        } => {
            enqueue_event(
                queue,
                "proof_or_reason",
                serde_json::json!({
                    "proof_locator": proof_locator,
                    "reason": reason,
                }),
            );
        }
    }
}

fn prepare_artifact_root(path: &str) -> Result<std::path::PathBuf, String> {
    if path.trim().is_empty() {
        return Err("Artifact root must not be empty".to_string());
    }
    let root = std::path::PathBuf::from(path);
    if !root.is_absolute() {
        return Err("Artifact root must be absolute".to_string());
    }
    std::fs::create_dir_all(&root).map_err(|error| format!("Artifact root error: {error}"))?;
    std::fs::canonicalize(&root).map_err(|error| format!("Artifact root error: {error}"))
}

fn validated_storage_components(storage_rel_path: &str) -> Result<Vec<&std::ffi::OsStr>, String> {
    let relative = std::path::Path::new(storage_rel_path);
    if storage_rel_path.is_empty() || relative.is_absolute() {
        return Err("Artifact index contains an invalid relative path".to_string());
    }
    let components: Vec<&std::ffi::OsStr> = relative
        .components()
        .map(|component| match component {
            std::path::Component::Normal(value) => Ok(value),
            _ => Err("Artifact index contains an invalid relative path".to_string()),
        })
        .collect::<Result<_, _>>()?;
    if components.is_empty() {
        return Err("Artifact index contains an invalid relative path".to_string());
    }
    Ok(components)
}

async fn list_android_artifacts(
    runtime: &dyn AgentRuntime,
    session_id: &str,
) -> Result<Vec<AndroidArtifactInfo>, String> {
    runtime
        .list_artifacts(session_id)
        .await
        .map(|artifacts| {
            artifacts
                .into_iter()
                .map(AndroidArtifactInfo::from)
                .collect()
        })
        .map_err(|error| error.to_string())
}

fn validate_artifact_id(artifact_id: &str) -> Result<(), String> {
    if artifact_id.is_empty()
        || artifact_id.len() > 128
        || artifact_id == "."
        || artifact_id == ".."
        || artifact_id.chars().any(|character| {
            character.is_control()
                || character.is_whitespace()
                || character == '/'
                || character == '\\'
        })
        || std::path::Path::new(artifact_id).is_absolute()
    {
        return Err("Artifact id is malformed".to_string());
    }
    Ok(())
}

async fn read_android_artifact(
    runtime: &dyn AgentRuntime,
    session_id: &str,
    artifact_root: &std::path::Path,
    artifact_id: &str,
) -> Result<Vec<u8>, String> {
    validate_artifact_id(artifact_id)?;
    let storage_rel_path = runtime
        .artifact_storage_rel_path(session_id, artifact_id)
        .await
        .map_err(|error| error.to_string())?
        .ok_or_else(|| format!("Unknown artifact id: {artifact_id}"))?;
    read_nofollow_file(artifact_root, &storage_rel_path)
}

fn read_nofollow_file(
    artifact_root: &std::path::Path,
    storage_rel_path: &str,
) -> Result<Vec<u8>, String> {
    let components = validated_storage_components(storage_rel_path)?;
    let root = std::ffi::CString::new(artifact_root.as_os_str().as_encoded_bytes())
        .map_err(|_| "Artifact root contains a NUL byte".to_string())?;
    let root_fd = unsafe {
        libc::open(
            root.as_ptr(),
            libc::O_RDONLY | libc::O_CLOEXEC | libc::O_DIRECTORY | libc::O_NOFOLLOW,
        )
    };
    if root_fd < 0 {
        return Err(format!(
            "Artifact root open error: {}",
            std::io::Error::last_os_error()
        ));
    }
    let mut directory = unsafe { std::fs::File::from_raw_fd(root_fd) };
    for component in &components[..components.len() - 1] {
        let component = std::ffi::CString::new(component.as_encoded_bytes())
            .map_err(|_| "Artifact path contains a NUL byte".to_string())?;
        let fd = unsafe {
            libc::openat(
                directory.as_raw_fd(),
                component.as_ptr(),
                libc::O_RDONLY | libc::O_CLOEXEC | libc::O_DIRECTORY | libc::O_NOFOLLOW,
            )
        };
        if fd < 0 {
            return Err(format!(
                "Artifact directory open error: {}",
                std::io::Error::last_os_error()
            ));
        }
        directory = unsafe { std::fs::File::from_raw_fd(fd) };
    }
    let file_name = std::ffi::CString::new(
        components
            .last()
            .ok_or_else(|| "Artifact index contains an invalid relative path".to_string())?
            .as_encoded_bytes(),
    )
    .map_err(|_| "Artifact path contains a NUL byte".to_string())?;
    let fd = unsafe {
        libc::openat(
            directory.as_raw_fd(),
            file_name.as_ptr(),
            libc::O_RDONLY | libc::O_CLOEXEC | libc::O_NOFOLLOW,
        )
    };
    if fd < 0 {
        return Err(format!(
            "Artifact open error: {}",
            std::io::Error::last_os_error()
        ));
    }
    let mut file = unsafe { std::fs::File::from_raw_fd(fd) };
    let metadata = file
        .metadata()
        .map_err(|error| format!("Artifact metadata error: {error}"))?;
    if !metadata.is_file() || metadata.len() > MAX_ANDROID_ARTIFACT_BYTES {
        return Err("Artifact is not a shareable file or exceeds the size limit".to_string());
    }
    let mut bytes = Vec::with_capacity(metadata.len() as usize);
    file.read_to_end(&mut bytes)
        .map_err(|error| format!("Artifact read error: {error}"))?;
    Ok(bytes)
}

#[no_mangle]
pub unsafe extern "system" fn Java_dev_maho_browser_MahoBridge_nativeAgentCreateSession<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    core_ptr: jlong,
    session_id: JString<'local>,
    artifact_root: JString<'local>,
) -> jlong {
    let core = match unsafe { get_core(core_ptr) } {
        Some(c) => c,
        None => return 0,
    };
    let sid = match get_rust_string(&mut env, &session_id) {
        Some(s) => s,
        None => return 0,
    };
    let artifact_root = match get_rust_string(&mut env, &artifact_root)
        .and_then(|path| prepare_artifact_root(&path).ok())
    {
        Some(root) => root,
        None => return 0,
    };

    if let Ok(vm) = env.get_java_vm() {
        let _ = JAVA_VM.set(vm);
    }
    if let Ok(cls) = env.find_class("dev/maho/browser/MahoBridge") {
        if let Ok(global_cls) = env.new_global_ref(cls) {
            let _ = MAHO_BRIDGE_CLASS.set(global_cls);
        }
    }

    let db_path_str = match core.sqlite_db_path() {
        Some(p) => p,
        None => return 0,
    };
    let db_path = std::path::Path::new(&db_path_str);
    let workspace_root = db_path.parent().unwrap_or(db_path).to_path_buf();

    let storage = match maho_storage::sqlite::SqliteStorage::open(&db_path_str) {
        Ok(s) => s,
        Err(_) => return 0,
    };

    let agent_storage = Arc::new(MutexAgentStorage(Arc::new(Mutex::new(storage))));

    let permission_cb = Box::new(|req| jni_permission_handler(req));
    let runtime = maho_agent::omo::factory::create_agent_runtime(
        agent_storage.clone(),
        Some(permission_cb),
        workspace_root,
        false, // allow_insecure_key_storage = false on mobile/Android JNI
    );

    runtime.set_artifact_root(Some(artifact_root.clone()));

    let secure_storage_cb = Box::new(jni_secure_storage_handler);
    if runtime
        .set_secure_storage_callback(secure_storage_cb)
        .is_err()
    {
        return 0;
    }

    let isolation = match SessionRuntime::new() {
        Ok(i) => Arc::new(i),
        Err(_) => return 0,
    };

    register_agent_session(MahoAgentSession {
        runtime,
        storage: agent_storage,
        isolation,
        session_id: sid,
        event_queue: Arc::new(Mutex::new(AgentEventQueue::default())),
        completion_generation: Arc::new((Mutex::new(0), std::sync::Condvar::new())),
        artifact_root,
    })
}

#[no_mangle]
pub unsafe extern "system" fn Java_dev_maho_browser_MahoBridge_nativeAgentFreeSession(
    _env: JNIEnv,
    _class: JClass,
    session_ptr: jlong,
) {
    let _ = remove_agent_session(session_ptr);
}

#[no_mangle]
pub unsafe extern "system" fn Java_dev_maho_browser_MahoBridge_nativeAgentSendMessage<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    session_ptr: jlong,
    message: JString<'local>,
) -> jboolean {
    let session = match acquire_agent_session(session_ptr) {
        Some(session) => session,
        None => return 0,
    };
    let msg_str = match get_rust_string(&mut env, &message) {
        Some(s) => s,
        None => return 0,
    };

    let runtime = Arc::clone(&session.runtime);
    let session_id = session.session_id.clone();
    let q_clone = Arc::clone(&session.event_queue);
    let q_clone_token = Arc::clone(&session.event_queue);
    let q_clone_event = Arc::clone(&session.event_queue);
    let completion_generation = Arc::clone(&session.completion_generation);

    let on_token = Box::new(move |token: &str| {
        enqueue_event(&q_clone_token, "token", serde_json::json!(token));
    });

    let on_event = Arc::new(move |event: maho_agent::AgentStreamEvent| {
        enqueue_agent_stream_event(&q_clone_event, event);
    });

    let fut = async move {
        let user_chat_msg =
            maho_types::chat::ChatMessage::user(maho_types::chat::ChatContent::text(msg_str));
        match runtime
            .run_turn(&session_id, user_chat_msg, Some(on_token), Some(on_event))
            .await
        {
            Ok(response) => {
                let text = match &response.content {
                    maho_types::chat::ChatContent::Text(t) => t.clone(),
                    _ => String::new(),
                };
                let payload = serde_json::json!({
                    "full_text": text,
                    "tool_calls_json": "[]"
                });
                enqueue_event(&q_clone, "complete", payload);
                let (generation, completed) = &*completion_generation;
                if let Ok(mut generation) = generation.lock() {
                    *generation += 1;
                    completed.notify_all();
                }
            }
            Err(e) => {
                let err_msg = agent_error_event_text(&e);
                enqueue_event(&q_clone, "error", serde_json::json!(err_msg));
                let (generation, completed) = &*completion_generation;
                if let Ok(mut generation) = generation.lock() {
                    *generation += 1;
                    completed.notify_all();
                }
            }
        }
    };

    let isolation = Arc::clone(&session.isolation);
    let _ = isolation.block_on(async move {
        tokio::spawn(fut);
    });

    1
}

#[no_mangle]
pub unsafe extern "system" fn Java_dev_maho_browser_MahoBridge_nativeAgentCancel(
    _env: JNIEnv,
    _class: JClass,
    session_ptr: jlong,
) -> jboolean {
    let session = match acquire_agent_session(session_ptr) {
        Some(session) => session,
        None => return 0,
    };
    let runtime = Arc::clone(&session.runtime);
    let session_id = session.session_id.clone();

    let isolation = Arc::clone(&session.isolation);
    let res = isolation.block_on(async move { runtime.cancel(&session_id).await });

    if res.is_ok() {
        1
    } else {
        0
    }
}

#[no_mangle]
pub unsafe extern "system" fn Java_dev_maho_browser_MahoBridge_nativeAgentListTools<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    session_ptr: jlong,
) -> jstring {
    let session = match acquire_agent_session(session_ptr) {
        Some(session) => session,
        None => return std::ptr::null_mut(),
    };
    let runtime = Arc::clone(&session.runtime);

    let isolation = Arc::clone(&session.isolation);
    let tools_res = isolation.block_on(async move { runtime.list_tools().await });

    let tools: Vec<maho_agent::ToolDefinition> = match tools_res {
        Ok(Ok(t)) => t,
        _ => return std::ptr::null_mut(),
    };

    let json_str = serde_json::to_string(&tools).unwrap_or_else(|_| "[]".to_string());
    match env.new_string(&json_str) {
        Ok(s) => s.into_raw(),
        Err(_) => std::ptr::null_mut(),
    }
}

#[no_mangle]
pub unsafe extern "system" fn Java_dev_maho_browser_MahoBridge_nativeAgentListArtifacts<'local>(
    env: JNIEnv<'local>,
    _class: JClass<'local>,
    session_ptr: jlong,
) -> jstring {
    let session = match acquire_agent_session(session_ptr) {
        Some(session) => session,
        None => return std::ptr::null_mut(),
    };
    let runtime = Arc::clone(&session.runtime);
    let session_id = session.session_id.clone();
    let isolation = Arc::clone(&session.isolation);
    let artifacts = match isolation
        .block_on(async move { list_android_artifacts(runtime.as_ref(), &session_id).await })
    {
        Ok(Ok(artifacts)) => artifacts,
        _ => return std::ptr::null_mut(),
    };
    let json = match serde_json::to_string(&artifacts) {
        Ok(json) => json,
        Err(_) => return std::ptr::null_mut(),
    };
    match env.new_string(json) {
        Ok(value) => value.into_raw(),
        Err(_) => std::ptr::null_mut(),
    }
}

#[no_mangle]
pub unsafe extern "system" fn Java_dev_maho_browser_MahoBridge_nativeAgentReadArtifact<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    session_ptr: jlong,
    artifact_id: JString<'local>,
) -> jni::sys::jbyteArray {
    let session = match acquire_agent_session(session_ptr) {
        Some(session) => session,
        None => return std::ptr::null_mut(),
    };
    let artifact_id = match get_rust_string(&mut env, &artifact_id) {
        Some(id) if validate_artifact_id(&id).is_ok() => id,
        _ => return std::ptr::null_mut(),
    };
    let runtime = Arc::clone(&session.runtime);
    let session_id = session.session_id.clone();
    let artifact_root = session.artifact_root.clone();
    let isolation = Arc::clone(&session.isolation);
    let bytes = match isolation.block_on(async move {
        read_android_artifact(runtime.as_ref(), &session_id, &artifact_root, &artifact_id).await
    }) {
        Ok(Ok(bytes)) => bytes,
        _ => return std::ptr::null_mut(),
    };
    match env.byte_array_from_slice(&bytes) {
        Ok(value) => value.into_raw(),
        Err(_) => std::ptr::null_mut(),
    }
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeAgentInstallDeterministicFsWriteModel(
    _env: JNIEnv,
    _class: JClass,
    session_ptr: jlong,
) -> jboolean {
    let _session = match acquire_agent_session(session_ptr) {
        Some(session) => session,
        None => return 0,
    };
    1
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeAgentTestCompletionGeneration(
    _env: JNIEnv,
    _class: JClass,
    session_ptr: jlong,
) -> jlong {
    let session = match acquire_agent_session(session_ptr) {
        Some(session) => session,
        None => return -1,
    };
    session
        .completion_generation
        .0
        .lock()
        .map(|generation| *generation as jlong)
        .unwrap_or(-1)
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeAgentTestAwaitCompletion(
    _env: JNIEnv,
    _class: JClass,
    session_ptr: jlong,
    previous_generation: jlong,
    timeout_ms: jlong,
) -> jboolean {
    let session = match acquire_agent_session(session_ptr) {
        Some(session) => session,
        None => return 0,
    };
    let target = match u64::try_from(previous_generation) {
        Ok(generation) => generation,
        Err(_) => return 0,
    };
    let timeout = match u64::try_from(timeout_ms) {
        Ok(timeout) => std::time::Duration::from_millis(timeout),
        Err(_) => return 0,
    };
    let (generation, completed) = &*session.completion_generation;
    let generation = match generation.lock() {
        Ok(generation) => generation,
        Err(_) => return 0,
    };
    if *generation > target {
        return 1;
    }
    let result = match completed
        .wait_timeout_while(generation, timeout, |generation| *generation <= target)
    {
        Ok((generation, _)) if *generation > target => 1,
        _ => 0,
    };
    result
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeAgentTestEnqueueArtifactCreatedTwice<
    'local,
>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    session_ptr: jlong,
    artifact_id: JString<'local>,
) -> jboolean {
    let session = match acquire_agent_session(session_ptr) {
        Some(session) => session,
        None => return 0,
    };
    let artifact_id = match get_rust_string(&mut env, &artifact_id) {
        Some(artifact_id) if validate_artifact_id(&artifact_id).is_ok() => artifact_id,
        _ => return 0,
    };
    for _ in 0..2 {
        enqueue_event(
            &session.event_queue,
            "artifact_created",
            serde_json::json!({
                "artifact_id": artifact_id.clone(),
                "session_id": session.session_id.clone(),
                "display_name": "duplicate.txt",
                "mime_type": "text/plain",
                "size_bytes": 0,
                "created_at": 0.0,
            }),
        );
    }
    1
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeAgentTestSeedArtifact<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    session_ptr: jlong,
    artifact_id: JString<'local>,
    storage_rel_path: JString<'local>,
    display_name: JString<'local>,
    mime_type: JString<'local>,
) -> jboolean {
    let session = match acquire_agent_session(session_ptr) {
        Some(session) => session,
        None => return 0,
    };
    let artifact_id = match get_rust_string(&mut env, &artifact_id) {
        Some(artifact_id) if validate_artifact_id(&artifact_id).is_ok() => artifact_id,
        _ => return 0,
    };
    let storage_rel_path = match get_rust_string(&mut env, &storage_rel_path) {
        Some(path) => path,
        None => return 0,
    };
    let display_name = match get_rust_string(&mut env, &display_name) {
        Some(display_name) => display_name,
        None => return 0,
    };
    let mime_type = match get_rust_string(&mut env, &mime_type) {
        Some(mime_type) => mime_type,
        None => return 0,
    };
    let artifact = maho_types::artifact::ArtifactInfo {
        artifact_id,
        session_id: session.session_id.clone(),
        display_name,
        mime_type,
        size_bytes: 0,
        storage_rel_path,
        created_at_ms: 0,
        kind: None,
    };
    let storage = Arc::clone(&session.storage);
    let isolation = Arc::clone(&session.isolation);
    match isolation.block_on(async move { storage.save_artifact(&artifact).await }) {
        Ok(Ok(())) => 1,
        _ => 0,
    }
}

#[no_mangle]
pub unsafe extern "system" fn Java_dev_maho_browser_MahoBridge_nativeAgentPollEvent<'local>(
    env: JNIEnv<'local>,
    _class: JClass<'local>,
    session_ptr: jlong,
) -> jstring {
    let session = match acquire_agent_session(session_ptr) {
        Some(session) => session,
        None => return std::ptr::null_mut(),
    };

    let event = session
        .event_queue
        .lock()
        .ok()
        .and_then(|mut queue| queue.pop());

    match event {
        Some(evt) => match env.new_string(&evt) {
            Ok(s) => s.into_raw(),
            Err(_) => std::ptr::null_mut(),
        },
        None => std::ptr::null_mut(),
    }
}

#[no_mangle]
pub unsafe extern "system" fn Java_dev_maho_browser_MahoBridge_nativeQueryFilterDecision<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    url: JString<'local>,
    source_url: JString<'local>,
    request_type: JString<'local>,
) -> jboolean {
    let core = match get_core(ptr) {
        Some(c) => c,
        None => return 0,
    };
    let url_str = match get_rust_string(&mut env, &url) {
        Some(s) => s,
        None => return 0,
    };
    let source_str = match get_rust_string(&mut env, &source_url) {
        Some(s) => s,
        None => return 0,
    };
    let type_str = match get_rust_string(&mut env, &request_type) {
        Some(s) => s,
        None => return 0,
    };

    let Ok(_query_guard) = CONTENT_BLOCKER_QUERY_GATE.lock() else {
        return 0;
    };
    if core.should_block_request(&url_str, &source_str, &type_str) {
        1
    } else {
        0
    }
}

#[no_mangle]
pub unsafe extern "system" fn Java_dev_maho_browser_MahoBridge_nativeCreateConversation<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    id: JString<'local>,
    title: JString<'local>,
    space_id: JString<'local>,
    model: JString<'local>,
) -> jboolean {
    let core = match get_core(ptr) {
        Some(c) => c,
        None => return 0,
    };
    let id_str = match get_rust_string(&mut env, &id) {
        Some(s) => s,
        None => return 0,
    };
    let title_opt = get_rust_string(&mut env, &title).filter(|s| !s.is_empty());
    let space_opt = get_rust_string(&mut env, &space_id).filter(|s| !s.is_empty());
    let model_opt = get_rust_string(&mut env, &model).filter(|s| !s.is_empty());

    if core.create_conversation_persisted(
        &id_str,
        title_opt.as_deref(),
        space_opt.as_deref(),
        model_opt.as_deref(),
    ) {
        1
    } else {
        0
    }
}

#[no_mangle]
pub unsafe extern "system" fn Java_dev_maho_browser_MahoBridge_nativeListConversations<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    limit: jlong,
) -> jstring {
    let core = match get_core(ptr) {
        Some(c) => c,
        None => return std::ptr::null_mut(),
    };
    let conversations = if let Some(storage) = core.storage_ref() {
        storage
            .list_conversations(
                maho_types::chat::ConversationListState::Active,
                limit as usize,
            )
            .unwrap_or_default()
    } else {
        Vec::new()
    };
    let json_str = serde_json::to_string(&conversations).unwrap_or_else(|_| "[]".to_string());
    match env.new_string(&json_str) {
        Ok(s) => s.into_raw(),
        Err(_) => std::ptr::null_mut(),
    }
}

#[no_mangle]
pub unsafe extern "system" fn Java_dev_maho_browser_MahoBridge_nativeListConversationsV2<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    query_json: JString<'local>,
) -> jstring {
    let core = match get_core(ptr) {
        Some(c) => c,
        None => return std::ptr::null_mut(),
    };
    let query = match get_rust_string(&mut env, &query_json)
        .and_then(|json| serde_json::from_str::<serde_json::Value>(&json).ok())
    {
        Some(query) => query,
        None => return std::ptr::null_mut(),
    };
    let state = match query
        .get("state")
        .and_then(serde_json::Value::as_str)
        .unwrap_or("active")
    {
        "active" => maho_types::chat::ConversationListState::Active,
        "archived" => maho_types::chat::ConversationListState::Archived,
        "all" => maho_types::chat::ConversationListState::All,
        _ => return std::ptr::null_mut(),
    };
    let limit = query
        .get("limit")
        .and_then(serde_json::Value::as_u64)
        .unwrap_or(100) as usize;
    if limit == 0 {
        return std::ptr::null_mut();
    }
    let Some(json) = conversation_list_json(core, state, limit) else {
        return std::ptr::null_mut();
    };
    env.new_string(json)
        .map_or(std::ptr::null_mut(), |value| value.into_raw())
}

fn conversation_list_json(
    core: &MahoCore,
    state: maho_types::chat::ConversationListState,
    limit: usize,
) -> Option<String> {
    let rows = core.list_conversations(state, limit).ok()?;
    serde_json::to_string(&rows).ok()
}

#[no_mangle]
pub unsafe extern "system" fn Java_dev_maho_browser_MahoBridge_nativeArchiveConversation<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    id: JString<'local>,
) -> jboolean {
    let Some(core) = get_core(ptr) else { return 0 };
    let Some(id) = get_rust_string(&mut env, &id) else {
        return 0;
    };
    core.archive_conversation_persisted(&id) as jboolean
}

#[no_mangle]
pub unsafe extern "system" fn Java_dev_maho_browser_MahoBridge_nativeUnarchiveConversation<
    'local,
>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    id: JString<'local>,
) -> jboolean {
    let Some(core) = get_core(ptr) else { return 0 };
    let Some(id) = get_rust_string(&mut env, &id) else {
        return 0;
    };
    core.unarchive_conversation_persisted(&id) as jboolean
}

#[no_mangle]
pub unsafe extern "system" fn Java_dev_maho_browser_MahoBridge_nativeApplyConversationBulkOperation<
    'local,
>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    request_json: JString<'local>,
) -> jstring {
    let Some(core) = get_core(ptr) else {
        return std::ptr::null_mut();
    };
    let Some(request) = get_rust_string(&mut env, &request_json)
        .and_then(|json| serde_json::from_str::<serde_json::Value>(&json).ok())
    else {
        return std::ptr::null_mut();
    };
    let operation = match request.get("op").and_then(serde_json::Value::as_str) {
        Some("archive") => maho_types::chat::ConversationBulkOperation::Archive,
        Some("unarchive") => maho_types::chat::ConversationBulkOperation::Unarchive,
        Some("delete") => maho_types::chat::ConversationBulkOperation::Delete,
        _ => return std::ptr::null_mut(),
    };
    let Some(ids) = request
        .get("ids")
        .and_then(serde_json::Value::as_array)
        .map(|ids| {
            ids.iter()
                .filter_map(serde_json::Value::as_str)
                .map(str::to_string)
                .collect::<Vec<_>>()
        })
    else {
        return std::ptr::null_mut();
    };
    let now = chrono::Utc::now().to_rfc3339();
    let value = match core.apply_conversation_bulk_operation(operation, ids, &now) {
        Ok(result) => serde_json::to_value(result).unwrap_or_default(),
        Err(error) => error,
    };
    env.new_string(value.to_string())
        .map_or(std::ptr::null_mut(), |value| value.into_raw())
}

#[no_mangle]
pub unsafe extern "system" fn Java_dev_maho_browser_MahoBridge_nativeGetConversationAutoArchivePolicy<
    'local,
>(
    _env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
) -> jint {
    get_core(ptr)
        .and_then(|core| core.get_conversation_auto_archive_policy().ok())
        .unwrap_or(-1)
}

#[no_mangle]
pub unsafe extern "system" fn Java_dev_maho_browser_MahoBridge_nativeSetConversationAutoArchivePolicy<
    'local,
>(
    _env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    days: jint,
) -> jboolean {
    get_core(ptr).is_some_and(|core| core.set_conversation_auto_archive_policy(days)) as jboolean
}

#[no_mangle]
pub unsafe extern "system" fn Java_dev_maho_browser_MahoBridge_nativeAutoArchiveConversations<
    'local,
>(
    _env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    now_seconds: jlong,
) -> jint {
    match get_core(ptr).map(|core| core.auto_archive_conversations(now_seconds)) {
        Some(Ok(Some(count))) => i32::try_from(count).unwrap_or(i32::MAX),
        Some(Ok(None)) => -2,
        Some(Err(_)) | None => -1,
    }
}

#[no_mangle]
pub unsafe extern "system" fn Java_dev_maho_browser_MahoBridge_nativeGetConversationMessages<
    'local,
>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    session_id: JString<'local>,
) -> jstring {
    let core = match get_core(ptr) {
        Some(c) => c,
        None => return std::ptr::null_mut(),
    };
    let sid_str = match get_rust_string(&mut env, &session_id) {
        Some(s) => s,
        None => return std::ptr::null_mut(),
    };
    let messages = if let Some(storage) = core.storage_ref() {
        storage
            .get_conversation_messages(&sid_str)
            .unwrap_or_default()
    } else {
        Vec::new()
    };
    let json_str = serde_json::to_string(&messages).unwrap_or_else(|_| "[]".to_string());
    match env.new_string(&json_str) {
        Ok(s) => s.into_raw(),
        Err(_) => std::ptr::null_mut(),
    }
}

#[no_mangle]
pub unsafe extern "system" fn Java_dev_maho_browser_MahoBridge_nativeSaveConversationMessage<
    'local,
>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    session_id: JString<'local>,
    role: JString<'local>,
    content: JString<'local>,
    url_context: JString<'local>,
) -> jboolean {
    let core = match get_core(ptr) {
        Some(c) => c,
        None => return 0,
    };
    let sid_str = match get_rust_string(&mut env, &session_id) {
        Some(s) => s,
        None => return 0,
    };
    let role_str = match get_rust_string(&mut env, &role) {
        Some(s) => s,
        None => return 0,
    };
    let content_str = match get_rust_string(&mut env, &content) {
        Some(s) => s,
        None => return 0,
    };
    let url_opt = get_rust_string(&mut env, &url_context).filter(|s| !s.is_empty());

    if core.save_conversation_message_persisted(
        &sid_str,
        &role_str,
        &content_str,
        url_opt.as_deref(),
    ) {
        1
    } else {
        0
    }
}

#[no_mangle]
pub unsafe extern "system" fn Java_dev_maho_browser_MahoBridge_nativeDeleteConversation<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    id: JString<'local>,
) -> jboolean {
    let core = match get_core(ptr) {
        Some(c) => c,
        None => return 0,
    };
    let id_str = match get_rust_string(&mut env, &id) {
        Some(s) => s,
        None => return 0,
    };
    if core.delete_conversation_persisted(&id_str) {
        1
    } else {
        0
    }
}

#[no_mangle]
pub unsafe extern "system" fn Java_dev_maho_browser_MahoBridge_nativeListConversationProjects<
    'local,
>(
    env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
) -> jstring {
    let Some(core) = get_core(ptr) else {
        return std::ptr::null_mut();
    };
    let json = serde_json::to_string(&core.list_conversation_projects())
        .unwrap_or_else(|_| "[]".to_string());
    env.new_string(json)
        .map_or(std::ptr::null_mut(), |value| value.into_raw())
}

#[no_mangle]
pub unsafe extern "system" fn Java_dev_maho_browser_MahoBridge_nativeCreateConversationProject<
    'local,
>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    name: JString<'local>,
) -> jstring {
    let Some(core) = get_core(ptr) else {
        return std::ptr::null_mut();
    };
    let Some(name) = get_rust_string(&mut env, &name) else {
        return std::ptr::null_mut();
    };
    let Some(project) = core.create_conversation_project_persisted(&name) else {
        return std::ptr::null_mut();
    };
    let Ok(json) = serde_json::to_string(&project) else {
        return std::ptr::null_mut();
    };
    env.new_string(json)
        .map_or(std::ptr::null_mut(), |value| value.into_raw())
}

#[no_mangle]
pub unsafe extern "system" fn Java_dev_maho_browser_MahoBridge_nativeRenameConversationProject<
    'local,
>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    id: JString<'local>,
    name: JString<'local>,
) -> jboolean {
    let Some(core) = get_core(ptr) else { return 0 };
    let Some(id) = get_rust_string(&mut env, &id) else {
        return 0;
    };
    let Some(name) = get_rust_string(&mut env, &name) else {
        return 0;
    };
    core.rename_conversation_project_persisted(&id, &name) as jboolean
}

#[no_mangle]
pub unsafe extern "system" fn Java_dev_maho_browser_MahoBridge_nativeDeleteConversationProject<
    'local,
>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    id: JString<'local>,
) -> jboolean {
    let Some(core) = get_core(ptr) else { return 0 };
    let Some(id) = get_rust_string(&mut env, &id) else {
        return 0;
    };
    core.delete_conversation_project_persisted(&id) as jboolean
}

#[no_mangle]
pub unsafe extern "system" fn Java_dev_maho_browser_MahoBridge_nativeMoveConversationsToProject<
    'local,
>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    request_json: JString<'local>,
) -> jstring {
    let Some(core) = get_core(ptr) else {
        return std::ptr::null_mut();
    };
    let Some(request) = get_rust_string(&mut env, &request_json)
        .and_then(|json| serde_json::from_str::<serde_json::Value>(&json).ok())
    else {
        return std::ptr::null_mut();
    };
    let Some(ids) = request
        .get("ids")
        .and_then(serde_json::Value::as_array)
        .map(|values| {
            values
                .iter()
                .filter_map(serde_json::Value::as_str)
                .map(str::to_string)
                .collect::<Vec<_>>()
        })
    else {
        return std::ptr::null_mut();
    };
    let project_id = request.get("projectId").and_then(serde_json::Value::as_str);
    let Ok(result) = core.move_conversations_to_project_persisted(ids, project_id) else {
        return std::ptr::null_mut();
    };
    let Ok(json) = serde_json::to_string(&result) else {
        return std::ptr::null_mut();
    };
    env.new_string(json)
        .map_or(std::ptr::null_mut(), |value| value.into_raw())
}

#[no_mangle]
pub unsafe extern "system" fn Java_dev_maho_browser_MahoBridge_nativeRenameConversation<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    id: JString<'local>,
    title: JString<'local>,
) -> jboolean {
    let core = match get_core(ptr) {
        Some(c) => c,
        None => return 0,
    };
    let id_str = match get_rust_string(&mut env, &id) {
        Some(s) => s,
        None => return 0,
    };
    let title_str = match get_rust_string(&mut env, &title) {
        Some(s) => s,
        None => return 0,
    };
    if core.rename_conversation_persisted(&id_str, &title_str) {
        1
    } else {
        0
    }
}

// ============================================================
// Composer Draft JNI Bindings
// ============================================================
//
// These forward to the same `MahoCore` composer-draft methods that maho-ffi
// exposes to the Swift/iOS shell, so both mobile platforms share one storage
// contract (settings table, device/profile-local, never synced). `scope_json`
// is passed through verbatim: core owns scope parsing, key mapping,
// empty-string deletes and orphan reaping.

/// # Safety
/// `ptr` must have been created by `nativeCreate` and not yet freed.
#[no_mangle]
pub unsafe extern "system" fn Java_dev_maho_browser_MahoBridge_nativeGetComposerDraft<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    scope_json: JString<'local>,
) -> jstring {
    let core = match get_core(ptr) {
        Some(c) => c,
        None => return std::ptr::null_mut(),
    };
    let scope_str = match get_rust_string(&mut env, &scope_json) {
        Some(s) => s,
        None => return std::ptr::null_mut(),
    };
    // A missing draft, an orphaned scope and a malformed scope all surface as
    // null, which Kotlin reads as "no draft".
    let Some(draft_json) = core.get_composer_draft(&scope_str) else {
        return std::ptr::null_mut();
    };
    match env.new_string(&draft_json) {
        Ok(s) => s.into_raw(),
        Err(_) => std::ptr::null_mut(),
    }
}

/// # Safety
/// `ptr` must have been created by `nativeCreate` and not yet freed.
#[no_mangle]
pub unsafe extern "system" fn Java_dev_maho_browser_MahoBridge_nativeSetComposerDraft<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    scope_json: JString<'local>,
    text: JString<'local>,
) -> jboolean {
    let core = match get_core(ptr) {
        Some(c) => c,
        None => return 0,
    };
    let scope_str = match get_rust_string(&mut env, &scope_json) {
        Some(s) => s,
        None => return 0,
    };
    // Text is stored verbatim and is never trimmed; an empty string deletes the
    // row, so it must NOT be filtered out here.
    let text_str = match get_rust_string(&mut env, &text) {
        Some(s) => s,
        None => return 0,
    };
    if core.set_composer_draft(&scope_str, &text_str) {
        1
    } else {
        0
    }
}

/// # Safety
/// `ptr` must have been created by `nativeCreate` and not yet freed.
#[no_mangle]
pub unsafe extern "system" fn Java_dev_maho_browser_MahoBridge_nativeDeleteComposerDraft<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    scope_json: JString<'local>,
) -> jboolean {
    let core = match get_core(ptr) {
        Some(c) => c,
        None => return 0,
    };
    let scope_str = match get_rust_string(&mut env, &scope_json) {
        Some(s) => s,
        None => return 0,
    };
    if core.delete_composer_draft(&scope_str) {
        1
    } else {
        0
    }
}

// ============================================================
// Chat Session JNI Bindings
// ============================================================
//
// These forward directly to `maho_core::chat_session::ChatSession` (the same
// type maho-ffi exposes to the Swift/iOS shells), so there is no duplicated
// chat business logic here. The C-ABI event sink pushes streamed tokens,
// completions, and errors into a per-session queue that Kotlin drains via
// `nativeChatSessionPollEvent` — mirroring the poll-based Agent bindings above.

/// Per-session event buffer drained by `nativeChatSessionPollEvent`.
type ChatEventQueue = Mutex<Vec<String>>;

/// Owns a `ChatSession` plus the queue the event sink writes into. The sink's
/// `user_data` borrows the queue via `Arc::as_ptr`; the `Arc` stored here keeps
/// that allocation alive for as long as the session (and its streaming task,
/// which `ChatSession::drop` aborts) can reference it.
pub struct MahoChatJniSession {
    session: maho_core::chat_session::ChatSession,
    queue: Arc<ChatEventQueue>,
}

const MAX_CHAT_EVENT_QUEUE_DEPTH: usize = 2048;

fn push_chat_event(queue: &ChatEventQueue, event_type: &str, data: serde_json::Value) {
    let seq = EVENT_SEQ.fetch_add(1, Ordering::Relaxed);
    let payload = serde_json::json!({
        "type": event_type,
        "seq": seq,
        "data": data,
    });
    if let Ok(mut lock) = queue.lock() {
        let terminal = matches!(event_type, "complete" | "error");
        if lock.len() >= MAX_CHAT_EVENT_QUEUE_DEPTH {
            if !terminal {
                return;
            }
            // Terminal callbacks carry the authoritative result. Prefer losing
            // a streamed fragment; never discard a completion already queued.
            if let Some(index) = lock.iter().position(|event| {
                let event: serde_json::Value =
                    serde_json::from_str(event).expect("queue contains serialized events");
                !matches!(event["type"].as_str(), Some("complete" | "error"))
            }) {
                lock.remove(index);
            }
        }
        lock.push(payload.to_string());
    }
}

/// # Safety
/// `user_data` must point to a live `ChatEventQueue` (kept alive by the owning
/// `MahoChatJniSession`). `token`, when non-null, must be a valid C string.
unsafe extern "C" fn chat_on_token(user_data: *mut c_void, token: *const c_char) {
    if user_data.is_null() || token.is_null() {
        return;
    }
    let queue = &*(user_data as *const ChatEventQueue);
    let Ok(token_str) = CStr::from_ptr(token).to_str() else {
        return;
    };
    push_chat_event(queue, "token", serde_json::json!(token_str));
}

/// # Safety
/// `user_data` must point to a live `ChatEventQueue`. The text pointers, when
/// non-null, must be valid C strings.
unsafe extern "C" fn chat_on_complete(
    user_data: *mut c_void,
    full_text: *const c_char,
    tool_calls_json: *const c_char,
) {
    if user_data.is_null() {
        return;
    }
    let queue = &*(user_data as *const ChatEventQueue);
    let full = if full_text.is_null() {
        String::new()
    } else {
        CStr::from_ptr(full_text).to_string_lossy().into_owned()
    };
    let tool_calls = if tool_calls_json.is_null() {
        String::new()
    } else {
        CStr::from_ptr(tool_calls_json)
            .to_string_lossy()
            .into_owned()
    };
    push_chat_event(
        queue,
        "complete",
        serde_json::json!({
            "full_text": full,
            "tool_calls_json": tool_calls,
        }),
    );
}

/// # Safety
/// `user_data` must point to a live `ChatEventQueue`. `thinking`, when non-null,
/// must be a valid C string.
unsafe extern "C" fn chat_on_thinking(user_data: *mut c_void, thinking: *const c_char) {
    if user_data.is_null() || thinking.is_null() {
        return;
    }
    let queue = &*(user_data as *const ChatEventQueue);
    let Ok(thinking_str) = CStr::from_ptr(thinking).to_str() else {
        return;
    };
    push_chat_event(queue, "thinking", serde_json::json!(thinking_str));
}

/// # Safety
/// `user_data` must point to a live `ChatEventQueue`. `error`, when non-null,
/// must be a valid C string.
unsafe extern "C" fn chat_on_error(user_data: *mut c_void, error: *const c_char) {
    if user_data.is_null() {
        return;
    }
    let queue = &*(user_data as *const ChatEventQueue);
    let err = if error.is_null() {
        String::new()
    } else {
        CStr::from_ptr(error).to_string_lossy().into_owned()
    };
    push_chat_event(queue, "error", serde_json::json!(err));
}

/// Build a session wrapper with its event sink wired to a fresh queue.
/// Extracted from the JNI entrypoint so it can be unit-tested without a `JNIEnv`.
fn build_chat_session(config: maho_core::chat_session::ChatConfig) -> Box<MahoChatJniSession> {
    let mut session = maho_core::chat_session::ChatSession::new(config);
    let queue: Arc<ChatEventQueue> = Arc::new(Mutex::new(Vec::new()));
    let sink = maho_core::chat_session::MahoChatEventSink {
        on_token: Some(chat_on_token),
        on_thinking: Some(chat_on_thinking),
        on_complete: Some(chat_on_complete),
        on_error: Some(chat_on_error),
        user_data: Arc::as_ptr(&queue) as *mut c_void,
    };
    session.set_event_sink(sink);
    Box::new(MahoChatJniSession { session, queue })
}

/// # Safety
/// `ptr` must be a pointer returned by `nativeChatSessionNew` and not yet freed.
unsafe fn get_chat_session(ptr: jlong) -> Option<&'static mut MahoChatJniSession> {
    let ptr = ptr as *mut MahoChatJniSession;
    if ptr.is_null() {
        None
    } else {
        Some(&mut *ptr)
    }
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeChatSessionNew<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    api_key: JString<'local>,
    endpoint: JString<'local>,
    model: JString<'local>,
    system_instruction: JString<'local>,
) -> jlong {
    let api_key = get_rust_string(&mut env, &api_key).unwrap_or_default();
    let endpoint = match get_rust_string(&mut env, &endpoint) {
        Some(s) => s,
        None => return 0,
    };
    let model = match get_rust_string(&mut env, &model) {
        Some(s) => s,
        None => return 0,
    };
    let system_instruction = get_rust_string(&mut env, &system_instruction).unwrap_or_default();

    let config = maho_core::chat_session::ChatConfig {
        api_key,
        endpoint,
        model,
        system_instruction,
    };
    Box::into_raw(build_chat_session(config)) as jlong
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeChatSessionFree(
    _env: JNIEnv,
    _class: JClass,
    session_ptr: jlong,
) {
    let ptr = session_ptr as *mut MahoChatJniSession;
    if !ptr.is_null() {
        // Fields drop in declaration order: `session` first (its `Drop` aborts any
        // active streaming task), then `queue`, so no callback can outlive the queue.
        unsafe { drop(Box::from_raw(ptr)) };
    }
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeChatSendUserTurn<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    session_ptr: jlong,
    message: JString<'local>,
) -> jboolean {
    let session = match unsafe { get_chat_session(session_ptr) } {
        Some(s) => s,
        None => return jni::sys::JNI_FALSE,
    };
    let message = match get_rust_string(&mut env, &message) {
        Some(s) => s,
        None => return jni::sys::JNI_FALSE,
    };
    if session.session.send_user_turn(&message) {
        jni::sys::JNI_TRUE
    } else {
        jni::sys::JNI_FALSE
    }
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeChatSendImage<'local>(
    _env: JNIEnv<'local>,
    _class: JClass<'local>,
    _session_ptr: jlong,
    _mime: JString<'local>,
    _data: JByteArray<'local>,
) -> jboolean {
    // maho-core `ChatSession` (and maho-ffi) expose only text turns; there is no
    // multimodal/image message API. Report unsupported honestly rather than fake
    // success — the Kotlin bridge surfaces this as a failed send.
    jni::sys::JNI_FALSE
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeChatSendTextWithImage<'local>(
    _env: JNIEnv<'local>,
    _class: JClass<'local>,
    _session_ptr: jlong,
    _text: JString<'local>,
    _mime: JString<'local>,
    _data: JByteArray<'local>,
) -> jboolean {
    // See `nativeChatSendImage`: no multimodal path exists in core/ffi yet.
    jni::sys::JNI_FALSE
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeChatCancel(
    _env: JNIEnv,
    _class: JClass,
    session_ptr: jlong,
) -> jboolean {
    let session = match unsafe { get_chat_session(session_ptr) } {
        Some(s) => s,
        None => return jni::sys::JNI_FALSE,
    };
    session.session.cancel();
    jni::sys::JNI_TRUE
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeChatSessionPollEvent<'local>(
    env: JNIEnv<'local>,
    _class: JClass<'local>,
    session_ptr: jlong,
) -> jstring {
    let session = match unsafe { get_chat_session(session_ptr) } {
        Some(s) => s,
        None => return std::ptr::null_mut(),
    };
    let event = match session.queue.lock() {
        Ok(mut lock) => {
            if lock.is_empty() {
                None
            } else {
                Some(lock.remove(0))
            }
        }
        Err(_) => None,
    };
    match event {
        Some(evt) => match env.new_string(&evt) {
            Ok(s) => s.into_raw(),
            Err(_) => std::ptr::null_mut(),
        },
        None => std::ptr::null_mut(),
    }
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeChatRegisterTool<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    session_ptr: jlong,
    name: JString<'local>,
    description: JString<'local>,
    parameters_json: JString<'local>,
) -> jboolean {
    let session = match unsafe { get_chat_session(session_ptr) } {
        Some(s) => s,
        None => return jni::sys::JNI_FALSE,
    };
    let name = match get_rust_string(&mut env, &name) {
        Some(s) => s,
        None => return jni::sys::JNI_FALSE,
    };
    let description = match get_rust_string(&mut env, &description) {
        Some(s) => s,
        None => return jni::sys::JNI_FALSE,
    };
    let parameters_json = match get_rust_string(&mut env, &parameters_json) {
        Some(s) => s,
        None => return jni::sys::JNI_FALSE,
    };
    if session
        .session
        .register_tool(&name, &description, &parameters_json)
    {
        jni::sys::JNI_TRUE
    } else {
        jni::sys::JNI_FALSE
    }
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeChatSendToolResult<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    session_ptr: jlong,
    tool_call_id: JString<'local>,
    name: JString<'local>,
    output: JString<'local>,
    trigger: jboolean,
) -> jboolean {
    let session = match unsafe { get_chat_session(session_ptr) } {
        Some(s) => s,
        None => return jni::sys::JNI_FALSE,
    };
    let tool_call_id = match get_rust_string(&mut env, &tool_call_id) {
        Some(s) => s,
        None => return jni::sys::JNI_FALSE,
    };
    let name = match get_rust_string(&mut env, &name) {
        Some(s) => s,
        None => return jni::sys::JNI_FALSE,
    };
    let output = match get_rust_string(&mut env, &output) {
        Some(s) => s,
        None => return jni::sys::JNI_FALSE,
    };
    if session.session.send_tool_result(
        &tool_call_id,
        &name,
        &output,
        trigger != jni::sys::JNI_FALSE,
    ) {
        jni::sys::JNI_TRUE
    } else {
        jni::sys::JNI_FALSE
    }
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeChatAppendUserMessage<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    session_ptr: jlong,
    content: JString<'local>,
) -> jboolean {
    let session = match unsafe { get_chat_session(session_ptr) } {
        Some(s) => s,
        None => return jni::sys::JNI_FALSE,
    };
    let content = match get_rust_string(&mut env, &content) {
        Some(s) => s,
        None => return jni::sys::JNI_FALSE,
    };
    session.session.append_user_message(&content);
    jni::sys::JNI_TRUE
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeChatAppendAssistantMessage<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    session_ptr: jlong,
    content: JString<'local>,
    tool_calls_json: JString<'local>,
) -> jboolean {
    let session = match unsafe { get_chat_session(session_ptr) } {
        Some(s) => s,
        None => return jni::sys::JNI_FALSE,
    };
    let content = match get_rust_string(&mut env, &content) {
        Some(s) => s,
        None => return jni::sys::JNI_FALSE,
    };
    let tool_calls_json = get_rust_string(&mut env, &tool_calls_json).unwrap_or_default();
    session
        .session
        .append_assistant_message(&content, &tool_calls_json);
    jni::sys::JNI_TRUE
}

#[cfg(test)]
mod tests {
    use super::*;
    use async_trait::async_trait;
    use maho_agent::{AgentRuntime, AgentStorage};

    fn temporary_artifact_root(test_name: &str) -> std::path::PathBuf {
        let root = std::env::temp_dir().join(format!(
            "maho-jni-{test_name}-{}",
            maho_types::artifact::ArtifactId::new()
        ));
        std::fs::create_dir_all(&root).expect("create temporary artifact root");
        root
    }

    fn test_agent_runtime() -> (Arc<dyn AgentRuntime>, Arc<MutexAgentStorage>) {
        let sqlite =
            maho_storage::sqlite::SqliteStorage::open_in_memory_with_key("jni-artifact-test")
                .expect("open in-memory artifact index");
        let storage = Arc::new(MutexAgentStorage(Arc::new(Mutex::new(sqlite))));
        let runtime = maho_agent::omo::factory::create_agent_runtime(
            storage.clone(),
            None,
            std::env::temp_dir(),
            true,
        );
        (runtime, storage)
    }

    #[tokio::test]
    async fn android_artifact_driver_writes_queues_lists_and_reads_by_opaque_id() {
        let root = temporary_artifact_root("roundtrip");
        let relative_path = "session-1/report.txt";
        let absolute_path = root.join(relative_path);
        std::fs::create_dir_all(absolute_path.parent().expect("artifact parent"))
            .expect("create artifact parent");
        std::fs::write(&absolute_path, b"native-only contents").expect("write artifact fixture");

        let (runtime, storage) = test_agent_runtime();
        runtime.set_artifact_root(Some(root.clone()));
        let artifact = maho_types::artifact::ArtifactInfo {
            artifact_id: maho_types::artifact::ArtifactId::new(),
            session_id: "session-1".to_string(),
            display_name: "report.txt".to_string(),
            mime_type: "text/plain".to_string(),
            size_bytes: 20,
            storage_rel_path: relative_path.to_string(),
            created_at_ms: 1_750_000_000_000,
            kind: None,
        };
        storage
            .save_artifact(&artifact)
            .await
            .expect("persist artifact index entry");

        let queue = Arc::new(Mutex::new(AgentEventQueue::default()));
        enqueue_agent_stream_event(
            &queue,
            maho_agent::AgentStreamEvent::ArtifactCreated {
                artifact: artifact.clone(),
            },
        );
        let event = queue
            .lock()
            .expect("event queue")
            .pop()
            .expect("queued ArtifactCreated event");
        let event_json: serde_json::Value = serde_json::from_str(&event).expect("event JSON");
        assert_eq!(event_json["type"], "artifact_created");
        assert_eq!(event_json["data"]["artifact_id"], artifact.artifact_id);
        assert!(event_json["data"].get("storage_rel_path").is_none());

        let listed = list_android_artifacts(runtime.as_ref(), "session-1")
            .await
            .expect("list indexed artifacts");
        assert_eq!(listed.len(), 1);
        assert_eq!(listed[0].artifact_id, artifact.artifact_id);
        let listed_json = serde_json::to_value(&listed).expect("serialize Android artifact DTOs");
        assert!(listed_json[0].get("storageRelPath").is_none());

        let bytes = read_android_artifact(runtime.as_ref(), "session-1", &root, &artifact.artifact_id)
            .await
            .expect("read indexed artifact without exposing a path");
        assert_eq!(bytes, b"native-only contents");

        std::fs::remove_dir_all(root).expect("clean temporary artifact root");
    }

    #[cfg(unix)]
    #[tokio::test]
    async fn android_artifact_read_rejects_symlink_swaps_at_open_time() {
        use std::os::unix::fs::symlink;

        let root = temporary_artifact_root("nofollow");
        let outside = root.parent().expect("temporary root parent").join(format!(
            "maho-jni-outside-{}",
            maho_types::artifact::ArtifactId::new()
        ));
        std::fs::write(&outside, b"outside").expect("write outside fixture");
        symlink(&outside, root.join("swapped.txt")).expect("create final-component symlink");
        symlink(
            root.parent().expect("root parent"),
            root.join("swapped-dir"),
        )
        .expect("create directory-component symlink");
        let (runtime, storage) = test_agent_runtime();
        let directory_symlink_path = format!(
            "swapped-dir/{}",
            outside.file_name().expect("outside name").to_string_lossy()
        );
        for (artifact_id, storage_rel_path) in [
            ("final-symlink", "swapped.txt"),
            ("directory-symlink", directory_symlink_path.as_str()),
        ] {
            storage
                .save_artifact(&maho_types::artifact::ArtifactInfo {
                    artifact_id: artifact_id.to_string(),
                    session_id: "session-1".to_string(),
                    display_name: "outside.txt".to_string(),
                    mime_type: "text/plain".to_string(),
                    size_bytes: 7,
                    storage_rel_path: storage_rel_path.to_string(),
                    created_at_ms: 1,
                    kind: None,
                })
                .await
                .expect("persist symlink index fixture");
            assert!(
                read_android_artifact(runtime.as_ref(), "session-1", &root, artifact_id)
                    .await
                    .is_err(),
                "nofollow open accepted {artifact_id}"
            );
        }
        std::fs::remove_dir_all(root).expect("clean temporary artifact root");
        std::fs::remove_file(outside).expect("clean outside fixture");
    }

    #[tokio::test]
    async fn android_artifact_read_rejects_unknown_ids_and_escaping_index_paths() {
        let root = temporary_artifact_root("boundary");
        let outside = root.parent().expect("temporary root parent").join(format!(
            "maho-jni-outside-{}",
            maho_types::artifact::ArtifactId::new()
        ));
        std::fs::write(&outside, b"outside").expect("write outside fixture");
        let (runtime, storage) = test_agent_runtime();

        let unknown = read_android_artifact(runtime.as_ref(), "session-1", &root, "unknown")
            .await
            .expect_err("unknown artifact id must error");
        assert!(unknown.contains("Unknown artifact id"));

        let escaping = maho_types::artifact::ArtifactInfo {
            artifact_id: "escaping-id".to_string(),
            session_id: "session-1".to_string(),
            display_name: "outside.txt".to_string(),
            mime_type: "text/plain".to_string(),
            size_bytes: 7,
            storage_rel_path: format!(
                "../{}",
                outside
                    .file_name()
                    .expect("outside file name")
                    .to_string_lossy()
            ),
            created_at_ms: 1,
            kind: None,
        };
        storage
            .save_artifact(&escaping)
            .await
            .expect("persist malicious index fixture");
        let error = read_android_artifact(runtime.as_ref(), "session-1", &root, &escaping.artifact_id)
            .await
            .expect_err("escaping indexed path must error");
        assert!(error.contains("invalid relative path"));

        std::fs::remove_dir_all(root).expect("clean temporary artifact root");
        std::fs::remove_file(outside).expect("clean outside fixture");
    }

    #[test]
    fn artifact_ids_are_strictly_opaque() {
        for malformed in [
            "",
            ".",
            "..",
            "../escape",
            "/absolute",
            "a/b",
            "a\\b",
            "two words",
            "line\nbreak",
        ] {
            assert!(
                validate_artifact_id(malformed).is_err(),
                "accepted malformed id: {malformed:?}"
            );
        }
        assert!(validate_artifact_id("artifact-42_AZ.az").is_ok());
    }

    #[test]
    fn agent_event_queue_deduplicates_artifacts_drops_stale_oldest_and_resets_per_turn() {
        let mut queue = AgentEventQueue::default();
        for index in 0..=MAX_AGENT_EVENT_QUEUE_DEPTH {
            queue.push("token", None, format!("token-{index}"));
        }
        assert_eq!(queue.events.len(), MAX_AGENT_EVENT_QUEUE_DEPTH);
        assert_eq!(queue.pop().as_deref(), Some("token-1"));

        queue.push("artifact_created", Some("artifact-1"), "first".to_string());
        queue.push(
            "artifact_created",
            Some("artifact-1"),
            "duplicate".to_string(),
        );
        assert!(!queue.events.iter().any(|event| event == "duplicate"));
        queue.push("complete", None, "complete".to_string());
        queue.push(
            "artifact_created",
            Some("artifact-1"),
            "next-turn".to_string(),
        );
        assert!(queue.events.iter().any(|event| event == "next-turn"));
    }

    #[test]
    fn agent_session_registry_keeps_acquired_owners_alive_after_free() {
        let (runtime, storage) = test_agent_runtime();
        let root = temporary_artifact_root("registry");
        let session = MahoAgentSession {
            runtime,
            storage,
            isolation: Arc::new(SessionRuntime::new().expect("session runtime")),
            session_id: "registry-session".to_string(),
            event_queue: Arc::new(Mutex::new(AgentEventQueue::default())),
            completion_generation: Arc::new((Mutex::new(0), std::sync::Condvar::new())),
            artifact_root: root.clone(),
        };
        let handle = register_agent_session(session);
        let owner = acquire_agent_session(handle).expect("registered session");
        assert!(remove_agent_session(handle).is_some());
        assert!(acquire_agent_session(handle).is_none());
        assert_eq!(owner.session_id, "registry-session");
        std::fs::remove_dir_all(root).expect("clean temporary artifact root");
    }

    #[test]
    fn artifact_root_must_be_absolute_and_is_canonicalized() {
        assert!(prepare_artifact_root("").is_err());
        assert!(prepare_artifact_root("relative/artifacts").is_err());

        let root = temporary_artifact_root("root");
        assert_eq!(
            prepare_artifact_root(root.to_str().expect("UTF-8 temporary root"))
                .expect("prepare absolute root"),
            std::fs::canonicalize(&root).expect("canonical root")
        );
        std::fs::remove_dir_all(root).expect("clean temporary artifact root");
    }

    #[test]
    fn conversation_storage_failure_is_not_an_empty_history() {
        let core = MahoCore::new();
        assert!(core
            .list_conversations(maho_types::chat::ConversationListState::Active, 100)
            .is_err());
        assert_eq!(
            conversation_list_json(&core, maho_types::chat::ConversationListState::Active, 100),
            None
        );
    }

    #[test]
    fn full_chat_queue_preserves_terminal_callbacks() {
        let queue = ChatEventQueue::new(Vec::new());
        let user_data = &queue as *const ChatEventQueue as *mut c_void;
        for _ in 0..MAX_CHAT_EVENT_QUEUE_DEPTH {
            push_chat_event(&queue, "token", serde_json::json!("partial"));
        }
        unsafe {
            chat_on_complete(user_data, c"final answer".as_ptr(), c"[]".as_ptr());
            chat_on_error(user_data, c"request failed".as_ptr());
        }
        let events: Vec<serde_json::Value> = queue
            .lock()
            .unwrap()
            .iter()
            .map(|event| serde_json::from_str(event).unwrap())
            .collect();
        assert!(events.iter().any(|event|
            event["type"] == "complete" && event["data"]["full_text"] == "final answer"
        ));
        assert!(events.iter().any(|event|
            event["type"] == "error" && event["data"] == "request failed"
        ));
        assert!(events.len() <= MAX_CHAT_EVENT_QUEUE_DEPTH);
    }

    #[test]
    fn chat_session_wrapper_records_history_and_queues_sink_events() {
        let config = maho_core::chat_session::ChatConfig {
            api_key: String::new(),
            endpoint: String::new(),
            model: "test-model".to_string(),
            system_instruction: "be helpful".to_string(),
        };
        let mut wrapper = build_chat_session(config);

        wrapper.session.append_user_message("first question");
        wrapper.session.append_assistant_message("first answer", "");
        assert!(wrapper.session.register_tool("web_search", "search", "{}"));

        let user_data = Arc::as_ptr(&wrapper.queue) as *mut c_void;
        let token = std::ffi::CString::new("hello").expect("token c-string");
        let error = std::ffi::CString::new("boom").expect("error c-string");
        unsafe {
            chat_on_token(user_data, token.as_ptr());
            chat_on_error(user_data, error.as_ptr());
        }

        let events: Vec<String> = {
            let mut lock = wrapper.queue.lock().expect("queue lock");
            std::mem::take(&mut *lock)
        };
        assert_eq!(events.len(), 2);
        assert!(events[0].contains("\"type\":\"token\""));
        assert!(events[0].contains("hello"));
        assert!(events[1].contains("\"type\":\"error\""));
        assert!(events[1].contains("boom"));
    }

    #[test]
    fn chat_session_completion_event_carries_full_text_payload() {
        let config = maho_core::chat_session::ChatConfig {
            api_key: String::new(),
            endpoint: String::new(),
            model: "m".to_string(),
            system_instruction: String::new(),
        };
        let wrapper = build_chat_session(config);

        let user_data = Arc::as_ptr(&wrapper.queue) as *mut c_void;
        let full = std::ffi::CString::new("final answer").expect("full-text c-string");
        let tools = std::ffi::CString::new("[]").expect("tool-calls c-string");
        unsafe {
            chat_on_complete(user_data, full.as_ptr(), tools.as_ptr());
        }

        let event = wrapper
            .queue
            .lock()
            .expect("queue lock")
            .first()
            .cloned()
            .expect("one completion event");
        let parsed: serde_json::Value = serde_json::from_str(&event).expect("valid json");
        assert_eq!(parsed["type"], "complete");
        assert_eq!(parsed["data"]["full_text"], "final answer");
    }

    #[test]
    fn secure_storage_json_preserves_provider_overrides() {
        let bundle = parse_android_secure_storage_response(
            r#"{"key":"custom-key","base_url":"https://ai.example.test/v1","model":"custom-model"}"#,
        )
        .expect("valid secure-storage JSON should parse");

        assert_eq!(bundle.key.as_str(), "custom-key");
        assert_eq!(
            bundle.base_url.as_deref(),
            Some("https://ai.example.test/v1")
        );
        assert_eq!(bundle.model.as_deref(), Some("custom-model"));
    }

    #[test]
    fn legacy_secure_storage_plain_string_remains_supported() {
        let bundle = parse_android_secure_storage_response("legacy-key")
            .expect("legacy plain key should remain supported");

        assert_eq!(bundle.key.as_str(), "legacy-key");
        assert_eq!(bundle.base_url, None);
        assert_eq!(bundle.model, None);
    }

    #[test]
    fn malformed_secure_storage_json_is_not_used_as_a_key() {
        assert!(
            parse_android_secure_storage_response(r#"{"base_url":"https://example.test"}"#)
                .is_err()
        );
    }

    #[test]
    fn normal_preview_call_order_stays_compatible() {
        let mut core = MahoCore::new();
        let tab_id = maho_types::identifiers::TabId::new("test-tab");
        let image_data = maho_types::common::ImageData {
            data: vec![1, 2, 3],
            width: 0,
            height: 0,
            format: maho_types::common::ImageFormat::Png,
        };
        core.update_tab_preview(&tab_id, image_data);
        assert!(core.get_tab_preview(&tab_id).is_some());
    }

    #[test]
    fn private_preview_call_order_stays_compatible() {
        let mut core = MahoCore::new();
        let tab_id = maho_types::identifiers::TabId::new("private-tab");
        let image_data = maho_types::common::ImageData {
            data: vec![4, 5, 6],
            width: 0,
            height: 0,
            format: maho_types::common::ImageFormat::Png,
        };
        core.update_tab_preview(&tab_id, image_data);
        assert!(core.get_tab_preview(&tab_id).is_some());
    }

    #[test]
    fn agent_error_event_text_emits_typed_credential_envelope() {
        // A typed credential error must surface as the structured envelope so the
        // WebView UI can render an actionable credential card (never raw text).
        let error = AgentError::Credential(maho_agent::CredentialError::SecureStoreUnavailable);
        let text = agent_error_event_text(&error);
        let parsed: serde_json::Value =
            serde_json::from_str(&text).expect("credential error text must be JSON envelope");
        assert_eq!(parsed["kind"], "credential_error");
        assert_eq!(parsed["code"], "secure_store_unavailable");
        assert!(
            !text.contains("Secure storage callback"),
            "must not leak raw diagnostic text, got: {text}"
        );
    }

    #[test]
    fn agent_error_event_text_keeps_other_errors_as_plain_string() {
        let error = AgentError::Cancelled;
        let text = agent_error_event_text(&error);
        assert_eq!(text, "Cancelled");
    }

    #[test]
    fn secure_storage_parse_failures_are_typed_credential_errors() {
        // Malformed JSON secure-storage response is a KEY problem, not a raw
        // execution error: it must surface as a typed CredentialError so the
        // event envelope can carry a code.
        let bad = parse_android_secure_storage_response("{\"key\": unquoted}");
        assert!(
            matches!(bad, Err(AgentError::Credential(_))),
            "malformed secure-storage JSON must be typed"
        );
        let empty = parse_android_secure_storage_response(r#"{"key":""}"#);
        assert!(
            matches!(empty, Err(AgentError::Credential(_))),
            "empty secure key must be typed"
        );
    }
}
