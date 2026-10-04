// Copyright 2026 The Maho Authors. All rights reserved.

//! Visual Computer-Use Agent (CUA) pipeline and strict native action parsing.
//!
//! Provides typed structures for native OS actions (Click, Type, Key), visual frames,
//! prompt message generation for multimodal providers, strict completion response parsing,
//! deterministic fixture providers for testing/benchmarking without locator oracles,
//! and fail-closed dispatch pipeline preventing execution on stale frames, denied consent,
//! or provider errors.

use serde::{Deserialize, Serialize};
use serde_json::{json, Value};
use std::sync::Arc;

use crate::fallback_ladder::ScreenshotCuaApproval;

/// 2D floating-point point in CSS pixels.
#[derive(Clone, Copy, Debug, PartialEq, Serialize, Deserialize)]
pub struct PointF {
    pub x: f32,
    pub y: f32,
}

impl PointF {
    pub fn new(x: f32, y: f32) -> Self {
        Self { x, y }
    }

    pub fn is_finite(&self) -> bool {
        self.x.is_finite() && self.y.is_finite()
    }
}

/// 2D rectangle in CSS pixels.
#[derive(Clone, Copy, Debug, PartialEq, Serialize, Deserialize)]
pub struct RectF {
    pub x: f32,
    pub y: f32,
    pub width: f32,
    pub height: f32,
}

impl RectF {
    pub fn new(x: f32, y: f32, width: f32, height: f32) -> Self {
        Self {
            x,
            y,
            width,
            height,
        }
    }

    pub fn is_finite(&self) -> bool {
        self.x.is_finite()
            && self.y.is_finite()
            && self.width.is_finite()
            && self.height.is_finite()
    }

    pub fn contains(&self, p: &PointF) -> bool {
        p.x >= self.x && p.x <= self.x + self.width && p.y >= self.y && p.y <= self.y + self.height
    }
}

/// Motion profile for native pointer movements.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize, Default)]
#[serde(rename_all = "snake_case")]
pub enum ClickMotionProfile {
    #[default]
    Direct,
    Smooth,
}

/// Discriminated native action enum.
#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
#[serde(tag = "kind", rename_all = "snake_case")]
pub enum NativeAction {
    Click {
        click_point_css: PointF,
        target_rect_css: RectF,
        #[serde(default)]
        motion_profile: ClickMotionProfile,
    },
    Type {
        text: String,
    },
    Key {
        key: String,
        #[serde(default)]
        modifiers: Vec<String>,
    },
}

/// Strict typed native action request matching browser engine expectations.
#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
pub struct NativeActionRequest {
    pub action: NativeAction,
    pub frame_token: String,
    pub tab_id: i64,
    pub lease_epoch: u64,
    pub document_epoch: u64,
    #[serde(default)]
    pub user_modifiers_active: bool,
}

impl NativeActionRequest {
    /// Validates the request matching NativeActionRequest::Validate() semantics.
    pub fn validate(&self) -> Result<(), VisualCuaError> {
        if self.frame_token.trim().is_empty() {
            return Err(VisualCuaError::InvalidAction(
                "frame_token cannot be empty".into(),
            ));
        }
        if self.lease_epoch == 0 {
            return Err(VisualCuaError::InvalidAction(
                "lease_epoch must be > 0".into(),
            ));
        }
        match &self.action {
            NativeAction::Click {
                click_point_css,
                target_rect_css,
                ..
            } => {
                if !click_point_css.is_finite() {
                    return Err(VisualCuaError::InvalidAction(
                        "click_point coords must be finite".into(),
                    ));
                }
                if !target_rect_css.is_finite() {
                    return Err(VisualCuaError::InvalidAction(
                        "target_rect coords must be finite".into(),
                    ));
                }
                if target_rect_css.width <= 0.0 || target_rect_css.height <= 0.0 {
                    return Err(VisualCuaError::InvalidAction(
                        "target_rect dimensions must be strictly positive".into(),
                    ));
                }
                if !target_rect_css.contains(click_point_css) {
                    return Err(VisualCuaError::InvalidAction(format!(
                        "target_rect ({:?}) does not contain click_point ({:?})",
                        target_rect_css, click_point_css
                    )));
                }
            }
            NativeAction::Type { text } => {
                if text.is_empty() {
                    return Err(VisualCuaError::InvalidAction(
                        "type text cannot be empty".into(),
                    ));
                }
            }
            NativeAction::Key { key, .. } => {
                if key.is_empty() {
                    return Err(VisualCuaError::InvalidAction("key cannot be empty".into()));
                }
            }
        }
        Ok(())
    }
}

