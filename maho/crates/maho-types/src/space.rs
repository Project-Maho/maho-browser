use serde::de::{self, MapAccess, Visitor};
use serde::{Deserialize, Deserializer, Serialize};

use crate::common::DateTime;
use crate::folder::Folder;
use crate::identifiers::{FolderId, ProfileId, SpaceId, TabId};

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq)]
pub struct ThemeColorPosition {
    pub x: f64,
    pub y: f64,
}

/// Color stop for gradient/Zen themes. Accepts flat HSB or Zen `c`-format on
/// the wire; see custom `Deserialize` impl for both representations.
#[derive(Clone, Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ThemeColor {
    pub hue: f64,
    pub saturation: f64,
    pub brightness: f64,
    #[serde(default)]
    pub is_custom: bool,
    #[serde(default)]
    pub is_primary: bool,
    /// Raw `c` from Zen-format stops (hex string or `[r,g,b]` triple).
    /// Re-emitted on serialization so round-trips are lossless.
    #[serde(rename = "c", default, skip_serializing_if = "Option::is_none")]
    pub c_raw: Option<CValue>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub position: Option<ThemeColorPosition>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub lightness: Option<f64>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub algorithm: Option<ColorHarmony>,
}

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq)]
#[serde(untagged)]
pub enum CValue {
    Hex(String),
    Rgb([u16; 3]),
}

impl<'de> Deserialize<'de> for ThemeColor {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: Deserializer<'de>,
    {
        struct ThemeColorVisitor;

        impl<'de> Visitor<'de> for ThemeColorVisitor {
            type Value = ThemeColor;

            fn expecting(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
                f.write_str("a ThemeColor object (flat HSB or Zen c-format)")
            }

            fn visit_map<A>(self, mut map: A) -> Result<Self::Value, A::Error>
            where
                A: MapAccess<'de>,
            {
                let mut hue: Option<f64> = None;
                let mut saturation: Option<f64> = None;
                let mut brightness: Option<f64> = None;
                let mut is_custom = false;
                let mut is_primary = false;
                let mut c_raw: Option<CValue> = None;
                let mut position: Option<ThemeColorPosition> = None;
                let mut lightness: Option<f64> = None;
                let mut algorithm: Option<ColorHarmony> = None;

                while let Some(key) = map.next_key::<String>()? {
                    match key.as_str() {
                        "hue" => hue = Some(map.next_value()?),
                        "saturation" => saturation = Some(map.next_value()?),
                        "brightness" => brightness = Some(map.next_value()?),
                        "isCustom" => is_custom = map.next_value()?,
                        "isPrimary" => is_primary = map.next_value()?,
                        "c" => {
                            let raw: serde_json::Value = map.next_value()?;
                            if let Some(s) = raw.as_str() {
                                if s.is_empty() {
                                    return Err(de::Error::custom("c string must not be empty"));
                                }
                                c_raw = Some(CValue::Hex(s.to_owned()));
                            } else if let Some(arr) = raw.as_array() {
                                if arr.len() != 3 {
                                    return Err(de::Error::custom(
                                        "c array must have exactly 3 elements",
                                    ));
                                }
                                let r = arr[0]
                                    .as_u64()
                                    .ok_or_else(|| de::Error::custom("c[0] must be a number"))?;
                                let g = arr[1]
                                    .as_u64()
                                    .ok_or_else(|| de::Error::custom("c[1] must be a number"))?;
                                let b = arr[2]
                                    .as_u64()
                                    .ok_or_else(|| de::Error::custom("c[2] must be a number"))?;
                                c_raw = Some(CValue::Rgb([r as u16, g as u16, b as u16]));
                            } else {
                                return Err(de::Error::custom(
                                    "c must be a string or 3-element numeric array",
                                ));
                            }
                        }
                        "position" => position = map.next_value()?,
                        "lightness" => lightness = map.next_value()?,
                        "algorithm" => algorithm = map.next_value()?,
                        _ => {
                            let _ = map.next_value::<serde_json::Value>()?;
                        }
                    }
                }

                let has_hsb = hue.is_some() && saturation.is_some() && brightness.is_some();
                let has_c = c_raw.is_some();

                if !has_hsb && !has_c {
                    return Err(de::Error::custom(
                        "ThemeColor requires either hue/saturation/brightness or a c field",
                    ));
                }

                Ok(ThemeColor {
                    hue: hue.unwrap_or(0.0),
                    saturation: saturation.unwrap_or(0.0),
                    brightness: brightness.unwrap_or(0.0),
                    is_custom,
                    is_primary,
                    c_raw,
                    position,
                    lightness,
                    algorithm,
                })
            }
        }

        deserializer.deserialize_map(ThemeColorVisitor)
    }
}

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum ThemeScheme {
    Auto,
    Light,
    Dark,
}

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum ColorHarmony {
    Complementary,
    SingleAnalogous,
    SplitComplementary,
    Analogous,
    Triadic,
    Custom,
    Floating,
}

