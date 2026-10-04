//! `maho-stt-ffi` — C ABI over [`maho_stt`] for the Chromium browser process.
//!
//! Standalone static lib (its own `[workspace]`, NOT a member of
//! `maho/Cargo.toml`, NOT a dependency of `maho-ffi`). Every export is wrapped
//! in `catch_unwind`: a panic must NEVER cross the C frame (UB Category 14).
//! The header `maho_stt_ffi.h` is cbindgen-generated (see `cbindgen.toml`) and
//! consumed by `maho_ai_page_handler.cc`.

#![forbid(unsafe_op_in_unsafe_fn)]

use core::ffi::{CStr, c_char, c_int};
use std::panic::{AssertUnwindSafe, catch_unwind};

use maho_stt::{CancelToken, WhisperSession};
use std::sync::Mutex;

/// Transcription succeeded; the non-negative return is the byte length written.
pub const MAHO_STT_OK: c_int = 0;
/// A required pointer argument was null.
pub const MAHO_STT_ERR_NULL_ARG: c_int = -1;
/// A panic was caught at the FFI boundary.
pub const MAHO_STT_ERR_PANIC: c_int = -2;
/// whisper transcription failed.
pub const MAHO_STT_ERR_TRANSCRIBE: c_int = -3;
/// The caller-provided output buffer was too small for the transcript + NUL.
pub const MAHO_STT_ERR_BUFFER_TOO_SMALL: c_int = -4;
/// Transcription was cancelled via `maho_stt_session_cancel`.
pub const MAHO_STT_ERR_CANCELLED: c_int = -5;
/// The model path was not valid UTF-8.
pub const MAHO_STT_ERR_BAD_PATH: c_int = -6;

/// Default whisper worker thread count (0 asks the engine to auto-clamp).
const DEFAULT_THREADS: u32 = 0;

/// Opaque session handle returned to C. Owns one [`WhisperSession`].
pub struct MahoSttSession {
    inner: Mutex<WhisperSession>,
    cancel: CancelToken,
}

/// Create a session by loading a GGML model from a NUL-terminated `model_path`.
///
/// Returns null on any failure (null/invalid path, model load error, panic).
/// The returned pointer must be released with [`maho_stt_session_free`].
///
/// # Safety
/// `model_path` must be null or point to a valid NUL-terminated C string.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn maho_stt_session_create(model_path: *const c_char) -> *mut MahoSttSession {
    catch_unwind(|| {
        if model_path.is_null() {
            return core::ptr::null_mut();
        }
        // SAFETY: [Category 8 — FFI boundary] The caller contracts that
        // `model_path` is a valid NUL-terminated C string for this call.
        let c_str = unsafe { CStr::from_ptr(model_path) };
        let Ok(path) = c_str.to_str() else {
            return core::ptr::null_mut();
        };
        match WhisperSession::load(path, DEFAULT_THREADS) {
            Ok(inner) => {
                let cancel = inner.cancel_token();
                Box::into_raw(Box::new(MahoSttSession {
                    inner: Mutex::new(inner),
                    cancel,
                }))
            }
            Err(_) => core::ptr::null_mut(),
        }
    })
    .unwrap_or(core::ptr::null_mut())
}

/// Transcribe `n_samples` of 16 kHz mono f32 PCM at `pcm` into the caller-owned
/// buffer `out` (capacity `out_cap` bytes, NUL-terminated on success).
///
/// Returns the transcript byte length (excluding NUL) on success, or a negative
/// `MAHO_STT_ERR_*` code on failure.
///
/// # Safety
/// `handle` must be null or a live pointer from [`maho_stt_session_create`].
/// `pcm` must address `n_samples` contiguous f32 values (or be null when
/// `n_samples == 0`). `out` must be null or point to a writable buffer of at
/// least `out_cap` bytes.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn maho_stt_session_transcribe(
    handle: *mut MahoSttSession,
    pcm: *const f32,
    n_samples: usize,
    out: *mut c_char,
    out_cap: usize,
) -> c_int {
    catch_unwind(AssertUnwindSafe(|| {
        // SAFETY: [Category 8 — FFI boundary] `handle` is null or a live pointer
        // from `maho_stt_session_create`; `as_ref` yields None for null.
        // The mutex exclusively borrows the decoder, not the enclosing handle.
        let Some(session) = (unsafe { handle.as_ref() }) else {
            return MAHO_STT_ERR_NULL_ARG;
        };
        if pcm.is_null() && n_samples != 0 {
            return MAHO_STT_ERR_NULL_ARG;
        }
        let samples: &[f32] = if n_samples == 0 {
            &[]
        } else {
            // SAFETY: [Category 10 — out-of-bounds] `pcm` is non-null (guarded
            // above) and addresses `n_samples` contiguous f32 per the C caller
            // contract; the slice is only read during the synchronous call.
            unsafe { core::slice::from_raw_parts(pcm, n_samples) }
        };
        let Ok(mut inner) = session.inner.lock() else {
            return MAHO_STT_ERR_PANIC;
        };
        let result = inner.transcribe(samples);
        // End this utterance's cancellation scope while still owning the
        // decoder lock. Never clear at startup: that could lose a new cancel.
        session.cancel.reset();
        match result {
            Ok(text) => write_cstr(&text, out, out_cap),
            Err(maho_stt::SttError::Cancelled) => MAHO_STT_ERR_CANCELLED,
            Err(_) => MAHO_STT_ERR_TRANSCRIBE,
        }
    }))
    .unwrap_or(MAHO_STT_ERR_PANIC)
}

