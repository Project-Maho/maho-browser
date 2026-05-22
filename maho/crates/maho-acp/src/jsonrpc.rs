use std::collections::HashMap;
use std::future::Future;
use std::pin::Pin;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::Arc;

use serde_json::Value;
use tokio::sync::{oneshot, Mutex};

/// A pending outbound request awaiting a response.
struct PendingRequest {
    tx: oneshot::Sender<Result<Value, JsonRpcError>>,
}

/// A JSON-RPC error object.
#[derive(Debug, Clone)]
pub struct JsonRpcError {
    pub code: i64,
    pub message: String,
    pub data: Option<Value>,
}

impl std::fmt::Display for JsonRpcError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "JSON-RPC error {}: {}", self.code, self.message)
    }
}

impl std::error::Error for JsonRpcError {}

/// Handler for inbound notifications (no response expected).
pub type NotificationHandler = Box<dyn Fn(Value) + Send + Sync>;

/// Handler for inbound requests (must return a result or error).
pub type RequestHandler = Box<
    dyn Fn(Value) -> Pin<Box<dyn Future<Output = Result<Value, JsonRpcError>> + Send>>
        + Send
        + Sync,
>;

/// JSON-RPC 2.0 router with outbound id correlation and inbound dispatch.
pub struct Router {
    next_id: AtomicU64,
    pending: Arc<Mutex<HashMap<u64, PendingRequest>>>,
    notification_handlers: Arc<Mutex<HashMap<String, NotificationHandler>>>,
    request_handlers: Arc<Mutex<HashMap<String, RequestHandler>>>,
}

impl Default for Router {
    fn default() -> Self {
        Self::new()
    }
}

impl Router {
    pub fn new() -> Self {
        Self {
            next_id: AtomicU64::new(1),
            pending: Arc::new(Mutex::new(HashMap::new())),
            notification_handlers: Arc::new(Mutex::new(HashMap::new())),
            request_handlers: Arc::new(Mutex::new(HashMap::new())),
        }
    }

    /// Allocate the next outbound request id (monotonic from 1).
    pub fn next_id(&self) -> u64 {
        self.next_id.fetch_add(1, Ordering::Relaxed)
    }

    /// Build an outbound JSON-RPC request message and register a pending entry.
    /// Returns (message_value, receiver for the response).
    pub async fn build_request(
        &self,
        method: &str,
        params: Value,
    ) -> (Value, oneshot::Receiver<Result<Value, JsonRpcError>>) {
        let id = self.next_id();
        let (tx, rx) = oneshot::channel();
        self.pending.lock().await.insert(id, PendingRequest { tx });

        let msg = serde_json::json!({
            "jsonrpc": "2.0",
            "id": id,
            "method": method,
            "params": params,
        });

        (msg, rx)
    }

    /// Build an outbound notification (no id, no response expected).
    pub fn build_notification(method: &str, params: Option<Value>) -> Value {
        let mut msg = serde_json::json!({
            "jsonrpc": "2.0",
            "method": method,
        });
        if let Some(p) = params {
            msg["params"] = p;
        }
        msg
    }

    /// Register a handler for inbound notifications with the given method.
    pub async fn on_notification<F>(&self, method: &str, handler: F)
    where
        F: Fn(Value) + Send + Sync + 'static,
    {
        self.notification_handlers
            .lock()
            .await
            .insert(method.to_string(), Box::new(handler));
    }

    /// Register a handler for inbound requests with the given method.
    pub async fn on_request<F, Fut>(&self, method: &str, handler: F)
    where
        F: Fn(Value) -> Fut + Send + Sync + 'static,
        Fut: Future<Output = Result<Value, JsonRpcError>> + Send + 'static,
    {
        self.request_handlers.lock().await.insert(
            method.to_string(),
            Box::new(move |params| Box::pin(handler(params))),
        );
    }

    /// Dispatch an inbound message. Returns an optional response to send back.
    pub async fn dispatch(&self, msg: &Value) -> Option<Value> {
        let jsonrpc = msg.get("jsonrpc").and_then(|v| v.as_str());
        if jsonrpc != Some("2.0") {
            return None;
        }

        let has_id = msg.get("id").is_some();
        let has_method = msg.get("method").is_some();
        let has_result = msg.get("result").is_some();
        let has_error = msg.get("error").is_some();

        // Inbound response (has id, has result or error, no method)
        if has_id && (has_result || has_error) && !has_method {
            let id = msg["id"].as_u64().unwrap_or(0);
            let mut pending = self.pending.lock().await;
            if let Some(entry) = pending.remove(&id) {
                if has_error {
                    let err_obj = &msg["error"];
                    let error = JsonRpcError {
                        code: err_obj["code"].as_i64().unwrap_or(-1),
                        message: err_obj["message"]
                            .as_str()
                            .unwrap_or("Unknown error")
                            .to_string(),
                        data: err_obj.get("data").cloned(),
                    };
                    let _ = entry.tx.send(Err(error));
                } else {
                    let _ = entry.tx.send(Ok(msg["result"].clone()));
                }
            }
            return None;
        }

        // Inbound request (has id + method)
        if has_id && has_method {
            let method = msg["method"].as_str().unwrap_or("");
            let params = msg.get("params").cloned().unwrap_or(Value::Null);
            let id = msg["id"].clone();

            let handlers = self.request_handlers.lock().await;
            if let Some(handler) = handlers.get(method) {
                match handler(params).await {
                    Ok(result) => {
                        return Some(serde_json::json!({
                            "jsonrpc": "2.0",
                            "id": id,
                            "result": result,
                        }));
                    }
                    Err(e) => {
                        return Some(serde_json::json!({
                            "jsonrpc": "2.0",
                            "id": id,
                            "error": {
                                "code": e.code,
                                "message": e.message,
                            },
                        }));
                    }
                }
            } else {
                // Default: Method not found (-32601)
                return Some(serde_json::json!({
                    "jsonrpc": "2.0",
                    "id": id,
                    "error": {
                        "code": -32601,
                        "message": "Method not found",
                    },
                }));
            }
        }

        // Inbound notification (has method, no id)
        if has_method && !has_id {
            let method = msg["method"].as_str().unwrap_or("");
            let params = msg.get("params").cloned().unwrap_or(Value::Null);

            let handlers = self.notification_handlers.lock().await;
            if let Some(handler) = handlers.get(method) {
                handler(params);
            }
            // Notifications never produce a response
            return None;
        }

        None
    }