fn default_opacity() -> f64 {
    1.0
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(tag = "type", rename_all = "camelCase")]
pub enum SpaceTheme {
    #[serde(rename = "solid")]
    Solid {
        color: SpaceColor,
        #[serde(default = "default_opacity")]
        opacity: f64,
        #[serde(rename = "schemaVersion", default, skip_serializing_if = "is_zero_u32")]
        schema_version: u32,
    },
    #[serde(rename = "gradient")]
    Gradient {
        #[serde(rename = "gradientColors")]
        colors: Vec<ThemeColor>,
        harmony: ColorHarmony,
        #[serde(default, skip_serializing_if = "Option::is_none")]
        scheme: Option<ThemeScheme>,
        #[serde(default = "default_opacity")]
        opacity: f64,
        #[serde(default)]
        texture: f64,
        #[serde(rename = "schemaVersion", default, skip_serializing_if = "is_zero_u32")]
        schema_version: u32,
    },
    #[serde(rename = "zen")]
    Zen {
        #[serde(rename = "gradientColors")]
        colors: Vec<ThemeColor>,
        #[serde(default, skip_serializing_if = "Option::is_none")]
        harmony: Option<ColorHarmony>,
        #[serde(default, skip_serializing_if = "Option::is_none")]
        scheme: Option<ThemeScheme>,
        #[serde(default = "default_opacity")]
        opacity: f64,
        #[serde(default)]
        texture: f64,
        #[serde(rename = "schemaVersion", default, skip_serializing_if = "is_zero_u32")]
        schema_version: u32,
    },
}

impl SpaceTheme {
    pub fn validate(&self) -> bool {
        match self {
            SpaceTheme::Solid { .. } => true,
            SpaceTheme::Gradient {
                harmony, scheme, ..
            } => {
                if matches!(harmony, ColorHarmony::Floating) && scheme.is_none() {
                    return false;
                }
                true
            }
            SpaceTheme::Zen {
                harmony, scheme, ..
            } => {
                if let Some(ColorHarmony::Floating) = harmony {
                    if scheme.is_none() {
                        return false;
                    }
                }
                true
            }
        }
    }
}

impl SpaceTheme {
    pub fn base_color(&self) -> SpaceColor {
        match self {
            SpaceTheme::Solid { color, .. } => color.clone(),
            SpaceTheme::Gradient { colors, .. } | SpaceTheme::Zen { colors, .. } => colors
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
                }),
        }
    }

    pub fn from_legacy_color(color: &SpaceColor) -> Self {
        SpaceTheme::Solid {
            color: color.clone(),
            opacity: default_opacity(),
            schema_version: 0,
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

/// Identifies a root-level sidebar item (tab or folder) for unified ordering.
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(tag = "kind", content = "id", rename_all = "snake_case")]
pub enum RootItem {
    Tab(TabId),
    Folder(FolderId),
}

/// Specifies where to insert a root item in the unified root order.
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(tag = "kind", rename_all = "snake_case")]
pub enum RootInsertionPoint {
    /// Insert before the specified root item (tab or folder).
    Before { target: RootItem },
    /// Append after the last root item.
    Append,
}

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

