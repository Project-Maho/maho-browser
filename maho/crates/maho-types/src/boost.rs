use serde::{Deserialize, Serialize};

use crate::common::DateTime;
use crate::identifiers::BoostId;

#[derive(Clone, Copy, Debug, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Point {
    pub x: f32,
    pub y: f32,
}

impl Default for Point {
    fn default() -> Self {
        Self { x: 0.0, y: 0.0 }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub enum CaseMode {
    None,
    Upper,
    Lower,
    Capitalize,
}

/// Backward-compat alias for external crates that still reference `TextCase`.
pub type TextCase = CaseMode;

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub enum SizeMode {
    K90,
    #[default]
    K100,
    K110,
    K125,
    K150,
}

impl SizeMode {
    pub fn as_zoom(&self) -> f32 {
        match self {
            SizeMode::K90 => 0.9,
            SizeMode::K100 => 1.0,
            SizeMode::K110 => 1.1,
            SizeMode::K125 => 1.25,
            SizeMode::K150 => 1.5,
        }
    }

    pub fn from_zoom(z: f32) -> Self {
        // Round to nearest of the 5 valid values.
        let candidates = [
            (0.9, SizeMode::K90),
            (1.0, SizeMode::K100),
            (1.1, SizeMode::K110),
            (1.25, SizeMode::K125),
            (1.5, SizeMode::K150),
        ];
        candidates
            .iter()
            .min_by(|a, b| {
                let da = (a.0 - z).abs();
                let db = (b.0 - z).abs();
                da.partial_cmp(&db).unwrap_or(std::cmp::Ordering::Equal)
            })
            .map(|c| c.1)
            .unwrap_or(SizeMode::K100)
    }

    pub fn as_db_string(&self) -> &'static str {
        match self {
            SizeMode::K90 => "k90",
            SizeMode::K100 => "k100",
            SizeMode::K110 => "k110",
            SizeMode::K125 => "k125",
            SizeMode::K150 => "k150",
        }
    }

    pub fn from_db_string(s: &str) -> Option<Self> {
        match s {
            "k90" => Some(SizeMode::K90),
            "k100" => Some(SizeMode::K100),
            "k110" => Some(SizeMode::K110),
            "k125" => Some(SizeMode::K125),
            "k150" => Some(SizeMode::K150),
            _ => None,
        }
    }
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ColorBoost {
    pub dot_pos: Point,
    pub dot_distance: f32,
    pub dot_angle_deg: f64,
    pub secondary_dot_pos: Point,
    pub secondary_dot_angle_deg_delta: f64,
    #[serde(alias = "autoTheme")]
    pub magic_theme: bool,
    pub color_boost_enabled: bool,
    pub smart_invert: bool,
    pub contrast: f64,
    pub brightness: f64,
    pub saturation: f64,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct TypographyBoost {
    pub font_family: String,
    pub case_mode: CaseMode,
    pub size_mode: SizeMode,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Boost {
    pub id: BoostId,
    pub domain: String,
    pub name: String,
    pub color: ColorBoost,
    pub typography: TypographyBoost,
    pub zap_selectors: Vec<String>,
    // TRUST BOUNDARY: Values may remain raw at rest; boost_css_sanitizer.rs enforces
    // the final compose_css injection boundary, while import_boost filters secondarily.
    pub custom_css: String,
    pub change_was_made: bool,
    pub created_at: DateTime,
    pub updated_at: DateTime,
}

/// Zen-compatible export/import DTO matching Zen's `exportBoost` / `importBoost`
/// JSON envelope. Field names follow Zen vocabulary (`boostName`,
/// `enableColorBoost`, ...). Maho-only fields (id, domain, timestamps) are
/// omitted and regenerated on import.
#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ZenCompatBoostData {
    pub boost_name: String,
    pub dot_pos: Point,
    pub dot_distance: f32,
    pub dot_angle_deg: f64,
    pub secondary_dot_pos: Point,
    pub secondary_dot_angle_deg_delta: f64,
    #[serde(alias = "autoTheme")]
    pub magic_theme: bool,
    #[serde(alias = "colorBoostEnabled")]
    pub enable_color_boost: bool,
    pub smart_invert: bool,
    pub contrast: f64,
    pub brightness: f64,
    pub saturation: f64,
    pub font_family: String,
    pub text_case_override: String,
    pub size_override: f32,
    pub zap_selectors: Vec<String>,
    #[serde(rename = "customCSS")]
    pub custom_css: String,
    pub change_was_made: bool,
}

impl From<&Boost> for ZenCompatBoostData {
    fn from(b: &Boost) -> Self {
        Self {
            boost_name: b.name.clone(),
            dot_pos: b.color.dot_pos,
            dot_distance: b.color.dot_distance,
            dot_angle_deg: b.color.dot_angle_deg,
            secondary_dot_pos: b.color.secondary_dot_pos,
            secondary_dot_angle_deg_delta: b.color.secondary_dot_angle_deg_delta,
            magic_theme: b.color.magic_theme,
            enable_color_boost: b.color.color_boost_enabled,
            smart_invert: b.color.smart_invert,
            contrast: b.color.contrast,
            brightness: b.color.brightness,
            saturation: b.color.saturation,
            font_family: b.typography.font_family.clone(),
            text_case_override: match b.typography.case_mode {
                CaseMode::None => "none",
                CaseMode::Upper => "uppercase",
                CaseMode::Lower => "lowercase",
                CaseMode::Capitalize => "capitalize",
            }
            .to_string(),
            size_override: b.typography.size_mode.as_zoom(),
            zap_selectors: b.zap_selectors.clone(),
            custom_css: b.custom_css.clone(),
            change_was_made: b.change_was_made,
        }
    }
}

impl TryFrom<(ZenCompatBoostData, String)> for Boost {
    type Error = String;

    fn try_from((dto, domain): (ZenCompatBoostData, String)) -> Result<Self, Self::Error> {
        let case_mode = match dto.text_case_override.as_str() {
            "none" => CaseMode::None,
            "uppercase" => CaseMode::Upper,
            "lowercase" => CaseMode::Lower,
            "capitalize" => CaseMode::Capitalize,
            other => return Err(format!("unknown textCaseOverride: {other}")),
        };

        Ok(Boost {
            id: BoostId::generate(),
            domain,
            name: dto.boost_name,
            color: ColorBoost {
                dot_pos: dto.dot_pos,
                dot_distance: dto.dot_distance,
                dot_angle_deg: dto.dot_angle_deg,
                secondary_dot_pos: dto.secondary_dot_pos,
                secondary_dot_angle_deg_delta: dto.secondary_dot_angle_deg_delta,
                magic_theme: dto.magic_theme,
                color_boost_enabled: dto.enable_color_boost,
                smart_invert: dto.smart_invert,
                contrast: dto.contrast,
                brightness: dto.brightness,
                saturation: dto.saturation,
            },
            typography: TypographyBoost {
                font_family: dto.font_family,
                case_mode,
                size_mode: SizeMode::from_zoom(dto.size_override),
            },
            zap_selectors: dto.zap_selectors,
            custom_css: dto.custom_css,
            change_was_made: dto.change_was_made,
            created_at: DateTime::now(),
            updated_at: DateTime::now(),
        })
    }
}

#[derive(Clone, Debug, Default, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ColorBoostUpdate {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub color_boost_enabled: Option<bool>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub dot_angle_deg: Option<f64>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub secondary_dot_angle_deg_delta: Option<f64>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub brightness: Option<f64>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub saturation: Option<f64>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub contrast: Option<f64>,
    #[serde(skip_serializing_if = "Option::is_none", alias = "autoTheme")]
    pub magic_theme: Option<bool>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub smart_invert: Option<bool>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub dot_pos: Option<Point>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub dot_distance: Option<f32>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub secondary_dot_pos: Option<Point>,
}

#[derive(Clone, Debug, Default, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct TypographyBoostUpdate {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub font_family: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub case_mode: Option<CaseMode>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub size_mode: Option<SizeMode>,
}

#[derive(Clone, Debug, Default, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct BoostUpdate {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub name: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub color: Option<ColorBoostUpdate>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub typography: Option<TypographyBoostUpdate>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub zap_selectors: Option<Vec<String>>,
    // TRUST BOUNDARY: Values may remain raw at rest; boost_css_sanitizer.rs enforces
    // the final compose_css injection boundary, while import_boost filters secondarily.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub custom_css: Option<String>,
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_zen_auto_theme_alias_deserializes() {
        let json = r#"{"autoTheme": true}"#;
        let update: ColorBoostUpdate = serde_json::from_str(json).unwrap();
        assert_eq!(update.magic_theme, Some(true));
    }
}
