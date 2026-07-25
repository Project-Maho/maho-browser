// maho/crates/maho-ffi/tests/null_safety.rs
//! Smoke tests proving 14 stub FFI functions don't segfault on NULL input.
//! Runs as `cargo test --test null_safety`.

use std::ffi::CString;
use std::ptr;

#[test]
fn create_conversation_with_null_ptr_does_not_crash() {
    unsafe {
        let _result = maho_ffi::maho_core_create_conversation(
            ptr::null_mut(),
            ptr::null(),
            ptr::null(),
            ptr::null(),
            ptr::null(),
        );
        // Stub returns true; just verify no segfault.
    }
}

#[test]
fn list_conversations_with_null_ptr_returns_some_string() {
    unsafe {
        let result = maho_ffi::maho_core_list_conversations(ptr::null_mut(), 100);
        // Stub returns "[]" cstring. Free it.
        if !result.is_null() {
            maho_ffi::maho_string_free(result);
        }
        // No segfault = pass.
    }
}

#[test]
fn get_conversation_messages_with_null_session_id() {
    unsafe {
        let result = maho_ffi::maho_core_get_conversation_messages(ptr::null_mut(), ptr::null());
        if !result.is_null() {
            maho_ffi::maho_string_free(result);
        }
    }
}

#[test]
fn save_conversation_message_with_all_null() {
    unsafe {
        let _ = maho_ffi::maho_core_save_conversation_message(
            ptr::null_mut(),
            ptr::null(),
            ptr::null(),
            ptr::null(),
            ptr::null(),
        );
    }
}

#[test]
fn delete_conversation_with_null_session_id() {
    unsafe {
        let _ = maho_ffi::maho_core_delete_conversation(ptr::null_mut(), ptr::null());
    }
}

#[test]
fn rename_conversation_with_null_strings() {
    unsafe {
        let _ = maho_ffi::maho_core_rename_conversation(ptr::null_mut(), ptr::null(), ptr::null());
    }
}

#[test]
fn byok_set_key_with_null_strings() {
    unsafe {
        let _ = maho_ffi::maho_core_byok_set_key(ptr::null_mut(), ptr::null(), ptr::null());
    }
}

#[test]
fn byok_get_key_with_null_provider() {
    unsafe {
        let result = maho_ffi::maho_core_byok_get_key(ptr::null_mut(), ptr::null());
        if !result.is_null() {
            maho_ffi::maho_string_free(result);
        }
    }
}

#[test]
fn byok_delete_key_with_null() {
    unsafe {
        let _ = maho_ffi::maho_core_byok_delete_key(ptr::null_mut(), ptr::null());
    }
}

#[test]
fn byok_get_providers_with_null_ptr() {
    unsafe {
        let result = maho_ffi::maho_core_byok_get_providers(ptr::null_mut());
        if !result.is_null() {
            maho_ffi::maho_string_free(result);
        }
    }
}

#[test]
fn byok_validate_key_with_null() {
    unsafe {
        let _ = maho_ffi::maho_core_byok_validate_key(ptr::null_mut(), ptr::null(), ptr::null());
    }
}

#[test]
fn chat_send_image_with_null_session() {
    unsafe {
        let mime = CString::new("image/png").unwrap();
        let _ = maho_ffi::maho_chat_send_image(ptr::null_mut(), mime.as_ptr(), ptr::null(), 0);
    }
}

#[test]
fn chat_send_image_with_null_mime() {
    unsafe {
        let data = vec![0u8; 4];
        let _ =
            maho_ffi::maho_chat_send_image(ptr::null_mut(), ptr::null(), data.as_ptr(), data.len());
    }
}

#[test]
fn chat_send_image_with_null_data_but_nonzero_len() {
    unsafe {
        let mime = CString::new("image/png").unwrap();
        // Caller-side bug to send NULL data with nonzero len. Must not crash.
        let _ = maho_ffi::maho_chat_send_image(ptr::null_mut(), mime.as_ptr(), ptr::null(), 100);
    }
}

#[test]
fn chat_send_text_with_image_full_null() {
    unsafe {
        let _ = maho_ffi::maho_chat_send_text_with_image(
            ptr::null_mut(),
            ptr::null(),
            ptr::null(),
            ptr::null(),
            0,
        );
    }
}

#[test]
fn vault_backend_handles_accept_null_without_crashing() {
    unsafe {
        assert!(maho_ffi::vault_backend::maho_vault_backend_session_new(ptr::null_mut()).is_null());
        maho_ffi::vault_backend::maho_vault_backend_session_close(ptr::null_mut());
        maho_ffi::vault_backend::maho_vault_backend_session_free(ptr::null_mut());
        assert!(maho_ffi::vault_backend::maho_vault_backend_session_execute(
            ptr::null_mut(),
            ptr::null(),
        )
        .is_null());
        assert!(
            maho_ffi::vault_backend::maho_vault_backend_session_execute_credentials(
                ptr::null_mut(),
                ptr::null(),
            )
            .is_null()
        );
        assert_eq!(
            maho_ffi::vault_backend::maho_vault_backend_result_status(ptr::null()),
            maho_ffi::vault_backend::MahoVaultBackendResultStatus::Failed,
        );
        assert!(
            maho_ffi::vault_backend::maho_vault_backend_result_consume(ptr::null_mut()).is_null()
        );
        maho_ffi::vault_backend::maho_vault_backend_result_free(ptr::null_mut());
        assert!(maho_ffi::vault_backend::maho_vault_backend_buffer_data(ptr::null()).is_null());
        assert_eq!(
            maho_ffi::vault_backend::maho_vault_backend_buffer_len(ptr::null()),
            0
        );
        maho_ffi::vault_backend::maho_vault_backend_buffer_free(ptr::null_mut());
    }
}