/// Visual frame captured from the browser for visual inspection and coordinates.
#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
pub struct VisualFrame {
    pub frame_token: String,
    pub tab_id: i64,
    pub lease_epoch: u64,
    pub document_epoch: u64,
    pub viewport_width: f32,
    pub viewport_height: f32,
    pub redacted_png: Vec<u8>,
    pub is_stale: bool,
}

impl VisualFrame {
    pub fn is_fresh(&self) -> bool {
        !self.is_stale
            && !self.frame_token.trim().is_empty()
            && self.lease_epoch > 0
            && self.document_epoch > 0
            && !self.redacted_png.is_empty()
    }
}

/// Error type for visual CUA operations.
#[derive(Debug, thiserror::Error, Clone, PartialEq)]
pub enum VisualCuaError {
    #[error("consent denied: explicit approval required for visual/native input")]
    ConsentDenied,
    #[error("stale frame: visual frame is empty, stale, or epoch mismatch")]
    StaleFrame,
    #[error("provider error: {0}")]
    ProviderError(String),
    #[error("parse error: {0}")]
    ParseError(String),
    #[error("invalid native action: {0}")]
    InvalidAction(String),
    #[error("dispatch error: {0}")]
    DispatchError(String),
}

/// Multimodal completion request holding structured `prompt_messages`.
/// Mirrors desktop C++ `CompletionRequest::prompt_messages`.
#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
pub struct CompletionRequest {
    pub prompt_messages: Vec<PromptMessage>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub model: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub endpoint: Option<String>,
}

#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
pub struct PromptMessage {
    pub role: String,
    pub content: Value,
}

impl PromptMessage {
    pub fn user_multimodal(text: &str, image_png: &[u8]) -> Self {
        use base64::Engine;
        let b64 = base64::prelude::BASE64_STANDARD.encode(image_png);
        let data_url = format!("data:image/png;base64,{b64}");
        Self {
            role: "user".to_string(),
            content: json!([
                {
                    "type": "text",
                    "text": text,
                },
                {
                    "type": "image_url",
                    "image_url": {
                        "url": data_url,
                    },
                }
            ]),
        }
    }

    pub fn system(text: &str) -> Self {
        Self {
            role: "system".to_string(),
            content: json!(text),
        }
    }
}

/// Parse raw text/json from a model completion into a strict typed `NativeAction`.
pub fn parse_native_action(raw: &str) -> Result<NativeAction, VisualCuaError> {
    let trimmed = raw.trim();
    if trimmed.is_empty() {
        return Err(VisualCuaError::ParseError(
            "empty completion response".into(),
        ));
    }

    // Unescape markdown code fences if wrapped in ```json ... ```
    let json_text = if let Some(start) = trimmed.find("```") {
        let after_start = &trimmed[start + 3..];
        let content_start = if let Some(stripped) = after_start.strip_prefix("json") {
            stripped
        } else {
            after_start
        };
        if let Some(end) = content_start.find("```") {
            content_start[..end].trim()
        } else {
            content_start.trim()
        }
    } else {
        trimmed
    };

    // Parse JSON
    let val: Value = serde_json::from_str(json_text)
        .map_err(|e| VisualCuaError::ParseError(format!("malformed JSON: {e}")))?;

    // Accept envelope { "action": ... } or direct action object
    let act_val = if let Some(act) = val.get("action") {
        act
    } else {
        &val
    };

    let obj = act_val
        .as_object()
        .ok_or_else(|| VisualCuaError::ParseError("action must be a JSON object".into()))?;

    let kind = obj
        .get("kind")
        .or_else(|| obj.get("type"))
        .or_else(|| obj.get("action"))
        .and_then(Value::as_str)
        .ok_or_else(|| {
            VisualCuaError::ParseError(
                "action missing required 'kind' / 'type' / 'action' field".into(),
            )
        })?;

    match kind {
        "click" => {
            let (cx, cy) = parse_point(
                obj.get("click_point_css")
                    .or_else(|| obj.get("click_point"))
                    .or_else(|| obj.get("point")),
            )
            .ok_or_else(|| {
                VisualCuaError::ParseError("click requires valid 'click_point_css' [x, y]".into())
            })?;

            let (rx, ry, rw, rh) = parse_rect(
                obj.get("target_rect_css")
                    .or_else(|| obj.get("target_rect"))
                    .or_else(|| obj.get("rect")),
            )
            .ok_or_else(|| {
                VisualCuaError::ParseError(
                    "click requires valid 'target_rect_css' [x, y, width, height]".into(),
                )
            })?;

            let motion_profile = match obj.get("motion_profile").and_then(Value::as_str) {
                Some("smooth") => ClickMotionProfile::Smooth,
                _ => ClickMotionProfile::Direct,
            };

            Ok(NativeAction::Click {
                click_point_css: PointF::new(cx as f32, cy as f32),
                target_rect_css: RectF::new(rx as f32, ry as f32, rw as f32, rh as f32),
                motion_profile,
            })
        }
        "type" => {
            let text = obj
                .get("text")
                .and_then(Value::as_str)
                .ok_or_else(|| VisualCuaError::ParseError("type action missing 'text'".into()))?;
            Ok(NativeAction::Type {
                text: text.to_string(),
            })
        }
        "key" => {
            let key = obj
                .get("key")
                .and_then(Value::as_str)
                .ok_or_else(|| VisualCuaError::ParseError("key action missing 'key'".into()))?;
            let modifiers = obj
                .get("modifiers")
                .and_then(Value::as_array)
                .map(|arr| {
                    arr.iter()
                        .filter_map(Value::as_str)
                        .map(ToString::to_string)
                        .collect()
                })
                .unwrap_or_default();
            Ok(NativeAction::Key {
                key: key.to_string(),
                modifiers,
            })
        }
        other => Err(VisualCuaError::ParseError(format!(
            "unknown native action kind: '{other}'"
        ))),
    }
}

