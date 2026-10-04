//! Exercise the real C boundary and safe session with a deterministic native
//! decoder double. Run under Miri to check cancellation's overlapping borrows.
use core::ffi::{c_char, c_int};
use maho_stt::sys::*;
use maho_stt_ffi::*;
use std::cell::Cell;

thread_local! {
    static CANCEL_DURING_DECODE: Cell<*mut MahoSttSession> = const { Cell::new(core::ptr::null_mut()) };
}

#[unsafe(no_mangle)]
extern "C" fn whisper_context_default_params() -> WhisperContextParams {
    // SAFETY: every field accepts zero, including the None enum discriminant.
    unsafe { core::mem::zeroed() }
}
#[unsafe(no_mangle)]
extern "C" fn whisper_init_from_file_with_params(
    _: *const c_char,
    _: WhisperContextParams,
) -> *mut WhisperContext {
    Box::into_raw(Box::new(0_u8)).cast()
}
#[unsafe(no_mangle)]
extern "C" fn whisper_full_default_params(_: WhisperSamplingStrategy) -> WhisperFullParams {
    // SAFETY: zero is valid for every field (Greedy, null callbacks and pointers).
    unsafe { core::mem::zeroed() }
}
#[unsafe(no_mangle)]
extern "C" fn whisper_full(
    _: *mut WhisperContext,
    params: WhisperFullParams,
    _: *const f32,
    _: c_int,
) -> c_int {
    CANCEL_DURING_DECODE.with(|slot| {
        let handle = slot.replace(core::ptr::null_mut());
        if !handle.is_null() {
            // SAFETY: the test owns this live handle until transcription returns.
            unsafe { maho_stt_session_cancel(handle) };
            // SAFETY: the real wrapper supplies a live cancellation callback.
            assert!(unsafe { params.abort_callback.unwrap()(params.abort_callback_user_data) });
        }
    });
    0
}
#[unsafe(no_mangle)]
extern "C" fn whisper_full_n_segments(_: *mut WhisperContext) -> c_int {
    1
}
#[unsafe(no_mangle)]
extern "C" fn whisper_full_get_segment_text(_: *mut WhisperContext, _: c_int) -> *const c_char {
    c"hello".as_ptr()
}
#[unsafe(no_mangle)]
unsafe extern "C" fn whisper_free(ctx: *mut WhisperContext) {
    // SAFETY: paired with the allocation in the decoder double above.
    drop(unsafe { Box::from_raw(ctx.cast::<u8>()) });
}

#[test]
fn cancelled_handle_can_transcribe_the_next_utterance() {
    // SAFETY: each synchronous call completes before reuse/free; buffers live throughout.
    unsafe {
        let handle = maho_stt_session_create(c"test-model".as_ptr());
        assert!(!handle.is_null());
        CANCEL_DURING_DECODE.with(|slot| slot.set(handle));
        let mut out = [0; 32];
        assert_eq!(
            maho_stt_session_transcribe(handle, [0.0].as_ptr(), 1, out.as_mut_ptr(), out.len()),
            MAHO_STT_ERR_CANCELLED
        );
        let next =
            maho_stt_session_transcribe(handle, [0.0].as_ptr(), 1, out.as_mut_ptr(), out.len());
        maho_stt_session_free(handle);
        assert_eq!(next, 5, "cancelled model must remain reusable");
    }
}

#[test]
fn cancellation_during_decode_is_alias_safe() {
    // SAFETY: buffers and handle remain live for all calls, and free is last.
    unsafe {
        let handle = maho_stt_session_create(c"test-model".as_ptr());
        assert!(!handle.is_null());
        CANCEL_DURING_DECODE.with(|slot| slot.set(handle));
        let mut out = [0; 32];
        assert_eq!(
            maho_stt_session_transcribe(handle, [0.0].as_ptr(), 1, out.as_mut_ptr(), out.len()),
            MAHO_STT_ERR_CANCELLED
        );
        maho_stt_session_free(handle);
    }
}
