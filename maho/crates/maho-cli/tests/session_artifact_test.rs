#![cfg(unix)]

use std::io::{BufRead, BufReader, Write};
use std::os::unix::net::UnixListener;
use std::process::{Command, Output};

use serde_json::{json, Value};

struct FakeBrowser {
    _dir: tempfile::TempDir,
    socket_path: std::path::PathBuf,
    request: std::sync::mpsc::Receiver<Value>,
    handle: std::thread::JoinHandle<()>,
}

impl FakeBrowser {
    fn spawn(result: Value) -> Self {
        let dir = tempfile::tempdir().expect("tempdir");
        let socket_path = dir.path().join("browser.sock");
        let listener = UnixListener::bind(&socket_path).expect("bind fake browser");
        let (request_tx, request) = std::sync::mpsc::channel();
        let handle = std::thread::spawn(move || {
            let (stream, _) = listener.accept().expect("accept CLI");
            let mut writer = stream.try_clone().expect("clone stream");
            let mut reader = BufReader::new(stream);

            let initialize = read_request(&mut reader);
            write_response(
                &mut writer,
                initialize["id"].clone(),
                json!({
                    "protocolVersion": "2025-03-26",
                    "serverInfo": {"name": "maho-browser", "version": "0.1.0"},
                    "sessionInfo": {
                        "id": "transport-session",
                        "displayLabel": "Maho CLI",
                        "controllerKind": "maho-cli"
                    },
                    "capabilities": {"tools": {}}
                }),
            );

            let control = read_request(&mut reader);
            request_tx.send(control.clone()).expect("record request");
            write_response(&mut writer, control["id"].clone(), result);
        });
        Self {
            _dir: dir,
            socket_path,
            request,
            handle,
        }
    }

    fn socket(&self) -> &str {
        self.socket_path.to_str().expect("UTF-8 socket")
    }

    fn finish(self) -> Value {
        let request = self.request.recv().expect("control request");
        self.handle.join().expect("fake browser thread");
        request
    }
}

fn read_request(reader: &mut BufReader<std::os::unix::net::UnixStream>) -> Value {
    let mut line = String::new();
    reader.read_line(&mut line).expect("read request");
    serde_json::from_str(&line).expect("parse request")
}

fn write_response(writer: &mut std::os::unix::net::UnixStream, id: Value, result: Value) {
    let response = json!({"jsonrpc": "2.0", "id": id, "result": result});
    serde_json::to_writer(&mut *writer, &response).expect("serialize response");
    writer.write_all(b"\n").expect("terminate response");
    writer.flush().expect("flush response");
}

fn run_cli(args: &[&str], socket: Option<&str>, db_path: Option<&std::path::Path>) -> Output {
    let mut command = Command::new(env!("CARGO_BIN_EXE_maho"));
    command.args(args).env("MAHO_CLI_TEST", "1");
    if let Some(socket) = socket {
        command.args(["--socket-path", socket]);
    }
    if let Some(db_path) = db_path {
        command.env("MAHO_DB_PATH", db_path);
    }
    command.output().expect("run maho")
}

fn assert_control_call(args: &[&str], result: Value, name: &str, arguments: Value) -> Output {
    let fake = FakeBrowser::spawn(result);
    let output = run_cli(args, Some(fake.socket()), None);
    let request = fake.finish();
    assert!(
        output.status.success(),
        "command failed: {}",
        String::from_utf8_lossy(&output.stderr)
    );
    assert_eq!(request["method"], "maho/control/call");
    assert_eq!(request["params"]["name"], name);
    assert_eq!(request["params"]["arguments"], arguments);
    output
}

