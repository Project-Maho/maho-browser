// Model Context Protocol Client
use anyhow::Result;
use maho_types::ai::{AiMcpServer, McpTransport};
use maho_types::tool::{ToolDescriptor, ToolPermission, ToolProvenance};
use rmcp::model::{CallToolRequestParams, ClientInfo, ProtocolVersion};
use rmcp::service::RunningService;
use rmcp::transport::IntoTransport;
use rmcp::{Peer, RoleClient};
use serde_json::Value;
use std::ops::Deref;
use std::sync::{Arc, OnceLock};
use std::time::Duration;
use tokio::runtime::Runtime;

fn get_mcp_runtime() -> Result<&'static Runtime> {
    static RUNTIME: OnceLock<Result<Runtime, String>> = OnceLock::new();
    let res = RUNTIME.get_or_init(|| {
        tokio::runtime::Builder::new_multi_thread()
            .worker_threads(2)
            .thread_name("mcp-worker-pool")
            .enable_all()
            .build()
            .map_err(|e| format!("Failed to build MCP dedicated runtime: {e}"))
    });
    match res {
        Ok(rt) => Ok(rt),
        Err(e) => Err(anyhow::anyhow!("{e}")),
    }
}

/// Decide the *candidate* default permission for tools exposed over a UDS server.
///
/// Only the browser's OWN dogfood socket (the in-process/UDS browser server,
/// resolved via [`maho_browser_mcp::paths::default_socket_path`]) is a candidate
/// for `AutoApprove`; it is lease/firewall-protected by the browser. Arbitrary
/// user-imported UDS servers lack that protection and must remain `AlwaysAsk`.
///
/// SECURITY (FIX-9b): matching the socket *path* is necessary but NOT sufficient.
/// The `MAHO_MCP_SOCKET_PATH` env override means the "default" path can be pointed
/// at an attacker-controlled socket, and any local process can bind the canonical
/// path if the browser is not running. Therefore `AutoApprove` returned here is
/// only a candidate: [`McpClient::connect`] additionally verifies the connected
/// UDS *peer* (same-uid, best-effort browser-executable check) via
/// [`verify_uds_peer`] and fail-closed downgrades to `AlwaysAsk` if the peer is
/// not a trustworthy same-user Maho browser process. Callers that need the
/// *effective* permission (post peer verification) must use
/// [`McpClient::resolved_permission`], not this function alone.
///
/// SECURITY (FIX-9a): even when the effective permission is `AutoApprove`, a
/// tool may auto-run only when browser-authoritative discovery metadata proves
/// it read-only. Missing or malformed policy metadata remains sensitive.
pub fn resolve_uds_permission(
    transport: McpTransport,
    socket_path: Option<&str>,
) -> ToolPermission {
    let is_browser_dogfood = transport == McpTransport::Uds
        && socket_path.is_some_and(|p| {
            std::path::Path::new(p) == maho_browser_mcp::paths::default_socket_path()
        });

    if is_browser_dogfood {
        ToolPermission::AutoApprove
    } else {
        ToolPermission::AlwaysAsk
    }
}

