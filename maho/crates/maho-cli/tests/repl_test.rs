use serde_json::{json, Value};
use std::io::Write;
use std::path::PathBuf;
use std::process::{Command, Stdio};
use tokio::io::{AsyncBufReadExt, AsyncWriteExt, BufReader};
use tokio::net::UnixListener;

async fn make_mock_server(
    responses: Vec<Value>,
) -> (tempfile::TempDir, PathBuf, tokio::task::JoinHandle<()>) {
    let dir = tempfile::tempdir().unwrap();
    let sock_path = dir.path().join("test.sock");
    let listener = UnixListener::bind(&sock_path).unwrap();

    let handle = tokio::spawn(async move {
        let (stream, _) = listener.accept().await.unwrap();
        let (reader, mut writer) = stream.into_split();
        let mut reader = BufReader::new(reader);
        let mut line = String::new();

        reader.read_line(&mut line).await.unwrap();
        let req: Value = serde_json::from_str(&line).unwrap();
        let id = req["id"].as_i64().unwrap();
        let init_resp = json!({
            "jsonrpc": "2.0",
            "id": id,
            "result": {
                "protocolVersion": "2025-03-26",
                "serverInfo": {"name": "maho-browser", "version": "0.1.0"},
                "capabilities": {"tools": {}}
            }
        });
        let mut buf = serde_json::to_vec(&init_resp).unwrap();
        buf.push(b'\n');
        writer.write_all(&buf).await.unwrap();

        for response_result in responses {
            line.clear();
            reader.read_line(&mut line).await.unwrap();
            let req: Value = serde_json::from_str(&line).unwrap();
            let id = req["id"].as_i64().unwrap();
            let resp = json!({
                "jsonrpc": "2.0",
                "id": id,
                "result": {
                    "content": [{"type": "text", "text": serde_json::to_string(&response_result).unwrap()}]
                }
            });
            let mut buf = serde_json::to_vec(&resp).unwrap();
            buf.push(b'\n');
            writer.write_all(&buf).await.unwrap();
        }
    });

    (dir, sock_path.clone(), handle)
}

fn run_cli_with_stdin(
    args: &[&str],
    sock_path: &std::path::Path,
    stdin_data: &str,
) -> std::process::Output {
    let bin = env!("CARGO_BIN_EXE_maho");
    let mut child = Command::new(bin)
        .args(["--socket-path", sock_path.to_str().unwrap()])
        .args(args)
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()
        .expect("failed to spawn CLI binary");

    if let Some(mut stdin) = child.stdin.take() {
        stdin.write_all(stdin_data.as_bytes()).unwrap();
    }

    child.wait_with_output().expect("failed to wait on CLI")
}

#[tokio::test(flavor = "multi_thread")]
async fn repl_navigate_then_quit() {
    let nav_resp = json!({ "ok": true });
    let (_dir, sock_path, server) = make_mock_server(vec![nav_resp]).await;

    let output = run_cli_with_stdin(
        &["browser", "repl"],
        &sock_path,
        "navigate https://example.com\n.quit\n",
    );
    server.abort();

    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(
        stdout.contains("ok"),
        "expected 'ok' in stdout, got: {stdout}"
    );
}

#[tokio::test(flavor = "multi_thread")]
async fn repl_tabs_prints_list() {
    let tabs_resp = json!({
        "tabs": [
            {"id": 1, "title": "Test Page", "url": "https://test.com", "is_active": true},
            {"id": 2, "title": "Other", "url": "https://other.com", "is_active": false}
        ]
    });
    let (_dir, sock_path, server) = make_mock_server(vec![tabs_resp]).await;

    let output = run_cli_with_stdin(&["browser", "repl"], &sock_path, "tabs\n.quit\n");
    server.abort();

    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(
        stdout.contains("Test Page"),
        "expected tab title in stdout, got: {stdout}"
    );
    assert!(
        stdout.contains("ID"),
        "expected header in stdout, got: {stdout}"
    );
}

#[tokio::test(flavor = "multi_thread")]
async fn repl_help_prints_help() {
    let (_dir, sock_path, server) = make_mock_server(vec![]).await;

    let output = run_cli_with_stdin(&["browser", "repl"], &sock_path, "help\n.quit\n");
    server.abort();

    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(
        stdout.contains("Commands:"),
        "expected help text, got: {stdout}"
    );
    assert!(
        stdout.contains("navigate"),
        "expected 'navigate' in help, got: {stdout}"
    );
    assert!(
        stdout.contains("screenshot"),
        "expected 'screenshot' in help, got: {stdout}"
    );
}

