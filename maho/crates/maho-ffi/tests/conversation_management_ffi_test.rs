use std::ffi::{CStr, CString};

fn take(ptr: *mut std::os::raw::c_char) -> Option<String> {
    if ptr.is_null() {
        return None;
    }
    let value = unsafe { CStr::from_ptr(ptr) }
        .to_string_lossy()
        .into_owned();
    unsafe { maho_ffi::maho_string_free(ptr) };
    Some(value)
}

#[test]
fn allocated_native_session_blocks_delete_until_freed_and_cannot_recreate_parent() {
    let key = CString::new("conversation-native-session-key").unwrap();
    assert!(unsafe { maho_ffi::maho_storage_set_sqlcipher_key(key.as_ptr()) });
    let dir = tempfile::tempdir().unwrap();
    let path = CString::new(dir.path().join("native-session.sqlite").to_str().unwrap()).unwrap();
    let core = unsafe { maho_ffi::maho_core_new_with_storage(path.as_ptr()) };
    let id = CString::new("c").unwrap();
    assert!(unsafe {
        maho_ffi::maho_core_create_conversation(
            core,
            id.as_ptr(),
            std::ptr::null(),
            std::ptr::null(),
            std::ptr::null(),
        )
    });
    let session = unsafe {
        maho_ffi::maho_agent_create_session(
            core,
            id.as_ptr(),
            std::ptr::null(),
            true,
            None,
            std::ptr::null_mut(),
            None,
            std::ptr::null_mut(),
            None,
            std::ptr::null_mut(),
            std::ptr::null(),
        )
    };
    assert!(!session.is_null());

    assert!(!unsafe { maho_ffi::maho_core_delete_conversation(core, id.as_ptr()) });
    let query = CString::new(r#"{"state":"all","limit":10}"#).unwrap();
    let live_rows =
        take(unsafe { maho_ffi::maho_core_list_conversations_v2(core, query.as_ptr()) }).unwrap();
    assert_eq!(
        serde_json::from_str::<serde_json::Value>(&live_rows)
            .unwrap()
            .as_array()
            .unwrap()
            .len(),
        1
    );

    let delete_request = CString::new(r#"{"op":"delete","ids":["c"]}"#).unwrap();
    let rejection = take(unsafe {
        maho_ffi::maho_core_apply_conversation_bulk_operation(core, delete_request.as_ptr())
    })
    .unwrap();
    assert_eq!(
        serde_json::from_str::<serde_json::Value>(&rejection).unwrap(),
        serde_json::json!({"error":"active_sessions","ids":["c"]})
    );

    unsafe { maho_ffi::maho_agent_session_free(session) };
    let deleted = take(unsafe {
        maho_ffi::maho_core_apply_conversation_bulk_operation(core, delete_request.as_ptr())
    })
    .unwrap();
    assert_eq!(
        serde_json::from_str::<serde_json::Value>(&deleted).unwrap()["affectedIds"],
        serde_json::json!(["c"])
    );
    let rows =
        take(unsafe { maho_ffi::maho_core_list_conversations_v2(core, query.as_ptr()) }).unwrap();
    assert_eq!(
        serde_json::from_str::<serde_json::Value>(&rows).unwrap(),
        serde_json::json!([])
    );
    unsafe { maho_ffi::maho_core_free(core) };
}

#[test]
fn archive_bulk_policy_project_and_malformed_ffi_contract() {
    let key = CString::new("conversation-management-ffi-key").unwrap();
    assert!(unsafe { maho_ffi::maho_storage_set_sqlcipher_key(key.as_ptr()) });
    let dir = tempfile::tempdir().unwrap();
    let path = CString::new(dir.path().join("conversation-ffi.sqlite").to_str().unwrap()).unwrap();
    let core = unsafe { maho_ffi::maho_core_new_with_storage(path.as_ptr()) };
    let id = CString::new("c").unwrap();
    assert!(unsafe {
        maho_ffi::maho_core_create_conversation(
            core,
            id.as_ptr(),
            std::ptr::null(),
            std::ptr::null(),
            std::ptr::null(),
        )
    });
    assert!(unsafe { maho_ffi::maho_core_archive_conversation(core, id.as_ptr()) });
    let query = CString::new(r#"{"state":"archived","limit":10}"#).unwrap();
    assert_eq!(
        serde_json::from_str::<serde_json::Value>(
            &take(unsafe { maho_ffi::maho_core_list_conversations_v2(core, query.as_ptr()) })
                .unwrap()
        )
        .unwrap()
        .as_array()
        .unwrap()
        .len(),
        1
    );
    let bad = CString::new("{").unwrap();
    assert!(take(unsafe {
        maho_ffi::maho_core_apply_conversation_bulk_operation(core, bad.as_ptr())
    })
    .is_none());
    assert!(!unsafe { maho_ffi::maho_core_set_conversation_auto_archive_policy(core, 0) });
    assert!(!unsafe { maho_ffi::maho_core_set_conversation_auto_archive_policy(core, -2) });
    assert!(unsafe { maho_ffi::maho_core_set_conversation_auto_archive_policy(core, -1) });
    assert_eq!(
        unsafe { maho_ffi::maho_core_auto_archive_conversations(core, 1_800_000_000) },
        -2
    );

    let project_name = CString::new("Project").unwrap();
    let project_json = take(unsafe {
        maho_ffi::maho_core_create_conversation_project(core, project_name.as_ptr())
    })
    .unwrap();
    let project: serde_json::Value = serde_json::from_str(&project_json).unwrap();
    let move_request =
        CString::new(serde_json::json!({"ids":["c"],"projectId":project["id"]}).to_string())
            .unwrap();
    assert!(take(unsafe {
        maho_ffi::maho_core_move_conversations_to_project(core, move_request.as_ptr())
    })
    .is_some());
    assert_eq!(
        serde_json::from_str::<serde_json::Value>(
            &take(unsafe { maho_ffi::maho_core_list_conversation_projects(core) }).unwrap()
        )
        .unwrap()
        .as_array()
        .unwrap()
        .len(),
        1
    );

    unsafe { (&*core).mark_conversation_session_active("c") };
    let delete_request = CString::new(r#"{"op":"delete","ids":["c"]}"#).unwrap();
    let rejection = take(unsafe {
        maho_ffi::maho_core_apply_conversation_bulk_operation(core, delete_request.as_ptr())
    })
    .unwrap();
    assert_eq!(
        serde_json::from_str::<serde_json::Value>(&rejection).unwrap(),
        serde_json::json!({"error":"active_sessions","ids":["c"]})
    );
    unsafe { (&*core).mark_conversation_session_inactive("c") };
    unsafe { maho_ffi::maho_core_free(core) };
}
