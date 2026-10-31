#![cfg(feature = "visual-cua-native")]

use std::sync::{Arc, Mutex};

use maho_agent::visual_cua::{
    ClickMotionProfile, NativeAction, NativeActionDispatcher, NativeActionRequest, PointF, RectF,
    VisualCuaError,
};
use maho_agent::visual_cua_native::{BrowserToolCaller, McpNativeActionDispatcher};
use serde_json::{json, Value};

#[derive(Clone, Default)]
struct FakeBrowserPeer {
    calls: Arc<Mutex<Vec<(String, Value)>>>,
    reply: Arc<Mutex<Option<anyhow::Result<Value>>>>,
}

impl FakeBrowserPeer {
    fn reply_with(&self, reply: anyhow::Result<Value>) {
        *self.reply.lock().unwrap() = Some(reply);
    }
    fn calls(&self) -> Vec<(String, Value)> {
        self.calls.lock().unwrap().clone()
    }
}

#[async_trait::async_trait]
impl BrowserToolCaller for FakeBrowserPeer {
    async fn call_browser_tool(&self, name: &str, args: Value) -> anyhow::Result<Value> {
        self.calls.lock().unwrap().push((name.to_string(), args));
        self.reply
            .lock()
            .unwrap()
            .take()
            .unwrap_or_else(|| Ok(json!({ "content": [{ "type": "text", "text": "ok" }] })))
    }
}

fn request(action: NativeAction) -> NativeActionRequest {
    NativeActionRequest {
        action,
        frame_token: "frame-1".into(),
        tab_id: 42,
        lease_epoch: 3,
        document_epoch: 1,
        user_modifiers_active: false,
    }
}

#[tokio::test]
async fn native_actions_map_to_browser_tools() {
    let peer = FakeBrowserPeer::default();
    let dispatcher = McpNativeActionDispatcher::new(peer.clone());

    dispatcher
        .dispatch_native_action(&request(NativeAction::Click {
            click_point_css: PointF::new(15.0, 25.0),
            target_rect_css: RectF::new(10.0, 20.0, 40.0, 20.0),
            motion_profile: ClickMotionProfile::Direct,
        }))
        .await
        .expect("click dispatches");
    dispatcher
        .dispatch_native_action(&request(NativeAction::Type { text: "hi".into() }))
        .await
        .expect("type dispatches");
    dispatcher
        .dispatch_native_action(&request(NativeAction::Key {
            key: "a".into(),
            modifiers: vec!["shift".into()],
        }))
        .await
        .expect("key dispatches");

    let calls = peer.calls();
    assert_eq!(
        calls.iter().map(|(name, _)| name.as_str()).collect::<Vec<_>>(),
        ["browser_visual_click", "browser_native_type", "browser_native_key"]
    );
    assert_eq!(
        calls[0].1,
        json!({
            "tab_id": 42, "frame_token": "frame-1", "lease_epoch": 3,
            "click_point_css": { "x": 15.0, "y": 25.0 },
            "target_rect_css": { "x": 10.0, "y": 20.0, "width": 40.0, "height": 20.0 }
        })
    );
    assert_eq!(calls[1].1["text"], "hi");
    assert_eq!(calls[2].1["key"], "a");
    assert_eq!(calls[2].1["modifiers"], json!(["shift"]));
}

#[tokio::test]
async fn browser_refusals_are_typed_errors_never_success() {
    let peer = FakeBrowserPeer::default();
    let dispatcher = McpNativeActionDispatcher::new(peer.clone());
    let typing = request(NativeAction::Type { text: "x".into() });

    peer.reply_with(Err(anyhow::anyhow!(
        "-32011: native input requires the target tab's window to be frontmost"
    )));
    let error = dispatcher.dispatch_native_action(&typing).await.unwrap_err();
    assert!(matches!(&error, VisualCuaError::DispatchError(m) if m.contains("-32011")));

    peer.reply_with(Ok(json!({
        "isError": true,
        "content": [{ "type": "text", "text": "visual action budget exhausted" }]
    })));
    let error = dispatcher.dispatch_native_action(&typing).await.unwrap_err();
    assert!(matches!(&error, VisualCuaError::DispatchError(m) if m.contains("budget")));

    peer.reply_with(Ok(json!({ "error": { "code": -32009, "message": "stale frame token" } })));
    let error = dispatcher.dispatch_native_action(&typing).await.unwrap_err();
    assert!(matches!(&error, VisualCuaError::DispatchError(m) if m.contains("-32009")));

    let calls_before = peer.calls().len();
    let invalid = request(NativeAction::Type { text: String::new() });
    assert!(matches!(
        dispatcher.dispatch_native_action(&invalid).await,
        Err(VisualCuaError::InvalidAction(_))
    ));
    let mut held = typing.clone();
    held.user_modifiers_active = true;
    assert!(dispatcher.dispatch_native_action(&held).await.is_err());
    assert_eq!(peer.calls().len(), calls_before, "rejected requests never dispatch");
}
