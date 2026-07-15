use jni::objects::{JByteArray, JClass, JString};
use jni::sys::{jboolean, jlong, jstring};
use jni::JNIEnv;
use maho_core::maho_core::MahoCore;

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
    core.add_filter_list(id_str, name_str, url_str);
}

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeRebuildContentRules<'local>(
    _env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
) {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return,
    };
    core.rebuild_content_rules();
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
    let download_id = core.start_download(&filename_str, &url_str, 0, None, mime_type.as_deref());
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
    let json = core.join_sync(&phrase);
    match env.new_string(&json) {
        Ok(s) => s.into_raw(),
        Err(_) => std::ptr::null_mut(),
    }
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
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeSetAccentColor<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    color_json: JString<'local>,
) {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return,
    };
    let color_str = match get_rust_string(&mut env, &color_json) {
        Some(s) => s,
        None => return,
    };
    core.set_accent_color(&color_str);
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
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return,
    };
    let id_str = match get_rust_string(&mut env, &id) {
        Some(s) => s,
        None => return,
    };
    let content_str = match get_rust_string(&mut env, &content) {
        Some(s) => s,
        None => return,
    };
    core.update_filter_list_content(&id_str, content_str);
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

use std::sync::Arc;
use std::sync::Mutex;
use std::sync::OnceLock;
use jni::objects::GlobalRef;
use jni::JavaVM;
use maho_agent::{SwiftideBackend, SessionRuntime, MutexAgentStorage, ZeroizedString, AgentError, AgentRuntime};

pub struct MahoAgentSession {
    pub runtime: Arc<SwiftideBackend>,
    pub isolation: Arc<SessionRuntime>,
    pub session_id: String,
    pub event_queue: Arc<Mutex<Vec<String>>>,
}

static JAVA_VM: OnceLock<JavaVM> = OnceLock::new();
static MAHO_BRIDGE_CLASS: OnceLock<GlobalRef> = OnceLock::new();

fn get_jni_env() -> Option<jni::AttachGuard<'static>> {
    let vm = JAVA_VM.get()?;
    vm.attach_current_thread().ok()
}

fn call_android_permission_callback(tool_name: &str, arguments: &str) -> i32 {
    let Some(mut env_guard) = get_jni_env() else { return 1; };
    let env = &mut *env_guard;

    let Some(cls_ref) = MAHO_BRIDGE_CLASS.get() else { return 1; };
    let cls = unsafe { &*(cls_ref.as_obj() as *const jni::objects::JObject as *const jni::objects::JClass) };

    let Ok(tool_jstr) = env.new_string(tool_name) else { return 1; };
    let Ok(args_jstr) = env.new_string(arguments) else { return 1; };

    let val = env.call_static_method(
        cls,
        "requestAgentPermission",
        "(Ljava/lang/String;Ljava/lang/String;)I",
        &[
            jni::objects::JValue::Object(tool_jstr.as_ref()).into(),
            jni::objects::JValue::Object(args_jstr.as_ref()).into(),
        ]
    );

    match val {
        Ok(v) => v.i().unwrap_or(1),
        Err(_) => 1,
    }
}

