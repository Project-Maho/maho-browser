use std::collections::HashMap;
use std::ffi::{c_char, c_void, CStr, CString};
use std::future::Future;
use std::path::PathBuf;
use std::pin::Pin;
use std::ptr;
use std::sync::atomic::Ordering;
use std::sync::{Arc, Arc as FfiArc, Mutex, OnceLock};

use maho_agent::MutexAgentStorage;
use maho_core::maho_core::MahoCore;
use maho_core::workspace_manager::WorkspaceManager;
use maho_storage::sqlite::SqliteStorage;

use crate::agent::callback_lease::{SendableCallback, SendableUserData};
use crate::common::{to_json_cstring, CallbackLease, NEXT_CALLBACK_TOKEN};
use crate::ffi_safe;
use crate::import_gate;

fn get_storage(core: *mut MahoCore) -> Option<SqliteStorage> {
    if core.is_null() {
        return None;
    }
    let core_ref = unsafe { &*core };
    let db_path = core_ref.sqlite_db_path()?;
    SqliteStorage::open(db_path).ok()
}

pub unsafe extern "C" fn maho_ai_profile_create(
    core: *mut MahoCore,
    name: *const c_char,
    system_prompt: *const c_char,
    model: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if core.is_null() || name.is_null() || system_prompt.is_null() {
                return ptr::null_mut();
            }
            let storage = match get_storage(core) {
                Some(s) => s,
                None => return ptr::null_mut(),
            };

            let name_str = CStr::from_ptr(name).to_str().unwrap_or("").to_string();
            let prompt_str = CStr::from_ptr(system_prompt)
                .to_str()
                .unwrap_or("")
                .to_string();
            let model_str = if model.is_null() {
                None
            } else {
                CStr::from_ptr(model).to_str().ok().map(String::from)
            };

            let id = uuid::Uuid::new_v4().to_string();
            let now = maho_types::common::DateTime::now().0;
            let profile = maho_types::ai::AiProfile {
                id: id.clone(),
                name: name_str,
                system_prompt: prompt_str,
                preferred_model: model_str,
                tools: vec![],
                mcp_servers: vec![],
                is_default: false,
                created_at: now.clone(),
                updated_at: now,
            };

            if storage.create_ai_profile(&profile).is_ok() {
                let res = serde_json::json!({ "id": id });
                to_json_cstring(&res)
            } else {
                ptr::null_mut()
            }
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `core` must be a valid pointer to a `MahoCore`.
/// Caller must free returned string via `maho_string_free`.
pub unsafe extern "C" fn maho_ai_profile_list(core: *mut MahoCore) -> *mut c_char {
    ffi_safe!(
        {
            let storage = match get_storage(core) {
                Some(s) => s,
                None => return ptr::null_mut(),
            };
            match storage.list_ai_profiles() {
                Ok(list) => to_json_cstring(&list),
                Err(_) => ptr::null_mut(),
            }
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `core` must be a valid pointer to a `MahoCore`.
/// `id` and `json` must be null-terminated UTF-8 strings.
pub unsafe extern "C" fn maho_ai_profile_update(
    core: *mut MahoCore,
    id: *const c_char,
    json: *const c_char,
) -> bool {
    ffi_safe!(
        {
            if core.is_null() || id.is_null() || json.is_null() {
                return false;
            }
            let storage = match get_storage(core) {
                Some(s) => s,
                None => return false,
            };
            let id_str = match CStr::from_ptr(id).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            let json_str = match CStr::from_ptr(json).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };

            let mut profile: maho_types::ai::AiProfile = match serde_json::from_str(json_str) {
                Ok(p) => p,
                Err(_) => return false,
            };

            profile.id = id_str.to_string();
            profile.updated_at = maho_types::common::DateTime::now().0;

            storage.update_ai_profile(&profile).is_ok()
        },
        false
    )
}

/// # Safety
/// `core` must be a valid pointer to a `MahoCore`.
/// `id` must be a null-terminated UTF-8 string.
pub unsafe extern "C" fn maho_ai_profile_delete(core: *mut MahoCore, id: *const c_char) -> bool {
    ffi_safe!(
        {
            if core.is_null() || id.is_null() {
                return false;
            }
            let storage = match get_storage(core) {
                Some(s) => s,
                None => return false,
            };
            let id_str = match CStr::from_ptr(id).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            storage.delete_ai_profile(id_str).is_ok()
        },
        false
    )
}

/// # Safety
/// `core` must be a valid pointer to a `MahoCore`.
/// `workspace_id` must be a null-terminated UTF-8 string.
/// `toml_path` must be a null-terminated UTF-8 string.
/// The caller must free the returned string with `maho_core_free_string`.
pub unsafe extern "C" fn maho_ai_profile_import_toml(
    core: *mut MahoCore,
    workspace_id: *const c_char,
    toml_path: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if core.is_null() || workspace_id.is_null() || toml_path.is_null() {
                return ptr::null_mut();
            }
            let ws_id = match CStr::from_ptr(workspace_id).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let path_str = match CStr::from_ptr(toml_path).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let toml_content = match std::fs::read_to_string(path_str) {
                Ok(content) => content,
                Err(e) => {
                    let err_str = format!("Error reading file: {}", e);
                    return CString::new(err_str)
                        .map(|c| c.into_raw())
                        .unwrap_or(ptr::null_mut());
                }
            };
            let storage = match get_storage(core) {
                Some(s) => s,
                None => return ptr::null_mut(),
            };
            match maho_core::profile_import::import_profile_from_toml(
                &storage,
                ws_id,
                &toml_content,
            ) {
                Ok(profile_id) => CString::new(profile_id)
                    .map(|c| c.into_raw())
                    .unwrap_or(ptr::null_mut()),
                Err(e) => {
                    let err_str = format!("Error: {}", e);
                    CString::new(err_str)
                        .map(|c| c.into_raw())
                        .unwrap_or(ptr::null_mut())
                }
            }
        },
        ptr::null_mut()
    )
}

// Workspace CRUD

/// # Safety
/// `core` must be a valid pointer to a `MahoCore`.
/// `name` must be a null-terminated UTF-8 string.
/// `space_id` is an optional null-terminated UTF-8 string (can be null).
/// Caller must free returned string via `maho_string_free`.
pub unsafe extern "C" fn maho_ai_workspace_create(
    core: *mut MahoCore,
    name: *const c_char,
    space_id: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if core.is_null() || name.is_null() {
                return ptr::null_mut();
            }
            let storage = match get_storage(core) {
                Some(s) => s,
                None => return ptr::null_mut(),
            };
            let manager = WorkspaceManager::new(storage);
            let name_str = CStr::from_ptr(name).to_str().unwrap_or("");
            let space_str = if space_id.is_null() {
                None
            } else {
                CStr::from_ptr(space_id).to_str().ok()
            };

            match manager.create_workspace(name_str, space_str) {
                Ok(ws) => to_json_cstring(&ws),
                Err(_) => ptr::null_mut(),
            }
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `core` must be a valid pointer to a `MahoCore`.
/// `space_id` must be a null-terminated UTF-8 string.
/// Caller must free returned string via `maho_string_free`.
pub unsafe extern "C" fn maho_ai_workspace_get_by_space(
    core: *mut MahoCore,
    space_id: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if core.is_null() || space_id.is_null() {
                return ptr::null_mut();
            }
            let storage = match get_storage(core) {
                Some(s) => s,
                None => return ptr::null_mut(),
            };
            let manager = WorkspaceManager::new(storage);
            let space_str = match CStr::from_ptr(space_id).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };

            match manager.get_active_workspace(space_str) {
                Ok(ws) => to_json_cstring(&ws),
                Err(_) => ptr::null_mut(),
            }
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `core` must be a valid pointer to a `MahoCore`.
/// Caller must free returned string via `maho_string_free`.
pub unsafe extern "C" fn maho_ai_workspace_list(core: *mut MahoCore) -> *mut c_char {
    ffi_safe!(
        {
            let storage = match get_storage(core) {
                Some(s) => s,
                None => return ptr::null_mut(),
            };
            match storage.list_workspaces() {
                Ok(list) => to_json_cstring(&list),
                Err(_) => ptr::null_mut(),
            }
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `core` must be a valid pointer to a `MahoCore`.
/// `workspace_id` and `profile_id` must be null-terminated UTF-8 strings.
pub unsafe extern "C" fn maho_ai_workspace_switch_profile(
    core: *mut MahoCore,
    workspace_id: *const c_char,
    profile_id: *const c_char,
) -> bool {
    ffi_safe!(
        {
            if core.is_null() || workspace_id.is_null() || profile_id.is_null() {
                return false;
            }
            let storage = match get_storage(core) {
                Some(s) => s,
                None => return false,
            };
            let manager = WorkspaceManager::new(storage);
            let ws_str = match CStr::from_ptr(workspace_id).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            let prof_str = match CStr::from_ptr(profile_id).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            manager.switch_profile(ws_str, prof_str).is_ok()
        },
        false
    )
}

// MCP Server management

/// # Safety
/// `core` must be a valid pointer to a `MahoCore`.
/// `workspace_id` and `config_json` must be null-terminated UTF-8 strings.
pub unsafe extern "C" fn maho_ai_mcp_server_register(
    core: *mut MahoCore,
    workspace_id: *const c_char,
    config_json: *const c_char,
) -> bool {
    ffi_safe!(
        {
            if core.is_null() || workspace_id.is_null() || config_json.is_null() {
                return false;
            }
            let storage = match get_storage(core) {
                Some(s) => s,
                None => return false,
            };
            let manager = WorkspaceManager::new(storage);
            let ws_str = match CStr::from_ptr(workspace_id).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            let json_str = match CStr::from_ptr(config_json).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            let config: maho_types::ai::AiMcpServer = match serde_json::from_str(json_str) {
                Ok(c) => c,
                Err(_) => return false,
            };
            manager.register_mcp_server(ws_str, config).is_ok()
        },
        false
    )
}

/// # Safety
/// `core` must be a valid pointer to a `MahoCore`.
/// `workspace_id` and `server_name` must be null-terminated UTF-8 strings.
pub unsafe extern "C" fn maho_ai_mcp_server_remove(
    core: *mut MahoCore,
    workspace_id: *const c_char,
    server_name: *const c_char,
) -> bool {
    ffi_safe!(
        {
            if core.is_null() || workspace_id.is_null() || server_name.is_null() {
                return false;
            }
            let storage = match get_storage(core) {
                Some(s) => s,
                None => return false,
            };
            let manager = WorkspaceManager::new(storage);
            let ws_str = match CStr::from_ptr(workspace_id).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            let name_str = match CStr::from_ptr(server_name).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            manager.remove_mcp_server(ws_str, name_str).is_ok()
        },
        false
    )
}

/// # Safety
/// `core` must be a valid pointer to a `MahoCore`.
/// `workspace_id` must be a null-terminated UTF-8 string.
/// Caller must free returned string via `maho_string_free`.
pub unsafe extern "C" fn maho_ai_mcp_server_list(
    core: *mut MahoCore,
    workspace_id: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if core.is_null() || workspace_id.is_null() {
                return ptr::null_mut();
            }
            let storage = match get_storage(core) {
                Some(s) => s,
                None => return ptr::null_mut(),
            };
            let ws_str = match CStr::from_ptr(workspace_id).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            match storage.list_mcp_servers(ws_str) {
                Ok(list) => to_json_cstring(&list),
                Err(_) => ptr::null_mut(),
            }
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `core` must be a valid pointer to a `MahoCore`.
/// `workspace_id`, `server_name`, and `tools_json` must be null-terminated UTF-8 strings.
pub unsafe extern "C" fn maho_ai_mcp_server_approve_trust(
    core: *mut MahoCore,
    workspace_id: *const c_char,
    server_name: *const c_char,
    tools_json: *const c_char,
) -> bool {
    ffi_safe!(
        {
            if core.is_null()
                || workspace_id.is_null()
                || server_name.is_null()
                || tools_json.is_null()
            {
                return false;
            }
            let storage = match get_storage(core) {
                Some(s) => s,
                None => return false,
            };
            let ws_str = match CStr::from_ptr(workspace_id).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            let name_str = match CStr::from_ptr(server_name).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            let tools_str = match CStr::from_ptr(tools_json).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            let tools: Vec<String> = match serde_json::from_str(tools_str) {
                Ok(t) => t,
                Err(_) => return false,
            };

            let mut servers = match storage.list_mcp_servers(ws_str) {
                Ok(list) => list,
                Err(_) => return false,
            };
            let server = match servers.iter_mut().find(|s| s.name == name_str) {
                Some(s) => s,
                None => return false,
            };

            server.trusted = true;
            server.trusted_tools = if tools.is_empty() { None } else { Some(tools) };
            server.updated_at = maho_types::common::DateTime::now().0;

            storage.update_mcp_server(server).is_ok()
        },
        false
    )
}

// CLI Tool management

/// # Safety
/// `core` must be a valid pointer to a `MahoCore`.
/// `workspace_id` and `tool_json` must be null-terminated UTF-8 strings.
pub unsafe extern "C" fn maho_ai_cli_tool_register(
    core: *mut MahoCore,
    workspace_id: *const c_char,
    tool_json: *const c_char,
) -> bool {
    ffi_safe!(
        {
            if core.is_null() || workspace_id.is_null() || tool_json.is_null() {
                return false;
            }
            let storage = match get_storage(core) {
                Some(s) => s,
                None => return false,
            };
            let manager = WorkspaceManager::new(storage);
            let ws_str = match CStr::from_ptr(workspace_id).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            let json_str = match CStr::from_ptr(tool_json).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            let tool: maho_types::ai::AiCliTool = match serde_json::from_str(json_str) {
                Ok(t) => t,
                Err(_) => return false,
            };
            manager.register_cli_tool(ws_str, tool).is_ok()
        },
        false
    )
}

/// # Safety
/// `core` must be a valid pointer to a `MahoCore`.
/// `workspace_id` and `tool_name` must be null-terminated UTF-8 strings.
pub unsafe extern "C" fn maho_ai_cli_tool_remove(
    core: *mut MahoCore,
    workspace_id: *const c_char,
    tool_name: *const c_char,
) -> bool {
    ffi_safe!(
        {
            if core.is_null() || workspace_id.is_null() || tool_name.is_null() {
                return false;
            }
            let storage = match get_storage(core) {
                Some(s) => s,
                None => return false,
            };
            let manager = WorkspaceManager::new(storage);
            let ws_str = match CStr::from_ptr(workspace_id).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            let name_str = match CStr::from_ptr(tool_name).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            manager.remove_cli_tool(ws_str, name_str).is_ok()
        },
        false
    )
}

/// # Safety
/// `core` must be a valid pointer to a `MahoCore`.
/// `workspace_id` must be a null-terminated UTF-8 string.
/// Caller must free returned string via `maho_string_free`.
pub unsafe extern "C" fn maho_ai_cli_tool_list(
    core: *mut MahoCore,
    workspace_id: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if core.is_null() || workspace_id.is_null() {
                return ptr::null_mut();
            }
            let storage = match get_storage(core) {
                Some(s) => s,
                None => return ptr::null_mut(),
            };
            let ws_str = match CStr::from_ptr(workspace_id).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            match storage.list_cli_tools(ws_str) {
                Ok(list) => to_json_cstring(&list),
                Err(_) => ptr::null_mut(),
            }
        },
        ptr::null_mut()
    )
}

// Active tool set

/// # Safety
/// `core` must be a valid pointer to a `MahoCore`.
/// `workspace_id` must be a null-terminated UTF-8 string.
/// Caller must free returned string via `maho_string_free`.
pub unsafe extern "C" fn maho_ai_workspace_get_tools(
    core: *mut MahoCore,
    workspace_id: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if core.is_null() || workspace_id.is_null() {
                return ptr::null_mut();
            }
            let storage = match get_storage(core) {
                Some(s) => s,
                None => return ptr::null_mut(),
            };
            let manager = WorkspaceManager::new(storage);
            let ws_str = match CStr::from_ptr(workspace_id).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };

            let core_ref = &*core;
            let registry = core_ref.tool_registry();
            match manager.get_effective_tools(ws_str, registry) {
                Ok(tools) => to_json_cstring(&tools),
                Err(_) => ptr::null_mut(),
            }
        },
        ptr::null_mut()
    )
}

// ============================================================
// Account Tier FFI

// ============================================================
// Routines FFI
// ============================================================

type RoutineStatusCb = unsafe extern "C" fn(*mut c_void, *const c_char);

#[derive(Clone)]
struct RoutineStatusCallback {
    token: u64,
    core: usize,
    user_data: usize,
    callback: RoutineStatusCb,
    lease: FfiArc<CallbackLease>,
}

static ROUTINE_RUN_REGISTRIES: OnceLock<
    StdMutex<HashMap<usize, FfiArc<maho_core::routine_runs::RoutineRunRegistry>>>,
> = OnceLock::new();
static ROUTINE_STATUS_CALLBACKS: OnceLock<StdMutex<Vec<RoutineStatusCallback>>> = OnceLock::new();

fn routine_status_callbacks() -> &'static StdMutex<Vec<RoutineStatusCallback>> {
    ROUTINE_STATUS_CALLBACKS.get_or_init(|| StdMutex::new(Vec::new()))
}

