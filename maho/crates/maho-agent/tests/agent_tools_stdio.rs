use std::io::{BufRead, BufReader, Write};
use std::path::{Path, PathBuf};
use std::process::{Child, ChildStdin, ChildStdout, Command, Stdio};
use std::time::{Duration, Instant};

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
        elif name == "browser_snapshot":
            payload = {"url": url, "title": "Fake " + url, "nodes": [{"role": "heading", "name": "Hello"}]}
        else:
            result = {"content": [{"type": "text", "text": "unknown tool " + name}], "isError": True}
            print(json.dumps({"jsonrpc": "2.0", "id": msg["id"], "result": result}), flush=True)
            continue
        result = {"content": [{"type": "text", "text": json.dumps(payload)}], "isError": False}
    else:
        result = {}
    print(json.dumps({"jsonrpc": "2.0", "id": msg["id"], "result": result}), flush=True)
"#;

struct Server {
    child: Child,
    stdin: ChildStdin,
    stdout: BufReader<ChildStdout>,
    next_id: u64,
}

impl Server {
    fn start(artifact_dir: &Path, browser_mcp: Option<&Path>) -> Self {
        let mut command = Command::new(env!("CARGO_BIN_EXE_maho-agent-tools"));
        command
            .env("MAHO_AGENT_TOOLS_ARTIFACT_DIR", artifact_dir)
            .env_remove("MAHO_BROWSER_MCP_BINARY")
            .stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .stderr(Stdio::inherit());
        if let Some(binary) = browser_mcp {
            command.env("MAHO_BROWSER_MCP_BINARY", binary);
        }
        let mut child = command.spawn().expect("spawn maho-agent-tools");
        let stdin = child.stdin.take().unwrap();
        let stdout = BufReader::new(child.stdout.take().unwrap());
        let mut server = Self {
            child,
            stdin,
            stdout,
            next_id: 0,
        };
        let init = server.request("initialize", json!({"protocolVersion": "2025-03-26", "capabilities": {}, "clientInfo": {"name": "test", "version": "0"}}));
        assert_eq!(init["result"]["serverInfo"]["name"], "maho-agent-tools");
        server
    }

    fn request(&mut self, method: &str, params: Value) -> Value {
        self.next_id += 1;
        let line = json!({"jsonrpc": "2.0", "id": self.next_id, "method": method, "params": params});
        writeln!(self.stdin, "{line}").unwrap();
        self.stdin.flush().unwrap();
        let mut response = String::new();
        self.stdout.read_line(&mut response).unwrap();
        serde_json::from_str(&response).expect("json-rpc response")
    }

    fn call(&mut self, name: &str, arguments: Value) -> Value {
        self.request("tools/call", json!({"name": name, "arguments": arguments}))["result"].clone()
    }
}

impl Drop for Server {
    fn drop(&mut self) {
        let _ = self.child.kill();
        let _ = self.child.wait();
    }
}

fn fake_browser(dir: &Path) -> PathBuf {
    let path = dir.join("fake-browser-mcp");
    std::fs::write(&path, FAKE_BROWSER_MCP).unwrap();
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        std::fs::set_permissions(&path, std::fs::Permissions::from_mode(0o755)).unwrap();
    }
    path
}

#[test]
fn lists_and_runs_js_eval_artifact_and_browser_script() {
    let dir = tempfile::tempdir().unwrap();
    let artifacts = dir.path().join("artifacts");
    let browser = fake_browser(dir.path());
    let mut server = Server::start(&artifacts, Some(&browser));

    let tools = server.request("tools/list", json!({}));
    let names: Vec<&str> = tools["result"]["tools"]
        .as_array()
        .unwrap()
        .iter()
        .map(|tool| tool["name"].as_str().unwrap())
        .collect();
    for expected in ["js_eval", "artifact_create", "browser_script", "web_search", "image_search"] {
        assert!(names.contains(&expected), "{expected} missing from {names:?}");
    }

    let eval = server.call("js_eval", json!({"code": "1+1"}));
    assert_eq!(eval["isError"], false);
    assert_eq!(eval["structuredContent"]["result"], 2);

    let artifact = server.call(
        "artifact_create",
        json!({"kind": "xlsx", "filename": "report.xlsx", "content": {"sheets": [{"name": "S", "rows": [["a", "b"], [1, 2]]}]}}),
    );
    assert_eq!(artifact["isError"], false, "{artifact}");
    let path = PathBuf::from(artifact["structuredContent"]["path"].as_str().unwrap());
    assert!(path.starts_with(&artifacts));
    assert!(std::fs::metadata(&path).unwrap().len() > 0);

    let script = server.call(
        "browser_script",
        json!({
            "script": "browser.call('browser_navigate', {url: input.url}); const snap = browser.call('browser_snapshot'); ({title: snap.title, heading: snap.nodes[0].name})",
            "input": {"url": "https://example.test/"}
        }),
    );
    assert_eq!(script["isError"], false, "{script}");
    assert_eq!(
        script["structuredContent"],
        json!({"title": "Fake https://example.test/", "heading": "Hello"})
    );
}

#[test]
fn failures_are_typed_and_bounded() {
    let dir = tempfile::tempdir().unwrap();
    let browser = fake_browser(dir.path());
    let mut server = Server::start(&dir.path().join("artifacts"), Some(&browser));

    let started = Instant::now();
    let looped = server.call("js_eval", json!({"code": "while (true) {}"}));
    assert!(started.elapsed() < Duration::from_secs(10));
    assert_eq!(looped["isError"], true);
    assert_eq!(looped["structuredContent"]["error"]["code"], "timeout");

    let over_budget = server.call(
        "browser_script",
        json!({"script": "for (let i = 0; i < 5; i++) browser.call('browser_snapshot'); 'done'", "max_browser_calls": 3}),
    );
    assert_eq!(over_budget["isError"], true);
    assert_eq!(
        over_budget["structuredContent"]["error"]["code"],
        "browser_call_budget_exceeded"
    );

    for tool in ["web_search", "image_search"] {
        let search = server.call(tool, json!({"query": "maho browser"}));
        assert_eq!(search["isError"], true);
        assert_eq!(search["structuredContent"]["error"]["code"], "search_unavailable");
        let text = search.to_string();
        assert!(!text.contains("example-free.org") && !text.contains("example-premium.com"));
    }

    let escape = server.call(
        "artifact_create",
        json!({"kind": "pdf", "filename": "../escape.pdf", "content": {"blocks": []}}),
    );
    assert_eq!(escape["structuredContent"]["error"]["code"], "artifact_failed");
    assert!(!dir.path().join("escape.pdf").exists());

    let unknown = server.call("browser_script", json!({"script": "browser.call('nope')"}));
    assert_eq!(unknown["structuredContent"]["error"]["code"], "browser_call_failed");
}

#[test]
fn browser_script_without_browser_is_unavailable() {
    let dir = tempfile::tempdir().unwrap();
    let mut server = Server::start(&dir.path().join("artifacts"), None);
    let result = server.call("browser_script", json!({"script": "browser.call('browser_snapshot')"}));
    assert_eq!(result["structuredContent"]["error"]["code"], "browser_unavailable");
    let eval = server.call("js_eval", json!({"code": "[1,2].map(x => x * 3)"}));
    assert_eq!(eval["structuredContent"]["result"], json!([3, 6]));
}
