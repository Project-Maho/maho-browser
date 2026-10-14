use anyhow::{anyhow, Result};
use maho_types::ai::{AiMcpServer, McpTransport};
use maho_types::tool::{ToolDescriptor, ToolPermission, ToolProvenance};
use rmcp::model::{ClientInfo, ProtocolVersion};
use rmcp::transport::IntoTransport;
use serde_json::Value;
use std::sync::OnceLock;
use std::time::Duration;
use tokio::runtime::Runtime;

const UNSUPPORTED: &str = "MCP client is not available on iOS (banned syscalls: fork/execve/setsid). Configure MCP servers on desktop.";

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
        Err(e) => Err(anyhow!("{e}")),
    }
}

/// Decide the *candidate* default permission for tools exposed over a UDS server.
///
/// Only the browser's OWN dogfood socket (resolved via
/// [`maho_browser_mcp::paths::default_socket_path`]) is a candidate for
/// `AutoApprove`; it is lease/firewall-protected by the browser. Arbitrary
/// user-imported UDS servers lack that protection and must remain `AlwaysAsk`.
///
/// SECURITY (FIX-9b): a path match is necessary but NOT sufficient. `connect`
/// additionally verifies the connected UDS peer (same-uid) and fail-closed
/// downgrades to `AlwaysAsk` otherwise; use [`McpClient::resolved_permission`]
/// for the effective post-verification permission. On iOS the browser server is
/// in-process/UDS so the peer is always same-user.
///
/// SECURITY (FIX-9a): every tool listed by this stub stays `sensitive`, so the
/// permission callback still gates each call even at `AutoApprove`.
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

/// FIX-9b (#4): require the connected UDS peer to be a same-user process before
/// honoring dogfood `AutoApprove`. On iOS proc-path introspection is not used;
/// the same-uid baseline suffices (the browser server is in-process/UDS).
fn verify_uds_peer(stream: &tokio::net::UnixStream) -> std::result::Result<(), String> {
    let cred = stream
        .peer_cred()
        .map_err(|e| format!("peer_cred() unavailable: {e}"))?;
    let current_uid = unsafe { libc::getuid() };
    if cred.uid() != current_uid {
        return Err(format!(
            "peer uid {} != current uid {} (cross-user socket)",
            cred.uid(),
            current_uid
        ));
    }
    Ok(())
}

pub struct McpClient {
    workspace_id: String,
    server_name: String,
    peer: rmcp::Peer<rmcp::RoleClient>,
    client: Option<rmcp::service::RunningService<rmcp::RoleClient, ClientInfo>>,
    timeout: Duration,
    output_cap: usize,
    resolved_permission: ToolPermission,
}

impl McpClient {
    pub async fn connect(config: &AiMcpServer, auth_token: Option<String>) -> Result<Self> {
        if config.transport == McpTransport::Uds {
            let config_clone = config.clone();
            let handle = get_mcp_runtime()?
                .spawn(async move { Self::connect_uds(&config_clone, auth_token).await });
            let result = handle.await?;
            maho_types::events::core_update::emit_core_update(
                maho_types::events::core_update::CoreUpdate::ToolAvailabilityChanged {
                    workspace_id: config.workspace_id.clone(),
                    server_name: config.name.clone(),
                    available: result.is_ok(),
                },
            );
            result
        } else {
            Err(anyhow!(UNSUPPORTED))
        }
    }

    async fn connect_uds(config: &AiMcpServer, _auth_token: Option<String>) -> Result<Self> {
        let timeout = Duration::from_millis(config.timeout_ms.min(60_000));
        let output_cap = config.output_cap_bytes;
        let server_name = config.name.clone();

        let socket_path = config
            .socket_path
            .as_ref()
            .ok_or_else(|| anyhow!("Socket path is required for UDS transport"))?;
        let stream = tokio::net::UnixStream::connect(socket_path)
            .await
            .map_err(|e| anyhow!("Failed to connect to UDS: {:?}", e))?;

        let candidate_permission =
            resolve_uds_permission(config.transport, config.socket_path.as_deref());
        let resolved_permission = if candidate_permission == ToolPermission::AutoApprove {
            match verify_uds_peer(&stream) {
                Ok(()) => ToolPermission::AutoApprove,
                Err(reason) => {
                    tracing::warn!(
                        target: "maho_agent::mcp_client",
                        server = %server_name,
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
        let client =
            rmcp::service::serve_client(client_info, IntoTransport::into_transport(stream))
                .await
                .map_err(|e| anyhow!("Failed to start MCP UDS client: {:?}", e))?;

        let peer = std::ops::Deref::deref(&client).clone();

        Ok(Self {
            workspace_id: config.workspace_id.clone(),
            server_name,
            peer,
            client: Some(client),
            timeout,
            output_cap,
            resolved_permission,
        })
    }

    pub async fn list_tools(&self) -> Result<Vec<ToolDescriptor>> {
        let peer = self.peer.clone();
        let timeout = self.timeout;
        let server_name = self.server_name.clone();
        let permission = self.resolved_permission;

        let handle = get_mcp_runtime()?.spawn(async move {
            let fut = peer.list_all_tools();
            let tools = tokio::time::timeout(timeout, fut)
                .await
                .map_err(|_| anyhow!("list_tools timed out after {:?}", timeout))?
                .map_err(|e| anyhow!("Failed to list tools: {:?}", e))?;

            if tools.is_empty() {
                return Err(anyhow!("server exposes no callable tools"));
            }

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
        let peer = self.peer.clone();
        let timeout = self.timeout;
        let server_name = self.server_name.clone();
        let output_cap = self.output_cap;

        let prefix = format!("mcp:{}/", server_name);
        let raw_name = if name.starts_with(&prefix) {
            name[prefix.len()..].to_string()
        } else {
            name.to_string()
        };

        let mut params = rmcp::model::CallToolRequestParams::new(raw_name);
        if let Value::Object(obj) = args {
            params.arguments = Some(obj);
        } else if !args.is_null() {
            return Err(anyhow!("Arguments must be a JSON object"));
        }

        let handle = get_mcp_runtime()?.spawn(async move {
            let fut = peer.call_tool(params);
            let res = tokio::time::timeout(timeout, fut)
                .await
                .map_err(|_| anyhow!("call_tool timed out after {:?}", timeout))?
                .map_err(|e| anyhow!("Failed to call tool: {:?}", e))?;

            crate::tool_registry::apply_mcp_output_cap(serde_json::to_value(&res)?, output_cap)
        });

        handle.await?
    }

    pub fn is_alive(&self) -> bool {
        !self.peer.is_transport_closed()
    }

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
