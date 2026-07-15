//! Safe `WhisperSession` over the raw whisper.cpp FFI in [`crate::sys`].
//!
//! Push-to-talk model: accumulate PCM in the caller, then `transcribe` a
//! `&[f32]` 16 kHz mono buffer to text. Cancellation is cooperative via an
//! `AtomicBool` wired into whisper's `abort_callback`.

use core::ffi::{c_int, c_void};
use core::ptr::NonNull;
use core::sync::atomic::{AtomicBool, Ordering};
use std::ffi::{CStr, CString};
use std::sync::Arc;

use crate::error::SttError;
use crate::sys;

/// A cheaply-cloned handle used to cancel an in-flight transcription.
#[derive(Clone, Debug, Default)]
pub struct CancelToken {
    flag: Arc<AtomicBool>,
}

impl CancelToken {
    /// Create a fresh, un-cancelled token.
    #[must_use]
    pub fn new() -> Self {
        Self::default()
    }

    /// Request cancellation. The running `transcribe` aborts at the next
    /// whisper compute checkpoint and returns [`SttError::Cancelled`].
    pub fn cancel(&self) {
        self.flag.store(true, Ordering::SeqCst);
    }

    /// Whether cancellation has been requested.
    #[must_use]
    pub fn is_cancelled(&self) -> bool {
        self.flag.load(Ordering::SeqCst)
    }

    /// Clear the cancellation flag so the token can be reused.
    pub fn reset(&self) {
        self.flag.store(false, Ordering::SeqCst);
    }
}

/// A loaded whisper model plus its cancellation flag.
///
/// One session owns one `whisper_context`. `transcribe` takes `&mut self`, so a
/// context is never used by two threads at once — the invariant whisper.cpp
/// documents for thread safety.
pub struct WhisperSession {
    ctx: NonNull<sys::WhisperContext>,
    cancel: CancelToken,
    n_threads: c_int,
}

// SAFETY: [Category 9 — Send/Sync] The only non-Send field is the raw
// `whisper_context` pointer. Every method that touches it takes `&mut self`, so
// the context is never accessed concurrently; moving the session to another
// thread (e.g. a ThreadPool worker) transfers exclusive ownership. `CancelToken`
// is atomic. We deliberately do NOT implement Sync (shared `&self` context
// access would violate whisper's single-threaded-per-context contract).
unsafe impl Send for WhisperSession {}

impl WhisperSession {
    /// Load a GGML whisper model from `model_path`.
    ///
    /// # Errors
    /// - [`SttError::ModelPathNul`] if the path has an interior NUL byte.
    /// - [`SttError::ModelLoadFailed`] if whisper returns a null context.
    pub fn load(model_path: &str, n_threads: u32) -> Result<Self, SttError> {
        let c_path = CString::new(model_path).map_err(|_| SttError::ModelPathNul)?;
        // SAFETY: [Category 8 — FFI boundary] The default-params C constructor
        // returns a fully-initialized value matching the layout locked in sys.rs.
        let params = unsafe { sys::whisper_context_default_params() };
        // SAFETY: [Category 8 — FFI boundary] `c_path` is a valid NUL-terminated
        // C string that outlives the call; `params` is whisper-owned. whisper
        // returns null on failure, which `NonNull::new` rejects below.
        let raw = unsafe { sys::whisper_init_from_file_with_params(c_path.as_ptr(), params) };
        let ctx = NonNull::new(raw).ok_or_else(|| SttError::ModelLoadFailed {
            path: model_path.to_owned(),
        })?;
        Ok(Self {
            ctx,
            cancel: CancelToken::new(),
            n_threads: clamp_threads(n_threads),
        })
    }

    /// A clone of this session's cancellation token.
    #[must_use]
    pub fn cancel_token(&self) -> CancelToken {
        self.cancel.clone()
    }

    /// Request cancellation of an in-flight `transcribe`.
    pub fn cancel(&self) {
        self.cancel.cancel();
    }

    /// Transcribe a 16 kHz mono `f32` PCM buffer to text.
    ///
    /// # Errors
    /// - [`SttError::EmptyAudio`] / [`SttError::AudioTooLarge`] on a bad buffer.
    /// - [`SttError::Cancelled`] if cancellation was requested.
    /// - [`SttError::DecodeFailed`] on a non-zero whisper status.
    /// - [`SttError::NonUtf8Output`] if a segment is not valid UTF-8.
    pub fn transcribe(&mut self, pcm: &[f32]) -> Result<String, SttError> {
        let n_samples = validate_pcm(pcm)?;
        if self.cancel.is_cancelled() {
            return Err(SttError::Cancelled);
        }
        // SAFETY: [Category 8 — FFI boundary] GREEDY is a valid strategy enum;
        // the returned struct matches the layout locked in sys.rs.
        let mut params =
            unsafe { sys::whisper_full_default_params(sys::WhisperSamplingStrategy::Greedy) };
        configure_params(&mut params, self.n_threads, &self.cancel);
        // SAFETY: [Category 8 — FFI boundary] `self.ctx` is a live context owned
        // by `self`; `pcm.as_ptr()` addresses exactly `n_samples` f32 values; the
        // abort callback + its user_data borrow `self.cancel`, which outlives the
        // synchronous call.
        let status =
            unsafe { sys::whisper_full(self.ctx.as_ptr(), params, pcm.as_ptr(), n_samples) };
        if self.cancel.is_cancelled() {
            return Err(SttError::Cancelled);
        }
        if status != 0 {
            return Err(SttError::DecodeFailed { code: status });
        }
        collect_segments(self.ctx)
    }
}

