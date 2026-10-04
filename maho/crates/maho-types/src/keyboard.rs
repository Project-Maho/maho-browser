use serde::{Deserialize, Serialize};

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq, Hash)]
#[serde(rename_all = "snake_case")]
pub enum ShortcutCategory {
    Navigation,
    Tabs,
    Spaces,
    Window,
    Edit,
    View,
    Developer,
    Custom,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ShortcutBinding {
    pub action: String,
    pub label: String,
    pub category: ShortcutCategory,
    pub key_combo: KeyCombo,
    #[serde(default)]
    pub default_key_combo: KeyCombo,
    pub is_custom: bool,
    pub enabled: bool,
    #[serde(default)]
    pub updated_at: u64,
}

#[derive(Clone, Debug, Default, Serialize, Deserialize, PartialEq, Eq, Hash)]
#[serde(rename_all = "camelCase")]
pub struct KeyCombo {
    pub key: String,
    pub modifiers: Vec<KeyModifier>,
}

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq, Hash)]
#[serde(rename_all = "lowercase")]
pub enum KeyModifier {
    Ctrl,
    Shift,
    Alt,
    Meta,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ShortcutConflict {
    pub key_combo: KeyCombo,
    pub existing_action: String,
    pub new_action: String,
}

impl KeyCombo {
    pub fn new(key: &str, modifiers: Vec<KeyModifier>) -> Self {
        Self {
            key: key.to_string(),
            modifiers,
        }
    }

    // Normalized "alt+meta+t" format for conflict detection
    pub fn to_normalized_string(&self) -> String {
        let mut mods: Vec<String> = self
            .modifiers
            .iter()
            .map(|m| format!("{m:?}").to_lowercase())
            .collect();
        mods.sort();
        format!("{}+{}", mods.join("+"), self.key.to_lowercase())
    }
}

impl ShortcutBinding {
    pub fn new(action: &str, label: &str, category: ShortcutCategory, key_combo: KeyCombo) -> Self {
        Self {
            action: action.to_string(),
            label: label.to_string(),
            category,
            default_key_combo: key_combo.clone(),
            key_combo,
            is_custom: false,
            enabled: true,
            updated_at: 0,
        }
    }
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub enum SetShortcutError {
    Conflict(ShortcutConflict),
    Reserved,
    Invalid,
}
