#![cfg(unix)]
//! Proves the omo-ai plugin wiring end to end: Maho's real `OmoBackend` writes
//! `settings.json` with the plugin package, launches the real senpi RPC host with
//! Maho's argv, and the model is offered the memory engine. A scripted fake provider
//! then calls the `memory` tool and the test checks the git-backed memory file landed
//! under `<agent_dir>/memory`.
//!
//! Needs bun, the senpi package and the omo-ai plugin: `MAHO_OMO_BUN`,
//! `MAHO_OMO_RPC_ENTRY`, `MAHO_OMO_PLUGIN_DIR` (defaults: the global bun install). The
//! test is `#[ignore]`d like `omo_bundled_runtime_live.rs`; run it with `--ignored`, and
//! it fails (not skips) when a piece is missing.

use std::path::{Path, PathBuf};
use std::sync::{Arc, Mutex};
use std::time::Duration;

use maho_agent::omo::backend::OmoBackend;
use maho_agent::omo::config::{CustomProvider, OmoLaunchConfig};
use maho_agent::AgentRuntime;
use maho_types::chat::{ChatContent, ChatMessage};
use tokio::io::{AsyncReadExt, AsyncWriteExt};

fn home() -> PathBuf {
    PathBuf::from(std::env::var("HOME").expect("HOME"))
}

fn runtime() -> (PathBuf, PathBuf, PathBuf) {
    let bun = std::env::var("MAHO_OMO_BUN")
        .map(PathBuf::from)
        .unwrap_or_else(|_| home().join(".bun/bin/bun"));
    let global = home().join(".bun/install/global/node_modules");
    let entry = std::env::var("MAHO_OMO_RPC_ENTRY")
        .map(PathBuf::from)
        .unwrap_or_else(|_| global.join("@code-yeongyu/senpi/dist/rpc-entry.js"));
    let plugin = std::env::var("MAHO_OMO_PLUGIN_DIR")
        .map(PathBuf::from)
        .unwrap_or_else(|_| global.join("omo-ai/plugin"));
    for path in [&bun, &entry, &plugin.join("extensions/omo.js")] {
        assert!(path.exists(), "live omo plugin test needs {}", path.display());
    }
    (bun, entry, plugin)
}

fn sse(chunks: &[serde_json::Value]) -> String {
    let mut body = String::new();
    for chunk in chunks {
        body.push_str(&format!("data: {chunk}\n\n"));
    }
    body.push_str("data: [DONE]\n\n");
    body
}

fn chunk(delta: serde_json::Value, finish: Option<&str>) -> serde_json::Value {
    serde_json::json!({
        "id": "chatcmpl-fake",
        "object": "chat.completion.chunk",
        "created": 0,
        "model": "fake-model",
        "choices": [{"index": 0, "delta": delta, "finish_reason": finish}]
    })
}

async fn scripted_provider(
    responses: Vec<String>,
) -> (String, Arc<Mutex<Vec<serde_json::Value>>>) {
    let listener = tokio::net::TcpListener::bind("127.0.0.1:0").await.unwrap();
    let address = listener.local_addr().unwrap();
    let seen = Arc::new(Mutex::new(Vec::new()));
    let seen_task = seen.clone();
    tokio::spawn(async move {
        let mut index = 0usize;
        while let Ok((mut stream, _)) = listener.accept().await {
            let body = responses[index.min(responses.len() - 1)].clone();
            index += 1;
            let seen = seen_task.clone();
            tokio::spawn(async move {
                let request = read_request(&mut stream).await;
                if let Some(json) = request {
                    seen.lock().unwrap().push(json);
                }
                let response = format!(
                    "HTTP/1.1 200 OK\r\ncontent-type: text/event-stream\r\ncontent-length: {}\r\nconnection: close\r\n\r\n{body}",
                    body.len()
                );
                let _ = stream.write_all(response.as_bytes()).await;
            });
        }
    });
    (format!("http://{address}/v1"), seen)
}