pub(crate) fn routine_run_registry(
    core: *mut MahoCore,
) -> FfiArc<maho_core::routine_runs::RoutineRunRegistry> {
    let key = core as usize;
    let registries = ROUTINE_RUN_REGISTRIES.get_or_init(|| StdMutex::new(HashMap::new()));
    let mut registries = registries
        .lock()
        .unwrap_or_else(std::sync::PoisonError::into_inner);
    if let Some(registry) = registries.get(&key) {
        return FfiArc::clone(registry);
    }
    let registry = FfiArc::new(maho_core::routine_runs::RoutineRunRegistry::default());
    registry.add_observer(FfiArc::new(move |status| {
        let json = match serde_json::to_string(status)
            .ok()
            .and_then(|json| CString::new(json).ok())
        {
            Some(json) => json,
            None => return,
        };
        let callbacks = routine_status_callbacks()
            .lock()
            .map(|callbacks| {
                callbacks
                    .iter()
                    .filter(|entry| entry.core == key)
                    .cloned()
                    .collect::<Vec<_>>()
            })
            .unwrap_or_default();
        for entry in callbacks {
            let Some(_invocation) = entry.lease.enter(entry.token) else {
                continue;
            };
            unsafe {
                (entry.callback)(entry.user_data as *mut c_void, json.as_ptr());
            }
        }
    }));
    registries.insert(key, FfiArc::clone(&registry));
    registry
}

