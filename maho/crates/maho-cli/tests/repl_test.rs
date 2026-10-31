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
        "target 7\nnavigate https://example.com\n.quit\n",
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
    assert!(
        stdout.contains("upload <path>         Select a local file for the active pending chooser"),
        "expected generalized upload in help, got: {stdout}"
    );
}

#[tokio::test(flavor = "multi_thread")]
async fn repl_upload_usage_on_missing_arg() {
    let (_dir, sock_path, server) = make_mock_server(vec![]).await;

    let output = run_cli_with_stdin(&["browser", "repl"], &sock_path, "upload\n.quit\n");
    server.abort();

    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(
        stdout.contains("Usage: upload <absolute-path>"),
        "expected generalized usage error, got: {stdout}"
    );
}

#[tokio::test(flavor = "multi_thread")]
async fn repl_upload_dispatches_arbitrary_path() {
    let upload_resp = json!({ "ok": true, "file_selected": true });
    let (_dir, sock_path, server) = make_mock_server(vec![upload_resp]).await;

    let output = run_cli_with_stdin(
        &["browser", "repl"],
        &sock_path,
        "target 7\nupload /tmp/package.msix\n.quit\n",
    );
    server.abort();

    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(
        stdout.contains("ok"),
        "expected 'ok' for upload, got: {stdout}"
    );
}

#[tokio::test(flavor = "multi_thread")]
async fn repl_lease_without_target_refuses_without_leasing() {
    // Fail-closed contract: bare `lease` must NOT fall back to the focused tab.
    let (_dir, sock_path, server) = make_mock_server(vec![]).await;

    let output = run_cli_with_stdin(&["browser", "repl"], &sock_path, "lease\n.quit\n");
    server.abort();

    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(
        stdout.contains("No pinned target"),
        "expected pin guidance in stdout, got: {stdout}"
    );
    assert!(
        !stdout.contains("Lease acquired."),
        "bare lease must not acquire anything, got: {stdout}"
    );
}

#[tokio::test(flavor = "multi_thread")]
async fn repl_lease_active_pins_and_leases_the_active_tab() {
    // `lease active` is the deliberate opt-in: it resolves the currently active
    // tab once and pins it, after which every mutation carries that tab_id.
    let tabs_resp = json!({
        "tabs": [
            {"id": 7, "title": "App Store Connect", "url": "https://appstoreconnect.apple.com", "is_active": true}
        ]
    });
    let lease_resp = json!({ "session_id": "test-lease" });
    let (_dir, sock_path, server) = make_mock_server(vec![tabs_resp, lease_resp]).await;

    let output = run_cli_with_stdin(&["browser", "repl"], &sock_path, "lease active\n.quit\n");
    server.abort();

    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(
        stdout.contains("Lease acquired."),
        "expected lease confirmation in stdout, got: {stdout}"
    );
}

#[tokio::test(flavor = "multi_thread")]
async fn repl_lease_warns_when_stealing_from_previous_holder() {
    // ADR 0016 keeps user-driven force-steal; the repl must make the steal
    // visible instead of silently taking another session's lease.
    let lease_resp = json!({ "session_id": "test-lease", "previous_holder": "pipe-session-42" });
    let (_dir, sock_path, server) = make_mock_server(vec![lease_resp]).await;

    let output = run_cli_with_stdin(&["browser", "repl"], &sock_path, "lease 9\n.quit\n");
    server.abort();

    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(
        stdout.contains("WARNING: stole lease on tab 9 from session 'pipe-session-42'"),
        "expected steal warning in stdout, got: {stdout}"
    );
}

#[tokio::test(flavor = "multi_thread")]
async fn repl_mutation_without_pinned_target_is_refused_client_side() {
    // The repl refuses before dispatch: no tools/call reaches the browser. The
    // refusal surfaces on stderr (the repl error channel), not stdout.
    let (_dir, sock_path, server) = make_mock_server(vec![]).await;

    let output = run_cli_with_stdin(&["browser", "repl"], &sock_path, "click 1\n.quit\n");
    server.abort();

    let stdout = String::from_utf8_lossy(&output.stdout);
    let stderr = String::from_utf8_lossy(&output.stderr);
    assert!(
        stderr.contains("No pinned target"),
        "expected client-side refusal on stderr, got: {stderr}"
    );
    assert!(
        !stdout.contains("ok"),
        "refused mutation must not report success, got: {stdout}"
    );
}

#[tokio::test(flavor = "multi_thread")]
async fn repl_key_press_sends_key() {
    // `key <name>` presses a keyboard key on the active tab — required to drive
    // custom comboboxes (filter text + Enter) whose options carry no @ref.
    let key_resp = json!({ "ok": true });
    let (_dir, sock_path, server) = make_mock_server(vec![key_resp]).await;

    let output = run_cli_with_stdin(&["browser", "repl"], &sock_path, "target 7\nkey Enter\n.quit\n");
    server.abort();

    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(
        stdout.contains("ok"),
        "expected key-press confirmation in stdout, got: {stdout}"
    );
}