async fn read_request(stream: &mut tokio::net::TcpStream) -> Option<serde_json::Value> {
    let mut buffer = Vec::new();
    let mut chunk = vec![0u8; 64 * 1024];
    loop {
        let n = stream.read(&mut chunk).await.ok()?;
        if n == 0 {
            return None;
        }
        buffer.extend_from_slice(&chunk[..n]);
        let text = String::from_utf8_lossy(&buffer);
        if let Some(split) = text.find("\r\n\r\n") {
            let length = text[..split]
                .lines()
                .find_map(|line| {
                    let lower = line.to_ascii_lowercase();
                    lower
                        .strip_prefix("content-length:")
                        .map(|v| v.trim().parse::<usize>().unwrap_or(0))
                })
                .unwrap_or(0);
            if buffer.len() >= split + 4 + length {
                return serde_json::from_slice(&buffer[split + 4..split + 4 + length]).ok();
            }
        }
    }
}

fn tool_names(request: &serde_json::Value) -> Vec<String> {
    request["tools"]
        .as_array()
        .map(|tools| {
            tools
                .iter()
                .filter_map(|t| t["function"]["name"].as_str().map(str::to_string))
                .collect()
        })
        .unwrap_or_default()
}

fn find_file_containing(root: &Path, needle: &str) -> Option<PathBuf> {
    let entries = std::fs::read_dir(root).ok()?;
    for entry in entries.flatten() {
        let path = entry.path();
        if path.file_name().is_some_and(|n| n == ".git") {
            continue;
        }
        if path.is_dir() {
            if let Some(found) = find_file_containing(&path, needle) {
                return Some(found);
            }
        } else if std::fs::read_to_string(&path).is_ok_and(|t| t.contains(needle)) {
            return Some(path);
        }
    }
    None
}

#[tokio::test(flavor = "multi_thread")]
#[ignore = "needs bun, senpi and the omo-ai plugin; run with --ignored"]
async fn plugin_exposes_memory_engine_and_memory_tool_writes_repo() {
    let (bun, entry, plugin) = runtime();
    let scratch = tempfile::tempdir().unwrap();
    let agent_dir = scratch.path().join("agent");
    std::fs::create_dir_all(&agent_dir).unwrap();

    let memory_args = serde_json::json!({
        "command": "create",
        "reason": "parity probe",
        "file_path": "reference/parity-probe.md",
        "description": "parity probe",
        "file_text": "parity probe fact zebra-42"
    })
    .to_string();
    let tool_call = sse(&[
        chunk(
            serde_json::json!({"role": "assistant", "tool_calls": [{
                "index": 0, "id": "call_1", "type": "function",
                "function": {"name": "memory", "arguments": memory_args}
            }]}),
            None,
        ),
        chunk(serde_json::json!({}), Some("tool_calls")),
    ]);
    let done = sse(&[
        chunk(serde_json::json!({"role": "assistant", "content": "saved"}), None),
        chunk(serde_json::json!({}), Some("stop")),
    ]);
    let (base_url, seen) = scripted_provider(vec![tool_call, done]).await;

    let config = OmoLaunchConfig::builder(&agent_dir)
        .runtime_binary(bun)
        .rpc_entry(entry)
        .plugin_dir(&plugin)
        .socket_path(scratch.path().join("rpc.sock"))
        .custom_provider(CustomProvider {
            name: "fake".to_string(),
            base_url,
            api_key: "fake-key".to_string(),
            models: vec!["fake-model".to_string()],
        })
        .provider("fake")
        .model_id("fake-model")
        .build()
        .expect("config builds");
    let backend = OmoBackend::new(config, scratch.path().join("maho-browser-mcp"), scratch.path());

    let reply = tokio::time::timeout(
        Duration::from_secs(120),
        backend.run_turn(
            "memory-probe",
            ChatMessage::user(ChatContent::text("remember the parity probe")),
            None,
            None,
        ),
    )
    .await
    .expect("turn must reach a terminal event");
    reply.expect("turn succeeds");

    let settings: serde_json::Value =
        serde_json::from_slice(&std::fs::read(agent_dir.join("settings.json")).unwrap()).unwrap();
    assert_eq!(settings["packages"][0]["source"], plugin.to_string_lossy().as_ref());

    let requests = seen.lock().unwrap().clone();
    let first = requests.first().expect("omo called the provider");
    let names = tool_names(first);
    for wanted in ["memory", "task"] {
        assert!(names.iter().any(|n| n == wanted), "missing {wanted}: {names:?}");
    }
    assert!(
        !names.iter().any(|n| n.starts_with("lsp_")),
        "lsp tools must be excluded: {names:?}"
    );
    let prompt = first.to_string();
    assert!(prompt.contains("senpi-memory:"), "memory block missing from system prompt");

    let memory_root = agent_dir.join("memory").join("agents");
    let repos: Vec<PathBuf> = std::fs::read_dir(&memory_root)
        .unwrap_or_else(|e| panic!("no memory agents dir {}: {e}", memory_root.display()))
        .flatten()
        .map(|entry| entry.path().join("repo"))
        .filter(|repo| repo.is_dir())
        .collect();
    let written = repos
        .iter()
        .find_map(|repo| find_file_containing(repo, "zebra-42"))
        .unwrap_or_else(|| panic!("memory tool wrote nothing into {repos:?}"));
    assert!(written.ends_with("reference/parity-probe.md"), "{}", written.display());
}

