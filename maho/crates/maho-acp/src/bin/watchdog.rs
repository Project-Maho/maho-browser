//! `maho-acp-watchdog` — orphan-reaper watchdog for `opencode acp`.
//!
//! # Purpose
//!
//! When the Maho main process is killed with `kill -9` (or crashes), its Rust
//! destructors do not run, so `kill_on_drop` and graceful-shutdown logic have no
//! effect. The `opencode acp` child gets reparented to launchd/init (PID 1) and
//! persists as an orphan, potentially blocking subsequent Maho relaunches or
//! consuming resources silently.
//!
//! This binary solves the problem by acting as an intermediate process between
//! Maho and `opencode acp`. Maho spawns `maho-acp-watchdog <opencode-path> acp`,
//! the watchdog spawns `opencode acp` as its own child, and then polls
//! `getppid()` every 500 ms. When Maho dies abruptly, the watchdog's parent PID
//! becomes 1 (the init/launchd process). The watchdog detects this, SIGKILLs
//! `opencode acp`, and exits 0.
//!
//! # PPID-polling rationale
//!
//! `PR_SET_PDEATHSIG` (Linux-only) would be cleaner but is not available on
//! macOS. `kqueue EVFILT_PROC` could watch a specific PID on macOS, but requires
//! the watcher to be in the same session or have elevated privileges. Polling
//! `getppid() == 1` is the portable, privilege-free, zero-dependency approach
//! that works identically on macOS and Linux. The 500 ms poll interval is a
//! deliberate trade-off: low enough for snappy cleanup, high enough to be
//! imperceptible CPU overhead.
//!
//! # Migration plan reference
//!
//! This binary implements follow-up item #4 from the Maho AI ACP migration plan
//! (`.sisyphus/plans/maho-ai-acp-migration-plan.md` §11): the "`kill -9` orphan
//! reaper" watchdog option (iii) — introduce a tiny watchdog binary that polls
//! `getppid() == 1`. See also §3 commitment 4 and §8 risk table row for context
//! on the verified orphan-persistence behavior this addresses.
//!
//! # Usage
//!
//! ```text
//! maho-acp-watchdog <command> [args...]
//! ```
//!
//! The watchdog spawns `<command> [args...]` with inherited stdin/stdout/stderr,
//! so the existing pipe topology in `AcpProcess::spawn` is fully transparent.

use std::process::{Command, Stdio};
use std::time::Duration;

fn main() {
    let args: Vec<String> = std::env::args().collect();

    if args.len() < 2 {
        eprintln!("usage: maho-acp-watchdog <command> [args...]");
        std::process::exit(2);
    }

    let command = &args[1];
    let command_args = &args[2..];

    let mut child = match Command::new(command)
        .args(command_args)
        .stdin(Stdio::inherit())
        .stdout(Stdio::inherit())
        .stderr(Stdio::inherit())
        .spawn()
    {
        Ok(c) => c,
        Err(e) => {
            eprintln!("maho-acp-watchdog: failed to spawn {:?}: {}", command, e);
            std::process::exit(1);
        }
    };

    let child_pid = child.id() as i32;

    run_poll_loop(&mut child, child_pid);
}

#[cfg(unix)]
fn run_poll_loop(child: &mut std::process::Child, child_pid: i32) {
    loop {
        std::thread::sleep(Duration::from_millis(500));

        // Safety: getppid() is always safe to call; it has no invalid inputs.
        let ppid = unsafe { libc::getppid() };
        if ppid == 1 {
            // Parent has died and we have been reparented to init/launchd.
            // SIGKILL the opencode acp child so it does not become an orphan.
            //
            // Safety: child_pid is a valid positive PID we obtained from
            // std::process::Child::id() immediately after spawn. SIGKILL is
            // defined in libc. The only failure modes (ESRCH = already exited,
            // EPERM = no permission) are harmless here; we ignore the return.
            unsafe { libc::kill(child_pid, libc::SIGKILL) };
            std::process::exit(0);
        }

        match child.try_wait() {
            Ok(Some(status)) => {
                std::process::exit(status.code().unwrap_or(0));
            }
            Ok(None) => {
                // Child still running; continue polling.
            }
            Err(e) => {
                eprintln!("maho-acp-watchdog: try_wait error: {}", e);
                std::process::exit(1);
            }
        }
    }
}

#[cfg(not(unix))]
fn run_poll_loop(child: &mut std::process::Child, _child_pid: i32) {
    // On non-Unix platforms (Windows), we do not have getppid() or PPID==1
    // semantics. Simply wait for the child to finish naturally. No watchdog
    // protection is provided, but the binary still builds and forwards the
    // child's exit code correctly.
    match child.wait() {
        Ok(status) => {
            std::process::exit(status.code().unwrap_or(0));
        }
        Err(e) => {
            eprintln!("maho-acp-watchdog: wait error: {}", e);
            std::process::exit(1);
        }
    }
}