/// Return whether browser-authoritative discovery metadata proves a dogfood
/// MCP tool read-only. The load-bearing fields must be present with a safe
/// value; unknown, missing, or malformed policy remains sensitive (fail closed).
///
/// `requiresApproval` and `permission` are treated as OPTIONAL negative
/// assertions: the browser does not publish them (measured: 0 of 48 tools on a
/// live `tools/list`), so demanding them made this predicate return false for
/// every browser tool and silently disabled the dogfood auto-approve path.
/// Absent means "not asserted" and does not disqualify; an explicitly unsafe
/// value still does.
///
/// `sensitivity` IS published on every tool and is therefore REQUIRED to be
/// `"low"`. Read-only does not imply safe to auto-run: on the live catalog 26
/// tools are read-only but 16 are `"sensitive"` (page content, history,
/// bookmarks, mail readers) and `mail_extract_otp` is `"credential"`. Auto-
/// running those would exfiltrate PII and one-time passcodes without a prompt.
fn discovered_tool_is_readonly(tool: &Value) -> bool {
    let annotations_read_only = tool
        .pointer("/annotations/readOnlyHint")
        .and_then(Value::as_bool)
        == Some(true);
    let policy = tool.pointer("/_meta/policy").and_then(Value::as_object);

    annotations_read_only
        && policy.is_some_and(|policy| {
            let requires_approval_is_safe = policy
                .get("requiresApproval")
                .is_none_or(|value| value.as_bool() == Some(false));
            let permission_is_safe = policy
                .get("permission")
                .is_none_or(|value| value.as_str() == Some("auto_approve"));

            policy.get("mutability").and_then(Value::as_str) == Some("read_only")
                && policy.get("changesAuthority").and_then(Value::as_bool) == Some(false)
                && policy.get("sensitivity").and_then(Value::as_str) == Some("low")
                && requires_approval_is_safe
                && permission_is_safe
        })
}

/// Best-effort check that a peer-process executable path belongs to the Maho
/// browser. Case-insensitive substring match on `maho` — the real macOS bundle
/// path is `.../Maho.app/Contents/MacOS/Maho` and Linux/dev builds live under a
/// `maho`-named tree. Pure/side-effect-free so it is unit-testable.
#[cfg(unix)]
fn exe_path_is_trusted_browser(path: &str) -> bool {
    path.to_ascii_lowercase().contains("maho")
}

/// Verify that the connected UDS peer is a trustworthy same-user Maho browser
/// process before honoring a dogfood `AutoApprove`.
///
/// FIX-9b (#4): trust the *peer*, not the path. Mandatory baseline is a same-uid
/// check (portable via `tokio::net::UnixStream::peer_cred`, backed by
/// `SO_PEERCRED` on Linux / `LOCAL_PEERCRED` on macOS). A best-effort executable
/// check (`proc_pidpath` on macOS, `/proc/<pid>/exe` on Linux) additionally
/// requires the peer binary to look like the Maho browser, but — per spec — does
/// NOT hard-fail when proc introspection is unavailable (then same-uid alone
/// suffices). Returns `Err(reason)` when trust cannot be established; the caller
/// fail-closed downgrades to `AlwaysAsk`.
#[cfg(unix)]
fn verify_uds_peer(stream: &tokio::net::UnixStream) -> std::result::Result<(), String> {
    let cred = stream
        .peer_cred()
        .map_err(|e| format!("peer_cred() unavailable: {e}"))?;

    // Mandatory: same-uid.
    let current_uid = unsafe { libc::getuid() };
    if cred.uid() != current_uid {
        return Err(format!(
            "peer uid {} != current uid {} (cross-user socket)",
            cred.uid(),
            current_uid
        ));
    }

    // Best-effort: peer executable path looks like the Maho browser. Only
    // downgrade on a POSITIVE mismatch; if the path cannot be resolved, rely on
    // the same-uid baseline (do not hard-fail).
    if let Some(pid) = cred.pid() {
        if let Some(exe) = peer_exe_path(pid) {
            if !exe_path_is_trusted_browser(&exe) {
                return Err(format!(
                    "peer pid {pid} exe {exe:?} does not look like the Maho browser"
                ));
            }
            tracing::debug!(
                target: "maho_agent::mcp_client",
                peer_uid = cred.uid(),
                peer_pid = pid,
                peer_exe = %exe,
                "UDS peer verified (same-uid + browser exe)"
            );
        } else {
            tracing::debug!(
                target: "maho_agent::mcp_client",
                peer_uid = cred.uid(),
                peer_pid = pid,
                "UDS peer verified (same-uid); exe path unavailable, best-effort exe check skipped"
            );
        }
    } else {
        tracing::debug!(
            target: "maho_agent::mcp_client",
            peer_uid = cred.uid(),
            "UDS peer verified (same-uid); peer pid unavailable, best-effort exe check skipped"
        );
    }

    Ok(())
}

