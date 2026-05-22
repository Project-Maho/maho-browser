use std::path::PathBuf;
use std::sync::Arc;

use serde_json::Value;
use tokio::sync::{mpsc, Mutex};

use crate::events::{
    parse_session_update, AcpUpdate, PermissionOption, PermissionOutcome, PermissionRequest,
    PermissionToolCall, PromptResult, StopReason,
};
use crate::jsonrpc::Router;
use crate::process::AcpProcess;
use crate::session::{CloseSessionParams, McpServer, NewSessionParams, NewSessionResult};
use crate::transport::LineTransport;
use crate::turn::{
    build_cancel_notification, build_permission_reply, parse_prompt_result, PromptContentPart,
    PromptParams,
};
use crate::types::{InitializeParams, InitializeResult};
use crate::Error;

pub struct AcpClient {
    router: Arc<Router>,
    write_tx: mpsc::Sender<Value>,
    process: Mutex<Option<AcpProcess>>,
    update_tx: Arc<Mutex<Option<mpsc::UnboundedSender<AcpUpdate>>>>,
    permission_tx: Arc<Mutex<Option<mpsc::UnboundedSender<PermissionRequest>>>>,
    _read_task: tokio::task::JoinHandle<()>,
    _write_task: tokio::task::JoinHandle<()>,
}

impl AcpClient {
    pub async fn start(command_path: &str) -> Result<Self, Error> {
        let (process, stdin, stdout) = AcpProcess::spawn(command_path)?;

        let router = Arc::new(Router::new());
        let (write_tx, mut write_rx) = mpsc::channel::<Value>(64);

        let update_tx: Arc<Mutex<Option<mpsc::UnboundedSender<AcpUpdate>>>> =
            Arc::new(Mutex::new(None));
        let permission_tx: Arc<Mutex<Option<mpsc::UnboundedSender<PermissionRequest>>>> =
            Arc::new(Mutex::new(None));

        let router_for_read = router.clone();
        let write_tx_for_read = write_tx.clone();
        let update_tx_for_read = update_tx.clone();
        let perm_tx_for_read = permission_tx.clone();
        let mut read_transport = LineTransport::new(stdout, tokio::io::sink());

        let read_task = tokio::spawn(async move {
            loop {
                match read_transport.read_line().await {
                    Ok(Some(line)) => {
                        let msg: Value = match serde_json::from_str(&line) {
                            Ok(v) => v,
                            Err(e) => {
                                tracing::warn!("failed to parse inbound JSON: {}", e);
                                continue;
                            }
                        };

                        let method = msg.get("method").and_then(|v| v.as_str());
                        let has_id = msg.get("id").is_some();

                        // Intercept session/update notifications before the router
                        if method == Some("session/update") && !has_id {
                            let params =
                                msg.get("params").cloned().unwrap_or(Value::Null);
                            let event = parse_session_update(&params);
                            let guard = update_tx_for_read.lock().await;
                            if let Some(sender) = guard.as_ref() {
                                let _ = sender.send(event);
                            }
                            continue;
                        }

                        // Intercept session/request_permission — reply is deferred
                        // to the caller via reply_permission()
                        if method == Some("session/request_permission") && has_id {
                            let json_rpc_id =
                                msg.get("id").and_then(|v| v.as_i64()).unwrap_or(0);
                            let params =
                                msg.get("params").cloned().unwrap_or(Value::Null);

                            let session_id = params
                                .get("sessionId")
                                .and_then(|v| v.as_str())
                                .unwrap_or("")
                                .to_string();

                            let tool_call: PermissionToolCall = params
                                .get("toolCall")
                                .and_then(|v| serde_json::from_value(v.clone()).ok())
                                .unwrap_or(PermissionToolCall {
                                    id: String::new(),
                                    tool: String::new(),
                                    extra: Value::Null,
                                });

                            let options: Vec<PermissionOption> = params
                                .get("options")
                                .and_then(|v| serde_json::from_value(v.clone()).ok())
                                .unwrap_or_default();

                            let request = PermissionRequest {
                                json_rpc_id,
                                session_id,
                                tool_call,
                                options,
                                raw_params: params,
                            };

                            let guard = perm_tx_for_read.lock().await;
                            if let Some(sender) = guard.as_ref() {
                                let _ = sender.send(request);
                            } else {
                                let reply = serde_json::json!({
                                    "jsonrpc": "2.0",
                                    "id": json_rpc_id,
                                    "result": build_permission_reply(
                                        &PermissionOutcome::Cancelled
                                    )
                                });
                                let _ = write_tx_for_read.send(reply).await;
                            }
                            continue;
                        }

                        if let Some(reply) = router_for_read.dispatch(&msg).await {
                            let _ = write_tx_for_read.send(reply).await;
                        }
                    }
                    Ok(None) => {
                        router_for_read
                            .reject_all_pending("transport closed (EOF)")
                            .await;
                        break;
                    }
                    Err(e) => {
                        tracing::error!("transport read error: {}", e);
                        router_for_read
                            .reject_all_pending(&format!("transport error: {}", e))
                            .await;
                        break;
                    }
                }
            }
        });

        let mut write_transport = LineTransport::new(tokio::io::empty(), stdin);
        let write_task = tokio::spawn(async move {
            while let Some(msg) = write_rx.recv().await {
                if let Err(e) = write_transport.write_message(&msg).await {
                    tracing::error!("transport write error: {}", e);
                    break;
                }
            }
            let _ = write_transport.close_write().await;
        });

        Ok(Self {
            router,
            write_tx,
            process: Mutex::new(Some(process)),
            update_tx,
            permission_tx,
            _read_task: read_task,
            _write_task: write_task,
        })
    }

