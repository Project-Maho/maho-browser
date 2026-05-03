use serde::{Deserialize, Serialize};

use crate::common::DateTime;
use crate::folder::Folder;
use crate::identifiers::{ProfileId, SpaceId, TabId};

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ThemeColor {
    pub hue: f64,
    pub saturation: f64,
    pub brightness: f64,
    #[serde(default)]
    pub is_custom: bool,
    #[serde(default)]
    pub is_primary: bool,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum ColorHarmony {
    Complementary,
    SingleAnalogous,
    SplitComplementary,
    Analogous,
    Triadic,
    Custom,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(tag = "type", rename_all = "camelCase")]
pub enum SpaceTheme {
    #[serde(rename = "solid")]
    Solid {
        color: SpaceColor,
    },
    #[serde(rename = "gradient")]
    Gradient {
        #[serde(rename = "gradientColors")]
        colors: Vec<ThemeColor>,
        harmony: ColorHarmony,
        opacity: f64,
        texture: f64,
    },
}

impl SpaceTheme {
    pub fn base_color(&self) -> SpaceColor {
        match self {
            SpaceTheme::Solid { color } => color.clone(),
            SpaceTheme::Gradient { colors, .. } => {
                colors
                    .iter()
                    .find(|c| c.is_primary)
                    .or_else(|| colors.first())
                    .map(|c| SpaceColor {
                        hue: c.hue,
                        saturation: c.saturation,
                        brightness: c.brightness,
                        grain: 0.0,
                    })
                    .unwrap_or(SpaceColor {
                        hue: 0.58,
                        saturation: 0.8,
                        brightness: 0.9,
                        grain: 0.0,
                    })
            }
        }
    }

    pub fn from_legacy_color(color: &SpaceColor) -> Self {
        SpaceTheme::Solid {
            color: color.clone(),
        }
    }
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SpaceColor {
    pub hue: f64,
    pub saturation: f64,
    pub brightness: f64,
    #[serde(default)]
    pub grain: f64,
}

pub type SpaceIcon = String;

#[derive(Clone, Debug, Default, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub enum ActiveSpaceModel {
    #[default]
    Global,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ATCRule {
    pub id: String,
    pub condition: ATCCondition,
    pub action: ATCAction,
    pub enabled: bool,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum ATCAction {
    Close,
    Archive,
    Route { space_id: SpaceId },
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(tag = "kind", rename_all = "snake_case")]
pub enum ATCCondition {
    InactiveDuration { hours: f64 },
    TabCountExceeded { max_tabs: u32 },
    UrlPattern { pattern: String },
    UrlContains { text: String },
    UrlEquals { url: String },
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Space {
    pub id: SpaceId,
    #[serde(default)]
    pub profile_id: ProfileId,
    pub name: String,
    pub color: SpaceColor,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub theme: Option<SpaceTheme>,
    pub icon: Option<SpaceIcon>,
    pub tab_order: Vec<TabId>,
    pub folders: Vec<Folder>,
    pub atc_rules: Vec<ATCRule>,
    pub is_active: bool,
    pub created_at: DateTime,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub last_active_tab_id: Option<TabId>,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SpaceConfig {
    pub color: SpaceColor,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub theme: Option<SpaceTheme>,
    pub icon: Option<SpaceIcon>,
    pub name: String,
    pub profile_id: Option<ProfileId>,
}

#[derive(Clone, Debug, Default, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SpaceConfigUpdate {
    pub space_id: SpaceId,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub name: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub color: Option<SpaceColor>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub theme: Option<SpaceTheme>,
    /// `Some(Some("🌐"))` = set icon, `Some(None)` = clear icon, `None` = unchanged
    #[serde(skip_serializing_if = "Option::is_none")]
    pub icon: Option<Option<SpaceIcon>>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub profile_id: Option<ProfileId>,
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn space_theme_solid_roundtrip() {
        let theme = SpaceTheme::Solid {
            color: SpaceColor {
                hue: 0.58,
                saturation: 0.8,
                brightness: 0.9,
                grain: 0.2,
            },
        };
        let json = serde_json::to_string(&theme).unwrap();
        assert!(json.contains("\"type\":\"solid\""));
        let decoded: SpaceTheme = serde_json::from_str(&json).unwrap();
        match decoded {
            SpaceTheme::Solid { color } => {
                assert!((color.hue - 0.58).abs() < 0.001);
                assert!((color.grain - 0.2).abs() < 0.001);
            }
            _ => panic!("Expected Solid variant"),
        }
    }

    #[test]
    fn space_theme_gradient_roundtrip() {
        let theme = SpaceTheme::Gradient {
            colors: vec![
                ThemeColor { hue: 0.5, saturation: 0.8, brightness: 0.9, is_custom: false, is_primary: true },
                ThemeColor { hue: 0.0, saturation: 0.8, brightness: 0.9, is_custom: false, is_primary: false },
            ],
            harmony: ColorHarmony::Complementary,
            opacity: 0.85,
            texture: 0.3,
        };
        let json = serde_json::to_string(&theme).unwrap();
        assert!(json.contains("\"type\":\"gradient\""));
        assert!(json.contains("\"gradientColors\""));
        let decoded: SpaceTheme = serde_json::from_str(&json).unwrap();
        match decoded {
            SpaceTheme::Gradient { colors, harmony, opacity, texture } => {
                assert_eq!(colors.len(), 2);
                assert!(colors[0].is_primary);
                assert!(!colors[1].is_primary);
                assert!(matches!(harmony, ColorHarmony::Complementary));
                assert!((opacity - 0.85).abs() < 0.001);
                assert!((texture - 0.3).abs() < 0.001);
            }
            _ => panic!("Expected Gradient variant"),
        }
    }

    #[test]
    fn space_theme_base_color_solid() {
        let theme = SpaceTheme::Solid {
            color: SpaceColor { hue: 0.3, saturation: 0.5, brightness: 0.7, grain: 0.1 },
        };
        let base = theme.base_color();
        assert!((base.hue - 0.3).abs() < 0.001);
        assert!((base.grain - 0.1).abs() < 0.001);
    }

    #[test]
    fn space_theme_base_color_gradient_uses_primary() {
        let theme = SpaceTheme::Gradient {
            colors: vec![
                ThemeColor { hue: 0.1, saturation: 0.5, brightness: 0.9, is_custom: false, is_primary: false },
                ThemeColor { hue: 0.6, saturation: 0.8, brightness: 0.7, is_custom: false, is_primary: true },
            ],
            harmony: ColorHarmony::Analogous,
            opacity: 1.0,
            texture: 0.0,
        };
        let base = theme.base_color();
        assert!((base.hue - 0.6).abs() < 0.001);
    }

    #[test]
    fn space_with_theme_optional() {
        let space_json = r#"{"id":"s1","profileId":"p1","name":"Test","color":{"hue":0.5,"saturation":0.8,"brightness":0.9},"icon":null,"tabOrder":[],"folders":[],"atcRules":[],"isActive":true,"createdAt":"2024-01-01T00:00:00Z"}"#;
        let space: Space = serde_json::from_str(space_json).unwrap();
        assert!(space.theme.is_none());
    }

    #[test]
    fn color_harmony_roundtrip() {
        for harmony in [ColorHarmony::Complementary, ColorHarmony::SingleAnalogous, ColorHarmony::SplitComplementary, ColorHarmony::Analogous, ColorHarmony::Triadic, ColorHarmony::Custom] {
            let json = serde_json::to_string(&harmony).unwrap();
            let decoded: ColorHarmony = serde_json::from_str(&json).unwrap();
            assert_eq!(serde_json::to_string(&decoded).unwrap(), json);
        }
    }
}
