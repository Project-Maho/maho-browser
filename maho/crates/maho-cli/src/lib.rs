pub mod approval;
pub mod browser;
pub mod browser_pipe;
pub mod config;
pub mod credits;
pub mod vault;
pub mod workspace;

/// Session-binding browser tools whose side effects outlive a one-shot
/// `maho tool call`. Each acquires browser-side state (a lease, or an in-flight
/// network capture) that must be released by the same session that created it,
/// so invoking one from a one-shot call would orphan that state on exit. These
/// are rejected before execution unless run from a persistent session
/// (`maho repl`).
pub const STATEFUL_TOOLS: &[&str] = &[
    "browser_network_start_capture",
    "browser_network_stop_capture",
    "browser_network_get_har",
    "browser_acquire_lease",
    "browser_heartbeat_lease",
    "browser_release_lease",
];