#[tokio::test(flavor = "multi_thread")]
async fn repl_browser_back_uses_history_action() {
    let captured = std::sync::Arc::new(std::sync::Mutex::new(Vec::new()));
    let (_dir, sock_path, server) =
        make_mock_server_capturing(vec![json!({"navigated":true})], captured.clone()).await;
    let output = run_cli_with_stdin(
        &["browser", "repl"],
        &sock_path,
        "target 7\nkey BrowserBack\n.quit\n",
    );
    server.abort();
    assert!(output.status.success(), "{}", String::from_utf8_lossy(&output.stderr));
    assert!(String::from_utf8_lossy(&output.stdout).contains("ok"));
    let requests = captured.lock().unwrap();
    assert_eq!(requests.len(), 1);
    assert_eq!(requests[0]["params"]["name"], "browser_history_back");
    assert_eq!(requests[0]["params"]["arguments"]["tab_id"], 7);
}

#[tokio::test(flavor = "multi_thread")]
async fn repl_select_sends_value() {
    // `select <ref> <value>` chooses a dropdown/combobox option by value.
    let select_resp = json!({ "ok": true });
    let (_dir, sock_path, server) = make_mock_server(vec![select_resp]).await;

    let output = run_cli_with_stdin(
        &["browser", "repl"],
        &sock_path,
        "target 7\nselect 58 com.maho.browser\n.quit\n",
    );
    server.abort();

    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(
        stdout.contains("ok"),
        "expected select confirmation in stdout, got: {stdout}"
    );
}

