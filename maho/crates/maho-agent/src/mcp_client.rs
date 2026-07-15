// Model Context Protocol Client
use rmcp::service::RunningService;
use rmcp::{RoleClient, Peer};
use rmcp::model::{CallToolRequestParams, ClientInfo, ProtocolVersion};
use rmcp::transport::IntoTransport;
use maho_types::ai::{AiMcpServer, McpTransport};
use maho_types::tool::{ToolDescriptor, ToolProvenance, ToolPermission};
use std::time::Duration;
use std::sync::{Arc, OnceLock};
use std::ops::Deref;
use serde_json::Value;
use anyhow::Result;
use tokio::runtime::Runtime;

fn get_mcp_runtime() -> &'static Runtime {
    static RUNTIME: OnceLock<Runtime> = OnceLock::new();
    RUNTIME.get_or_init(|| {
        tokio::runtime::Builder::new_multi_thread()
            .worker_threads(2)
            .thread_name("mcp-worker-pool")
            .enable_all()
            .build()
            .expect("Failed to build MCP dedicated runtime")
    })
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
/// SECURITY (FIX-9a): even when the effective permission is `AutoApprove`, only
/// READ-ONLY dogfood tools may auto-run without a permission prompt. Sensitive
/// browser *actions* (clicks, typing, navigation, tab creation, etc.) must still
/// be gated by the permission callback — see [`is_readonly_dogfood_tool`].
pub fn resolve_uds_permission(transport: McpTransport, socket_path: Option<&str>) -> ToolPermission {
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

/// Read-only classification for dogfood (`AutoApprove`) browser MCP tools.
///
/// FIX-9a (#3): granting `AutoApprove` to the browser's dogfood socket must NOT
/// blanket-clear the `sensitive` flag for genuinely sensitive *actions*. The CLI
/// ships a `deny-sensitive` default gate (README: "denying sensitive browser
/// actions like clicks and keyboard input unless bypassed"), and the browser-side
/// `allowed_domains` firewall does NOT cover click/type — so the permission
/// callback is the only gate for those. Only tools on this READ-ONLY allowlist
/// auto-run under dogfood `AutoApprove`; everything else (mutation / navigation /
/// input, and — fail-closed — any unknown or newly-added tool) keeps
/// `sensitive == true` so the callback still gates it.
///
/// The set is derived from `maho-browser-mcp/src/server.rs` tool names. `raw_name`
/// is the tool name with any `mcp:<server>/` prefix stripped (e.g. `browser_click`).
/// The browser MCP protocol exposes no per-tool sensitivity annotation, so we
/// classify by a maintained name allowlist; a read-only *allowlist* (rather than a
/// sensitive *denylist*) is the safer posture because unknown/future tools default
/// to gated rather than silently auto-running.
pub fn is_readonly_dogfood_tool(raw_name: &str) -> bool {
    matches!(
        raw_name,
        // Pure reads / inspection — safe to auto-run.
        "browser_ping"
            | "browser_tab_list"
            | "browser_tab_get"
            | "browser_history_search"
            | "browser_bookmarks_search"
            | "browser_page_content"
            | "browser_page_text"
            | "browser_search_in_page"
            | "browser_page_context"
            | "browser_screenshot_full"
            | "browser_screenshot_element"
            | "browser_accessibility_snapshot"
            | "browser_console_messages"
            | "browser_network_get_har"
            | "browser_get_allowed_domains"
            | "browser_routines_list"
            | "mail_list_accounts"
            | "mail_list_folders"
            | "mail_list_emails"
            | "mail_get_email"
            | "mail_search_emails"
            | "mail_list_thread"
            | "mail_extract_otp"
    )
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
    let n = unsafe {
        libc::proc_pidpath(pid, buf.as_mut_ptr() as *mut libc::c_void, BUF_LEN as u32)
    };
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
        
        let handle = get_mcp_runtime().spawn(async move {
            Self::connect_internal(&config_clone, auth_token).await
        });
        
        match handle.await? {
            Ok(client) => {
                maho_types::events::core_update::emit_core_update(
                    maho_types::events::core_update::CoreUpdate::ToolAvailabilityChanged {
                        workspace_id: config.workspace_id.clone(),
                        server_name: config.name.clone(),
                        available: true,
                    }
                );
                Ok(client)
            }
            Err(e) => {
                maho_types::events::core_update::emit_core_update(
                    maho_types::events::core_update::CoreUpdate::ToolAvailabilityChanged {
                        workspace_id: config.workspace_id.clone(),
                        server_name: config.name.clone(),
                        available: false,
                    }
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
                let command_str = config.command.as_ref().ok_or_else(|| {
                    anyhow::anyhow!("Command is required for stdio transport")
                })?;

                let mut parts: Vec<String> = if command_str.trim().starts_with('[') {
                    serde_json::from_str(command_str)
                        .unwrap_or_else(|_| vec![command_str.to_string()])
                } else {
                    command_str.split_whitespace().map(|s| s.to_string()).collect()
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

                let child_transport = rmcp::transport::TokioChildProcess::new(tokio_cmd)
                    .map_err(|e| anyhow::anyhow!("Failed to build child process transport: {:?}", e))?;

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
                let url_str = config.url.as_ref().ok_or_else(|| {
                    anyhow::anyhow!("URL is required for HTTP transport")
                })?;

                let mut transport_config = rmcp::transport::streamable_http_client::StreamableHttpClientTransportConfig::with_uri(url_str.as_str());
                if let Some(token) = auth_token {
                    transport_config = transport_config.auth_header(token);
                }

                let http_transport = rmcp::transport::StreamableHttpClientTransport::from_config(transport_config);
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
                let resolved_permission = if candidate_permission
                    == ToolPermission::AutoApprove
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
                let client = rmcp::service::serve_client(client_info, IntoTransport::into_transport(stream))
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
            let handle = get_mcp_runtime().spawn(async move {
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
                        let parameters_schema = tool
                            .get("inputSchema")
                            .cloned()
                            .unwrap_or_else(|| serde_json::json!({ "type": "object", "properties": {} }));

                        Ok(ToolDescriptor {
                            name: format!("mcp:{}/{}", server_name, raw_name),
                            description,
                            parameters_schema,
                            provenance: ToolProvenance::External(server_name.clone()),
                            sensitive: true,
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

        let handle = get_mcp_runtime().spawn(async move {
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
                    let description = t.description.as_ref().map(|d| d.to_string()).unwrap_or_default();
                    let parameters_schema = t.schema_as_json_value();

                    ToolDescriptor {
                        name: namespaced_name,
                        description,
                        parameters_schema,
                        provenance: ToolProvenance::External(server_name.clone()),
                        sensitive: true,
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
            let handle = get_mcp_runtime().spawn(async move {
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

        let handle = get_mcp_runtime().spawn(async move {
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
            }
        );
    }

    pub async fn disconnect(&mut self) {
        let mut client_opt = self.client.take();
        self.browser_client.take();
        let ws_id = self.workspace_id.clone();
        let s_name = self.server_name.clone();
        
        let handle = get_mcp_runtime().spawn(async move {
            if let Some(client) = client_opt.take() {
                drop(client);
            }
        });
        
        let _ = handle.await;

        maho_types::events::core_update::emit_core_update(
            maho_types::events::core_update::CoreUpdate::ToolAvailabilityChanged {
                workspace_id: ws_id,
                server_name: s_name,
                available: false,
            }
        );
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn readonly_dogfood_allowlist_matches_read_tools() {
        for name in [
            "browser_ping",
            "browser_tab_list",
            "browser_tab_get",
            "browser_history_search",
            "browser_bookmarks_search",
            "browser_page_content",
            "browser_page_text",
            "browser_search_in_page",
            "browser_page_context",
            "browser_screenshot_full",
            "browser_screenshot_element",
            "browser_accessibility_snapshot",
            "browser_console_messages",
            "browser_network_get_har",
            "browser_get_allowed_domains",
            "browser_routines_list",
            "mail_list_accounts",
            "mail_list_folders",
            "mail_list_emails",
            "mail_get_email",
            "mail_search_emails",
            "mail_list_thread",
            "mail_extract_otp",
        ] {
            assert!(is_readonly_dogfood_tool(name), "{name} must be read-only");
        }
    }

    #[test]
    fn sensitive_action_tools_are_not_readonly() {
        for name in [
            "browser_navigate",
            "browser_tab_new",
            "browser_tab_close",
            "browser_tab_switch",
            "browser_bookmark_create",
            "browser_click",
            "browser_type",
            "browser_select",
            "browser_hover",
            "browser_scroll",
            "browser_key_press",
            "browser_set_viewport_size",
            "browser_wait_for_navigation",
            "browser_network_start_capture",
            "browser_network_stop_capture",
            "browser_set_allowed_domains",
            "browser_acquire_lease",
            "browser_heartbeat_lease",
            "browser_release_lease",
            "browser_routines_run",
            "mail_send",
        ] {
            assert!(
                !is_readonly_dogfood_tool(name),
                "{name} is a sensitive action and must NOT be auto-run"
            );
        }
    }

    #[test]
    fn unknown_tools_default_to_sensitive_fail_closed() {
        assert!(!is_readonly_dogfood_tool("browser_future_mutation"));
        assert!(!is_readonly_dogfood_tool("arbitrary_tool"));
        assert!(!is_readonly_dogfood_tool(""));
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