/// Resolve the executable path of a peer process, best-effort and per-OS.
#[cfg(target_os = "macos")]
fn peer_exe_path(pid: i32) -> Option<String> {
    // proc_pidpath fills an absolute path; PROC_PIDPATHINFO_MAXSIZE == 4 * MAXPATHLEN.
    const BUF_LEN: usize = 4 * 1024;
    let mut buf = vec![0u8; BUF_LEN];
    let n =
        unsafe { libc::proc_pidpath(pid, buf.as_mut_ptr() as *mut libc::c_void, BUF_LEN as u32) };
    if n <= 0 {
        return None;
    }
    buf.truncate(n as usize);
    String::from_utf8(buf).ok()
}

/// Resolve the executable path of a peer process, best-effort and per-OS.
#[cfg(target_os = "linux")]
fn peer_exe_path(pid: i32) -> Option<String> {
    std::fs::read_link(format!("/proc/{pid}/exe"))
        .ok()
        .map(|p| p.to_string_lossy().into_owned())
}

/// Fallback for other unix targets where proc introspection is unavailable.
#[cfg(all(unix, not(any(target_os = "macos", target_os = "linux"))))]
fn peer_exe_path(_pid: i32) -> Option<String> {
    None
}

pub struct McpClient {
    workspace_id: String,
    server_name: String,
    peer: Peer<RoleClient>,
    client: Option<RunningService<RoleClient, ClientInfo>>,
    browser_client: Option<Arc<maho_browser_mcp::client::BrowserClient>>,
    timeout: Duration,
    output_cap: usize,
    /// Effective permission after FIX-9b peer verification. For UDS this may be
    /// a fail-closed downgrade of [`resolve_uds_permission`]'s path-based verdict.
    resolved_permission: ToolPermission,
}

fn apply_output_cap(mut val: Value, output_cap: usize) -> Result<Value> {
    let mut truncated_inner = false;
    if let Some(content) = val.get_mut("content") {
        if let Some(arr) = content.as_array_mut() {
            if let Some(first) = arr.first_mut() {
                if let Some(text_val) = first.get_mut("text") {
                    if let Some(text) = text_val.as_str() {
                        if text.len() > output_cap {
                            let mut end = output_cap;
                            while end > 0 && !text.is_char_boundary(end) {
                                end -= 1;
                            }
                            let truncated = format!("{}... [output truncated]", &text[..end]);
                            *text_val = Value::String(truncated);
                            truncated_inner = true;
                        }
                    }
                }
            }
        }
    }

    if !truncated_inner {
        let mut val_str = serde_json::to_string(&val)?;
        if val_str.len() > output_cap {
            let mut end = output_cap;
            while end > 0 && !val_str.is_char_boundary(end) {
                end -= 1;
            }
            val_str.truncate(end);
            return Ok(Value::String(format!("{}... [output truncated]", val_str)));
        }
    }

    Ok(val)
}

impl McpClient {
    pub async fn connect(config: &AiMcpServer, auth_token: Option<String>) -> Result<Self> {
        let config_clone = config.clone();

        let handle = get_mcp_runtime()?
            .spawn(async move { Self::connect_internal(&config_clone, auth_token).await });

        match handle.await? {
            Ok(client) => {
                maho_types::events::core_update::emit_core_update(
                    maho_types::events::core_update::CoreUpdate::ToolAvailabilityChanged {
                        workspace_id: config.workspace_id.clone(),
                        server_name: config.name.clone(),
                        available: true,
                    },
                );
                Ok(client)
            }
            Err(e) => {
                maho_types::events::core_update::emit_core_update(
                    maho_types::events::core_update::CoreUpdate::ToolAvailabilityChanged {
                        workspace_id: config.workspace_id.clone(),
                        server_name: config.name.clone(),
                        available: false,
                    },
                );
                Err(e)
            }
        }
    }

