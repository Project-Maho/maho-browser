use std::io::{BufRead, BufReader, Read, Write};
use std::net::{TcpListener, TcpStream};
use std::os::unix::net::{UnixListener, UnixStream};
use std::process::{Command, Stdio};
use std::sync::mpsc::{self, Receiver, Sender};
use std::time::Duration;

use tempfile::{NamedTempFile, TempDir};

fn run_cli(args: &[&str], db_path: &std::path::Path) -> std::process::Output {
    let bin = env!("CARGO_BIN_EXE_maho");
    let mut cmd = Command::new(bin);
    cmd.args(args);
    cmd.env("MAHO_DB_PATH", db_path);
    cmd.output().expect("failed to run CLI binary")
}

fn setup_db() -> (NamedTempFile, std::path::PathBuf) {
    let db_file = NamedTempFile::new().expect("create test database");
    let db_path = db_file.path().to_path_buf();
    let storage = maho_cli::workspace::open_storage(&db_path).expect("open test storage");
    let ws = maho_types::ai::AiWorkspace {
        id: "default".to_string(),
        name: "Default Workspace".to_string(),
        space_id: None,
        profile_id: Some("blank".to_string()),
        workspace_root: None,
        created_at: String::new(),
        updated_at: String::new(),
    };
    storage
        .create_workspace(&ws)
        .expect("create default workspace");
    (db_file, db_path)
}

#[test]
fn removed_approval_flags_fail_as_unknown_and_are_hidden_from_help() {
    let (_db_file, db_path) = setup_db();

    for flag in ["--yolo", "--allow-tools"] {
        let output = run_cli(&["run", flag, "say hello"], &db_path);
        let stderr = String::from_utf8_lossy(&output.stderr);
        assert_eq!(
            output.status.code(),
            Some(2),
            "{flag} must be rejected as an unrecognized argument; stderr: {stderr}"
        );
        assert!(
            stderr.contains("unexpected argument") || stderr.contains("error:"),
            "{flag} must emit a clap error; stderr: {stderr}"
        );
    }

    for args in [
        ["run", "--help"].as_slice(),
        ["agent", "--help"].as_slice(),
        ["agent", "run", "--help"].as_slice(),
    ] {
        let output = run_cli(args, &db_path);
        assert!(output.status.success(), "help must succeed");
        let stdout = String::from_utf8_lossy(&output.stdout);
        assert!(!stdout.contains("--yolo"), "help leaked --yolo: {stdout}");
        assert!(
            !stdout.contains("--allow-tools"),
            "help leaked --allow-tools: {stdout}"
        );
    }
}

fn write_jsonrpc_response(
    writer: &mut impl Write,
    id: serde_json::Value,
    result: serde_json::Value,
) {
    let response = serde_json::json!({"jsonrpc": "2.0", "id": id, "result": result});
    writeln!(writer, "{response}").expect("write mock browser response");
    writer.flush().expect("flush mock browser response");
}

const MOCK_TIMEOUT: Duration = Duration::from_secs(5);
const CLI_TIMEOUT: Duration = Duration::from_secs(15);

fn spawn_mock_browser() -> (TempDir, String, Sender<()>, Receiver<Vec<String>>, std::thread::JoinHandle<()>) {
    let dir = tempfile::tempdir().expect("create mock browser directory");
    let socket_path = dir.path().join("mock-browser.sock");
    let listener = UnixListener::bind(&socket_path).expect("bind mock browser UDS");
    let socket_path_string = socket_path.to_string_lossy().into_owned();
    let (stop_tx, stop_rx) = mpsc::channel();
    let (calls_tx, calls_rx) = mpsc::channel();

    let handle = std::thread::spawn(move || {
        let (stream, _) = listener.accept().expect("CLI connects to mock browser");
        if stop_rx.try_recv().is_ok() {
            calls_tx.send(Vec::new()).expect("report mock browser calls");
            return;
        }
        stream.set_read_timeout(Some(MOCK_TIMEOUT)).expect("set mock browser read timeout");
        let mut writer = stream.try_clone().expect("clone mock browser stream");
        let mut reader = BufReader::new(stream);
        let mut calls = Vec::new();
        let mut line = String::new();
        loop {
            line.clear();
            if reader.read_line(&mut line).expect("read mock browser request") == 0 {
                break;
            }
            let request: serde_json::Value = serde_json::from_str(line.trim()).expect("mock browser request is JSON");
            let id = request.get("id").cloned().unwrap_or(serde_json::Value::Null);
            let method = request.get("method").and_then(serde_json::Value::as_str).unwrap_or_default();
            let result = match method {
                "initialize" => serde_json::json!({"protocolVersion": "2025-03-26", "serverInfo": {"name": "maho-browser", "version": "1"}, "capabilities": {"tools": {}}, "sessionInfo": {"id": "approval-test-session", "displayLabel": "Maho CLI", "controllerKind": "maho-cli"}}),
                "tools/list" => serde_json::json!({"tools": [{"capabilityId": "browser_click", "name": "browser_click", "description": "Click a browser element.", "inputSchema": {"type": "object"}, "schemaVersion": 1, "policy": {"sensitive": true, "permission": "always_ask"}}]}),
                "tools/call" => {
                    let name = request.pointer("/params/name").and_then(serde_json::Value::as_str).expect("tools/call includes a tool name");
                    calls.push(name.to_string());
                    serde_json::json!({"outputJson": "{\"clicked\":true}", "receipt": {"capabilityId": "browser_click", "executionId": "approval-test-click", "metadata": {"state": "completed"}}})
                }
                other => panic!("unexpected mock browser method: {other}"),
            };
            write_jsonrpc_response(&mut writer, id, result);
        }
        calls_tx.send(calls).expect("report mock browser calls");
    });

    (dir, socket_path_string, stop_tx, calls_rx, handle)
}