/// Request cancellation of an in-flight transcription. Safe on a null handle.
/// Cancellation ends with that transcription; the loaded model can be reused.
///
/// # Safety
/// `handle` must be null or a live pointer from [`maho_stt_session_create`].
#[unsafe(no_mangle)]
pub unsafe extern "C" fn maho_stt_session_cancel(handle: *mut MahoSttSession) {
    let _ = catch_unwind(AssertUnwindSafe(|| {
        // SAFETY: [Category 8 — FFI boundary] `handle` is null or a live pointer
        // from `maho_stt_session_create`; `as_ref` yields None for null.
        if let Some(session) = unsafe { handle.as_ref() } {
            session.cancel.cancel();
        }
    }));
}

/// Free a session created by [`maho_stt_session_create`]. Safe on a null handle.
///
/// # Safety
/// `handle` must be null or a live pointer from [`maho_stt_session_create`]
/// that has not already been freed. No other call using this handle may be
/// active or start concurrently with free.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn maho_stt_session_free(handle: *mut MahoSttSession) {
    let _ = catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() {
            return;
        }
        // SAFETY: [Category 12 — double free] `handle` was produced by
        // `Box::into_raw` in `create` and is reclaimed exactly once here.
        drop(unsafe { Box::from_raw(handle) });
    }));
}

/// Copy `text` (+ NUL) into `out` of capacity `out_cap`. Returns the byte length
/// written (excluding NUL), or a negative error code.
fn write_cstr(text: &str, out: *mut c_char, out_cap: usize) -> c_int {
    let bytes = text.as_bytes();
    let needed = bytes.len().saturating_add(1);
    if out.is_null() || out_cap < needed {
        return MAHO_STT_ERR_BUFFER_TOO_SMALL;
    }
    // SAFETY: [Category 10 — out-of-bounds] `out` has capacity `out_cap`, which
    // is `>= bytes.len() + 1`; we write exactly `bytes.len()` bytes plus a NUL.
    unsafe {
        core::ptr::copy_nonoverlapping(bytes.as_ptr().cast::<c_char>(), out, bytes.len());
        *out.add(bytes.len()) = 0;
    }
    c_int::try_from(bytes.len()).unwrap_or(c_int::MAX)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn read_c_buffer(buf: &[c_char]) -> String {
        let bytes: Vec<u8> = buf
            .iter()
            .take_while(|&&b| b != 0)
            .map(|&b| b as u8)
            .collect();
        String::from_utf8(bytes).unwrap_or_default()
    }

    #[test]
    fn write_cstr_writes_text_and_nul() {
        let mut buf = [1_i8; 8];
        let n = write_cstr("hey", buf.as_mut_ptr(), buf.len());
        assert_eq!(n, 3);
        assert_eq!(buf[3], 0);
        assert_eq!(read_c_buffer(&buf), "hey");
    }

    #[test]
    fn write_cstr_rejects_undersized_buffer() {
        let mut buf = [0_i8; 3];
        // "test" needs 5 bytes (4 + NUL); buffer holds 3.
        assert_eq!(
            write_cstr("test", buf.as_mut_ptr(), buf.len()),
            MAHO_STT_ERR_BUFFER_TOO_SMALL
        );
    }

    #[test]
    fn write_cstr_rejects_null_out() {
        assert_eq!(
            write_cstr("x", core::ptr::null_mut(), 64),
            MAHO_STT_ERR_BUFFER_TOO_SMALL
        );
    }

    #[test]
    fn write_cstr_exact_fit() {
        let mut buf = [1_i8; 2];
        let n = write_cstr("z", buf.as_mut_ptr(), buf.len());
        assert_eq!(n, 1);
        assert_eq!(buf[1], 0);
    }

    #[test]
    fn cancel_null_handle_is_safe() {
        // Exercises the null guard + catch_unwind path without touching whisper.
        // SAFETY: [Category 8 — FFI boundary] a null handle is an explicitly
        // supported argument; the export guards it before any dereference.
        unsafe { maho_stt_session_cancel(core::ptr::null_mut()) };
    }
}