pub(crate) fn remove_routine_run_registry(core: *mut MahoCore) {
    let key = core as usize;
    if let Some(registries) = ROUTINE_RUN_REGISTRIES.get() {
        registries
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner)
            .remove(&key);
    }
    let removed = routine_status_callbacks()
        .lock()
        .map(|mut callbacks| {
            let mut removed = Vec::new();
            let mut index = 0;
            while index < callbacks.len() {
                if callbacks[index].core == key {
                    removed.push(callbacks.remove(index));
                } else {
                    index += 1;
                }
            }
            removed
        })
        .unwrap_or_default();
    for entry in removed {
        entry.lease.remove_and_wait(entry.token);
    }
}

fn validate_routine_start(
    core: *mut MahoCore,
    id: &str,
    tier: maho_core::routines::UserTier,
) -> Result<String, String> {
    if tier != maho_core::routines::UserTier::Max {
        return Err("Routines are available on Max tier only".to_string());
    }
    let db_path = unsafe { &*core }
        .sqlite_db_path()
        .map(str::to_string)
        .ok_or_else(|| "Routine storage unavailable".to_string())?;
    let storage = maho_storage::sqlite::SqliteStorage::open(&db_path)
        .map_err(|error| format!("Failed to open storage: {error}"))?;
    let exists = maho_core::routines::list_all_routines(&storage)
        .map_err(|error| error.to_string())?
        .iter()
        .any(|routine| routine.id == id);
    if !exists {
        return Err(format!("No routine found with id: {id}"));
    }
    Ok(db_path)
}

