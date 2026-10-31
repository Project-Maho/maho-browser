#![cfg(unix)]

use std::io::Write;
use std::os::unix::fs::PermissionsExt;
use std::path::{Path, PathBuf};
use std::process::{Command, Output, Stdio};

use serde_json::{json, Value};

const FAKE_BROWSER_MCP: &str = r#"#!/usr/bin/env python3
import json, sys
url = "about:blank"
for line in sys.stdin:
    msg = json.loads(line)
    if "id" not in msg:
        continue
    method = msg.get("method")
    if method == "initialize":
        result = {"protocolVersion": "2025-03-26", "capabilities": {"tools": {}}, "serverInfo": {"name": "fake-browser", "version": "0"}}
    elif method == "tools/call":
        name = msg["params"]["name"]
        args = msg["params"].get("arguments", {})
        if name == "browser_navigate":
            url = args["url"]
            payload = {"navigated": url}
        elif name == "page_wait_for_selector":
            payload = {"found": True}
        elif name == "page_query_selector":
            payload = {"ref_id": "r1", "selector": args["selector"]}
        elif name == "page_get_text":
            payload = {"text": "results for " + url}
        else:
            result = {"content": [{"type": "text", "text": "unknown tool " + name}], "isError": True}
            print(json.dumps({"jsonrpc": "2.0", "id": msg["id"], "result": result}), flush=True)
            continue
        result = {"content": [{"type": "text", "text": json.dumps(payload)}], "isError": False}
    else:
        result = {}
    print(json.dumps({"jsonrpc": "2.0", "id": msg["id"], "result": result}), flush=True)
"#;

fn fake_browser(dir: &Path) -> PathBuf {
    let path = dir.join("fake-browser-mcp");
    std::fs::write(&path, FAKE_BROWSER_MCP).unwrap();
    std::fs::set_permissions(&path, std::fs::Permissions::from_mode(0o755)).unwrap();
    path
}

fn maho(dir: &Path, browser: &Path, args: &[&str], stdin: Option<&str>) -> Output {
    let mut child = Command::new(env!("CARGO_BIN_EXE_maho"))
        .args(args)
        .env("HOME", dir)
        .env("MAHO_BROWSER_MCP_BINARY", browser)
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()
        .expect("spawn maho");
    if let Some(text) = stdin {
        child.stdin.take().unwrap().write_all(text.as_bytes()).unwrap();
    }
    drop(child.stdin.take());
    child.wait_with_output().unwrap()
}

fn json_stdout(output: &Output) -> Value {
    assert!(
        output.status.success(),
        "stderr: {}",
        String::from_utf8_lossy(&output.stderr)
    );
    serde_json::from_slice(&output.stdout).expect("stdout is JSON")
}

#[test]
fn script_from_stdin_and_seed_site_pack_run_against_browser() {
    let dir = tempfile::tempdir().unwrap();
    let browser = fake_browser(dir.path());

    let output = maho(
        dir.path(),
        &browser,
        &["browser", "script", "-", "--input", r#"{"url":"https://example.test/"}"#],
        Some("browser.call('browser_navigate', {url: input.url}); ({ok: true, url: input.url})"),
    );
    assert_eq!(json_stdout(&output), json!({"ok": true, "url": "https://example.test/"}));

    let seed = Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("skills/maho-browser/sites/github/search_repos.js");
    let output = maho(
        dir.path(),
        &browser,
        &[
            "browser",
            "script",
            seed.to_str().unwrap(),
            "--site-tool",
            "--input",
            r#"{"tab_id":7,"query":"maho"}"#,
        ],
        None,
    );
    assert_eq!(
        json_stdout(&output),
        json!({"text": "results for https://github.com/search?type=repositories&q=maho"})
    );
}

#[test]
fn script_failures_are_typed_and_nonzero() {
    let dir = tempfile::tempdir().unwrap();
    let browser = fake_browser(dir.path());

    let refs = dir.path().join("refs.js");
    std::fs::write(&refs, "browser.call('browser_click', {target: '@e3'})").unwrap();
    let output = maho(dir.path(), &browser, &["browser", "script", refs.to_str().unwrap(), "--site-tool"], None);
    assert!(!output.status.success());
    assert!(String::from_utf8_lossy(&output.stderr).contains("ephemeral snapshot ref"));

    let output = maho(
        dir.path(),
        &browser,
        &["browser", "script", "-", "--max-browser-calls", "2"],
        Some("for (let i = 0; i < 3; i++) browser.call('browser_navigate', {url: 'https://a.test/'}); 1"),
    );
    assert!(!output.status.success());
    assert!(String::from_utf8_lossy(&output.stderr).contains("browser_call_budget_exceeded"));

    let output = maho(dir.path(), &browser, &["browser", "script", "/nonexistent/x.js"], None);
    assert!(!output.status.success());
}

#[test]
fn skills_install_ships_validated_site_packs() {
    let dir = tempfile::tempdir().unwrap();
    let browser = fake_browser(dir.path());
    let output = maho(dir.path(), &browser, &["skills", "install", "--agent", "agents"], None);
    assert!(output.status.success(), "{}", String::from_utf8_lossy(&output.stderr));
    let sites = dir.path().join(".agents/skills/maho-browser/sites");
    for file in ["github/manifest.json", "google/web_search.js", "x/compose_post.js"] {
        assert!(sites.join(file).is_file(), "{file}");
    }
}