fn is_zero_u32(v: &u32) -> bool {
    *v == 0
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
    #[serde(default)]
    pub root_order: Vec<RootItem>,
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
            opacity: 1.0,
            schema_version: 0,
        };
        let json = serde_json::to_string(&theme).unwrap();
        assert!(json.contains("\"type\":\"solid\""));
        let decoded: SpaceTheme = serde_json::from_str(&json).unwrap();
        match decoded {
            SpaceTheme::Solid { color, .. } => {
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
                ThemeColor {
                    hue: 0.5,
                    saturation: 0.8,
                    brightness: 0.9,
                    is_custom: false,
                    is_primary: true,
                    c_raw: None,
                    position: None,
                    lightness: None,
                    algorithm: None,
                },
                ThemeColor {
                    hue: 0.0,
                    saturation: 0.8,
                    brightness: 0.9,
                    is_custom: false,
                    is_primary: false,
                    c_raw: None,
                    position: None,
                    lightness: None,
                    algorithm: None,
                },
            ],
            harmony: ColorHarmony::Complementary,
            scheme: None,
            opacity: 0.85,
            texture: 0.3,
            schema_version: 0,
        };
        let json = serde_json::to_string(&theme).unwrap();
        assert!(json.contains("\"type\":\"gradient\""));
        assert!(json.contains("\"gradientColors\""));
        let decoded: SpaceTheme = serde_json::from_str(&json).unwrap();
        match decoded {
            SpaceTheme::Gradient {
                colors,
                harmony,
                opacity,
                texture,
                ..
            } => {
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
            color: SpaceColor {
                hue: 0.3,
                saturation: 0.5,
                brightness: 0.7,
                grain: 0.1,
            },
            opacity: 1.0,
            schema_version: 0,
        };
        let base = theme.base_color();
        assert!((base.hue - 0.3).abs() < 0.001);
        assert!((base.grain - 0.1).abs() < 0.001);
    }

    #[test]
    fn space_theme_base_color_gradient_uses_primary() {
        let theme = SpaceTheme::Gradient {
            colors: vec![
                ThemeColor {
                    hue: 0.1,
                    saturation: 0.5,
                    brightness: 0.9,
                    is_custom: false,
                    is_primary: false,
                    c_raw: None,
                    position: None,
                    lightness: None,
                    algorithm: None,
                },
                ThemeColor {
                    hue: 0.6,
                    saturation: 0.8,
                    brightness: 0.7,
                    is_custom: false,
                    is_primary: true,
                    c_raw: None,
                    position: None,
                    lightness: None,
                    algorithm: None,
                },
            ],
            harmony: ColorHarmony::Analogous,
            scheme: None,
            opacity: 1.0,
            texture: 0.0,
            schema_version: 0,
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
        for harmony in [
            ColorHarmony::Complementary,
            ColorHarmony::SingleAnalogous,
            ColorHarmony::SplitComplementary,
            ColorHarmony::Analogous,
            ColorHarmony::Triadic,
            ColorHarmony::Custom,
        ] {
            let json = serde_json::to_string(&harmony).unwrap();
            let decoded: ColorHarmony = serde_json::from_str(&json).unwrap();
            assert_eq!(serde_json::to_string(&decoded).unwrap(), json);
        }
    }

    #[test]
    fn space_theme_zen_roundtrip() {
        let theme = SpaceTheme::Zen {
            colors: vec![ThemeColor {
                hue: 0.0,
                saturation: 0.0,
                brightness: 0.0,
                is_custom: false,
                is_primary: true,
                c_raw: Some(CValue::Rgb([74, 144, 217])),
                position: None,
                lightness: None,
                algorithm: None,
            }],
            harmony: None,
            scheme: None,
            opacity: 1.0,
            texture: 0.2,
            schema_version: 0,
        };
        let json = serde_json::to_string(&theme).unwrap();
        assert!(
            json.contains("\"type\":\"zen\""),
            "serialized zen type: {json}"
        );
        assert!(json.contains("\"gradientColors\""));
        let decoded: SpaceTheme = serde_json::from_str(&json).unwrap();
        match decoded {
            SpaceTheme::Zen {
                colors,
                harmony,
                opacity,
                texture,
                ..
            } => {
                assert_eq!(colors.len(), 1);
                assert!(colors[0].is_primary);
                assert!(harmony.is_none());
                assert_eq!(colors[0].c_raw, Some(CValue::Rgb([74, 144, 217])));
                assert!((opacity - 1.0).abs() < 0.001);
                assert!((texture - 0.2).abs() < 0.001);
            }
            _ => panic!("Expected Zen variant"),
        }
    }

    #[test]
    fn space_theme_zen_with_harmony_roundtrip() {
        let theme = SpaceTheme::Zen {
            colors: vec![ThemeColor {
                hue: 0.0,
                saturation: 0.0,
                brightness: 0.0,
                is_custom: true,
                is_primary: true,
                c_raw: Some(CValue::Hex("#ff6b35".to_string())),
                position: None,
                lightness: None,
                algorithm: None,
            }],
            harmony: Some(ColorHarmony::Analogous),
            scheme: None,
            opacity: 0.9,
            texture: 0.1,
            schema_version: 0,
        };
        let json = serde_json::to_string(&theme).unwrap();
        assert!(json.contains("\"type\":\"zen\""));
        assert!(json.contains("\"harmony\":\"analogous\""));
        let decoded: SpaceTheme = serde_json::from_str(&json).unwrap();
        match decoded {
            SpaceTheme::Zen { harmony, .. } => {
                assert!(matches!(harmony, Some(ColorHarmony::Analogous)));
            }
            _ => panic!("Expected Zen variant"),
        }
    }

    #[test]
    fn space_theme_zen_base_color_uses_hsb_when_available() {
        let theme = SpaceTheme::Zen {
            colors: vec![
                ThemeColor {
                    hue: 0.2,
                    saturation: 0.6,
                    brightness: 0.8,
                    is_custom: false,
                    is_primary: false,
                    c_raw: None,
                    position: None,
                    lightness: None,
                    algorithm: None,
                },
                ThemeColor {
                    hue: 0.7,
                    saturation: 0.9,
                    brightness: 0.5,
                    is_custom: false,
                    is_primary: true,
                    c_raw: None,
                    position: None,
                    lightness: None,
                    algorithm: None,
                },
            ],
            harmony: None,
            scheme: None,
            opacity: 1.0,
            texture: 0.0,
            schema_version: 0,
        };
        let base = theme.base_color();
        assert!((base.hue - 0.7).abs() < 0.001);
        assert!((base.saturation - 0.9).abs() < 0.001);
    }

    #[test]
    fn theme_color_c_format_rgb_roundtrip() {
        let json = r#"{"c":[74,144,217],"isPrimary":true,"isCustom":false}"#;
        let tc: ThemeColor = serde_json::from_str(json).unwrap();
        assert_eq!(tc.c_raw, Some(CValue::Rgb([74, 144, 217])));
        assert!(tc.is_primary);
        assert!(!tc.is_custom);
        let re = serde_json::to_string(&tc).unwrap();
        assert!(re.contains("\"c\":[74,144,217]"), "re-serialized: {re}");
    }

    #[test]
    fn theme_color_c_format_hex_roundtrip() {
        let json = r##"{"c":"#ff6b35","isPrimary":false,"isCustom":true}"##;
        let tc: ThemeColor = serde_json::from_str(json).unwrap();
        assert_eq!(tc.c_raw, Some(CValue::Hex("#ff6b35".to_string())));
        assert!(!tc.is_primary);
        assert!(tc.is_custom);
        let re = serde_json::to_string(&tc).unwrap();
        assert!(re.contains(r##""c":"#ff6b35""##), "re-serialized: {re}");
    }

    #[test]
    fn theme_color_flat_hsb_roundtrip() {
        let json =
            r#"{"hue":220.0,"saturation":0.7,"brightness":0.85,"isPrimary":true,"isCustom":false}"#;
        let tc: ThemeColor = serde_json::from_str(json).unwrap();
        assert!((tc.hue - 220.0).abs() < 0.001);
        assert!(tc.c_raw.is_none());
        let re = serde_json::to_string(&tc).unwrap();
        assert!(!re.contains("\"c\""), "should not emit c field: {re}");
    }

    #[test]
    fn theme_color_c_format_empty_string_rejected() {
        let json = r#"{"c":"","isPrimary":true,"isCustom":false}"#;
        assert!(serde_json::from_str::<ThemeColor>(json).is_err());
    }

    #[test]
    fn theme_color_c_format_wrong_length_rejected() {
        let json = r#"{"c":[74,144],"isPrimary":true,"isCustom":false}"#;
        assert!(serde_json::from_str::<ThemeColor>(json).is_err());
    }

    #[test]
    fn theme_color_missing_color_rejected() {
        let json = r#"{"isPrimary":true,"isCustom":false}"#;
        assert!(serde_json::from_str::<ThemeColor>(json).is_err());
    }

    #[test]
    fn space_theme_schema_version_defaults_to_zero() {
        let json =
            r#"{"type":"solid","color":{"hue":0.5,"saturation":0.8,"brightness":0.9,"grain":0.1}}"#;
        let theme: SpaceTheme = serde_json::from_str(json).unwrap();
        match theme {
            SpaceTheme::Solid { schema_version, .. } => assert_eq!(schema_version, 0),
            _ => panic!("Expected Solid variant"),
        }
    }

    #[test]
    fn space_theme_schema_version_roundtrip() {
        let json = r#"{"type":"solid","color":{"hue":0.5,"saturation":0.8,"brightness":0.9,"grain":0.1},"schemaVersion":2}"#;
        let theme: SpaceTheme = serde_json::from_str(json).unwrap();
        match theme {
            SpaceTheme::Solid { schema_version, .. } => {
                assert_eq!(schema_version, 2);
                let re = serde_json::to_string(&SpaceTheme::Solid {
                    color: SpaceColor {
                        hue: 0.5,
                        saturation: 0.8,
                        brightness: 0.9,
                        grain: 0.1,
                    },
                    opacity: 1.0,
                    schema_version: 2,
                })
                .unwrap();
                assert!(re.contains("\"schemaVersion\":2"), "re-serialized: {re}");
            }
            _ => panic!("Expected Solid variant"),
        }
    }

    #[test]
    fn space_theme_schema_version_zero_not_serialized() {
        let theme = SpaceTheme::Solid {
            color: SpaceColor {
                hue: 0.5,
                saturation: 0.8,
                brightness: 0.9,
                grain: 0.1,
            },
            opacity: 1.0,
            schema_version: 0,
        };
        let re = serde_json::to_string(&theme).unwrap();
        assert!(
            !re.contains("schemaVersion"),
            "zero schema_version must be omitted: {re}"
        );
    }
}

/// Per-space AI configuration (model preferences, system prompt, focus areas).
#[derive(Clone, Debug, Default, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SpaceAIConfig {
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub system_prompt: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub tone: Option<String>,
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub focus_areas: Vec<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub preferred_model: Option<String>,
}