fn parse_point(v: Option<&Value>) -> Option<(f64, f64)> {
    match v? {
        Value::Array(arr) if arr.len() >= 2 => {
            let x = arr[0].as_f64()?;
            let y = arr[1].as_f64()?;
            Some((x, y))
        }
        Value::Object(obj) => {
            let x = obj.get("x").and_then(Value::as_f64)?;
            let y = obj.get("y").and_then(Value::as_f64)?;
            Some((x, y))
        }
        _ => None,
    }
}

fn parse_rect(v: Option<&Value>) -> Option<(f64, f64, f64, f64)> {
    match v? {
        Value::Array(arr) if arr.len() >= 4 => {
            let x = arr[0].as_f64()?;
            let y = arr[1].as_f64()?;
            let w = arr[2].as_f64()?;
            let h = arr[3].as_f64()?;
            Some((x, y, w, h))
        }
        Value::Object(obj) => {
            let x = obj.get("x").and_then(Value::as_f64)?;
            let y = obj.get("y").and_then(Value::as_f64)?;
            let w = obj
                .get("width")
                .or_else(|| obj.get("w"))
                .and_then(Value::as_f64)?;
            let h = obj
                .get("height")
                .or_else(|| obj.get("h"))
                .and_then(Value::as_f64)?;
            Some((x, y, w, h))
        }
        _ => None,
    }
}

/// 64-bit SplitMix PRNG matching desktop C++ SplitMix64Prng in input_synthesizer.h.
pub struct SplitMix64Prng {
    state: u64,
}

impl SplitMix64Prng {
    pub const fn new(seed: u64) -> Self {
        Self { state: seed }
    }

    pub fn next_u64(&mut self) -> u64 {
        self.state = self.state.wrapping_add(0x9e3779b97f4a7c15);
        let mut z = self.state;
        z = (z ^ (z >> 30)).wrapping_mul(0xbf58476d1ce4e5b9);
        z = (z ^ (z >> 27)).wrapping_mul(0x94d049bb133111eb);
        z ^ (z >> 31)
    }

    pub fn next_range(&mut self, min: i32, max: i32) -> i32 {
        if min >= max {
            return min;
        }
        let span = (max - min + 1) as u64;
        min + (self.next_u64() % span) as i32
    }
}

/// Deterministic 64-bit FNV-1a hash of bytes.
pub fn hash_bytes_fnv1a(bytes: &[u8]) -> u64 {
    let mut hash: u64 = 0xcbf29ce484222325;
    for &b in bytes {
        hash ^= b as u64;
        hash = hash.wrapping_mul(0x100000001b3);
    }
    hash
}

/// Async completion provider trait for visual CUA models.
#[async_trait::async_trait]
pub trait VisualCuaProvider: Send + Sync {
    async fn complete(&self, request: &CompletionRequest) -> Result<String, VisualCuaError>;
}

/// Async dispatcher trait for native actions.
#[async_trait::async_trait]
pub trait NativeActionDispatcher: Send + Sync {
    async fn dispatch_native_action(
        &self,
        request: &NativeActionRequest,
    ) -> Result<Value, VisualCuaError>;
}

/// Image-dependent deterministic fixture provider.
///
/// Inspects the redacted PNG in `prompt_messages` and returns a randomized fixture
/// position without any locator oracle (no DOM access, selectors, or @refs).
/// The resulting position is completely deterministic with respect to the image bytes.
pub struct DeterministicFixtureProvider;

impl DeterministicFixtureProvider {
    pub fn new() -> Self {
        Self
    }