impl Drop for WhisperSession {
    fn drop(&mut self) {
        // SAFETY: [Category 12 — Double free] `ctx` came from
        // `whisper_init_from_file_with_params` and is freed exactly once. The
        // session is neither Copy nor Clone, so no aliasing owner can free it.
        unsafe { sys::whisper_free(self.ctx.as_ptr()) };
    }
}

/// Validate a PCM buffer and return its length as a whisper `int`.
pub(crate) fn validate_pcm(pcm: &[f32]) -> Result<c_int, SttError> {
    if pcm.is_empty() {
        return Err(SttError::EmptyAudio);
    }
    c_int::try_from(pcm.len()).map_err(|_| SttError::AudioTooLarge { samples: pcm.len() })
}

/// Clamp a requested thread count into whisper's sane 1..=8 band.
pub(crate) const fn clamp_threads(requested: u32) -> c_int {
    let clamped = if requested == 0 {
        4
    } else if requested > 8 {
        8
    } else {
        requested
    };
    clamped as c_int
}

/// Tune decode params for low-latency English push-to-talk transcription and
/// wire the cancellation callback.
fn configure_params(params: &mut sys::WhisperFullParams, n_threads: c_int, cancel: &CancelToken) {
    params.n_threads = n_threads;
    params.translate = false;
    params.no_context = true;
    params.no_timestamps = true;
    params.single_segment = false;
    params.print_special = false;
    params.print_progress = false;
    params.print_realtime = false;
    params.print_timestamps = false;
    params.language = c"en".as_ptr();
    params.abort_callback = Some(abort_trampoline);
    params.abort_callback_user_data = Arc::as_ptr(&cancel.flag).cast_mut().cast::<c_void>();
}

/// Concatenate all decoded segments into a single trimmed string.
fn collect_segments(ctx: NonNull<sys::WhisperContext>) -> Result<String, SttError> {
    // SAFETY: [Category 8 — FFI boundary] `ctx` is live; this reads the default
    // state populated by the preceding `whisper_full` call.
    let n = unsafe { sys::whisper_full_n_segments(ctx.as_ptr()) };
    let mut out = String::new();
    for i in 0..n {
        // SAFETY: [Category 8 — FFI boundary] `0 <= i < n`; whisper returns a
        // NUL-terminated pointer into context-owned memory valid until the next
        // whisper call. Null is defended before dereference.
        let raw = unsafe { sys::whisper_full_get_segment_text(ctx.as_ptr(), i) };
        if raw.is_null() {
            continue;
        }
        // SAFETY: [Category 8 — FFI boundary] `raw` is non-null and
        // NUL-terminated; we copy the bytes out before the next whisper call.
        let text = unsafe { CStr::from_ptr(raw) };
        out.push_str(text.to_str().map_err(|_| SttError::NonUtf8Output)?);
    }
    Ok(out.trim().to_owned())
}

/// `ggml_abort_callback` trampoline: returns `true` to abort the computation.
extern "C" fn abort_trampoline(user_data: *mut c_void) -> bool {
    // Never unwind across the C frame (UB Category 14): a panic aborts the run.
    std::panic::catch_unwind(|| {
        // SAFETY: [Category 8 — FFI boundary / Category 3 — use-after-free]
        // `user_data` is the `*const AtomicBool` stored in
        // `abort_callback_user_data`; the AtomicBool lives in a `CancelToken`
        // held by the caller for the whole `whisper_full` call. Null is defended.
        match unsafe { user_data.cast::<AtomicBool>().as_ref() } {
            Some(flag) => flag.load(Ordering::SeqCst),
            None => false,
        }
    })
    .unwrap_or(true)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn validate_pcm_rejects_empty() {
        assert!(matches!(validate_pcm(&[]), Err(SttError::EmptyAudio)));
    }

    #[test]
    fn validate_pcm_returns_sample_count() {
        assert_eq!(validate_pcm(&[0.0, 1.0, -1.0]), Ok(3));
    }

    #[test]
    fn clamp_threads_bands() {
        assert_eq!(clamp_threads(0), 4);
        assert_eq!(clamp_threads(2), 2);
        assert_eq!(clamp_threads(999), 8);
    }

    #[test]
    fn cancel_token_toggles() {
        let token = CancelToken::new();
        assert!(!token.is_cancelled());
        token.cancel();
        assert!(token.is_cancelled());
        token.reset();
        assert!(!token.is_cancelled());
    }

    #[test]
    fn cancel_token_shares_state_across_clones() {
        let token = CancelToken::new();
        let clone = token.clone();
        token.cancel();
        assert!(clone.is_cancelled());
    }

    #[test]
    fn abort_trampoline_reports_flag() {
        let flag = AtomicBool::new(false);
        let ptr = core::ptr::from_ref(&flag).cast_mut().cast::<c_void>();
        assert!(!abort_trampoline(ptr));
        flag.store(true, Ordering::SeqCst);
        assert!(abort_trampoline(ptr));
    }

    #[test]
    fn abort_trampoline_null_does_not_abort() {
        assert!(!abort_trampoline(core::ptr::null_mut()));
    }
}
