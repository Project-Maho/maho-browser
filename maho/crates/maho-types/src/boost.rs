use serde::{Deserialize, Serialize};

use crate::common::{Color, UrlPattern};
use crate::identifiers::{BoostId, SpaceId};

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct ColorOverride {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub background: Option<Color>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub text: Option<Color>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub links: Option<Color>,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct FontOverride {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub font_family: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub font_size: Option<f64>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub line_height: Option<f64>,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Boost {
    pub id: BoostId,
    pub url_pattern: UrlPattern,
    pub custom_css: Option<String>,
    pub custom_js: Option<String>,
    pub color_theme: Option<ColorOverride>,
    pub font_override: Option<FontOverride>,
    pub enabled: bool,
    pub space_scope: Option<SpaceId>,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct BoostUpdate {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub url_pattern: Option<UrlPattern>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub custom_css: Option<Option<String>>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub custom_js: Option<Option<String>>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub color_theme: Option<Option<ColorOverride>>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub font_override: Option<Option<FontOverride>>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub enabled: Option<bool>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub space_scope: Option<Option<SpaceId>>,
}
