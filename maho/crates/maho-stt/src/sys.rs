//! Raw FFI declarations mirroring vendored whisper.cpp v1.9.1 (`whisper.h`).
//!
//! Layout is locked by the size/offset tests below. GN owns the C++ compile
//! (`maho-chromium/third_party/whisper/BUILD.gn`); this crate is link-only, so
//! these `extern "C"` symbols resolve only inside the Chromium link — never
//! under `cargo test`/`miri`, which is why no test calls a whisper symbol.

use core::ffi::{c_char, c_int, c_void};

/// Opaque `struct whisper_context`.
#[repr(C)]
pub struct WhisperContext {
    _private: [u8; 0],
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct WhisperContextParams {
    pub use_gpu: bool,
    pub flash_attn: bool,
    pub gpu_device: c_int,
    pub dtw_token_timestamps: bool,
    pub dtw_aheads_preset: WhisperAlignmentHeadsPreset,
    pub dtw_n_top: c_int,
    pub dtw_aheads: WhisperAheads,
    pub dtw_mem_size: usize,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub enum WhisperAlignmentHeadsPreset {
    None = 0,
    NTopMost = 1,
    Custom = 2,
    TinyEn = 3,
    Tiny = 4,
    BaseEn = 5,
    Base = 6,
    SmallEn = 7,
    Small = 8,
    MediumEn = 9,
    Medium = 10,
    LargeV1 = 11,
    LargeV2 = 12,
    LargeV3 = 13,
    LargeV3Turbo = 14,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct WhisperAhead {
    pub n_text_layer: c_int,
    pub n_head: c_int,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct WhisperAheads {
    pub n_heads: usize,
    pub heads: *const WhisperAhead,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub enum WhisperSamplingStrategy {
    Greedy = 0,
    BeamSearch = 1,
}

/// `ggml_abort_callback` — returns `true` to abort the running computation.
pub type GgmlAbortCallback = Option<unsafe extern "C" fn(user_data: *mut c_void) -> bool>;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct GreedyParams {
    pub best_of: c_int,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct BeamSearchParams {
    pub beam_size: c_int,
    pub patience: f32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct WhisperVadParams {
    pub threshold: f32,
    pub min_speech_duration_ms: c_int,
    pub min_silence_duration_ms: c_int,
    pub max_speech_duration_s: f32,
    pub speech_pad_ms: c_int,
    pub samples_overlap: f32,
}

/// Mirror of `struct whisper_full_params`. Passed BY VALUE to `whisper_full`;
/// obtained from `whisper_full_default_params`, then a few fields are mutated.
/// Callback fields we never set are typed `*mut c_void` (pointer-sized, round-trips).
#[repr(C)]
pub struct WhisperFullParams {
    pub strategy: WhisperSamplingStrategy,
    pub n_threads: c_int,
    pub n_max_text_ctx: c_int,
    pub offset_ms: c_int,
    pub duration_ms: c_int,
    pub translate: bool,
    pub no_context: bool,
    pub no_timestamps: bool,
    pub single_segment: bool,
    pub print_special: bool,
    pub print_progress: bool,
    pub print_realtime: bool,
    pub print_timestamps: bool,
    pub token_timestamps: bool,
    pub thold_pt: f32,
    pub thold_ptsum: f32,
    pub max_len: c_int,
    pub split_on_word: bool,
    pub max_tokens: c_int,
    pub debug_mode: bool,
    pub audio_ctx: c_int,
    pub tdrz_enable: bool,
    pub suppress_regex: *const c_char,
    pub initial_prompt: *const c_char,
    pub carry_initial_prompt: bool,
    pub prompt_tokens: *const c_int,
    pub prompt_n_tokens: c_int,
    pub language: *const c_char,
    pub detect_language: bool,
    pub suppress_blank: bool,
    pub suppress_nst: bool,
    pub temperature: f32,
    pub max_initial_ts: f32,
    pub length_penalty: f32,
    pub temperature_inc: f32,
    pub entropy_thold: f32,
    pub logprob_thold: f32,
    pub no_speech_thold: f32,
    pub greedy: GreedyParams,
    pub beam_search: BeamSearchParams,
    pub new_segment_callback: *mut c_void,
    pub new_segment_callback_user_data: *mut c_void,
    pub progress_callback: *mut c_void,
    pub progress_callback_user_data: *mut c_void,
    pub encoder_begin_callback: *mut c_void,
    pub encoder_begin_callback_user_data: *mut c_void,
    pub abort_callback: GgmlAbortCallback,
    pub abort_callback_user_data: *mut c_void,
    pub logits_filter_callback: *mut c_void,
    pub logits_filter_callback_user_data: *mut c_void,
    pub grammar_rules: *const *const c_void,
    pub n_grammar_rules: usize,
    pub i_start_rule: usize,
    pub grammar_penalty: f32,
    pub vad: bool,
    pub vad_model_path: *const c_char,
    pub vad_params: WhisperVadParams,
}

unsafe extern "C" {
    pub fn whisper_init_from_file_with_params(
        path_model: *const c_char,
        params: WhisperContextParams,
    ) -> *mut WhisperContext;
    pub fn whisper_context_default_params() -> WhisperContextParams;
    pub fn whisper_full_default_params(strategy: WhisperSamplingStrategy) -> WhisperFullParams;
    pub fn whisper_full(
        ctx: *mut WhisperContext,
        params: WhisperFullParams,
        samples: *const f32,
        n_samples: c_int,
    ) -> c_int;
    pub fn whisper_full_n_segments(ctx: *mut WhisperContext) -> c_int;
    pub fn whisper_full_get_segment_text(
        ctx: *mut WhisperContext,
        i_segment: c_int,
    ) -> *const c_char;
    pub fn whisper_free(ctx: *mut WhisperContext);
    pub fn whisper_free_context_params(params: *mut WhisperContextParams);
}

#[cfg(test)]
mod tests {
    use super::*;
    use core::mem::{align_of, offset_of, size_of};

    #[test]
    fn context_params_layout_matches_whisper_v1_9_1_on_64_bit_desktop() {
        assert_eq!(size_of::<WhisperContextParams>(), 48);
        assert_eq!(align_of::<WhisperContextParams>(), 8);
        assert_eq!(offset_of!(WhisperContextParams, use_gpu), 0);
        assert_eq!(offset_of!(WhisperContextParams, flash_attn), 1);
        assert_eq!(offset_of!(WhisperContextParams, gpu_device), 4);
        assert_eq!(offset_of!(WhisperContextParams, dtw_token_timestamps), 8);
        assert_eq!(offset_of!(WhisperContextParams, dtw_aheads_preset), 12);
        assert_eq!(offset_of!(WhisperContextParams, dtw_n_top), 16);
        assert_eq!(offset_of!(WhisperContextParams, dtw_aheads), 24);
        assert_eq!(offset_of!(WhisperContextParams, dtw_mem_size), 40);
    }

    #[test]
    fn full_params_layout_matches_whisper_v1_9_1_on_64_bit_desktop() {
        assert_eq!(size_of::<WhisperFullParams>(), 304);
        assert_eq!(align_of::<WhisperFullParams>(), 8);
        assert_eq!(offset_of!(WhisperFullParams, n_threads), 4);
        assert_eq!(offset_of!(WhisperFullParams, translate), 20);
        assert_eq!(offset_of!(WhisperFullParams, no_context), 21);
        assert_eq!(offset_of!(WhisperFullParams, no_timestamps), 22);
        assert_eq!(offset_of!(WhisperFullParams, single_segment), 23);
        assert_eq!(offset_of!(WhisperFullParams, print_realtime), 26);
        assert_eq!(offset_of!(WhisperFullParams, thold_pt), 32);
        assert_eq!(offset_of!(WhisperFullParams, language), 104);
        assert_eq!(offset_of!(WhisperFullParams, greedy), 144);
        assert_eq!(offset_of!(WhisperFullParams, beam_search), 148);
        assert_eq!(offset_of!(WhisperFullParams, abort_callback), 208);
        assert_eq!(offset_of!(WhisperFullParams, abort_callback_user_data), 216);
        assert_eq!(offset_of!(WhisperFullParams, vad), 268);
        assert_eq!(offset_of!(WhisperFullParams, vad_model_path), 272);
        assert_eq!(offset_of!(WhisperFullParams, vad_params), 280);
    }

    #[test]
    fn vad_params_layout() {
        assert_eq!(size_of::<WhisperVadParams>(), 24);
        assert_eq!(align_of::<WhisperVadParams>(), 4);
    }
}