fn spawn_tracked_routine(
    core: *mut MahoCore,
    id: String,
    tier: maho_core::routines::UserTier,
    source: maho_core::routines::RoutineRunSource,
    on_complete: SendableCallback<Option<unsafe extern "C" fn(*mut c_void, *const c_char)>>,
    on_error: SendableCallback<Option<unsafe extern "C" fn(*mut c_void, *const c_char)>>,
    user_data: SendableUserData,
) -> Result<maho_core::routine_runs::RoutineRunStatus, String> {
    let db_path = validate_routine_start(core, &id, tier)?;
    let registry = routine_run_registry(core);
    let queued = registry.begin(&id, source);
    let queued_for_worker = queued.clone();
    maho_core::memory_manager::get_runtime().spawn_blocking(move || {
        let rt = match tokio::runtime::Builder::new_current_thread()
            .enable_all()
            .build()
        {
            Ok(rt) => rt,
            Err(error) => {
                let message = format!("Failed to build runtime: {error}");
                let _ = registry.mark_setup_failed(&queued_for_worker.run_id, message.clone());
                if let Some(callback) = on_error.0 {
                    if let Ok(error) = CString::new(message) {
                        unsafe { callback(user_data.0 as *mut c_void, error.as_ptr()) };
                    }
                }
                return;
            }
        };
        rt.block_on(async move {
            let storage = match maho_storage::sqlite::SqliteStorage::open(&db_path) {
                Ok(storage) => storage,
                Err(error) => {
                    let message = format!("Failed to open storage: {error}");
                    let _ = registry.mark_setup_failed(&queued_for_worker.run_id, message.clone());
                    if let Some(callback) = on_error.0 {
                        if let Ok(error) = CString::new(message) {
                            unsafe { callback(user_data.0 as *mut c_void, error.as_ptr()) };
                        }
                    }
                    return;
                }
            };
            let agent_storage = MutexAgentStorage(Arc::new(Mutex::new(storage)));
            let approval_registry = FfiArc::clone(&registry);
            let approval_run_id = queued_for_worker.run_id.clone();
            let permission_callback: maho_agent::PermissionCallback = Box::new(move |request| {
                let registry = FfiArc::clone(&approval_registry);
                let run_id = approval_run_id.clone();
                Box::pin(async move {
                    if request.sensitivity != maho_agent::ToolSensitivity::Sensitive {
                        return maho_agent::PermissionDecision::Allow;
                    }
                    match registry.request_approval(
                        &run_id,
                        request.tool_name,
                        request.sensitivity.as_str().to_string(),
                    ) {
                        Ok(waiter) => {
                            if waiter.await {
                                maho_agent::PermissionDecision::Allow
                            } else {
                                maho_agent::PermissionDecision::Deny
                            }
                        }
                        Err(_) => maho_agent::PermissionDecision::Deny,
                    }
                })
            });
            let backend = maho_agent::omo::factory::create_agent_runtime(
                Arc::new(agent_storage),
                Some(permission_callback),
                std::env::current_dir().unwrap_or_else(|_| std::path::PathBuf::from(".")),
                true,
            );
            let resolve_storage = match maho_storage::sqlite::SqliteStorage::open(&db_path) {
                Ok(storage) => storage,
                Err(error) => {
                    let message = format!("Failed to open storage: {error}");
                    let _ = registry.mark_setup_failed(&queued_for_worker.run_id, message.clone());
                    if let Some(callback) = on_error.0 {
                        if let Ok(error) = CString::new(message) {
                            unsafe { callback(user_data.0 as *mut c_void, error.as_ptr()) };
                        }
                    }
                    return;
                }
            };
            let result = maho_core::routines::run_routine_and_record_tracked(
                &id,
                tier,
                &resolve_storage,
                source,
                |prompt| async move { backend.run_prompt(&prompt).await.map_err(|e| e.to_string()) },
                &registry,
                queued_for_worker,
            )
            .await;
            match result {
                Ok(result) => {
                    if let Some(callback) = on_complete.0 {
                        if let Some(json) = serde_json::to_string(&result)
                            .ok()
                            .and_then(|json| CString::new(json).ok())
                        {
                            unsafe { callback(user_data.0 as *mut c_void, json.as_ptr()) };
                        }
                    }
                }
                Err(error) => {
                    if let Some(callback) = on_error.0 {
                        if let Ok(error) = CString::new(error.to_string()) {
                            unsafe { callback(user_data.0 as *mut c_void, error.as_ptr()) };
                        }
                    }
                }
            }
        });
    });
    Ok(queued)
}

/// Returns JSON array of all hardcoded routines.
/// Caller must free with `maho_string_free`.
///
/// # Safety
/// `out_json` must be a valid pointer to a `*mut c_char`.
pub unsafe extern "C" fn maho_routines_list(out_json: *mut *mut c_char) {
    ffi_safe!(
        {
            if out_json.is_null() {
                return;
            }
            let routines = maho_core::routines::list_routines();
            let json_ptr = to_json_cstring(routines);
            *out_json = json_ptr;
        },
        ()
    )
}

/// Kicks off an async routine run. Returns 0 on success (queued), -1 on error.
/// The `user_tier` parameter: 0=Free, 1=Pro, 2=Max.
/// `on_complete` is called with the result JSON when done.
/// `on_error` is called with an error string on failure.
///
/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
/// `id` must be a valid null-terminated C string.
pub unsafe extern "C" fn maho_routines_run(
    core: *mut MahoCore,
    id: *const c_char,
    user_tier: i32,
    on_complete: Option<
        unsafe extern "C" fn(user_data: *mut std::ffi::c_void, result_json: *const c_char),
    >,
    on_error: Option<unsafe extern "C" fn(user_data: *mut std::ffi::c_void, error: *const c_char)>,
    user_data: *mut std::ffi::c_void,
) -> i32 {
    ffi_safe!(
        {
            if core.is_null() || id.is_null() {
                return -1;
            }
            let id = match CStr::from_ptr(id).to_str() {
                Ok(id) => id.to_string(),
                Err(_) => return -1,
            };
            let tier = match user_tier {
                0 => maho_core::routines::UserTier::Free,
                1 => maho_core::routines::UserTier::Pro,
                2 => maho_core::routines::UserTier::Max,
                _ => return -1,
            };
            match spawn_tracked_routine(
                core,
                id,
                tier,
                maho_core::routines::RoutineRunSource::Manual,
                SendableCallback(on_complete),
                SendableCallback(on_error),
                SendableUserData(user_data as usize),
            ) {
                Ok(_) => 0,
                Err(_) => -1,
            }
        },
        -1
    )
}

/// Start a manual routine and return its queued status JSON immediately.
/// Caller owns the returned string and must free it with `maho_string_free`.
pub unsafe extern "C" fn maho_routines_start(
    core: *mut MahoCore,
    id: *const c_char,
    user_tier: i32,
    source: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if core.is_null() || id.is_null() || source.is_null() {
                return ptr::null_mut();
            }
            let id = match CStr::from_ptr(id).to_str() {
                Ok(id) => id.to_string(),
                Err(_) => return ptr::null_mut(),
            };
            let source = match CStr::from_ptr(source).to_str() {
                Ok("manual") => maho_core::routines::RoutineRunSource::Manual,
                Ok("scheduled") => maho_core::routines::RoutineRunSource::Scheduled,
                Ok("event") => maho_core::routines::RoutineRunSource::Event,
                _ => return ptr::null_mut(),
            };
            let tier = match user_tier {
                0 => maho_core::routines::UserTier::Free,
                1 => maho_core::routines::UserTier::Pro,
                2 => maho_core::routines::UserTier::Max,
                _ => return ptr::null_mut(),
            };
            match spawn_tracked_routine(
                core,
                id,
                tier,
                source,
                SendableCallback(None),
                SendableCallback(None),
                SendableUserData(0),
            ) {
                Ok(status) => to_json_cstring(&status),
                Err(_) => ptr::null_mut(),
            }
        },
        ptr::null_mut()
    )
}

