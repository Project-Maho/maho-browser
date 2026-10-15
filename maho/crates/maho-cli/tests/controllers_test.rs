#![cfg(unix)]

use serde_json::{json, Value};
use std::io::{BufRead, BufReader, Write};
use std::os::unix::net::UnixListener;
use std::process::Command;
use std::sync::{Arc, Mutex};
use std::time::{Duration, SystemTime};

fn write_token(path: &std::path::Path, token: &str, modified: SystemTime) {
    std::fs::create_dir_all(path.parent().unwrap()).unwrap();
    let file = std::fs::File::create(path).unwrap();
    file.set_len(0).unwrap();
    std::fs::write(path, token).unwrap();
    file.set_times(std::fs::FileTimes::new().set_modified(modified))
        .unwrap();
}

struct MockBrowser {
    _dir: tempfile::TempDir,
    socket: std::path::PathBuf,
    calls: Arc<Mutex<Vec<Value>>>,
    thread: std::thread::JoinHandle<()>,
}

impl MockBrowser {
    fn start(responses: Vec<Value>) -> Self {
        Self::start_in(tempfile::tempdir().unwrap(), responses)
    }

    fn start_in(dir: tempfile::TempDir, responses: Vec<Value>) -> Self {
        let socket = dir.path().join("maho.sock");
        let listener = UnixListener::bind(&socket).unwrap();
        let calls = Arc::new(Mutex::new(Vec::new()));
        let seen = Arc::clone(&calls);
        let thread = std::thread::spawn(move || {
            let (stream, _) = listener.accept().unwrap();
            let mut stream = stream;
            let mut reader = BufReader::new(stream.try_clone().unwrap());
            let mut line = String::new();
            reader.read_line(&mut line).unwrap();
            let init: Value = serde_json::from_str(&line).unwrap();
            assert_eq!(init["method"], "initialize");
            seen.lock().unwrap().push(init.clone());
            respond(
                &mut stream,
                init["id"].clone(),
                json!({
                    "protocolVersion":"2025-03-26",
                    "serverInfo":{"name":"maho-browser","version":"test"},
                    "capabilities":{"tools":{}}
                }),
            );
            for result in responses {
                line.clear();
                if reader.read_line(&mut line).unwrap() == 0 {
                    break;
                }
                let request: Value = serde_json::from_str(&line).unwrap();
                seen.lock().unwrap().push(request.clone());
                let id = request["id"].clone();
                respond(&mut stream, id, result);
            }
        });
        Self {
            _dir: dir,
            socket,
            calls,
            thread,
        }
    }
}

fn respond(write: &mut impl Write, id: Value, result: Value) {
    let response = if result.get("__error").is_some() {
        json!({"jsonrpc":"2.0","id":id,"error":result["__error"]})
    } else {
        json!({"jsonrpc":"2.0","id":id,"result":result})
    };
    writeln!(write, "{response}").unwrap();
    write.flush().unwrap();
}

fn run(
    mock: &MockBrowser,
    args: &[&str],
    user_data: &std::path::Path,
    config: &std::path::Path,
) -> std::process::Output {
    Command::new(env!("CARGO_BIN_EXE_maho"))
        .args(args)
        .arg("--socket-path")
        .arg(&mock.socket)
        .env("HOME", user_data)
        .env("MAHO_DB_PATH", config)
        .output()
        .unwrap()
}