    async fn connect_internal(config: &AiMcpServer, auth_token: Option<String>) -> Result<Self> {
        let timeout = Duration::from_millis(config.timeout_ms.min(60_000));
        let output_cap = config.output_cap_bytes;
        let server_name = config.name.clone();

        match config.transport {
            McpTransport::Stdio => {
                let command_str = config
                    .command
                    .as_ref()
                    .ok_or_else(|| anyhow::anyhow!("Command is required for stdio transport"))?;

                let mut parts: Vec<String> = if command_str.trim().starts_with('[') {
                    serde_json::from_str(command_str)
                        .unwrap_or_else(|_| vec![command_str.to_string()])
                } else {
                    command_str
                        .split_whitespace()
                        .map(|s| s.to_string())
                        .collect()
                };

                if parts.is_empty() {
                    return Err(anyhow::anyhow!("Command parts cannot be empty"));
                }

                let exec = parts.remove(0);
                let mut tokio_cmd = tokio::process::Command::new(exec);
                tokio_cmd.args(&parts);

                #[cfg(unix)]
                {
                    unsafe {
                        tokio_cmd.pre_exec(|| {
                            libc::setsid();
                            Ok(())
                        });
                    }
                }

                tokio_cmd.current_dir(std::env::temp_dir());

                let child_transport =
                    rmcp::transport::TokioChildProcess::new(tokio_cmd).map_err(|e| {
                        anyhow::anyhow!("Failed to build child process transport: {:?}", e)
                    })?;

                let client = rmcp::service::serve_client(ClientInfo::default(), child_transport)
                    .await
                    .map_err(|e| anyhow::anyhow!("Failed to start MCP stdio client: {:?}", e))?;

                let peer = client.deref().clone();

                Ok(Self {
                    workspace_id: config.workspace_id.clone(),
                    server_name,
                    peer,
                    client: Some(client),
                    browser_client: None,
                    timeout,
                    output_cap,
                    resolved_permission: ToolPermission::AlwaysAsk,
                })
            }
            McpTransport::Http => {
                let url_str = config
                    .url
                    .as_ref()
                    .ok_or_else(|| anyhow::anyhow!("URL is required for HTTP transport"))?;

                let mut transport_config = rmcp::transport::streamable_http_client::StreamableHttpClientTransportConfig::with_uri(url_str.as_str());
                if let Some(token) = auth_token {
                    transport_config = transport_config.auth_header(token);
                }

                let http_transport =
                    rmcp::transport::StreamableHttpClientTransport::from_config(transport_config);
                let client = rmcp::service::serve_client(ClientInfo::default(), http_transport)
                    .await
                    .map_err(|e| anyhow::anyhow!("Failed to start MCP HTTP client: {:?}", e))?;

                let peer = client.deref().clone();

                Ok(Self {
                    workspace_id: config.workspace_id.clone(),
                    server_name,
                    peer,
                    client: Some(client),
                    browser_client: None,
                    timeout,
                    output_cap,
                    resolved_permission: ToolPermission::AlwaysAsk,
                })
            }
            McpTransport::Uds => {
                #[cfg(not(unix))]
                {
                    let _ = &config;
                    anyhow::bail!(
                        "UDS transport is not supported on this platform (use named-pipe transport)"
                    );
                }
                #[cfg(unix)]
                {
                    let socket_path = config.socket_path.as_ref().ok_or_else(|| {
                        anyhow::anyhow!("Socket path is required for UDS transport")
                    })?;
                    tracing::debug!(
                        target: "maho_agent::mcp_client",
                        server = %server_name,
                        socket_path = %socket_path,
                        "McpClient::connect UDS starting"
                    );
                    let stream = tokio::net::UnixStream::connect(socket_path)
                        .await
                        .map_err(|e| anyhow::anyhow!("Failed to connect to UDS: {:?}", e))?;

                    // FIX-9b (#4): trust the peer, not just the path. If the socket
                    // path matches the dogfood default (candidate AutoApprove), verify
                    // the connected peer is a same-user Maho browser process; on
                    // failure fail-closed to AlwaysAsk so an env-redirected or
                    // path-squatting socket cannot inherit auto-approval.
                    let candidate_permission =
                        resolve_uds_permission(config.transport, config.socket_path.as_deref());
                    let resolved_permission = if candidate_permission == ToolPermission::AutoApprove
                    {
                        match verify_uds_peer(&stream) {
                            Ok(()) => ToolPermission::AutoApprove,
                            Err(reason) => {
                                tracing::warn!(
                                    target: "maho_agent::mcp_client",
                                    server = %server_name,
                                    socket_path = %socket_path,
                                    %reason,
                                    "UDS peer verification failed; downgrading AutoApprove -> AlwaysAsk (fail-closed)"
                                );
                                ToolPermission::AlwaysAsk
                            }
                        }
                    } else {
                        candidate_permission
                    };

                    let client_info =
                        ClientInfo::default().with_protocol_version(ProtocolVersion::V_2025_03_26);
                    let client = rmcp::service::serve_client(
                        client_info,
                        IntoTransport::into_transport(stream),
                    )
                    .await
                    .map_err(|e| anyhow::anyhow!("Failed to start MCP UDS client: {:?}", e))?;

                    let peer = client.deref().clone();
                    tracing::debug!(
                        target: "maho_agent::mcp_client",
                        server = %server_name,
                        socket_path = %socket_path,
                        permission = ?resolved_permission,
                        "McpClient::connect UDS succeeded"
                    );
                    let browser_client = if resolved_permission == ToolPermission::AutoApprove {
                        Some(Arc::new(
                            maho_browser_mcp::client::BrowserClient::connect(std::path::Path::new(
                                socket_path,
                            ))
                            .await?,
                        ))
                    } else {
                        None
                    };

                    Ok(Self {
                        workspace_id: config.workspace_id.clone(),
                        server_name,
                        peer,
                        client: Some(client),
                        browser_client,
                        timeout,
                        output_cap,
                        resolved_permission,
                    })
                }
            }
        }
    }