fn read_http_request(stream: &mut TcpStream) {
    let mut request = Vec::new();
    let mut byte = [0_u8; 1];
    while !request.ends_with(b"\r\n\r\n") {
        stream.read_exact(&mut byte).expect("read mock LLM headers");
        request.push(byte[0]);
    }
    let headers = String::from_utf8(request).expect("mock LLM headers are UTF-8");
    let content_length = headers
        .lines()
        .find_map(|line| line.strip_prefix("Content-Length: ").or_else(|| line.strip_prefix("content-length: ")))
        .expect("mock LLM request has content length")
        .parse::<usize>()
        .expect("mock LLM content length is numeric");
    let mut body = vec![0; content_length];
    stream.read_exact(&mut body).expect("read mock LLM body");
}

fn write_openai_response(stream: &mut TcpStream, body: serde_json::Value) {
    let body = body.to_string();
    write!(
        stream,
        "HTTP/1.1 200 OK\r\ncontent-type: application/json\r\ncontent-length: {}\r\nconnection: close\r\n\r\n{}",
        body.len(),
        body
    )
    .expect("write mock LLM response");
    stream.flush().expect("flush mock LLM response");
}

fn spawn_mock_openai() -> (String, String, Sender<()>, Receiver<usize>, std::thread::JoinHandle<()>) {
    let listener = TcpListener::bind("127.0.0.1:0").expect("bind mock OpenAI listener");
    let address = listener.local_addr().expect("mock OpenAI address").to_string();
    let url = format!("http://{address}");
    let (stop_tx, stop_rx) = mpsc::channel();
    let (requests_tx, requests_rx) = mpsc::channel();
    let handle = std::thread::spawn(move || {
        let mut requests = 0;
        for request_number in 0..2 {
            let (mut stream, _) = listener.accept().expect("CLI calls mock OpenAI");
            if stop_rx.try_recv().is_ok() { break; }
            stream.set_read_timeout(Some(MOCK_TIMEOUT)).expect("set mock OpenAI read timeout");
            read_http_request(&mut stream);
            requests += 1;
            let message = if request_number == 0 {
                serde_json::json!({"role": "assistant", "content": null, "tool_calls": [{"id": "approval-test-tool-call", "type": "function", "function": {"name": "browser_click", "arguments": "{}"}}]})
            } else { serde_json::json!({"role": "assistant", "content": "click completed"}) };
            write_openai_response(&mut stream, serde_json::json!({"choices": [{"message": message}]}));
        }
        requests_tx.send(requests).expect("report mock OpenAI requests");
    });
    (url, address, stop_tx, requests_rx, handle)
}

/// Exercises `maho run` with no approval flags. Reverting build_cli_backend's
/// AllowAll policy to deny-sensitive prevents the sensitive browser_click call
/// from reaching this UDS harness.
#[test]
fn unflagged_run_approval_dispatches_sensitive_browser_tool() {
    let (_db_file, db_path) = setup_db();
    let (_browser_dir, socket_path, browser_stop, browser_calls_rx, browser_handle) = spawn_mock_browser();
    let (openai_url, openai_address, openai_stop, openai_requests_rx, openai_handle) = spawn_mock_openai();

    let bin = env!("CARGO_BIN_EXE_maho");
    let child = Command::new(bin)
        .args(["run", "--provider", "openai", "--model", "approval-test-model", "dispatch the sensitive browser click", "--socket-path", &socket_path])
        .env("MAHO_DB_PATH", &db_path)
        .env("OPENAI_API_KEY", "approval-test-key")
        .env("MAHO_BYOK_BASE_URL", openai_url)
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()
        .expect("start maho CLI against mock services");
    let pid = child.id();
    let (output_tx, output_rx) = mpsc::channel();
    std::thread::spawn(move || { output_tx.send(child.wait_with_output()).expect("report maho CLI output"); });
    let output = match output_rx.recv_timeout(CLI_TIMEOUT) {
        Ok(result) => result.expect("wait for maho CLI"),
        Err(mpsc::RecvTimeoutError::Timeout) => {
            nix::sys::signal::kill(nix::unistd::Pid::from_raw(pid.try_into().expect("CLI pid fits in i32")), nix::sys::signal::Signal::SIGKILL).expect("kill timed out maho CLI");
            output_rx.recv_timeout(MOCK_TIMEOUT).expect("maho CLI exits after timeout").expect("wait for timed out maho CLI");
            panic!("maho CLI did not exit within {CLI_TIMEOUT:?}");
        }
        Err(mpsc::RecvTimeoutError::Disconnected) => panic!("maho CLI wait thread disconnected"),
    };

    let _ = openai_stop.send(());
    let _ = TcpStream::connect(&openai_address);
    let _ = browser_stop.send(());
    let _ = UnixStream::connect(&socket_path);
    let openai_requests = openai_requests_rx.recv_timeout(MOCK_TIMEOUT).expect("mock OpenAI must report received requests within the timeout");
    openai_handle.join().expect("mock OpenAI completes");
    let browser_calls = browser_calls_rx.recv_timeout(MOCK_TIMEOUT).expect("mock browser must report received tool calls within the timeout");
    browser_handle.join().expect("mock browser completes");

    assert!(
        browser_calls.iter().any(|tool| tool == "browser_click"),
        "unflagged maho run must dispatch the sensitive browser tool; calls: {browser_calls:?}; OpenAI requests: {openai_requests}; stdout: {}; stderr: {}",
        String::from_utf8_lossy(&output.stdout),
        String::from_utf8_lossy(&output.stderr)
    );
    assert!(output.status.success(), "unflagged maho run must succeed; stderr: {}", String::from_utf8_lossy(&output.stderr));
}
