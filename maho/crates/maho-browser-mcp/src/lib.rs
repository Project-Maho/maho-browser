// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Maho Browser MCP — stdio→UDS bridge exposing browser tools to AI agents.
#![warn(missing_docs)]

mod catalog_repair;
pub mod client;
pub mod error;
pub mod firewall;
pub mod paths;
pub mod protocol;
pub mod redacted;
pub mod server;
pub mod settings;
pub mod win_token;

#[cfg(windows)]
pub mod win_transport;

pub use redacted::Redacted;