    pub async fn initialize(&self) -> Result<InitializeResult, Error> {
        let params = InitializeParams::maho_default();
        let params_value = serde_json::to_value(&params).map_err(Error::Json)?;

        let (msg, rx) = self.router.build_request("initialize", params_value).await;
        self.write_tx
            .send(msg)
            .await
            .map_err(|_| Error::TransportClosed)?;

        let result = rx.await.map_err(|_| Error::TransportClosed)?;
        let result = result.map_err(|e| Error::JsonRpc {
            code: e.code,
            message: e.message,
        })?;

        let init_result: InitializeResult =
            serde_json::from_value(result).map_err(Error::Json)?;

        if init_result.protocol_version != 1 {
            return Err(Error::ProtocolVersionMismatch {
                expected: 1,
                got: init_result.protocol_version,
            });
        }

        Ok(init_result)
    }

    pub async fn new_session(
        &self,
        cwd: PathBuf,
        mcp_servers: Vec<McpServer>,
    ) -> Result<NewSessionResult, Error> {
        let params = NewSessionParams { cwd, mcp_servers };
        let params_value = serde_json::to_value(&params).map_err(Error::Json)?;

        let (msg, rx) = self
            .router
            .build_request("session/new", params_value)
            .await;
        self.write_tx
            .send(msg)
            .await
            .map_err(|_| Error::TransportClosed)?;

        let result = rx.await.map_err(|_| Error::TransportClosed)?;
        let result = result.map_err(|e| Error::JsonRpc {
            code: e.code,
            message: e.message,
        })?;

        serde_json::from_value(result).map_err(Error::Json)
    }

    pub async fn close_session(&self, session_id: &str) -> Result<(), Error> {
        let params = CloseSessionParams {
            session_id: session_id.to_string(),
        };
        let params_value = serde_json::to_value(&params).map_err(Error::Json)?;

        let (msg, rx) = self
            .router
            .build_request("session/close", params_value)
            .await;
        self.write_tx
            .send(msg)
            .await
            .map_err(|_| Error::TransportClosed)?;

        let result = rx.await.map_err(|_| Error::TransportClosed)?;
        result.map_err(|e| Error::JsonRpc {
            code: e.code,
            message: e.message,
        })?;

        Ok(())
    }