#[tokio::test(flavor = "multi_thread")]
async fn repl_lease_acquires_active_tab_permission() {
    // Bare `lease` resolves the active tab, then acquires an exclusive lease on
    // it so subsequent click/type mutations pass the active-tab lease gate.
    let tabs_resp = json!({
        "tabs": [
            {"id": 7, "title": "App Store Connect", "url": "https://appstoreconnect.apple.com", "is_active": true}
        ]
    });
    let lease_resp = json!({ "session_id": "test-lease" });
    let (_dir, sock_path, server) = make_mock_server(vec![tabs_resp, lease_resp]).await;

    let output = run_cli_with_stdin(&["browser", "repl"], &sock_path, "lease\n.quit\n");
    server.abort();

    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(
        stdout.contains("Lease acquired."),
        "expected lease confirmation in stdout, got: {stdout}"
    );
}

#[tokio::test(flavor = "multi_thread")]
async fn repl_key_press_sends_key() {
    // `key <name>` presses a keyboard key on the active tab — required to drive
    // custom comboboxes (filter text + Enter) whose options carry no @ref.
    let key_resp = json!({ "ok": true });
    let (_dir, sock_path, server) = make_mock_server(vec![key_resp]).await;

    let output = run_cli_with_stdin(&["browser", "repl"], &sock_path, "key Enter\n.quit\n");
    server.abort();

    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(
        stdout.contains("ok"),
        "expected key-press confirmation in stdout, got: {stdout}"
    );
}

#[tokio::test(flavor = "multi_thread")]
async fn repl_select_sends_value() {
    // `select <ref> <value>` chooses a dropdown/combobox option by value.
    let select_resp = json!({ "ok": true });
    let (_dir, sock_path, server) = make_mock_server(vec![select_resp]).await;

    let output = run_cli_with_stdin(
        &["browser", "repl"],
        &sock_path,
        "select 58 com.maho.browser\n.quit\n",
    );
    server.abort();

    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(
        stdout.contains("ok"),
        "expected select confirmation in stdout, got: {stdout}"
    );
}

fn asc_like_tree() -> Value {
    // Minimal accessibility snapshot: interactive nodes carry an integer `ref`.
    json!({
        "role": "WebArea",
        "name": "App Store Connect",
        "children": [
            { "role": "button", "name": "New App", "ref": 8 },
            { "role": "textbox", "name": "Name", "ref": 54, "value": "" },
            { "role": "link", "name": "Contact Us", "ref": 50 }
        ]
    })
}

#[tokio::test(flavor = "multi_thread")]
async fn repl_refs_lists_interactive() {
    // `refs` prints ONE compact line per interactive element, never the full
    // JSON tree, so an agent can read refs without re-dumping the snapshot.
    let (_dir, sock_path, server) = make_mock_server(vec![asc_like_tree()]).await;

    let output = run_cli_with_stdin(&["browser", "repl"], &sock_path, "refs\n.quit\n");
    server.abort();

    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(
        stdout.contains("New App") && stdout.contains('8'),
        "expected compact ref line for New App, got: {stdout}"
    );
    assert!(
        !stdout.contains("\"children\""),
        "refs must not dump the raw JSON tree, got: {stdout}"
    );
}

#[tokio::test(flavor = "multi_thread")]
async fn repl_click_by_name_resolves() {
    // `click @<name>` resolves the current @ref from a fresh snapshot (so it
    // never goes stale) then clicks it. Mock: snapshot first, then click.
    let clicked = json!({ "clicked": true });
    let (_dir, sock_path, server) = make_mock_server(vec![asc_like_tree(), clicked]).await;

    let output = run_cli_with_stdin(&["browser", "repl"], &sock_path, "click @New App\n.quit\n");
    server.abort();

    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(
        stdout.contains("ok"),
        "expected click-by-name confirmation in stdout, got: {stdout}"
    );
}

#[tokio::test(flavor = "multi_thread")]
async fn repl_type_by_name_resolves() {
    // `type @<name>=<text>` resolves the ref from a fresh snapshot then types.
    let typed = json!({ "ok": true });
    let (_dir, sock_path, server) = make_mock_server(vec![asc_like_tree(), typed]).await;

    let output = run_cli_with_stdin(
        &["browser", "repl"],
        &sock_path,
        "type @Name=Maho Browser\n.quit\n",
    );
    server.abort();

    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(
        stdout.contains("ok"),
        "expected type-by-name confirmation in stdout, got: {stdout}"
    );
}

#[tokio::test(flavor = "multi_thread")]
async fn repl_unknown_command() {
    let (_dir, sock_path, server) = make_mock_server(vec![]).await;

    let output = run_cli_with_stdin(&["browser", "repl"], &sock_path, "foobar\n.quit\n");
    server.abort();

    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(
        stdout.contains("unknown command: foobar"),
        "expected unknown cmd msg, got: {stdout}"
    );
}