/// Return every routine run known to this browser process, including terminal
/// runs. This is the initial snapshot paired with status callbacks.
pub unsafe extern "C" fn maho_routines_respond_to_approval(
    core: *mut MahoCore,
    run_id: *const c_char,
    approval_id: *const c_char,
    approved: bool,
) -> bool {
    ffi_safe!(
        {
            if core.is_null() || run_id.is_null() || approval_id.is_null() {
                return false;
            }
            let run_id = match CStr::from_ptr(run_id).to_str() {
                Ok(value) => value,
                Err(_) => return false,
            };
            let approval_id = match CStr::from_ptr(approval_id).to_str() {
                Ok(value) => value,
                Err(_) => return false,
            };
            routine_run_registry(core).respond_to_approval(run_id, approval_id, approved)
        },
        false
    )
}

pub unsafe extern "C" fn maho_routines_active_runs(core: *mut MahoCore) -> *mut c_char {
    ffi_safe!(
        {
            if core.is_null() {
                return ptr::null_mut();
            }
            to_json_cstring(&routine_run_registry(core).snapshot())
        },
        ptr::null_mut()
    )
}

pub unsafe extern "C" fn maho_routines_register_status_callback(
    core: *mut MahoCore,
    user_data: *mut c_void,
    callback: RoutineStatusCb,
) -> u64 {
    ffi_safe!(
        {
            if core.is_null() {
                return 0;
            }
            let _ = routine_run_registry(core);
            let token = NEXT_CALLBACK_TOKEN.fetch_add(1, Ordering::SeqCst);
            routine_status_callbacks()
                .lock()
                .unwrap_or_else(std::sync::PoisonError::into_inner)
                .push(RoutineStatusCallback {
                    token,
                    core: core as usize,
                    user_data: user_data as usize,
                    callback,
                    lease: FfiArc::new(CallbackLease::default()),
                });
            token
        },
        0
    )
}

pub unsafe extern "C" fn maho_routines_unregister_status_callback(core: *mut MahoCore, token: u64) {
    let _ = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        if core.is_null() || token == 0 {
            return;
        }
        let core_key = core as usize;
        let removed = routine_status_callbacks()
            .lock()
            .ok()
            .and_then(|mut callbacks| {
                callbacks
                    .iter()
                    .position(|entry| entry.core == core_key && entry.token == token)
                    .map(|index| callbacks.remove(index))
            });
        if let Some(entry) = removed {
            entry.lease.remove_and_wait(token);
        }
    }));
}

// ============================================================
// Custom Routines FFI (list-all / create / delete / fire-event)
// ============================================================

#[derive(serde::Deserialize)]
struct CreateCustomRoutineInput {
    name: String,
    prompt: String,
    #[serde(default)]
    schedule: Option<String>,
    #[serde(default)]
    trigger: Option<String>,
}

#[derive(serde::Deserialize)]
struct FireRoutineEventInput {
    kind: String,
    #[serde(default)]
    count: Option<u32>,
    #[serde(default)]
    channel: Option<String>,
}