#[test]
fn session_list_resume_and_history_use_shared_storage_offline() {
    let dir = tempfile::tempdir().expect("tempdir");
    let db_path = dir.path().join("maho.db");
    let storage = maho_cli::workspace::open_storage(&db_path).expect("open shared storage");
    storage
        .create_conversation("s1", Some("Research"), None, Some("test-model"))
        .expect("create conversation");
    storage
        .insert_conversation_turn("t1", "s1", "user", "Find the answer", None)
        .expect("insert user turn");
    storage
        .insert_conversation_turn("t2", "s1", "assistant", "The answer is 42", None)
        .expect("insert assistant turn");
    drop(storage);

    let list = run_cli(&["--json", "session", "list"], None, Some(&db_path));
    assert!(
        list.status.success(),
        "{}",
        String::from_utf8_lossy(&list.stderr)
    );
    let listed: Value = serde_json::from_slice(&list.stdout).expect("list JSON");
    assert_eq!(listed["sessions"][0]["id"], "s1");

    let history = run_cli(
        &["--json", "session", "history", "s1"],
        None,
        Some(&db_path),
    );
    assert!(
        history.status.success(),
        "{}",
        String::from_utf8_lossy(&history.stderr)
    );
    let history: Value = serde_json::from_slice(&history.stdout).expect("history JSON");
    assert_eq!(history["messages"][0]["content"], "Find the answer");
    assert_eq!(history["messages"][1]["role"], "assistant");

    let resume = run_cli(&["--json", "session", "resume", "s1"], None, Some(&db_path));
    assert!(
        resume.status.success(),
        "{}",
        String::from_utf8_lossy(&resume.stderr)
    );
    let resumed: Value = serde_json::from_slice(&resume.stdout).expect("resume JSON");
    assert_eq!(resumed["resumed"], true);
    assert_eq!(resumed["session"]["id"], "s1");
    assert_eq!(resumed["messages"].as_array().unwrap().len(), 2);
}

#[test]
fn artifact_list_and_export_remain_control_plane_operations() {
    let list = assert_control_call(
        &["--json", "artifact", "list", "--session", "s1"],
        json!({"artifacts": [{
            "artifact_id":"a1", "session_id":"s1", "display_name":"report.txt",
            "mime_type":"text/plain", "size_bytes":12, "created_at_ms":1
        }]}),
        "artifact.list",
        json!({"session_id":"s1"}),
    );
    assert!(String::from_utf8_lossy(&list.stdout).contains("report.txt"));

    let dir = tempfile::tempdir().expect("export dir");
    let out = dir.path().join("report.txt");
    let out_string = out.to_string_lossy().to_string();
    let validated_out = maho_cli::session_artifact::validate_export_destination(&out)
        .expect("test export destination");
    assert_control_call(
        &["artifact", "export", "a1", "--out", &out_string],
        json!({"exported":true,"destination":validated_out}),
        "artifact.export",
        json!({"artifact_id":"a1","destination":validated_out.to_string_lossy()}),
    );
}

#[test]
fn bad_export_path_is_rejected_before_transport() {
    let output = run_cli(
        &["artifact", "export", "a1", "--out", "../outside.txt"],
        Some("/tmp/unused-maho-session-artifact.sock"),
        None,
    );
    assert_eq!(output.status.code(), Some(1));
    assert!(String::from_utf8_lossy(&output.stderr).contains("must not contain '..'"));
}

#[test]
fn session_and_artifact_help_expose_only_supported_commands() {
    for (group, present, absent) in [
        (
            "session",
            &["list", "resume", "history"][..],
            &["new", "status"][..],
        ),
        (
            "artifact",
            &["list", "export"][..],
            &["open", "rename", "delete"][..],
        ),
    ] {
        let output = run_cli(&[group, "--help"], None, None);
        assert!(output.status.success());
        let stdout = String::from_utf8_lossy(&output.stdout);
        for command in present {
            assert!(
                stdout.contains(command),
                "{group} help omitted {command}: {stdout}"
            );
        }
        for command in absent {
            assert!(
                !stdout.contains(command),
                "{group} help exposed {command}: {stdout}"
            );
        }
    }
}