    pub async fn submit_prompt(
        &self,
        session_id: &str,
        text: String,
    ) -> Result<PromptHandle, Error> {
        let params = PromptParams {
            session_id: session_id.to_string(),
            prompt: vec![PromptContentPart {
                content_type: "text".to_string(),
                text,
            }],
        };

        let (event_tx, event_rx) = mpsc::unbounded_channel();
        *self.update_tx.lock().await = Some(event_tx);

        let (perm_tx, perm_rx) = mpsc::unbounded_channel();
        *self.permission_tx.lock().await = Some(perm_tx);

        let (msg, rx) = self
            .router
            .build_request("session/prompt", params.to_value())
            .await;
        self.write_tx
            .send(msg)
            .await
            .map_err(|_| Error::TransportClosed)?;

        let write_tx = self.write_tx.clone();
        let session_id_owned = session_id.to_string();
        let update_tx = self.update_tx.clone();
        let permission_tx_clone = self.permission_tx.clone();

        let completion = tokio::spawn(async move {
            let result = rx.await.map_err(|_| Error::TransportClosed)?;

            *update_tx.lock().await = None;
            *permission_tx_clone.lock().await = None;

            match result {
                Ok(value) => Ok(parse_prompt_result(&value)),
                Err(e) => Err(Error::JsonRpc {
                    code: e.code,
                    message: e.message,
                }),
            }
        });

        Ok(PromptHandle {
            events: event_rx,
            permission_requests: perm_rx,
            completion,
            write_tx,
            session_id: session_id_owned,
            cancelled: false,
        })
    }

    pub async fn reply_permission(
        &self,
        json_rpc_id: i64,
        outcome: PermissionOutcome,
    ) -> Result<(), Error> {
        let reply = serde_json::json!({
            "jsonrpc": "2.0",
            "id": json_rpc_id,
            "result": build_permission_reply(&outcome)
        });
        self.write_tx
            .send(reply)
            .await
            .map_err(|_| Error::TransportClosed)
    }

    pub async fn shutdown(self) -> Result<(), Error> {
        drop(self.write_tx);

        let mut proc_guard = self.process.lock().await;
        if let Some(process) = proc_guard.take() {
            process.shutdown().await?;
        }

        Ok(())
    }

    pub async fn send_notification(&self, method: &str, params: Option<Value>) -> Result<(), Error> {
        let notif = Router::build_notification(method, params);
        self.write_tx
            .send(notif)
            .await
            .map_err(|_| Error::TransportClosed)
    }

    pub fn router(&self) -> &Router {
        &self.router
    }
}

pub struct PromptHandle {
    pub events: mpsc::UnboundedReceiver<AcpUpdate>,
    pub permission_requests: mpsc::UnboundedReceiver<PermissionRequest>,
    pub completion: tokio::task::JoinHandle<Result<PromptResult, Error>>,
    write_tx: mpsc::Sender<Value>,
    session_id: String,
    cancelled: bool,
}

impl PromptHandle {
    pub async fn await_completion(self) -> Result<PromptResult, Error> {
        match self.completion.await {
            Ok(result) => {
                if self.cancelled {
                    match &result {
                        Err(_) => Ok(PromptResult {
                            stop_reason: StopReason::Cancelled,
                            usage: None,
                            raw: Value::Null,
                        }),
                        Ok(r) => Ok(r.clone()),
                    }
                } else {
                    result
                }
            }
            Err(_join_err) => {
                if self.cancelled {
                    Ok(PromptResult {
                        stop_reason: StopReason::Cancelled,
                        usage: None,
                        raw: Value::Null,
                    })
                } else {
                    Err(Error::TransportClosed)
                }
            }
        }
    }

    pub async fn cancel(&mut self) -> Result<(), Error> {
        self.cancelled = true;
        let notif = Router::build_notification(
            "session/cancel",
            Some(build_cancel_notification(&self.session_id)),
        );
        self.write_tx
            .send(notif)
            .await
            .map_err(|_| Error::TransportClosed)
    }
}