/// Returns JSON array of ALL routines (built-in + user-defined custom), each an
/// object with `id`, `name`, `cron`, `trigger`, `description`, `enabled`, and
/// `source` ("builtin"|"custom"). Caller must free with `maho_string_free`.
///
/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
pub unsafe extern "C" fn maho_routines_list_all(core: *mut MahoCore) -> *mut c_char {
    ffi_safe!(
        {
            if core.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let db_path = match (&*core).sqlite_db_path() {
                Some(p) => p.to_string(),
                None => return ptr::null_mut(),
            };
            let storage = match maho_storage::sqlite::SqliteStorage::open(&db_path) {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            match maho_core::routines::list_all_routines(&storage) {
                Ok(views) => to_json_cstring(&views),
                Err(_) => ptr::null_mut(),
            }
        },
        ptr::null_mut()
    )
}

/// Create a user-defined routine from JSON `{name,prompt,schedule?,trigger?}`.
/// Validates the cron `schedule` and/or event `trigger` string when present.
/// Returns 0 on success, -1 on error (null args / parse failure / invalid
/// schedule-or-trigger / empty name-or-prompt / storage failure).
///
/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
/// `json` must be a valid null-terminated C string.
pub unsafe extern "C" fn maho_routines_create_custom(
    core: *mut MahoCore,
    json: *const c_char,
) -> i32 {
    ffi_safe!(
        {
            if core.is_null() || json.is_null() {
                return -1;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return -1;
            }
            let json_str = match CStr::from_ptr(json).to_str() {
                Ok(s) => s,
                Err(_) => return -1,
            };
            let input: CreateCustomRoutineInput = match serde_json::from_str(json_str) {
                Ok(v) => v,
                Err(_) => return -1,
            };
            if input.name.trim().is_empty() || input.prompt.trim().is_empty() {
                return -1;
            }
            // Normalize blank strings to None so an empty form field is treated as
            // "no trigger" rather than an invalid one.
            let schedule = input.schedule.filter(|s| !s.trim().is_empty());
            let trigger = input.trigger.filter(|s| !s.trim().is_empty());
            // Validate whichever trigger the caller supplied.
            if let Some(ref cron) = schedule {
                if maho_core::routines::CronSchedule::parse(cron).is_none() {
                    return -1;
                }
            }
            if let Some(ref ev) = trigger {
                if maho_core::routines::RoutineEvent::parse(ev).is_none() {
                    return -1;
                }
            }
            let db_path = match (&*core).sqlite_db_path() {
                Some(p) => p.to_string(),
                None => return -1,
            };
            let storage = match maho_storage::sqlite::SqliteStorage::open(&db_path) {
                Ok(s) => s,
                Err(_) => return -1,
            };
            let routine = maho_storage::CustomRoutine {
                id: uuid::Uuid::new_v4().to_string(),
                name: input.name,
                prompt: input.prompt,
                schedule,
                trigger,
                enabled: true,
                created_at: chrono::Utc::now().to_rfc3339(),
            };
            match storage.create_custom_routine(&routine) {
                Ok(()) => 0,
                Err(_) => -1,
            }
        },
        -1
    )
}

/// Delete a user-defined routine by id. Returns 0 if a row was removed, -1 on
/// null args / not-found / storage failure.
///
/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
/// `id` must be a valid null-terminated C string.
pub unsafe extern "C" fn maho_routines_delete_custom(
    core: *mut MahoCore,
    id: *const c_char,
) -> i32 {
    ffi_safe!(
        {
            if core.is_null() || id.is_null() {
                return -1;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return -1;
            }
            let id_str = match CStr::from_ptr(id).to_str() {
                Ok(s) => s,
                Err(_) => return -1,
            };
            let db_path = match (&*core).sqlite_db_path() {
                Some(p) => p.to_string(),
                None => return -1,
            };
            let storage = match maho_storage::sqlite::SqliteStorage::open(&db_path) {
                Ok(s) => s,
                Err(_) => return -1,
            };
            match storage.delete_custom_routine(id_str) {
                Ok(true) => 0,
                Ok(false) => -1,
                Err(_) => -1,
            }
        },
        -1
    )
}

/// Fire browser-event-triggered custom routines matching `event_json`
/// (`{"kind":"on_startup"}` or `{"kind":"on_many_tabs","count":N}`). Max-tier
/// gated (mirrors the scheduler tick); fire-and-forget — the batch runs async
/// on the shared, owned maho-core runtime so no panic crosses the FFI boundary
/// even when called from the Chromium C++ browser thread (no ambient runtime).
///
/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
/// `event_json` must be a valid null-terminated C string.
pub unsafe extern "C" fn maho_routines_fire_event(core: *mut MahoCore, event_json: *const c_char) {
    ffi_safe!(
        {
            if core.is_null() || event_json.is_null() {
                return;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return;
            }
            let core_ref = &*core;
            // Only Max users get event-triggered routines.
            match core_ref.get_account_tier() {
                Some(maho_types::account::UserTier::Max) => {}
                _ => return,
            };
            let json_str = match CStr::from_ptr(event_json).to_str() {
                Ok(s) => s,
                Err(_) => return,
            };
            let input: FireRoutineEventInput = match serde_json::from_str(json_str) {
                Ok(v) => v,
                Err(_) => return,
            };
            let event = match input.kind.as_str() {
                "on_startup" => maho_core::routines::RoutineEvent::OnStartup,
                // The fired event's `threshold` carries the ACTUAL live tab count; a
                // configured routine matches when its threshold <= this actual count.
                "on_many_tabs" => maho_core::routines::RoutineEvent::OnManyTabs {
                    threshold: input.count.unwrap_or(0),
                },
                // Deferred-wiring parity triggers: reachable here so an emitter can
                // fire them once wired, but no browser/mail adapter emits them yet.
                "on_notification" => maho_core::routines::RoutineEvent::OnNotification {
                    channel: input.channel.unwrap_or_default(),
                },
                "on_inbox_heartbeat" => maho_core::routines::RoutineEvent::OnInboxHeartbeat {
                    min_unread: input.count.unwrap_or(0),
                },
                _ => return,
            };
            let db_path = match core_ref.sqlite_db_path() {
                Some(p) => p.to_string(),
                None => return,
            };
            let registry = routine_run_registry(core);

            // Dispatch onto the shared, owned maho-core runtime (see `maho_routines_run`
            // for the no-ambient-runtime rationale). `fire_event_routines` borrows a
            // `!Send` SqliteStorage across an await, so drive it on a current-thread
            // runtime pinned to a `spawn_blocking` thread (the future never migrates).
            maho_core::memory_manager::get_runtime().spawn_blocking(move || {
                let rt = match tokio::runtime::Builder::new_current_thread()
                    .enable_all()
                    .build()
                {
                    Ok(rt) => rt,
                    Err(e) => {
                        eprintln!("[maho_routines_fire_event] failed to build runtime: {e}");
                        return;
                    }
                };
                rt.block_on(async move {
                    // Read handle for routine resolution.
                    let storage = match maho_storage::sqlite::SqliteStorage::open(&db_path) {
                        Ok(s) => s,
                        Err(e) => {
                            eprintln!("[maho_routines_fire_event] failed to open storage: {e}");
                            return;
                        }
                    };
                    // `fire_event_routines` may run several routines, so each run owns a
                    // backend whose permission callback is keyed to that run id.
                    let runner = |prompt: String, run_id: String| {
                        let agent_storage =
                            match maho_storage::sqlite::SqliteStorage::open(&db_path) {
                                Ok(storage) => MutexAgentStorage(Arc::new(Mutex::new(storage))),
                                Err(error) => {
                                    return Box::pin(async move { Err(error.to_string()) })
                                        as Pin<Box<dyn Future<Output = Result<String, String>>>>;
                                }
                            };
                        let approval_registry = FfiArc::clone(&registry);
                        let permission_callback: maho_agent::PermissionCallback =
                            Box::new(move |request| {
                                let registry = FfiArc::clone(&approval_registry);
                                let run_id = run_id.clone();
                                Box::pin(async move {
                                    if request.sensitivity != maho_agent::ToolSensitivity::Sensitive
                                    {
                                        return maho_agent::PermissionDecision::Allow;
                                    }
                                    match registry.request_approval(
                                        &run_id,
                                        request.tool_name,
                                        request.sensitivity.as_str().to_string(),
                                    ) {
                                        Ok(waiter) => {
                                            if waiter.await {
                                                maho_agent::PermissionDecision::Allow
                                            } else {
                                                maho_agent::PermissionDecision::Deny
                                            }
                                        }
                                        Err(_) => maho_agent::PermissionDecision::Deny,
                                    }
                                })
                            });
                        let backend = maho_agent::omo::factory::create_agent_runtime(
                            Arc::new(agent_storage),
                            Some(permission_callback),
                            std::env::current_dir()
                                .unwrap_or_else(|_| std::path::PathBuf::from(".")),
                            true,
                        );
                        Box::pin(async move {
                            backend.run_prompt(&prompt).await.map_err(|e| e.to_string())
                        })
                            as Pin<Box<dyn Future<Output = Result<String, String>>>>
                    };
                    if let Err(e) = maho_core::routines::fire_event_routines_tracked(
                        event,
                        maho_core::routines::UserTier::Max,
                        &storage,
                        runner,
                        &registry,
                    )
                    .await
                    {
                        eprintln!("[maho_routines_fire_event] failed: {e}");
                    }
                });
            });
        },
        ()
    )
}

// ============================================================
// Routines Scheduler Tick FFI
// ============================================================

use std::sync::Mutex as StdMutex;

/// Global last-run timestamps for the routines scheduler.
/// Key: routine id (static str), Value: epoch seconds of last successful fire.
static ROUTINES_LAST_RUNS: OnceLock<StdMutex<HashMap<&'static str, i64>>> = OnceLock::new();

pub(crate) fn get_last_runs() -> &'static StdMutex<HashMap<&'static str, i64>> {
    ROUTINES_LAST_RUNS.get_or_init(|| StdMutex::new(HashMap::new()))
}

/// In-process occurrence idempotency map preventing duplicate fires for the same
/// (routine_id, scheduled_occurrence_minute) bucket under the 30s scheduler cadence.
/// Key: format!("{routine_id}:{minute_bucket}"), Value: epoch seconds of last fire.
static ROUTINES_OCCURRENCE_GUARD: OnceLock<StdMutex<HashMap<String, i64>>> = OnceLock::new();

static CUSTOM_OCCURRENCE_GUARD_PATH: StdMutex<Option<PathBuf>> = StdMutex::new(None);

pub fn set_custom_occurrence_guard_path(path: Option<PathBuf>) {
    *CUSTOM_OCCURRENCE_GUARD_PATH
        .lock()
        .unwrap_or_else(|e| e.into_inner()) = path;
}

pub(crate) fn occurrence_guard_file_path(db_path: &str) -> PathBuf {
    if let Some(custom) = CUSTOM_OCCURRENCE_GUARD_PATH
        .lock()
        .unwrap_or_else(|e| e.into_inner())
        .clone()
    {
        return custom;
    }
    std::path::Path::new(db_path).with_extension("routine_occurrences.json")
}

pub fn load_occurrence_guard_from_disk(path: &std::path::Path) -> HashMap<String, i64> {
    if !path.exists() {
        return HashMap::new();
    }
    match std::fs::read_to_string(path) {
        Ok(content) => serde_json::from_str(&content).unwrap_or_default(),
        Err(_) => HashMap::new(),
    }
}

pub fn save_occurrence_guard_to_disk(path: &std::path::Path, guard: &HashMap<String, i64>) {
    if let Some(parent) = path.parent() {
        let _ = std::fs::create_dir_all(parent);
    }
    if let Ok(json) = serde_json::to_string(guard) {
        let _ = std::fs::write(path, json);
    }
}

pub fn reload_occurrence_guard_from_disk(path: &std::path::Path) {
    let loaded = load_occurrence_guard_from_disk(path);
    let mut guard = get_occurrence_guard()
        .lock()
        .unwrap_or_else(|e| e.into_inner());
    *guard = loaded;
}

pub fn clear_occurrence_guard() {
    let mut guard = get_occurrence_guard()
        .lock()
        .unwrap_or_else(|e| e.into_inner());
    guard.clear();
}

pub fn get_occurrence_guard() -> &'static StdMutex<HashMap<String, i64>> {
    ROUTINES_OCCURRENCE_GUARD.get_or_init(|| StdMutex::new(HashMap::new()))
}