    pub async fn list_tools(&self) -> Result<Vec<ToolDescriptor>> {
        if let Some(browser_client) = self.browser_client.clone() {
            let server_name = self.server_name.clone();
            let permission = self.resolved_permission;
            let handle = get_mcp_runtime()?.spawn(async move {
                let result = browser_client
                    .call("tools/list", serde_json::json!({}))
                    .await?;
                let tools = result
                    .get("tools")
                    .and_then(|tools| tools.as_array())
                    .ok_or_else(|| anyhow::anyhow!("tools/list result missing tools array"))?;

                if tools.is_empty() {
                    return Err(anyhow::anyhow!("server exposes no callable tools"));
                }

                let tool_names: Vec<String> = tools
                    .iter()
                    .filter_map(|tool| tool.get("name").and_then(|name| name.as_str()))
                    .map(|name| format!("mcp:{}/{}", server_name, name))
                    .collect();
                tracing::debug!(
                    target: "maho_agent::mcp_client",
                    server = %server_name,
                    tool_count = tools.len(),
                    permission = ?permission,
                    names = ?tool_names,
                    "McpClient::list_tools aggregated"
                );

                tools
                    .iter()
                    .map(|tool| {
                        let raw_name = tool
                            .get("name")
                            .and_then(|name| name.as_str())
                            .ok_or_else(|| anyhow::anyhow!("tool missing name"))?;
                        let description = tool
                            .get("description")
                            .and_then(|description| description.as_str())
                            .unwrap_or_default()
                            .to_string();
                        let parameters_schema = tool.get("inputSchema").cloned().unwrap_or_else(
                            || serde_json::json!({ "type": "object", "properties": {} }),
                        );
                        let sensitive = permission != ToolPermission::AutoApprove
                            || !discovered_tool_is_readonly(tool);

                        Ok(ToolDescriptor {
                            name: format!("mcp:{}/{}", server_name, raw_name),
                            description,
                            parameters_schema,
                            provenance: ToolProvenance::External(server_name.clone()),
                            sensitive,
                            permission,
                        })
                    })
                    .collect::<Result<Vec<_>>>()
            });

            return handle.await?;
        }

        let peer = self.peer.clone();
        let timeout = self.timeout;
        let server_name = self.server_name.clone();

        let permission = self.resolved_permission;

        let handle = get_mcp_runtime()?.spawn(async move {
            let fut = peer.list_all_tools();
            let tools = tokio::time::timeout(timeout, fut)
                .await
                .map_err(|_| anyhow::anyhow!("list_tools timed out after {:?}", timeout))?
                .map_err(|e| anyhow::anyhow!("Failed to list tools: {:?}", e))?;

            if tools.is_empty() {
                return Err(anyhow::anyhow!("server exposes no callable tools"));
            }

            let tool_names: Vec<String> = tools
                .iter()
                .map(|tool| format!("mcp:{}/{}", server_name, tool.name))
                .collect();
            tracing::debug!(
                target: "maho_agent::mcp_client",
                server = %server_name,
                tool_count = tools.len(),
                permission = ?permission,
                names = ?tool_names,
                "McpClient::list_tools aggregated"
            );

            let descriptors = tools
                .into_iter()
                .map(|t| {
                    let namespaced_name = format!("mcp:{}/{}", server_name, t.name);
                    let description = t
                        .description
                        .as_ref()
                        .map(|d| d.to_string())
                        .unwrap_or_default();
                    let parameters_schema = t.schema_as_json_value();
                    let sensitive = permission != ToolPermission::AutoApprove
                        || serde_json::to_value(&t)
                            .ok()
                            .is_none_or(|tool| !discovered_tool_is_readonly(&tool));

                    ToolDescriptor {
                        name: namespaced_name,
                        description,
                        parameters_schema,
                        provenance: ToolProvenance::External(server_name.clone()),
                        sensitive,
                        permission,
                    }
                })
                .collect();

            Ok(descriptors)
        });

        handle.await?
    }