    /// Reject all pending requests (e.g., on transport close).
    pub async fn reject_all_pending(&self, reason: &str) {
        let mut pending = self.pending.lock().await;
        for (_, entry) in pending.drain() {
            let _ = entry.tx.send(Err(JsonRpcError {
                code: -32000,
                message: reason.to_string(),
                data: None,
            }));
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[tokio::test]
    async fn outbound_id_monotonic_from_1() {
        let router = Router::new();
        assert_eq!(router.next_id(), 1);
        assert_eq!(router.next_id(), 2);
        assert_eq!(router.next_id(), 3);
    }

    #[tokio::test]
    async fn inbound_response_correlates_by_id() {
        let router = Router::new();
        let (msg, rx) = router.build_request("test", serde_json::json!({})).await;

        let id = msg["id"].as_u64().unwrap();
        assert_eq!(id, 1);

        // Simulate inbound response
        let response = serde_json::json!({
            "jsonrpc": "2.0",
            "id": id,
            "result": {"ok": true},
        });
        let reply = router.dispatch(&response).await;
        assert!(reply.is_none()); // responses don't produce replies

        let result = rx.await.unwrap().unwrap();
        assert_eq!(result["ok"], true);
    }

    #[tokio::test]
    async fn inbound_error_response() {
        let router = Router::new();
        let (_msg, rx) = router.build_request("test", serde_json::json!({})).await;

        let response = serde_json::json!({
            "jsonrpc": "2.0",
            "id": 1,
            "error": {"code": -32600, "message": "Invalid Request"},
        });
        router.dispatch(&response).await;

        let result = rx.await.unwrap();
        assert!(result.is_err());
        let err = result.unwrap_err();
        assert_eq!(err.code, -32600);
    }

    #[tokio::test]
    async fn inbound_notification_dispatch() {
        let router = Router::new();
        let received = Arc::new(Mutex::new(Vec::new()));
        let received_clone = received.clone();

        router
            .on_notification("session/update", move |params| {
                let r = received_clone.clone();
                tokio::spawn(async move {
                    r.lock().await.push(params);
                });
            })
            .await;

        let notif = serde_json::json!({
            "jsonrpc": "2.0",
            "method": "session/update",
            "params": {"data": 42},
        });
        let reply = router.dispatch(&notif).await;
        assert!(reply.is_none());

        // Give the spawned task a moment
        tokio::time::sleep(std::time::Duration::from_millis(10)).await;
        let items = received.lock().await;
        assert_eq!(items.len(), 1);
        assert_eq!(items[0]["data"], 42);
    }

    #[tokio::test]
    async fn inbound_request_dispatch_with_handler() {
        let router = Router::new();
        router
            .on_request("session/request_permission", |_params| async move {
                Ok(serde_json::json!({"outcome": {"outcome": "selected", "optionId": "once"}}))
            })
            .await;

        let req = serde_json::json!({
            "jsonrpc": "2.0",
            "id": 99,
            "method": "session/request_permission",
            "params": {"tool": "write_file"},
        });
        let reply = router.dispatch(&req).await.unwrap();
        assert_eq!(reply["id"], 99);
        assert_eq!(reply["result"]["outcome"]["optionId"], "once");
    }

    #[tokio::test]
    async fn inbound_request_default_method_not_found() {
        let router = Router::new();

        let req = serde_json::json!({
            "jsonrpc": "2.0",
            "id": 5,
            "method": "unknown/method",
            "params": {},
        });
        let reply = router.dispatch(&req).await.unwrap();
        assert_eq!(reply["id"], 5);
        assert_eq!(reply["error"]["code"], -32601);
    }

    #[tokio::test]
    async fn reject_all_pending() {
        let router = Router::new();
        let (_msg1, rx1) = router.build_request("a", serde_json::json!({})).await;
        let (_msg2, rx2) = router.build_request("b", serde_json::json!({})).await;

        router.reject_all_pending("transport closed").await;

        assert!(rx1.await.unwrap().is_err());
        assert!(rx2.await.unwrap().is_err());
    }
}