/// Tick the routines scheduler. Called every 30 seconds by the Chromium shell.
/// Internally checks tier, iterates the routines, fires any due to run (async)
/// with occurrence idempotency to prevent duplicate firing within the same minute.
/// Returns 0 on success, -1 on tier-not-max (no-op), -2 on internal error.
///
/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
pub unsafe extern "C" fn maho_routines_tick(core: *mut MahoCore, now_sec: i64) -> i32 {
    ffi_safe!(
        {
            if core.is_null() {
                return -2;
            }
            // Check tier — only Max users get scheduled routines
            let core_ref = &*core;
            match core_ref.get_account_tier() {
                Some(maho_types::account::UserTier::Max) => {}
                _ => return -1,
            };

            let db_path = match core_ref.sqlite_db_path() {
                Some(p) => p.to_string(),
                None => return -2,
            };

            let last_runs_mutex = get_last_runs();
            let mut last_runs = match last_runs_mutex.lock() {
                Ok(guard) => guard,
                Err(poisoned) => poisoned.into_inner(),
            };

            let occurrence_guard_mutex = get_occurrence_guard();
            let mut occurrence_guard = match occurrence_guard_mutex.lock() {
                Ok(guard) => guard,
                Err(poisoned) => poisoned.into_inner(),
            };

            let occurrence_path = occurrence_guard_file_path(&db_path);
            let disk_guard = load_occurrence_guard_from_disk(&occurrence_path);
            occurrence_guard.extend(disk_guard);

            // Prune occurrence guard entries older than 2 minutes (120s)
            occurrence_guard.retain(|_, fired_at| now_sec.saturating_sub(*fired_at) <= 120);

            let minute_bucket = if now_sec > 0 { (now_sec / 60) * 60 } else { 0 };

            let routines = maho_core::routines::list_routines();
            for routine in routines {
                let occurrence_key = format!("{}:{}", routine.id, minute_bucket);
                if occurrence_guard.contains_key(&occurrence_key) {
                    // Duplicate occurrence within the same minute bucket already fired -> skip
                    continue;
                }

                let schedule = match maho_core::routines::CronSchedule::parse(routine.cron) {
                    Some(s) => s,
                    None => {
                        eprintln!(
                            "[maho_routines_tick] failed to parse cron for {}: {}",
                            routine.id, routine.cron
                        );
                        continue;
                    }
                };

                let last_run = last_runs.get(routine.id).copied().unwrap_or(0);
                let next_fire = schedule.next_fire_after(last_run);

                if now_sec >= next_fire {
                    // Record in occurrence guard and last_runs before spawning
                    occurrence_guard.insert(occurrence_key, now_sec);
                    last_runs.insert(routine.id, now_sec);
                    save_occurrence_guard_to_disk(&occurrence_path, &occurrence_guard);

                    // Fire the routine asynchronously via the existing run_routine mechanism
                    let id_str = routine.id.to_string();
                    let db_path_clone = db_path.clone();
                    let registry = routine_run_registry(core);
                    let queued =
                        registry.begin(&id_str, maho_core::routines::RoutineRunSource::Scheduled);

                    // Spawn onto the shared, owned maho-core runtime. `maho_routines_tick`
                    // is called on a 30s cadence from the Chromium C++ browser thread which has
                    // NO ambient tokio runtime; a bare `tokio::spawn` there panics and the
                    // scheduled routine silently never fires. `Runtime::spawn` spawns onto
                    // its own runtime, so no panic can cross the FFI boundary.
                    // `run_routine_and_record` borrows a `!Send` SqliteStorage across an await,
                    // so drive it on a current-thread runtime pinned to a `spawn_blocking`
                    // thread (the future never migrates) — mirroring `maho_routines_run`.
                    maho_core::memory_manager::get_runtime().spawn_blocking(move || {
                        let rt = match tokio::runtime::Builder::new_current_thread()
                            .enable_all()
                            .build()
                        {
                            Ok(rt) => rt,
                            Err(e) => {
                                let message = format!("Failed to build runtime: {e}");
                                let _ = registry.mark_setup_failed(&queued.run_id, message.clone());
                                eprintln!("[routines scheduled] {message}");
                                return;
                            }
                        };
                        rt.block_on(async move {
                            let storage =
                                match maho_storage::sqlite::SqliteStorage::open(&db_path_clone) {
                                    Ok(s) => s,
                                    Err(e) => {
                                        let message = format!("Failed to open sqlite storage: {e}");
                                        let _ = registry
                                            .mark_setup_failed(&queued.run_id, message.clone());
                                        eprintln!("[routines scheduled] {message}");
                                        return;
                                    }
                                };
                            // Separate handle for resolving the routine and recording its
                            // result into the durable inbox; the agent moves its own handle.
                            let record_storage =
                                match maho_storage::sqlite::SqliteStorage::open(&db_path_clone) {
                                    Ok(s) => s,
                                    Err(e) => {
                                        let message = format!("Failed to open record storage: {e}");
                                        let _ = registry
                                            .mark_setup_failed(&queued.run_id, message.clone());
                                        eprintln!("[routines scheduled] {message}");
                                        return;
                                    }
                                };
                            let agent_storage = MutexAgentStorage(Arc::new(Mutex::new(storage)));
                            let approval_registry = FfiArc::clone(&registry);
                            let approval_run_id = queued.run_id.clone();
                            let permission_callback: maho_agent::PermissionCallback =
                                Box::new(move |request| {
                                    let registry = FfiArc::clone(&approval_registry);
                                    let run_id = approval_run_id.clone();
                                    Box::pin(async move {
                                        if request.sensitivity
                                            != maho_agent::ToolSensitivity::Sensitive
                                        {
                                            return maho_agent::PermissionDecision::Allow;
                                        }
                                        match registry.request_approval(
                                            &run_id,
                                            request.tool_name,
                                            request.sensitivity.as_str().to_string(),
                                        ) {
                                            Ok(waiter) => {
                                                if waiter.await {
                                                    maho_agent::PermissionDecision::Allow
                                                } else {
                                                    maho_agent::PermissionDecision::Deny
                                                }
                                            }
                                            Err(_) => maho_agent::PermissionDecision::Deny,
                                        }
                                    })
                                });
                            let backend = maho_agent::omo::factory::create_agent_runtime(
                                Arc::new(agent_storage),
                                Some(permission_callback),
                                std::env::current_dir()
                                    .unwrap_or_else(|_| std::path::PathBuf::from(".")),
                                true,
                            );

                            let result = maho_core::routines::run_routine_and_record_tracked(
                                &id_str,
                                maho_core::routines::UserTier::Max,
                                &record_storage,
                                maho_core::routines::RoutineRunSource::Scheduled,
                                |prompt| async move {
                                    backend.run_prompt(&prompt).await.map_err(|e| e.to_string())
                                },
                                &registry,
                                queued,
                            )
                            .await;

                            if let Err(e) = result {
                                eprintln!("[RoutineScheduler] routine {} failed: {}", id_str, e);
                            }
                        });
                    });
                }
            }

            0
        },
        -2
    )
}

