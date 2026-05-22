//! End-to-end integration test for the ACP FFI surface.
//!
//! Drives the C-ABI functions from Rust without any Chromium involvement.
//! Marked `#[ignore]` so CI doesn't require the `opencode` binary.

use std::ffi::{c_void, CString};
use std::sync::{Arc, Mutex};
use std::time::Duration;

use maho_ffi::acp::*;

struct EventSink {
    blobs: Arc<Mutex<Vec<Vec<u8>>>>,
}

impl EventSink {
    fn new() -> Self {
        Self {
            blobs: Arc::new(Mutex::new(Vec::new())),
        }
    }

    fn blobs(&self) -> Arc<Mutex<Vec<Vec<u8>>>> {
        self.blobs.clone()
    }
}

extern "C" fn event_callback(user_data: *mut c_void, data: *const u8, len: usize) {
    // SAFETY: user_data is a raw pointer to Arc<Mutex<Vec<Vec<u8>>>> that
    // we created below and remains valid for the test duration.
    let blobs = unsafe { &*(user_data as *const Arc<Mutex<Vec<Vec<u8>>>>) };
    let slice = unsafe { std::slice::from_raw_parts(data, len) };
    blobs.lock().unwrap().push(slice.to_vec());
}

fn wait_for_event<F>(
    blobs: &Arc<Mutex<Vec<Vec<u8>>>>,
    from_index: usize,
    timeout: Duration,
    mut predicate: F,
) -> Option<(usize, FfiEvent)>
where
    F: FnMut(&FfiEvent) -> bool,
{
    let deadline = std::time::Instant::now() + timeout;
    loop {
        {
            let guard = blobs.lock().unwrap();
            for i in from_index..guard.len() {
                if let Ok(event) = decode_ffi_event(&guard[i]) {
                    if predicate(&event) {
                        return Some((i + 1, event));
                    }
                }
            }
        }
        if std::time::Instant::now() >= deadline {
            return None;
        }
        std::thread::sleep(Duration::from_millis(50));
    }
}

fn opencode_available() -> bool {
    std::process::Command::new("opencode")
        .arg("--version")
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .status()
        .map(|s| s.success())
        .unwrap_or(false)
}

#[test]
#[ignore]
fn acp_ffi_end_to_end() {
    if !opencode_available() {
        eprintln!("SKIP: opencode binary not found in PATH");
        return;
    }

    let sink = EventSink::new();
    let blobs = sink.blobs();
    let blobs_ptr = &blobs as *const Arc<Mutex<Vec<Vec<u8>>>> as *mut c_void;

    let handle = maho_acp_create();
    assert!(!handle.is_null(), "maho_acp_create returned null");

    // SAFETY: handle is valid, callback and user_data are valid for test duration.
    unsafe {
        assert_eq!(
            maho_acp_set_event_callback(handle, event_callback, blobs_ptr),
            MAHO_ACP_OK
        );
    }

    let cmd = CString::new("opencode").unwrap();
    // SAFETY: handle and cmd are valid.
    let rc = unsafe { maho_acp_start(handle, cmd.as_ptr()) };
    assert_eq!(rc, MAHO_ACP_OK, "maho_acp_start failed with {}", rc);

    // SAFETY: handle is valid.
    let rc = unsafe { maho_acp_initialize(handle) };
    assert_eq!(rc, MAHO_ACP_OK);

    let timeout = Duration::from_secs(30);
    let (next_idx, _init_event) = wait_for_event(&blobs, 0, timeout, |e| {
        matches!(e, FfiEvent::InitializeOk { .. })
    })
    .expect("timed out waiting for InitializeOk");

    eprintln!("InitializeOk received");

    // SAFETY: handle is valid.
    let rc = unsafe { maho_acp_create_session(handle) };
    assert_eq!(rc, MAHO_ACP_OK);

    let (next_idx, session_event) = wait_for_event(&blobs, next_idx, timeout, |e| {
        matches!(e, FfiEvent::SessionCreated { .. })
    })
    .expect("timed out waiting for SessionCreated");

    let session_id = match &session_event {
        FfiEvent::SessionCreated { session_id, .. } => session_id.clone(),
        _ => unreachable!(),
    };
    eprintln!("SessionCreated: {}", session_id);

    let sid_c = CString::new(session_id.as_str()).unwrap();
    let prompt = CString::new("Reply with the single word PROBE_OK and nothing else.").unwrap();
    // SAFETY: handle, sid_c, prompt are valid.
    let rc = unsafe { maho_acp_submit_prompt(handle, sid_c.as_ptr(), prompt.as_ptr()) };
    assert_eq!(rc, MAHO_ACP_OK);

    let prompt_timeout = Duration::from_secs(120);
    let mut accumulated_text = String::new();
    let mut got_message_chunk = false;
    let scan_idx = next_idx;

    let (_, complete_event) = wait_for_event(&blobs, scan_idx, prompt_timeout, |e| {
        match e {
            FfiEvent::Update(FfiAcpUpdate::AgentMessageChunk { text, .. }) => {
                accumulated_text.push_str(text);
                got_message_chunk = true;
                false
            }
            FfiEvent::PromptComplete { .. } => true,
            FfiEvent::PromptErr { .. } => true,
            _ => false,
        }
    })
    .expect("timed out waiting for PromptComplete");

    // Drain any remaining message chunks we haven't seen
    {
        let guard = blobs.lock().unwrap();
        for i in scan_idx..guard.len() {
            if let Ok(FfiEvent::Update(FfiAcpUpdate::AgentMessageChunk { text, .. })) =
                decode_ffi_event(&guard[i])
            {
                if !accumulated_text.contains(&text) {
                    accumulated_text.push_str(&text);
                    got_message_chunk = true;
                }
            }
        }
    }

    eprintln!("Accumulated text: {:?}", accumulated_text);
    assert!(got_message_chunk, "expected at least one AgentMessageChunk");
    assert!(
        accumulated_text.contains("PROBE_OK"),
        "expected PROBE_OK in accumulated text, got: {}",
        accumulated_text
    );

    match &complete_event {
        FfiEvent::PromptComplete { stop_reason, .. } => {
            assert_eq!(stop_reason, "end_turn", "unexpected stop_reason: {}", stop_reason);
        }
        FfiEvent::PromptErr { message, .. } => {
            panic!("prompt failed: {}", message);
        }
        _ => unreachable!(),
    }

    // SAFETY: handle is valid.
    let rc = unsafe { maho_acp_shutdown(handle) };
    assert_eq!(rc, MAHO_ACP_OK, "shutdown failed with {}", rc);

    // SAFETY: handle is valid, becomes invalid after this call.
    unsafe { maho_acp_destroy(handle) };

    eprintln!("ACP FFI end-to-end test PASSED");
}
