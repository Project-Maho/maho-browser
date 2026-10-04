//! Typed errors for the whisper STT engine.

use thiserror::Error;

/// Failure modes for loading a model or transcribing audio.
#[derive(Debug, Error, PartialEq, Eq)]
pub enum SttError {
    /// The supplied model path contained an interior NUL byte and cannot be
    /// converted to a C string.
    #[error("model path contains an interior NUL byte")]
    ModelPathNul,

    /// whisper.cpp returned a null context (bad/missing model file, OOM).
    #[error("whisper failed to load model from `{path}`")]
    ModelLoadFailed {
        /// The model path that was passed to whisper.
        path: String,
    },

    /// The PCM buffer was empty; nothing to transcribe.
    #[error("empty PCM buffer")]
    EmptyAudio,

    /// The PCM sample count overflowed the C `int` whisper expects.
    #[error("PCM buffer has {samples} samples, exceeding the whisper int limit")]
    AudioTooLarge {
        /// The offending sample count.
        samples: usize,
    },

    /// The caller cancelled the session before/while decoding.
    #[error("transcription cancelled")]
    Cancelled,

    /// `whisper_full` returned a non-zero status code.
    #[error("whisper_full failed with status {code}")]
    DecodeFailed {
        /// The non-zero return code from `whisper_full`.
        code: i32,
    },

    /// A produced segment was not valid UTF-8.
    #[error("whisper produced non-UTF-8 output")]
    NonUtf8Output,
}