#[tokio::test(flavor = "multi_thread")]
#[ignore = "needs bun, senpi and the omo-ai plugin; run with --ignored"]
async fn omo_dispatches_js_eval_to_the_staged_agent_tools_server() {
    let (bun, entry, plugin) = runtime();
    let scratch = tempfile::tempdir().unwrap();
    let agent_dir = scratch.path().join("agent");
    std::fs::create_dir_all(&agent_dir).unwrap();
    let helpers = scratch.path().join("helpers");
    std::fs::create_dir_all(&helpers).unwrap();
    std::fs::write(helpers.join("maho-browser-mcp"), "#!/bin/sh\nexit 1\n").unwrap();
    {
        use std::os::unix::fs::PermissionsExt;
        std::fs::set_permissions(
            helpers.join("maho-browser-mcp"),
            std::fs::Permissions::from_mode(0o755),
        )
        .unwrap();
    }
    std::os::unix::fs::symlink(
        env!("CARGO_BIN_EXE_maho-agent-tools"),
        helpers.join(maho_agent::omo::backend::AGENT_TOOLS_BINARY),
    )
    .unwrap();

    let call = sse(&[
        chunk(
            serde_json::json!({"role": "assistant", "tool_calls": [{
                "index": 0, "id": "call_1", "type": "function",
                "function": {"name": "mcp_maho-agent-tools_js_eval", "arguments": "{\"code\":\"6*7\"}"}
            }]}),
            None,
        ),
        chunk(serde_json::json!({}), Some("tool_calls")),
    ]);
    let done = sse(&[
        chunk(serde_json::json!({"role": "assistant", "content": "done"}), None),
        chunk(serde_json::json!({}), Some("stop")),
    ]);
    let (base_url, seen) = scripted_provider(vec![call, done]).await;
    let config = OmoLaunchConfig::builder(&agent_dir)
        .runtime_binary(bun)
        .rpc_entry(entry)
        .plugin_dir(&plugin)
        .socket_path(scratch.path().join("rpc.sock"))
        .custom_provider(CustomProvider {
            name: "fake".to_string(),
            base_url,
            api_key: "fake-key".to_string(),
            models: vec!["fake-model".to_string()],
        })
        .provider("fake")
        .model_id("fake-model")
        .build()
        .expect("config builds");
    let backend = OmoBackend::new(config, helpers.join("maho-browser-mcp"), scratch.path());
    tokio::time::timeout(
        Duration::from_secs(120),
        backend.run_turn(
            "tools-probe",
            ChatMessage::user(ChatContent::text("compute")),
            None,
            None,
        ),
    )
    .await
    .expect("turn must reach a terminal event")
    .expect("turn succeeds");

    let requests = seen.lock().unwrap().clone();
    let names = tool_names(requests.first().expect("omo called the provider"));
    for wanted in ["js_eval", "artifact_create", "browser_script"] {
        let qualified = format!("mcp_maho-agent-tools_{wanted}");
        assert!(names.contains(&qualified), "missing {qualified}: {names:?}");
    }
    let second = requests.get(1).expect("omo sent the tool result back").to_string();
    assert!(second.contains("42"), "js_eval result missing from follow-up request: {second}");
}