    /// Extract raw image bytes from multimodal prompt messages.
    pub fn extract_image_bytes(request: &CompletionRequest) -> Result<Vec<u8>, VisualCuaError> {
        for msg in &request.prompt_messages {
            if let Some(arr) = msg.content.as_array() {
                for part in arr {
                    if part.get("type").and_then(Value::as_str) == Some("image_url") {
                        if let Some(url) = part.pointer("/image_url/url").and_then(Value::as_str) {
                            if let Some(b64) = url.strip_prefix("data:image/png;base64,") {
                                use base64::Engine;
                                let bytes =
                                    base64::prelude::BASE64_STANDARD.decode(b64).map_err(|e| {
                                        VisualCuaError::ProviderError(format!(
                                            "invalid base64 image: {e}"
                                        ))
                                    })?;
                                return Ok(bytes);
                            }
                        }
                    }
                }
            }
        }
        Err(VisualCuaError::ProviderError(
            "no image found in prompt_messages".into(),
        ))
    }
}

impl Default for DeterministicFixtureProvider {
    fn default() -> Self {
        Self::new()
    }
}

#[async_trait::async_trait]
impl VisualCuaProvider for DeterministicFixtureProvider {
    async fn complete(&self, request: &CompletionRequest) -> Result<String, VisualCuaError> {
        let image_bytes = Self::extract_image_bytes(request)?;
        let seed = hash_bytes_fnv1a(&image_bytes);
        let mut prng = SplitMix64Prng::new(seed);

        // Derive randomized coordinates bounded within standard viewport
        let x = prng.next_range(50, 750) as f32;
        let y = prng.next_range(50, 550) as f32;
        let width = prng.next_range(30, 120) as f32;
        let height = prng.next_range(20, 60) as f32;
        let rect_x = x - (width * 0.4);
        let rect_y = y - (height * 0.4);

        let response = json!({
            "kind": "click",
            "click_point_css": { "x": x, "y": y },
            "target_rect_css": {
                "x": rect_x,
                "y": rect_y,
                "width": width,
                "height": height
            },
            "motion_profile": "direct"
        });

        Ok(response.to_string())
    }
}

/// Visual CUA pipeline orchestrating screenshot-based native actions.
pub struct VisualCuaPipeline {
    provider: Arc<dyn VisualCuaProvider>,
    dispatcher: Arc<dyn NativeActionDispatcher>,
}

impl VisualCuaPipeline {
    pub fn new(
        provider: Arc<dyn VisualCuaProvider>,
        dispatcher: Arc<dyn NativeActionDispatcher>,
    ) -> Self {
        Self {
            provider,
            dispatcher,
        }
    }

    /// Executes one visual CUA step.
    ///
    /// Invariants strictly enforced:
    /// 1. Denied consent never dispatches (returns `ConsentDenied`).
    /// 2. Stale/empty image never dispatches (returns `StaleFrame`).
    /// 3. Provider error never dispatches (returns `ProviderError`).
    /// 4. Invalid/out-of-bounds action never dispatches (returns `InvalidAction`).
    pub async fn execute_step(
        &self,
        frame: &VisualFrame,
        consent_approved: bool,
        instruction: &str,
    ) -> Result<Value, VisualCuaError> {
        // Invariant 1: Denied consent NEVER dispatches
        if !consent_approved {
            return Err(VisualCuaError::ConsentDenied);
        }

        // Invariant 2: Stale frame NEVER dispatches
        if !frame.is_fresh() {
            return Err(VisualCuaError::StaleFrame);
        }

        // Multimodal prompt messages
        let request = CompletionRequest {
            prompt_messages: vec![
                PromptMessage::system(
                    "You are a visual browser automation agent. Output strict JSON action.",
                ),
                PromptMessage::user_multimodal(instruction, &frame.redacted_png),
            ],
            model: None,
            endpoint: None,
        };

        // Invariant 3: Provider error NEVER dispatches
        let raw_completion = self.provider.complete(&request).await?;

        // Parse action
        let action = parse_native_action(&raw_completion)?;

        // Build NativeActionRequest with frame token and epochs
        let req = NativeActionRequest {
            action,
            frame_token: frame.frame_token.clone(),
            tab_id: frame.tab_id,
            lease_epoch: frame.lease_epoch,
            document_epoch: frame.document_epoch,
            user_modifiers_active: false,
        };

        // Invariant 4: Validation failures NEVER dispatch
        req.validate()?;

        // Dispatch
        self.dispatcher.dispatch_native_action(&req).await
    }

    /// Step execution with typed `ScreenshotCuaApproval`.
    pub async fn execute_step_with_approval(
        &self,
        frame: &VisualFrame,
        approval: ScreenshotCuaApproval,
        instruction: &str,
    ) -> Result<Value, VisualCuaError> {
        let consent_approved = matches!(approval, ScreenshotCuaApproval::Approved);
        self.execute_step(frame, consent_approved, instruction)
            .await
    }
}