#[tokio::test(flavor = "multi_thread")]
async fn repl_select_observed_ref_sends_value() {
    let snapshot = json!({"role":"WebArea","children":[{"role":"combobox","ref":31,"name":"Category"}]});
    let (_dir, sock_path, server) = make_mock_server(vec![snapshot, json!({"ok":true})]).await;
    let output = run_cli_with_stdin(
        &["browser", "repl"],
        &sock_path,
        "target 7\nselect @e31 mail\n.quit\n",
    );
    server.abort();
    assert!(output.status.success(), "{}", String::from_utf8_lossy(&output.stderr));
    assert!(String::from_utf8_lossy(&output.stdout).contains("ok"));
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

    let output = run_cli_with_stdin(
        &["browser", "repl"],
        &sock_path,
        "target 7\nclick @New App\n.quit\n",
    );
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
        "target 7\ntype @Name=Maho Browser\n.quit\n",
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

#[tokio::test(flavor = "multi_thread")]
async fn repl_observe_prints_compact_tree() {
    let obs_resp = json!({
        "snapshot_token": "s_1",
        "tree": "WebArea 'Test Page'\n  button 'Submit' [ref=1]\n"
    });
    let (_dir, sock_path, server) = make_mock_server(vec![obs_resp]).await;

    let output = run_cli_with_stdin(&["browser", "repl"], &sock_path, "observe\n.quit\n");
    server.abort();

    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(
        stdout.contains("WebArea 'Test Page'"),
        "expected tree in stdout, got: {stdout}"
    );
    assert!(
        stdout.contains("button 'Submit'"),
        "expected button in tree output, got: {stdout}"
    );
}

#[tokio::test(flavor = "multi_thread")]
async fn repl_wait_selector() {
    let wait_resp = json!({ "found": true });
    let (_dir, sock_path, server) = make_mock_server(vec![wait_resp]).await;

    let output = run_cli_with_stdin(
        &["browser", "repl"],
        &sock_path,
        "wait selector #main\n.quit\n",
    );
    server.abort();

    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(
        stdout.contains("ok"),
        "expected 'ok' in stdout, got: {stdout}"
    );
}

// ─── D4: public `grant <origin>` REPL path (browser_grant_exact_origin) ──────

async fn make_mock_server_capturing(
    responses: Vec<Value>,
    captured: std::sync::Arc<std::sync::Mutex<Vec<Value>>>,
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
            captured.lock().unwrap().push(req.clone());
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

#[tokio::test(flavor = "multi_thread")]
async fn repl_grant_sends_grant_exact_origin() {
    let captured = std::sync::Arc::new(std::sync::Mutex::new(Vec::new()));
    let grant_resp = json!({ "ok": true });
    let (_dir, sock_path, server) =
        make_mock_server_capturing(vec![grant_resp], captured.clone()).await;

    let output = run_cli_with_stdin(
        &["browser", "repl"],
        &sock_path,
        "grant https://contest.aitestbed.kr\n.quit\n",
    );
    server.abort();

    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(
        stdout.contains("Origin granted"),
        "expected 'Origin granted' in stdout, got: {stdout}"
    );
    let requests = captured.lock().unwrap();
    let grant_req = requests
        .iter()
        .find(|req| req["method"] == "maho/control/call")
        .expect("grant must issue a maho/control/call (control-plane only capability)");
    assert_eq!(grant_req["params"]["name"], "browser_grant_exact_origin");
    assert_eq!(
        grant_req["params"]["arguments"]["origin"],
        "https://contest.aitestbed.kr"
    );
}

#[tokio::test(flavor = "multi_thread")]
async fn repl_grant_without_origin_prints_usage() {
    let (_dir, sock_path, server) = make_mock_server(vec![]).await;

    let output = run_cli_with_stdin(&["browser", "repl"], &sock_path, "grant\n.quit\n");
    server.abort();

    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(
        stdout.contains("Usage: grant <origin>"),
        "expected usage line in stdout, got: {stdout}"
    );
}

#[tokio::test(flavor = "multi_thread")]
async fn repl_help_lists_grant() {
    let (_dir, sock_path, server) = make_mock_server(vec![]).await;

    let output = run_cli_with_stdin(&["browser", "repl"], &sock_path, "help\n.quit\n");
    server.abort();

    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(
        stdout.contains("grant <origin>"),
        "expected 'grant <origin>' in help, got: {stdout}"
    );
}

#[tokio::test(flavor = "multi_thread")]
async fn repl_upload_dispatches_exact_path_matrix() {
    let test_paths = vec![
        "/tmp/app-release.aab",
        "/tmp/package.msix",
        "/tmp/installer.dmg",
        "/tmp/archive.zip",
        "/tmp/photo.png",
        "/tmp/document.pdf",
        "/tmp/한글_테스트_파일.txt",
        "/tmp/path with spaces/file name.bin",
        "/tmp/executable_no_ext",
    ];

    for path in test_paths {
        let captured = std::sync::Arc::new(std::sync::Mutex::new(Vec::new()));
        let upload_resp = json!({ "ok": true, "file_selected": true });
        let (_dir, sock_path, server) =
            make_mock_server_capturing(vec![upload_resp], captured.clone()).await;

        let input = format!("target 7\nupload {}\n.quit\n", path);
        let output = run_cli_with_stdin(&["browser", "repl"], &sock_path, &input);
        server.abort();

        let stdout = String::from_utf8_lossy(&output.stdout);
        assert!(
            stdout.contains("ok"),
            "expected 'ok' for upload of {path}, got: {stdout}"
        );

        let requests = captured.lock().unwrap();
        let upload_req = requests
            .iter()
            .find(|req| req["method"] == "tools/call")
            .unwrap_or_else(|| panic!("upload must issue tools/call for {path}"));

        assert_eq!(
            upload_req["params"]["name"], "browser_file_upload_select",
            "tool name must be browser_file_upload_select"
        );
        assert_eq!(
            upload_req["params"]["arguments"]["path"], path,
            "path argument must match exact input path"
        );
        assert!(
            upload_req["params"]["arguments"].get("file_path").is_none(),
            "legacy file_path argument must not be sent"
        );
    }
}

#[tokio::test(flavor = "multi_thread")]
async fn repl_lease_pins_target_for_subsequent_commands() {
    // `lease <tab_id>` pins the session: later tab-scoped commands must carry
    // that tab_id explicitly instead of following whatever tab is active.
    let captured = std::sync::Arc::new(std::sync::Mutex::new(Vec::new()));
    let lease_resp = json!({ "session_id": "test-lease" });
    let nav_resp = json!({ "ok": true });
    let (_dir, sock_path, server) =
        make_mock_server_capturing(vec![lease_resp, nav_resp], captured.clone()).await;

    let output = run_cli_with_stdin(
        &["browser", "repl"],
        &sock_path,
        "lease 2\nnavigate https://example.com\n.quit\n",
    );
    server.abort();

    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(
        stdout.contains("Lease acquired."),
        "expected lease confirmation in stdout, got: {stdout}"
    );

    let requests = captured.lock().unwrap();
    let lease_req = requests
        .iter()
        .find(|req| req["method"] == "maho/control/call")
        .expect("lease must issue a maho/control/call");
    assert_eq!(lease_req["params"]["name"], "browser_acquire_lease");
    assert_eq!(lease_req["params"]["arguments"]["tab_id"], 2);

    let nav_req = requests
        .iter()
        .find(|req| req["method"] == "tools/call" && req["params"]["name"] == "browser_navigate")
        .expect("navigate must issue a tools/call");
    assert_eq!(
        nav_req["params"]["arguments"]["tab_id"], 2,
        "navigate must carry the pinned tab_id"
    );
    assert_eq!(nav_req["params"]["arguments"]["url"], "https://example.com");
}

