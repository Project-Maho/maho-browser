#![cfg(target_os = "windows")]

use maho_agent::sandbox::windows::run_confined;
use maho_agent::sandbox::{SandboxBackend, SandboxError, SandboxPolicy};
use std::os::windows::fs::OpenOptionsExt;
use std::time::Duration;

fn policy_for(root: &std::path::Path) -> SandboxPolicy {
    SandboxPolicy {
        writable_roots: vec![root.to_path_buf()],
        readable_roots: Vec::new(),
        denied_paths: Vec::new(),
        network: false,
    }
}

fn env_lock() -> std::sync::MutexGuard<'static, ()> {
    static LOCK: std::sync::Mutex<()> = std::sync::Mutex::new(());
    LOCK.lock()
        .unwrap_or_else(std::sync::PoisonError::into_inner)
}

#[test]
fn write_inside_root_succeeds_and_outside_is_denied() {
    let _lock = env_lock();
    let root = tempfile::tempdir().unwrap();
    let outside = tempfile::tempdir().unwrap();
    let inside_file = root.path().join("inside.txt");
    let outside_file = outside.path().join("outside.txt");
    let command = format!(
        "cmd.exe /c \"echo x> \"{}\" & echo x> \"{}\"\"",
        inside_file.display(),
        outside_file.display()
    );
    let exit = run_confined(
        &command,
        root.path(),
        &policy_for(root.path()),
        Duration::from_secs(20),
    )
    .unwrap();
    assert!(!exit.timed_out);
    assert!(
        inside_file.exists(),
        "write inside the writable root failed"
    );
    assert!(
        !outside_file.exists(),
        "sandbox wrote outside its writable root"
    );
}

#[test]
fn network_is_denied() {
    let _lock = env_lock();
    let listener = std::net::TcpListener::bind("127.0.0.1:0").unwrap();
    let port = listener.local_addr().unwrap().port();
    listener.set_nonblocking(true).unwrap();
    let root = tempfile::tempdir().unwrap();
    let started = root.path().join("started.txt");
    let marker = root.path().join("connected.txt");
    // PowerShell cannot start inside an AppContainer (0xc0000142), so the
    // probe is cmd.exe + the system curl.exe.
    let command = format!(
        "cmd.exe /c \"echo started> \"{}\" & curl.exe -s -m 5 http://127.0.0.1:{port}/ && echo ok> \"{}\"\"",
        started.display(),
        marker.display()
    );
    let exit = run_confined(
        &command,
        root.path(),
        &policy_for(root.path()),
        Duration::from_secs(60),
    )
    .unwrap();
    assert!(!exit.timed_out);
    assert!(started.exists(), "confined process never ran: {exit:?}");
    assert!(!marker.exists(), "confined process reached the network");
    assert!(
        listener.accept().is_err(),
        "listener received a connection from the sandbox"
    );
}

#[test]
fn timeout_kills_the_whole_process_tree() {
    let _lock = env_lock();
    let root = tempfile::tempdir().unwrap();
    let hold = root.path().join("hold.txt");
    // Console tools such as choice/timeout/ping fail to start inside an
    // AppContainer, so both processes spin in cmd's built-in loop. The
    // grandchild inherits the redirect handle to hold.txt and keeps it open
    // for as long as it lives, so the file becomes exclusively openable only
    // once the job has killed it.
    let spin = "for /L %i in (1,1,400000000) do @rem";
    let command = format!(
        "cmd.exe /c \"start /b cmd.exe /c \"{spin}\" > \"{}\" & {spin}\"",
        hold.display()
    );
    let exit = run_confined(
        &command,
        root.path(),
        &policy_for(root.path()),
        Duration::from_secs(2),
    )
    .unwrap();
    assert!(exit.timed_out, "{exit:?}");
    assert!(hold.exists(), "the grandchild was never started");
    // Process teardown releases handles asynchronously; wait for that state,
    // bounded far below the grandchild's own run time.
    let deadline = std::time::Instant::now() + Duration::from_secs(10);
    loop {
        let exclusive = std::fs::OpenOptions::new()
            .read(true)
            .share_mode(0)
            .open(&hold);
        if exclusive.is_ok() {
            break;
        }
        assert!(
            std::time::Instant::now() < deadline,
            "a grandchild survived the job object after timeout: {:?}",
            exclusive.err()
        );
        std::thread::yield_now();
    }
}

#[test]
fn network_enabled_policy_fails_closed() {
    let _lock = env_lock();
    let root = tempfile::tempdir().unwrap();
    let mut policy = policy_for(root.path());
    policy.network = true;
    let result = maho_agent::sandbox::windows::WindowsSandbox.prepare(&policy);
    assert!(matches!(result, Err(SandboxError::Unavailable(_))));
    let marker = root.path().join("ran.txt");
    let result = run_confined(
        &format!("cmd.exe /c \"echo x> \"{}\"\"", marker.display()),
        root.path(),
        &policy,
        Duration::from_secs(10),
    );
    assert!(result.is_err());
    assert!(!marker.exists());
}

#[cfg(feature = "debug-test-hooks")]
#[test]
fn forced_unavailable_spawns_nothing() {
    let _lock = env_lock();
    let root = tempfile::tempdir().unwrap();
    let marker = root.path().join("ran.txt");
    std::env::set_var("MAHO_SANDBOX_FORCE_UNAVAILABLE", "1");
    let result = run_confined(
        &format!("cmd.exe /c \"echo x> \"{}\"\"", marker.display()),
        root.path(),
        &policy_for(root.path()),
        Duration::from_secs(10),
    );
    std::env::remove_var("MAHO_SANDBOX_FORCE_UNAVAILABLE");
    assert!(matches!(result, Err(SandboxError::Unavailable(_))));
    assert!(!marker.exists());
}