#[test]
fn token_is_attached_and_missing_pairing_has_exact_message_and_revocation_deletes_token() {
    let dir = tempfile::tempdir().unwrap();
    let profile = tempfile::TempDir::new_in(dir.path()).unwrap();
    let token_dir = profile.path().join("controllers/maho-cli");
    let older_token_path = token_dir.join("controller-old.token");
    let newest_token_path = token_dir.join("controller-new.token");
    let other_kind_token_path = profile
        .path()
        .join("controllers/maho-cli-repl/controller-other-kind.token");
    let now = SystemTime::now();
    write_token(
        &older_token_path,
        "older-token-value",
        now - Duration::from_secs(60),
    );
    write_token(
        &newest_token_path,
        "newest-token-value",
        now - Duration::from_secs(30),
    );
    write_token(&other_kind_token_path, "other-kind-token-value", now);

    let paired = MockBrowser::start_in(profile, vec![json!({"revoked":true})]);
    let out = run(
        &paired,
        &["controllers", "revoke", "controller-new"],
        paired._dir.path(),
        dir.path(),
    );
    paired.thread.join().unwrap();
    assert!(
        out.status.success(),
        "{}",
        String::from_utf8_lossy(&out.stderr)
    );
    let calls = paired.calls.lock().unwrap();
    assert_eq!(calls[0]["params"]["pairingToken"], "newest-token-value");
    assert_eq!(calls[0]["params"]["controllerId"], "controller-new");
    assert_eq!(calls[1]["method"], "maho/controllers/revoke");
    assert_eq!(calls[1]["params"]["controllerId"], "controller-new");
    assert!(older_token_path.exists());
    assert!(newest_token_path.exists());
    assert!(other_kind_token_path.exists());

    drop(calls);
    let missing_profile = tempfile::TempDir::new_in(dir.path()).unwrap();
    let missing = MockBrowser::start_in(
        missing_profile,
        vec![json!({"__error":{"code":-32014,"message":"pairing required"}})],
    );
    let out = run(
        &missing,
        &["controllers", "revoke", "controller-123"],
        missing._dir.path(),
        dir.path(),
    );
    missing.thread.join().unwrap();
    assert!(!out.status.success());
    assert_eq!(
        String::from_utf8_lossy(&out.stderr).trim(),
        "Error: This controller is not paired. Approve it in Maho (a card appeared), then retry."
    );

    let revoked_profile = tempfile::TempDir::new_in(dir.path()).unwrap();
    let revoked_token = revoked_profile
        .path()
        .join("controllers/maho-cli/controller-new.token");
    let retained_token = revoked_profile
        .path()
        .join("controllers/maho-cli/controller-old.token");
    let retained_other_kind_token = revoked_profile
        .path()
        .join("controllers/maho-cli-repl/controller-other-kind.token");
    write_token(
        &retained_token,
        "older-token-value",
        now - Duration::from_secs(60),
    );
    write_token(
        &revoked_token,
        "newest-token-value",
        now - Duration::from_secs(30),
    );
    write_token(&retained_other_kind_token, "other-kind-token-value", now);
    let revoked = MockBrowser::start_in(
        revoked_profile,
        vec![json!({"__error":{"code":-32015,"message":"pairing revoked"}})],
    );
    let out = run(
        &revoked,
        &["controllers", "revoke", "controller-new"],
        revoked._dir.path(),
        dir.path(),
    );
    revoked.thread.join().unwrap();
    assert!(!out.status.success());
    assert!(!revoked_token.exists());
    assert!(retained_token.exists());
    assert!(retained_other_kind_token.exists());
}

#[test]
fn controllers_list_and_revoke_use_expected_methods_and_only_print_safe_fields() {
    let dir = tempfile::tempdir().unwrap();
    let config = dir.path().join("config");
    let config_file = config.join("mcp.db");
    std::fs::create_dir_all(config_file.parent().unwrap()).unwrap();
    let original = b"mcp registry fixture bytes\0\x01";
    std::fs::write(&config_file, original).unwrap();

    let listing = MockBrowser::start(vec![json!([{
        "controllerId":"controller-1","kind":"maho-cli","name":"Terminal","pairedAt":"time","lastUsedAt":"time","pairingToken":"must-not-print"
    }])]);
    let out = run(
        &listing,
        &["controllers", "list", "--json"],
        dir.path(),
        &config_file,
    );
    listing.thread.join().unwrap();
    assert!(
        out.status.success(),
        "{}",
        String::from_utf8_lossy(&out.stderr)
    );
    let printed = String::from_utf8(out.stdout).unwrap();
    assert!(printed.contains("controller-1"));
    assert!(printed.contains("maho-cli"));
    assert!(printed.contains("Terminal"));
    assert!(!printed.contains("must-not-print"));
    assert_eq!(
        listing.calls.lock().unwrap()[1]["method"],
        "maho/controllers/list"
    );
    assert_eq!(std::fs::read(&config_file).unwrap(), original);

    let revoke = MockBrowser::start(vec![json!({"revoked":true})]);
    let out = run(
        &revoke,
        &["controllers", "revoke", "controller-1"],
        dir.path(),
        &config_file,
    );
    revoke.thread.join().unwrap();
    assert!(
        out.status.success(),
        "{}",
        String::from_utf8_lossy(&out.stderr)
    );
    assert_eq!(
        revoke.calls.lock().unwrap()[1]["method"],
        "maho/controllers/revoke"
    );
    assert_eq!(
        revoke.calls.lock().unwrap()[1]["params"]["controllerId"],
        "controller-1"
    );
    assert_eq!(std::fs::read(&config_file).unwrap(), original);
}
