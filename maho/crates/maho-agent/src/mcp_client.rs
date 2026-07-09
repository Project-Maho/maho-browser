// Model Context Protocol Client
use rmcp::service::RunningService;
use rmcp::{RoleClient, Peer};
use rmcp::model::CallToolRequestParams;
use maho_types::ai::{AiMcpServer, McpTransport};
use maho_types::tool::{ToolDescriptor, ToolProvenance, ToolPermission};
use std::time::Duration;
use std::sync::OnceLock;
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

pub struct McpClient {
    server_name: String,
    peer: Peer<RoleClient>,
    client: Option<RunningService<RoleClient, ()>>,
    timeout: Duration,
    output_cap: usize,
}

impl McpClient {
    pub async fn connect(config: &AiMcpServer, auth_token: Option<String>) -> Result<Self> {
        let config_clone = config.clone();
        
        let handle = get_mcp_runtime().spawn(async move {
            Self::connect_internal(&config_clone, auth_token).await
        });
        
        handle.await?
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

                let client = rmcp::service::serve_client((), child_transport)
                    .await
                    .map_err(|e| anyhow::anyhow!("Failed to start MCP stdio client: {:?}", e))?;

                let peer = client.deref().clone();

                Ok(Self {
                    server_name,
                    peer,
                    client: Some(client),
                    timeout,
                    output_cap,
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
                let client = rmcp::service::serve_client((), http_transport)
                    .await
                    .map_err(|e| anyhow::anyhow!("Failed to start MCP HTTP client: {:?}", e))?;

                let peer = client.deref().clone();

                Ok(Self {
                    server_name,
                    peer,
                    client: Some(client),
                    timeout,
                    output_cap,
                })
            }
        }
    }

    pub async fn list_tools(&self) -> Result<Vec<ToolDescriptor>> {
        let peer = self.peer.clone();
        let timeout = self.timeout;
        let server_name = self.server_name.clone();

        let handle = get_mcp_runtime().spawn(async move {
            let fut = peer.list_all_tools();
            let tools = tokio::time::timeout(timeout, fut)
                .await
                .map_err(|_| anyhow::anyhow!("list_tools timed out after {:?}", timeout))?
                .map_err(|e| anyhow::anyhow!("Failed to list tools: {:?}", e))?;

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
                        sensitive: false,
                        permission: ToolPermission::SessionApprove,
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

        // Strip namespace prefix before dispatching to server
        let prefix = format!("mcp:{}/", server_name);
        let raw_name = if name.starts_with(&prefix) {
            name[prefix.len()..].to_string()
        } else {
            name.to_string()
        };

        let mut params = CallToolRequestParams::new(raw_name);
        if let Value::Object(obj) = args {
            params.arguments = Some(obj);
        } else if !args.is_null() {
            return Err(anyhow::anyhow!("Arguments must be a JSON object"));
        }

        let handle = get_mcp_runtime().spawn(async move {
            let fut = peer.call_tool(params);
            let res = tokio::time::timeout(timeout, fut)
                .await
                .map_err(|_| anyhow::anyhow!("call_tool timed out after {:?}", timeout))?
                .map_err(|e| anyhow::anyhow!("Failed to call tool: {:?}", e))?;

            let val = serde_json::to_value(&res)?;
            let mut val_str = serde_json::to_string(&val)?;
            if val_str.len() > output_cap {
                val_str.truncate(output_cap);
                return Ok(Value::String(format!("{}... [output truncated]", val_str)));
            }

            Ok(val)
        });

        handle.await?
    }

    pub async fn disconnect(&mut self) {
        let mut client_opt = self.client.take();
        
        let handle = get_mcp_runtime().spawn(async move {
            if let Some(client) = client_opt.take() {
                drop(client);
            }
        });
        
        let _ = handle.await;
    }
}
