use serde::{Deserialize, Serialize};
use std::fmt;

use crate::identifiers::{SpaceId, TabId};

// === Url (branded wrapper) ===
#[derive(Clone, Debug, PartialEq, Eq, Hash, Serialize, Deserialize)]
pub struct Url(pub String);

impl Url {
    pub fn new(url: impl Into<String>) -> Self {
        Self(url.into())
    }
}

impl fmt::Display for Url {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", self.0)
    }
}

impl AsRef<str> for Url {
    fn as_ref(&self) -> &str {
        &self.0
    }
}

// === DateTime (ISO 8601 branded wrapper) ===
#[derive(Clone, Debug, PartialEq, Eq, Hash, Serialize, Deserialize)]
pub struct DateTime(pub String);

impl DateTime {
    pub fn now() -> Self {
        Self(chrono::Utc::now().to_rfc3339_opts(chrono::SecondsFormat::Millis, true))
    }

    pub fn from_iso(s: impl Into<String>) -> Self {
        Self(s.into())
    }
}

impl fmt::Display for DateTime {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", self.0)
    }
}

impl AsRef<str> for DateTime {
    fn as_ref(&self) -> &str {
        &self.0
    }
}

// === Geometric Types ===
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Point {
    pub x: f64,
    pub y: f64,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Size {
    pub width: f64,
    pub height: f64,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Rect {
    pub origin: Point,
    pub size: Size,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Viewport {
    pub offset: Point,
    pub zoom: f64,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct ScrollPosition {
    pub x: f64,
    pub y: f64,
}

impl Default for ScrollPosition {
    fn default() -> Self {
        Self { x: 0.0, y: 0.0 }
    }
}

// === Media Types ===
#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum ImageFormat {
    Png,
    Jpeg,
    Webp,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct ImageData {
    #[serde(deserialize_with = "deserialize_bytes")]
    pub data: Vec<u8>,
    pub width: u32,
    pub height: u32,
    pub format: ImageFormat,
}

fn deserialize_bytes<'de, D>(deserializer: D) -> Result<Vec<u8>, D::Error>
where
    D: serde::Deserializer<'de>,
{
    struct BytesVisitor;

    impl<'de> serde::de::Visitor<'de> for BytesVisitor {
        type Value = Vec<u8>;

        fn expecting(&self, formatter: &mut std::fmt::Formatter) -> std::fmt::Result {
            formatter.write_str("a base64-encoded string or a sequence of bytes")
        }

        fn visit_str<E>(self, v: &str) -> Result<Self::Value, E>
        where
            E: serde::de::Error,
        {
            base64_decode(v).ok_or_else(|| E::custom("invalid base64 string"))
        }

        fn visit_seq<A>(self, mut seq: A) -> Result<Self::Value, A::Error>
        where
            A: serde::de::SeqAccess<'de>,
        {
            let mut values = Vec::new();
            while let Some(value) = seq.next_element()? {
                values.push(value);
            }
            Ok(values)
        }
    }

    deserializer.deserialize_any(BytesVisitor)
}

fn base64_decode(input: &str) -> Option<Vec<u8>> {
    let mut bytes = Vec::new();
    let mut buffer = 0u32;
    let mut num_bits = 0;

    for c in input.chars() {
        if c == '=' {
            break;
        }
        let val = match c {
            'A'..='Z' => c as u32 - 'A' as u32,
            'a'..='z' => c as u32 - 'a' as u32 + 26,
            '0'..='9' => c as u32 - '0' as u32 + 52,
            '+' | '-' => 62,
            '/' | '_' => 63,
            _ => continue,
        };
        buffer = (buffer << 6) | val;
        num_bits += 6;
        if num_bits >= 8 {
            num_bits -= 8;
            bytes.push(((buffer >> num_bits) & 0xFF) as u8);
        }
    }
    Some(bytes)
}

// === URL Pattern (for Boosts) ===
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct UrlPattern {
    pub pattern: String,
    #[serde(rename = "type")]
    pub pattern_type: UrlPatternType,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum UrlPatternType {
    Glob,
    Regex,
}

// === Cookie ===
#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Cookie {
    pub name: String,
    pub value: String,
    pub domain: String,
    pub path: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub expires: Option<String>,
    pub http_only: bool,
    pub secure: bool,
    pub same_site: SameSite,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum SameSite {
    Strict,
    Lax,
    None,
}

// === Data Types for clear_data ===
#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct DataTypes {
    pub cookies: bool,
    pub cache: bool,
    pub local_storage: bool,
    pub session_storage: bool,
    pub indexed_db: bool,
    pub service_workers: bool,
}

// === JS Interop ===
#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(untagged)]
pub enum JsValue {
    Null,
    Bool(bool),
    Number(f64),
    String(String),
    Array(Vec<JsValue>),
    Object(std::collections::HashMap<String, JsValue>),
}

// === Script Injection ===
#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum ScriptWorld {
    Main,
    Isolated,
}

// === Find in Page ===
#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct FindOptions {
    pub case_sensitive: bool,
    pub whole_word: bool,
    pub backwards: bool,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct FindResult {
    pub match_count: u32,
    pub active_match_index: u32,
}

// === Content Rules (legacy — replaced by adblock::Engine network-level blocking) ===
#[deprecated(note = "Use adblock::Engine via ContentBlocker::should_block_request instead")]
#[derive(Clone, Debug, Serialize, Deserialize)]
#[allow(deprecated)]
pub struct ContentRule {
    pub id: String,
    pub trigger: ContentRuleTrigger,
    pub action: ContentRuleAction,
}

#[deprecated(note = "Use adblock::Engine via ContentBlocker::should_block_request instead")]
#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ContentRuleTrigger {
    pub url_filter: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub url_filter_is_case_sensitive: Option<bool>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub resource_type: Option<Vec<String>>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub load_type: Option<Vec<String>>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub if_domain: Option<Vec<String>>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub unless_domain: Option<Vec<String>>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub if_top_url: Option<Vec<String>>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub unless_top_url: Option<Vec<String>>,
}

#[deprecated(note = "Use adblock::Engine via ContentBlocker::should_block_request instead")]
#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "kebab-case")]
pub enum ContentRuleAction {
    Block,
    BlockCookies,
    CssDisplayNone(String),
    IgnorePreviousRules,
}

// === Color ===
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Color {
    pub r: u8,
    pub g: u8,
    pub b: u8,
    pub a: f64,
}

// === Orientation ===
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum Orientation {
    Horizontal,
    Vertical,
}

// === Memory Pressure ===
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum MemoryPressureLevel {
    Normal,
    Warning,
    Critical,
    Extreme,
}

// === Drag & Drop ===
#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct DropTarget {
    pub space_id: SpaceId,
    pub position: usize,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub parent_tab_id: Option<TabId>,
}

// === Tab Snapshot (for suspension) ===
#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct TabSnapshot {
    pub url: String,
    pub title: String,
    pub scroll_position: ScrollPosition,
    pub interaction_state: Vec<u8>,
    pub captured_at: DateTime,
}

// === Easel Drawing ===
#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct DrawPath {
    pub points: Vec<Point>,
    pub stroke_color: Color,
    pub stroke_width: f64,
}