// ============================================================
// Routine Inbox FFI (durable run-result history)
// ============================================================

/// Returns JSON array of recorded routine results (the routine inbox), newest
/// first. If `routine_id` is non-null, only that routine's history is returned;
/// pass null for all routines. `limit` caps the number of rows (0 is treated as
/// a sane default of 50). Caller must free with `maho_string_free`.
///
/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`. `routine_id`, if
/// non-null, must be a valid null-terminated C string.
pub unsafe extern "C" fn maho_routines_history(
    core: *mut MahoCore,
    routine_id: *const c_char,
    limit: u32,
) -> *mut c_char {
    ffi_safe!(
        {
            if core.is_null() {
                return ptr::null_mut();
            }
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let db_path = match (&*core).sqlite_db_path() {
                Some(p) => p.to_string(),
                None => return ptr::null_mut(),
            };
            let storage = match maho_storage::sqlite::SqliteStorage::open(&db_path) {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let capped = if limit == 0 { 50 } else { limit };
            let results = if routine_id.is_null() {
                storage.list_routine_results(capped)
            } else {
                let id_str = match CStr::from_ptr(routine_id).to_str() {
                    Ok(s) => s,
                    Err(_) => return ptr::null_mut(),
                };
                storage.list_routine_results_for(id_str, capped)
            };
            match results {
                Ok(records) => to_json_cstring(&records),
                Err(_) => ptr::null_mut(),
            }
        },
        ptr::null_mut()
    )
}

/// Returns the single most recent routine result for `routine_id` as JSON, or
/// null if the routine has never run. Caller must free with `maho_string_free`.
///
/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`. `routine_id`
/// must be a valid null-terminated C string.
pub unsafe extern "C" fn maho_routines_latest(
    core: *mut MahoCore,
    routine_id: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if core.is_null() || routine_id.is_null() {
                return ptr::null_mut();
            }
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let db_path = match (&*core).sqlite_db_path() {
                Some(p) => p.to_string(),
                None => return ptr::null_mut(),
            };
            let storage = match maho_storage::sqlite::SqliteStorage::open(&db_path) {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let id_str = match CStr::from_ptr(routine_id).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            match storage.latest_routine_result(id_str) {
                Ok(Some(record)) => to_json_cstring(&record),
                Ok(None) => ptr::null_mut(),
                Err(_) => ptr::null_mut(),
            }
        },
        ptr::null_mut()
    )
}

/// Sets the available browser capabilities for skill activation and filtering.
///
/// Queries discoverable skills matching the supplied capability list (plus any
/// user-handoff skills) and updates the internal capability filter on `MahoCore`.
/// Returns a JSON array of `SkillInfo` structs representing discoverable skills.
///
/// # Safety
/// `core` must be a valid non-null pointer to a `MahoCore`.
/// `capabilities_json` must be a valid null-terminated UTF-8 JSON string array.
/// Caller must free returned JSON string via `maho_string_free`.
pub unsafe extern "C" fn maho_skills_set_available_capabilities(
    core: *mut MahoCore,
    capabilities_json: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if core.is_null() || capabilities_json.is_null() {
                return ptr::null_mut();
            }
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core_ref = unsafe { &mut *core };
            let json_str = match unsafe { CStr::from_ptr(capabilities_json) }.to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let capabilities: Vec<String> = match serde_json::from_str(json_str) {
                Ok(caps) => caps,
                Err(_) => return ptr::null_mut(),
            };
            let cap_refs: Vec<&str> = capabilities.iter().map(|s| s.as_str()).collect();
            let discovered = core_ref.discover_skills(&cap_refs);
            let skill_infos: Vec<maho_types::events::core_update::SkillInfo> =
                discovered.into_iter().map(|s| s.into()).collect();
            core_ref.set_skills_available_capabilities(capabilities);
            to_json_cstring(&skill_infos)
        },
        ptr::null_mut()
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_occurrence_guard_restart_durability() {
        let temp_dir = tempfile::tempdir().unwrap();
        let state_path = temp_dir.path().join("routine_occurrences.json");
        set_custom_occurrence_guard_path(Some(state_path.clone()));

        clear_occurrence_guard();

        // 1. Initial fire: simulate occurrence guard recording an occurrence
        let routine_id = "daily_cleanup";
        let minute_bucket: i64 = 1771800000;
        let occurrence_key = format!("{}:{}", routine_id, minute_bucket);
        let now_sec = minute_bucket + 10;

        {
            let mut guard = get_occurrence_guard().lock().unwrap();
            guard.insert(occurrence_key.clone(), now_sec);
            save_occurrence_guard_to_disk(&state_path, &guard);
        }

        // Verify state file exists on disk and contains occurrence
        assert!(state_path.exists());
        let disk_guard = load_occurrence_guard_from_disk(&state_path);
        assert_eq!(disk_guard.get(&occurrence_key), Some(&now_sec));

        // 2. Simulate process restart: clear in-memory guard and reload from disk
        clear_occurrence_guard();
        {
            let guard = get_occurrence_guard().lock().unwrap();
            assert!(
                guard.is_empty(),
                "In-memory guard should be empty after clear"
            );
        }

        reload_occurrence_guard_from_disk(&state_path);

        // 3. Verify same occurrence is NOT re-fired
        {
            let guard = get_occurrence_guard().lock().unwrap();
            assert!(
                guard.contains_key(&occurrence_key),
                "Reloaded guard must contain previous occurrence"
            );
        }

        // 4. New occurrence in next minute bucket fires successfully
        let next_minute_bucket = minute_bucket + 60;
        let next_occurrence_key = format!("{}:{}", routine_id, next_minute_bucket);
        let next_now_sec = next_minute_bucket + 5;

        {
            let mut guard = get_occurrence_guard().lock().unwrap();
            assert!(
                !guard.contains_key(&next_occurrence_key),
                "New minute bucket must not be blocked"
            );
            guard.insert(next_occurrence_key.clone(), next_now_sec);
            save_occurrence_guard_to_disk(&state_path, &guard);
        }

        let updated_disk_guard = load_occurrence_guard_from_disk(&state_path);
        assert!(updated_disk_guard.contains_key(&next_occurrence_key));
        assert!(updated_disk_guard.contains_key(&occurrence_key));

        set_custom_occurrence_guard_path(None);
    }
}