    pub async fn call_tool(&self, name: &str, args: Value) -> Result<Value> {
        let server_name = self.server_name.clone();

        let prefix = format!("mcp:{}/", server_name);
        let raw_name = if name.starts_with(&prefix) {
            name[prefix.len()..].to_string()
        } else {
            name.to_string()
        };

        if !args.is_object() && !args.is_null() {
            return Err(anyhow::anyhow!("Arguments must be a JSON object"));
        }

        if let Some(browser_client) = self.browser_client.clone() {
            let output_cap = self.output_cap;
            let handle = get_mcp_runtime()?.spawn(async move {
                let result = browser_client.call_tool(&raw_name, args).await?;
                apply_output_cap(result, output_cap)
            });

            return handle.await?;
        }

        let peer = self.peer.clone();
        let timeout = self.timeout;
        let output_cap = self.output_cap;

        let mut params = CallToolRequestParams::new(raw_name);
        if let Value::Object(obj) = args {
            params.arguments = Some(obj);
        }

        let handle = get_mcp_runtime()?.spawn(async move {
            let fut = peer.call_tool(params);
            let res = tokio::time::timeout(timeout, fut)
                .await
                .map_err(|_| anyhow::anyhow!("call_tool timed out after {:?}", timeout))?
                .map_err(|e| anyhow::anyhow!("Failed to call tool: {:?}", e))?;

            apply_output_cap(serde_json::to_value(&res)?, output_cap)
        });

        handle.await?
    }

    pub fn is_alive(&self) -> bool {
        !self.peer.is_transport_closed()
    }

    /// Effective tool permission after FIX-9b peer verification.
    pub fn resolved_permission(&self) -> ToolPermission {
        self.resolved_permission
    }