fn call_android_secure_storage_callback(provider: &str) -> Option<String> {
    let Some(mut env_guard) = get_jni_env() else { return None; };
    let env = &mut *env_guard;

    let Some(cls_ref) = MAHO_BRIDGE_CLASS.get() else { return None; };
    let cls = unsafe { &*(cls_ref.as_obj() as *const jni::objects::JObject as *const jni::objects::JClass) };

    let Ok(provider_jstr) = env.new_string(provider) else { return None; };

    let val = env.call_static_method(
        cls,
        "getAgentSecureStorage",
        "(Ljava/lang/String;)Ljava/lang/String;",
        &[jni::objects::JValue::Object(provider_jstr.as_ref()).into()]
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

fn jni_permission_handler(req: maho_agent::PermissionRequest) -> std::pin::Pin<Box<dyn std::future::Future<Output = maho_agent::PermissionDecision> + Send>> {
    let decision = call_android_permission_callback(&req.tool_name, &req.arguments);
    let p_decision = if decision == 0 {
        maho_agent::PermissionDecision::Allow
    } else {
        maho_agent::PermissionDecision::Deny
    };
    Box::pin(async move { p_decision })
}

fn jni_secure_storage_handler(provider: &str) -> Result<ZeroizedString, AgentError> {
    if let Some(key) = call_android_secure_storage_callback(provider) {
        Ok(ZeroizedString::new(key))
    } else {
        Err(AgentError::ExecutionError("Secure key not found".to_string()))
    }
}

fn enqueue_event(queue: &Arc<Mutex<Vec<String>>>, event_type: &str, data: serde_json::Value) {
    const MAX_EVENT_QUEUE_DEPTH: usize = 2048;
    let payload = serde_json::json!({
        "type": event_type,
        "data": data
    });
    if let Ok(mut lock) = queue.lock() {
        if lock.len() < MAX_EVENT_QUEUE_DEPTH {
            lock.push(payload.to_string());
        }
    }
}

#[no_mangle]
pub unsafe extern "system" fn Java_dev_maho_browser_MahoBridge_nativeAgentCreateSession<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    core_ptr: jlong,
    session_id: JString<'local>,
) -> jlong {
    let core = match unsafe { get_core(core_ptr) } {
        Some(c) => c,
        None => return 0,
    };
    let sid = match get_rust_string(&mut env, &session_id) {
        Some(s) => s,
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

    let agent_storage = MutexAgentStorage(Arc::new(Mutex::new(storage)));

    let permission_cb = Box::new(|req| jni_permission_handler(req));
    let runtime = Arc::new(SwiftideBackend::new(
        Arc::new(agent_storage),
        Some(permission_cb),
        workspace_root,
        false, // allow_insecure_key_storage = false on mobile/Android JNI
    ));

    let secure_storage_cb = Box::new(|provider: &str| {
        jni_secure_storage_handler(provider).map(|key| maho_agent::SecureKeyBundle {
            key,
            base_url: None,
            model: None,
        })
    });
    if runtime.set_secure_storage_callback(secure_storage_cb).is_err() {
        return 0;
    }

    let isolation = match SessionRuntime::new() {
        Ok(i) => Arc::new(i),
        Err(_) => return 0,
    };

    let session = Box::new(MahoAgentSession {
        runtime,
        isolation,
        session_id: sid,
        event_queue: Arc::new(Mutex::new(Vec::new())),
    });

    Box::into_raw(session) as jlong
}

#[no_mangle]
pub unsafe extern "system" fn Java_dev_maho_browser_MahoBridge_nativeAgentFreeSession(
    _env: JNIEnv,
    _class: JClass,
    session_ptr: jlong,
) {
    let ptr = session_ptr as *mut MahoAgentSession;
    if !ptr.is_null() {
        drop(Box::from_raw(ptr));
    }
}

#[no_mangle]
pub unsafe extern "system" fn Java_dev_maho_browser_MahoBridge_nativeAgentSendMessage<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    session_ptr: jlong,
    message: JString<'local>,
) -> jboolean {
    let session = session_ptr as *mut MahoAgentSession;
    if session.is_null() {
        return 0;
    }
    let session = &mut *session;
    let msg_str = match get_rust_string(&mut env, &message) {
        Some(s) => s,
        None => return 0,
    };

    let runtime = Arc::clone(&session.runtime);
    let session_id = session.session_id.clone();
    let q_clone = Arc::clone(&session.event_queue);
    let q_clone_token = Arc::clone(&session.event_queue);

    let on_token = Box::new(move |token: &str| {
        enqueue_event(&q_clone_token, "token", serde_json::json!(token));
    });

    let fut = async move {
        let user_chat_msg = maho_types::chat::ChatMessage::user(maho_types::chat::ChatContent::text(msg_str));
        match runtime.run_turn(&session_id, user_chat_msg, Some(on_token)).await {
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
            }
            Err(e) => {
                let err_msg = e.to_string();
                enqueue_event(&q_clone, "error", serde_json::json!(err_msg));
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
    let session = session_ptr as *mut MahoAgentSession;
    if session.is_null() {
        return 0;
    }
    let session = &*session;
    let runtime = Arc::clone(&session.runtime);
    let session_id = session.session_id.clone();

    let isolation = Arc::clone(&session.isolation);
    let res = isolation.block_on(async move {
        runtime.cancel(&session_id).await
    });

    if res.is_ok() { 1 } else { 0 }
}

#[no_mangle]
pub unsafe extern "system" fn Java_dev_maho_browser_MahoBridge_nativeAgentListTools<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    session_ptr: jlong,
) -> jstring {
    let session = session_ptr as *mut MahoAgentSession;
    if session.is_null() {
        return std::ptr::null_mut();
    }
    let session = &*session;
    let runtime = Arc::clone(&session.runtime);

    let isolation = Arc::clone(&session.isolation);
    let tools_res = isolation.block_on(async move {
        runtime.list_tools().await
    });

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
pub unsafe extern "system" fn Java_dev_maho_browser_MahoBridge_nativeAgentPollEvent<'local>(
    env: JNIEnv<'local>,
    _class: JClass<'local>,
    session_ptr: jlong,
) -> jstring {
    let session = session_ptr as *mut MahoAgentSession;
    if session.is_null() {
        return std::ptr::null_mut();
    }
    let session = &*session;

    let event = if let Ok(mut lock) = session.event_queue.lock() {
        if lock.is_empty() {
            None
        } else {
            Some(lock.remove(0))
        }
    } else {
        None
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

    if core.create_conversation_persisted(&id_str, title_opt.as_deref(), space_opt.as_deref(), model_opt.as_deref()) {
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
        storage.list_conversations(limit as usize).unwrap_or_default()
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
pub unsafe extern "system" fn Java_dev_maho_browser_MahoBridge_nativeGetConversationMessages<'local>(
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
        storage.get_conversation_messages(&sid_str).unwrap_or_default()
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
pub unsafe extern "system" fn Java_dev_maho_browser_MahoBridge_nativeSaveConversationMessage<'local>(
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

    if core.save_conversation_message_persisted(&sid_str, &role_str, &content_str, url_opt.as_deref()) {
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

#[no_mangle]
pub extern "system" fn Java_dev_maho_browser_MahoBridge_nativeClusterTabs<'local>(
    mut env: JNIEnv<'local>,
    _class: JClass<'local>,
    ptr: jlong,
    threshold: jni::sys::jdouble,
) -> jstring {
    let core = match unsafe { get_core(ptr) } {
        Some(c) => c,
        None => return std::ptr::null_mut(),
    };
    let clusters = core.cluster_tabs(threshold);
    to_json_jstring(&mut env, &clusters)
}

#[cfg(test)]
mod tests {
    use super::*;

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
}


