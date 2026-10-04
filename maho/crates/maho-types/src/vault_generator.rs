use std::fmt;

use serde::{Deserialize, Serialize};

pub const MIN_PASSWORD_LENGTH: u32 = 8;
pub const MAX_PASSWORD_LENGTH: u32 = 128;
pub const DEFAULT_PASSWORD_LENGTH: u32 = 20;

pub const MIN_PASSPHRASE_WORDS: u32 = 3;
pub const MAX_PASSPHRASE_WORDS: u32 = 12;
pub const DEFAULT_PASSPHRASE_WORDS: u32 = 5;
pub const DEFAULT_PASSPHRASE_SEPARATOR: &str = "-";

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum PasswordGeneratorMode {
    Password,
    Passphrase,
}

impl Default for PasswordGeneratorMode {
    fn default() -> Self {
        Self::Password
    }
}

fn default_mode() -> PasswordGeneratorMode {
    PasswordGeneratorMode::Password
}

fn default_length() -> u32 {
    DEFAULT_PASSWORD_LENGTH
}

fn default_true() -> bool {
    true
}

fn default_false() -> bool {
    false
}

fn default_word_count() -> u32 {
    DEFAULT_PASSPHRASE_WORDS
}

fn default_separator() -> String {
    DEFAULT_PASSPHRASE_SEPARATOR.to_string()
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct PasswordGeneratorOptions {
    #[serde(default = "default_mode")]
    pub mode: PasswordGeneratorMode,

    #[serde(default = "default_length")]
    pub length: u32,
    #[serde(default = "default_true")]
    pub include_lowercase: bool,
    #[serde(default = "default_true")]
    pub include_uppercase: bool,
    #[serde(default = "default_true")]
    pub include_digits: bool,
    #[serde(default = "default_true")]
    pub include_symbols: bool,
    #[serde(default = "default_false")]
    pub avoid_ambiguous: bool,

    #[serde(default = "default_word_count")]
    pub word_count: u32,
    #[serde(default = "default_separator")]
    pub separator: String,
    #[serde(default = "default_true")]
    pub capitalize: bool,
    #[serde(default = "default_true")]
    pub include_number: bool,
}

impl Default for PasswordGeneratorOptions {
    fn default() -> Self {
        Self {
            mode: default_mode(),
            length: default_length(),
            include_lowercase: true,
            include_uppercase: true,
            include_digits: true,
            include_symbols: true,
            avoid_ambiguous: false,
            word_count: default_word_count(),
            separator: default_separator(),
            capitalize: true,
            include_number: true,
        }
    }
}

impl PasswordGeneratorOptions {
    pub fn new_password(length: u32) -> Self {
        Self {
            mode: PasswordGeneratorMode::Password,
            length,
            ..Default::default()
        }
    }

    pub fn new_passphrase(word_count: u32) -> Self {
        Self {
            mode: PasswordGeneratorMode::Passphrase,
            word_count,
            ..Default::default()
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash, Serialize, Deserialize)]
#[repr(u8)]
pub enum PasswordStrengthScore {
    VeryWeak = 0,
    Weak = 1,
    Fair = 2,
    Good = 3,
    Strong = 4,
}

impl PasswordStrengthScore {
    pub fn from_u8(value: u8) -> Self {
        match value {
            0 => Self::VeryWeak,
            1 => Self::Weak,
            2 => Self::Fair,
            3 => Self::Good,
            _ => Self::Strong,
        }
    }

    pub fn as_u8(&self) -> u8 {
        *self as u8
    }

    pub fn is_weak(&self) -> bool {
        self.as_u8() <= 1
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct PasswordStrength {
    pub score: u8,
    pub entropy_bits: f64,
}

impl PasswordStrength {
    pub fn new(score: u8, entropy_bits: f64) -> Self {
        Self {
            score: score.min(4),
            entropy_bits,
        }
    }

    pub fn score_enum(&self) -> PasswordStrengthScore {
        PasswordStrengthScore::from_u8(self.score)
    }

    pub fn is_weak(&self) -> bool {
        self.score <= 1
    }
}

#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct GeneratedPasswordResult {
    pub success: bool,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub password: Option<String>,
    pub strength_score: u8,
    pub entropy_bits: f64,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub error: Option<String>,
}

impl GeneratedPasswordResult {
    pub fn success(password: String, strength: PasswordStrength) -> Self {
        Self {
            success: true,
            password: Some(password),
            strength_score: strength.score,
            entropy_bits: strength.entropy_bits,
            error: None,
        }
    }

    pub fn failure(err: impl fmt::Display) -> Self {
        Self {
            success: false,
            password: None,
            strength_score: 0,
            entropy_bits: 0.0,
            error: Some(err.to_string()),
        }
    }
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub enum VaultGeneratorError {
    InvalidLength { length: u32, min: u32, max: u32 },
    NoCharacterClassesEnabled,
    InvalidWordCount { count: u32, min: u32, max: u32 },
    RngFailure(String),
}

impl fmt::Display for VaultGeneratorError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::InvalidLength { length, min, max } => {
                write!(
                    f,
                    "password length {length} out of bounds: must be between {min} and {max}"
                )
            }
            Self::NoCharacterClassesEnabled => {
                write!(f, "no character classes enabled for password generation")
            }
            Self::InvalidWordCount { count, min, max } => {
                write!(
                    f,
                    "passphrase word count {count} out of bounds: must be between {min} and {max}"
                )
            }
            Self::RngFailure(reason) => {
                write!(f, "random number generation failed: {reason}")
            }
        }
    }
}

impl std::error::Error for VaultGeneratorError {}
