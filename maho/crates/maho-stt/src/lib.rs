//! `maho-stt` — desktop-only on-device speech-to-text over vendored whisper.cpp.
//!
//! This crate is a **standalone static lib** (its own `[workspace]`, NOT a
//! member of `maho/Cargo.toml`, NOT a dependency of `maho-ffi`). It wraps the
//! whisper.cpp `extern "C"` API only; GN owns the C++ compile
//! (`maho-chromium/third_party/whisper/BUILD.gn`), so this crate is link-only.
//!
//! The browser process reaches this engine through the C ABI in the sibling
//! `maho-stt-ffi` crate.

#![forbid(unsafe_op_in_unsafe_fn)]

mod error;
mod session;
pub mod sys;

pub use error::SttError;
pub use session::{CancelToken, WhisperSession};