    pub fn emit_unavailable(&self) {
        maho_types::events::core_update::emit_core_update(
            maho_types::events::core_update::CoreUpdate::ToolAvailabilityChanged {
                workspace_id: self.workspace_id.clone(),
                server_name: self.server_name.clone(),
                available: false,
            },
        );
    }

    pub async fn disconnect(&mut self) {
        let mut client_opt = self.client.take();
        self.browser_client.take();
        let ws_id = self.workspace_id.clone();
        let s_name = self.server_name.clone();

        if let Ok(rt) = get_mcp_runtime() {
            let handle = rt.spawn(async move {
                if let Some(client) = client_opt.take() {
                    drop(client);
                }
            });

            let _ = handle.await;
        }

        maho_types::events::core_update::emit_core_update(
            maho_types::events::core_update::CoreUpdate::ToolAvailabilityChanged {
                workspace_id: ws_id,
                server_name: s_name,
                available: false,
            },
        );
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn discovered_tool(policy: Value) -> Value {
        serde_json::json!({
            "name": "future_tool_not_known_to_agent",
            "annotations": {"readOnlyHint": true},
            "_meta": {"policy": policy}
        })
    }

    #[test]
    fn discovery_policy_proves_read_only_without_a_name_inventory() {
        let tool = discovered_tool(serde_json::json!({
            "mutability": "read_only",
            "changesAuthority": false,
            "sensitivity": "low",
            "requiresApproval": false,
            "permission": "auto_approve"
        }));
        assert!(discovered_tool_is_readonly(&tool));
    }

    /// The policy shape the browser ACTUALLY publishes, captured verbatim from a
    /// live `tools/list` against `maho-browser-mcp` (browser_tab_list). It carries
    /// neither `requiresApproval` nor `permission` — measured: 0 of 48 published
    /// tools carry either field. Requiring them made the predicate return false for
    /// every browser tool, so the dogfood auto-approve path was dead in practice.
    #[test]
    fn real_browser_published_policy_proves_a_read_only_tool() {
        let tool = serde_json::json!({
            "name": "browser_tab_list",
            "annotations": {"readOnlyHint": true, "destructiveHint": false},
            "_meta": {"policy": {
                "changesAuthority": false,
                "featureGate": "always",
                "missingPolicy": "fail_closed",
                "mutability": "read_only",
                "requiredBoundary": "profile",
                "requiredBroker": "browser",
                "sensitivity": "low"
            }}
        });
        assert!(
            discovered_tool_is_readonly(&tool),
            "the browser's own read-only descriptor must prove read-only"
        );
    }

    /// Read-only does NOT imply safe to auto-run. Both descriptors below are
    /// verbatim from the same live `tools/list` and are `readOnlyHint: true` +
    /// `mutability: "read_only"` + `changesAuthority: false`, exactly like
    /// `browser_tab_list`. Only the published `sensitivity` separates them, so
    /// without that conjunct these auto-run with no prompt and leak one-time
    /// passcodes and page/history PII.
    #[test]
    fn read_only_but_sensitive_or_credential_tools_still_require_approval() {
        let otp = serde_json::json!({
            "name": "mail_extract_otp",
            "annotations": {"readOnlyHint": true},
            "_meta": {"policy": {
                "changesAuthority": false,
                "featureGate": "mail_beta",
                "missingPolicy": "fail_closed",
                "mutability": "read_only",
                "requiredBoundary": "profile",
                "requiredBroker": "mail",
                "sensitivity": "credential"
            }}
        });
        assert!(
            !discovered_tool_is_readonly(&otp),
            "a credential-sensitivity OTP reader must never auto-run"
        );

        let page_content = serde_json::json!({
            "name": "browser_page_content",
            "annotations": {"readOnlyHint": true},
            "_meta": {"policy": {
                "changesAuthority": false,
                "featureGate": "always",
                "missingPolicy": "fail_closed",
                "mutability": "read_only",
                "requiredBoundary": "tab",
                "requiredBroker": "browser",
                "sensitivity": "sensitive"
            }}
        });
        assert!(
            !discovered_tool_is_readonly(&page_content),
            "a sensitive-sensitivity page reader must never auto-run"
        );

        let mut missing_sensitivity = otp.clone();
        missing_sensitivity["_meta"]["policy"]
            .as_object_mut()
            .expect("policy object")
            .remove("sensitivity");
        assert!(
            !discovered_tool_is_readonly(&missing_sensitivity),
            "absent sensitivity must fail closed"
        );
    }

    /// An explicit unsafe value still fails closed even though the field is optional.
    #[test]
    fn explicitly_unsafe_optional_policy_fields_still_fail_closed() {
        for (key, value) in [
            ("requiresApproval", serde_json::json!(true)),
            ("permission", serde_json::json!("always_ask")),
        ] {
            let mut tool = serde_json::json!({
                "annotations": {"readOnlyHint": true},
                "_meta": {"policy": {
                    "mutability": "read_only",
                    "changesAuthority": false,
                    "sensitivity": "low"
                }}
            });
            tool["_meta"]["policy"][key] = value;
            assert!(
                !discovered_tool_is_readonly(&tool),
                "an explicit unsafe {key} must fail closed"
            );
        }
    }

    #[test]
    fn missing_or_malformed_discovery_policy_fails_closed() {
        let safe = serde_json::json!({
            "mutability": "read_only",
            "changesAuthority": false,
            "sensitivity": "low",
            "requiresApproval": false,
            "permission": "auto_approve"
        });
        // Only the fields the browser actually publishes are load-bearing.
        // requiresApproval/permission are absent from every real descriptor, so
        // their ABSENCE must not disqualify; an explicitly unsafe VALUE still does
        // (covered by explicitly_unsafe_optional_policy_fields_still_fail_closed).
        for pointer in [
            "/annotations/readOnlyHint",
            "/_meta/policy/mutability",
            "/_meta/policy/changesAuthority",
            "/_meta/policy/sensitivity",
        ] {
            let mut tool = discovered_tool(safe.clone());
            tool.pointer_mut(pointer).expect("test field exists").take();
            assert!(!discovered_tool_is_readonly(&tool), "missing {pointer}");
        }

        let mut mutable = discovered_tool(safe);
        mutable["_meta"]["policy"]["requiresApproval"] = serde_json::json!(true);
        assert!(!discovered_tool_is_readonly(&mutable));
        assert!(!discovered_tool_is_readonly(&serde_json::json!({})));
    }

    #[cfg(unix)]
    #[test]
    fn exe_path_browser_predicate() {
        assert!(exe_path_is_trusted_browser(
            "/Applications/Maho.app/Contents/MacOS/Maho"
        ));
        assert!(exe_path_is_trusted_browser(
            "/Users/x/code/maho-workspace/chromium/src/out/Default/Maho.app/Contents/MacOS/Maho"
        ));
        assert!(!exe_path_is_trusted_browser("/tmp/evil-server"));
        assert!(!exe_path_is_trusted_browser("/usr/bin/python3"));
    }

    #[cfg(unix)]
    #[tokio::test]
    async fn same_process_uds_peer_is_same_uid() {
        let dir = tempfile::tempdir().unwrap();
        let sock = dir.path().join("peer.sock");
        let listener = tokio::net::UnixListener::bind(&sock).unwrap();
        let accept = tokio::spawn(async move { listener.accept().await.map(|(s, _)| s) });
        let client = tokio::net::UnixStream::connect(&sock).await.unwrap();
        let _server = accept.await.unwrap().unwrap();

        let cred = client.peer_cred().expect("peer_cred on same-host UDS");
        assert_eq!(
            cred.uid(),
            unsafe { libc::getuid() },
            "a same-process UDS peer must report the current uid"
        );
    }
}
